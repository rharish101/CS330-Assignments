#include "lib.h"
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#define cfree(x, size) munmap((x), (size))  // Free pointer alloc. by cmalloc

// Constants
#define KEY_SIZE 32
#define MAX_OBJS 1000000
#define OBJ_BMP_LN ((MAX_OBJS / 64) + 1)
#define BLK_BMP_LN ((32 * 1024 * 1024l) / (4 * 64))
#define MAX_SIZE (BLOCK_SIZE * (4 + 4 * 1024))
#define BLK_BMP_BLOCKS 256
#define OBJ_PER_BLOCK 50
#define OBJ_BLOCKS 20000
#define RES_BLOCKS (BLK_BMP_BLOCKS + OBJ_BLOCKS)
#define CACHE_LEN (CACHE_SIZE / BLOCK_SIZE)

// Inode structure
typedef struct object
{
    uint64_t size;        // Size
    char key[KEY_SIZE];   // Key
    uint32_t direct[4];   // Direct pointers
    uint32_t indirect[4]; // Indirect pointers (level one)
} Obj, *pObj;

// To save in objfs->objstore_data
typedef struct pointers
{
    // Being saved to disk
    Obj objects[MAX_OBJS];        // List of objects/inodes
    uint64_t obj_bmp[OBJ_BMP_LN]; // Object bitmap for dirty bits
    uint64_t blk_bmp[BLK_BMP_LN]; // Block bitmap

    // Only in memory
    pthread_mutex_t obj_cl_lk;      // Mutex for preventing object clashes
    pthread_mutex_t obj_bmp_lk;     // Mutex for object bitmap
    pthread_mutex_t blk_bmp_lk;     // Mutex for block bitmap
    pthread_mutex_t ch_lock;        // Mutex for cache
    pthread_mutex_t rd_lock;        // Mutex for read
    uint32_t cache_info[CACHE_LEN]; // Cache info
} ObjData, *pObjData;

//
// ========================= SELF-PURPOSE FUNCTIONS =========================
//

/*
   Get a dynamically allocated generic pointer of given size.

   Return value: Success --> pointer to allocated size
   Failure --> NULL
   */
void *cmalloc(const uint64_t size)
{
    void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED)
        return NULL;
    else
        return ptr;
}

#ifdef CACHE  // CACHED implementation
/*
   Flushes a block a/c to round robin in the cache to the disk.
   NOTE: No locks

   Return value: Success --> 0
   Failure --> -1
   */
int flush_ch_blk(struct objfs_state *objfs, uint32_t index)
{
    pObjData objstore_data = (pObjData) objfs->objstore_data;
    uint32_t *cache = objstore_data->cache_info;
    if (write_block(objfs, cache[index], objfs->cache + BLOCK_SIZE * index) < 0)
        return -1;
    cache[index] = 0;
    return 0;
}
#endif

/*
   Fills the block data of the given block number in the given pointer from cache
   If not in cache, then reads the given block number, adds it to cache, and then proceeds

   Return value: Success --> 0
   Failure --> -1
   */
int read_ch_blk(struct objfs_state *objfs, const uint32_t block_num, char *buf)
{
#ifdef CACHE  // CACHED implementation
    pObjData objstore_data = (pObjData) objfs->objstore_data;
    uint32_t *cache = objstore_data->cache_info;
    long index = block_num % CACHE_LEN;

    pthread_mutex_lock(&(objstore_data->ch_lock));
    if (cache[index] != block_num)  // Read block from disk to cache
    {
        if (flush_ch_blk(objfs, index) < 0)
        {
            pthread_mutex_unlock(&(objstore_data->ch_lock));
            return -1;
        }
        cache[index] = block_num;
        if (read_block(objfs, block_num, objfs->cache + BLOCK_SIZE * index) < 0)
        {
            pthread_mutex_unlock(&(objstore_data->ch_lock));
            return -1;
        }
    }

    memcpy(buf, objfs->cache + BLOCK_SIZE * index, BLOCK_SIZE);
    pthread_mutex_unlock(&(objstore_data->ch_lock));
    return 0;
#else
    return read_block(objfs, block_num, buf);
#endif
}

