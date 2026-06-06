#define _POSIX_C_SOURCE 200809L
#include "os_project.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * MiniOS shell entry point.
 * Parses tour scripts and interactive commands, then routes them to the
 * integrated MiniKernel and standalone experiment modules.
 */

static const Process DEFAULT_PROCESSES[] = {
    {"P1", 0, 7, 2},
    {"P2", 2, 4, 1},
    {"P3", 4, 1, 3},
    {"P4", 5, 4, 2},
};
static const int DEFAULT_PROCESS_COUNT = (int)(sizeof(DEFAULT_PROCESSES) / sizeof(DEFAULT_PROCESSES[0]));
static const int DEFAULT_REFS[] = {7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2};
static const int DEFAULT_REF_COUNT = (int)(sizeof(DEFAULT_REFS) / sizeof(DEFAULT_REFS[0]));
#define SHELL_LINE_MAX 8192
static const RealTimeTask DEFAULT_RT_TASKS[] = {
    {"T1", 0, 1, 4, 4},
    {"T2", 0, 2, 5, 5},
    {"T3", 0, 1, 10, 10},
};
static const int DEFAULT_RT_TASK_COUNT = (int)(sizeof(DEFAULT_RT_TASKS) / sizeof(DEFAULT_RT_TASKS[0]));

static void usage(void) {
    printf("MiniOS 操作系统模拟平台（C语言版）\n");
    printf("用法:\n");
    printf("  build/os_project[.exe]                     # 进入交互式 MiniOS\n");
    printf("  build/os_project[.exe] tour                # 同上\n");
    printf("  build/os_project[.exe] tour --mode lite    # 轻量闭环 demo\n");
    printf("  build/os_project[.exe] tour --mode full    # 完整 Full Demo\n");
    printf("  build/os_project[.exe] tour --script data/tour_script.txt\n");
    printf("  build/os_project[.exe] tour --script data/os_workflow_demo.txt --cinematic\n");
    printf("\n项目已整理为单一 tour 导览/交互模式；调度、内存、同步、文件系统、实时调度和性能实验都在 MiniOS Shell 内执行。\n");
}

static const char *suggest_shell_command(const char *old_command) {
    if (old_command == NULL) {
        return "help";
    }
    if (strcmp(old_command, "schedule") == 0 || strcmp(old_command, "demo") == 0 || strcmp(old_command, "kernel") == 0) {
        return "sched/status";
    }
    if (strcmp(old_command, "partition") == 0) {
        return "mem";
    }
    if (strcmp(old_command, "fs") == 0) {
        return "mkdir/touch/write/cat/ls/tree";
    }
    if (strcmp(old_command, "benchmark") == 0) {
        return "tracebench";
    }
    return old_command;
}

static void section(const char *title) {
    printf("\n== %s ==\n", title);
}

static int clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static int parse_int_strict(const char *text, int *out) {
    char *end = NULL;
    long value;
    if (text == NULL || text[0] == '\0' || out == NULL) {
        return 0;
    }
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || value < INT_MIN || value > INT_MAX) {
        return 0;
    }
    *out = (int)value;
    return 1;
}

static int token_starts_like_number(const char *text) {
    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    if (text[0] == '-' || text[0] == '+') {
        return text[1] >= '0' && text[1] <= '9';
    }
    return text[0] >= '0' && text[0] <= '9';
}

static int parse_optional_int_range(const char *text, int default_value, int min_value, int max_value,
                                    const char *usage_text, int *out) {
    int value;
    if (text == NULL) {
        *out = default_value;
        return 1;
    }
    if (!parse_int_strict(text, &value) || value < min_value || value > max_value) {
        printf("%s\n", usage_text);
        return 0;
    }
    *out = value;
    return 1;
}

static int parse_required_int_range(const char *text, int min_value, int max_value, const char *usage_text, int *out) {
    if (text == NULL) {
        printf("%s\n", usage_text);
        return 0;
    }
    return parse_optional_int_range(text, 0, min_value, max_value, usage_text, out);
}

static void sleep_ms(int milliseconds) {
    struct timespec ts;
    if (milliseconds <= 0) {
        return;
    }
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
    (void)nanosleep(&ts, NULL);
}

static void print_typed_text(const char *text, int type_ms) {
    const char *cursor = text;
    if (text == NULL) {
        return;
    }
    if (type_ms <= 0) {
        printf("%s", text);
        fflush(stdout);
        return;
    }
    while (*cursor != '\0') {
        putchar(*cursor++);
        fflush(stdout);
        sleep_ms(type_ms);
    }
}

static char *read_token(char **cursor) {
    char *start;
    char *end;
    if (cursor == NULL || *cursor == NULL) {
        return NULL;
    }
    start = *cursor;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        start++;
    }
    if (*start == '\0') {
        *cursor = start;
        return NULL;
    }
    end = start;
    while (*end != '\0' && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n') {
        end++;
    }
    if (*end != '\0') {
        *end = '\0';
        end++;
    }
    *cursor = end;
    return start;
}

static char *read_rest(char **cursor) {
    char *start;
    if (cursor == NULL || *cursor == NULL) {
        return NULL;
    }
    start = *cursor;
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    start[strcspn(start, "\r\n")] = '\0';
    *cursor = start + strlen(start);
    return *start == '\0' ? NULL : start;
}

static void decode_shell_escapes(const char *src, char *dst, size_t dst_size) {
    size_t out = 0;
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    while (*src != '\0' && out + 1 < dst_size) {
        if (*src == '\\' && src[1] != '\0') {
            src++;
            if (*src == 'n') {
                dst[out++] = '\n';
            } else if (*src == 't') {
                dst[out++] = '\t';
            } else {
                dst[out++] = *src;
            }
            src++;
            continue;
        }
        dst[out++] = *src++;
    }
    dst[out] = '\0';
}

static void normalize_shell_path(const char *cwd, const char *raw, char *out, size_t out_size) {
    char combined[MAX_PATH_LEN * 2];
    char normalized[MAX_PATH_LEN];
    char *parts[32];
    int part_count = 0;
    char *token;
    char *saveptr = NULL;
    int i;

    if (out == NULL || out_size == 0) {
        return;
    }
    if (raw == NULL || raw[0] == '\0') {
        safe_copy(out, out_size, cwd == NULL || cwd[0] == '\0' ? "/" : cwd);
        return;
    }
    if (raw[0] == '/') {
        safe_copy(combined, sizeof(combined), raw);
    } else if (cwd == NULL || strcmp(cwd, "/") == 0) {
        (void)snprintf(combined, sizeof(combined), "/%s", raw);
    } else {
        (void)snprintf(combined, sizeof(combined), "%s/%s", cwd, raw);
    }
    safe_copy(normalized, sizeof(normalized), combined);
    normalized[strcspn(normalized, "\r\n")] = '\0';
    for (token = strtok_r(normalized, "/", &saveptr); token != NULL; token = strtok_r(NULL, "/", &saveptr)) {
        if (strcmp(token, ".") == 0 || token[0] == '\0') {
            continue;
        }
        if (strcmp(token, "..") == 0) {
            if (part_count > 0) {
                part_count--;
            }
            continue;
        }
        if (part_count < (int)(sizeof(parts) / sizeof(parts[0]))) {
            parts[part_count++] = token;
        }
    }
    if (part_count == 0) {
        safe_copy(out, out_size, "/");
        return;
    }
    out[0] = '\0';
    for (i = 0; i < part_count; i++) {
        if (strlen(out) + strlen(parts[i]) + 2 >= out_size) {
            safe_copy(out, out_size, "/");
            return;
        }
        strcat(out, "/");
        strcat(out, parts[i]);
    }
}

