#ifndef OS_PROJECT_H
#define OS_PROJECT_H

#include <stddef.h>
#include <stdio.h>

/* Public data structures and module interfaces for MiniOS. */

#define MAX_PROCESSES 64
#define MAX_PID_LEN 32
#define MAX_SEGMENTS 1024
#define MAX_REFERENCES 512
#define MAX_FRAMES 32
#define MAX_MEMORY_BLOCKS 128
#define MAX_PARTITION_EVENTS 256
#define MAX_FS_BLOCKS 128
#define MAX_FS_BLOCK_SIZE 256
#define MAX_FS_FILES 64
#define MAX_FS_DIRS 32
#define MAX_FILE_BLOCKS 64
#define MAX_PATH_LEN 128
#define MAX_CONTENT_LEN 4096
#define MAX_MESSAGE_LEN 160
#define MAX_RT_TASKS 32
#define MAX_RT_JOBS 512
#define MAX_RT_TIMELINE 2048
#define MAX_KERNEL_PROCESSES 16
#define MAX_KERNEL_LOGS 160
#define MAX_KERNEL_LOG_LEN 192
#define MAX_KERNEL_ACCESSES 256
#define MAX_KERNEL_FDS 16

#define KERNEL_SCHED_RR 0
#define KERNEL_SCHED_FCFS 1
#define KERNEL_SCHED_SJF 2
#define KERNEL_SCHED_PRIORITY 3
#define KERNEL_SCHED_MLFQ 4
#define KERNEL_VM_FIFO 0
#define KERNEL_VM_LRU 1
#define KERNEL_MLFQ_LEVELS 3
#define KERNEL_MLFQ_AGING_TICKS 6

/* Static process input used by the batch scheduling algorithms. */
typedef struct {
    char pid[MAX_PID_LEN];
    int arrival;
    int burst;
    int priority;
} Process;

/* One continuous CPU ownership interval in a Gantt chart. */
typedef struct {
    char pid[MAX_PID_LEN];
    int start;
    int end;
} ScheduleSegment;

/* Per-process metrics derived from a scheduling run. */
typedef struct {
    char pid[MAX_PID_LEN];
    int arrival;
    int burst;
    int priority;
    int start;
    int completion;
    int turnaround;
    int waiting;
    int response;
    double weighted_turnaround;
} ProcessMetric;

/* Complete scheduling result: timeline plus aggregate performance metrics. */
typedef struct {
    char algorithm[64];
    ScheduleSegment timeline[MAX_SEGMENTS];
    int timeline_count;
    ProcessMetric metrics[MAX_PROCESSES];
    int process_count;
    double average_turnaround;
    double average_waiting;
    double average_response;
    double average_weighted_turnaround;
    double cpu_utilization;
    double throughput;
} ScheduleReport;

/* Periodic real-time task definition for EDF/RMS simulation. */
typedef struct {
    char name[MAX_PID_LEN];
    int phase;
    int execution;
    int period;
    int deadline;
} RealTimeTask;

/* One real-time scheduler timeline segment. */
typedef struct {
    char label[MAX_PID_LEN];
    int start;
    int end;
} RTSegment;

/* Per-job result for a released real-time task instance. */
typedef struct {
    char task[MAX_PID_LEN];
    int job_no;
    int release;
    int execution;
    int deadline;
    int start;
    int completion;
    int deadline_miss;
} RTJobMetric;

/* Complete EDF/RMS report. */
typedef struct {
    char algorithm[64];
    RTSegment timeline[MAX_RT_TIMELINE];
    int timeline_count;
    RTJobMetric jobs[MAX_RT_JOBS];
    int job_count;
    int simulation_time;
    int deadline_misses;
    double completion_rate;
    double cpu_utilization;
} RTScheduleReport;

/* One page-reference step in FIFO/LRU replacement. */
typedef struct {
    int step;
    int page;
    int frames[MAX_FRAMES];
    int frame_count;
    int fault;
    int evicted;
} PageEvent;

/* Page replacement report with every frame snapshot. */
typedef struct {
    char algorithm[32];
    int frame_count;
    int references[MAX_REFERENCES];
    int reference_count;
    PageEvent events[MAX_REFERENCES];
    int event_count;
    int faults;
    int hits;
    double fault_rate;
} PageReport;

/* Dynamic partition block used by first-fit memory management. */
typedef struct {
    int start;
    int size;
    char pid[MAX_PID_LEN];
    int free;
} MemoryBlock;

/* Snapshot of one allocation/free operation and the partition table after it. */
typedef struct {
    char operation[16];
    char pid[MAX_PID_LEN];
    int size;
    int success;
    char message[MAX_MESSAGE_LEN];
    MemoryBlock blocks[MAX_MEMORY_BLOCKS];
    int block_count;
} PartitionEvent;

