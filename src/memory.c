#include "os_project.h"
#include <stdlib.h>
#include <string.h>

/*
 * Memory management module.
 * Covers FIFO/LRU page replacement and first-fit dynamic partition
 * allocation with operation snapshots for demo and report output.
 */

static void page_report_init(PageReport *report, const char *algorithm, const int *references, int reference_count, int frame_count) {
    int i;
    memset(report, 0, sizeof(*report));
    safe_copy(report->algorithm, sizeof(report->algorithm), algorithm);
    report->frame_count = frame_count;
    report->reference_count = reference_count;
    for (i = 0; i < reference_count && i < MAX_REFERENCES; i++) {
        report->references[i] = references[i];
    }
}

static void page_record(PageReport *report, int step, int page, const int *frames, int fault, int evicted) {
    int i;
    PageEvent *event = NULL;
    if (report->event_count >= MAX_REFERENCES) {
        return;
    }
    event = &report->events[report->event_count++];
    event->step = step;
    event->page = page;
    event->frame_count = report->frame_count;
    event->fault = fault;
    event->evicted = evicted;
    for (i = 0; i < report->frame_count; i++) {
        event->frames[i] = frames[i];
    }
    if (fault) {
        report->faults++;
    } else {
        report->hits++;
    }
    report->fault_rate = report->event_count > 0 ? (double)report->faults / report->event_count : 0.0;
}

static int find_frame(const int *frames, int frame_count, int page) {
    int i;
    for (i = 0; i < frame_count; i++) {
        if (frames[i] == page) {
            return i;
        }
    }
    return -1;
}

PageReport page_fifo(const int *references, int reference_count, int frame_count) {
    PageReport report;
    int frames[MAX_FRAMES];
    int queue[MAX_FRAMES];
    int head = 0;
    int tail = 0;
    int i;

    page_report_init(&report, "FIFO", references, reference_count, frame_count);
    if (references == NULL || reference_count <= 0 || reference_count > MAX_REFERENCES || frame_count <= 0 || frame_count > MAX_FRAMES) {
        return report;
    }
    for (i = 0; i < frame_count; i++) {
        frames[i] = -1;
        queue[i] = -1;
    }
    for (i = 0; i < reference_count; i++) {
        /* FIFO evicts the page that has stayed in memory the longest. */
        int page = references[i];
        int evicted = -1;
        int slot = find_frame(frames, frame_count, page);
        if (slot >= 0) {
            page_record(&report, i + 1, page, frames, 0, -1);
            continue;
        }
        slot = find_frame(frames, frame_count, -1);
        if (slot < 0) {
            evicted = queue[head % frame_count];
            head++;
            slot = find_frame(frames, frame_count, evicted);
        }
        frames[slot] = page;
        queue[tail % frame_count] = page;
        tail++;
        page_record(&report, i + 1, page, frames, 1, evicted);
    }
    return report;
}

PageReport page_lru(const int *references, int reference_count, int frame_count) {
    PageReport report;
    int frames[MAX_FRAMES];
    int last_used[MAX_FRAMES];
    int i;

    page_report_init(&report, "LRU", references, reference_count, frame_count);
    if (references == NULL || reference_count <= 0 || reference_count > MAX_REFERENCES || frame_count <= 0 || frame_count > MAX_FRAMES) {
        return report;
    }
    for (i = 0; i < frame_count; i++) {
        frames[i] = -1;
        last_used[i] = -1;
    }
    for (i = 0; i < reference_count; i++) {
        /* LRU evicts the frame with the oldest last-used timestamp. */
        int page = references[i];
        int evicted = -1;
        int slot = find_frame(frames, frame_count, page);
        if (slot >= 0) {
            last_used[slot] = i + 1;
            page_record(&report, i + 1, page, frames, 0, -1);
            continue;
        }
        slot = find_frame(frames, frame_count, -1);
        if (slot < 0) {
            int j;
            slot = 0;
            for (j = 1; j < frame_count; j++) {
                if (last_used[j] < last_used[slot]) {
                    slot = j;
                }
            }
            evicted = frames[slot];
        }
        frames[slot] = page;
        last_used[slot] = i + 1;
        page_record(&report, i + 1, page, frames, 1, evicted);
    }
    return report;
}

void print_page_report(const PageReport *report) {
    int i;
    int j;
    if (report == NULL || report->event_count <= 0) {
        printf("页面置换报告为空。\n");
        return;
    }
    printf("算法: %s, 物理块数=%d, 缺页次数=%d, 命中次数=%d, 缺页率=%.1f%%\n",
           report->algorithm, report->frame_count, report->faults, report->hits, report->fault_rate * 100.0);
    printf("%-6s %-6s %-24s %-8s %-8s\n", "Step", "Page", "Frames", "Result", "Evict");
    for (i = 0; i < report->event_count; i++) {
        const PageEvent *event = &report->events[i];
        printf("%-6d %-6d ", event->step, event->page);
        for (j = 0; j < event->frame_count; j++) {
            if (event->frames[j] < 0) {
                printf("- ");
            } else {
                printf("%d ", event->frames[j]);
            }
        }
        for (j = event->frame_count; j < 10; j++) {
            printf("  ");
        }
        printf("%-8s ", event->fault ? "fault" : "hit");
        if (event->evicted >= 0) {
            printf("%-8d", event->evicted);
        }
        printf("\n");
    }
}