static void print_shell_help(void) {
    printf("MiniOS Shell commands:\n");
    printf("  help                              显示命令帮助\n");
    printf("  man <topic>                       查看命令主题: files/fd/proc/sched/mem/vm/sync/perf\n");
    printf("  about | overview                  查看项目总览、模块和推荐演示路径\n");
    printf("  uname / uptime / whoami / hostname 类 Unix 系统信息命令\n");
    printf("  note <text>                       导览脚本说明文字\n");
    printf("  status | ps | top | jobs          查看 tick、调度器、PCB、内存表、文件表\n");
    printf("  dmesg                             查看内核事件日志\n");
    printf("  create <name> <burst> <mem> [prio] 创建进程并分配内存\n");
    printf("  tick [n] | run [n]                推进 n 个时钟 tick，输出真实调度事件\n");
    printf("  sleep|block <pid> <ticks>         阻塞进程，模拟 I/O 等待和 timer IRQ 唤醒\n");
    printf("  kill <pid>                        终止进程并回收内存\n");
    printf("  pwd / cd <path> / clear / echo    类 Unix shell 基础命令\n");
    printf("  mkdir <path> / rmdir <path>       创建/删除目录，支持相对路径\n");
    printf("  touch <path> [content]            创建文件\n");
    printf("  put <path> <content>              覆盖写文件，不存在则创建\n");
    printf("  write <path> <content>            追加写文件\n");
    printf("  cat <path> / rm <path>            读取/删除文件\n");
    printf("  open <path> [r|w|a|rw]            打开文件并返回 fd\n");
    printf("  readfd <fd> [n] / writefd <fd> <text> 通过 fd offset 读写\n");
    printf("  seekfd <fd> <offset>              调整打开文件偏移量\n");
    printf("  close <fd> / fd / lsof            关闭或查看打开文件表\n");
    printf("  savefs|loadfs|fscheck <image>     保存、加载或校验 TinyFS 镜像\n");
    printf("  ls [path] / tree / df / mount     列目录、打印树、查看 TinyFS 空间和挂载\n");
    printf("  mem | free                        查看首次适应动态分区状态\n");
    printf("  sched                             查看当前调度策略\n");
    printf("  sched rr <q>|mlfq <q>|fcfs|sjf|priority 切换当前内核调度器\n");
    printf("  sched compare [q]                 用当前 PCB 快照对比调度算法\n");
    printf("  access <pid> <page>               记录当前进程页访问\n");
    printf("  vm [fifo|lru|reset|frames n]      配置在线 VM frame table\n");
    printf("  page [frames] | page demo [n]     基于 access 历史或默认引用串运行 FIFO/LRU\n");
    printf("  realtime [time]                   运行 EDF/RMS 实时调度实验\n");
    printf("  sync [all|pc|rw|dp]               运行三大同步问题\n");
    printf("  tracebench                        运行多组 MiniOS 模拟工作负载指标\n");
    printf("  bench                             tracebench 的兼容别名\n");
    printf("  benchsubset [scale] [iterations]  运行 MiniOS 状态测试教学子集\n");
    printf("  autotune [scale] [iterations]     自动比较调度/VM/并发配置并给出推荐\n");
    printf("  benchcsv <path> [scale] [iters]   将状态指标写成 CSV\n");
    printf("  perfreport <path> [scale] [iters] 写出 Markdown 配置分析报告\n");
    printf("  exit | shutdown                   退出 MiniOS\n");
}

static void print_project_overview(void) {
    printf("MiniOS project overview\n");
    printf("  定位: C11 + POSIX 用户态微型操作系统模拟平台\n");
    printf("  主线: 单一 MiniKernel 状态贯穿 PCB、tick 调度、FF 内存、VM、TinyFS、fd、dmesg\n");
    printf("  基础: FCFS/SJF/Priority/RR, FF, FIFO/LRU, pthread 同步, TinyFS CRUD\n");
    printf("  提升: MLFQ + aging, EDF/RMS, tracebench 多配置状态测试, autotune, GUI Full Demo, 验收记录\n");
    printf("  推荐演示:\n");
    printf("    make tour                       # 终端默认系统时间线\n");
    printf("    make demo                       # 浏览器 MiniOS Control Center / Full Demo\n");
    printf("    make evidence                   # 运行核心验证并生成验收记录\n");
    printf("    Windows 下将 make 替换为 mingw32-make\n");
}

static int fs_free_blocks_for_shell(const TinyFS *fs) {
    int i;
    int count = 0;
    if (fs == NULL) {
        return 0;
    }
    for (i = 0; i < fs->total_blocks; i++) {
        if (!fs->used[i]) {
            count++;
        }
    }
    return count;
}

static void print_uname(const MiniKernel *kernel) {
    printf("MiniOS minios 1.0 C11/POSIX user-space-kernel %s scheduler=%s tick=%d\n", "x86_64",
           kernel_scheduler_name(kernel), kernel == NULL ? 0 : kernel->tick);
}

static void print_uptime(const MiniKernel *kernel) {
    int runnable = 0;
    int i;
    if (kernel == NULL) {
        return;
    }
    for (i = 0; i < kernel->process_count; i++) {
        const KernelProcess *process = &kernel->processes[i];
        if (strcmp(process->state, "READY") == 0 || strcmp(process->state, "RUNNING") == 0) {
            runnable++;
        }
    }
    printf("up %d ticks, idle %d ticks, processes %d, runnable %d, scheduler %s\n", kernel->tick, kernel->idle_ticks,
           kernel->process_count, runnable, kernel_scheduler_name(kernel));
}

static void print_df(const TinyFS *fs) {
    int free_blocks = fs_free_blocks_for_shell(fs);
    int used_blocks = fs == NULL ? 0 : fs->total_blocks - free_blocks;
    int total_bytes = fs == NULL ? 0 : fs->total_blocks * fs->block_size;
    int used_bytes = fs == NULL ? 0 : used_blocks * fs->block_size;
    int free_bytes = fs == NULL ? 0 : free_blocks * fs->block_size;
    int use_percent = total_bytes <= 0 ? 0 : (used_bytes * 100 + total_bytes - 1) / total_bytes;
    printf("%-12s %10s %10s %10s %5s %s\n", "Filesystem", "Size", "Used", "Avail", "Use%", "Mounted");
    printf("%-12s %9dB %9dB %9dB %4d%% %s\n", "tinyfs", total_bytes, used_bytes, free_bytes, use_percent, "/");
    printf("inodes: dirs=%d/%d files=%d/%d block_size=%d\n", fs == NULL ? 0 : fs->directory_count, MAX_FS_DIRS,
           fs == NULL ? 0 : fs->file_count, MAX_FS_FILES, fs == NULL ? 0 : fs->block_size);
}

