#include "os_project.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * MiniOS state workloads.
 * tracebench and related exports replay fixed scheduling, VM, memory,
 * TinyFS, fd, and synchronization scenarios inside the user-space MiniKernel.
 * The numbers are for coursework analysis, not official benchmark results.
 */

typedef struct {
    int total_items;
    int next_item;
    int consumed;
    int buffer[64];
    int capacity;
    int in;
    int out;
    sem_t empty;
    sem_t full;
    pthread_mutex_t mutex;
} PCBenchContext;

typedef struct {
    PCBenchContext *ctx;
} PCBenchArg;

typedef struct {
    char name[32];
    double average_wait;
    double average_response;
    double average_turnaround;
    int ticks;
    int context_switches;
    double score;
} SchedulerTuneResult;

typedef struct {
    char name[32];
    int frames;
    int faults;
    double fault_rate;
    double score;
} VMTuneResult;

typedef struct {
    int total_size;
    int allocation_attempts;
    int allocated;
    int freed;
    int block_count;
    int free_total;
    int largest_free;
    int external_fragmentation;
} MemoryStateResult;

typedef struct {
    int target_files;
    int created;
    int writes;
    int deleted;
    int remaining_files;
    int free_blocks;
} FileStateResult;

static void *pc_bench_producer(void *arg) {
    PCBenchContext *ctx = ((PCBenchArg *)arg)->ctx;
    for (;;) {
        int item;
        pthread_mutex_lock(&ctx->mutex);
        if (ctx->next_item >= ctx->total_items) {
            pthread_mutex_unlock(&ctx->mutex);
            break;
        }
        item = ctx->next_item++;
        pthread_mutex_unlock(&ctx->mutex);

        /* Exercise the same semaphore/mutex path as the producer-consumer case. */
        sem_wait(&ctx->empty);
        pthread_mutex_lock(&ctx->mutex);
        ctx->buffer[ctx->in] = item;
        ctx->in = (ctx->in + 1) % ctx->capacity;
        pthread_mutex_unlock(&ctx->mutex);
        sem_post(&ctx->full);
    }
    return NULL;
}

static void *pc_bench_consumer(void *arg) {
    PCBenchContext *ctx = ((PCBenchArg *)arg)->ctx;
    for (;;) {
        int item;
        sem_wait(&ctx->full);
        pthread_mutex_lock(&ctx->mutex);
        item = ctx->buffer[ctx->out];
        ctx->out = (ctx->out + 1) % ctx->capacity;
        if (item >= 0) {
            ctx->consumed++;
        }
        pthread_mutex_unlock(&ctx->mutex);
        sem_post(&ctx->empty);
        if (item < 0) {
            break;
        }
    }
    return NULL;
}

static double elapsed_ms(clock_t start, clock_t end) {
    return (double)(end - start) * 1000.0 / (double)CLOCKS_PER_SEC;
}

