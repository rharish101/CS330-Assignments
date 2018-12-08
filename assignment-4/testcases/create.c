#include "kvstore.h"
#include <string.h>
#include "debug.h"

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s key \"value\"\n", argv[0]);
        exit(100);
    }
#ifdef ALLOC
    int vsize = strlen(argv[2]);
    char *value = (char *)malloc(vsize * sizeof(char));
    if (!value) {
        dprintf("OOM\n");
        exit(200);
    }
    memcpy(value, argv[2], vsize * sizeof(char));
    if (put_key(argv[1], value, vsize) < 0) {
#else
    if (put_key(argv[1], argv[2], strlen(argv[2])) < 0) {
#endif
        dprintf("FAILED\n");
#ifdef ALLOC
        free(value);
#endif
        exit(10);
    } else
        dprintf("SUCCESS\n");
#ifdef ALLOC
    free(value);
#endif
    return 0;
}