static void print_lsof(const MiniKernel *kernel) {
    int i;
    printf("%-7s %-5s %-8s %-8s %-9s %-8s\n", "Owner", "FD", "Mode", "Offset", "State", "Path");
    if (kernel == NULL) {
        return;
    }
    for (i = 0; i < MAX_KERNEL_FDS; i++) {
        if (kernel->open_files[i].used) {
            printf("%-7d %-5d %-8s %-8d %-9s %-8s\n", kernel->open_files[i].owner_pid, kernel->open_files[i].fd,
                   kernel->open_files[i].mode, kernel->open_files[i].offset,
                   kernel->open_files[i].deleted ? "deleted" : "linked", kernel->open_files[i].path);
        }
    }
}

static void print_jobs(const MiniKernel *kernel) {
    int i;
    printf("%-5s %-12s %-10s %-7s %-8s %-8s\n", "PID", "Name", "State", "Prio", "CPU", "Mem");
    if (kernel == NULL) {
        return;
    }
    for (i = 0; i < kernel->process_count; i++) {
        const KernelProcess *process = &kernel->processes[i];
        if (strcmp(process->state, "TERMINATED") != 0) {
            printf("%-5d %-12s %-10s %-7d %-8d %-8d\n", process->pid, process->name, process->state,
                   process->priority, process->cpu_time, process->memory_size);
        }
    }
}

static void print_man_command(const char *command) {
    if (command == NULL) {
        printf("MiniOS manual: try `man files`, `man proc`, `man sched`, `man vm`, `man sync`, `man perf`, or `help`.\n");
        return;
    }
    if (strcmp(command, "files") == 0 || strcmp(command, "fs") == 0 || strcmp(command, "tinyfs") == 0) {
        printf("TinyFS: pwd, cd, ls, tree, mkdir, rmdir, touch, put, write, cat, rm, df, mount, savefs, loadfs, fscheck\n");
    } else if (strcmp(command, "fd") == 0 || strcmp(command, "lsof") == 0) {
        printf("File descriptors: open <path> [r|w|a|rw], readfd <fd> [n], writefd <fd> <text>, seekfd <fd> <offset>, close <fd>, fd, lsof\n");
    } else if (strcmp(command, "proc") == 0 || strcmp(command, "process") == 0 || strcmp(command, "jobs") == 0) {
        printf("Processes: create <name> <burst> <mem> [prio], run|tick [n], sleep|block <pid> <ticks>, kill <pid>, ps, top, jobs, status, dmesg\n");
    } else if (strcmp(command, "sched") == 0 || strcmp(command, "scheduler") == 0) {
        printf("Scheduler: sched, sched compare [q], sched rr <q>, sched mlfq <q>, sched fcfs, sched sjf, sched priority, sched demo [q]\n");
    } else if (strcmp(command, "mem") == 0 || strcmp(command, "memory") == 0 || strcmp(command, "free") == 0) {
        printf("Memory: free|mem shows first-fit partitions, MEM_WAIT pressure, used/free totals and fragmentation.\n");
    } else if (strcmp(command, "vm") == 0 || strcmp(command, "page") == 0) {
        printf("VM: access <pid> <page>, vm [fifo|lru|reset|frames n], page [frames], page demo [frames]\n");
    } else if (strcmp(command, "sync") == 0) {
        printf("Sync: sync pc, sync rw, sync dp, sync all\n");
    } else if (strcmp(command, "realtime") == 0 || strcmp(command, "rt") == 0) {
        printf("Realtime: realtime [simulation_ticks] compares EDF and RMS deadline behavior.\n");
    } else if (strcmp(command, "bench") == 0 || strcmp(command, "benchmark") == 0 ||
               strcmp(command, "tracebench") == 0 || strcmp(command, "perf") == 0 ||
               strcmp(command, "autotune") == 0) {
        printf("State analysis: tracebench, benchsubset [scale] [iters], autotune [scale] [iters], benchcsv <path> [scale] [iters], perfreport <path> [scale] [iters]\n");
        printf("tracebench reports MiniOS simulated OS-state metrics, not real kernel latency or hardware performance.\n");
        printf("benchcsv/perfreport export the same kind of state and configuration metrics for tables and reports.\n");
    } else {
        printf("No manual entry for %s. Try `help` for the full command list.\n", command);
    }
}

static void print_shell_banner(void) {
    printf("MiniOS booting...\n");
    printf("[boot] C11/POSIX user-space kernel image loaded\n");
    printf("[kernel] init process table, scheduler, first-fit memory, TinyFS, VM trace, dmesg\n");
    printf("[kernel] ready. 输入 about 看项目总览，输入 help 查看命令；命令会持续改变同一个 MiniKernel 状态。\n");
}

static const char *mode_script_path(const char *mode) {
    if (mode == NULL || strcmp(mode, "lite") == 0 || strcmp(mode, "quick") == 0) {
        return "data/os_workflow_demo.txt";
    }
    if (strcmp(mode, "full") == 0) {
        return "data/os_workflow_full_demo.txt";
    }
    return NULL;
}

static int set_script_input(FILE **input, int *close_input, const char *script_path) {
    FILE *next_input;
    if (input == NULL || close_input == NULL || script_path == NULL) {
        return 0;
    }
    if (*close_input && *input != NULL) {
        fclose(*input);
        *close_input = 0;
    }
    if (strcmp(script_path, "-") == 0) {
        *input = stdin;
        return 1;
    }
    next_input = fopen(script_path, "r");
    if (next_input == NULL) {
        fprintf(stderr, "cannot open tour script: %s\n", script_path);
        *input = stdin;
        return 0;
    }
    *input = next_input;
    *close_input = 1;
    return 1;
}

static void run_schedule_demo(int quantum) {
    int quantums[] = {1, 2, 4, 8};
    quantum = clamp_int(quantum, 1, 16);
    section("处理机调度：FCFS / SJF / Priority / RR / HRRN");
    print_schedule_comparison(DEFAULT_PROCESSES, DEFAULT_PROCESS_COUNT, quantum);
    section("调度优化：Round Robin 多时间片对比");
    print_rr_quantum_comparison(DEFAULT_PROCESSES, DEFAULT_PROCESS_COUNT, quantums, 4);
}

static void run_page_demo(int frames) {
    PageReport fifo;
    PageReport lru;
    frames = clamp_int(frames, 1, MAX_FRAMES);
    fifo = page_fifo(DEFAULT_REFS, DEFAULT_REF_COUNT, frames);
    lru = page_lru(DEFAULT_REFS, DEFAULT_REF_COUNT, frames);
    section("内存管理：FIFO 页面置换");
    print_page_report(&fifo);
    section("内存管理：LRU 页面置换");
    print_page_report(&lru);
}

static void print_new_kernel_logs(const MiniKernel *kernel, int start) {
    int i;
    if (kernel == NULL) {
        return;
    }
    if (start < 0 || start > kernel->log_count) {
        start = 0;
    }
    for (i = start; i < kernel->log_count; i++) {
        printf("%s\n", kernel->logs[i]);
    }
}

