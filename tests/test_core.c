#include "os_project.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const Process SAMPLE_PROCESSES[] = {
    {"P1", 0, 7, 2},
    {"P2", 2, 4, 1},
    {"P3", 4, 1, 3},
    {"P4", 5, 4, 2},
};

static const RealTimeTask SAMPLE_RT_TASKS[] = {
    {"T1", 0, 1, 4, 4},
    {"T2", 0, 2, 5, 5},
    {"T3", 0, 1, 10, 10},
};

typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t header_size;
    uint32_t payload_size;
    uint32_t checksum;
} TestTinyFSImageHeader;

static uint32_t test_fs_image_checksum(const unsigned char *data, size_t size) {
    size_t i;
    uint32_t hash = 2166136261U;
    for (i = 0; i < size; i++) {
        hash ^= (uint32_t)data[i];
        hash *= 16777619U;
    }
    return hash;
}

static int write_test_image(const char *path, const TinyFS *fs) {
    static const char magic[8] = {'T', 'F', 'S', 'I', 'M', 'G', '2', '\0'};
    TestTinyFSImageHeader header;
    FILE *file = NULL;
    int ok;

    memset(&header, 0, sizeof(header));
    memcpy(header.magic, magic, sizeof(magic));
    header.version = 2U;
    header.header_size = (uint32_t)sizeof(header);
    header.payload_size = (uint32_t)sizeof(*fs);
    header.checksum = test_fs_image_checksum((const unsigned char *)fs, sizeof(*fs));
    file = fopen(path, "wb");
    if (file == NULL) {
        return 0;
    }
    ok = fwrite(&header, sizeof(header), 1, file) == 1 && fwrite(fs, sizeof(*fs), 1, file) == 1;
    if (fclose(file) != 0) {
        ok = 0;
    }
    return ok;
}

static void write_bad_magic_image(const char *path) {
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fputs("BAD", file) >= 0);
    assert(fclose(file) == 0);
}

static void copy_image_prefix(const char *src, const char *dst, long bytes) {
    FILE *in = fopen(src, "rb");
    FILE *out = fopen(dst, "wb");
    int ch;
    long pos = 0;
    assert(in != NULL);
    assert(out != NULL);
    while (pos < bytes && (ch = fgetc(in)) != EOF) {
        assert(fputc(ch, out) != EOF);
        pos++;
    }
    assert(ferror(in) == 0);
    assert(fclose(in) == 0);
    assert(fclose(out) == 0);
}

static void copy_image_with_flip(const char *src, const char *dst, long offset) {
    FILE *in = fopen(src, "rb");
    FILE *out = fopen(dst, "wb");
    int ch;
    long pos = 0;
    assert(in != NULL);
    assert(out != NULL);
    while ((ch = fgetc(in)) != EOF) {
        if (pos == offset) {
            ch ^= 0x5a;
        }
        assert(fputc(ch, out) != EOF);
        pos++;
    }
    assert(pos > offset);
    assert(ferror(in) == 0);
    assert(fclose(in) == 0);
    assert(fclose(out) == 0);
}

static void append_byte_to_file(const char *path) {
    FILE *file = fopen(path, "ab");
    assert(file != NULL);
    assert(fputc('X', file) != EOF);
    assert(fclose(file) == 0);
}

static void assert_bad_image_rejected(const char *path) {
    TinyFS loaded;
    char message[MAX_MESSAGE_LEN];
    fs_init(&loaded, 1, 1);
    assert(fs_load_image(&loaded, path, message, sizeof(message)) == 0);
    assert(fs_check_image(path, message, sizeof(message)) == 0);
}

static int kernel_log_contains(const MiniKernel *kernel, const char *needle) {
    int i;
    for (i = 0; i < kernel->log_count; i++) {
        if (strstr(kernel->logs[i], needle) != NULL) {
            return 1;
        }
    }
    return 0;
}

