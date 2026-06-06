#include "os_project.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * TinyFS module.
 * Simulates directories, file metadata, block allocation, and a bitmap-
 * managed in-memory disk image for MiniOS file-system demos.
 */

typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t header_size;
    uint32_t payload_size;
    uint32_t checksum;
} TinyFSImageHeader;

static const char TINYFS_IMAGE_MAGIC[8] = {'T', 'F', 'S', 'I', 'M', 'G', '2', '\0'};
static const uint32_t TINYFS_IMAGE_VERSION = 2U;

static uint32_t fs_image_checksum(const unsigned char *data, size_t size) {
    size_t i;
    uint32_t hash = 2166136261U;
    for (i = 0; i < size; i++) {
        hash ^= (uint32_t)data[i];
        hash *= 16777619U;
    }
    return hash;
}

static int normalize_path(const char *raw, char *out, size_t out_size) {
    size_t len;
    if (raw == NULL || out == NULL || out_size == 0) {
        return 0;
    }
    while (*raw == ' ' || *raw == '\t') {
        raw++;
    }
    if (*raw == '\0') {
        safe_copy(out, out_size, "/");
        return 1;
    }
    if (*raw == '/') {
        safe_copy(out, out_size, raw);
    } else {
        if (snprintf(out, out_size, "/%s", raw) >= (int)out_size) {
            return 0;
        }
    }
    len = strlen(out);
    while (len > 1 && (out[len - 1] == '\n' || out[len - 1] == '\r' || out[len - 1] == ' ' || out[len - 1] == '\t' || out[len - 1] == '/')) {
        out[len - 1] = '\0';
        len--;
    }
    return len > 0 && len < out_size;
}

static void parent_path(const char *path, char *out, size_t out_size) {
    char *slash = NULL;
    safe_copy(out, out_size, path);
    slash = strrchr(out, '/');
    if (slash == NULL || slash == out) {
        safe_copy(out, out_size, "/");
    } else {
        *slash = '\0';
    }
}

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash == NULL ? path : slash + 1;
}

static int fs_find_dir(const TinyFS *fs, const char *path) {
    char normalized[MAX_PATH_LEN];
    int i;
    if (!normalize_path(path, normalized, sizeof(normalized))) {
        return -1;
    }
    for (i = 0; i < fs->directory_count; i++) {
        if (strcmp(fs->directories[i], normalized) == 0) {
            return i;
        }
    }
    return -1;
}

static int fs_find_file(const TinyFS *fs, const char *name) {
    char normalized[MAX_PATH_LEN];
    int i;
    if (!normalize_path(name, normalized, sizeof(normalized))) {
        return -1;
    }
    for (i = 0; i < fs->file_count; i++) {
        if (strcmp(fs->files[i].name, normalized) == 0) {
            return i;
        }
    }
    return -1;
}

static int fs_free_count(const TinyFS *fs) {
    int i;
    int count = 0;
    for (i = 0; i < fs->total_blocks; i++) {
        if (!fs->used[i]) {
            count++;
        }
    }
    return count;
}

static int blocks_needed(const TinyFS *fs, int size) {
    if (size <= 0) {
        return 0;
    }
    return (size + fs->block_size - 1) / fs->block_size;
}

static int reserve_blocks(TinyFS *fs, int count, int *blocks) {
    int i;
    int selected = 0;
    for (i = 0; i < fs->total_blocks && selected < count; i++) {
        /* Bitmap value 1 means the block is already reserved. */
        if (!fs->used[i]) {
            fs->used[i] = 1;
            blocks[selected++] = i;
        }
    }
    if (selected != count) {
        /* Roll back partial reservations so failed creates do not leak blocks. */
        int j;
        for (j = 0; j < selected; j++) {
            fs->used[blocks[j]] = 0;
        }
        return 0;
    }
    return 1;
}

static void release_blocks(TinyFS *fs, const int *blocks, int count) {
    int i;
    for (i = 0; i < count; i++) {
        int block = blocks[i];
        if (block >= 0 && block < fs->total_blocks) {
            fs->used[block] = 0;
            memset(fs->data[block], 0, sizeof(fs->data[block]));
        }
    }
}

