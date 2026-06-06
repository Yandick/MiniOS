#include "os_project.h"
#include <stdlib.h>
#include <string.h>

/*
 * Processor scheduling module.
 * Implements FCFS, SJF, priority, HRRN, and round-robin using the
 * same Process input and ScheduleReport output format.
 */

static int validate_processes(const Process *processes, int count) {
    int i;
    int j;
    if (processes == NULL || count <= 0 || count > MAX_PROCESSES) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        if (processes[i].pid[0] == '\0' || processes[i].arrival < 0 || processes[i].burst <= 0) {
            return 0;
        }
        for (j = i + 1; j < count; j++) {
            if (strcmp(processes[i].pid, processes[j].pid) == 0) {
                return 0;
            }
        }
    }
    return 1;
}

static void append_segment(ScheduleReport *report, const char *pid, int start, int end) {
    ScheduleSegment *segment = NULL;
    if (report == NULL || start >= end || report->timeline_count >= MAX_SEGMENTS) {
        return;
    }
    segment = &report->timeline[report->timeline_count++];
    safe_copy(segment->pid, sizeof(segment->pid), pid);
    segment->start = start;
    segment->end = end;
}

static void compute_metrics(
    ScheduleReport *report,
    const Process *processes,
    int count,
    const int *starts,
    const int *completions
) {
    int i;
    int total_turnaround = 0;
    int total_waiting = 0;
    int total_response = 0;
    double total_weighted_turnaround = 0.0;
    int busy_time = 0;
    int span = 0;

    /* Metrics are derived from first start and final completion timestamps. */
    report->process_count = count;
    for (i = 0; i < count; i++) {
        ProcessMetric *metric = &report->metrics[i];
        safe_copy(metric->pid, sizeof(metric->pid), processes[i].pid);
        metric->arrival = processes[i].arrival;
        metric->burst = processes[i].burst;
        metric->priority = processes[i].priority;
        metric->start = starts[i];
        metric->completion = completions[i];
        metric->turnaround = metric->completion - metric->arrival;
        metric->waiting = metric->turnaround - metric->burst;
        metric->response = metric->start - metric->arrival;
        metric->weighted_turnaround = (double)metric->turnaround / metric->burst;
        total_turnaround += metric->turnaround;
        total_waiting += metric->waiting;
        total_response += metric->response;
        total_weighted_turnaround += metric->weighted_turnaround;
        busy_time += processes[i].burst;
    }
    if (report->timeline_count > 0) {
        span = report->timeline[report->timeline_count - 1].end - report->timeline[0].start;
    }
    report->average_turnaround = (double)total_turnaround / count;
    report->average_waiting = (double)total_waiting / count;
    report->average_response = (double)total_response / count;
    report->average_weighted_turnaround = total_weighted_turnaround / count;
    report->cpu_utilization = span > 0 ? (double)busy_time / span : 0.0;
    report->throughput = span > 0 ? (double)count / span : 0.0;
}

static int earliest_arrival(const Process *processes, int count) {
    int i;
    int value = processes[0].arrival;
    for (i = 1; i < count; i++) {
        if (processes[i].arrival < value) {
            value = processes[i].arrival;
        }
    }
    return value;
}

static void sort_indices_by_arrival(const Process *processes, int count, int *order) {
    int i;
    int changed;
    for (i = 0; i < count; i++) {
        order[i] = i;
    }
    do {
        changed = 0;
        for (i = 0; i < count - 1; i++) {
            int left = order[i];
            int right = order[i + 1];
            if (processes[left].arrival > processes[right].arrival) {
                order[i] = right;
                order[i + 1] = left;
                changed = 1;
            }
        }
    } while (changed != 0);
}

