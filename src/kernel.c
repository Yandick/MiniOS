#include "os_project.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/*
 * MiniKernel module.
 * Integrates PCB management, scheduler ticks, first-fit memory, TinyFS,
 * page-access tracing, and dmesg-style event logging into one OS state.
 */

static KernelProcess *find_kernel_process(MiniKernel *kernel, int pid) {
    int i;
    for (i = 0; i < kernel->process_count; i++) {
        if (kernel->processes[i].pid == pid) {
            return &kernel->processes[i];
        }
    }
    return NULL;
}

static const KernelProcess *find_kernel_process_const(const MiniKernel *kernel, int pid) {
    int i;
    for (i = 0; i < kernel->process_count; i++) {
        if (kernel->processes[i].pid == pid) {
            return &kernel->processes[i];
        }
    }
    return NULL;
}

static void kernel_pid_name(int pid, char *out, size_t out_size) {
    (void)snprintf(out, out_size, "K%d", pid);
}

static void kernel_path_name(const char *path, char *out, size_t out_size) {
    if (path == NULL || path[0] == '\0') {
        safe_copy(out, out_size, "/");
    } else if (path[0] == '/') {
        safe_copy(out, out_size, path);
    } else {
        (void)snprintf(out, out_size, "/%s", path);
    }
}

static void kernel_logf(MiniKernel *kernel, const char *fmt, ...) {
    char body[MAX_KERNEL_LOG_LEN];
    va_list args;
    int slot;
    int i;
    if (kernel == NULL || fmt == NULL) {
        return;
    }
    va_start(args, fmt);
    (void)vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);
    if (kernel->log_count < MAX_KERNEL_LOGS) {
        slot = kernel->log_count++;
    } else {
        /* Bound dmesg storage by discarding the oldest entry on overflow. */
        for (i = 1; i < MAX_KERNEL_LOGS; i++) {
            safe_copy(kernel->logs[i - 1], sizeof(kernel->logs[i - 1]), kernel->logs[i]);
        }
        slot = MAX_KERNEL_LOGS - 1;
    }
    (void)snprintf(kernel->logs[slot], sizeof(kernel->logs[slot]), "[tick %03d] %.170s", kernel->tick, body);
}

void kernel_log_event(MiniKernel *kernel, const char *event) {
    kernel_logf(kernel, "%s", event == NULL ? "event" : event);
}

void kernel_print_dmesg(const MiniKernel *kernel) {
    int i;
    if (kernel == NULL) {
        return;
    }
    if (kernel->log_count == 0) {
        printf("[dmesg] no kernel events yet\n");
        return;
    }
    for (i = 0; i < kernel->log_count; i++) {
        printf("%s\n", kernel->logs[i]);
    }
}

static int kernel_mlfq_level_quantum(const MiniKernel *kernel, int level) {
    int base = kernel == NULL || kernel->time_quantum <= 0 ? 2 : kernel->time_quantum;
    if (level <= 0) {
        return base;
    }
    if (level == 1) {
        return base * 2;
    }
    return base * 4;
}

static void kernel_reset_mlfq_process(KernelProcess *process) {
    if (process == NULL) {
        return;
    }
    process->mlfq_level = 0;
    process->mlfq_ticks_left = 0;
    process->mlfq_wait_ticks = 0;
}

static void kernel_reset_mlfq_state(MiniKernel *kernel) {
    int i;
    if (kernel == NULL) {
        return;
    }
    for (i = 0; i < kernel->process_count; i++) {
        if (strcmp(kernel->processes[i].state, "TERMINATED") != 0) {
            kernel_reset_mlfq_process(&kernel->processes[i]);
        }
    }
}

const char *kernel_scheduler_name(const MiniKernel *kernel) {
    int policy = kernel == NULL ? KERNEL_SCHED_RR : kernel->scheduler_policy;
    if (policy == KERNEL_SCHED_FCFS) {
        return "FCFS";
    }
    if (policy == KERNEL_SCHED_SJF) {
        return "SJF";
    }
    if (policy == KERNEL_SCHED_PRIORITY) {
        return "PRIORITY";
    }
    if (policy == KERNEL_SCHED_MLFQ) {
        return "MLFQ";
    }
    return "RR";
}

const char *kernel_vm_policy_name(const MiniKernel *kernel) {
    int policy = kernel == NULL ? KERNEL_VM_LRU : kernel->vm_policy;
    return policy == KERNEL_VM_FIFO ? "FIFO" : "LRU";
}

int kernel_set_scheduler(MiniKernel *kernel, const char *policy, int quantum) {
    if (kernel == NULL || policy == NULL) {
        return 0;
    }
    if (strcmp(policy, "rr") == 0 || strcmp(policy, "RR") == 0) {
        kernel->scheduler_policy = KERNEL_SCHED_RR;
        kernel->time_quantum = quantum > 0 ? quantum : kernel->time_quantum;
        if (kernel->time_quantum <= 0) {
            kernel->time_quantum = 2;
        }
    } else if (strcmp(policy, "fcfs") == 0 || strcmp(policy, "FCFS") == 0) {
        kernel->scheduler_policy = KERNEL_SCHED_FCFS;
    } else if (strcmp(policy, "sjf") == 0 || strcmp(policy, "SJF") == 0) {
        kernel->scheduler_policy = KERNEL_SCHED_SJF;
    } else if (strcmp(policy, "priority") == 0 || strcmp(policy, "prio") == 0 ||
               strcmp(policy, "PRIORITY") == 0) {
        kernel->scheduler_policy = KERNEL_SCHED_PRIORITY;
    } else if (strcmp(policy, "mlfq") == 0 || strcmp(policy, "MLFQ") == 0) {
        kernel->scheduler_policy = KERNEL_SCHED_MLFQ;
        kernel->time_quantum = quantum > 0 ? quantum : kernel->time_quantum;
        if (kernel->time_quantum <= 0) {
            kernel->time_quantum = 2;
        }
        kernel_reset_mlfq_state(kernel);
    } else {
        return 0;
    }
    if (kernel->current_index >= 0 && kernel->current_index < kernel->process_count &&
        strcmp(kernel->processes[kernel->current_index].state, "RUNNING") == 0) {
        safe_copy(kernel->processes[kernel->current_index].state, sizeof(kernel->processes[kernel->current_index].state), "READY");
        kernel_logf(kernel, "sched requeue pid=%d after policy change", kernel->processes[kernel->current_index].pid);
        kernel->current_index = -1;
    }
    kernel->quantum_left = 0;
    if (kernel->scheduler_policy == KERNEL_SCHED_MLFQ) {
        kernel_logf(kernel, "sched set policy=MLFQ base_quantum=%d aging=%d", kernel->time_quantum,
                    KERNEL_MLFQ_AGING_TICKS);
    } else {
        kernel_logf(kernel, "sched set policy=%s quantum=%d", kernel_scheduler_name(kernel), kernel->time_quantum);
    }
    return 1;
}