/*
   Fills the block data of the given block number in the cache from the given pointer

   Return value: Success --> 0
   Failure --> -1
   */
int write_ch_blk(struct objfs_state *objfs, const uint32_t block_num, char *buf)
{
#ifdef CACHE  // CACHED implementation
    pObjData objstore_data = (pObjData) objfs->objstore_data;
    uint32_t *cache = objstore_data->cache_info;
    long index = block_num % CACHE_LEN;

    pthread_mutex_lock(&(objstore_data->ch_lock));
    if (cache[index] != block_num)
    {
        if (flush_ch_blk(objfs, index) < 0)
        {
            pthread_mutex_unlock(&(objstore_data->ch_lock));
            return -1;
        }
        cache[index] = block_num;
    }

    memcpy(objfs->cache + BLOCK_SIZE * index, buf, BLOCK_SIZE);
    pthread_mutex_unlock(&(objstore_data->ch_lock));
    return 0;
#else
    return write_block(objfs, block_num, buf);
#endif
}

/*
   Gets a free block that is marked as available in the block bitmap.
   Marks the obtained block as in use.

   Return value: Success --> block number of the available block
   Failure --> -1
   */
uint32_t alloc_block(struct objfs_state *objfs)
{
    pObjData objstore_data = (pObjData) objfs->objstore_data;
    uint64_t *bitmap = objstore_data->blk_bmp;
    for (uint32_t i = ((RES_BLOCKS / 64) - 1); i < BLK_BMP_LN; i++)
    {
        uint64_t temp = bitmap[i];
        for (int j = 0; j < 64; j++)
        {
            pthread_mutex_lock(&(objstore_data->blk_bmp_lk));
            if (!(temp & 0x1))  // Unallocated block (bit = 0) found
            {
                bitmap[i] |= 0x1l << j;  // Mark that bit 1
                pthread_mutex_unlock(&(objstore_data->blk_bmp_lk));
                return 64 * i + j;
            }
            pthread_mutex_unlock(&(objstore_data->blk_bmp_lk));
            temp >>= 1;
        }
    }
    return -1;
}

/*
   Frees the block by marking it as available in the block bitmap

   Return value: Success --> 0
   Failure --> -1
   */
int free_block(struct objfs_state *objfs, const uint32_t block_num)
{
    pObjData objstore_data = (pObjData) objfs->objstore_data;
    uint64_t *bitmap = objstore_data->blk_bmp;
    uint32_t index = block_num / 64;
    uint32_t bit = block_num % 64;

    pthread_mutex_lock(&(objstore_data->blk_bmp_lk));
    bitmap[index] &= ~(0x1l << bit);
    pthread_mutex_unlock(&(objstore_data->blk_bmp_lk));
    return 0;
}

/*
   Frees all pointers in a data block, and then frees the block itself

   Return value: Success --> 0
   Failure --> -1
   */
int free_pointer_block(struct objfs_state *objfs, const uint32_t block_num)
{
    char *buf = (char*) cmalloc(BLOCK_SIZE);
    if (!buf)
        return -1;
    if (read_ch_blk(objfs, block_num, buf) < 0)
        return -1;

    uint32_t *block_data = (uint32_t*) buf;
    for (uint32_t i = 0; i < (BLOCK_SIZE / 4); i++)
        if (block_data[i])  // Free only allocated block
            if (free_block(objfs, block_data[i]))
                return -1;
    if (free_block(objfs, block_num))
        return -1;

    cfree(buf, BLOCK_SIZE);  // Free the pointer block
    return 0;
}

/*
   Marks the given object as dirty in the object bitmap
   NOTE: Has locks

   Return value: Success --> 0
   Failure --> -1
   */
int mark_obj_dirty(struct objfs_state *objfs, const uint32_t objid)
{
    pObjData objstore_data = (pObjData) objfs->objstore_data;
    uint64_t *bitmap = objstore_data->obj_bmp;
    pthread_mutex_lock(&(objstore_data->obj_bmp_lk));
    bitmap[(objid - 2) / 64] |= 0x1l << ((objid - 2) % 64);
    pthread_mutex_unlock(&(objstore_data->obj_bmp_lk));
    return 0;
}