/* First-fit memory manager state and operation history. */
typedef struct {
    int total_size;
    MemoryBlock blocks[MAX_MEMORY_BLOCKS];
    int block_count;
    PartitionEvent history[MAX_PARTITION_EVENTS];
    int history_count;
} FirstFitMemory;

/* TinyFS file metadata: path, logical size, and allocated blocks. */
typedef struct {
    char name[MAX_PATH_LEN];
    int size;
    int blocks[MAX_FILE_BLOCKS];
    int block_count;
} FsFile;

/* TinyFS in-memory disk image with directory table and block bitmap. */
typedef struct {
    int total_blocks;
    int block_size;
    int used[MAX_FS_BLOCKS];
    char data[MAX_FS_BLOCKS][MAX_FS_BLOCK_SIZE];
    char directories[MAX_FS_DIRS][MAX_PATH_LEN];
    int directory_count;
    FsFile files[MAX_FS_FILES];
    int file_count;
} TinyFS;

/* PCB entry inside the user-space MiniKernel. */
typedef struct {
    int pid;
    char name[MAX_PID_LEN];
    int burst;
    int remaining;
    int priority;
    int memory_size;
    int requested_memory;
    int arrival_tick;
    int first_run_tick;
    int finish_tick;
    int cpu_time;
    int wait_time;
    int context_switches;
    int wake_tick;
    int mlfq_level;
    int mlfq_ticks_left;
    int mlfq_wait_ticks;
    char state[16];
} KernelProcess;

typedef struct {
    int used;
    int owner_pid;
    int fd;
    char path[MAX_PATH_LEN];
    char mode[8];
    int offset;
    int deleted;
    char content[MAX_CONTENT_LEN];
} KernelOpenFile;

/* Integrated MiniOS state shared by shell commands and demos. */
typedef struct {
    int tick;
    int next_pid;
    int current_index;
    int scheduler_policy;
    int time_quantum;
    int quantum_left;
    int context_switches;
    int idle_ticks;
    KernelProcess processes[MAX_KERNEL_PROCESSES];
    int process_count;
    FirstFitMemory memory;
    TinyFS fs;
    char logs[MAX_KERNEL_LOGS][MAX_KERNEL_LOG_LEN];
    int log_count;
    int access_pid[MAX_KERNEL_ACCESSES];
    int access_page[MAX_KERNEL_ACCESSES];
    int access_count;
    int page_frames;
    int vm_policy;
    int vm_clock;
    int vm_faults;
    int vm_hits;
    int vm_frame_pid[MAX_FRAMES];
    int vm_frame_page[MAX_FRAMES];
    int vm_loaded_at[MAX_FRAMES];
    int vm_last_used[MAX_FRAMES];
    KernelOpenFile open_files[MAX_KERNEL_FDS];
} MiniKernel;

/* Utility helpers. */
void safe_copy(char *dst, size_t dst_size, const char *src);
char *trim(char *text);
int parse_int_value(const char *text, int *out);
int parse_int_list(const char *text, int *out, int max_count);
int read_text_file(const char *path, char *buffer, size_t buffer_size);

/* Processor scheduling. */
int load_processes_csv(const char *path, Process *processes, int max_count, char *error, size_t error_size);
int read_processes_interactive(Process *processes, int max_count);
ScheduleReport schedule_fcfs(const Process *processes, int count);
ScheduleReport schedule_sjf(const Process *processes, int count);
ScheduleReport schedule_priority(const Process *processes, int count, int lower_number_first);
ScheduleReport schedule_hrrn(const Process *processes, int count);
ScheduleReport schedule_round_robin(const Process *processes, int count, int quantum);
void print_schedule_report(const ScheduleReport *report);
void print_schedule_comparison(const Process *processes, int count, int quantum);
void print_rr_quantum_comparison(const Process *processes, int count, const int *quantums, int quantum_count);

/* Real-time scheduling. */
int load_rt_tasks_csv(const char *path, RealTimeTask *tasks, int max_count, char *error, size_t error_size);
RTScheduleReport schedule_edf(const RealTimeTask *tasks, int count, int simulation_time);
RTScheduleReport schedule_rms(const RealTimeTask *tasks, int count, int simulation_time);
void print_rt_report(const RTScheduleReport *report);
void print_rt_comparison(const RealTimeTask *tasks, int count, int simulation_time);

/* Memory management. */
PageReport page_fifo(const int *references, int reference_count, int frame_count);
PageReport page_lru(const int *references, int reference_count, int frame_count);
void print_page_report(const PageReport *report);
void ff_init(FirstFitMemory *memory, int total_size);
PartitionEvent ff_allocate(FirstFitMemory *memory, const char *pid, int size);
PartitionEvent ff_free(FirstFitMemory *memory, const char *pid);
int ff_used_size(const FirstFitMemory *memory);
int ff_free_size(const FirstFitMemory *memory);
int ff_external_fragmentation(const FirstFitMemory *memory);
void print_partition_event(const PartitionEvent *event, int index);
void print_partition_summary(const FirstFitMemory *memory);
int run_partition_script(const char *path, FirstFitMemory *memory);

