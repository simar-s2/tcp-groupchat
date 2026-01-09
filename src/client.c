/**
 * @file client.c
 * @brief TCP Group Chat Client with username support
 * 
 * This client connects to the group chat server and uses two threads:
 * - Sender thread: Sends random messages or user input to the server
 * - Receiver thread: Receives and logs messages from other clients
 * 
 * The client supports username registration and displays formatted chat messages.
 */

/* Feature test macros defined in Makefile */
#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "protocol.h"
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define RAND_BYTES 10

/* Shared data between threads */
typedef struct {
    int socket_fd;
    int num_messages;
    FILE *log_file;
    volatile int should_stop;
    char username[MAX_USERNAME_LEN];
} thread_data_t;

/**
 * @brief Sender thread - sends messages to the server
 */
void *sender_thread(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;
    
    /* First, send username registration */
    uint8_t username_msg[BUF_SIZE];
    int offset = 0;
    username_msg[offset++] = MSG_TYPE_USERNAME;
    
    uint8_t username_len = (uint8_t)strlen(data->username);
    username_msg[offset++] = username_len;
    memcpy(username_msg + offset, data->username, username_len);
    offset += username_len;
    username_msg[offset++] = '\n';
    
    if (send(data->socket_fd, username_msg, offset, 0) < 0) {
        log_message(LOG_ERROR, "Failed to send username");
        return NULL;
    }
    
    log_message(LOG_INFO, "Registered username: %s", data->username);
    usleep(100000); /* Small delay to let username propagate */
    
    /* Send random messages */
    for (int i = 0; i < data->num_messages && !data->should_stop; i++) {
        uint8_t random_bytes[RAND_BYTES];
        char hex_str[RAND_BYTES * 2 + 1];
        
        /* Generate random bytes using /dev/urandom (cross-platform) */
        FILE *urandom = fopen("/dev/urandom", "r");
        if (!urandom || fread(random_bytes, 1, sizeof(random_bytes), urandom) != sizeof(random_bytes)) {
            if (urandom) fclose(urandom);
            log_message(LOG_ERROR, "Failed to read random bytes");
            break;
        }
        fclose(urandom);
        
        if (bytes_to_hex(random_bytes, RAND_BYTES, hex_str, sizeof(hex_str)) != 0) {
            log_message(LOG_ERROR, "bytes_to_hex failed");
            break;
        }
        
        uint8_t send_buf[BUF_SIZE];
        send_buf[0] = MSG_TYPE_CHAT;
        
        int msg_len = (int)strlen(hex_str);
        memcpy(&send_buf[1], hex_str, msg_len);
        send_buf[1 + msg_len] = '\n';
        
        if (send(data->socket_fd, send_buf, 1 + msg_len + 1, 0) < 0) {
            log_message(LOG_ERROR, "Failed to send message");
            break;
        }
        
        log_message(LOG_DEBUG, "Sent message %d/%d", i + 1, data->num_messages);
        usleep(100000); /* 100ms delay between messages */
    }
    
    /* Send disconnect notification */
    uint8_t end_msg[2];
    end_msg[0] = MSG_TYPE_DISCONNECT;
    end_msg[1] = '\n';
    
    if (send(data->socket_fd, end_msg, 2, 0) < 0) {
        log_message(LOG_WARN, "Failed to send disconnect");
    }
    
    log_message(LOG_INFO, "Sender thread completed");
    data->should_stop = 1;
    
    return NULL;
}

/**
 * @brief Receiver thread - receives and logs messages from server
 */