static void write_data(TinyFS *fs, FsFile *file, const char *content) {
    int remaining = file->size;
    int offset = 0;
    int i;
    for (i = 0; i < file->block_count; i++) {
        /* File content is striped across its allocated fixed-size blocks. */
        int block = file->blocks[i];
        int chunk = remaining < fs->block_size ? remaining : fs->block_size;
        memset(fs->data[block], 0, sizeof(fs->data[block]));
        if (chunk > 0) {
            memcpy(fs->data[block], content + offset, (size_t)chunk);
        }
        remaining -= chunk;
        offset += chunk;
    }
}

void fs_init(TinyFS *fs, int total_blocks, int block_size) {
    memset(fs, 0, sizeof(*fs));
    if (total_blocks <= 0) {
        total_blocks = 16;
    }
    if (block_size <= 0) {
        block_size = 32;
    }
    if (total_blocks > MAX_FS_BLOCKS) {
        total_blocks = MAX_FS_BLOCKS;
    }
    if (block_size > MAX_FS_BLOCK_SIZE) {
        block_size = MAX_FS_BLOCK_SIZE;
    }
    fs->total_blocks = total_blocks;
    fs->block_size = block_size;
    fs->directory_count = 1;
    safe_copy(fs->directories[0], sizeof(fs->directories[0]), "/");
}

int fs_create(TinyFS *fs, const char *name, const char *content, char *message, size_t message_size) {
    char normalized[MAX_PATH_LEN];
    char parent[MAX_PATH_LEN];
    FsFile *file = NULL;
    int size;
    int required;

    if (fs == NULL || !normalize_path(name, normalized, sizeof(normalized)) || strcmp(normalized, "/") == 0) {
        safe_copy(message, message_size, "invalid file name");
        return 0;
    }
    parent_path(normalized, parent, sizeof(parent));
    if (fs_find_dir(fs, parent) < 0) {
        safe_copy(message, message_size, "parent directory does not exist");
        return 0;
    }
    if (fs_find_file(fs, normalized) >= 0 || fs_find_dir(fs, normalized) >= 0) {
        safe_copy(message, message_size, "file already exists");
        return 0;
    }
    if (fs->file_count >= MAX_FS_FILES) {
        safe_copy(message, message_size, "file table is full");
        return 0;
    }
    if (content == NULL) {
        content = "";
    }
    size = (int)strlen(content);
    required = blocks_needed(fs, size);
    if (required > MAX_FILE_BLOCKS || required > fs_free_count(fs)) {
        safe_copy(message, message_size, "not enough free blocks");
        return 0;
    }
    file = &fs->files[fs->file_count];
    memset(file, 0, sizeof(*file));
    safe_copy(file->name, sizeof(file->name), normalized);
    file->size = size;
    file->block_count = required;
    if (!reserve_blocks(fs, required, file->blocks)) {
        safe_copy(message, message_size, "free-space bitmap is inconsistent");
        return 0;
    }
    write_data(fs, file, content);
    fs->file_count++;
    (void)snprintf(message, message_size, "created %s using %d block(s)", normalized, required);
    return 1;
}