int load_processes_csv(const char *path, Process *processes, int max_count, char *error, size_t error_size) {
    FILE *file = NULL;
    char line[256];
    int count = 0;
    int line_no = 0;

    if (path == NULL || processes == NULL || max_count <= 0) {
        safe_copy(error, error_size, "invalid process loader arguments");
        return -1;
    }
    file = fopen(path, "r");
    if (file == NULL) {
        safe_copy(error, error_size, "cannot open process CSV file");
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *pid = NULL;
        char *arrival = NULL;
        char *burst = NULL;
        char *priority = NULL;
        char *first = NULL;
        int parsed_arrival;
        int parsed_burst;
        int parsed_priority = 0;
        line_no++;
        first = trim(line);
        if (first[0] == '\0' || first[0] == '#') {
            continue;
        }
        if (strncmp(first, "pid", 3) == 0 || strncmp(first, "PID", 3) == 0) {
            continue;
        }
        if (count >= max_count) {
            safe_copy(error, error_size, "too many processes in CSV file");
            (void)fclose(file);
            return -1;
        }
        pid = trim(strtok(first, ","));
        arrival = trim(strtok(NULL, ","));
        burst = trim(strtok(NULL, ","));
        priority = trim(strtok(NULL, ","));
        if (pid == NULL || arrival == NULL || burst == NULL) {
            (void)snprintf(error, error_size, "invalid process CSV format at line %d", line_no);
            (void)fclose(file);
            return -1;
        }
        if (!parse_int_value(arrival, &parsed_arrival) || !parse_int_value(burst, &parsed_burst) ||
            (priority != NULL && !parse_int_value(priority, &parsed_priority))) {
            (void)snprintf(error, error_size, "invalid numeric process CSV field at line %d", line_no);
            (void)fclose(file);
            return -1;
        }
        safe_copy(processes[count].pid, sizeof(processes[count].pid), pid);
        processes[count].arrival = parsed_arrival;
        processes[count].burst = parsed_burst;
        processes[count].priority = parsed_priority;
        count++;
    }
    (void)fclose(file);
    if (!validate_processes(processes, count)) {
        safe_copy(error, error_size, "process data failed validation");
        return -1;
    }
    return count;
}

int read_processes_interactive(Process *processes, int max_count) {
    int count;
    int i;

    if (processes == NULL || max_count <= 0) {
        return -1;
    }
    printf("请输入进程数量(1-%d): ", max_count);
    if (scanf("%d", &count) != 1 || count <= 0 || count > max_count) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        printf("进程%d: PID 到达时间 运行时间 优先级: ", i + 1);
        if (scanf("%31s %d %d %d", processes[i].pid, &processes[i].arrival, &processes[i].burst, &processes[i].priority) != 4) {
            return -1;
        }
    }
    return validate_processes(processes, count) ? count : -1;
}

ScheduleReport schedule_fcfs(const Process *processes, int count) {
    ScheduleReport report;
    int order[MAX_PROCESSES];
    int starts[MAX_PROCESSES];
    int completions[MAX_PROCESSES];
    int time = 0;
    int i;

    memset(&report, 0, sizeof(report));
    safe_copy(report.algorithm, sizeof(report.algorithm), "FCFS");
    if (!validate_processes(processes, count)) {
        return report;
    }
    sort_indices_by_arrival(processes, count, order);
    for (i = 0; i < count; i++) {
        int index = order[i];
        if (time < processes[index].arrival) {
            append_segment(&report, "IDLE", time, processes[index].arrival);
            time = processes[index].arrival;
        }
        starts[index] = time;
        time += processes[index].burst;
        completions[index] = time;
        append_segment(&report, processes[index].pid, starts[index], completions[index]);
    }
    compute_metrics(&report, processes, count, starts, completions);
    return report;
}