void *receiver_thread(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;
    
    while (!data->should_stop) {
        uint8_t type;
        ssize_t r = recv_exact(data->socket_fd, &type, 1);
        
        if (r <= 0) {
            if (r == 0) {
                log_message(LOG_INFO, "Server closed connection");
            } else {
                log_message(LOG_ERROR, "recv_exact failed");
            }
            break;
        }
        
        if (type == MSG_TYPE_CHAT) {
            /* Chat message: [type][ip][port][username_len][username][message]\n */
            uint32_t ip_net;
            uint16_t port_net;
            
            if (recv_exact(data->socket_fd, (uint8_t *)&ip_net, sizeof(ip_net)) <= 0) {
                break;
            }
            
            if (recv_exact(data->socket_fd, (uint8_t *)&port_net, sizeof(port_net)) <= 0) {
                break;
            }
            
            uint8_t username_len;
            if (recv_exact(data->socket_fd, &username_len, 1) <= 0) {
                break;
            }
            
            char username[MAX_USERNAME_LEN];
            if (username_len > 0 && username_len < MAX_USERNAME_LEN) {
                if (recv_exact(data->socket_fd, (uint8_t *)username, username_len) <= 0) {
                    break;
                }
                username[username_len] = '\0';
            } else {
                strcpy(username, "unknown");
            }
            
            /* Read message until newline */
            char msg_buf[BUF_SIZE];
            int idx = 0;
            while (1) {
                uint8_t ch;
                r = recv(data->socket_fd, &ch, 1, 0);
                if (r <= 0) {
                    data->should_stop = 1;
                    break;
                }
                if (ch == '\n') {
                    break;
                }
                if (idx < (int)sizeof(msg_buf) - 1) {
                    msg_buf[idx++] = (char)ch;
                }
            }
            
            if (data->should_stop) {
                break;
            }
            
            msg_buf[idx] = '\0';
            
            /* Convert IP to string */
            struct in_addr addr;
            addr.s_addr = ip_net;
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));
            
            uint16_t port_host = ntohs(port_net);
            
            /* Log formatted message */
            fprintf(data->log_file, "[%s@%s:%u] %s\n", 
                    username, ip_str, port_host, msg_buf);
            fflush(data->log_file);
            
        } else if (type == MSG_TYPE_JOIN) {
            /* Join notification: [type][ip][port][username_len][username]\n */
            uint32_t ip_net;
            uint16_t port_net;
            
            if (recv_exact(data->socket_fd, (uint8_t *)&ip_net, sizeof(ip_net)) <= 0) {
                break;
            }
            
            if (recv_exact(data->socket_fd, (uint8_t *)&port_net, sizeof(port_net)) <= 0) {
                break;
            }
            
            uint8_t username_len;
            if (recv_exact(data->socket_fd, &username_len, 1) <= 0) {
                break;
            }
            
            char username[MAX_USERNAME_LEN];
            if (username_len > 0 && username_len < MAX_USERNAME_LEN) {
                if (recv_exact(data->socket_fd, (uint8_t *)username, username_len) <= 0) {
                    break;
                }
                username[username_len] = '\0';
            } else {
                strcpy(username, "unknown");
            }
            
            /* Read newline */
            uint8_t newline;
            if (recv(data->socket_fd, &newline, 1, 0) <= 0) {
                break;
            }
            
            struct in_addr addr;
            addr.s_addr = ip_net;
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));
            
            fprintf(data->log_file, "*** %s joined the chat from %s:%u ***\n", 
                    username, ip_str, ntohs(port_net));
            fflush(data->log_file);
            
        } else if (type == MSG_TYPE_DISCONNECT) {
            /* Disconnect notification */
            uint32_t ip_net;
            uint16_t port_net;
            
            if (recv_exact(data->socket_fd, (uint8_t *)&ip_net, sizeof(ip_net)) <= 0) {
                break;
            }
            
            if (recv_exact(data->socket_fd, (uint8_t *)&port_net, sizeof(port_net)) <= 0) {
                break;
            }
            
            uint8_t username_len;
            if (recv_exact(data->socket_fd, &username_len, 1) <= 0) {
                break;
            }
            
            char username[MAX_USERNAME_LEN];
            if (username_len > 0 && username_len < MAX_USERNAME_LEN) {
                if (recv_exact(data->socket_fd, (uint8_t *)username, username_len) <= 0) {
                    break;
                }
                username[username_len] = '\0';
            } else {
                strcpy(username, "unknown");
            }
            
            /* Read newline */
            uint8_t newline;
            if (recv(data->socket_fd, &newline, 1, 0) <= 0) {
                break;
            }
            
            struct in_addr addr;
            addr.s_addr = ip_net;
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));
            
            fprintf(data->log_file, "*** %s left the chat from %s:%u ***\n", 
                    username, ip_str, ntohs(port_net));
            fflush(data->log_file);
            
        } else {
            log_message(LOG_WARN, "Unknown message type: %u", type);
            data->should_stop = 1;
            break;
        }
    }
    
    log_message(LOG_INFO, "Receiver thread completed");
    return NULL;
}

/**
 * @brief Main client function
 */
int main(int argc, char *argv[]) {
    if (argc != 6) {
        fprintf(stderr, "Usage: %s <IP> <port> <username> <#messages> <log_file>\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    char *ip_addr = argv[1];
    int port = atoi(argv[2]);
    char *username = argv[3];
    int num_messages = atoi(argv[4]);
    char *log_file_path = argv[5];
    
    if (strlen(username) >= MAX_USERNAME_LEN) {
        fprintf(stderr, "Username too long (max %d characters)\n", MAX_USERNAME_LEN - 1);
        return EXIT_FAILURE;
    }
    
    /* Initialize logging */
    log_init(NULL, LOG_INFO);
    log_message(LOG_INFO, "Connecting to %s:%d as %s", ip_addr, port, username);
    
    /* Open log file for chat messages */
    FILE *log_file = fopen(log_file_path, "w");
    if (log_file == NULL) {
        handle_error("fopen");
    }
    
    /* Create socket */
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd == -1) {
        fclose(log_file);
        handle_error("socket");
    }
    
    /* Setup server address */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip_addr, &addr.sin_addr) <= 0) {
        close(sfd);
        fclose(log_file);
        handle_error("inet_pton");
    }
    
    /* Connect to server */
    if (connect(sfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        close(sfd);
        fclose(log_file);
        handle_error("connect");
    }
    
    log_message(LOG_INFO, "Connected to server");
    
    /* Setup thread data */
    thread_data_t thread_data = {
        .socket_fd = sfd,
        .num_messages = num_messages,
        .log_file = log_file,
        .should_stop = 0,
    };
    strncpy(thread_data.username, username, MAX_USERNAME_LEN - 1);
    thread_data.username[MAX_USERNAME_LEN - 1] = '\0';
    
    /* Create threads */
    pthread_t sender_tid, receiver_tid;
    
    if (pthread_create(&receiver_tid, NULL, receiver_thread, &thread_data) != 0) {
        close(sfd);
        fclose(log_file);
        handle_error("pthread_create receiver");
    }
    
    if (pthread_create(&sender_tid, NULL, sender_thread, &thread_data) != 0) {
        close(sfd);
        fclose(log_file);
        handle_error("pthread_create sender");
    }
    
    /* Wait for threads to complete */
    pthread_join(sender_tid, NULL);
    pthread_join(receiver_tid, NULL);
    
    log_message(LOG_INFO, "Disconnected from server");
    
    /* Cleanup */
    close(sfd);
    fclose(log_file);
    log_close();
    
    return EXIT_SUCCESS;
}