void kernel_reset_vm(MiniKernel *kernel) {
    int i;
    if (kernel == NULL) {
        return;
    }
    kernel->vm_clock = 0;
    kernel->vm_faults = 0;
    kernel->vm_hits = 0;
    for (i = 0; i < MAX_FRAMES; i++) {
        kernel->vm_frame_pid[i] = -1;
        kernel->vm_frame_page[i] = -1;
        kernel->vm_loaded_at[i] = -1;
        kernel->vm_last_used[i] = -1;
    }
}

int kernel_set_vm_policy(MiniKernel *kernel, const char *policy) {
    if (kernel == NULL || policy == NULL) {
        return 0;
    }
    if (strcmp(policy, "fifo") == 0 || strcmp(policy, "FIFO") == 0) {
        kernel->vm_policy = KERNEL_VM_FIFO;
    } else if (strcmp(policy, "lru") == 0 || strcmp(policy, "LRU") == 0) {
        kernel->vm_policy = KERNEL_VM_LRU;
    } else {
        return 0;
    }
    kernel_reset_vm(kernel);
    kernel_logf(kernel, "vm set policy=%s reset_frames=yes", kernel_vm_policy_name(kernel));
    return 1;
}

static int kernel_vm_frame_count(const MiniKernel *kernel) {
    if (kernel == NULL) {
        return 1;
    }
    if (kernel->page_frames < 1) {
        return 1;
    }
    if (kernel->page_frames > MAX_FRAMES) {
        return MAX_FRAMES;
    }
    return kernel->page_frames;
}

static int kernel_find_vm_frame(const MiniKernel *kernel, int pid, int page) {
    int i;
    int frames = kernel_vm_frame_count(kernel);
    for (i = 0; i < frames; i++) {
        if (kernel->vm_frame_pid[i] == pid && kernel->vm_frame_page[i] == page) {
            return i;
        }
    }
    return -1;
}

static int kernel_pick_vm_victim(const MiniKernel *kernel) {
    int i;
    int victim = 0;
    int frames = kernel_vm_frame_count(kernel);
    for (i = 0; i < frames; i++) {
        if (kernel->vm_frame_pid[i] < 0) {
            return i;
        }
    }
    for (i = 1; i < frames; i++) {
        int value = kernel->vm_policy == KERNEL_VM_FIFO ? kernel->vm_loaded_at[i] : kernel->vm_last_used[i];
        int best = kernel->vm_policy == KERNEL_VM_FIFO ? kernel->vm_loaded_at[victim] : kernel->vm_last_used[victim];
        if (value < best) {
            victim = i;
        }
    }
    return victim;
}

static int process_is_runnable(const KernelProcess *process) {
    return process != NULL &&
           (strcmp(process->state, "READY") == 0 || strcmp(process->state, "RUNNING") == 0);
}

static void kernel_wake_blocked_processes(MiniKernel *kernel) {
    int i;
    if (kernel == NULL) {
        return;
    }
    for (i = 0; i < kernel->process_count; i++) {
        KernelProcess *process = &kernel->processes[i];
        if (strcmp(process->state, "BLOCKED") == 0 && process->wake_tick >= 0 && process->wake_tick <= kernel->tick) {
            process->wake_tick = -1;
            process->mlfq_wait_ticks = 0;
            safe_copy(process->state, sizeof(process->state), "READY");
            kernel_logf(kernel, "irq wake pid=%d -> READY", process->pid);
        }
    }
}

static void kernel_account_ready_wait(MiniKernel *kernel) {
    int i;
    if (kernel == NULL) {
        return;
    }
    for (i = 0; i < kernel->process_count; i++) {
        KernelProcess *process = &kernel->processes[i];
        if (strcmp(process->state, "READY") == 0) {
            process->wait_time++;
            if (kernel->scheduler_policy == KERNEL_SCHED_MLFQ) {
                process->mlfq_wait_ticks++;
                if (process->mlfq_level > 0 && process->mlfq_wait_ticks >= KERNEL_MLFQ_AGING_TICKS) {
                    int old_level = process->mlfq_level;
                    process->mlfq_level--;
                    process->mlfq_wait_ticks = 0;
                    process->mlfq_ticks_left = 0;
                    kernel_logf(kernel, "sched mlfq aging promote pid=%d q%d->q%d", process->pid, old_level,
                                process->mlfq_level);
                }
            }
        }
    }
}

static int choose_fcfs_process(const MiniKernel *kernel) {
    int i;
    for (i = 0; i < kernel->process_count; i++) {
        if (process_is_runnable(&kernel->processes[i])) {
            return i;
        }
    }
    return -1;
}

static int choose_sjf_process(const MiniKernel *kernel) {
    int best = -1;
    int i;
    for (i = 0; i < kernel->process_count; i++) {
        if (!process_is_runnable(&kernel->processes[i])) {
            continue;
        }
        if (best < 0 || kernel->processes[i].remaining < kernel->processes[best].remaining) {
            best = i;
        }
    }
    return best;
}

static int choose_priority_process(const MiniKernel *kernel) {
    int best = -1;
    int i;
    for (i = 0; i < kernel->process_count; i++) {
        if (!process_is_runnable(&kernel->processes[i])) {
            continue;
        }
        if (best < 0 || kernel->processes[i].priority < kernel->processes[best].priority) {
            best = i;
        }
    }
    return best;
}

static int choose_rr_process(const MiniKernel *kernel) {
    int attempts;
    for (attempts = 0; attempts < kernel->process_count; attempts++) {
        int index = (kernel->current_index + 1 + attempts) % kernel->process_count;
        if (process_is_runnable(&kernel->processes[index])) {
            return index;
        }
    }
    return -1;
}

