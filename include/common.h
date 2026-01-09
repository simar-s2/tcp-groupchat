/**
 * @file common.h
 * @brief Common utility functions and definitions
 */

#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <sys/types.h>
#include <stdio.h>

/* Error handling macro */
#define handle_error(msg)                                                      \
  do {                                                                         \
    perror(msg);                                                               \
    exit(EXIT_FAILURE);                                                        \
  } while (0)

/* Logging levels */
typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} log_level_t;

/**
 * @brief Initialize logging system
 * @param log_file File to write logs to (NULL for stderr)
 * @param level Minimum log level to output
 */
void log_init(FILE *log_file, log_level_t level);

/**
 * @brief Log a message with the specified level
 * @param level Log level
 * @param format Printf-style format string
 */
void log_message(log_level_t level, const char *format, ...);

/**
 * @brief Close the logging system
 */
void log_close(void);

/**
 * @brief Receive exactly len bytes from a socket
 * @param sock Socket file descriptor
 * @param buf Buffer to store received data
 * @param len Number of bytes to receive
 * @return Number of bytes received, or -1 on error
 */
ssize_t recv_exact(int sock, uint8_t *buf, size_t len);

/**
 * @brief Send exactly len bytes to a socket
 * @param sock Socket file descriptor
 * @param buf Buffer containing data to send
 * @param len Number of bytes to send
 * @return Number of bytes sent, or -1 on error
 */
ssize_t send_exact(int sock, const uint8_t *buf, size_t len);

/**
 * @brief Convert random bytes to hexadecimal string
 * @param buf Input buffer with random bytes
 * @param buf_size Size of input buffer
 * @param str Output string buffer
 * @param str_size Size of output buffer
 * @return 0 on success, -1 on error
 */
int bytes_to_hex(const uint8_t *buf, ssize_t buf_size, char *str, ssize_t str_size);

/**
 * @brief Set socket to non-blocking mode
 * @param fd Socket file descriptor
 * @return 0 on success, -1 on error
 */
int set_nonblocking(int fd);

#endif /* COMMON_H */