static int build_kernel_process_snapshot(const MiniKernel *kernel, Process *out, int max_count) {
    int count = 0;
    int i;
    if (kernel == NULL || out == NULL || max_count <= 0) {
        return 0;
    }
    for (i = 0; i < kernel->process_count && count < max_count; i++) {
        /* Compare only runnable PCBs; waiting and finished jobs would distort batch metrics. */
        const KernelProcess *process = &kernel->processes[i];
        if ((strcmp(process->state, "READY") != 0 && strcmp(process->state, "RUNNING") != 0) || process->remaining <= 0) {
            continue;
        }
        (void)snprintf(out[count].pid, sizeof(out[count].pid), "K%d", process->pid);
        out[count].arrival = 0;
        out[count].burst = process->remaining;
        out[count].priority = process->priority;
        count++;
    }
    return count;
}

static void print_kernel_schedule_status(const MiniKernel *kernel) {
    printf("[scheduler] policy=%s quantum=%d quantum_left=%d mlfq_levels=%d aging=%d runnable_from_current_state=yes\n",
           kernel_scheduler_name(kernel), kernel->time_quantum, kernel->quantum_left, KERNEL_MLFQ_LEVELS,
           KERNEL_MLFQ_AGING_TICKS);
    printf("use: sched rr <q> | sched mlfq <q> | sched fcfs | sched sjf | sched priority | sched compare [q]\n");
}

static void handle_sched_command(MiniKernel *kernel, char **cursor) {
    char *mode = read_token(cursor);
    if (mode == NULL) {
        print_kernel_schedule_status(kernel);
        return;
    }
    if (strcmp(mode, "compare") == 0) {
        Process snapshot[MAX_KERNEL_PROCESSES];
        int quantum;
        int count = build_kernel_process_snapshot(kernel, snapshot, MAX_KERNEL_PROCESSES);
        if (!parse_optional_int_range(read_token(cursor), kernel->time_quantum, 1, 16,
                                      "usage: sched compare [q] (q must be 1..16)", &quantum)) {
            return;
        }
        if (count <= 0) {
            printf("[scheduler] no runnable PCB snapshot; create processes first\n");
            return;
        }
        section("当前 PCB 快照调度对比");
        print_schedule_comparison(snapshot, count, quantum);
        return;
    }
    if (strcmp(mode, "demo") == 0) {
        int quantum;
        if (!parse_optional_int_range(read_token(cursor), 2, 1, 16, "usage: sched demo [q] (q must be 1..16)",
                                      &quantum)) {
            return;
        }
        run_schedule_demo(quantum);
        return;
    }
    if (token_starts_like_number(mode)) {
        int quantum;
        if (!parse_required_int_range(mode, 1, 16, "usage: sched <q> (q must be 1..16)", &quantum)) {
            return;
        }
        (void)kernel_set_scheduler(kernel, "rr", quantum);
        printf("[scheduler] policy=RR quantum=%d\n", kernel->time_quantum);
        return;
    }
    if (strcmp(mode, "rr") == 0) {
        int quantum;
        if (!parse_optional_int_range(read_token(cursor), kernel->time_quantum, 1, 16,
                                      "usage: sched rr [q] (q must be 1..16)", &quantum)) {
            return;
        }
        if (kernel_set_scheduler(kernel, "rr", quantum)) {
            printf("[scheduler] policy=RR quantum=%d\n", kernel->time_quantum);
        }
        return;
    }
    if (strcmp(mode, "mlfq") == 0) {
        int quantum;
        if (!parse_optional_int_range(read_token(cursor), kernel->time_quantum, 1, 16,
                                      "usage: sched mlfq [q] (q must be 1..16)", &quantum)) {
            return;
        }
        if (kernel_set_scheduler(kernel, "mlfq", quantum)) {
            printf("[scheduler] policy=MLFQ base_quantum=%d levels=%d aging=%d\n", kernel->time_quantum,
                   KERNEL_MLFQ_LEVELS, KERNEL_MLFQ_AGING_TICKS);
        }
        return;
    }
    if (kernel_set_scheduler(kernel, mode, kernel->time_quantum)) {
        printf("[scheduler] policy=%s quantum=%d\n", kernel_scheduler_name(kernel), kernel->time_quantum);
    } else {
        printf("unknown scheduler: %s (use rr|mlfq|fcfs|sjf|priority|compare|demo)\n", mode);
    }
}

static int find_access_frame(const int *frame_pid, const int *frame_page, int frame_count, int pid, int page) {
    int i;
    for (i = 0; i < frame_count; i++) {
        if (frame_pid[i] == pid && frame_page[i] == page) {
            return i;
        }
    }
    return -1;
}

static void print_access_frames(const int *frame_pid, const int *frame_page, int frame_count) {
    int i;
    for (i = 0; i < frame_count; i++) {
        if (frame_pid[i] < 0) {
            printf("- ");
        } else {
            printf("P%d:%d ", frame_pid[i], frame_page[i]);
        }
    }
}

static void print_access_page_report(const MiniKernel *kernel, int frames, int use_lru) {
    int frame_pid[MAX_FRAMES];
    int frame_page[MAX_FRAMES];
    int loaded_at[MAX_FRAMES];
    int last_used[MAX_FRAMES];
    int faults = 0;
    int hits = 0;
    int step;
    int i;

    frames = clamp_int(frames, 1, MAX_FRAMES);
    printf("算法: %s, 物理块数=%d, 访问次数=%d\n", use_lru ? "LRU" : "FIFO", frames, kernel->access_count);
    printf("%-6s %-5s %-6s %-28s %-8s %-8s\n", "Step", "PID", "Page", "Frames", "Result", "Evict");
    for (i = 0; i < frames; i++) {
        frame_pid[i] = -1;
        frame_page[i] = -1;
        loaded_at[i] = -1;
        last_used[i] = -1;
    }
    for (step = 0; step < kernel->access_count; step++) {
        /* Page identity is scoped by PID during replay. */
        int pid = kernel->access_pid[step];
        int page = kernel->access_page[step];
        int slot = find_access_frame(frame_pid, frame_page, frames, pid, page);
        int evict_pid = -1;
        int evict_page = -1;
        if (slot >= 0) {
            hits++;
            last_used[slot] = step;
        } else {
            faults++;
            slot = find_access_frame(frame_pid, frame_page, frames, -1, -1);
            if (slot < 0) {
                int best_value = use_lru ? last_used[0] : loaded_at[0];
                slot = 0;
                for (i = 1; i < frames; i++) {
                    int value = use_lru ? last_used[i] : loaded_at[i];
                    if (value < best_value) {
                        best_value = value;
                        slot = i;
                    }
                }
                evict_pid = frame_pid[slot];
                evict_page = frame_page[slot];
            }
            frame_pid[slot] = pid;
            frame_page[slot] = page;
            loaded_at[slot] = step;
            last_used[slot] = step;
        }
        printf("%-6d %-5d %-6d ", step + 1, pid, page);
        print_access_frames(frame_pid, frame_page, frames);
        for (i = 0; i < 28 - frames * 6; i++) {
            printf(" ");
        }
        printf("%-8s ", find_access_frame(frame_pid, frame_page, frames, pid, page) >= 0 && evict_pid == -1 &&
                              loaded_at[slot] != step
                          ? "hit"
                          : (slot >= 0 && frame_pid[slot] == pid && frame_page[slot] == page && evict_pid == -1 &&
                                 loaded_at[slot] == step
                             ? "fault"
                             : (evict_pid >= 0 ? "fault" : "hit")));
        if (evict_pid >= 0) {
            printf("P%d:%d", evict_pid, evict_page);
        }
        printf("\n");
    }
    printf("summary: faults=%d hits=%d fault_rate=%.1f%%\n", faults, hits,
           kernel->access_count > 0 ? (double)faults * 100.0 / kernel->access_count : 0.0);
}