static int choose_mlfq_process(const MiniKernel *kernel) {
    int level;
    int attempts;
    for (level = 0; level < KERNEL_MLFQ_LEVELS; level++) {
        for (attempts = 0; attempts < kernel->process_count; attempts++) {
            int index = (kernel->current_index + 1 + attempts) % kernel->process_count;
            if (process_is_runnable(&kernel->processes[index]) && kernel->processes[index].mlfq_level == level) {
                return index;
            }
        }
    }
    return -1;
}

static int kernel_mlfq_has_higher_ready(const MiniKernel *kernel, int current_level) {
    int i;
    if (kernel == NULL || current_level <= 0) {
        return 0;
    }
    for (i = 0; i < kernel->process_count; i++) {
        if (strcmp(kernel->processes[i].state, "READY") == 0 && kernel->processes[i].mlfq_level < current_level) {
            return 1;
        }
    }
    return 0;
}

static void kernel_mlfq_preempt_expired(MiniKernel *kernel, int index) {
    KernelProcess *process;
    int old_level;
    if (kernel == NULL || index < 0 || index >= kernel->process_count) {
        return;
    }
    process = &kernel->processes[index];
    if (strcmp(process->state, "RUNNING") != 0) {
        return;
    }
    old_level = process->mlfq_level;
    if (process->mlfq_level + 1 < KERNEL_MLFQ_LEVELS) {
        process->mlfq_level++;
    }
    process->mlfq_ticks_left = 0;
    process->mlfq_wait_ticks = 0;
    safe_copy(process->state, sizeof(process->state), "READY");
    if (old_level != process->mlfq_level) {
        kernel_logf(kernel, "sched mlfq demote pid=%d q%d->q%d quantum expired", process->pid, old_level,
                    process->mlfq_level);
    } else {
        kernel_logf(kernel, "sched mlfq preempt pid=%d q%d quantum expired", process->pid, process->mlfq_level);
    }
}

static void kernel_mlfq_preempt_for_higher_ready(MiniKernel *kernel, int index) {
    KernelProcess *process;
    if (kernel == NULL || index < 0 || index >= kernel->process_count) {
        return;
    }
    process = &kernel->processes[index];
    if (strcmp(process->state, "RUNNING") != 0) {
        return;
    }
    safe_copy(process->state, sizeof(process->state), "READY");
    kernel_logf(kernel, "sched mlfq preempt pid=%d q%d higher_queue_ready", process->pid, process->mlfq_level);
}

static int choose_next_process(MiniKernel *kernel) {
    int current = kernel == NULL ? -1 : kernel->current_index;
    if (kernel == NULL) {
        return -1;
    }
    if (kernel->scheduler_policy != KERNEL_SCHED_RR && kernel->scheduler_policy != KERNEL_SCHED_MLFQ &&
        current >= 0 && current < kernel->process_count &&
        strcmp(kernel->processes[current].state, "RUNNING") == 0) {
        /* Non-preemptive policies keep the current process until it exits. */
        return current;
    }
    if (kernel->scheduler_policy == KERNEL_SCHED_FCFS) {
        return choose_fcfs_process(kernel);
    }
    if (kernel->scheduler_policy == KERNEL_SCHED_SJF) {
        return choose_sjf_process(kernel);
    }
    if (kernel->scheduler_policy == KERNEL_SCHED_PRIORITY) {
        return choose_priority_process(kernel);
    }
    if (kernel->scheduler_policy == KERNEL_SCHED_MLFQ) {
        return choose_mlfq_process(kernel);
    }
    return choose_rr_process(kernel);
}

static void kernel_retry_memory_waiters(MiniKernel *kernel) {
    int i;
    if (kernel == NULL) {
        return;
    }
    for (i = 0; i < kernel->process_count; i++) {
        /* Recheck MEM_WAIT processes after memory is released. */
        KernelProcess *process = &kernel->processes[i];
        if (strcmp(process->state, "MEM_WAIT") == 0 && process->requested_memory > 0) {
            char owner[MAX_PID_LEN];
            PartitionEvent event;
            kernel_pid_name(process->pid, owner, sizeof(owner));
            event = ff_allocate(&kernel->memory, owner, process->requested_memory);
            if (event.success) {
                process->memory_size = process->requested_memory;
                process->wake_tick = -1;
                kernel_reset_mlfq_process(process);
                safe_copy(process->state, sizeof(process->state), "READY");
                kernel_logf(kernel, "mm wake pid=%d mem=%d -> READY", process->pid, process->memory_size);
            }
        }
    }
}

void kernel_init(MiniKernel *kernel) {
    memset(kernel, 0, sizeof(*kernel));
    kernel->next_pid = 1;
    kernel->current_index = -1;
    kernel->scheduler_policy = KERNEL_SCHED_RR;
    kernel->time_quantum = 2;
    kernel->quantum_left = 0;
    kernel->page_frames = 3;
    kernel->vm_policy = KERNEL_VM_LRU;
    kernel_reset_vm(kernel);
    ff_init(&kernel->memory, 128);
    fs_init(&kernel->fs, 128, 32);
    kernel_logf(kernel, "boot init scheduler=RR quantum=2 memory=128 fs_blocks=128 vm=LRU frames=3");
}

int kernel_alloc(MiniKernel *kernel, int pid, int size) {
    KernelProcess *process = find_kernel_process(kernel, pid);
    char owner[MAX_PID_LEN];
    PartitionEvent event;
    if (process == NULL || size <= 0) {
        return 0;
    }
    kernel_pid_name(pid, owner, sizeof(owner));
    event = ff_allocate(&kernel->memory, owner, size);
    if (event.success) {
        process->memory_size = size;
        process->requested_memory = size;
        kernel_logf(kernel, "mm alloc pid=%d size=%d OK", pid, size);
    } else {
        kernel_logf(kernel, "mm alloc pid=%d size=%d FAIL", pid, size);
    }
    return event.success;
}

int kernel_free(MiniKernel *kernel, int pid) {
    KernelProcess *process = find_kernel_process(kernel, pid);
    char owner[MAX_PID_LEN];
    PartitionEvent event;
    if (process == NULL) {
        return 0;
    }
    kernel_pid_name(pid, owner, sizeof(owner));
    event = ff_free(&kernel->memory, owner);
    if (event.success) {
        process->memory_size = 0;
        kernel_logf(kernel, "mm free pid=%d", pid);
        kernel_retry_memory_waiters(kernel);
    }
    return event.success;
}

int kernel_create_process(MiniKernel *kernel, const char *name, int burst, int memory_size) {
    return kernel_create_process_with_priority(kernel, name, burst, memory_size, 5);
}