/*
   Returns the object pointer for the given key.

   Return value: Success --> object pointer of the given key
   Failure --> NULL
   */
pObj find_object(struct objfs_state *objfs, const char *key)
{
    pObjData objstore_data = (pObjData) objfs->objstore_data;
    pObj start = objstore_data->objects;
    for (pObj object = start; object != (start + MAX_OBJS); object++)
        if (!strcmp(object->key, key))
            return object;
    return NULL;
}

//
// ========================= REQUIRED FUNCTIONS =========================
//

/* Returns the object ID.  -1 (invalid), 0, 1 - reserved */
long find_object_id(const char *key, struct objfs_state *objfs)
{
    pObj object = find_object(objfs, key);
    pObjData objstore_data = (pObjData) objfs->objstore_data;
    if (object)
        return object - objstore_data->objects + 2;
    else
        return -1;
}

/*
   Creates a new object with obj.key=key. Object ID must be >=2.
   Must check for duplicates.

   Return value: Success --> object ID of the newly created object
   Failure --> -1
   */
long create_object(const char *key, struct objfs_state *objfs)
{
    pObjData objstore_data = (pObjData) objfs->objstore_data;
    pObj start = objstore_data->objects;
    pthread_mutex_lock(&(objstore_data->obj_cl_lk));
    int free = -1;
    for (int i = 0; i < MAX_OBJS; i++)
        if ((free < 0) && (start[i].size == 0))
            free = i;
        else if (!strcmp(start[i].key, key))
        {
            pthread_mutex_unlock(&(objstore_data->obj_cl_lk));
            return -1;
        }

    if (free < 0)
    {
        pthread_mutex_unlock(&(objstore_data->obj_cl_lk));
        return -1;
    }
    strcpy(start[free].key, key);
    start[free].size = 1;
    pthread_mutex_unlock(&(objstore_data->obj_cl_lk));
    mark_obj_dirty(objfs, free + 2);
    return free + 2;
}

/*
   One of the users of the object has dropped a reference
   Can be useful to implement caching.

   Return value: Success --> 0
   Failure --> -1
   */
long release_object(int objid, struct objfs_state *objfs)
{
    return 0;
}

/*
   Destroys an object with obj.key=key. Object ID is ensured to be >=2.

   Return value: Success --> 0
   Failure --> -1
   */
long destroy_object(const char *key, struct objfs_state *objfs)
{
    pObjData objstore_data = (pObjData) objfs->objstore_data;
    pObj start = objstore_data->objects;
    pObj object = find_object(objfs, key);
    if (!object)
        return -1;
    else if (!(object->size))
        return -1;

    // Free direct pointers
    for (int i = 0; i < 4; i++)
        if ((object->direct)[i])
        {
            if (free_block(objfs, (object->direct)[i]))
                return -1;
            (object->direct)[i] = 0;
        }

    // Free indirect pointers
    for (int i = 0; i < 4; i++)
        if ((object->indirect)[i])
        {
            if (free_pointer_block(objfs, (object->indirect)[i]))
                return -1;
            (object->indirect)[i] = 0;
        }

    object->size = 0;
    for (int i = 0; i < KEY_SIZE; i++)
        (object->key)[i] = '\0';
    mark_obj_dirty(objfs, object - start + 2);
    return 0;
}

/*
   Renames a new object with obj.key=key. Object ID must be >=2.
   Must check for duplicates.

   Return value: Success --> object ID of the newly created object
   Failure --> -1
   */
long rename_object(const char *key, const char *newname, struct objfs_state *objfs)
{
    pObjData objstore_data = (pObjData) objfs->objstore_data;
    pObj start = objstore_data->objects;
    pthread_mutex_lock(&(objstore_data->obj_cl_lk));
    int obj_index = -1;
    for (int i = 0; i < MAX_OBJS; i++)
    {
        if (!strcmp(start[i].key, key))
            obj_index = i;
        if (!strcmp(start[i].key, newname))
        {
            pthread_mutex_unlock(&(objstore_data->obj_cl_lk));
            return -1;
        }
    }

    if (obj_index < 0)
    {
        pthread_mutex_unlock(&(objstore_data->obj_cl_lk));
        return -1;
    }
    else
    {
        long id = obj_index + 2;
        pObj object = start + obj_index;
        memcpy(object->key, newname, KEY_SIZE);
        pthread_mutex_unlock(&(objstore_data->obj_cl_lk));
        mark_obj_dirty(objfs, id);
        return id;
    }
}