static int bounded(int value, int min_value, int max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static MiniKernel *new_benchmark_kernel(void) {
    MiniKernel *kernel = (MiniKernel *)calloc(1, sizeof(*kernel));
    if (kernel != NULL) {
        kernel_init(kernel);
    }
    return kernel;
}

static void generate_processes(Process *processes, int count) {
    int i;
    for (i = 0; i < count; i++) {
        (void)snprintf(processes[i].pid, sizeof(processes[i].pid), "P%d", i + 1);
        processes[i].arrival = (i * 3) % (count / 2 + 1);
        processes[i].burst = (i * 7) % 9 + 1;
        processes[i].priority = (i * 5) % 5 + 1;
    }
}

static void generate_references(int *refs, int ref_count) {
    int i;
    static const int pattern[] = {7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2};
    for (i = 0; i < ref_count; i++) {
        refs[i] = pattern[i % (int)(sizeof(pattern) / sizeof(pattern[0]))];
    }
}

static int kernel_all_done(const MiniKernel *kernel) {
    int i;
    for (i = 0; i < kernel->process_count; i++) {
        if (strcmp(kernel->processes[i].state, "TERMINATED") != 0) {
            return 0;
        }
    }
    return 1;
}

static SchedulerTuneResult simulate_kernel_scheduler(const char *name, const char *policy, int quantum,
                                                     const Process *processes, int count) {
    SchedulerTuneResult result;
    MiniKernel *kernel;
    int i;
    int safety_ticks = 0;
    memset(&result, 0, sizeof(result));
    safe_copy(result.name, sizeof(result.name), name);
    kernel = new_benchmark_kernel();
    if (kernel == NULL) {
        return result;
    }
    for (i = 0; i < count && i < MAX_KERNEL_PROCESSES; i++) {
        (void)kernel_create_process_with_priority(kernel, processes[i].pid, processes[i].burst, 1, processes[i].priority);
    }
    (void)kernel_set_scheduler(kernel, policy, quantum);
    while (!kernel_all_done(kernel) && safety_ticks < 10000) {
        kernel_schedule_tick(kernel);
        safety_ticks++;
    }
    for (i = 0; i < kernel->process_count; i++) {
        const KernelProcess *process = &kernel->processes[i];
        int response = process->first_run_tick >= 0 ? process->first_run_tick - process->arrival_tick : kernel->tick;
        int turnaround = process->finish_tick >= 0 ? process->finish_tick - process->arrival_tick : kernel->tick;
        result.average_wait += process->wait_time;
        result.average_response += response;
        result.average_turnaround += turnaround;
    }
    if (kernel->process_count > 0) {
        result.average_wait /= kernel->process_count;
        result.average_response /= kernel->process_count;
        result.average_turnaround /= kernel->process_count;
    }
    result.ticks = kernel->tick;
    result.context_switches = kernel->context_switches;
    result.score = result.average_response * 0.45 + result.average_wait * 0.35 +
                   result.average_turnaround * 0.10 + (double)result.context_switches * 0.10;
    free(kernel);
    return result;
}

static int find_best_scheduler(const SchedulerTuneResult *results, int count) {
    int best = 0;
    int i;
    for (i = 1; i < count; i++) {
        if (results[i].score < results[best].score) {
            best = i;
        }
    }
    return best;
}

static VMTuneResult evaluate_vm_policy(const char *name, const int *refs, int ref_count, int frames, int use_lru) {
    VMTuneResult result;
    PageReport report = use_lru ? page_lru(refs, ref_count, frames) : page_fifo(refs, ref_count, frames);
    memset(&result, 0, sizeof(result));
    safe_copy(result.name, sizeof(result.name), name);
    result.frames = frames;
    result.faults = report.faults;
    result.fault_rate = report.fault_rate;
    result.score = report.fault_rate * 100.0 + (double)frames * 0.25;
    return result;
}

static int find_best_vm(const VMTuneResult *results, int count) {
    int best = 0;
    int i;
    for (i = 1; i < count; i++) {
        if (results[i].score < results[best].score) {
            best = i;
        }
    }
    return best;
}

static void write_metric_row(FILE *file, const char *category, const char *name, int scale, int iterations,
                             const char *metric_name, double metric_value, const char *unit, const char *notes) {
    (void)fprintf(file, "%s,%s,%d,%d,%s,%.6f,%s,%s\n", category, name, scale, iterations, metric_name,
                  metric_value, unit == NULL ? "" : unit, notes == NULL ? "" : notes);
}

typedef struct {
    char name[32];
    int ticks;
    int completed;
    int context_switches;
    int idle_ticks;
    double average_wait;
    double average_response;
    double average_turnaround;
    int irq_wakeups;
    int mem_wait_wakeups;
    int mlfq_events;
    int vm_faults;
    int vm_hits;
    int fs_ops;
    int fs_events;
} TraceSchedResult;

typedef struct {
    char policy[8];
    int frames;
    int references;
    int faults;
    int hits;
    double fault_rate;
} TraceVMResult;

typedef struct {
    char name[32];
    int files;
    int writes_per_file;
    int created;
    int writes;
    int fd_ops;
    int free_blocks;
    int remaining_files;
} TraceFSResult;

static int trace_log_count(const MiniKernel *kernel, const char *needle) {
    int count = 0;
    int i;
    if (kernel == NULL || needle == NULL) {
        return 0;
    }
    for (i = 0; i < kernel->log_count; i++) {
        if (strstr(kernel->logs[i], needle) != NULL) {
            count++;
        }
    }
    return count;
}

static int trace_free_blocks(const TinyFS *fs) {
    int free_blocks = 0;
    int i;
    if (fs == NULL) {
        return 0;
    }
    for (i = 0; i < fs->total_blocks; i++) {
        if (!fs->used[i]) {
            free_blocks++;
        }
    }
    return free_blocks;
}

static void trace_run_until_done(MiniKernel *kernel, int max_ticks) {
    int guard = 0;
    while (!kernel_all_done(kernel) && guard < max_ticks) {
        kernel_schedule_tick(kernel);
        guard++;
    }
}

static void trace_access_workload(MiniKernel *kernel, const int *pids, int pid_count) {
    static const int refs[][2] = {
        {0, 0}, {1, 0}, {0, 1}, {1, 1}, {0, 0}, {1, 0}, {2, 0}, {0, 2},
        {0, 0}, {1, 1}, {2, 0}, {0, 1}, {1, 2}, {0, 0}, {1, 1}, {2, 0},
    };
    int i;
    for (i = 0; i < (int)(sizeof(refs) / sizeof(refs[0])); i++) {
        int pid_index = refs[i][0];
        if (pid_index >= 0 && pid_index < pid_count) {
            (void)kernel_record_page_access(kernel, pids[pid_index], refs[i][1]);
        }
    }
}

static TraceSchedResult run_trace_scheduler_config(const char *name, const char *policy, int quantum) {
    TraceSchedResult result;
    MiniKernel *kernel;
    char message[MAX_MESSAGE_LEN];
    char read_buffer[MAX_CONTENT_LEN];
    int pids[5];
    int fd;
    int i;
    int completed = 0;

    memset(&result, 0, sizeof(result));
    safe_copy(result.name, sizeof(result.name), name);
    kernel = new_benchmark_kernel();
    if (kernel == NULL) {
        return result;
    }
    pids[0] = kernel_create_process_with_priority(kernel, "shell", 6, 12, 2);
    pids[1] = kernel_create_process_with_priority(kernel, "editor", 7, 28, 3);
    pids[2] = kernel_create_process_with_priority(kernel, "compiler", 9, 36, 1);
    pids[3] = kernel_create_process_with_priority(kernel, "logger", 5, 16, 1);
    pids[4] = kernel_create_process_with_priority(kernel, "renderer", 4, 70, 1);

    if (fs_mkdir(&kernel->fs, "/trace", message, sizeof(message))) {
        result.fs_ops++;
    }
    if (fs_mkdir(&kernel->fs, "/trace/logs", message, sizeof(message))) {
        result.fs_ops++;
    }
    if (kernel_create_file(kernel, "/trace/logs/run.txt", "boot")) {
        result.fs_ops++;
    }
    if (kernel_write_file(kernel, "/trace/logs/run.txt", "-tick")) {
        result.fs_ops++;
    }
    fd = kernel_open_file(kernel, "/trace/logs/run.txt", "a");
    if (fd >= 3) {
        result.fs_ops++;
        if (kernel_write_fd(kernel, fd, "-fd") >= 0) {
            result.fs_ops++;
        }
        if (kernel_close_fd(kernel, fd)) {
            result.fs_ops++;
        }
    }
    fd = kernel_open_file(kernel, "/trace/logs/run.txt", "r");
    if (fd >= 3) {
        result.fs_ops++;
        if (kernel_read_fd_count(kernel, fd, 64, read_buffer, sizeof(read_buffer)) >= 0) {
            result.fs_ops++;
        }
        if (kernel_close_fd(kernel, fd)) {
            result.fs_ops++;
        }
    }

    trace_access_workload(kernel, pids, 3);
    (void)kernel_set_scheduler(kernel, policy, quantum);
    for (i = 0; i < 4; i++) {
        kernel_schedule_tick(kernel);
    }
    (void)kernel_sleep_process(kernel, pids[1], 3);
    for (i = 0; i < 3; i++) {
        kernel_schedule_tick(kernel);
    }
    trace_run_until_done(kernel, 160);

    for (i = 0; i < kernel->process_count; i++) {
        const KernelProcess *process = &kernel->processes[i];
        if (strcmp(process->state, "TERMINATED") == 0) {
            int response = process->first_run_tick >= 0 ? process->first_run_tick - process->arrival_tick : kernel->tick;
            int turnaround = process->finish_tick >= 0 ? process->finish_tick - process->arrival_tick : kernel->tick;
            result.average_wait += process->wait_time;
            result.average_response += response;
            result.average_turnaround += turnaround;
            completed++;
        }
    }
    result.completed = completed;
    if (completed > 0) {
        result.average_wait /= completed;
        result.average_response /= completed;
        result.average_turnaround /= completed;
    }
    result.ticks = kernel->tick;
    result.context_switches = kernel->context_switches;
    result.idle_ticks = kernel->idle_ticks;
    result.irq_wakeups = trace_log_count(kernel, "irq wake");
    result.mem_wait_wakeups = trace_log_count(kernel, "mm wake");
    result.mlfq_events = trace_log_count(kernel, "sched mlfq");
    result.vm_faults = kernel->vm_faults;
    result.vm_hits = kernel->vm_hits;
    result.fs_events = trace_log_count(kernel, "fs ");
    free(kernel);
    return result;
}

static TraceVMResult run_trace_vm_config(const char *policy, int frames) {
    TraceVMResult result;
    MiniKernel *kernel;
    int pids[3];
    memset(&result, 0, sizeof(result));
    safe_copy(result.policy, sizeof(result.policy), policy);
    result.frames = frames;
    kernel = new_benchmark_kernel();
    if (kernel == NULL) {
        return result;
    }
    kernel->page_frames = frames;
    (void)kernel_set_vm_policy(kernel, policy);
    pids[0] = kernel_create_process_with_priority(kernel, "shell", 6, 8, 2);
    pids[1] = kernel_create_process_with_priority(kernel, "editor", 7, 8, 3);
    pids[2] = kernel_create_process_with_priority(kernel, "compiler", 9, 8, 1);
    trace_access_workload(kernel, pids, 3);
    result.references = kernel->access_count;
    result.faults = kernel->vm_faults;
    result.hits = kernel->vm_hits;
    result.fault_rate = result.references > 0 ? (double)result.faults * 100.0 / (double)result.references : 0.0;
    free(kernel);
    return result;
}

static TraceFSResult run_trace_fs_config(const char *name, int files, int writes_per_file) {
    TraceFSResult result;
    MiniKernel *kernel;
    char message[MAX_MESSAGE_LEN];
    int i;
    int j;
    int fd;
    memset(&result, 0, sizeof(result));
    safe_copy(result.name, sizeof(result.name), name);
    result.files = files;
    result.writes_per_file = writes_per_file;
    kernel = new_benchmark_kernel();
    if (kernel == NULL) {
        return result;
    }
    (void)fs_mkdir(&kernel->fs, "/trace", message, sizeof(message));
    for (i = 0; i < files; i++) {
        char path[MAX_PATH_LEN];
        (void)snprintf(path, sizeof(path), "/trace/f%d.txt", i);
        if (kernel_create_file(kernel, path, "seed")) {
            result.created++;
        }
        for (j = 0; j < writes_per_file; j++) {
            if (kernel_write_file(kernel, path, "-append")) {
                result.writes++;
            }
        }
    }
    fd = kernel_open_file(kernel, "/trace/f0.txt", "a");
    if (fd >= 3) {
        result.fd_ops++;
        if (kernel_write_fd(kernel, fd, "-fd") >= 0) {
            result.fd_ops++;
        }
        if (kernel_close_fd(kernel, fd)) {
            result.fd_ops++;
        }
    }
    result.free_blocks = trace_free_blocks(&kernel->fs);
    result.remaining_files = kernel->fs.file_count;
    free(kernel);
    return result;
}

static MemoryStateResult run_memory_state_workload(int scale) {
    MemoryStateResult result;
    FirstFitMemory *memory;
    int i;
    int attempts = bounded(scale, 4, MAX_MEMORY_BLOCKS / 2);

    memset(&result, 0, sizeof(result));
    result.total_size = scale * 8;
    memory = (FirstFitMemory *)calloc(1, sizeof(*memory));
    if (memory == NULL) {
        return result;
    }
    ff_init(memory, result.total_size);
    result.allocation_attempts = attempts;
    for (i = 0; i < attempts; i++) {
        char pid[MAX_PID_LEN];
        PartitionEvent event;
        int size = (i * 5) % 17 + 1;
        (void)snprintf(pid, sizeof(pid), "M%d", i);
        event = ff_allocate(memory, pid, size);
        if (event.success) {
            result.allocated++;
        }
    }
    for (i = 1; i < attempts; i += 3) {
        char pid[MAX_PID_LEN];
        PartitionEvent event;
        (void)snprintf(pid, sizeof(pid), "M%d", i);
        event = ff_free(memory, pid);
        if (event.success) {
            result.freed++;
        }
    }
    result.block_count = memory->block_count;
    result.free_total = ff_free_size(memory);
    result.external_fragmentation = ff_external_fragmentation(memory);
    for (i = 0; i < memory->block_count; i++) {
        if (memory->blocks[i].free && memory->blocks[i].size > result.largest_free) {
            result.largest_free = memory->blocks[i].size;
        }
    }
    free(memory);
    return result;
}

static FileStateResult run_file_state_workload(int scale) {
    FileStateResult result;
    TinyFS fs;
    char message[MAX_MESSAGE_LEN];
    int i;
    int files = bounded(scale, 4, MAX_FS_FILES);

    memset(&result, 0, sizeof(result));
    result.target_files = files;
    fs_init(&fs, MAX_FS_BLOCKS, 32);
    for (i = 0; i < files; i++) {
        char name[MAX_PATH_LEN];
        (void)snprintf(name, sizeof(name), "f%d.txt", i);
        if (fs_create(&fs, name, "data", message, sizeof(message))) {
            result.created++;
        }
        if (fs_write(&fs, name, "+append", 1, message, sizeof(message))) {
            result.writes++;
        }
    }
    for (i = 0; i < files; i += 3) {
        char name[MAX_PATH_LEN];
        (void)snprintf(name, sizeof(name), "f%d.txt", i);
        if (fs_delete(&fs, name, message, sizeof(message))) {
            result.deleted++;
        }
    }
    result.remaining_files = fs.file_count;
    result.free_blocks = trace_free_blocks(&fs);
    return result;
}

static double run_pc_benchmark(int producers, int consumers, int buffer_size, int total_items, double *elapsed) {
    PCBenchContext ctx;
    PCBenchArg arg;
    pthread_t producer_threads[8];
    pthread_t consumer_threads[8];
    clock_t start;
    clock_t end;
    int i;

    if (producers > 8) {
        producers = 8;
    }
    if (consumers > 8) {
        consumers = 8;
    }
    if (buffer_size > 64) {
        buffer_size = 64;
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.total_items = total_items;
    ctx.capacity = buffer_size;
    sem_init(&ctx.empty, 0, (unsigned int)buffer_size);
    sem_init(&ctx.full, 0, 0);
    pthread_mutex_init(&ctx.mutex, NULL);
    arg.ctx = &ctx;

    start = clock();
    for (i = 0; i < consumers; i++) {
        pthread_create(&consumer_threads[i], NULL, pc_bench_consumer, &arg);
    }
    for (i = 0; i < producers; i++) {
        pthread_create(&producer_threads[i], NULL, pc_bench_producer, &arg);
    }
    for (i = 0; i < producers; i++) {
        pthread_join(producer_threads[i], NULL);
    }
    for (i = 0; i < consumers; i++) {
        /* Use one stop token per consumer so every thread can exit cleanly. */
        sem_wait(&ctx.empty);
        pthread_mutex_lock(&ctx.mutex);
        ctx.buffer[ctx.in] = -1;
        ctx.in = (ctx.in + 1) % ctx.capacity;
        pthread_mutex_unlock(&ctx.mutex);
        sem_post(&ctx.full);
    }
    for (i = 0; i < consumers; i++) {
        pthread_join(consumer_threads[i], NULL);
    }
    end = clock();

    pthread_mutex_destroy(&ctx.mutex);
    sem_destroy(&ctx.empty);
    sem_destroy(&ctx.full);
    *elapsed = elapsed_ms(start, end);
    return *elapsed > 0.0 ? (double)ctx.consumed / (*elapsed / 1000.0) : 0.0;
}

void run_tracebench_suite(void) {
    TraceSchedResult sched_results[6];
    TraceVMResult vm_results[6];
    TraceFSResult fs_results[3];
    int i;

    sched_results[0] = run_trace_scheduler_config("RR q=1", "rr", 1);
    sched_results[1] = run_trace_scheduler_config("RR q=2", "rr", 2);
    sched_results[2] = run_trace_scheduler_config("SJF", "sjf", 2);
    sched_results[3] = run_trace_scheduler_config("Priority", "priority", 2);
    sched_results[4] = run_trace_scheduler_config("MLFQ q=1", "mlfq", 1);
    sched_results[5] = run_trace_scheduler_config("MLFQ q=2", "mlfq", 2);

    vm_results[0] = run_trace_vm_config("fifo", 3);
    vm_results[1] = run_trace_vm_config("lru", 3);
    vm_results[2] = run_trace_vm_config("fifo", 4);
    vm_results[3] = run_trace_vm_config("lru", 4);
    vm_results[4] = run_trace_vm_config("fifo", 5);
    vm_results[5] = run_trace_vm_config("lru", 5);

    fs_results[0] = run_trace_fs_config("small", 4, 1);
    fs_results[1] = run_trace_fs_config("medium", 8, 2);
    fs_results[2] = run_trace_fs_config("larger", 12, 3);

    printf("MiniOS tracebench: 多组模拟工作负载行为指标\n");
    printf("说明: tracebench 统计 MiniKernel 的 tick、PCB、上下文切换、MEM_WAIT、VM、TinyFS 和 fd 状态变化；");
    printf("不测真实 OS 调度延迟，也不代表硬件性能。\n\n");

    printf("[1] 调度/内存/VM/TinyFS 混合工作负载\n");
    printf("工作负载: 5 个 PCB、1 个 MEM_WAIT 进程、1 次 I/O sleep、16 次在线 VM 访问和一组 TinyFS/fd 操作。\n");
    printf("%-12s %-6s %-6s %-6s %-8s %-8s %-8s %-7s %-7s %-7s %-7s %-6s\n", "Policy", "Ticks",
           "Done", "Ctx", "AvgWait", "AvgResp", "AvgTurn", "IRQ", "MEMWK", "VMF", "VMH", "FSOps");
    for (i = 0; i < 6; i++) {
        printf("%-12s %-6d %-6d %-6d %-8.2f %-8.2f %-8.2f %-7d %-7d %-7d %-7d %-6d\n",
               sched_results[i].name, sched_results[i].ticks, sched_results[i].completed,
               sched_results[i].context_switches, sched_results[i].average_wait, sched_results[i].average_response,
               sched_results[i].average_turnaround, sched_results[i].irq_wakeups,
               sched_results[i].mem_wait_wakeups, sched_results[i].vm_faults, sched_results[i].vm_hits,
               sched_results[i].fs_ops);
    }
    printf("解释: AvgResp/AvgWait/AvgTurn 来自 PCB 统计；MEMWK 表示等待连续内存的进程被释放内存唤醒。\n\n");

    printf("[2] 在线 VM 多配置访问 trace\n");
    printf("工作负载: 3 个进程共享 16 次页面访问，比较 FIFO/LRU 和不同 frame 数。\n");
    printf("%-8s %-8s %-8s %-8s %-10s\n", "Policy", "Frames", "Refs", "Faults", "FaultRate");
    for (i = 0; i < 6; i++) {
        char rate_text[16];
        (void)snprintf(rate_text, sizeof(rate_text), "%.1f%%", vm_results[i].fault_rate);
        printf("%-8s %-8d %-8d %-8d %-10s\n", vm_results[i].policy, vm_results[i].frames,
               vm_results[i].references, vm_results[i].faults, rate_text);
    }
    printf("\n");

    printf("[3] TinyFS/fd 多规模状态 trace\n");
    printf("工作负载: 创建多文件、重复追加写入，并执行一次 fd append/close。\n");
    printf("%-10s %-8s %-8s %-8s %-8s %-8s %-10s %-10s\n", "Case", "Files", "Writes", "Created",
           "WritesOK", "FDOps", "FreeBlk", "Remain");
    for (i = 0; i < 3; i++) {
        printf("%-10s %-8d %-8d %-8d %-8d %-8d %-10d %-10d\n", fs_results[i].name, fs_results[i].files,
               fs_results[i].writes_per_file, fs_results[i].created, fs_results[i].writes, fs_results[i].fd_ops,
               fs_results[i].free_blocks, fs_results[i].remaining_files);
    }
}

void run_benchmark_subset_suite(int scale, int iterations) {
    Process processes[MAX_PROCESSES];
    int refs[MAX_REFERENCES];
    SchedulerTuneResult sched_results[4];
    VMTuneResult fifo3;
    VMTuneResult lru3;
    VMTuneResult fifo4;
    VMTuneResult lru4;
    MemoryStateResult memory_result;
    FileStateResult file_result;
    int sched_count;
    int ref_count;
    int total_items;
    int i;
    double pc_base_ms;
    double pc_opt_ms;
    double pc_base;
    double pc_opt;

    scale = bounded(scale, 8, 512);
    iterations = bounded(iterations, 1, 100000);
    sched_count = bounded(scale, 4, MAX_KERNEL_PROCESSES);
    ref_count = bounded(scale * 4, 16, MAX_REFERENCES);
    total_items = bounded(scale * iterations * 5, 100, 200000);
    generate_processes(processes, sched_count);
    generate_references(refs, ref_count);
    memory_result = run_memory_state_workload(scale);
    file_result = run_file_state_workload(scale);

    printf("MiniOS 状态测试教学子集: scale=%d iterations=%d\n", scale, iterations);
    printf("说明: 本子集按 MiniOS 课程模块构造可重复工作负载，输出 tick、PCB、缺页、分区、TinyFS 和同步指标；");
    printf("不是 SPEC、UnixBench、lmbench 等官方基准成绩，也不测真实 OS 调度延迟。\n");
    printf("用途: 在同一 MiniOS 模拟环境内比较不同算法和配置的状态变化。\n\n");

    printf("[1] 调度/上下文切换子集\n");
    printf("测量内容: 使用固定 PCB 工作负载比较调度策略的响应、等待、完成 tick 和上下文切换次数。\n");
    sched_results[0] = simulate_kernel_scheduler("RR q=1", "rr", 1, processes, sched_count);
    sched_results[1] = simulate_kernel_scheduler("RR q=2", "rr", 2, processes, sched_count);
    sched_results[2] = simulate_kernel_scheduler("SJF", "sjf", 2, processes, sched_count);
    sched_results[3] = simulate_kernel_scheduler("MLFQ q=2 aging", "mlfq", 2, processes, sched_count);
    printf("%-18s %-10s %-10s %-12s %-8s %-8s %-10s\n", "Policy", "AvgWait", "AvgResp", "AvgTurn", "Ticks",
           "Ctx", "Score");
    for (i = 0; i < 4; i++) {
        printf("%-18s %-10.2f %-10.2f %-12.2f %-8d %-8d %-10.2f\n", sched_results[i].name,
               sched_results[i].average_wait, sched_results[i].average_response, sched_results[i].average_turnaround,
               sched_results[i].ticks, sched_results[i].context_switches, sched_results[i].score);
    }
    printf("\n");

    printf("[2] VM 局部性/页面置换子集\n");
    printf("测量内容: 使用固定局部性访问串比较 FIFO/LRU 的缺页次数和缺页率。\n");
    fifo3 = evaluate_vm_policy("FIFO", refs, ref_count, 3, 0);
    lru3 = evaluate_vm_policy("LRU", refs, ref_count, 3, 1);
    fifo4 = evaluate_vm_policy("FIFO", refs, ref_count, 4, 0);
    lru4 = evaluate_vm_policy("LRU", refs, ref_count, 4, 1);
    printf("%-8s %-8s %-8s %-12s %-10s\n", "Policy", "Frames", "Faults", "FaultRate", "Score");
    {
        const VMTuneResult rows[] = {fifo3, lru3, fifo4, lru4};
        for (i = 0; i < 4; i++) {
            char rate_text[16];
            (void)snprintf(rate_text, sizeof(rate_text), "%.1f%%", rows[i].fault_rate * 100.0);
            printf("%-8s %-8d %-8d %-12s %-10.2f\n", rows[i].name, rows[i].frames, rows[i].faults,
                   rate_text, rows[i].score);
        }
    }
    printf("\n");

    printf("[3] 首次适应分区压力子集\n");
    printf("测量内容: 执行 allocate/free 后观察分区表、空闲总量、最大空闲块和外部碎片。\n");
    printf("total=%d attempts=%d allocated=%d freed=%d blocks=%d free_total=%d largest_free=%d external_fragmentation=%d\n\n",
           memory_result.total_size, memory_result.allocation_attempts, memory_result.allocated, memory_result.freed,
           memory_result.block_count, memory_result.free_total, memory_result.largest_free,
           memory_result.external_fragmentation);

    printf("[4] TinyFS 文件操作子集\n");
    printf("测量内容: 批量执行 TinyFS 创建、追加写入和删除，观察文件表和块位图状态。\n");
    printf("target_files=%d created=%d writes=%d deleted=%d remaining=%d free_blocks=%d\n\n",
           file_result.target_files, file_result.created, file_result.writes, file_result.deleted,
           file_result.remaining_files, file_result.free_blocks);

    printf("[5] 并发吞吐子集\n");
    printf("测量内容: 使用 pthread producer-consumer 工作负载比较小/大缓冲区对同步阻塞和吞吐量的影响。\n");
    pc_base = run_pc_benchmark(2, 2, 1, total_items, &pc_base_ms);
    pc_opt = run_pc_benchmark(2, 2, 8, total_items, &pc_opt_ms);
    printf("buffer=1 total=%.3fms throughput=%.2f ops/s\n", pc_base_ms, pc_base);
    printf("buffer=8 total=%.3fms throughput=%.2f ops/s speedup=%.2fx\n", pc_opt_ms, pc_opt,
           pc_base > 0.0 ? pc_opt / pc_base : 0.0);
}

void run_autotune_suite(int scale, int iterations) {
    Process processes[MAX_PROCESSES];
    int refs[MAX_REFERENCES];
    SchedulerTuneResult sched_results[6];
    VMTuneResult vm_results[8];
    int sched_count;
    int ref_count;
    int vm_count = 0;
    int best_sched;
    int best_vm;
    int i;
    double pc_base_ms;
    double pc_opt_ms;
    double pc_base;
    double pc_opt;
    int total_items;

    scale = bounded(scale, 8, 512);
    iterations = bounded(iterations, 1, 100000);
    sched_count = bounded(scale, 4, MAX_KERNEL_PROCESSES);
    ref_count = bounded(scale * 4, 16, MAX_REFERENCES);
    total_items = bounded(scale * iterations * 5, 100, 200000);
    generate_processes(processes, sched_count);
    generate_references(refs, ref_count);

    sched_results[0] = simulate_kernel_scheduler("RR q=1", "rr", 1, processes, sched_count);
    sched_results[1] = simulate_kernel_scheduler("RR q=2", "rr", 2, processes, sched_count);
    sched_results[2] = simulate_kernel_scheduler("RR q=4", "rr", 4, processes, sched_count);
    sched_results[3] = simulate_kernel_scheduler("Priority", "priority", 2, processes, sched_count);
    sched_results[4] = simulate_kernel_scheduler("SJF", "sjf", 2, processes, sched_count);
    sched_results[5] = simulate_kernel_scheduler("MLFQ q=2 aging", "mlfq", 2, processes, sched_count);
    best_sched = find_best_scheduler(sched_results, 6);

    for (i = 2; i <= 5; i++) {
        vm_results[vm_count++] = evaluate_vm_policy("FIFO", refs, ref_count, i, 0);
        vm_results[vm_count++] = evaluate_vm_policy("LRU", refs, ref_count, i, 1);
    }
    best_vm = find_best_vm(vm_results, vm_count);

    pc_base = run_pc_benchmark(2, 2, 1, total_items, &pc_base_ms);
    pc_opt = run_pc_benchmark(2, 2, 8, total_items, &pc_opt_ms);

    printf("自适应调优: scale=%d iterations=%d\n", scale, iterations);
    printf("调度评分: score 越低越好，综合响应时间、等待时间、完成时间和上下文切换。\n");
    printf("%-18s %-10s %-10s %-12s %-8s %-8s %-10s\n", "Scheduler", "AvgWait", "AvgResp", "AvgTurn", "Ticks",
           "Ctx", "Score");
    for (i = 0; i < 6; i++) {
        printf("%-18s %-10.2f %-10.2f %-12.2f %-8d %-8d %-10.2f%s\n", sched_results[i].name,
               sched_results[i].average_wait, sched_results[i].average_response, sched_results[i].average_turnaround,
               sched_results[i].ticks, sched_results[i].context_switches, sched_results[i].score,
               i == best_sched ? "  <-- recommend" : "");
    }
    printf("推荐调度策略: %s\n\n", sched_results[best_sched].name);

    printf("VM 调优: score=缺页率*100 + frame 成本，越低越好。\n");
    printf("%-8s %-8s %-8s %-12s %-10s\n", "Policy", "Frames", "Faults", "FaultRate", "Score");
    for (i = 0; i < vm_count; i++) {
        printf("%-8s %-8d %-8d %-11.1f%% %-10.2f%s\n", vm_results[i].name, vm_results[i].frames,
               vm_results[i].faults, vm_results[i].fault_rate * 100.0, vm_results[i].score,
               i == best_vm ? "  <-- recommend" : "");
    }
    printf("推荐 VM 策略: %s frames=%d\n\n", vm_results[best_vm].name, vm_results[best_vm].frames);

    printf("并发调优: producer-consumer buffer=1 throughput=%.2f ops/s, buffer=8 throughput=%.2f ops/s, speedup=%.2fx\n",
           pc_base, pc_opt, pc_base > 0.0 ? pc_opt / pc_base : 0.0);
    printf("推荐并发配置: buffer=%d\n", pc_opt >= pc_base ? 8 : 1);
}

int write_benchmark_csv(const char *path, int scale, int iterations, char *message, size_t message_size) {
    FILE *file;
    Process processes[MAX_PROCESSES];
    int refs[MAX_REFERENCES];
    SchedulerTuneResult sched_results[4];
    VMTuneResult vm_results[6];
    MemoryStateResult memory_result;
    FileStateResult file_result;
    int ref_count;
    int i;

    if (path == NULL || path[0] == '\0') {
        safe_copy(message, message_size, "missing CSV path");
        return 0;
    }
    scale = bounded(scale, 8, 512);
    iterations = bounded(iterations, 1, 100000);
    ref_count = bounded(scale * 4, 16, MAX_REFERENCES);
    generate_processes(processes, bounded(scale, 4, MAX_KERNEL_PROCESSES));
    generate_references(refs, ref_count);
    sched_results[0] = simulate_kernel_scheduler("RR q=1", "rr", 1, processes, bounded(scale, 4, MAX_KERNEL_PROCESSES));
    sched_results[1] = simulate_kernel_scheduler("RR q=2", "rr", 2, processes, bounded(scale, 4, MAX_KERNEL_PROCESSES));
    sched_results[2] = simulate_kernel_scheduler("SJF", "sjf", 2, processes, bounded(scale, 4, MAX_KERNEL_PROCESSES));
    sched_results[3] = simulate_kernel_scheduler("MLFQ q=2 aging", "mlfq", 2, processes,
                                                  bounded(scale, 4, MAX_KERNEL_PROCESSES));
    vm_results[0] = evaluate_vm_policy("FIFO", refs, ref_count, 3, 0);
    vm_results[1] = evaluate_vm_policy("LRU", refs, ref_count, 3, 1);
    vm_results[2] = evaluate_vm_policy("FIFO", refs, ref_count, 4, 0);
    vm_results[3] = evaluate_vm_policy("LRU", refs, ref_count, 4, 1);
    vm_results[4] = evaluate_vm_policy("FIFO", refs, ref_count, 5, 0);
    vm_results[5] = evaluate_vm_policy("LRU", refs, ref_count, 5, 1);
    memory_result = run_memory_state_workload(scale);
    file_result = run_file_state_workload(scale);

    file = fopen(path, "w");
    if (file == NULL) {
        safe_copy(message, message_size, "cannot open CSV path for writing");
        return 0;
    }
    (void)fprintf(file, "category,name,scale,iterations,metric_name,metric_value,unit,notes\n");

    for (i = 0; i < 4; i++) {
        write_metric_row(file, "scheduling", sched_results[i].name, scale, iterations, "avg_wait",
                         sched_results[i].average_wait, "tick", "MiniKernel PCB workload");
        write_metric_row(file, "scheduling", sched_results[i].name, scale, iterations, "avg_response",
                         sched_results[i].average_response, "tick", "MiniKernel PCB workload");
        write_metric_row(file, "scheduling", sched_results[i].name, scale, iterations, "context_switches",
                         (double)sched_results[i].context_switches, "count", "MiniKernel PCB workload");
        write_metric_row(file, "scheduling", sched_results[i].name, scale, iterations, "score",
                         sched_results[i].score, "score", "lower is better");
    }
    for (i = 0; i < 6; i++) {
        char name[48];
        (void)snprintf(name, sizeof(name), "%s frames=%d", vm_results[i].name, vm_results[i].frames);
        write_metric_row(file, "paging", name, scale, iterations, "faults", (double)vm_results[i].faults, "count",
                         "fixed page reference trace");
        write_metric_row(file, "paging", name, scale, iterations, "fault_rate", vm_results[i].fault_rate * 100.0,
                         "percent", "fixed page reference trace");
    }
    write_metric_row(file, "memory", "First-fit partitions", scale, iterations, "allocated",
                     (double)memory_result.allocated, "count", "allocate/free replay");
    write_metric_row(file, "memory", "First-fit partitions", scale, iterations, "free_total",
                     (double)memory_result.free_total, "unit", "allocate/free replay");
    write_metric_row(file, "memory", "First-fit partitions", scale, iterations, "largest_free",
                     (double)memory_result.largest_free, "unit", "allocate/free replay");
    write_metric_row(file, "memory", "First-fit partitions", scale, iterations, "external_fragmentation",
                     (double)memory_result.external_fragmentation, "unit", "allocate/free replay");
    write_metric_row(file, "filesystem", "TinyFS create/write/delete", scale, iterations, "created",
                     (double)file_result.created, "count", "metadata and bitmap replay");
    write_metric_row(file, "filesystem", "TinyFS create/write/delete", scale, iterations, "writes",
                     (double)file_result.writes, "count", "metadata and bitmap replay");
    write_metric_row(file, "filesystem", "TinyFS create/write/delete", scale, iterations, "remaining_files",
                     (double)file_result.remaining_files, "count", "metadata and bitmap replay");
    write_metric_row(file, "filesystem", "TinyFS create/write/delete", scale, iterations, "free_blocks",
                     (double)file_result.free_blocks, "block", "metadata and bitmap replay");

    {
        double base_ms;
        double opt_ms;
        int total_items = bounded(scale * iterations * 5, 100, 200000);
        double base = run_pc_benchmark(2, 2, 1, total_items, &base_ms);
        double opt = run_pc_benchmark(2, 2, 8, total_items, &opt_ms);
        write_metric_row(file, "concurrency", "producer-consumer buffer=1", scale, iterations, "throughput", base,
                         "ops/s", "pthread workload");
        write_metric_row(file, "concurrency", "producer-consumer buffer=1", scale, iterations, "elapsed_ms", base_ms,
                         "ms", "pthread workload");
        write_metric_row(file, "concurrency", "producer-consumer buffer=8", scale, iterations, "throughput", opt,
                         "ops/s", "pthread workload");
        write_metric_row(file, "concurrency", "producer-consumer buffer=8", scale, iterations, "elapsed_ms", opt_ms,
                         "ms", "pthread workload");
    }

    if (fclose(file) != 0) {
        safe_copy(message, message_size, "failed to close CSV file");
        return 0;
    }
    (void)snprintf(message, message_size, "wrote %s scale=%d iterations=%d", path, scale, iterations);
    return 1;
}

int write_performance_report(const char *path, int scale, int iterations, char *message, size_t message_size) {
    FILE *file;
    Process processes[MAX_PROCESSES];
    int refs[MAX_REFERENCES];
    SchedulerTuneResult sched_results[6];
    VMTuneResult vm_results[8];
    MemoryStateResult memory_result;
    FileStateResult file_result;
    int sched_count;
    int ref_count;
    int vm_count = 0;
    int best_sched;
    int best_vm;
    int i;
    double pc_base_ms;
    double pc_opt_ms;
    double pc_base;
    double pc_opt;
    int total_items;

    if (path == NULL || path[0] == '\0') {
        safe_copy(message, message_size, "missing report path");
        return 0;
    }
    scale = bounded(scale, 8, 512);
    iterations = bounded(iterations, 1, 100000);
    sched_count = bounded(scale, 4, MAX_KERNEL_PROCESSES);
    ref_count = bounded(scale * 4, 16, MAX_REFERENCES);
    total_items = bounded(scale * iterations * 5, 100, 200000);
    generate_processes(processes, sched_count);
    generate_references(refs, ref_count);

    sched_results[0] = simulate_kernel_scheduler("RR q=1", "rr", 1, processes, sched_count);
    sched_results[1] = simulate_kernel_scheduler("RR q=2", "rr", 2, processes, sched_count);
    sched_results[2] = simulate_kernel_scheduler("RR q=4", "rr", 4, processes, sched_count);
    sched_results[3] = simulate_kernel_scheduler("Priority", "priority", 2, processes, sched_count);
    sched_results[4] = simulate_kernel_scheduler("SJF", "sjf", 2, processes, sched_count);
    sched_results[5] = simulate_kernel_scheduler("MLFQ q=2 aging", "mlfq", 2, processes, sched_count);
    best_sched = find_best_scheduler(sched_results, 6);
    for (i = 2; i <= 5; i++) {
        vm_results[vm_count++] = evaluate_vm_policy("FIFO", refs, ref_count, i, 0);
        vm_results[vm_count++] = evaluate_vm_policy("LRU", refs, ref_count, i, 1);
    }
    best_vm = find_best_vm(vm_results, vm_count);
    pc_base = run_pc_benchmark(2, 2, 1, total_items, &pc_base_ms);
    pc_opt = run_pc_benchmark(2, 2, 8, total_items, &pc_opt_ms);
    memory_result = run_memory_state_workload(scale);
    file_result = run_file_state_workload(scale);

    file = fopen(path, "w");
    if (file == NULL) {
        safe_copy(message, message_size, "cannot open report path for writing");
        return 0;
    }
    (void)fprintf(file, "# MiniOS 配置分析报告\n\n");
    (void)fprintf(file, "- scale: %d\n- iterations: %d\n- scheduler workload: %d MiniKernel PCBs\n- page references: %d\n\n",
                  scale, iterations, sched_count, ref_count);
    (void)fprintf(file, "说明: 本报告来自 MiniOS 教学工作负载，统计调度、VM、内存、TinyFS 和同步配置的状态指标；");
    (void)fprintf(file, "不是官方基准成绩，也不代表真实内核或硬件性能。\n\n");
    (void)fprintf(file, "## 自适应调度选择\n\n");
    (void)fprintf(file, "| Scheduler | AvgWait | AvgResp | AvgTurn | Ticks | Ctx | Score |\n");
    (void)fprintf(file, "| --- | ---: | ---: | ---: | ---: | ---: | ---: |\n");
    for (i = 0; i < 6; i++) {
        (void)fprintf(file, "| %s%s | %.2f | %.2f | %.2f | %d | %d | %.2f |\n",
                      sched_results[i].name, i == best_sched ? " (推荐)" : "",
                      sched_results[i].average_wait, sched_results[i].average_response,
                      sched_results[i].average_turnaround, sched_results[i].ticks,
                      sched_results[i].context_switches, sched_results[i].score);
    }
    (void)fprintf(file, "\n推荐调度策略为 `%s`，评分综合响应时间、等待时间、完成时间和上下文切换次数。\n\n",
                  sched_results[best_sched].name);

    (void)fprintf(file, "## 页面置换调优\n\n");
    (void)fprintf(file, "| Policy | Frames | Faults | Fault Rate | Score |\n");
    (void)fprintf(file, "| --- | ---: | ---: | ---: | ---: |\n");
    for (i = 0; i < vm_count; i++) {
        (void)fprintf(file, "| %s%s | %d | %d | %.1f%% | %.2f |\n", vm_results[i].name,
                      i == best_vm ? " (推荐)" : "", vm_results[i].frames, vm_results[i].faults,
                      vm_results[i].fault_rate * 100.0, vm_results[i].score);
    }
    (void)fprintf(file, "\n推荐 VM 策略为 `%s`，frame 数为 `%d`。\n\n", vm_results[best_vm].name,
                  vm_results[best_vm].frames);

    (void)fprintf(file, "## 内存分区状态\n\n");
    (void)fprintf(file, "| Total | Attempts | Allocated | Freed | Blocks | Free Total | Largest Free | External Fragmentation |\n");
    (void)fprintf(file, "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
    (void)fprintf(file, "| %d | %d | %d | %d | %d | %d | %d | %d |\n\n",
                  memory_result.total_size, memory_result.allocation_attempts, memory_result.allocated,
                  memory_result.freed, memory_result.block_count, memory_result.free_total,
                  memory_result.largest_free, memory_result.external_fragmentation);

    (void)fprintf(file, "## TinyFS 状态\n\n");
    (void)fprintf(file, "| Target Files | Created | Writes | Deleted | Remaining | Free Blocks |\n");
    (void)fprintf(file, "| ---: | ---: | ---: | ---: | ---: | ---: |\n");
    (void)fprintf(file, "| %d | %d | %d | %d | %d | %d |\n\n", file_result.target_files,
                  file_result.created, file_result.writes, file_result.deleted,
                  file_result.remaining_files, file_result.free_blocks);

    (void)fprintf(file, "## 同步配置吞吐对比\n\n");
    (void)fprintf(file, "| Config | Total ms | Throughput |\n");
    (void)fprintf(file, "| --- | ---: | ---: |\n");
    (void)fprintf(file, "| buffer=1 基线配置 | %.3f | %.2f ops/s |\n", pc_base_ms, pc_base);
    (void)fprintf(file, "| buffer=8 优化配置 | %.3f | %.2f ops/s |\n", pc_opt_ms, pc_opt);
    (void)fprintf(file, "\n吞吐提升约为 `%.2fx`，该项仅用于说明 pthread 生产者-消费者场景中缓冲区容量对阻塞频率的影响。\n",
                  pc_base > 0.0 ? pc_opt / pc_base : 0.0);
    if (fclose(file) != 0) {
        safe_copy(message, message_size, "failed to close report file");
        return 0;
    }
    (void)snprintf(message, message_size, "wrote %s scale=%d iterations=%d", path, scale, iterations);
    return 1;
}