int kernel_create_process_with_priority(MiniKernel *kernel, const char *name, int burst, int memory_size, int priority) {
    KernelProcess *process = NULL;
    if (kernel == NULL || name == NULL || burst <= 0 || kernel->process_count >= MAX_KERNEL_PROCESSES) {
        return -1;
    }
    process = &kernel->processes[kernel->process_count++];
    memset(process, 0, sizeof(*process));
    process->pid = kernel->next_pid++;
    safe_copy(process->name, sizeof(process->name), name);
    process->burst = burst;
    process->remaining = burst;
    process->priority = priority;
    process->requested_memory = memory_size;
    process->arrival_tick = kernel->tick;
    process->first_run_tick = -1;
    process->finish_tick = -1;
    process->wake_tick = -1;
    kernel_reset_mlfq_process(process);
    safe_copy(process->state, sizeof(process->state), "READY");
    kernel_logf(kernel, "proc create pid=%d name=%s burst=%d mem=%d priority=%d", process->pid, process->name, burst,
                memory_size, priority);
    if (memory_size > 0 && !kernel_alloc(kernel, process->pid, memory_size)) {
        safe_copy(process->state, sizeof(process->state), "MEM_WAIT");
        kernel_logf(kernel, "proc pid=%d state=MEM_WAIT", process->pid);
    }
    return process->pid;
}

void kernel_schedule_tick(MiniKernel *kernel) {
    int selected = -1;
    int previous = -1;
    if (kernel == NULL || kernel->process_count <= 0) {
        if (kernel != NULL) {
            kernel->tick++;
            kernel->idle_ticks++;
            kernel_logf(kernel, "timer tick -> idle (no process)");
        }
        return;
    }
    previous = kernel->current_index;
    if (kernel->scheduler_policy == KERNEL_SCHED_RR && previous >= 0 && previous < kernel->process_count &&
        strcmp(kernel->processes[previous].state, "RUNNING") == 0 && kernel->quantum_left <= 0) {
        /* RR preempts a still-running process only when its fixed time slice expires. */
        safe_copy(kernel->processes[previous].state, sizeof(kernel->processes[previous].state), "READY");
        kernel_logf(kernel, "sched preempt pid=%d quantum expired", kernel->processes[previous].pid);
    }
    if (kernel->scheduler_policy == KERNEL_SCHED_MLFQ && previous >= 0 && previous < kernel->process_count &&
        strcmp(kernel->processes[previous].state, "RUNNING") == 0 && kernel->processes[previous].mlfq_ticks_left <= 0) {
        kernel_mlfq_preempt_expired(kernel, previous);
    }
    kernel->tick++;
    kernel_wake_blocked_processes(kernel);
    if (kernel->scheduler_policy == KERNEL_SCHED_RR && previous >= 0 && previous < kernel->process_count &&
        strcmp(kernel->processes[previous].state, "RUNNING") == 0 && kernel->quantum_left > 0) {
        selected = previous;
    } else if (kernel->scheduler_policy == KERNEL_SCHED_MLFQ && previous >= 0 && previous < kernel->process_count &&
               strcmp(kernel->processes[previous].state, "RUNNING") == 0 &&
               kernel->processes[previous].mlfq_ticks_left > 0) {
        if (kernel_mlfq_has_higher_ready(kernel, kernel->processes[previous].mlfq_level)) {
            kernel_mlfq_preempt_for_higher_ready(kernel, previous);
            selected = choose_next_process(kernel);
        } else {
            selected = previous;
        }
    } else {
        selected = choose_next_process(kernel);
    }
    if (selected < 0) {
        kernel->idle_ticks++;
        kernel_logf(kernel, "timer tick -> cpu idle");
        return;
    }
    if (previous >= 0 && previous < kernel->process_count && previous != selected &&
        strcmp(kernel->processes[previous].state, "RUNNING") == 0) {
        safe_copy(kernel->processes[previous].state, sizeof(kernel->processes[previous].state), "READY");
        if (kernel->scheduler_policy != KERNEL_SCHED_RR) {
            kernel_logf(kernel, "sched switch pid=%d -> READY", kernel->processes[previous].pid);
        }
    }
    if (previous != selected || strcmp(kernel->processes[selected].state, "RUNNING") != 0) {
        if (kernel->scheduler_policy == KERNEL_SCHED_MLFQ) {
            int q = kernel_mlfq_level_quantum(kernel, kernel->processes[selected].mlfq_level);
            if (kernel->processes[selected].mlfq_ticks_left <= 0) {
                kernel->processes[selected].mlfq_ticks_left = q;
            }
            kernel_logf(kernel, "sched dispatch pid=%d name=%s policy=MLFQ q=%d slice=%d",
                        kernel->processes[selected].pid, kernel->processes[selected].name,
                        kernel->processes[selected].mlfq_level, kernel->processes[selected].mlfq_ticks_left);
        } else {
            kernel_logf(kernel, "sched dispatch pid=%d name=%s policy=%s", kernel->processes[selected].pid,
                        kernel->processes[selected].name, kernel_scheduler_name(kernel));
        }
        kernel->context_switches++;
        kernel->processes[selected].context_switches++;
    }
    kernel->current_index = selected;
    safe_copy(kernel->processes[selected].state, sizeof(kernel->processes[selected].state), "RUNNING");
    kernel->processes[selected].mlfq_wait_ticks = 0;
    kernel_account_ready_wait(kernel);
    if (kernel->scheduler_policy == KERNEL_SCHED_RR && kernel->quantum_left <= 0) {
        kernel->quantum_left = kernel->time_quantum;
    }
    if (kernel->scheduler_policy == KERNEL_SCHED_MLFQ && kernel->processes[selected].mlfq_ticks_left <= 0) {
        kernel->processes[selected].mlfq_ticks_left = kernel_mlfq_level_quantum(kernel, kernel->processes[selected].mlfq_level);
    }
    if (kernel->processes[selected].first_run_tick < 0) {
        kernel->processes[selected].first_run_tick = kernel->tick;
    }
    kernel->processes[selected].remaining--;
    kernel->processes[selected].cpu_time++;
    if (kernel->scheduler_policy == KERNEL_SCHED_RR) {
        kernel->quantum_left--;
    } else if (kernel->scheduler_policy == KERNEL_SCHED_MLFQ) {
        kernel->processes[selected].mlfq_ticks_left--;
    }
    kernel_logf(kernel, "proc run pid=%d remaining=%d", kernel->processes[selected].pid, kernel->processes[selected].remaining);
    if (kernel->processes[selected].remaining <= 0) {
        /* Process exit releases owned memory and can unblock MEM_WAIT processes. */
        int pid = kernel->processes[selected].pid;
        safe_copy(kernel->processes[selected].state, sizeof(kernel->processes[selected].state), "TERMINATED");
        kernel->processes[selected].finish_tick = kernel->tick;
        kernel->processes[selected].mlfq_ticks_left = 0;
        kernel_logf(kernel, "proc exit pid=%d", pid);
        kernel->current_index = -1;
        kernel->quantum_left = 0;
        (void)kernel_free(kernel, pid);
    }
}