static void handle_page_command(MiniKernel *kernel, char **cursor) {
    char *arg = read_token(cursor);
    int frames;
    if (arg != NULL && strcmp(arg, "demo") == 0) {
        if (!parse_optional_int_range(read_token(cursor), 3, 1, MAX_FRAMES,
                                      "usage: page demo [frames] (frames must be 1..32)", &frames)) {
            return;
        }
        run_page_demo(frames);
        return;
    }
    if (!parse_optional_int_range(arg, kernel->page_frames, 1, MAX_FRAMES,
                                  "usage: page [frames] (frames must be 1..32)", &frames)) {
        return;
    }
    kernel->page_frames = frames;
    if (kernel->access_count <= 0) {
        printf("[vm] no page access history; use `access <pid> <page>` first or `page demo`\n");
        return;
    }
    section("当前 access 历史：FIFO 页面置换");
    print_access_page_report(kernel, kernel->page_frames, 0);
    section("当前 access 历史：LRU 页面置换");
    print_access_page_report(kernel, kernel->page_frames, 1);
}

static void print_kernel_vm_brief(const MiniKernel *kernel) {
    int i;
    int frames = clamp_int(kernel->page_frames, 1, MAX_FRAMES);
    printf("[vm] online policy=%s frames=%d faults=%d hits=%d fault_rate=%.1f%%\n", kernel_vm_policy_name(kernel), frames,
           kernel->vm_faults, kernel->vm_hits,
           kernel->access_count > 0 ? (double)kernel->vm_faults * 100.0 / kernel->access_count : 0.0);
    printf("frames: ");
    for (i = 0; i < frames; i++) {
        if (kernel->vm_frame_pid[i] < 0) {
            printf("[%d:-] ", i);
        } else {
            printf("[%d:P%d:%d] ", i, kernel->vm_frame_pid[i], kernel->vm_frame_page[i]);
        }
    }
    printf("\nuse: vm fifo | vm lru | vm reset | vm frames <n>\n");
}

static void handle_vm_command(MiniKernel *kernel, char **cursor) {
    char *mode = read_token(cursor);
    if (mode == NULL) {
        print_kernel_vm_brief(kernel);
        return;
    }
    if (strcmp(mode, "reset") == 0) {
        kernel_reset_vm(kernel);
        kernel_log_event(kernel, "vm reset frames/statistics");
        printf("[vm] reset online frame table\n");
        return;
    }
    if (strcmp(mode, "frames") == 0) {
        int frames;
        if (!parse_required_int_range(read_token(cursor), 1, MAX_FRAMES,
                                      "usage: vm frames <n> (n must be 1..32)", &frames)) {
            return;
        }
        kernel->page_frames = frames;
        kernel_reset_vm(kernel);
        kernel_log_event(kernel, "vm reset after frame-count change");
        printf("[vm] frames=%d and online frame table reset\n", kernel->page_frames);
        return;
    }
    if (kernel_set_vm_policy(kernel, mode)) {
        printf("[vm] policy=%s and online frame table reset\n", kernel_vm_policy_name(kernel));
    } else {
        printf("unknown vm command: %s (use fifo|lru|reset|frames)\n", mode);
    }
}

static void run_sync_tour(const char *target) {
    int all = target == NULL || strcmp(target, "all") == 0;
    if (all || strcmp(target, "pc") == 0 || strcmp(target, "producer-consumer") == 0) {
        section("进程同步：生产者-消费者");
        run_producer_consumer_demo(2, 2, 3, 3);
    }
    if (all || strcmp(target, "rw") == 0 || strcmp(target, "readers-writers") == 0) {
        section("进程同步：读者-写者");
        run_readers_writers_demo(3, 2, 2, 2);
    }
    if (all || strcmp(target, "dp") == 0 || strcmp(target, "dining-philosophers") == 0) {
        section("进程同步：哲学家进餐");
        run_dining_philosophers_demo(5, 2);
    }
    if (!all && strcmp(target, "pc") != 0 && strcmp(target, "producer-consumer") != 0 &&
        strcmp(target, "rw") != 0 && strcmp(target, "readers-writers") != 0 &&
        strcmp(target, "dp") != 0 && strcmp(target, "dining-philosophers") != 0) {
        printf("unknown sync target: %s (use all|pc|rw|dp)\n", target);
    }
}