/*
   Writes the content of the buffer into the object with objid = objid.

   Return value: Success --> #of bytes written
   Failure --> -1
   */
long objstore_write(int objid, const char *buf, int size, struct objfs_state *objfs, off_t offset)
{
    pObjData objstore_data = (pObjData) objfs->objstore_data;
    pObj object = objstore_data->objects + (objid - 2);

    // Check validity
    if (objid < 2)
        return -1;
    else if (size > MAX_SIZE)
        return -1;

    int remaining = size;
    char *temp = (char*) cmalloc(BLOCK_SIZE);
    uint32_t start_block = offset / BLOCK_SIZE;
    uint32_t shift = offset % BLOCK_SIZE;
    uint32_t blocks_done = 0;

    /*
       If bytes are left to write, then writes it to the block number pointed by the block pointer

       Return value: Success --> 0
       Failure --> -1
       */
    inline int update_block(uint32_t *block_ptr)
    {
        if (remaining > 0)
        {
            // Allocate block if not exists
            if (!(*block_ptr))
            {
                *block_ptr = alloc_block(objfs);
                if (*block_ptr < 0)
                    return -1;
            }

            // Read existing data so as not to overwrite
            if (shift > 0)
                if (read_ch_blk(objfs, *block_ptr, temp) < 0)
                    return -1;

            int cur_size = (remaining > BLOCK_SIZE) ? BLOCK_SIZE : remaining;
            memcpy(temp + shift, buf + (size - remaining), cur_size);
            if (write_ch_blk(objfs, *block_ptr, temp) < 0)
                return -1;
            remaining -= cur_size;
            shift = 0;
        }
        return 0;
    }

    // Use direct pointers
    for (int i = 0; i < 4; i++)
        if (blocks_done < start_block)
            blocks_done++;
        else
            update_block(object->direct + i);

    // Use indirect pointers
    for (int i = 0; i < 4; i++)
        if (remaining > 0)
        {
            // Buffer to hold block data
            char *block_buf;
            block_buf = (char*) cmalloc(BLOCK_SIZE);
            if (!block_buf)
                return -1;

            if (!((object->indirect)[i]))  // Allocate block if not exists
            {
                (object->indirect)[i] = alloc_block(objfs);
                if ((object->indirect)[i] < 0)
                    return -1;
                for (int j = 0; j < BLOCK_SIZE; j++)
                    block_buf[j] = 0;
            }
            else if (read_ch_blk(objfs, (object->indirect)[i], block_buf) < 0)  // Get block data pointers
                return -1;
            uint32_t *block_data = (uint32_t*) block_buf;

            // Write data to block
            for (uint32_t j = 0; j < (BLOCK_SIZE / 4); j++)
                if (blocks_done < start_block)
                    blocks_done++;
                else
                    update_block(block_data + j);

            // Write pointers to block
            if (write_ch_blk(objfs, (object->indirect)[i], block_buf) < 0)
                return -1;

            cfree(block_buf, BLOCK_SIZE);
        }

    object->size = offset + size;
    mark_obj_dirty(objfs, objid);
    cfree(temp, BLOCK_SIZE);
    return size;
}

/*
   Reads the content of the object onto the buffer with objid = objid.

   Return value: Success --> #of bytes read
   Failure --> -1
   */