int kernel_kill_process(MiniKernel *kernel, int pid) {
    KernelProcess *process = find_kernel_process(kernel, pid);
    if (process == NULL || strcmp(process->state, "TERMINATED") == 0) {
        return 0;
    }
    kernel_logf(kernel, "proc kill pid=%d", pid);
    process->remaining = 0;
    process->finish_tick = kernel->tick;
    process->mlfq_ticks_left = 0;
    safe_copy(process->state, sizeof(process->state), "TERMINATED");
    if (process->memory_size > 0) {
        (void)kernel_free(kernel, pid);
    } else {
        kernel_retry_memory_waiters(kernel);
    }
    if (kernel->current_index >= 0 && kernel->current_index < kernel->process_count &&
        kernel->processes[kernel->current_index].pid == pid) {
        kernel->current_index = -1;
        kernel->quantum_left = 0;
    }
    return 1;
}

int kernel_sleep_process(MiniKernel *kernel, int pid, int ticks) {
    KernelProcess *process = find_kernel_process(kernel, pid);
    if (process == NULL || ticks <= 0 || strcmp(process->state, "TERMINATED") == 0 ||
        strcmp(process->state, "MEM_WAIT") == 0) {
        return 0;
    }
    if (kernel->current_index >= 0 && kernel->current_index < kernel->process_count &&
        kernel->processes[kernel->current_index].pid == pid) {
        kernel->current_index = -1;
        kernel->quantum_left = 0;
    }
    process->wake_tick = kernel->tick + ticks;
    process->mlfq_ticks_left = 0;
    process->mlfq_wait_ticks = 0;
    safe_copy(process->state, sizeof(process->state), "BLOCKED");
    kernel_logf(kernel, "proc sleep pid=%d until_tick=%d", pid, process->wake_tick);
    return 1;
}

int kernel_record_page_access(MiniKernel *kernel, int pid, int page) {
    KernelProcess *process = find_kernel_process(kernel, pid);
    int slot;
    int i;
    int frame;
    if (kernel == NULL || process == NULL || page < 0 || strcmp(process->state, "TERMINATED") == 0) {
        return 0;
    }
    if (kernel->access_count < MAX_KERNEL_ACCESSES) {
        slot = kernel->access_count++;
    } else {
        for (i = 1; i < MAX_KERNEL_ACCESSES; i++) {
            kernel->access_pid[i - 1] = kernel->access_pid[i];
            kernel->access_page[i - 1] = kernel->access_page[i];
        }
        slot = MAX_KERNEL_ACCESSES - 1;
    }
    kernel->access_pid[slot] = pid;
    kernel->access_page[slot] = page;
    kernel->vm_clock++;
    frame = kernel_find_vm_frame(kernel, pid, page);
    if (frame >= 0) {
        kernel->vm_hits++;
        kernel->vm_last_used[frame] = kernel->vm_clock;
        kernel_logf(kernel, "vm hit pid=%d page=%d frame=%d policy=%s", pid, page, frame, kernel_vm_policy_name(kernel));
    } else {
        int old_pid;
        int old_page;
        frame = kernel_pick_vm_victim(kernel);
        old_pid = kernel->vm_frame_pid[frame];
        old_page = kernel->vm_frame_page[frame];
        kernel->vm_faults++;
        kernel->vm_frame_pid[frame] = pid;
        kernel->vm_frame_page[frame] = page;
        kernel->vm_loaded_at[frame] = kernel->vm_clock;
        kernel->vm_last_used[frame] = kernel->vm_clock;
        if (old_pid >= 0) {
            kernel_logf(kernel, "vm fault pid=%d page=%d frame=%d evict=P%d:%d policy=%s", pid, page, frame, old_pid,
                        old_page, kernel_vm_policy_name(kernel));
        } else {
            kernel_logf(kernel, "vm fault pid=%d page=%d frame=%d load policy=%s", pid, page, frame,
                        kernel_vm_policy_name(kernel));
        }
    }
    return 1;
}

int kernel_create_file(MiniKernel *kernel, const char *path, const char *content) {
    char message[MAX_MESSAGE_LEN];
    if (kernel == NULL) {
        return 0;
    }
    return fs_create(&kernel->fs, path, content, message, sizeof(message));
}

int kernel_write_file(MiniKernel *kernel, const char *path, const char *content) {
    char message[MAX_MESSAGE_LEN];
    if (kernel == NULL) {
        return 0;
    }
    return fs_write(&kernel->fs, path, content, 1, message, sizeof(message));
}

static void kernel_mark_unlinked_fds(MiniKernel *kernel, const char *path);

int kernel_delete_file(MiniKernel *kernel, const char *path) {
    char message[MAX_MESSAGE_LEN];
    int ok;
    if (kernel == NULL || path == NULL) {
        return 0;
    }
    kernel_mark_unlinked_fds(kernel, path);
    ok = fs_delete(&kernel->fs, path, message, sizeof(message));
    if (ok) {
        char normalized[MAX_PATH_LEN];
        kernel_path_name(path, normalized, sizeof(normalized));
        kernel_logf(kernel, "fs delete path=%s result=OK", normalized);
    }
    return ok;
}

static int kernel_fd_owner_exists(const MiniKernel *kernel, int owner_pid) {
    if (owner_pid == 0) {
        return 1;
    }
    return find_kernel_process_const(kernel, owner_pid) != NULL;
}

static KernelOpenFile *kernel_find_fd_for_owner(MiniKernel *kernel, int owner_pid, int fd) {
    int i;
    if (kernel == NULL || fd < 3) {
        return NULL;
    }
    for (i = 0; i < MAX_KERNEL_FDS; i++) {
        if (kernel->open_files[i].used && kernel->open_files[i].owner_pid == owner_pid && kernel->open_files[i].fd == fd) {
            return &kernel->open_files[i];
        }
    }
    return NULL;
}