int fs_write(TinyFS *fs, const char *name, const char *content, int append, char *message, size_t message_size) {
    char normalized[MAX_PATH_LEN];
    char combined[MAX_CONTENT_LEN];
    char current[MAX_CONTENT_LEN];
    FsFile *file = NULL;
    int index;
    int new_size;
    int required;
    int old_count;
    int extra;

    if (fs == NULL || !normalize_path(name, normalized, sizeof(normalized)) || content == NULL) {
        safe_copy(message, message_size, "invalid write request");
        return 0;
    }
    index = fs_find_file(fs, normalized);
    if (index < 0) {
        safe_copy(message, message_size, "file does not exist");
        return 0;
    }
    file = &fs->files[index];
    if (append) {
        /* Append is implemented by rebuilding content and resizing block allocation. */
        char read_message[MAX_MESSAGE_LEN];
        if (!fs_read(fs, normalized, current, sizeof(current), read_message, sizeof(read_message))) {
            safe_copy(message, message_size, read_message);
            return 0;
        }
        if (strlen(current) + strlen(content) >= sizeof(combined)) {
            safe_copy(message, message_size, "content is too large");
            return 0;
        }
        (void)snprintf(combined, sizeof(combined), "%s%s", current, content);
    } else {
        if (strlen(content) >= sizeof(combined)) {
            safe_copy(message, message_size, "content is too large");
            return 0;
        }
        safe_copy(combined, sizeof(combined), content);
    }

    new_size = (int)strlen(combined);
    required = blocks_needed(fs, new_size);
    old_count = file->block_count;
    extra = required - old_count;
    if (required > MAX_FILE_BLOCKS || (extra > 0 && extra > fs_free_count(fs))) {
        safe_copy(message, message_size, "not enough free blocks to write file");
        return 0;
    }
    if (extra > 0) {
        /* Growing a file reserves only the additional blocks it needs. */
        if (!reserve_blocks(fs, extra, file->blocks + old_count)) {
            safe_copy(message, message_size, "free-space bitmap is inconsistent");
            return 0;
        }
    } else if (extra < 0) {
        release_blocks(fs, file->blocks + required, old_count - required);
    }
    file->block_count = required;
    file->size = new_size;
    write_data(fs, file, combined);
    (void)snprintf(message, message_size, "%s %s; size=%d byte(s)", append ? "appended" : "wrote", normalized, new_size);
    return 1;
}

int fs_read(const TinyFS *fs, const char *name, char *out, size_t out_size, char *message, size_t message_size) {
    char normalized[MAX_PATH_LEN];
    int index;
    const FsFile *file = NULL;
    int remaining;
    int offset = 0;
    int i;

    if (fs == NULL || !normalize_path(name, normalized, sizeof(normalized)) || out == NULL || out_size == 0) {
        safe_copy(message, message_size, "invalid read request");
        return 0;
    }
    index = fs_find_file(fs, normalized);
    if (index < 0) {
        safe_copy(message, message_size, "file does not exist");
        out[0] = '\0';
        return 0;
    }
    file = &fs->files[index];
    if ((size_t)file->size + 1 > out_size) {
        safe_copy(message, message_size, "read buffer is too small");
        out[0] = '\0';
        return 0;
    }
    remaining = file->size;
    for (i = 0; i < file->block_count; i++) {
        int chunk = remaining < fs->block_size ? remaining : fs->block_size;
        memcpy(out + offset, fs->data[file->blocks[i]], (size_t)chunk);
        remaining -= chunk;
        offset += chunk;
    }
    out[file->size] = '\0';
    (void)snprintf(message, message_size, "read %s", normalized);
    return 1;
}

int fs_delete(TinyFS *fs, const char *name, char *message, size_t message_size) {
    char normalized[MAX_PATH_LEN];
    int index;
    int i;
    if (fs == NULL || !normalize_path(name, normalized, sizeof(normalized))) {
        safe_copy(message, message_size, "invalid delete request");
        return 0;
    }
    index = fs_find_file(fs, normalized);
    if (index < 0) {
        safe_copy(message, message_size, "file does not exist");
        return 0;
    }
    release_blocks(fs, fs->files[index].blocks, fs->files[index].block_count);
    for (i = index; i < fs->file_count - 1; i++) {
        fs->files[i] = fs->files[i + 1];
    }
    fs->file_count--;
    (void)snprintf(message, message_size, "deleted %s", normalized);
    return 1;
}