long objstore_read(int objid, char *buf, int size, struct objfs_state *objfs, off_t offset)
{
    pObjData objstore_data = (pObjData) objfs->objstore_data;
    pObj object = objstore_data->objects + (objid - 2);
    pthread_mutex_lock(&(objstore_data->rd_lock));

    // Check validity
    if (objid < 2)
    {
        pthread_mutex_unlock(&(objstore_data->rd_lock));
        return -1;
    }
    else if (size > MAX_SIZE)
    {
        pthread_mutex_unlock(&(objstore_data->rd_lock));
        return -1;
    }

    int remaining = size;
    char *temp = (char*) cmalloc(BLOCK_SIZE);
    uint32_t start_block = offset / BLOCK_SIZE;
    uint32_t shift = offset % BLOCK_SIZE;
    uint32_t blocks_done = 0;

    /*
       If bytes are left to read, then reads it from the block number

       Return value: Success --> 0
       Failure --> -1
       */
    inline int get_block_data(uint32_t block_num)
    {
        if (remaining > 0)
        {
            int cur_size = (remaining > BLOCK_SIZE) ? BLOCK_SIZE : remaining;
            if (read_ch_blk(objfs, block_num, temp) < 0)
            {
                pthread_mutex_unlock(&(objstore_data->rd_lock));
                return -1;
            }
            memcpy(buf + (size - remaining), temp + shift, cur_size);
            remaining -= cur_size;
            shift = 0;
        }
        return 0;
    }

    // Use direct pointers
    for (int i = 0; i < 4; i++)
        if (blocks_done < start_block)
            blocks_done++;
        else if ((object->direct)[i])
            get_block_data((object->direct)[i]);
        else
            break;

    // Use indirect pointers
    for (int i = 0; i < 4; i++)
        if (((object->indirect)[i]) && (remaining > 0))
        {
            // Buffer to hold block data
            char *block_buf;
            block_buf = (char*) cmalloc(BLOCK_SIZE);
            if (!block_buf)
            {
                pthread_mutex_unlock(&(objstore_data->rd_lock));
                return -1;
            }

            // Get block data pointers
            if (read_ch_blk(objfs, (object->indirect)[i], block_buf) < 0)
            {
                pthread_mutex_unlock(&(objstore_data->rd_lock));
                return -1;
            }
            uint32_t *block_data = (uint32_t*) block_buf;

            // Read data from block
            for (uint32_t j = 0; j < (BLOCK_SIZE / 4); j++)
                if (blocks_done < start_block)
                    blocks_done++;
                else if (block_data[j])
                    get_block_data(block_data[j]);
                else
                    break;

            cfree(block_buf, BLOCK_SIZE);
        }
        else
            break;

    if (remaining > 0)
        for (; remaining > 0; remaining--)
            buf[size - remaining] = 0;

    cfree(temp, BLOCK_SIZE);
    pthread_mutex_unlock(&(objstore_data->rd_lock));
    return size;
}

/*
   Reads the object metadata for obj->id = buf->st_ino
   Fillup buf->st_size and buf->st_blocks correctly
   See man 2 stat

   Return value: Success --> 0
   Failure --> -1
   */
int fillup_size_details(struct stat *buf, struct objfs_state *objfs)
{
    pObjData objstore_data = (pObjData) objfs->objstore_data;
    pObj start = objstore_data->objects;
    pObj object = start + (buf->st_ino - 2);
    if (buf->st_ino < 2)  // Check validity
        return -1;

    buf->st_size = object->size;
    buf->st_blocks = object->size >> 9;  // # of 512B blocks
    if (((object->size >> 9) << 9) != object->size)
        (buf->st_blocks)++;
    return 0;
}

/* Set your private pointers, anyway you like.

   Return value: Success --> 0
   Failure --> -1
   */