static void test_scheduling(void) {
    ScheduleReport fcfs = schedule_fcfs(SAMPLE_PROCESSES, 4);
    ScheduleReport sjf = schedule_sjf(SAMPLE_PROCESSES, 4);
    ScheduleReport prio = schedule_priority(SAMPLE_PROCESSES, 4, 1);
    ScheduleReport hrrn = schedule_hrrn(SAMPLE_PROCESSES, 4);
    ScheduleReport rr = schedule_round_robin(SAMPLE_PROCESSES, 4, 2);

    assert(fcfs.process_count == 4);
    assert(strcmp(fcfs.timeline[0].pid, "P1") == 0);
    assert(strcmp(fcfs.timeline[1].pid, "P2") == 0);
    assert((int)(fcfs.average_waiting * 100 + 0.5) == 475);
    assert((int)(fcfs.average_weighted_turnaround * 100 + 0.5) == 350);

    assert(strcmp(sjf.timeline[1].pid, "P3") == 0);
    assert((int)(sjf.average_waiting * 100 + 0.5) == 400);

    assert(strcmp(prio.timeline[1].pid, "P2") == 0);
    assert(strcmp(hrrn.timeline[1].pid, "P3") == 0);

    assert(rr.metrics[0].completion == 16);
    assert(rr.metrics[1].completion == 9);
    assert(rr.metrics[2].completion == 7);
    assert(rr.metrics[3].completion == 15);
    assert((int)(rr.average_response * 100 + 0.5) == 150);
}

static void test_realtime(void) {
    RTScheduleReport edf = schedule_edf(SAMPLE_RT_TASKS, 3, 20);
    RTScheduleReport rms = schedule_rms(SAMPLE_RT_TASKS, 3, 20);
    assert(edf.job_count == 11);
    assert(rms.job_count == 11);
    assert(edf.deadline_misses == 0);
    assert(rms.deadline_misses == 0);
}

static void test_memory(void) {
    const int refs[] = {7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2};
    PageReport fifo = page_fifo(refs, 13, 3);
    PageReport lru = page_lru(refs, 13, 3);
    FirstFitMemory memory;

    assert(fifo.faults == 10);
    assert(lru.faults == 9);

    ff_init(&memory, 64);
    assert(ff_allocate(&memory, "A", 20).success == 1);
    assert(ff_allocate(&memory, "B", 15).success == 1);
    assert(ff_allocate(&memory, "C", 10).success == 1);
    assert(ff_free(&memory, "B").success == 1);
    assert(ff_allocate(&memory, "D", 12).success == 1);
    assert(ff_external_fragmentation(&memory) == 3);
}