ScheduleReport schedule_sjf(const Process *processes, int count) {
    ScheduleReport report;
    int done[MAX_PROCESSES] = {0};
    int starts[MAX_PROCESSES];
    int completions[MAX_PROCESSES];
    int finished = 0;
    int time;

    memset(&report, 0, sizeof(report));
    safe_copy(report.algorithm, sizeof(report.algorithm), "SJF (non-preemptive)");
    if (!validate_processes(processes, count)) {
        return report;
    }
    time = earliest_arrival(processes, count);
    if (time > 0) {
        append_segment(&report, "IDLE", 0, time);
    }
    while (finished < count) {
        /* Non-preemptive SJF chooses the shortest arrived job, then runs it to completion. */
        int best = -1;
        int i;
        for (i = 0; i < count; i++) {
            if (!done[i] && processes[i].arrival <= time) {
                if (best == -1 || processes[i].burst < processes[best].burst ||
                    (processes[i].burst == processes[best].burst && processes[i].arrival < processes[best].arrival) ||
                    (processes[i].burst == processes[best].burst && processes[i].arrival == processes[best].arrival && i < best)) {
                    best = i;
                }
            }
        }
        if (best == -1) {
            int next_arrival = 0;
            int initialized = 0;
            for (i = 0; i < count; i++) {
                if (!done[i] && (!initialized || processes[i].arrival < next_arrival)) {
                    next_arrival = processes[i].arrival;
                    initialized = 1;
                }
            }
            append_segment(&report, "IDLE", time, next_arrival);
            time = next_arrival;
            continue;
        }
        starts[best] = time;
        time += processes[best].burst;
        completions[best] = time;
        done[best] = 1;
        finished++;
        append_segment(&report, processes[best].pid, starts[best], completions[best]);
    }
    compute_metrics(&report, processes, count, starts, completions);
    return report;
}

ScheduleReport schedule_priority(const Process *processes, int count, int lower_number_first) {
    ScheduleReport report;
    int done[MAX_PROCESSES] = {0};
    int starts[MAX_PROCESSES];
    int completions[MAX_PROCESSES];
    int finished = 0;
    int time;

    memset(&report, 0, sizeof(report));
    safe_copy(report.algorithm, sizeof(report.algorithm), lower_number_first ? "Priority (lower number first)" : "Priority (higher number first)");
    if (!validate_processes(processes, count)) {
        return report;
    }
    time = earliest_arrival(processes, count);
    if (time > 0) {
        append_segment(&report, "IDLE", 0, time);
    }
    while (finished < count) {
        /* HRRN uses cross-multiplication to compare response ratios without rounding. */
        int best = -1;
        int i;
        for (i = 0; i < count; i++) {
            if (!done[i] && processes[i].arrival <= time) {
                int better_priority = 0;
                if (best == -1) {
                    better_priority = 1;
                } else if (lower_number_first) {
                    better_priority = processes[i].priority < processes[best].priority;
                } else {
                    better_priority = processes[i].priority > processes[best].priority;
                }
                if (better_priority ||
                    (processes[i].priority == processes[best].priority && processes[i].arrival < processes[best].arrival) ||
                    (processes[i].priority == processes[best].priority && processes[i].arrival == processes[best].arrival && i < best)) {
                    best = i;
                }
            }
        }
        if (best == -1) {
            int next_arrival = 0;
            int initialized = 0;
            for (i = 0; i < count; i++) {
                if (!done[i] && (!initialized || processes[i].arrival < next_arrival)) {
                    next_arrival = processes[i].arrival;
                    initialized = 1;
                }
            }
            append_segment(&report, "IDLE", time, next_arrival);
            time = next_arrival;
            continue;
        }
        starts[best] = time;
        time += processes[best].burst;
        completions[best] = time;
        done[best] = 1;
        finished++;
        append_segment(&report, processes[best].pid, starts[best], completions[best]);
    }
    compute_metrics(&report, processes, count, starts, completions);
    return report;
}