static int run_shell_command(MiniKernel *kernel, char *line, char *cwd, size_t cwd_size) {
    char *cursor = line;
    char *command = read_token(&cursor);
    char message[MAX_MESSAGE_LEN];
    char output[MAX_CONTENT_LEN];
    char path[MAX_PATH_LEN];

    if (command == NULL || command[0] == '\0' || command[0] == '#') {
        return 1;
    }
    if (strcmp(command, "help") == 0) {
        print_shell_help();
    } else if (strcmp(command, "man") == 0) {
        print_man_command(read_token(&cursor));
    } else if (strcmp(command, "about") == 0 || strcmp(command, "overview") == 0) {
        print_project_overview();
    } else if (strcmp(command, "uname") == 0) {
        print_uname(kernel);
    } else if (strcmp(command, "uptime") == 0) {
        print_uptime(kernel);
    } else if (strcmp(command, "whoami") == 0) {
        printf("root\n");
    } else if (strcmp(command, "hostname") == 0) {
        printf("minios\n");
    } else if (strcmp(command, "pwd") == 0) {
        printf("%s\n", cwd == NULL || cwd[0] == '\0' ? "/" : cwd);
    } else if (strcmp(command, "cd") == 0) {
        char *target = read_token(&cursor);
        normalize_shell_path(cwd, target == NULL ? "/" : target, path, sizeof(path));
        if (fs_is_dir(&kernel->fs, path)) {
            safe_copy(cwd, cwd_size, path);
        } else {
            printf("cd: no such directory: %s\n", path);
        }
    } else if (strcmp(command, "clear") == 0) {
        printf("\033[2J\033[H");
    } else if (strcmp(command, "echo") == 0) {
        char *text = read_rest(&cursor);
        printf("%s\n", text == NULL ? "" : text);
    } else if (strcmp(command, "note") == 0) {
        char *text = read_rest(&cursor);
        printf("[tour] %s\n", text == NULL ? "" : text);
    } else if (strcmp(command, "status") == 0 || strcmp(command, "ps") == 0 || strcmp(command, "top") == 0) {
        kernel_status(kernel);
    } else if (strcmp(command, "jobs") == 0) {
        print_jobs(kernel);
    } else if (strcmp(command, "dmesg") == 0) {
        kernel_print_dmesg(kernel);
    } else if (strcmp(command, "create") == 0) {
        char *name = read_token(&cursor);
        char *burst_text = read_token(&cursor);
        char *mem_text = read_token(&cursor);
        char *priority_text = read_token(&cursor);
        int burst;
        int mem;
        int priority;
        int pid;
        if (name == NULL) {
            printf("usage: create <name> <burst> <mem> [priority]\n");
            return 1;
        }
        if (!parse_required_int_range(burst_text, 1, 100000, "usage: create <name> <burst> <mem> [priority]", &burst) ||
            !parse_required_int_range(mem_text, 1, 100000, "usage: create <name> <burst> <mem> [priority]", &mem) ||
            !parse_optional_int_range(priority_text, 5, 1, 100000, "usage: create <name> <burst> <mem> [priority]",
                                      &priority)) {
            return 1;
        }
        pid = kernel_create_process_with_priority(kernel, name, burst, mem, priority);
        if (pid > 0) {
            printf("[syscall] create_process -> pid=%d name=%s burst=%d mem=%d priority=%d\n", pid, name, burst, mem, priority);
        } else {
            printf("[syscall] create_process failed\n");
        }
    } else if (strcmp(command, "tick") == 0 || strcmp(command, "run") == 0) {
        int count;
        int start_log = kernel->log_count;
        int i;
        if (!parse_optional_int_range(read_token(&cursor), 1, 1, 100000, "usage: tick|run [n] (n must be 1..100000)",
                                      &count)) {
            return 1;
        }
        for (i = 0; i < count; i++) {
            kernel_schedule_tick(kernel);
        }
        printf("[timer] advanced %d tick(s)\n", count);
        print_new_kernel_logs(kernel, start_log);
    } else if (strcmp(command, "sleep") == 0 || strcmp(command, "block") == 0) {
        char *pid_text = read_token(&cursor);
        char *tick_text = read_token(&cursor);
        int pid;
        int ticks;
        int start_log = kernel->log_count;
        if (!parse_required_int_range(pid_text, 1, INT_MAX, "usage: sleep <pid> <ticks>", &pid) ||
            !parse_required_int_range(tick_text, 1, 100000, "usage: sleep <pid> <ticks>", &ticks)) {
            return 1;
        }
        if (kernel_sleep_process(kernel, pid, ticks)) {
            printf("[syscall] sleep -> pid=%d blocked for %d tick(s)\n", pid, ticks);
            print_new_kernel_logs(kernel, start_log);
        } else {
            printf("usage: sleep <pid> <ticks> (pid must be READY/RUNNING/BLOCKED and not MEM_WAIT/terminated)\n");
        }
    } else if (strcmp(command, "kill") == 0) {
        char *pid_text = read_token(&cursor);
        int pid;
        int start_log = kernel->log_count;
        if (!parse_required_int_range(pid_text, 1, INT_MAX, "usage: kill <pid>", &pid)) {
            return 1;
        }
        printf("%s\n", kernel_kill_process(kernel, pid) ? "[syscall] kill -> process terminated" : "[syscall] kill failed");
        print_new_kernel_logs(kernel, start_log);
    } else if (strcmp(command, "mkdir") == 0) {
        char *arg = read_token(&cursor);
        normalize_shell_path(cwd, arg, path, sizeof(path));
        int ok = fs_mkdir(&kernel->fs, arg == NULL ? NULL : path, message, sizeof(message));
        printf("%s\n", ok ? message : message);
        (void)snprintf(output, sizeof(output), "fs mkdir path=%s result=%s", arg == NULL ? "(null)" : path, ok ? "OK" : "FAIL");
        kernel_log_event(kernel, output);
    } else if (strcmp(command, "rmdir") == 0) {
        char *arg = read_token(&cursor);
        normalize_shell_path(cwd, arg, path, sizeof(path));
        int ok = fs_rmdir(&kernel->fs, arg == NULL ? NULL : path, message, sizeof(message));
        printf("%s\n", ok ? message : message);
        (void)snprintf(output, sizeof(output), "fs rmdir path=%s result=%s", arg == NULL ? "(null)" : path, ok ? "OK" : "FAIL");
        kernel_log_event(kernel, output);
    } else if (strcmp(command, "touch") == 0) {
        char *arg = read_token(&cursor);
        char *content = read_rest(&cursor);
        normalize_shell_path(cwd, arg, path, sizeof(path));
        int ok = fs_create(&kernel->fs, arg == NULL ? NULL : path, content == NULL ? "" : content, message, sizeof(message));
        printf("%s\n", ok ? message : message);
        (void)snprintf(output, sizeof(output), "fs create path=%s bytes=%zu result=%s", arg == NULL ? "(null)" : path,
                       content == NULL ? 0U : strlen(content), ok ? "OK" : "FAIL");
        kernel_log_event(kernel, output);
    } else if (strcmp(command, "put") == 0) {
        char *arg = read_token(&cursor);
        char *content = read_rest(&cursor);
        char decoded[MAX_CONTENT_LEN];
        int ok;
        normalize_shell_path(cwd, arg, path, sizeof(path));
        if (arg == NULL) {
            printf("usage: put <path> <content>\n");
            return 1;
        }
        decode_shell_escapes(content == NULL ? "" : content, decoded, sizeof(decoded));
        ok = fs_is_file(&kernel->fs, path)
                 ? fs_write(&kernel->fs, path, decoded, 0, message, sizeof(message))
                 : fs_create(&kernel->fs, path, decoded, message, sizeof(message));
        printf("%s\n", ok ? message : message);
        (void)snprintf(output, sizeof(output), "fs put path=%s bytes=%zu result=%s", path,
                       strlen(decoded), ok ? "OK" : "FAIL");
        kernel_log_event(kernel, output);
    } else if (strcmp(command, "write") == 0) {
        char *arg = read_token(&cursor);
        char *content = read_rest(&cursor);
        normalize_shell_path(cwd, arg, path, sizeof(path));
        int ok = fs_write(&kernel->fs, arg == NULL ? NULL : path, content == NULL ? "" : content, 1, message, sizeof(message));
        printf("%s\n", ok ? message : message);
        (void)snprintf(output, sizeof(output), "fs append path=%s bytes=%zu result=%s", arg == NULL ? "(null)" : path,
                       content == NULL ? 0U : strlen(content), ok ? "OK" : "FAIL");
        kernel_log_event(kernel, output);
    } else if (strcmp(command, "cat") == 0) {
        char *arg = read_token(&cursor);
        normalize_shell_path(cwd, arg, path, sizeof(path));
        if (fs_read(&kernel->fs, arg == NULL ? NULL : path, output, sizeof(output), message, sizeof(message))) {
            printf("%s\n", output);
            (void)snprintf(message, sizeof(message), "fs read path=%s result=OK", arg == NULL ? "(null)" : path);
            kernel_log_event(kernel, message);
        } else {
            printf("%s\n", message);
            (void)snprintf(output, sizeof(output), "fs read path=%s result=FAIL", arg == NULL ? "(null)" : path);
            kernel_log_event(kernel, output);
        }
    } else if (strcmp(command, "rm") == 0) {
        char *arg = read_token(&cursor);
        normalize_shell_path(cwd, arg, path, sizeof(path));
        int ok = kernel_delete_file(kernel, arg == NULL ? NULL : path);
        (void)snprintf(message, sizeof(message), "%s %s", ok ? "deleted" : "delete failed",
                       arg == NULL ? "(null)" : path);
        printf("%s\n", message);
        (void)snprintf(output, sizeof(output), "fs delete path=%s result=%s", arg == NULL ? "(null)" : path, ok ? "OK" : "FAIL");
        kernel_log_event(kernel, output);
    } else if (strcmp(command, "open") == 0) {
        char *arg = read_token(&cursor);
        char *mode = read_token(&cursor);
        int fd;
        normalize_shell_path(cwd, arg, path, sizeof(path));
        fd = kernel_open_file(kernel, arg == NULL ? NULL : path, mode == NULL ? "r" : mode);
        if (fd >= 0) {
            printf("[syscall] open -> fd=%d path=%s mode=%s\n", fd, path, mode == NULL ? "r" : mode);
        } else {
            printf("usage: open <path> [r|w|a|rw]\n");
        }
    } else if (strcmp(command, "readfd") == 0) {
        char *fd_text = read_token(&cursor);
        char *len_text = read_token(&cursor);
        int fd;
        int max_bytes = -1;
        int bytes;
        if (!parse_required_int_range(fd_text, 3, 3 + MAX_KERNEL_FDS - 1,
                                      "usage: readfd <fd> [n] (fd must be an integer >= 3)", &fd) ||
            !parse_optional_int_range(len_text, -1, 1, MAX_CONTENT_LEN,
                                      "usage: readfd <fd> [n] (n must be 1..4096)", &max_bytes)) {
            return 1;
        }
        bytes = kernel_read_fd_count(kernel, fd, max_bytes, output, sizeof(output));
        if (bytes >= 0) {
            if (bytes > 0) {
                printf("%s\n", output);
            }
            printf("[syscall] readfd -> fd=%d bytes=%d%s\n", fd, bytes, bytes == 0 ? " EOF" : "");
        } else {
            printf("usage: readfd <fd> [n] (fd must be open for read)\n");
        }
    } else if (strcmp(command, "writefd") == 0) {
        char *fd_text = read_token(&cursor);
        char *content = read_rest(&cursor);
        int fd;
        int bytes;
        if (!parse_required_int_range(fd_text, 3, 3 + MAX_KERNEL_FDS - 1,
                                      "usage: writefd <fd> <content> (fd must be an integer >= 3)", &fd)) {
            return 1;
        }
        bytes = kernel_write_fd(kernel, fd, content == NULL ? "" : content);
        if (bytes >= 0) {
            printf("[syscall] writefd -> fd=%d bytes=%d\n", fd, bytes);
        } else {
            printf("usage: writefd <fd> <content> (fd must be open for write/append)\n");
        }
    } else if (strcmp(command, "close") == 0) {
        char *fd_text = read_token(&cursor);
        int fd;
        if (!parse_required_int_range(fd_text, 3, 3 + MAX_KERNEL_FDS - 1,
                                      "usage: close <fd> (fd must be an integer >= 3)", &fd)) {
            return 1;
        }
        printf("%s\n", kernel_close_fd(kernel, fd) ? "[syscall] close -> fd closed" : "[syscall] close failed");
    } else if (strcmp(command, "seekfd") == 0) {
        char *fd_text = read_token(&cursor);
        char *offset_text = read_token(&cursor);
        int fd;
        int offset;
        if (!parse_required_int_range(fd_text, 3, 3 + MAX_KERNEL_FDS - 1,
                                      "usage: seekfd <fd> <offset> (fd must be an integer >= 3)", &fd) ||
            !parse_required_int_range(offset_text, 0, MAX_CONTENT_LEN,
                                      "usage: seekfd <fd> <offset> (offset must be 0..4096)", &offset)) {
            return 1;
        }
        if (kernel_seek_fd(kernel, fd, offset)) {
            printf("[syscall] seekfd -> fd=%d offset=%d\n", fd, offset);
        } else {
            printf("usage: seekfd <fd> <offset> (fd must be open and offset >= 0)\n");
        }
    } else if (strcmp(command, "fd") == 0 || strcmp(command, "fds") == 0 || strcmp(command, "lsof") == 0) {
        print_lsof(kernel);
    } else if (strcmp(command, "savefs") == 0) {
        char *path = read_token(&cursor);
        int ok = fs_save_image(&kernel->fs, path, message, sizeof(message));
        printf("%s\n", message);
        (void)snprintf(output, sizeof(output), "fs image save path=%s result=%s", path == NULL ? "(null)" : path,
                       ok ? "OK" : "FAIL");
        kernel_log_event(kernel, output);
    } else if (strcmp(command, "loadfs") == 0) {
        char *path = read_token(&cursor);
        int ok = fs_load_image(&kernel->fs, path, message, sizeof(message));
        if (ok) {
            memset(kernel->open_files, 0, sizeof(kernel->open_files));
        }
        printf("%s\n", message);
        (void)snprintf(output, sizeof(output), "fs image load path=%s result=%s", path == NULL ? "(null)" : path,
                       ok ? "OK" : "FAIL");
        kernel_log_event(kernel, output);
    } else if (strcmp(command, "fscheck") == 0) {
        char *path = read_token(&cursor);
        int ok = fs_check_image(path, message, sizeof(message));
        printf("%s\n", message);
        (void)snprintf(output, sizeof(output), "fs image check path=%s result=%s", path == NULL ? "(null)" : path,
                       ok ? "OK" : "FAIL");
        kernel_log_event(kernel, output);
    } else if (strcmp(command, "ls") == 0) {
        char *arg = read_token(&cursor);
        normalize_shell_path(cwd, arg, path, sizeof(path));
        fs_list_dir(&kernel->fs, path);
    } else if (strcmp(command, "tree") == 0) {
        fs_print_tree(&kernel->fs);
        fs_print_bitmap(&kernel->fs);
    } else if (strcmp(command, "df") == 0) {
        print_df(&kernel->fs);
    } else if (strcmp(command, "mount") == 0) {
        printf("tinyfs on / type tinyfs (rw,blocks=%d,block_size=%d)\n", kernel->fs.total_blocks, kernel->fs.block_size);
    } else if (strcmp(command, "mem") == 0 || strcmp(command, "free") == 0) {
        print_partition_summary(&kernel->memory);
    } else if (strcmp(command, "sched") == 0) {
        handle_sched_command(kernel, &cursor);
    } else if (strcmp(command, "access") == 0) {
        char *pid_text = read_token(&cursor);
        char *page_text = read_token(&cursor);
        int pid;
        int page;
        int start_log = kernel->log_count;
        if (!parse_required_int_range(pid_text, 1, INT_MAX, "usage: access <pid> <page>", &pid) ||
            !parse_required_int_range(page_text, 0, INT_MAX, "usage: access <pid> <page>", &page)) {
            return 1;
        }
        if (kernel_record_page_access(kernel, pid, page)) {
            printf("[vm] access pid=%d page=%d recorded\n", pid, page);
            print_new_kernel_logs(kernel, start_log);
        } else {
            printf("usage: access <pid> <page> (pid must exist and not be terminated)\n");
        }
    } else if (strcmp(command, "vm") == 0) {
        handle_vm_command(kernel, &cursor);
    } else if (strcmp(command, "page") == 0) {
        handle_page_command(kernel, &cursor);
    } else if (strcmp(command, "realtime") == 0) {
        int simulation_time;
        if (!parse_optional_int_range(read_token(&cursor), 20, 1, 200,
                                      "usage: realtime [time] (time must be 1..200)", &simulation_time)) {
            return 1;
        }
        print_rt_comparison(DEFAULT_RT_TASKS, DEFAULT_RT_TASK_COUNT, simulation_time);
    } else if (strcmp(command, "sync") == 0) {
        run_sync_tour(read_token(&cursor));
    } else if (strcmp(command, "tracebench") == 0 || strcmp(command, "bench") == 0) {
        char *extra = read_token(&cursor);
        if (extra != NULL && strcmp(command, "tracebench") == 0) {
            printf("usage: tracebench\n");
            return 1;
        }
        if (extra != NULL) {
            printf("[bench] 兼容入口已切换为 tracebench，忽略旧 scale/iterations 参数。\n");
        }
        run_tracebench_suite();
    } else if (strcmp(command, "benchsubset") == 0 || strcmp(command, "bench-subset") == 0) {
        int scale;
        int iterations;
        if (!parse_optional_int_range(read_token(&cursor), 16, 4, 1024,
                                      "usage: benchsubset [scale] [iterations] (scale must be 4..1024)", &scale) ||
            !parse_optional_int_range(read_token(&cursor), 3, 1, 100000,
                                      "usage: benchsubset [scale] [iterations] (iterations must be 1..100000)",
                                      &iterations)) {
            return 1;
        }
        run_benchmark_subset_suite(scale, iterations);
    } else if (strcmp(command, "autotune") == 0) {
        int scale;
        int iterations;
        if (!parse_optional_int_range(read_token(&cursor), 16, 4, 1024,
                                      "usage: autotune [scale] [iterations] (scale must be 4..1024)", &scale) ||
            !parse_optional_int_range(read_token(&cursor), 3, 1, 100000,
                                      "usage: autotune [scale] [iterations] (iterations must be 1..100000)",
                                      &iterations)) {
            return 1;
        }
        run_autotune_suite(scale, iterations);
    } else if (strcmp(command, "benchcsv") == 0) {
        char *path = read_token(&cursor);
        int scale;
        int iterations;
        if (!parse_optional_int_range(read_token(&cursor), 32, 4, 1024,
                                      "usage: benchcsv <path> [scale] [iterations] (scale must be 4..1024)",
                                      &scale) ||
            !parse_optional_int_range(read_token(&cursor), 50, 1, 100000,
                                      "usage: benchcsv <path> [scale] [iterations] (iterations must be 1..100000)",
                                      &iterations)) {
            return 1;
        }
        if (path == NULL) {
            printf("usage: benchcsv <path> [scale] [iterations]\n");
        } else if (write_benchmark_csv(path, scale, iterations, message, sizeof(message))) {
            printf("[benchcsv] %s\n", message);
        } else {
            printf("[benchcsv] failed: %s\n", message);
        }
    } else if (strcmp(command, "perfreport") == 0) {
        char *path = read_token(&cursor);
        int scale;
        int iterations;
        if (!parse_optional_int_range(read_token(&cursor), 32, 4, 1024,
                                      "usage: perfreport <path> [scale] [iterations] (scale must be 4..1024)",
                                      &scale) ||
            !parse_optional_int_range(read_token(&cursor), 50, 1, 100000,
                                      "usage: perfreport <path> [scale] [iterations] (iterations must be 1..100000)",
                                      &iterations)) {
            return 1;
        }
        if (path == NULL) {
            printf("usage: perfreport <path> [scale] [iterations]\n");
        } else if (write_performance_report(path, scale, iterations, message, sizeof(message))) {
            printf("[perfreport] %s\n", message);
        } else {
            printf("[perfreport] failed: %s\n", message);
        }
    } else if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0 || strcmp(command, "shutdown") == 0) {
        printf("MiniOS shutdown.\n");
        return 0;
    } else {
        printf("unknown command: %s (输入 help 查看命令)\n", command);
    }
    return 1;
}

