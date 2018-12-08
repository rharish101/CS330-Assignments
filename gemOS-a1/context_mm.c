#include<context.h>
#include<memory.h>
#include<lib.h>

u64 get_index(struct exec_context *ctx, u8 segment, u8 level_num)
{
    u64 va; // Index in the page table
    if (segment == MM_SEG_STACK)
        va = ctx->mms[segment].end - 0x1000;
    else
        va = ctx->mms[segment].start;
    return (va >> (level_num * 9 + 3)) & 0x1FF;
}

void create_page_table_entry(struct exec_context *ctx, u8 segment, u64* level, u8 level_num)
{
    u64 index = get_index(ctx, segment, level_num); // Index in the page table

    if ((level[index] & 0x1) == 0)
    {
        if ((level_num == 1) && (segment != MM_SEG_DATA))
            level[index] = os_pfn_alloc(USER_REG) << 12; // Page frame for actual data
        else if (level_num == 1)
            level[index] = ctx->arg_pfn << 12; // Data segment PFN
        else
        {
            // Create next level page table and initialize with zeros
            level[index] = os_pfn_alloc(OS_PT_REG);
            u64* next_level = (u64*) osmap(level[index]);
            level[index] = level[index] << 12;
            for (u16 i = 0; i < 512; i++)
                next_level[i] = 0;
        }
        level[index] |= 0x5; // Present bit and user/supervisor bit
    }

    level[index] |= ctx->mms[segment].access_flags & 0x2; // R/W access flag

    // Recursively create entries in lower page tables
    if (level_num != 1)
        create_page_table_entry(ctx, segment, (u64*) osmap(level[index] >> 12), level_num - 1);
}

void prepare_context_mm(struct exec_context *ctx)
{
    ctx->pgd = os_pfn_alloc(OS_PT_REG);
    // Initialize L4 Table with zeros
    u64* level_4 = (u64*) osmap(ctx->pgd);
    for (u16 i = 0; i < 512; i++)
        level_4[i] = 0;

    // Allocate the physical frames
    create_page_table_entry(ctx, MM_SEG_STACK, level_4, 4);
    create_page_table_entry(ctx, MM_SEG_CODE, level_4, 4);
    create_page_table_entry(ctx, MM_SEG_DATA, level_4, 4);
}

void clean_page_table(struct exec_context *ctx, u64** levels, u8 level_num)
{
    u64* next_levels[4];
    u64 indices[4];

    indices[MM_SEG_STACK] = get_index(ctx, MM_SEG_STACK, level_num);
    next_levels[MM_SEG_STACK] = (u64*) osmap(levels[MM_SEG_STACK][indices[MM_SEG_STACK]] >> 12);
    indices[MM_SEG_CODE] = get_index(ctx, MM_SEG_CODE, level_num);
    next_levels[MM_SEG_CODE] = (u64*) osmap(levels[MM_SEG_CODE][indices[MM_SEG_CODE]] >> 12);
    indices[MM_SEG_DATA] = get_index(ctx, MM_SEG_DATA, level_num);
    next_levels[MM_SEG_DATA] = (u64*) osmap(levels[MM_SEG_DATA][indices[MM_SEG_DATA]] >> 12);

    u64 stack_pfn = levels[MM_SEG_STACK][indices[MM_SEG_STACK]] >> 12;
    u64 code_pfn = levels[MM_SEG_CODE][indices[MM_SEG_CODE]] >> 12;
    u64 data_pfn = levels[MM_SEG_DATA][indices[MM_SEG_DATA]] >> 12;

    u8 region;

    // Recursively clean entries in lower page tables
    if (level_num != 1)
    {
        clean_page_table(ctx, next_levels, level_num - 1);
        region = OS_PT_REG;
    }
    else
        region = USER_REG;

    os_pfn_free(region, stack_pfn);
    if (code_pfn != stack_pfn)
        os_pfn_free(region, code_pfn);
    if ((data_pfn != stack_pfn) && (data_pfn != code_pfn))
        os_pfn_free(region, data_pfn);
}

void cleanup_context_mm(struct exec_context *ctx)
{
    u64* level_4 = (u64*) osmap(ctx->pgd);
    u64* levels[4];
    levels[MM_SEG_STACK] = level_4;
    levels[MM_SEG_CODE] = level_4;
    levels[MM_SEG_DATA] = level_4;

    clean_page_table(ctx, levels, 4);
    os_pfn_free(OS_PT_REG, ctx->pgd);
}