ScheduleReport schedule_hrrn(const Process *processes, int count) {
    ScheduleReport report;
    int done[MAX_PROCESSES] = {0};
    int starts[MAX_PROCESSES];
    int completions[MAX_PROCESSES];
    int finished = 0;
    int time;

    memset(&report, 0, sizeof(report));
    safe_copy(report.algorithm, sizeof(report.algorithm), "HRRN (highest response ratio next)");
    if (!validate_processes(processes, count)) {
        return report;
    }
    time = earliest_arrival(processes, count);
    if (time > 0) {
        append_segment(&report, "IDLE", 0, time);
    }
    while (finished < count) {
        int best = -1;
        int i;
        for (i = 0; i < count; i++) {
            if (!done[i] && processes[i].arrival <= time) {
                int best_left;
                int best_right;
                int current_left;
                int current_right;
                if (best == -1) {
                    best = i;
                    continue;
                }
                current_left = time - processes[i].arrival + processes[i].burst;
                current_right = processes[i].burst;
                best_left = time - processes[best].arrival + processes[best].burst;
                best_right = processes[best].burst;
                if (current_left * best_right > best_left * current_right ||
                    (current_left * best_right == best_left * current_right && processes[i].arrival < processes[best].arrival) ||
                    (current_left * best_right == best_left * current_right && processes[i].arrival == processes[best].arrival && i < best)) {
                    best = i;
                }
            }
        }
        if (best == -1) {
            int next_arrival = 0;
            int initialized = 0;
            for (i = 0; i < count; i++) {
                if (!done[i] && (!initialized || processes[i].arrival < next_arrival)) {
                    next_arrival = processes[i].arrival;
                    initialized = 1;
                }
            }
            append_segment(&report, "IDLE", time, next_arrival);
            time = next_arrival;
            continue;
        }
        starts[best] = time;
        time += processes[best].burst;
        completions[best] = time;
        done[best] = 1;
        finished++;
        append_segment(&report, processes[best].pid, starts[best], completions[best]);
    }
    compute_metrics(&report, processes, count, starts, completions);
    return report;
}

ScheduleReport schedule_round_robin(const Process *processes, int count, int quantum) {
    ScheduleReport report;
    int order[MAX_PROCESSES];
    int remaining[MAX_PROCESSES];
    int starts[MAX_PROCESSES];
    int completions[MAX_PROCESSES];
    int queue[MAX_SEGMENTS];
    int head = 0;
    int tail = 0;
    int cursor = 0;
    int completed = 0;
    int time = 0;
    int i;

    memset(&report, 0, sizeof(report));
    (void)snprintf(report.algorithm, sizeof(report.algorithm), "Round Robin (q=%d)", quantum);
    if (!validate_processes(processes, count) || quantum <= 0) {
        return report;
    }
    sort_indices_by_arrival(processes, count, order);
    for (i = 0; i < count; i++) {
        remaining[i] = processes[i].burst;
        starts[i] = -1;
        completions[i] = 0;
    }

    while (completed < count) {
        /* The ready queue stores process indices in arrival order and requeues unfinished jobs. */
        if (head == tail) {
            int next_index = order[cursor];
            if (time < processes[next_index].arrival) {
                append_segment(&report, "IDLE", time, processes[next_index].arrival);
                time = processes[next_index].arrival;
            }
            while (cursor < count && processes[order[cursor]].arrival <= time) {
                queue[tail++ % MAX_SEGMENTS] = order[cursor++];
            }
        }
        if (head == tail) {
            continue;
        }
        i = queue[head++ % MAX_SEGMENTS];
        if (starts[i] < 0) {
            starts[i] = time;
        }
        {
            int runtime = remaining[i] < quantum ? remaining[i] : quantum;
            int start = time;
            time += runtime;
            remaining[i] -= runtime;
            append_segment(&report, processes[i].pid, start, time);
        }
        while (cursor < count && processes[order[cursor]].arrival <= time) {
            queue[tail++ % MAX_SEGMENTS] = order[cursor++];
        }
        if (remaining[i] > 0) {
            queue[tail++ % MAX_SEGMENTS] = i;
        } else {
            completions[i] = time;
            completed++;
        }
    }
    compute_metrics(&report, processes, count, starts, completions);
    return report;
}