static void record_partition_event(FirstFitMemory *memory, const char *operation, const char *pid, int size, int success, const char *message) {
    PartitionEvent *event = NULL;
    int i;
    if (memory->history_count >= MAX_PARTITION_EVENTS) {
        return;
    }
    /* Store a full partition-table snapshot so demos can replay allocation history. */
    event = &memory->history[memory->history_count++];
    safe_copy(event->operation, sizeof(event->operation), operation);
    safe_copy(event->pid, sizeof(event->pid), pid);
    event->size = size;
    event->success = success;
    safe_copy(event->message, sizeof(event->message), message);
    event->block_count = memory->block_count;
    for (i = 0; i < memory->block_count; i++) {
        event->blocks[i] = memory->blocks[i];
    }
}

static PartitionEvent last_partition_event(const FirstFitMemory *memory) {
    if (memory->history_count <= 0) {
        PartitionEvent empty;
        memset(&empty, 0, sizeof(empty));
        return empty;
    }
    return memory->history[memory->history_count - 1];
}

void ff_init(FirstFitMemory *memory, int total_size) {
    memset(memory, 0, sizeof(*memory));
    memory->total_size = total_size > 0 ? total_size : 1;
    memory->block_count = 1;
    memory->blocks[0].start = 0;
    memory->blocks[0].size = memory->total_size;
    memory->blocks[0].pid[0] = '\0';
    memory->blocks[0].free = 1;
}

PartitionEvent ff_allocate(FirstFitMemory *memory, const char *pid, int size) {
    int i;
    char message[MAX_MESSAGE_LEN];

    if (memory == NULL) {
        PartitionEvent empty;
        memset(&empty, 0, sizeof(empty));
        return empty;
    }
    if (pid == NULL || pid[0] == '\0' || size <= 0) {
        record_partition_event(memory, "alloc", pid == NULL ? "" : pid, size, 0, "invalid allocation request");
        return last_partition_event(memory);
    }
    for (i = 0; i < memory->block_count; i++) {
        if (!memory->blocks[i].free && strcmp(memory->blocks[i].pid, pid) == 0) {
            (void)snprintf(message, sizeof(message), "process %s already owns a partition", pid);
            record_partition_event(memory, "alloc", pid, size, 0, message);
            return last_partition_event(memory);
        }
    }
    for (i = 0; i < memory->block_count; i++) {
        MemoryBlock *block = &memory->blocks[i];
        if (block->free && block->size >= size) {
            int old_size = block->size;
            if (old_size > size && memory->block_count >= MAX_MEMORY_BLOCKS) {
                record_partition_event(memory, "alloc", pid, size, 0, "too many memory blocks to split");
                return last_partition_event(memory);
            }
            block->size = size;
            block->free = 0;
            safe_copy(block->pid, sizeof(block->pid), pid);
            if (old_size > size) {
                /* First-fit splits only the first free block large enough for the request. */
                int j;
                for (j = memory->block_count; j > i + 1; j--) {
                    memory->blocks[j] = memory->blocks[j - 1];
                }
                memory->blocks[i + 1].start = block->start + size;
                memory->blocks[i + 1].size = old_size - size;
                memory->blocks[i + 1].pid[0] = '\0';
                memory->blocks[i + 1].free = 1;
                memory->block_count++;
            }
            (void)snprintf(message, sizeof(message), "allocated %d units to %s", size, pid);
            record_partition_event(memory, "alloc", pid, size, 1, message);
            return last_partition_event(memory);
        }
    }
    (void)snprintf(message, sizeof(message), "no free block can satisfy %d units", size);
    record_partition_event(memory, "alloc", pid, size, 0, message);
    return last_partition_event(memory);
}