int fs_mkdir(TinyFS *fs, const char *path, char *message, size_t message_size) {
    char normalized[MAX_PATH_LEN];
    char parent[MAX_PATH_LEN];
    if (fs == NULL || !normalize_path(path, normalized, sizeof(normalized)) || strcmp(normalized, "/") == 0) {
        safe_copy(message, message_size, "invalid directory path");
        return 0;
    }
    if (fs->directory_count >= MAX_FS_DIRS) {
        safe_copy(message, message_size, "directory table is full");
        return 0;
    }
    parent_path(normalized, parent, sizeof(parent));
    if (fs_find_dir(fs, parent) < 0) {
        safe_copy(message, message_size, "parent directory does not exist");
        return 0;
    }
    if (fs_find_dir(fs, normalized) >= 0 || fs_find_file(fs, normalized) >= 0) {
        safe_copy(message, message_size, "directory already exists");
        return 0;
    }
    safe_copy(fs->directories[fs->directory_count], sizeof(fs->directories[fs->directory_count]), normalized);
    fs->directory_count++;
    (void)snprintf(message, message_size, "created directory %s", normalized);
    return 1;
}

int fs_rmdir(TinyFS *fs, const char *path, char *message, size_t message_size) {
    char normalized[MAX_PATH_LEN];
    int index;
    int i;
    if (fs == NULL || !normalize_path(path, normalized, sizeof(normalized)) || strcmp(normalized, "/") == 0) {
        safe_copy(message, message_size, "invalid directory path");
        return 0;
    }
    index = fs_find_dir(fs, normalized);
    if (index < 0) {
        safe_copy(message, message_size, "directory does not exist");
        return 0;
    }
    for (i = 0; i < fs->file_count; i++) {
        char parent[MAX_PATH_LEN];
        parent_path(fs->files[i].name, parent, sizeof(parent));
        if (strcmp(parent, normalized) == 0) {
            safe_copy(message, message_size, "directory is not empty");
            return 0;
        }
    }
    for (i = 0; i < fs->directory_count; i++) {
        char parent[MAX_PATH_LEN];
        if (i == index) {
            continue;
        }
        parent_path(fs->directories[i], parent, sizeof(parent));
        if (strcmp(parent, normalized) == 0) {
            safe_copy(message, message_size, "directory has subdirectories");
            return 0;
        }
    }
    for (i = index; i < fs->directory_count - 1; i++) {
        safe_copy(fs->directories[i], sizeof(fs->directories[i]), fs->directories[i + 1]);
    }
    fs->directory_count--;
    (void)snprintf(message, message_size, "removed directory %s", normalized);
    return 1;
}

int fs_is_dir(const TinyFS *fs, const char *path) {
    return fs != NULL && fs_find_dir(fs, path) >= 0;
}

int fs_is_file(const TinyFS *fs, const char *path) {
    return fs != NULL && fs_find_file(fs, path) >= 0;
}

void fs_list(const TinyFS *fs) {
    int i;
    int j;
    printf("%-28s %-8s %-20s\n", "Path", "Size", "Blocks");
    if (fs->file_count == 0) {
        printf("(no files)\n");
        return;
    }
    for (i = 0; i < fs->file_count; i++) {
        printf("%-28s %-8d ", fs->files[i].name, fs->files[i].size);
        for (j = 0; j < fs->files[i].block_count; j++) {
            printf("%d%s", fs->files[i].blocks[j], j == fs->files[i].block_count - 1 ? "" : ",");
        }
        printf("\n");
    }
}

void fs_list_dir(const TinyFS *fs, const char *path) {
    char normalized[MAX_PATH_LEN];
    int i;
    if (fs == NULL || !normalize_path(path, normalized, sizeof(normalized)) || fs_find_dir(fs, normalized) < 0) {
        printf("directory not found: %s\n", path == NULL ? "(null)" : path);
        return;
    }
    printf("Directory %s\n", normalized);
    printf("%-8s %-28s %-8s\n", "Type", "Name", "Size");
    for (i = 0; i < fs->directory_count; i++) {
        char parent[MAX_PATH_LEN];
        if (strcmp(fs->directories[i], normalized) == 0) {
            continue;
        }
        parent_path(fs->directories[i], parent, sizeof(parent));
        if (strcmp(parent, normalized) == 0) {
            printf("%-8s %-28s %-8s\n", "DIR", base_name(fs->directories[i]), "-");
        }
    }
    for (i = 0; i < fs->file_count; i++) {
        char parent[MAX_PATH_LEN];
        parent_path(fs->files[i].name, parent, sizeof(parent));
        if (strcmp(parent, normalized) == 0) {
            printf("%-8s %-28s %-8d\n", "FILE", base_name(fs->files[i].name), fs->files[i].size);
        }
    }
}