static int kernel_next_fd_for_owner(const MiniKernel *kernel, int owner_pid) {
    int fd;
    int i;
    for (fd = 3; fd < 3 + MAX_KERNEL_FDS; fd++) {
        int used = 0;
        for (i = 0; i < MAX_KERNEL_FDS; i++) {
            if (kernel->open_files[i].used && kernel->open_files[i].owner_pid == owner_pid &&
                kernel->open_files[i].fd == fd) {
                used = 1;
                break;
            }
        }
        if (!used) {
            return fd;
        }
    }
    return -1;
}

static void kernel_mark_unlinked_fds(MiniKernel *kernel, const char *path) {
    char normalized[MAX_PATH_LEN];
    char content[MAX_CONTENT_LEN];
    char message[MAX_MESSAGE_LEN];
    int i;
    if (kernel == NULL || path == NULL) {
        return;
    }
    kernel_path_name(path, normalized, sizeof(normalized));
    for (i = 0; i < MAX_KERNEL_FDS; i++) {
        KernelOpenFile *file = &kernel->open_files[i];
        if (file->used && !file->deleted && strcmp(file->path, normalized) == 0) {
            if (fs_read(&kernel->fs, normalized, content, sizeof(content), message, sizeof(message))) {
                safe_copy(file->content, sizeof(file->content), content);
            }
            file->deleted = 1;
            kernel_logf(kernel, "fs unlink-open owner=%d fd=%d path=%s", file->owner_pid, file->fd, file->path);
        }
    }
}

static int kernel_mode_can_read(const char *mode) {
    return mode != NULL && strchr(mode, 'r') != NULL;
}

static int kernel_mode_can_write(const char *mode) {
    return mode != NULL && (strchr(mode, 'w') != NULL || strchr(mode, 'a') != NULL);
}

static int kernel_mode_is_valid(const char *mode) {
    return mode != NULL &&
           (strcmp(mode, "r") == 0 || strcmp(mode, "w") == 0 || strcmp(mode, "a") == 0 ||
            strcmp(mode, "rw") == 0 || strcmp(mode, "wr") == 0);
}

int kernel_open_file_for_process(MiniKernel *kernel, int owner_pid, const char *path, const char *mode) {
    char normalized[MAX_PATH_LEN];
    char content[MAX_CONTENT_LEN];
    char message[MAX_MESSAGE_LEN];
    const char *open_mode = mode == NULL || mode[0] == '\0' ? "r" : mode;
    int slot;
    int exists;
    if (kernel == NULL || path == NULL || !kernel_fd_owner_exists(kernel, owner_pid) || !kernel_mode_is_valid(open_mode)) {
        return -1;
    }
    for (slot = 0; slot < MAX_KERNEL_FDS; slot++) {
        if (!kernel->open_files[slot].used) {
            break;
        }
    }
    if (slot >= MAX_KERNEL_FDS) {
        kernel_path_name(path, normalized, sizeof(normalized));
        kernel_logf(kernel, "fs open owner=%d path=%s mode=%s FAIL fd_table_full", owner_pid, normalized, open_mode);
        return -1;
    }
    kernel_path_name(path, normalized, sizeof(normalized));
    exists = fs_read(&kernel->fs, normalized, content, sizeof(content), message, sizeof(message));
    if (!exists && strcmp(open_mode, "r") == 0) {
        kernel_logf(kernel, "fs open path=%s mode=%s FAIL", normalized, open_mode);
        return -1;
    }
    if (!exists && kernel_mode_can_write(open_mode) && !fs_create(&kernel->fs, normalized, "", message, sizeof(message))) {
        kernel_logf(kernel, "fs open path=%s mode=%s FAIL", normalized, open_mode);
        return -1;
    }
    if (exists && strcmp(open_mode, "w") == 0 && !fs_write(&kernel->fs, normalized, "", 0, message, sizeof(message))) {
        kernel_logf(kernel, "fs open truncate path=%s FAIL", normalized);
        return -1;
    }
    {
        KernelOpenFile *file = &kernel->open_files[slot];
        int fd = kernel_next_fd_for_owner(kernel, owner_pid);
        if (fd < 0) {
            kernel_logf(kernel, "fs open owner=%d path=%s mode=%s FAIL fd_namespace_full", owner_pid, normalized,
                        open_mode);
            return -1;
        }
        memset(file, 0, sizeof(*file));
        file->used = 1;
        file->owner_pid = owner_pid;
        file->fd = fd;
        safe_copy(file->path, sizeof(file->path), normalized);
        safe_copy(file->mode, sizeof(file->mode), open_mode);
        safe_copy(file->content, sizeof(file->content), strcmp(open_mode, "w") == 0 ? "" : content);
        file->offset = strchr(open_mode, 'a') != NULL && exists ? (int)strlen(content) : 0;
        kernel_logf(kernel, "fs open fd=%d path=%s mode=%s offset=%d owner=%d", file->fd, file->path, file->mode,
                    file->offset, file->owner_pid);
        return file->fd;
    }
}

int kernel_open_file(MiniKernel *kernel, const char *path, const char *mode) {
    return kernel_open_file_for_process(kernel, 0, path, mode);
}

int kernel_read_fd(MiniKernel *kernel, int fd, char *out, size_t out_size) {
    return kernel_read_fd_count(kernel, fd, -1, out, out_size);
}

int kernel_read_fd_count(MiniKernel *kernel, int fd, int max_bytes, char *out, size_t out_size) {
    return kernel_read_fd_count_for_process(kernel, 0, fd, max_bytes, out, out_size);
}