PartitionEvent ff_free(FirstFitMemory *memory, const char *pid) {
    int i;
    char message[MAX_MESSAGE_LEN];
    if (memory == NULL) {
        PartitionEvent empty;
        memset(&empty, 0, sizeof(empty));
        return empty;
    }
    if (pid == NULL) {
        record_partition_event(memory, "free", "", 0, 0, "invalid free request");
        return last_partition_event(memory);
    }
    for (i = 0; i < memory->block_count; i++) {
        if (!memory->blocks[i].free && strcmp(memory->blocks[i].pid, pid) == 0) {
            int released_size = memory->blocks[i].size;
            memory->blocks[i].free = 1;
            memory->blocks[i].pid[0] = '\0';
            while (i + 1 < memory->block_count && memory->blocks[i + 1].free) {
                /* Merge adjacent free blocks to reduce external fragmentation. */
                int j;
                memory->blocks[i].size += memory->blocks[i + 1].size;
                for (j = i + 1; j < memory->block_count - 1; j++) {
                    memory->blocks[j] = memory->blocks[j + 1];
                }
                memory->block_count--;
            }
            if (i > 0 && memory->blocks[i - 1].free) {
                int j;
                memory->blocks[i - 1].size += memory->blocks[i].size;
                for (j = i; j < memory->block_count - 1; j++) {
                    memory->blocks[j] = memory->blocks[j + 1];
                }
                memory->block_count--;
            }
            (void)snprintf(message, sizeof(message), "released %d units from %s", released_size, pid);
            record_partition_event(memory, "free", pid, released_size, 1, message);
            return last_partition_event(memory);
        }
    }
    (void)snprintf(message, sizeof(message), "process %s does not own a partition", pid);
    record_partition_event(memory, "free", pid, 0, 0, message);
    return last_partition_event(memory);
}

int ff_used_size(const FirstFitMemory *memory) {
    int i;
    int used = 0;
    for (i = 0; i < memory->block_count; i++) {
        if (!memory->blocks[i].free) {
            used += memory->blocks[i].size;
        }
    }
    return used;
}

int ff_free_size(const FirstFitMemory *memory) {
    return memory->total_size - ff_used_size(memory);
}

int ff_external_fragmentation(const FirstFitMemory *memory) {
    int i;
    int total_free = 0;
    int largest_free = 0;
    for (i = 0; i < memory->block_count; i++) {
        if (memory->blocks[i].free) {
            total_free += memory->blocks[i].size;
            if (memory->blocks[i].size > largest_free) {
                largest_free = memory->blocks[i].size;
            }
        }
    }
    return total_free - largest_free;
}

void print_partition_event(const PartitionEvent *event, int index) {
    int i;
    if (event == NULL) {
        return;
    }
    printf("#%-3d %-6s %-8s size=%-4d %-5s %s\n", index, event->operation, event->pid,
           event->size, event->success ? "OK" : "FAIL", event->message);
    printf("     blocks: ");
    for (i = 0; i < event->block_count; i++) {
        const MemoryBlock *block = &event->blocks[i];
        printf("[%d,%d)=%s%s", block->start, block->start + block->size,
               block->free ? "FREE" : block->pid, i == event->block_count - 1 ? "" : " ");
    }
    printf("\n");
}

void print_partition_summary(const FirstFitMemory *memory) {
    int i;
    for (i = 0; i < memory->history_count; i++) {
        print_partition_event(&memory->history[i], i + 1);
    }
    printf("Total=%d, Used=%d, Free=%d, External fragmentation=%d\n",
           memory->total_size, ff_used_size(memory), ff_free_size(memory), ff_external_fragmentation(memory));
}

int run_partition_script(const char *path, FirstFitMemory *memory) {
    FILE *file = NULL;
    char line[256];
    int line_no = 0;

    if (path == NULL || memory == NULL) {
        return 0;
    }
    file = fopen(path, "r");
    if (file == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *op = NULL;
        char *pid = NULL;
        char *size_text = NULL;
        char *first = NULL;
        line_no++;
        first = trim(line);
        if (first[0] == '\0' || first[0] == '#') {
            continue;
        }
        if (strncmp(first, "op", 2) == 0 || strncmp(first, "OP", 2) == 0) {
            continue;
        }
        op = trim(strtok(first, ","));
        pid = trim(strtok(NULL, ","));
        size_text = trim(strtok(NULL, ","));
        if (op == NULL || pid == NULL) {
            (void)fprintf(stderr, "invalid partition script line %d\n", line_no);
            (void)fclose(file);
            return 0;
        }
        if (strcmp(op, "alloc") == 0 || strcmp(op, "allocate") == 0) {
            int size;
            if (size_text == NULL) {
                (void)fprintf(stderr, "missing allocation size at line %d\n", line_no);
                (void)fclose(file);
                return 0;
            }
            if (!parse_int_value(size_text, &size) || size <= 0) {
                (void)fprintf(stderr, "invalid allocation size at line %d\n", line_no);
                (void)fclose(file);
                return 0;
            }
            (void)ff_allocate(memory, pid, size);
        } else if (strcmp(op, "free") == 0 || strcmp(op, "release") == 0) {
            (void)ff_free(memory, pid);
        } else {
            (void)fprintf(stderr, "unknown partition operation at line %d: %s\n", line_no, op);
            (void)fclose(file);
            return 0;
        }
    }
    (void)fclose(file);
    return 1;
}