static void test_filesystem(void) {
    TinyFS fs;
    TinyFS bad;
    char message[MAX_MESSAGE_LEN];
    char output[MAX_CONTENT_LEN];
    fs_init(&fs, 4, 4);
    assert(fs_create(&fs, "a.txt", "abc", message, sizeof(message)) == 1);
    assert(fs_write(&fs, "a.txt", "def", 1, message, sizeof(message)) == 1);
    assert(fs_read(&fs, "a.txt", output, sizeof(output), message, sizeof(message)) == 1);
    assert(strcmp(output, "abcdef") == 0);
    assert(fs_delete(&fs, "a.txt", message, sizeof(message)) == 1);
    assert(fs_mkdir(&fs, "/docs", message, sizeof(message)) == 1);
    assert(fs_create(&fs, "/docs/note.txt", "note", message, sizeof(message)) == 1);
    assert(fs_read(&fs, "/docs/note.txt", output, sizeof(output), message, sizeof(message)) == 1);
    assert(strcmp(output, "note") == 0);
    assert(fs_rmdir(&fs, "/docs", message, sizeof(message)) == 0);
    assert(fs_delete(&fs, "/docs/note.txt", message, sizeof(message)) == 1);
    assert(fs_rmdir(&fs, "/docs", message, sizeof(message)) == 1);
    assert(fs_create(&fs, "b.txt", "abcd", message, sizeof(message)) == 1);
    assert(fs_write(&fs, "b.txt", "abcde", 0, message, sizeof(message)) == 1);
    assert(fs_save_image(&fs, "build/test_fs_image.bin", message, sizeof(message)) == 1);
    assert(fs_check_image("build/test_fs_image.bin", message, sizeof(message)) == 1);
    fs_init(&fs, 2, 2);
    assert(fs_load_image(&fs, "build/test_fs_image.bin", message, sizeof(message)) == 1);
    assert(fs_read(&fs, "b.txt", output, sizeof(output), message, sizeof(message)) == 1);
    assert(strcmp(output, "abcde") == 0);

    bad = fs;
    bad.used[3] = 1;
    assert(fs_save_image(&bad, "build/test_fs_bad_save.bin", message, sizeof(message)) == 0);

    write_bad_magic_image("build/test_fs_bad_magic.bin");
    assert_bad_image_rejected("build/test_fs_bad_magic.bin");
    copy_image_prefix("build/test_fs_image.bin", "build/test_fs_truncated.bin", 12);
    assert_bad_image_rejected("build/test_fs_truncated.bin");
    copy_image_with_flip("build/test_fs_image.bin", "build/test_fs_checksum.bin", 40);
    assert_bad_image_rejected("build/test_fs_checksum.bin");
    copy_image_prefix("build/test_fs_image.bin", "build/test_fs_trailing.bin", 200000);
    append_byte_to_file("build/test_fs_trailing.bin");
    assert_bad_image_rejected("build/test_fs_trailing.bin");

    bad = fs;
    bad.total_blocks = MAX_FS_BLOCKS + 1;
    assert(write_test_image("build/test_fs_bad_metadata.bin", &bad) == 1);
    assert_bad_image_rejected("build/test_fs_bad_metadata.bin");
    bad = fs;
    bad.directory_count = 2;
    safe_copy(bad.directories[1], sizeof(bad.directories[1]), "/b.txt");
    assert(write_test_image("build/test_fs_file_dir_conflict.bin", &bad) == 1);
    assert_bad_image_rejected("build/test_fs_file_dir_conflict.bin");
    bad = fs;
    safe_copy(bad.files[0].name, sizeof(bad.files[0].name), "/b.txt/");
    assert(write_test_image("build/test_fs_noncanonical.bin", &bad) == 1);
    assert_bad_image_rejected("build/test_fs_noncanonical.bin");
    bad = fs;
    bad.files[0].blocks[0] = bad.total_blocks;
    assert(write_test_image("build/test_fs_bad_block.bin", &bad) == 1);
    assert_bad_image_rejected("build/test_fs_bad_block.bin");

    (void)remove("build/test_fs_image.bin");
    (void)remove("build/test_fs_bad_save.bin");
    (void)remove("build/test_fs_bad_magic.bin");
    (void)remove("build/test_fs_truncated.bin");
    (void)remove("build/test_fs_checksum.bin");
    (void)remove("build/test_fs_trailing.bin");
    (void)remove("build/test_fs_bad_metadata.bin");
    (void)remove("build/test_fs_file_dir_conflict.bin");
    (void)remove("build/test_fs_noncanonical.bin");
    (void)remove("build/test_fs_bad_block.bin");
}