int kernel_read_fd_count_for_process(MiniKernel *kernel, int owner_pid, int fd, int max_bytes, char *out,
                                     size_t out_size) {
    KernelOpenFile *file = kernel_find_fd_for_owner(kernel, owner_pid, fd);
    char content[MAX_CONTENT_LEN];
    char message[MAX_MESSAGE_LEN];
    size_t length;
    size_t available;
    size_t to_copy;
    if (file == NULL || out == NULL || out_size == 0 || max_bytes < -1 || !kernel_mode_can_read(file->mode)) {
        return -1;
    }
    if (file->deleted) {
        safe_copy(content, sizeof(content), file->content);
    } else if (fs_read(&kernel->fs, file->path, content, sizeof(content), message, sizeof(message))) {
        safe_copy(file->content, sizeof(file->content), content);
    } else {
        file->deleted = 1;
        safe_copy(content, sizeof(content), file->content);
    }
    length = strlen(content);
    if (file->offset < 0) {
        file->offset = 0;
    }
    if ((size_t)file->offset >= length) {
        safe_copy(out, out_size, "");
        to_copy = 0;
    } else {
        available = length - (size_t)file->offset;
        to_copy = available;
        if (max_bytes >= 0 && (size_t)max_bytes < to_copy) {
            to_copy = (size_t)max_bytes;
        }
        if (to_copy >= out_size) {
            to_copy = out_size - 1;
        }
        memcpy(out, content + file->offset, to_copy);
        out[to_copy] = '\0';
        file->offset += (int)to_copy;
    }
    kernel_logf(kernel, "fs readfd owner=%d fd=%d bytes=%zu offset=%d", owner_pid, fd, strlen(out), file->offset);
    return (int)to_copy;
}

int kernel_write_fd(MiniKernel *kernel, int fd, const char *content) {
    return kernel_write_fd_for_process(kernel, 0, fd, content);
}

int kernel_write_fd_for_process(MiniKernel *kernel, int owner_pid, int fd, const char *content) {
    KernelOpenFile *file = kernel_find_fd_for_owner(kernel, owner_pid, fd);
    char message[MAX_MESSAGE_LEN];
    char current[MAX_CONTENT_LEN];
    char combined[MAX_CONTENT_LEN];
    size_t current_len;
    size_t content_len;
    size_t offset;
    size_t suffix_offset;
    size_t suffix_len;
    if (file == NULL || content == NULL || !kernel_mode_can_write(file->mode)) {
        return -1;
    }
    if (file->deleted) {
        safe_copy(current, sizeof(current), file->content);
    } else if (fs_read(&kernel->fs, file->path, current, sizeof(current), message, sizeof(message))) {
        safe_copy(file->content, sizeof(file->content), current);
    } else {
        file->deleted = 1;
        safe_copy(current, sizeof(current), file->content);
    }
    current_len = strlen(current);
    content_len = strlen(content);
    if (strchr(file->mode, 'a') != NULL) {
        if (!file->deleted && !fs_write(&kernel->fs, file->path, content, 1, message, sizeof(message))) {
            kernel_logf(kernel, "fs writefd fd=%d path=%s FAIL", fd, file->path);
            return -1;
        }
        if (file->deleted) {
            if (current_len + content_len >= sizeof(file->content)) {
                kernel_logf(kernel, "fs writefd fd=%d path=%s FAIL content_too_large", fd, file->path);
                return -1;
            }
            (void)snprintf(file->content, sizeof(file->content), "%s%s", current, content);
            file->offset = (int)strlen(file->content);
        } else if (fs_read(&kernel->fs, file->path, current, sizeof(current), message, sizeof(message))) {
            safe_copy(file->content, sizeof(file->content), current);
            file->offset = (int)strlen(current);
        }
        kernel_logf(kernel, "fs writefd owner=%d fd=%d bytes=%zu offset=%d", owner_pid, fd, content_len,
                    file->offset);
        return (int)content_len;
    }
    if (file->offset < 0) {
        file->offset = 0;
    }
    offset = (size_t)file->offset;
    if (offset > current_len) {
        offset = current_len;
    }
    suffix_offset = offset + content_len;
    suffix_len = suffix_offset < current_len ? current_len - suffix_offset : 0;
    if (offset + content_len + suffix_len >= sizeof(combined)) {
        kernel_logf(kernel, "fs writefd fd=%d path=%s FAIL content_too_large", fd, file->path);
        return -1;
    }
    memcpy(combined, current, offset);
    memcpy(combined + offset, content, content_len);
    if (suffix_len > 0) {
        memcpy(combined + offset + content_len, current + suffix_offset, suffix_len);
    }
    combined[offset + content_len + suffix_len] = '\0';
    safe_copy(file->content, sizeof(file->content), combined);
    if (!file->deleted && !fs_write(&kernel->fs, file->path, combined, 0, message, sizeof(message))) {
        kernel_logf(kernel, "fs writefd fd=%d path=%s FAIL", fd, file->path);
        return -1;
    }
    file->offset = (int)(offset + content_len);
    kernel_logf(kernel, "fs writefd owner=%d fd=%d bytes=%zu offset=%d", owner_pid, fd, content_len, file->offset);
    return (int)content_len;
}

int kernel_seek_fd(MiniKernel *kernel, int fd, int offset) {
    return kernel_seek_fd_for_process(kernel, 0, fd, offset);
}

int kernel_seek_fd_for_process(MiniKernel *kernel, int owner_pid, int fd, int offset) {
    KernelOpenFile *file = kernel_find_fd_for_owner(kernel, owner_pid, fd);
    char content[MAX_CONTENT_LEN];
    char message[MAX_MESSAGE_LEN];
    size_t length;
    if (file == NULL || offset < 0) {
        return 0;
    }
    if (file->deleted) {
        safe_copy(content, sizeof(content), file->content);
    } else if (fs_read(&kernel->fs, file->path, content, sizeof(content), message, sizeof(message))) {
        safe_copy(file->content, sizeof(file->content), content);
    } else {
        file->deleted = 1;
        safe_copy(content, sizeof(content), file->content);
    }
    length = strlen(content);
    file->offset = offset > (int)length ? (int)length : offset;
    kernel_logf(kernel, "fs seekfd owner=%d fd=%d offset=%d", owner_pid, fd, file->offset);
    return 1;
}

int kernel_close_fd(MiniKernel *kernel, int fd) {
    return kernel_close_fd_for_process(kernel, 0, fd);
}

int kernel_close_fd_for_process(MiniKernel *kernel, int owner_pid, int fd) {
    KernelOpenFile *file = kernel_find_fd_for_owner(kernel, owner_pid, fd);
    if (file == NULL) {
        return 0;
    }
    kernel_logf(kernel, "fs close owner=%d fd=%d path=%s%s", owner_pid, fd, file->path,
                file->deleted ? " deleted" : "");
    memset(file, 0, sizeof(*file));
    return 1;
}