void fs_print_tree(const TinyFS *fs) {
    int i;
    printf("Directory table:\n");
    for (i = 0; i < fs->directory_count; i++) {
        printf("  [DIR]  %s\n", fs->directories[i]);
    }
    printf("File table:\n");
    for (i = 0; i < fs->file_count; i++) {
        printf("  [FILE] %s size=%d blocks=%d\n", fs->files[i].name, fs->files[i].size, fs->files[i].block_count);
    }
}

void fs_print_bitmap(const TinyFS *fs) {
    int i;
    printf("Bitmap(1=free,0=used): ");
    for (i = 0; i < fs->total_blocks; i++) {
        printf("%c", fs->used[i] ? '0' : '1');
    }
    printf(" free=%d/%d\n", fs_free_count(fs), fs->total_blocks);
}

static int fixed_string_is_terminated(const char *text, size_t size) {
    return text != NULL && memchr(text, '\0', size) != NULL;
}

static int stored_path_is_canonical(const char *path, size_t size) {
    char normalized[MAX_PATH_LEN];
    return fixed_string_is_terminated(path, size) && normalize_path(path, normalized, sizeof(normalized)) &&
           strcmp(path, normalized) == 0;
}

static int validate_image_state(const TinyFS *fs) {
    int i;
    int used_by_file[MAX_FS_BLOCKS];
    if (fs == NULL || fs->total_blocks <= 0 || fs->total_blocks > MAX_FS_BLOCKS ||
        fs->block_size <= 0 || fs->block_size > MAX_FS_BLOCK_SIZE ||
        fs->directory_count <= 0 || fs->directory_count > MAX_FS_DIRS ||
        fs->file_count < 0 || fs->file_count > MAX_FS_FILES) {
        return 0;
    }
    for (i = 0; i < MAX_FS_BLOCKS; i++) {
        used_by_file[i] = 0;
        if (i < fs->total_blocks && fs->used[i] != 0 && fs->used[i] != 1) {
            return 0;
        }
        if (i >= fs->total_blocks && fs->used[i] != 0) {
            return 0;
        }
    }
    if (!stored_path_is_canonical(fs->directories[0], sizeof(fs->directories[0])) || strcmp(fs->directories[0], "/") != 0) {
        return 0;
    }
    for (i = 0; i < fs->directory_count; i++) {
        char parent[MAX_PATH_LEN];
        int j;
        if (!stored_path_is_canonical(fs->directories[i], sizeof(fs->directories[i])) || fs->directories[i][0] != '/') {
            return 0;
        }
        for (j = i + 1; j < fs->directory_count; j++) {
            if (!stored_path_is_canonical(fs->directories[j], sizeof(fs->directories[j])) ||
                strcmp(fs->directories[i], fs->directories[j]) == 0) {
                return 0;
            }
        }
        if (i > 0) {
            parent_path(fs->directories[i], parent, sizeof(parent));
            if (fs_find_dir(fs, parent) < 0) {
                return 0;
            }
        }
    }
    for (i = 0; i < fs->file_count; i++) {
        const FsFile *file = &fs->files[i];
        char parent[MAX_PATH_LEN];
        int j;
        if (!stored_path_is_canonical(file->name, sizeof(file->name)) || file->name[0] != '/' ||
            strcmp(file->name, "/") == 0 || file->size < 0 || file->size >= MAX_CONTENT_LEN ||
            file->block_count < 0 || file->block_count > MAX_FILE_BLOCKS ||
            file->block_count != blocks_needed(fs, file->size)) {
            return 0;
        }
        parent_path(file->name, parent, sizeof(parent));
        if (fs_find_dir(fs, parent) < 0) {
            return 0;
        }
        for (j = 0; j < fs->directory_count; j++) {
            if (strcmp(file->name, fs->directories[j]) == 0) {
                return 0;
            }
        }
        for (j = i + 1; j < fs->file_count; j++) {
            if (!stored_path_is_canonical(fs->files[j].name, sizeof(fs->files[j].name)) ||
                strcmp(file->name, fs->files[j].name) == 0) {
                return 0;
            }
        }
        for (j = 0; j < file->block_count; j++) {
            int block = file->blocks[j];
            if (block < 0 || block >= fs->total_blocks || fs->used[block] != 1 || used_by_file[block]) {
                return 0;
            }
            used_by_file[block] = 1;
        }
    }
    for (i = 0; i < fs->total_blocks; i++) {
        if (fs->used[i] != used_by_file[i]) {
            return 0;
        }
    }
    return 1;
}

