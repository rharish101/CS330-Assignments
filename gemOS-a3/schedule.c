#include<context.h>
#include<memory.h>
#include<schedule.h>
#include<apic.h>
#include<lib.h>
#include<idt.h>

static u64 numticks = 0;
static u32 stack_to_free = 0;

// Convert physical to virtual address
static inline u64* translate (u64 phy_addr)
{
    u64 offset = phy_addr & 0xFFF;
    return (u64*) (((u64) osmap(phy_addr >> 12)) | offset);
}

static void save_current_context(u8 state, u64 rsp_bak, u64 rbp_bak, int skip_rax)
{
    struct exec_context *current = get_current_ctx();
    current->state = state;

    u64* rbp_point = translate(rbp_bak);
    u64* rsp_point = translate(rsp_bak);

    // Backing up current's special regs
    current->regs.rbp = rbp_point[0];
    current->regs.entry_rip = rbp_point[1];
    current->regs.entry_cs = rbp_point[2];
    current->regs.entry_rflags = rbp_point[3];
    current->regs.entry_rsp = rbp_point[4];
    current->regs.entry_ss = rbp_point[5];

    // Backing up current's GPRs
    current->regs.r15 = rsp_point[0];
    current->regs.r14 = rsp_point[1];
    current->regs.r13 = rsp_point[2];
    current->regs.r12 = rsp_point[3];
    current->regs.r11 = rsp_point[4];
    current->regs.r10 = rsp_point[5];
    current->regs.r9 = rsp_point[6];
    current->regs.r8 = rsp_point[7];
    current->regs.rdi = rsp_point[8];
    current->regs.rsi = rsp_point[9];
    current->regs.rdx = rsp_point[10];
    current->regs.rcx = rsp_point[11];
    current->regs.rbx = rsp_point[12];
    if (!skip_rax)
        current->regs.rax = rsp_point[13];
}

static void schedule_context(struct exec_context *next, int do_ack_irq)
{
    next->state = RUNNING;
    /*get_current_ctx() still returns the old context*/
    struct exec_context *current = get_current_ctx();
    printf("schedluing: old pid = %d  new pid  = %d\n", current->pid, next->pid); /*XXX: Don't remove*/

    /*These two lines must be executed*/
    if (do_ack_irq)
        ack_irq();  /*acknowledge the interrupt, before calling iretq */
    set_tss_stack_ptr(next);
    set_current_ctx(next);

    // Restoring next's PCB and returning to it
    // First restoring special registers
    asm volatile ("mov %0, (%%rbp);"
                  "mov %1, 8(%%rbp);"
                  "mov %2, 16(%%rbp);"
                  "mov %3, 24(%%rbp);"
                  "mov %4, 32(%%rbp);"
                  "mov %5, 40(%%rbp);"
                  :: "r" (next->regs.rbp),
                     "r" (next->regs.entry_rip),
                     "r" (next->regs.entry_cs),
                     "r" (next->regs.entry_rflags),
                     "r" (next->regs.entry_rsp),
                     "r" (next->regs.entry_ss)
                  : "memory");

    // Pushing GPRs onto stack and then poppping to avoid mixmatch
    asm volatile ("mov %0, %%r15;" :: "r" (next->regs.r15));
    asm volatile ("mov %0, %%r14;" :: "r" (next->regs.r14));
    asm volatile ("mov %0, %%r13;" :: "r" (next->regs.r13));
    asm volatile ("mov %0, %%r12;" :: "r" (next->regs.r12));
    asm volatile ("mov %0, %%r11;" :: "r" (next->regs.r11));
    asm volatile ("mov %0, %%r10;" :: "r" (next->regs.r10));
    asm volatile ("mov %0, %%r9;" :: "r" (next->regs.r9));
    asm volatile ("mov %0, %%r8;" :: "r" (next->regs.r8));
    asm volatile ("mov %0, %%rdi;" :: "r" (next->regs.rdi));
    asm volatile ("mov %0, %%rsi;" :: "r" (next->regs.rsi));
    asm volatile ("mov %0, %%rdx;" :: "r" (next->regs.rdx));
    asm volatile ("mov %0, %%rcx;" :: "r" (next->regs.rcx));
    asm volatile ("mov %0, %%rbx;" :: "r" (next->regs.rbx));
    asm volatile ("mov %0, %%rax;" :: "r" (next->regs.rax));

    // Finishing
    asm volatile ("mov %%rbp, %%rsp;"
                  "pop %%rbp;"
                  "iretq;"
                  ::: "memory");
}

// Round Robin
static struct exec_context *pick_next_context()
{
    int running, i;
    struct exec_context *list = get_ctx_list();

