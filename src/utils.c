#include "os_project.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

void safe_copy(char *dst, size_t dst_size, const char *src) {
    if (dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    (void)snprintf(dst, dst_size, "%s", src);
}

char *trim(char *text) {
    char *end = NULL;
    if (text == NULL) {
        return NULL;
    }
    while (isspace((unsigned char)*text)) {
        text++;
    }
    if (*text == '\0') {
        return text;
    }
    end = text + strlen(text) - 1;
    while (end > text && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return text;
}

int parse_int_value(const char *text, int *out) {
    char *endptr = NULL;
    long value;
    if (text == NULL || text[0] == '\0' || out == NULL) {
        return 0;
    }
    errno = 0;
    value = strtol(text, &endptr, 10);
    if (errno == ERANGE || endptr == text || *endptr != '\0' || value < INT_MIN || value > INT_MAX) {
        return 0;
    }
    *out = (int)value;
    return 1;
}

int parse_int_list(const char *text, int *out, int max_count) {
    char buffer[4096];
    char *cursor = NULL;
    int count = 0;

    if (text == NULL || out == NULL || max_count <= 0) {
        return -1;
    }
    safe_copy(buffer, sizeof(buffer), text);
    cursor = buffer;
    while (*cursor != '\0') {
        char *endptr = NULL;
        long value;
        while (*cursor == ',' || *cursor == ';' || isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        errno = 0;
        value = strtol(cursor, &endptr, 10);
        if (cursor == endptr || errno != 0 || value < INT_MIN || value > INT_MAX) {
            return -1;
        }
        if (count >= max_count) {
            return -1;
        }
        out[count++] = (int)value;
        cursor = endptr;
    }
    return count;
}

int read_text_file(const char *path, char *buffer, size_t buffer_size) {
    FILE *file = NULL;
    size_t read_size;

    if (path == NULL || buffer == NULL || buffer_size == 0) {
        return 0;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    read_size = fread(buffer, 1, buffer_size - 1, file);
    buffer[read_size] = '\0';
    if (ferror(file) != 0) {
        (void)fclose(file);
        return 0;
    }
    (void)fclose(file);
    return 1;
}