static int read_image_file(const char *path, TinyFS *loaded, char *message, size_t message_size) {
    TinyFSImageHeader header;
    FILE *file = NULL;
    int extra;

    if (path == NULL || loaded == NULL) {
        safe_copy(message, message_size, "invalid image load request");
        return 0;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        safe_copy(message, message_size, "cannot open image for reading");
        return 0;
    }
    if (fread(&header, sizeof(header), 1, file) != 1) {
        safe_copy(message, message_size, "invalid or truncated file-system image header");
        (void)fclose(file);
        return 0;
    }
    if (memcmp(header.magic, TINYFS_IMAGE_MAGIC, sizeof(TINYFS_IMAGE_MAGIC)) != 0 ||
        header.version != TINYFS_IMAGE_VERSION || header.header_size != (uint32_t)sizeof(header) ||
        header.payload_size != (uint32_t)sizeof(*loaded)) {
        safe_copy(message, message_size, "invalid file-system image header");
        (void)fclose(file);
        return 0;
    }
    if (fread(loaded, sizeof(*loaded), 1, file) != 1) {
        safe_copy(message, message_size, "invalid or truncated file-system image payload");
        (void)fclose(file);
        return 0;
    }
    extra = fgetc(file);
    if (extra != EOF) {
        safe_copy(message, message_size, "file-system image has trailing data");
        (void)fclose(file);
        return 0;
    }
    if (ferror(file)) {
        safe_copy(message, message_size, "failed to read file-system image");
        (void)fclose(file);
        return 0;
    }
    (void)fclose(file);
    if (fs_image_checksum((const unsigned char *)loaded, sizeof(*loaded)) != header.checksum) {
        safe_copy(message, message_size, "file-system image checksum mismatch");
        return 0;
    }
    if (!validate_image_state(loaded)) {
        safe_copy(message, message_size, "file-system image metadata is out of range");
        return 0;
    }
    return 1;
}

int fs_save_image(const TinyFS *fs, const char *path, char *message, size_t message_size) {
    TinyFSImageHeader header;
    FILE *file = NULL;
    int ok;

    if (fs == NULL || path == NULL) {
        safe_copy(message, message_size, "invalid image save request");
        return 0;
    }
    if (!validate_image_state(fs)) {
        safe_copy(message, message_size, "file-system state failed image validation");
        return 0;
    }
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, TINYFS_IMAGE_MAGIC, sizeof(TINYFS_IMAGE_MAGIC));
    header.version = TINYFS_IMAGE_VERSION;
    header.header_size = (uint32_t)sizeof(header);
    header.payload_size = (uint32_t)sizeof(*fs);
    header.checksum = fs_image_checksum((const unsigned char *)fs, sizeof(*fs));
    file = fopen(path, "wb");
    if (file == NULL) {
        safe_copy(message, message_size, "cannot open image for writing");
        return 0;
    }
    ok = fwrite(&header, sizeof(header), 1, file) == 1 && fwrite(fs, sizeof(*fs), 1, file) == 1;
    if (fclose(file) != 0) {
        ok = 0;
    }
    if (!ok) {
        safe_copy(message, message_size, "failed to write file-system image");
        return 0;
    }
    (void)snprintf(message, message_size, "saved image to %s checksum=%08x", path, header.checksum);
    return 1;
}