/* Synchronization demos. */
void run_producer_consumer_demo(int producers, int consumers, int items_per_producer, int buffer_size);
void run_readers_writers_demo(int readers, int writers, int read_rounds, int write_rounds);
void run_dining_philosophers_demo(int philosophers, int rounds);

/* Tiny file system. */
void fs_init(TinyFS *fs, int total_blocks, int block_size);
int fs_create(TinyFS *fs, const char *name, const char *content, char *message, size_t message_size);
int fs_write(TinyFS *fs, const char *name, const char *content, int append, char *message, size_t message_size);
int fs_read(const TinyFS *fs, const char *name, char *out, size_t out_size, char *message, size_t message_size);
int fs_delete(TinyFS *fs, const char *name, char *message, size_t message_size);
int fs_mkdir(TinyFS *fs, const char *path, char *message, size_t message_size);
int fs_rmdir(TinyFS *fs, const char *path, char *message, size_t message_size);
int fs_is_dir(const TinyFS *fs, const char *path);
int fs_is_file(const TinyFS *fs, const char *path);
void fs_list(const TinyFS *fs);
void fs_list_dir(const TinyFS *fs, const char *path);
void fs_print_tree(const TinyFS *fs);
void fs_print_bitmap(const TinyFS *fs);
int fs_run_script(TinyFS *fs, const char *path);
int fs_save_image(const TinyFS *fs, const char *path, char *message, size_t message_size);
int fs_load_image(TinyFS *fs, const char *path, char *message, size_t message_size);
int fs_check_image(const char *path, char *message, size_t message_size);

/* State workloads and configuration analysis. */
void run_benchmark_subset_suite(int scale, int iterations);
void run_tracebench_suite(void);
void run_autotune_suite(int scale, int iterations);
int write_benchmark_csv(const char *path, int scale, int iterations, char *message, size_t message_size);
int write_performance_report(const char *path, int scale, int iterations, char *message, size_t message_size);

/* User-space MiniKernel. */
void kernel_init(MiniKernel *kernel);
int kernel_create_process(MiniKernel *kernel, const char *name, int burst, int memory_size);
int kernel_create_process_with_priority(MiniKernel *kernel, const char *name, int burst, int memory_size, int priority);
void kernel_schedule_tick(MiniKernel *kernel);
int kernel_kill_process(MiniKernel *kernel, int pid);
int kernel_sleep_process(MiniKernel *kernel, int pid, int ticks);
int kernel_set_scheduler(MiniKernel *kernel, const char *policy, int quantum);
const char *kernel_scheduler_name(const MiniKernel *kernel);
const char *kernel_vm_policy_name(const MiniKernel *kernel);
void kernel_log_event(MiniKernel *kernel, const char *event);
void kernel_print_dmesg(const MiniKernel *kernel);
int kernel_record_page_access(MiniKernel *kernel, int pid, int page);
int kernel_set_vm_policy(MiniKernel *kernel, const char *policy);
void kernel_reset_vm(MiniKernel *kernel);
int kernel_alloc(MiniKernel *kernel, int pid, int size);
int kernel_free(MiniKernel *kernel, int pid);
int kernel_create_file(MiniKernel *kernel, const char *path, const char *content);
int kernel_write_file(MiniKernel *kernel, const char *path, const char *content);
int kernel_delete_file(MiniKernel *kernel, const char *path);
int kernel_open_file(MiniKernel *kernel, const char *path, const char *mode);
int kernel_open_file_for_process(MiniKernel *kernel, int owner_pid, const char *path, const char *mode);
/* fd reads/writes return byte counts; negative values indicate failure. */
int kernel_read_fd(MiniKernel *kernel, int fd, char *out, size_t out_size);
int kernel_read_fd_count(MiniKernel *kernel, int fd, int max_bytes, char *out, size_t out_size);
int kernel_read_fd_count_for_process(MiniKernel *kernel, int owner_pid, int fd, int max_bytes, char *out, size_t out_size);
int kernel_write_fd(MiniKernel *kernel, int fd, const char *content);
int kernel_write_fd_for_process(MiniKernel *kernel, int owner_pid, int fd, const char *content);
int kernel_seek_fd(MiniKernel *kernel, int fd, int offset);
int kernel_seek_fd_for_process(MiniKernel *kernel, int owner_pid, int fd, int offset);
int kernel_close_fd(MiniKernel *kernel, int fd);
int kernel_close_fd_for_process(MiniKernel *kernel, int owner_pid, int fd);
int syscall_dispatch(MiniKernel *kernel, const char *call, const char *arg1, const char *arg2);
void kernel_status(const MiniKernel *kernel);
void run_kernel_demo(void);

#endif