static void test_kernel(void) {
    MiniKernel kernel;
    int waiting_pid;
    int waiting_index;
    int vm_pid;
    int vm_index;
    int p1;
    int p2;
    int fd;
    int fd2;
    int p1_fd;
    int p2_fd;
    int opened[MAX_KERNEL_FDS];
    int i;
    char message[MAX_MESSAGE_LEN];
    char fd_output[MAX_CONTENT_LEN];
    kernel_init(&kernel);
    assert(kernel.scheduler_policy == KERNEL_SCHED_RR);
    assert(kernel_set_scheduler(&kernel, "priority", 0) == 1);
    assert(kernel.scheduler_policy == KERNEL_SCHED_PRIORITY);
    p1 = kernel_create_process(&kernel, "init", 2, 8);
    assert(p1 > 0);
    assert(syscall_dispatch(&kernel, "mkdir", "/k", NULL) == 1);
    assert(syscall_dispatch(&kernel, "create_file", "/k/log.txt", "boot") == 1);
    fd = kernel_open_file(&kernel, "/k/log.txt", "rw");
    assert(fd >= 3);
    assert(kernel_read_fd(&kernel, fd, fd_output, sizeof(fd_output)) == 4);
    assert(strcmp(fd_output, "boot") == 0);
    assert(kernel_write_fd(&kernel, fd, "-fd") == 3);
    assert(kernel_seek_fd(&kernel, fd, 0) == 1);
    assert(kernel_read_fd_count(&kernel, fd, 4, fd_output, sizeof(fd_output)) == 4);
    assert(strcmp(fd_output, "boot") == 0);
    assert(kernel_write_fd(&kernel, fd, "OS!") == 3);
    assert(kernel_seek_fd(&kernel, fd, 0) == 1);
    assert(kernel_read_fd(&kernel, fd, fd_output, sizeof(fd_output)) == 7);
    assert(strcmp(fd_output, "bootOS!") == 0);
    assert(kernel_read_fd(&kernel, fd, fd_output, sizeof(fd_output)) == 0);
    assert(strcmp(fd_output, "") == 0);
    assert(kernel_close_fd(&kernel, fd) == 1);
    assert(kernel_open_file(&kernel, "/k/log.txt", "raw") < 0);
    fd2 = kernel_open_file(&kernel, "/k/new.txt", "w");
    assert(fd2 >= 3);
    assert(kernel_write_fd(&kernel, fd2, "new") == 3);
    assert(kernel_close_fd(&kernel, fd2) == 1);
    fd2 = syscall_dispatch(&kernel, "open", "/k/new.txt", "r");
    assert(fd2 >= 3);
    assert(kernel_close_fd(&kernel, fd2) == 1);
    assert(syscall_dispatch(&kernel, "open", "/k/missing.txt", "r") < 0);

    assert(syscall_dispatch(&kernel, "create_file", "/k/unlink.txt", "live") == 1);
    fd2 = kernel_open_file(&kernel, "/k/unlink.txt", "r");
    assert(fd2 >= 3);
    assert(kernel_delete_file(&kernel, "/k/unlink.txt") == 1);
    assert(fs_read(&kernel.fs, "/k/unlink.txt", fd_output, sizeof(fd_output), message, sizeof(message)) == 0);
    assert(kernel_read_fd(&kernel, fd2, fd_output, sizeof(fd_output)) == 4);
    assert(strcmp(fd_output, "live") == 0);
    assert(kernel_close_fd(&kernel, fd2) == 1);

    p2 = kernel_create_process(&kernel, "worker", 1, 8);
    assert(p2 > 0);
    assert(syscall_dispatch(&kernel, "create_file", "/k/p1.txt", "one") == 1);
    assert(syscall_dispatch(&kernel, "create_file", "/k/p2.txt", "two") == 1);
    p1_fd = kernel_open_file_for_process(&kernel, p1, "/k/p1.txt", "r");
    p2_fd = kernel_open_file_for_process(&kernel, p2, "/k/p2.txt", "r");
    assert(p1_fd == 3);
    assert(p2_fd == 3);
    assert(kernel_read_fd_count_for_process(&kernel, p1, p1_fd, -1, fd_output, sizeof(fd_output)) == 3);
    assert(strcmp(fd_output, "one") == 0);
    assert(kernel_read_fd_count_for_process(&kernel, p2, p2_fd, -1, fd_output, sizeof(fd_output)) == 3);
    assert(strcmp(fd_output, "two") == 0);
    assert(kernel_close_fd_for_process(&kernel, p1, p1_fd) == 1);
    assert(kernel_close_fd_for_process(&kernel, p2, p2_fd) == 1);

    for (i = 0; i < MAX_KERNEL_FDS; i++) {
        opened[i] = kernel_open_file(&kernel, "/k/log.txt", "r");
        assert(opened[i] >= 3);
    }
    assert(kernel_open_file(&kernel, "/k/side-effect.txt", "w") < 0);
    assert(fs_read(&kernel.fs, "/k/side-effect.txt", fd_output, sizeof(fd_output), message, sizeof(message)) == 0);
    assert(kernel_open_file(&kernel, "/k/log.txt", "w") < 0);
    assert(fs_read(&kernel.fs, "/k/log.txt", fd_output, sizeof(fd_output), message, sizeof(message)) == 1);
    assert(strcmp(fd_output, "bootOS!") == 0);
    for (i = 0; i < MAX_KERNEL_FDS; i++) {
        assert(kernel_close_fd(&kernel, opened[i]) == 1);
    }
    kernel_schedule_tick(&kernel);
    kernel_schedule_tick(&kernel);
    assert(kernel.tick == 2);
    waiting_pid = kernel_create_process(&kernel, "big", 2, 200);
    assert(waiting_pid > 0);
    waiting_index = -1;
    for (i = 0; i < kernel.process_count; i++) {
        if (kernel.processes[i].pid == waiting_pid) {
            waiting_index = i;
        }
    }
    assert(waiting_index >= 0);
    assert(strcmp(kernel.processes[waiting_index].state, "MEM_WAIT") == 0);
    assert(kernel_kill_process(&kernel, waiting_pid) == 1);
    assert(strcmp(kernel.processes[waiting_index].state, "TERMINATED") == 0);
    vm_pid = kernel_create_process_with_priority(&kernel, "vm", 2, 8, 1);
    assert(vm_pid > 0);
    vm_index = -1;
    for (i = 0; i < kernel.process_count; i++) {
        if (kernel.processes[i].pid == vm_pid) {
            vm_index = i;
        }
    }
    assert(vm_index >= 0);
    assert(kernel_record_page_access(&kernel, vm_pid, 7) == 1);
    assert(kernel_record_page_access(&kernel, vm_pid, 3) == 1);
    assert(kernel_record_page_access(&kernel, vm_pid, 7) == 1);
    assert(kernel.access_count == 3);
    assert(kernel.vm_faults == 2);
    assert(kernel.vm_hits == 1);
    assert(kernel_set_vm_policy(&kernel, "fifo") == 1);
    assert(kernel.vm_faults == 0);
    assert(kernel_record_page_access(&kernel, vm_pid, 7) == 1);
    assert(kernel.vm_faults == 1);
    assert(kernel_sleep_process(&kernel, vm_pid, 2) == 1);
    assert(strcmp(kernel.processes[vm_index].state, "BLOCKED") == 0);
    kernel_schedule_tick(&kernel);
    assert(strcmp(kernel.processes[vm_index].state, "BLOCKED") == 0);
    kernel_schedule_tick(&kernel);
    assert(strcmp(kernel.processes[vm_index].state, "READY") == 0 || strcmp(kernel.processes[vm_index].state, "RUNNING") == 0);
    assert(kernel.context_switches > 0);
    assert(kernel.processes[vm_index].cpu_time + kernel.processes[vm_index].wait_time >= 0);
    assert(kernel.log_count > 0);
}