static int command_tour(int argc, char **argv, int first_arg) {
    MiniKernel kernel;
    FILE *input = stdin;
    int scripted = 0;
    int close_input = 0;
    int delay_ms = 0;
    int type_ms = 0;
    int clear_screen = 0;
    char line[SHELL_LINE_MAX];
    char cwd[MAX_PATH_LEN] = "/";
    int i;

    for (i = first_arg; i < argc; i++) {
        /* --mode expands to the curated Lite/Full tour script paths. */
        if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            const char *script_path = argv[++i];
            scripted = 1;
            if (!set_script_input(&input, &close_input, script_path)) {
                return 1;
            }
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            const char *mode = argv[++i];
            const char *script_path = mode_script_path(mode);
            if (script_path == NULL) {
                fprintf(stderr, "unknown tour mode: %s (use lite|full)\n", mode);
                return 1;
            }
            scripted = 1;
            if (!set_script_input(&input, &close_input, script_path)) {
                return 1;
            }
        } else if (strcmp(argv[i], "--interactive") == 0) {
            scripted = 0;
            if (close_input && input != NULL) {
                fclose(input);
                close_input = 0;
            }
            input = stdin;
        } else if (strcmp(argv[i], "--delay-ms") == 0 && i + 1 < argc) {
            if (!parse_int_strict(argv[++i], &delay_ms) || delay_ms < 0 || delay_ms > 60000) {
                fprintf(stderr, "usage: --delay-ms <0..60000>\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--type-ms") == 0 && i + 1 < argc) {
            if (!parse_int_strict(argv[++i], &type_ms) || type_ms < 0 || type_ms > 60000) {
                fprintf(stderr, "usage: --type-ms <0..60000>\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--cinematic") == 0) {
            scripted = 1;
            delay_ms = 850;
            type_ms = 18;
            clear_screen = 1;
        } else if (strcmp(argv[i], "--clear-screen") == 0) {
            clear_screen = 1;
        } else {
            fprintf(stderr, "unknown tour argument: %s\n", argv[i]);
            return 1;
        }
    }

    kernel_init(&kernel);
    if (clear_screen) {
        printf("\033[2J\033[H");
    }
    print_shell_banner();
    if (!scripted) {
        print_shell_help();
    }
    while (1) {
        if (!scripted) {
            printf("minios:%s:%03d$ ", cwd, kernel.tick);
            fflush(stdout);
        }
        if (fgets(line, sizeof(line), input) == NULL) {
            break;
        }
        if (scripted) {
            char prompt[192];
            sleep_ms(delay_ms);
            (void)snprintf(prompt, sizeof(prompt), "minios:%s:%03d$ ", cwd, kernel.tick);
            printf("%s", prompt);
            fflush(stdout);
            print_typed_text(line, type_ms);
        }
        if (!run_shell_command(&kernel, line, cwd, sizeof(cwd))) {
            break;
        }
        if (scripted) {
            sleep_ms(delay_ms);
        }
    }
    if (close_input) {
        fclose(input);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        return command_tour(argc, argv, 1);
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage();
        return 0;
    }
    if (strcmp(argv[1], "tour") == 0) {
        return command_tour(argc, argv, 2);
    }
    if (strcmp(argv[1], "--script") == 0 || strcmp(argv[1], "--interactive") == 0) {
        return command_tour(argc, argv, 1);
    }
    fprintf(stderr, "MiniOS 现在只保留 tour 导览模式；请进入 Shell 后使用 `%s` 命令。\n\n", suggest_shell_command(argv[1]));
    usage();
    return 1;
}