    for (running = 1; (running < MAX_PROCESSES) && (list[running].state != RUNNING); running++);
    if (running >= MAX_PROCESSES) // No running process
    {
        for (i = 1; (i < MAX_PROCESSES) && (list[i].state != READY); i++);
        if (i < MAX_PROCESSES) // Ready process found
            return list + i;
        else
        {
            for (i = 1; i < MAX_PROCESSES; i++)
                if (list[i].state == WAITING)
                    return list;
            do_cleanup();
        }
    }

    // Cycling traversal over context list to find ready process
    for (i = running + 1; i < MAX_PROCESSES; i++)
        if (list[i].state == READY)
            return list + i;
    for (i = 1; i < running; i++)
        if (list[i].state == READY)
            return list + i;
    return list + running;  // No ready process found
}

// Check if waiting process exists
static int waiting_proc_exists()
{
    struct exec_context *list = get_ctx_list();
    for (int i = 1; i < MAX_PROCESSES; i++)
        if (list[i].state == WAITING)
            return 1;
    return 0;
}

// Decrement sleep and alarm count to 0 and take action if it just becomes 0
static void do_sleep_and_alarm_account()
{
    struct exec_context *ctx_list = get_ctx_list();
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        // Mark sleeping processes as READY
        if ((ctx_list[i].ticks_to_sleep > 0) && ((--(ctx_list[i].ticks_to_sleep)) == 0))
            ctx_list[i].state = READY;

        // Send alarm to process if it is RUNNING
        if ((ctx_list[i].ticks_to_alarm > 0) && ((--(ctx_list[i].ticks_to_alarm)) == 0))
        {
            ctx_list[i].ticks_to_alarm = ctx_list[i].alarm_config_time;
            if (ctx_list[i].state == RUNNING)
                invoke_sync_signal(SIGALRM, &(ctx_list[i].regs.entry_rsp), &(ctx_list[i].regs.entry_rip));
        }
    }
}

/*The five functions above are just a template. You may change the signatures as you wish*/
void handle_timer_tick()
{
    u64 rsp_bak, rbp_bak, proc_rsp, proc_rip;
    asm volatile ("push %%rax;"
                  "push %%rbx;"
                  "push %%rcx;"
                  "push %%rdx;"
                  "push %%rsi;"
                  "push %%rdi;"
                  "push %%r8;"
                  "push %%r9;"
                  "push %%r10;"
                  "push %%r11;"
                  "push %%r12;"
                  "push %%r13;"
                  "push %%r14;"
                  "push %%r15;"
                  ::: "memory");

    asm volatile ("mov 8(%%rbp), %0;"
                  "mov 32(%%rbp), %1;"
                  "mov %%rsp, %2;"
                  "mov %%rbp, %3;"
                  : "=r" (proc_rip), "=r" (proc_rsp), "=r" (rsp_bak), "=r" (rbp_bak)
                  :: "memory");

    struct exec_context *current = get_current_ctx();
    asm volatile ("mov (%%rbp), %0;" : "=r" (current->regs.rbp) :: "memory");
    current->regs.entry_rsp = proc_rsp;
    current->regs.entry_rip = proc_rip;

    printf("Got a tick. #ticks = %u\n", numticks++);   /*XXX Do not modify this line*/

    if (stack_to_free)
    {
        os_pfn_free(OS_PT_REG, stack_to_free);
        stack_to_free = 0;
    }

    do_sleep_and_alarm_account();

    struct exec_context *next = pick_next_context();
    if (next->pid == current->pid) // No process change
    {
        ack_irq();  /*acknowledge the interrupt, before calling iretq */
        asm volatile ("mov %0, 8(%%rbp);"
                      "mov %1, 32(%%rbp);"
                      "mov %2, %%rsp;"
                      "pop %%r15;"
                      "pop %%r14;"
                      "pop %%r13;"
                      "pop %%r12;"
                      "pop %%r11;"
                      "pop %%r10;"
                      "pop %%r9;"
                      "pop %%r8;"
                      "pop %%rdi;"
                      "pop %%rsi;"
                      "pop %%rdx;"
                      "pop %%rcx;"
                      "pop %%rbx;"
                      "pop %%rax;"
                      "mov %%rbp, %%rsp;"
                      "pop %%rbp;"
                      "iretq;"
                      :: "r" (current->regs.entry_rip), "r" (current->regs.entry_rsp), "r" (rsp_bak)
                      : "memory");
    }
    else
    {
        save_current_context(READY, rsp_bak, rbp_bak, 0);
        schedule_context(next, 1);
    }
}