void print_schedule_report(const ScheduleReport *report) {
    int i;
    if (report == NULL || report->process_count <= 0) {
        printf("调度报告为空。\n");
        return;
    }
    printf("算法: %s\n", report->algorithm);
    printf("甘特图: ");
    for (i = 0; i < report->timeline_count; i++) {
        printf("[%d,%d)%s%s", report->timeline[i].start, report->timeline[i].end, report->timeline[i].pid,
               i == report->timeline_count - 1 ? "" : " -> ");
    }
    printf("\n");
    printf("%-8s %-7s %-6s %-8s %-7s %-7s %-10s %-8s %-8s %-9s\n", "PID", "Arrive", "Burst", "Priority", "Start", "Finish", "Turnaround", "Waiting", "Response", "WTurn");
    for (i = 0; i < report->process_count; i++) {
        const ProcessMetric *m = &report->metrics[i];
        printf("%-8s %-7d %-6d %-8d %-7d %-7d %-10d %-8d %-8d %-9.2f\n", m->pid, m->arrival, m->burst, m->priority,
               m->start, m->completion, m->turnaround, m->waiting, m->response, m->weighted_turnaround);
    }
    printf("平均周转时间=%.2f, 平均等待时间=%.2f, 平均响应时间=%.2f, 平均带权周转时间=%.2f, CPU利用率=%.1f%%, 吞吐量=%.3f/process-time\n",
           report->average_turnaround, report->average_waiting, report->average_response, report->average_weighted_turnaround,
           report->cpu_utilization * 100.0, report->throughput);
}

void print_schedule_comparison(const Process *processes, int count, int quantum) {
    ScheduleReport reports[5];
    int i;
    int best = 0;
    reports[0] = schedule_fcfs(processes, count);
    reports[1] = schedule_sjf(processes, count);
    reports[2] = schedule_priority(processes, count, 1);
    reports[3] = schedule_hrrn(processes, count);
    reports[4] = schedule_round_robin(processes, count, quantum);

    printf("%-36s %-12s %-12s %-12s %-12s %-10s\n", "Algorithm", "AvgTurn", "AvgWait", "AvgResp", "AvgWTurn", "CPU");
    for (i = 0; i < 5; i++) {
        if (reports[i].average_waiting < reports[best].average_waiting) {
            best = i;
        }
        printf("%-36s %-12.2f %-12.2f %-12.2f %-12.2f %-9.1f%%\n", reports[i].algorithm,
               reports[i].average_turnaround, reports[i].average_waiting,
               reports[i].average_response, reports[i].average_weighted_turnaround, reports[i].cpu_utilization * 100.0);
    }
    printf("当前工作负载下平均等待时间最优: %s\n", reports[best].algorithm);
    printf("\n--- 详细甘特图与指标 ---\n");
    for (i = 0; i < 5; i++) {
        print_schedule_report(&reports[i]);
        printf("\n");
    }
}

void print_rr_quantum_comparison(const Process *processes, int count, const int *quantums, int quantum_count) {
    int i;
    if (!validate_processes(processes, count) || quantums == NULL || quantum_count <= 0) {
        printf("RR 多时间片对比输入无效。\n");
        return;
    }
    printf("Round Robin 多时间片对比（用于观察时间片对响应时间/等待时间的影响）\n");
    printf("%-8s %-12s %-12s %-12s %-12s %-10s\n", "Quantum", "AvgTurn", "AvgWait", "AvgResp", "AvgWTurn", "CPU");
    for (i = 0; i < quantum_count; i++) {
        ScheduleReport report;
        if (quantums[i] <= 0) {
            continue;
        }
        report = schedule_round_robin(processes, count, quantums[i]);
        printf("%-8d %-12.2f %-12.2f %-12.2f %-12.2f %-9.1f%%\n", quantums[i],
               report.average_turnaround, report.average_waiting, report.average_response,
               report.average_weighted_turnaround, report.cpu_utilization * 100.0);
    }
}