int fs_load_image(TinyFS *fs, const char *path, char *message, size_t message_size) {
    TinyFS loaded;

    if (fs == NULL || path == NULL) {
        safe_copy(message, message_size, "invalid image load request");
        return 0;
    }
    if (!read_image_file(path, &loaded, message, message_size)) {
        return 0;
    }
    *fs = loaded;
    (void)snprintf(message, message_size, "loaded image from %s", path);
    return 1;
}

int fs_check_image(const char *path, char *message, size_t message_size) {
    TinyFS loaded;
    if (!read_image_file(path, &loaded, message, message_size)) {
        return 0;
    }
    (void)snprintf(message, message_size, "image %s passed TinyFS validation", path);
    return 1;
}

int fs_run_script(TinyFS *fs, const char *path) {
    FILE *file = NULL;
    char line[1024];
    int line_no = 0;

    if (fs == NULL || path == NULL) {
        return 0;
    }
    file = fopen(path, "r");
    if (file == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *first = trim(line);
        char *comma1 = NULL;
        char *comma2 = NULL;
        char *op = NULL;
        char *name = NULL;
        char *content = NULL;
        char message[MAX_MESSAGE_LEN];
        char output[MAX_CONTENT_LEN];
        int ok = 0;

        line_no++;
        if (first[0] == '\0' || first[0] == '#') {
            continue;
        }
        if (strncmp(first, "op", 2) == 0 || strncmp(first, "OP", 2) == 0) {
            continue;
        }
        comma1 = strchr(first, ',');
        if (comma1 != NULL) {
            *comma1 = '\0';
            comma2 = strchr(comma1 + 1, ',');
            if (comma2 != NULL) {
                *comma2 = '\0';
            }
        }
        op = trim(first);
        name = comma1 == NULL ? NULL : trim(comma1 + 1);
        content = comma2 == NULL ? "" : trim(comma2 + 1);

        if (strcmp(op, "create") == 0) {
            ok = fs_create(fs, name, content, message, sizeof(message));
        } else if (strcmp(op, "write") == 0) {
            ok = fs_write(fs, name, content, 0, message, sizeof(message));
        } else if (strcmp(op, "append") == 0) {
            ok = fs_write(fs, name, content, 1, message, sizeof(message));
        } else if (strcmp(op, "read") == 0) {
            ok = fs_read(fs, name, output, sizeof(output), message, sizeof(message));
            if (ok) {
                printf("read %s => %s\n", name, output);
            }
        } else if (strcmp(op, "delete") == 0 || strcmp(op, "rm") == 0) {
            ok = fs_delete(fs, name, message, sizeof(message));
        } else if (strcmp(op, "mkdir") == 0) {
            ok = fs_mkdir(fs, name, message, sizeof(message));
        } else if (strcmp(op, "rmdir") == 0) {
            ok = fs_rmdir(fs, name, message, sizeof(message));
        } else if (strcmp(op, "lsdir") == 0 || strcmp(op, "listdir") == 0) {
            fs_list_dir(fs, name == NULL || name[0] == '\0' ? "/" : name);
            safe_copy(message, sizeof(message), "listed directory");
            ok = 1;
        } else if (strcmp(op, "tree") == 0) {
            fs_print_tree(fs);
            safe_copy(message, sizeof(message), "printed directory tree");
            ok = 1;
        } else if (strcmp(op, "list") == 0 || strcmp(op, "ls") == 0) {
            fs_list(fs);
            safe_copy(message, sizeof(message), "listed files");
            ok = 1;
        } else {
            (void)snprintf(message, sizeof(message), "unknown operation at line %d", line_no);
        }
        printf("fs %-6s line=%d result=%s message=%s\n", op, line_no, ok ? "OK" : "FAIL", message);
    }
    (void)fclose(file);
    return 1;
}
