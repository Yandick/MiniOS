#include "os_project.h"
#include <stdlib.h>
#include <string.h>

/*
 * Real-time scheduling module.
 * Generates periodic jobs and simulates EDF/RMS one tick at a time while
 * collecting deadline and CPU-utilization metrics.
 */

typedef struct {
    int task_index;
    int job_no;
    int release;
    int execution;
    int deadline;
    int remaining;
    int start;
    int completion;
} InternalJob;

static int validate_rt_tasks(const RealTimeTask *tasks, int count) {
    int i;
    if (tasks == NULL || count <= 0 || count > MAX_RT_TASKS) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        if (tasks[i].name[0] == '\0' || tasks[i].phase < 0 || tasks[i].execution <= 0 || tasks[i].period <= 0) {
            return 0;
        }
        if (tasks[i].deadline <= 0) {
            return 0;
        }
    }
    return 1;
}

static void append_rt_segment(RTScheduleReport *report, const char *label, int start, int end) {
    if (start >= end || report->timeline_count >= MAX_RT_TIMELINE) {
        return;
    }
    if (report->timeline_count > 0) {
        RTSegment *last = &report->timeline[report->timeline_count - 1];
        if (last->end == start && strcmp(last->label, label) == 0) {
            last->end = end;
            return;
        }
    }
    safe_copy(report->timeline[report->timeline_count].label, sizeof(report->timeline[report->timeline_count].label), label);
    report->timeline[report->timeline_count].start = start;
    report->timeline[report->timeline_count].end = end;
    report->timeline_count++;
}

static int generate_jobs(const RealTimeTask *tasks, int count, int simulation_time, InternalJob *jobs) {
    int job_count = 0;
    int i;
    for (i = 0; i < count; i++) {
        int release = tasks[i].phase;
        int job_no = 0;
        int relative_deadline = tasks[i].deadline <= 0 ? tasks[i].period : tasks[i].deadline;
        while (release + relative_deadline <= simulation_time && job_count < MAX_RT_JOBS) {
            /* Only generate jobs whose absolute deadline falls inside the simulation. */
            jobs[job_count].task_index = i;
            jobs[job_count].job_no = job_no;
            jobs[job_count].release = release;
            jobs[job_count].execution = tasks[i].execution;
            jobs[job_count].deadline = release + relative_deadline;
            jobs[job_count].remaining = tasks[i].execution;
            jobs[job_count].start = -1;
            jobs[job_count].completion = -1;
            job_count++;
            job_no++;
            release += tasks[i].period;
        }
    }
    return job_count;
}

static int choose_job(const RealTimeTask *tasks, const InternalJob *jobs, int job_count, int time, int use_edf) {
    int best = -1;
    int i;
    for (i = 0; i < job_count; i++) {
        if (jobs[i].release <= time && jobs[i].remaining > 0) {
            if (best == -1) {
                best = i;
                continue;
            }
            if (use_edf) {
                /* EDF chooses the released job with the earliest absolute deadline. */
                if (jobs[i].deadline < jobs[best].deadline ||
                    (jobs[i].deadline == jobs[best].deadline && jobs[i].release < jobs[best].release) ||
                    (jobs[i].deadline == jobs[best].deadline && jobs[i].release == jobs[best].release && i < best)) {
                    best = i;
                }
            } else {
                /* RMS priority is static: shorter period means higher priority. */
                int period = tasks[jobs[i].task_index].period;
                int best_period = tasks[jobs[best].task_index].period;
                if (period < best_period ||
                    (period == best_period && jobs[i].deadline < jobs[best].deadline) ||
                    (period == best_period && jobs[i].deadline == jobs[best].deadline && i < best)) {
                    best = i;
                }
            }
        }
    }
    return best;
}

static RTScheduleReport schedule_rt(const RealTimeTask *tasks, int count, int simulation_time, int use_edf) {
    RTScheduleReport report;
    InternalJob jobs[MAX_RT_JOBS];
    int job_count;
    int time;
    int i;
    int executed = 0;
    int completed = 0;

    memset(&report, 0, sizeof(report));
    safe_copy(report.algorithm, sizeof(report.algorithm), use_edf ? "EDF (earliest deadline first)" : "RMS (rate monotonic scheduling)");
    report.simulation_time = simulation_time;
    if (!validate_rt_tasks(tasks, count) || simulation_time <= 0) {
        return report;
    }
    job_count = generate_jobs(tasks, count, simulation_time, jobs);
    for (time = 0; time < simulation_time; time++) {
        /* One simulation loop equals one CPU tick. */
        int selected = choose_job(tasks, jobs, job_count, time, use_edf);
        char label[MAX_PID_LEN];
        if (selected < 0) {
            append_rt_segment(&report, "IDLE", time, time + 1);
            continue;
        }
        if (jobs[selected].start < 0) {
            jobs[selected].start = time;
        }
        safe_copy(label, sizeof(label), tasks[jobs[selected].task_index].name);
        append_rt_segment(&report, label, time, time + 1);
        jobs[selected].remaining--;
        executed++;
        if (jobs[selected].remaining == 0) {
            jobs[selected].completion = time + 1;
        }
    }

    report.job_count = job_count;
    for (i = 0; i < job_count; i++) {
        RTJobMetric *metric = &report.jobs[i];
        safe_copy(metric->task, sizeof(metric->task), tasks[jobs[i].task_index].name);
        metric->job_no = jobs[i].job_no;
        metric->release = jobs[i].release;
        metric->execution = jobs[i].execution;
        metric->deadline = jobs[i].deadline;
        metric->start = jobs[i].start;
        metric->completion = jobs[i].completion;
        metric->deadline_miss = jobs[i].completion < 0 || jobs[i].completion > jobs[i].deadline;
        if (jobs[i].completion >= 0) {
            completed++;
        }
        if (metric->deadline_miss) {
            report.deadline_misses++;
        }
    }
    report.completion_rate = job_count > 0 ? (double)completed / job_count : 0.0;
    report.cpu_utilization = simulation_time > 0 ? (double)executed / simulation_time : 0.0;
    return report;
}