void do_exit()
{
    /*
        You may need to invoke the scheduler from here if there are
        other processes except swapper in the system. Make sure you make
        the status of the current process to UNUSED before scheduling
        the next process. If the only process alive in system is swapper,
        invoke do_cleanup() to shutdown gem5 (by crashing it, huh!)
    */
    struct exec_context *current = get_current_ctx();
    struct exec_context *next = pick_next_context();

    // Cleanup process
    stack_to_free = current->os_stack_pfn;
    current->state = UNUSED;

    if (next->pid != current->pid)
        schedule_context(next, 0);  // Switch to other process
    else if (waiting_proc_exists())
        schedule_context(get_ctx_by_pid(0), 0);
    else
        do_cleanup();  /*Call this conditionally, see comments above*/
}

/*system call handler for sleep*/
long do_sleep(u32 ticks)
{
    struct exec_context *current = get_current_ctx();
    current->ticks_to_sleep = ticks;

    // Save current context's PCB
    u64* os_rsp;
    asm volatile ("mov (%%rbp), %0;" : "=r" (os_rsp) :: "memory");

    // Rearrange a/c to order in handle_timer_tick()
    u64 rbp_bak = os_rsp[10];
    os_rsp[10] = os_rsp[11];
    os_rsp[11] = os_rsp[12];
    os_rsp[12] = os_rsp[13];
    os_rsp[13] = os_rsp[14];
    os_rsp[14] = os_rsp[15];
    os_rsp[15] = rbp_bak;

    save_current_context(WAITING, (u64) (os_rsp + 2), (u64) (os_rsp + 15), 1);
    struct exec_context *next = pick_next_context();
    schedule_context(next, 0);
}

long do_clone(void *th_func, void *user_stack)
{
    // itoa
    inline void num_to_str(char* str, int num)
    {
        int rev = 0;
        while (num > 0)
        {
            rev = rev * 10 + num % 10;
            num /= 10;
        }

        int i = 0;
        while (rev > 0)
        {
            str[i] = '0' + rev % 10;
            rev /= 10;
            i++;
        }
        str[i] = '\0';
    }

    struct exec_context *current = get_current_ctx();
    struct exec_context *new = get_new_ctx();

    // Copy all required from parent
    new->type = current->type;
    new->used_mem = current->used_mem;
    new->pgd = current->pgd;
    new->os_rsp = current->os_rsp;
    for (int i = 0; i < MAX_MM_SEGS; i++)
        (new->mms)[i] = (current->mms)[i];
    for (int i = 0; i < MAX_SIGNALS; i++)
        (new->sighandlers)[i] = (current->sighandlers)[i];

    new->pending_signal_bitmap = 0;
    new->ticks_to_sleep = 0;
    new->alarm_config_time = 0;
    new->ticks_to_alarm = 0;

    new->os_stack_pfn = os_pfn_alloc(OS_PT_REG);

    // parent's name + str(parent's pid)
    char name[CNAME_MAX];
    int length = strlen(current->name);
    memcpy(name, current->name, length);
    char pid[CNAME_MAX];
    num_to_str(pid, current->pid);
    int pid_len = strlen(pid);
    if ((length + pid_len + 1) < CNAME_MAX)
        strcat(name, pid);
    memcpy(new->name, name, length + pid_len + 1);
    new->name[length + pid_len + 1] = '\0';

    // Store req. reg. values and zero others
    new->regs = current->regs;
    new->regs.entry_rip = (u64) th_func;
    new->regs.entry_cs = 0x23;
    new->regs.entry_rsp = (u64) user_stack;
    new->regs.entry_ss = 0x2B;
    new->regs.rbp = (u64) user_stack;
    u64* address = (u64*) translate((((u64)current->os_stack_pfn + 1) << 12) - 8);
    new->regs.entry_rflags = *(address-2);

    // Release it to the wild!
    new->state = READY;
}

long invoke_sync_signal(int signo, u64 *ustackp, u64 *urip)
{
    /*If signal handler is registered, manipulate user stack and RIP to execute signal handler*/
    /*ustackp and urip are pointers to user RSP and user RIP in the exception/interrupt stack*/
    printf("Called signal with ustackp=%x urip=%x\n", *ustackp, *urip);
    /*Default behavior is exit( ) if sighandler is not registered for SIGFPE or SIGSEGV. Ignore for SIGALRM*/
    struct exec_context *current = get_current_ctx();
    if (((current->sighandlers)[signo] == NULL) && (signo != SIGALRM))
        do_exit();
    else // Push RIP onto RSP and set handler address in RIP
    {
        u64** stackp = (u64**) ustackp;
        --*stackp;
        **stackp = *urip;
        *urip = (u64)(current->sighandlers)[signo];
    }
}

/*system call handler for signal, to register a handler*/
long do_signal(int signo, unsigned long handler)
{
    struct exec_context *current = get_current_ctx();
    (current->sighandlers)[signo] = (void*) handler;
}

/*system call handler for alarm*/
long do_alarm(u32 ticks)
{
    struct exec_context *current = get_current_ctx();
    current->alarm_config_time = current->ticks_to_alarm = ticks;
}