int syscall_dispatch(MiniKernel *kernel, const char *call, const char *arg1, const char *arg2) {
    if (kernel == NULL || call == NULL) {
        return 0;
    }
    if (strcmp(call, "create_process") == 0) {
        int burst = 1;
        if (arg2 != NULL && (!parse_int_value(arg2, &burst) || burst <= 0)) {
            return 0;
        }
        return kernel_create_process(kernel, arg1 == NULL ? "proc" : arg1, burst, 8) > 0;
    }
    if (strcmp(call, "mkdir") == 0) {
        char message[MAX_MESSAGE_LEN];
        return fs_mkdir(&kernel->fs, arg1, message, sizeof(message));
    }
    if (strcmp(call, "create_file") == 0) {
        return kernel_create_file(kernel, arg1, arg2 == NULL ? "" : arg2);
    }
    if (strcmp(call, "write_file") == 0) {
        return kernel_write_file(kernel, arg1, arg2 == NULL ? "" : arg2);
    }
    if (strcmp(call, "open") == 0) {
        return kernel_open_file(kernel, arg1, arg2 == NULL ? "r" : arg2);
    }
    if (strcmp(call, "close") == 0) {
        int fd;
        if (!parse_int_value(arg1, &fd)) {
            return 0;
        }
        return kernel_close_fd(kernel, fd);
    }
    if (strcmp(call, "seek") == 0 || strcmp(call, "seekfd") == 0) {
        int fd;
        int offset;
        if (!parse_int_value(arg1, &fd) || !parse_int_value(arg2, &offset)) {
            return 0;
        }
        return kernel_seek_fd(kernel, fd, offset);
    }
    if (strcmp(call, "tick") == 0) {
        kernel_schedule_tick(kernel);
        return 1;
    }
    if (strcmp(call, "kill") == 0) {
        int pid;
        if (!parse_int_value(arg1, &pid)) {
            return 0;
        }
        return kernel_kill_process(kernel, pid);
    }
    if (strcmp(call, "sleep") == 0) {
        int pid;
        int ticks = 1;
        if (!parse_int_value(arg1, &pid) || (arg2 != NULL && (!parse_int_value(arg2, &ticks) || ticks <= 0))) {
            return 0;
        }
        return kernel_sleep_process(kernel, pid, ticks);
    }
    return 0;
}

void kernel_status(const MiniKernel *kernel) {
    int i;
    printf("Kernel tick=%d process_count=%d scheduler=%s quantum=%d qleft=%d ctx_switches=%d idle_ticks=%d access_count=%d logs=%d\n",
           kernel->tick, kernel->process_count, kernel_scheduler_name(kernel), kernel->time_quantum, kernel->quantum_left,
           kernel->context_switches, kernel->idle_ticks, kernel->access_count, kernel->log_count);
    printf("%-5s %-12s %-8s %-9s %-6s %-4s %-6s %-5s %-8s %-8s %-7s %-7s %-7s %-8s %-10s\n", "PID", "Name",
           "Burst", "Remain", "Prio", "Q", "Slice", "Age", "Mem", "NeedMem", "CPU", "Wait", "Ctx", "Wake", "State");
    for (i = 0; i < kernel->process_count; i++) {
        const KernelProcess *process = find_kernel_process_const(kernel, kernel->processes[i].pid);
        if (process != NULL) {
            char wake_text[16];
            if (process->wake_tick >= 0) {
                (void)snprintf(wake_text, sizeof(wake_text), "%d", process->wake_tick);
            } else {
                safe_copy(wake_text, sizeof(wake_text), "-");
            }
            printf("%-5d %-12s %-8d %-9d %-6d %-4d %-6d %-5d %-8d %-8d %-7d %-7d %-7d %-8s %-10s\n", process->pid,
                   process->name, process->burst, process->remaining, process->priority, process->mlfq_level,
                   process->mlfq_ticks_left, process->mlfq_wait_ticks, process->memory_size, process->requested_memory,
                   process->cpu_time, process->wait_time, process->context_switches, wake_text, process->state);
        }
    }
    printf("\nKernel VM state: policy=%s frames=%d faults=%d hits=%d fault_rate=%.1f%%\n", kernel_vm_policy_name(kernel),
           kernel_vm_frame_count(kernel), kernel->vm_faults, kernel->vm_hits,
           kernel->access_count > 0 ? (double)kernel->vm_faults * 100.0 / kernel->access_count : 0.0);
    printf("%-7s %-8s %-8s %-8s %-8s\n", "Frame", "PID", "Page", "Loaded", "LastUse");
    for (i = 0; i < kernel_vm_frame_count(kernel); i++) {
        if (kernel->vm_frame_pid[i] < 0) {
            printf("%-7d %-8s %-8s %-8s %-8s\n", i, "-", "-", "-", "-");
        } else {
            printf("%-7d %-8d %-8d %-8d %-8d\n", i, kernel->vm_frame_pid[i], kernel->vm_frame_page[i],
                   kernel->vm_loaded_at[i], kernel->vm_last_used[i]);
        }
    }
    printf("\nKernel open file table:\n");
    printf("%-7s %-5s %-8s %-8s %-9s %-8s\n", "Owner", "FD", "Mode", "Offset", "State", "Path");
    for (i = 0; i < MAX_KERNEL_FDS; i++) {
        if (kernel->open_files[i].used) {
            printf("%-7d %-5d %-8s %-8d %-9s %-8s\n", kernel->open_files[i].owner_pid, kernel->open_files[i].fd,
                   kernel->open_files[i].mode, kernel->open_files[i].offset,
                   kernel->open_files[i].deleted ? "deleted" : "linked", kernel->open_files[i].path);
        }
    }
    printf("\nKernel memory state:\n");
    print_partition_summary(&kernel->memory);
    printf("\nKernel file state:\n");
    fs_print_tree(&kernel->fs);
    fs_print_bitmap(&kernel->fs);
}

void run_kernel_demo(void) {
    MiniKernel kernel;
    int i;
    kernel_init(&kernel);
    printf("用户态简易内核演示：初始化 kernel、进程表、内存管理器、文件管理器和 syscall 分发层。\n");
    (void)syscall_dispatch(&kernel, "create_process", "shell", "3");
    (void)syscall_dispatch(&kernel, "create_process", "worker", "4");
    (void)syscall_dispatch(&kernel, "mkdir", "/kernel", NULL);
    (void)syscall_dispatch(&kernel, "create_file", "/kernel/log.txt", "boot");
    (void)syscall_dispatch(&kernel, "write_file", "/kernel/log.txt", " -> ticks");
    for (i = 0; i < 6; i++) {
        (void)syscall_dispatch(&kernel, "tick", NULL, NULL);
    }
    kernel_status(&kernel);
}
