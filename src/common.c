/**
 * @file common.c
 * @brief Implementation of common utility functions
 */

/* Feature test macros defined in Makefile */
#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

/* Global logging state */
static FILE *log_file_handle = NULL;
static log_level_t current_log_level = LOG_INFO;

static const char *level_strings[] = {
    "DEBUG", "INFO", "WARN", "ERROR"
};

void log_init(FILE *log_file, log_level_t level) {
    log_file_handle = log_file ? log_file : stderr;
    current_log_level = level;
}

void log_message(log_level_t level, const char *format, ...) {
    if (level < current_log_level || !log_file_handle) {
        return;
    }

    time_t now;
    time(&now);
    struct tm *tm_info = localtime(&now);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(log_file_handle, "[%s] [%s] ", time_buf, level_strings[level]);

    va_list args;
    va_start(args, format);
    vfprintf(log_file_handle, format, args);
    va_end(args);

    fprintf(log_file_handle, "\n");
    fflush(log_file_handle);
}

void log_close(void) {
    if (log_file_handle && log_file_handle != stderr) {
        fclose(log_file_handle);
        log_file_handle = NULL;
    }
}

ssize_t recv_exact(int sock, uint8_t *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t r = recv(sock, buf + total, len - total, 0);
        if (r < 0) {
            return -1;
        }
        if (r == 0) {
            return 0; /* Connection closed */
        }
        total += (size_t)r;
    }
    return (ssize_t)total;
}

ssize_t send_exact(int sock, const uint8_t *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t s = send(sock, buf + total, len - total, 0);
        if (s < 0) {
            return -1;
        }
        total += (size_t)s;
    }
    return (ssize_t)total;
}

int bytes_to_hex(const uint8_t *buf, ssize_t buf_size, char *str, ssize_t str_size) {
    if (buf == NULL || str == NULL || buf_size <= 0 || str_size < (buf_size * 2 + 1)) {
        return -1;
    }

    for (int i = 0; i < buf_size; i++) {
        sprintf(str + i * 2, "%02X", buf[i]);
    }
    str[buf_size * 2] = '\0';

    return 0;
}

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