static void test_mlfq_scheduler(void) {
    MiniKernel kernel;
    int i;
    kernel_init(&kernel);
    assert(kernel_set_scheduler(&kernel, "mlfq", 1) == 1);
    assert(kernel.scheduler_policy == KERNEL_SCHED_MLFQ);
    for (i = 0; i < 6; i++) {
        char name[MAX_PID_LEN];
        (void)snprintf(name, sizeof(name), "cpu%d", i + 1);
        assert(kernel_create_process_with_priority(&kernel, name, 20, 1, i % 3 + 1) > 0);
    }
    for (i = 0; i < 50; i++) {
        kernel_schedule_tick(&kernel);
    }
    assert(kernel.context_switches > 0);
    assert(kernel_log_contains(&kernel, "sched mlfq demote"));
    assert(kernel_log_contains(&kernel, "sched mlfq aging promote"));
}

static void test_benchmark_exports(void) {
    char message[MAX_MESSAGE_LEN];
    assert(write_benchmark_csv("build/test_bench.csv", 8, 1, message, sizeof(message)) == 1);
    assert(write_performance_report("build/test_performance.md", 8, 1, message, sizeof(message)) == 1);
    (void)remove("build/test_bench.csv");
    (void)remove("build/test_performance.md");
}

int main(void) {
    test_scheduling();
    test_realtime();
    test_memory();
    test_filesystem();
    test_kernel();
    test_mlfq_scheduler();
    test_benchmark_exports();
    printf("core tests passed\n");
    return 0;
}