int load_rt_tasks_csv(const char *path, RealTimeTask *tasks, int max_count, char *error, size_t error_size) {
    FILE *file = NULL;
    char line[256];
    int count = 0;
    int line_no = 0;

    if (path == NULL || tasks == NULL || max_count <= 0) {
        safe_copy(error, error_size, "invalid real-time task loader arguments");
        return -1;
    }
    file = fopen(path, "r");
    if (file == NULL) {
        safe_copy(error, error_size, "cannot open real-time task CSV file");
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *name = NULL;
        char *phase = NULL;
        char *execution = NULL;
        char *period = NULL;
        char *deadline = NULL;
        char *first = NULL;
        int parsed_phase;
        int parsed_execution;
        int parsed_period;
        int parsed_deadline;
        line_no++;
        first = trim(line);
        if (first[0] == '\0' || first[0] == '#') {
            continue;
        }
        if (strncmp(first, "name", 4) == 0 || strncmp(first, "task", 4) == 0 || strncmp(first, "TASK", 4) == 0) {
            continue;
        }
        if (count >= max_count) {
            safe_copy(error, error_size, "too many real-time tasks in CSV file");
            (void)fclose(file);
            return -1;
        }
        name = trim(strtok(first, ","));
        phase = trim(strtok(NULL, ","));
        execution = trim(strtok(NULL, ","));
        period = trim(strtok(NULL, ","));
        deadline = trim(strtok(NULL, ","));
        if (name == NULL || phase == NULL || execution == NULL || period == NULL) {
            (void)snprintf(error, error_size, "invalid real-time task CSV format at line %d", line_no);
            (void)fclose(file);
            return -1;
        }
        if (!parse_int_value(phase, &parsed_phase) || !parse_int_value(execution, &parsed_execution) ||
            !parse_int_value(period, &parsed_period) ||
            (deadline != NULL && deadline[0] != '\0' && !parse_int_value(deadline, &parsed_deadline))) {
            (void)snprintf(error, error_size, "invalid numeric real-time task CSV field at line %d", line_no);
            (void)fclose(file);
            return -1;
        }
        safe_copy(tasks[count].name, sizeof(tasks[count].name), name);
        tasks[count].phase = parsed_phase;
        tasks[count].execution = parsed_execution;
        tasks[count].period = parsed_period;
        tasks[count].deadline = deadline == NULL || deadline[0] == '\0' ? tasks[count].period : parsed_deadline;
        count++;
    }
    (void)fclose(file);
    if (!validate_rt_tasks(tasks, count)) {
        safe_copy(error, error_size, "real-time task data failed validation");
        return -1;
    }
    return count;
}

RTScheduleReport schedule_edf(const RealTimeTask *tasks, int count, int simulation_time) {
    return schedule_rt(tasks, count, simulation_time, 1);
}

RTScheduleReport schedule_rms(const RealTimeTask *tasks, int count, int simulation_time) {
    return schedule_rt(tasks, count, simulation_time, 0);
}

void print_rt_report(const RTScheduleReport *report) {
    int i;
    if (report == NULL || report->job_count <= 0) {
        printf("实时调度报告为空。\n");
        return;
    }
    printf("算法: %s, 仿真时长=%d, 作业数=%d, 完成率=%.1f%%, 期限违约=%d, CPU利用率=%.1f%%\n",
           report->algorithm, report->simulation_time, report->job_count, report->completion_rate * 100.0,
           report->deadline_misses, report->cpu_utilization * 100.0);
    printf("时间线: ");
    for (i = 0; i < report->timeline_count; i++) {
        printf("[%d,%d)%s%s", report->timeline[i].start, report->timeline[i].end, report->timeline[i].label,
               i == report->timeline_count - 1 ? "" : " -> ");
    }
    printf("\n");
    printf("%-8s %-5s %-7s %-7s %-8s %-7s %-10s %-6s\n", "Task", "Job", "Release", "Exec", "Deadline", "Start", "Complete", "Miss");
    for (i = 0; i < report->job_count; i++) {
        const RTJobMetric *job = &report->jobs[i];
        printf("%-8s %-5d %-7d %-7d %-8d %-7d %-10d %-6s\n", job->task, job->job_no, job->release,
               job->execution, job->deadline, job->start, job->completion, job->deadline_miss ? "yes" : "no");
    }
}

void print_rt_comparison(const RealTimeTask *tasks, int count, int simulation_time) {
    RTScheduleReport edf = schedule_edf(tasks, count, simulation_time);
    RTScheduleReport rms = schedule_rms(tasks, count, simulation_time);
    printf("%-36s %-10s %-12s %-12s %-10s\n", "Algorithm", "Jobs", "Complete", "Misses", "CPU");
    printf("%-36s %-10d %-11.1f%% %-12d %-9.1f%%\n", edf.algorithm, edf.job_count, edf.completion_rate * 100.0, edf.deadline_misses, edf.cpu_utilization * 100.0);
    printf("%-36s %-10d %-11.1f%% %-12d %-9.1f%%\n", rms.algorithm, rms.job_count, rms.completion_rate * 100.0, rms.deadline_misses, rms.cpu_utilization * 100.0);
    printf("\n--- EDF 详情 ---\n");
    print_rt_report(&edf);
    printf("\n--- RMS 详情 ---\n");
    print_rt_report(&rms);
}