int objstore_init(struct objfs_state *objfs)
{
    objfs->objstore_data = cmalloc(sizeof(ObjData));
    pObjData objstore_data = (pObjData) objfs->objstore_data;
    for (uint32_t i = 0; i < OBJ_BMP_LN; i++)
        (objstore_data->obj_bmp)[i] = 0;

    uint32_t buf_size = ((8 * BLK_BMP_LN) > BLOCK_SIZE) ? (8 * BLK_BMP_LN) : BLOCK_SIZE;
    char* res_blk_buf = cmalloc(buf_size);
    if (!res_blk_buf)
        return -1;

    // Read block bitmap blocks into block bitmap
    for (uint32_t i = 0; i < BLK_BMP_BLOCKS; i++)
        if (read_block(objfs, i, res_blk_buf + i * BLOCK_SIZE) < 0)
            return -1;
    memcpy((char*) (objstore_data->blk_bmp), res_blk_buf, 8 * BLK_BMP_LN);

    // Mark reserved blocks in block bitmap as used
    for (uint32_t i = 0; ((i * 64) < RES_BLOCKS) && (i < BLK_BMP_LN); i++)
        if (((i + 1) * 64) < RES_BLOCKS)
            (objstore_data->blk_bmp)[i] = ~0x0l;
        else
            (objstore_data->blk_bmp)[i] |= (0x1l << (RES_BLOCKS - (i * 64))) - 1;

    // Read objects into object array
    for (uint32_t i = 0; i < OBJ_BLOCKS; i++)
    {
        if (read_block(objfs, i + BLK_BMP_BLOCKS, res_blk_buf) < 0)
            return -1;
        memcpy((char*) (objstore_data->objects + i * OBJ_PER_BLOCK), res_blk_buf, OBJ_PER_BLOCK * sizeof(Obj));
    }

    // Initialize mutexes
    if (pthread_mutex_init(&(objstore_data->obj_cl_lk), NULL))
        return -1;
    if (pthread_mutex_init(&(objstore_data->obj_bmp_lk), NULL))
        return -1;
    if (pthread_mutex_init(&(objstore_data->blk_bmp_lk), NULL))
        return -1;
    if (pthread_mutex_init(&(objstore_data->ch_lock), NULL))
        return -1;
    if (pthread_mutex_init(&(objstore_data->rd_lock), NULL))
        return -1;

#ifdef CACHE
    // Cache initialized with 0s
    for (uint32_t i = 0; i < CACHE_LEN; i++)
        (objstore_data->cache_info)[i] = 0;
#endif

    cfree(res_blk_buf, buf_size);
    return 0;
}

/* Cleanup private data. FS is being unmounted

   Return value: Success --> 0
   Failure --> -1
   */
int objstore_destroy(struct objfs_state *objfs)
{
    pObjData objstore_data = (pObjData) objfs->objstore_data;

#ifdef CACHE
    // Flush all dirty cache blocks to cache
    for (uint32_t i = 0; i < CACHE_LEN; i++)
        if (!((objstore_data->cache_info)[i]))
            flush_ch_blk(objfs, i);
#endif

    // Destroy mutexes
    pthread_mutex_destroy(&(objstore_data->obj_cl_lk));
    pthread_mutex_destroy(&(objstore_data->obj_bmp_lk));
    pthread_mutex_destroy(&(objstore_data->blk_bmp_lk));
    pthread_mutex_destroy(&(objstore_data->ch_lock));
    pthread_mutex_destroy(&(objstore_data->rd_lock));

    uint32_t buf_size = ((8 * BLK_BMP_LN) > BLOCK_SIZE) ? (8 * BLK_BMP_LN) : BLOCK_SIZE;
    char* res_blk_buf = cmalloc(buf_size);
    if (!res_blk_buf)
        return -1;

    memcpy(res_blk_buf, (char*) objstore_data->blk_bmp, 8 * BLK_BMP_LN);
    // Write block bitmap to block bitmap blocks
    for (uint32_t i = 0; i < BLK_BMP_BLOCKS; i++)
        if (write_block(objfs, i, res_blk_buf + i * BLOCK_SIZE) < 0)
            return -1;

    // Write dirty objects to object blocks
    for (uint32_t i = 0; i < OBJ_BLOCKS; i++)
    {
        int dirty = 0;
        for (int j = 0; j < OBJ_PER_BLOCK; j++)
        {
            // Check if bit is dirty
            uint64_t *bitmap = objstore_data->obj_bmp;
            uint32_t objid = i * OBJ_PER_BLOCK + j + 2;
            int status = (bitmap[(objid - 2) / 64] >> ((objid - 2) % 64)) & 0x1;

            if (status < 0)
                return -1;
            else if (status)
            {
                dirty = 1;
                break;
            }
        }

        if (!dirty)
            continue;
        memcpy(res_blk_buf, (char*) (objstore_data->objects + i * OBJ_PER_BLOCK), OBJ_PER_BLOCK * sizeof(Obj));
        if (write_block(objfs, i + BLK_BMP_BLOCKS, res_blk_buf) < 0)
            return -1;
    }

    cfree(res_blk_buf, buf_size);
    cfree(objfs->objstore_data, sizeof(ObjData));
    objfs->objstore_data = NULL;
    return 0;
}
