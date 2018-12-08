#include<init.h>
#include<lib.h>
#include<context.h>
#include<memory.h>

/*System Call handler*/
long do_syscall(int syscall, u64 param1, u64 param2, u64 param3, u64 param4)
{
    struct exec_context *current = get_current_ctx();
    printf("[GemOS] System call invoked. syscall no  = %d\n", syscall);
    switch(syscall)
    {
        case SYSCALL_EXIT:
            printf("[GemOS] exit code = %d\n", (int) param1);
            do_exit();
            break;
        case SYSCALL_GETPID:
            printf("[GemOS] getpid called for process %s, with pid = %d\n", current->name, current->id);
            return current->id;
        case SYSCALL_WRITE:
        {
            u64 temp = param1;
            u64 *table;
            u16 offset = param1 & 0xFFF;
            int length = param2;
            if (length > 1024)
                return -1;
            do
            {
                table = (u64*) osmap(current->pgd);
                for (u8 level = 4; level >= 1; level--)
                {
                    u64 index = (temp >> (3 + 9 * level)) & 0x1FF;
                    if (!(table[index] & 0x1))
                        return -1;
                    table = (u64*) osmap(table[index] >> 12);
                }
                temp += 0x1000;
                length -= 0x1000 - offset;
                offset = 0;
            } while (length > 0);
            for (int i = 0; i < param2; i++)
                printf("%c", ((char*) param1)[i]);
            return param2;
        }
        case SYSCALL_EXPAND:
        {
            u8 segment;
            if (param2 == MAP_RD)
                segment = MM_SEG_RODATA;
            else if (param2 == MAP_WR)
                segment = MM_SEG_DATA;
            else
                return 0;
            if ((current->mms[segment].end - current->mms[segment].next_free) < (param1 << 12))
                return 0;
            u64 return_val = current->mms[segment].next_free;
            current->mms[segment].next_free = current->mms[segment].next_free + (param1 << 12);
            return return_val;
        }
        case SYSCALL_SHRINK:
        {
            u8 segment;
            if (param2 == MAP_RD)
                segment = MM_SEG_RODATA;
            else if (param2 == MAP_WR)
                segment = MM_SEG_DATA;
            else
                return 0;
            if ((current->mms[segment].next_free - current->mms[segment].start) < (param1 << 12))
                return 0;
            u64 new_next = current->mms[segment].next_free - (param1 << 12);
            for (u64 page_addr = current->mms[segment].next_free; page_addr >= new_next; page_addr -= 0x1000)
            {
                u64* table = (u64*) osmap(current->pgd);
                for (u8 level = 4; level >= 1; level--)
                {
                    u8 index = (page_addr >> (3 + 9 * level)) & 0x1FF;
                    if (! (table[index] & 0x1))
                        break;
                    u32 pfn = (table[index] >> 12) & 0xFFFFFFFFFF;
                    if (level > 1)
                        table = (u64*) osmap(pfn);
                    else
                    {
                        os_pfn_free(USER_REG, pfn);
                        table[index] = 0;
                        asm ("invlpg (%0)" :: "r" (page_addr) : "memory");
                    }
                }
            }
            current->mms[segment].next_free = new_next;
            return new_next;
        }
        default:
            return -1;
    }
    return 0;   /*GCC shut up!*/
}

extern int handle_div_by_zero(void)
{
    u64 rip_val;
    asm volatile ("movq 1(%%rbp), %0;" : "=r" (rip_val));
    printf("Div-by-zero detected at %x\n", rip_val);
    do_exit();
    return 0;
}

extern int handle_page_fault(void)
{
    u64 rsp_bak, vaddr, error, rip_val;
    // Save general purpose registers, VA of error, error code and RIP of error
    asm ("push %%rax;"
         "push %%rbx;"
         "push %%rcx;"
         "push %%rdx;"
         "push %%rdi;"
         "push %%rsi;"
         "push %%r8;"
         "push %%r9;"
         "push %%r10;"
         "push %%r11;"
         "push %%r12;"
         "push %%r13;"
         "push %%r14;"
         "push %%r15;"
         "mov %%rsp, %0;"
         "mov %%cr2, %1;"
         "mov 8(%%rbp), %2;"
         "mov 16(%%rbp), %3;"
         : "=r" (rsp_bak), "=r" (vaddr), "=r" (error), "=r" (rip_val));

    /* printf("PF Handler invoked. VA: %x, Error: %x, RIP: %x\n", vaddr, error, rip_val); */

    if (error & 0x1)  // Protection violation
    {
        printf("Page fault exception. VA: %x, RIP: %x, Error code: %x\n", vaddr, rip_val, error);
        do_exit();
        return 0;
    }

    struct exec_context *current = get_current_ctx();
    u8 segment;
    if ((vaddr >= current->mms[MM_SEG_DATA].start) && (vaddr <= current->mms[MM_SEG_DATA].end))
    {
        if (vaddr >= current->mms[MM_SEG_DATA].next_free)
        {
            printf("Page fault exception. VA: %x, RIP: %x, Error code: %x\n", vaddr, rip_val, error);
            do_exit();
            return 0;
        }
        segment = MM_SEG_DATA;
    }
    else if ((vaddr >= current->mms[MM_SEG_RODATA].start) && (vaddr <= current->mms[MM_SEG_RODATA].end))
    {
        if ((error & 0x2) || (vaddr >= current->mms[MM_SEG_DATA].next_free))
        {
            printf("Page fault exception. VA: %x, RIP: %x, Error code: %x\n", vaddr, rip_val, error);
            do_exit();
            return 0;
        }
        segment = MM_SEG_RODATA;
    }
    else if ((vaddr >= current->mms[MM_SEG_STACK].start) && (vaddr < current->mms[MM_SEG_STACK].end))
        segment = MM_SEG_STACK;
    else  // NOTA
    {
        printf("Page fault exception. VA: %x, RIP: %x, Error code: %x\n", vaddr, rip_val, error);
        do_exit();
        return 0;
    }

    // Create the page table mapping
    u64* table = (u64*) osmap(current->pgd);
    for (u8 level = 4; level >= 1; level--)
    {
        u64 index = (vaddr >> (3 + 9 * level)) & 0x1FF;
        if (!(table[index] & 0x1))
        {
            if (level == 1)
                table[index] = os_pfn_alloc(USER_REG) << 12;
            else
                table[index] = os_pfn_alloc(OS_PT_REG) << 12;
            table[index] |= 0x5;
        }
        table[index] |= current->mms[segment].access_flags & 0x2;
        table = (u64*) osmap(table[index] >> 12);
    }

    // Restore stack to point just after pushing general purpose registers and pop them
    asm ("mov %0, %%rsp;"
         "pop %%r15;"
         "pop %%r14;"
         "pop %%r13;"
         "pop %%r12;"
         "pop %%r11;"
         "pop %%r10;"
         "pop %%r9;"
         "pop %%r8;"
         "pop %%rsi;"
         "pop %%rdi;"
         "pop %%rdx;"
         "pop %%rcx;"
         "pop %%rbx;"
         "pop %%rax;"
         "mov %%rbp, %%rsp;"
         "pop %%rbp;"
         "add $8, %%rsp;"
         "iret;"
         :: "r" (rsp_bak));
    return 0;
}
