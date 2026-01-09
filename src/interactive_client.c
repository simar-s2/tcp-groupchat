/**
 * @file interactive_client.c
 * @brief Interactive TCP Group Chat Client
 * 
 * This client allows real-time keyboard input for sending messages.
 * Uses two threads:
 * - Sender thread: Reads from stdin and sends to server
 * - Receiver thread: Receives and displays messages from other clients
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

/* Shared data between threads */
typedef struct {
    int socket_fd;
    volatile int should_stop;
    char username[MAX_USERNAME_LEN];
} thread_data_t;

/**
 * @brief Sender thread - reads from stdin and sends to server
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
        fprintf(stderr, "Failed to register username\n");
        return NULL;
    }
    
    printf("\n✓ Connected as '%s'\n", data->username);
    printf("Type your messages (or 'quit' to exit):\n");
    printf("─────────────────────────────────────────\n");
    
    usleep(100000); /* Small delay to let username propagate */
    
    char input_line[MAX_MESSAGE_LEN];
    
    while (!data->should_stop) {
        printf("> ");
        fflush(stdout);
        
        if (fgets(input_line, sizeof(input_line), stdin) == NULL) {
            break;
        }
        
        /* Remove newline */
        size_t len = strlen(input_line);
        if (len > 0 && input_line[len - 1] == '\n') {
            input_line[len - 1] = '\0';
            len--;
        }
        
        /* Check for quit command */
        if (strcmp(input_line, "quit") == 0 || strcmp(input_line, "exit") == 0) {
            printf("Disconnecting...\n");
            data->should_stop = 1;
            break;
        }
        
        /* Skip empty messages */
        if (len == 0) {
            continue;
        }
        
        /* Send message */
        uint8_t send_buf[BUF_SIZE];
        send_buf[0] = MSG_TYPE_CHAT;
        memcpy(&send_buf[1], input_line, len);
        send_buf[1 + len] = '\n';
        
        if (send(data->socket_fd, send_buf, 1 + len + 1, 0) < 0) {
            fprintf(stderr, "\nFailed to send message\n");
            break;
        }
    }
    
    /* Send disconnect notification */
    uint8_t end_msg[2];
    end_msg[0] = MSG_TYPE_DISCONNECT;
    end_msg[1] = '\n';
    send(data->socket_fd, end_msg, 2, 0);
    
    data->should_stop = 1;
    return NULL;
}

/**
 * @brief Receiver thread - receives and displays messages from server
 */
void *receiver_thread(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;
    
    while (!data->should_stop) {
        uint8_t type;
        ssize_t r = recv_exact(data->socket_fd, &type, 1);
        
        if (r <= 0) {
            if (!data->should_stop) {
                printf("\n✗ Connection closed by server\n");
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
            
            /* Display formatted message */
            printf("\r\033[K");  /* Clear current line */
            printf("<%s> %s\n", username, msg_buf);
            printf("> ");
            fflush(stdout);
            
        } else if (type == MSG_TYPE_JOIN) {
            /* Join notification */
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
            
            printf("\r\033[K");
            printf("*** %s joined the chat ***\n", username);
            printf("> ");
            fflush(stdout);
            
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
            
            printf("\r\033[K");
            printf("*** %s left the chat ***\n", username);
            printf("> ");
            fflush(stdout);
        }
    }
    
    data->should_stop = 1;
    return NULL;
}

/**
 * @brief Main client function
 */
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <username>\n", argv[0]);
        fprintf(stderr, "Example: %s 127.0.0.1 alice\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    char *ip_addr = argv[1];
    char *username = argv[2];
    int port = 8080; /* Default port */
    
    if (strlen(username) >= MAX_USERNAME_LEN) {
        fprintf(stderr, "Username too long (max %d characters)\n", MAX_USERNAME_LEN - 1);
        return EXIT_FAILURE;
    }
    
    printf("Connecting to %s:%d...\n", ip_addr, port);
    
    /* Create socket */
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }
    
    /* Setup server address */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip_addr, &addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sfd);
        return EXIT_FAILURE;
    }
    
    /* Connect to server */
    if (connect(sfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("connect");
        close(sfd);
        return EXIT_FAILURE;
    }
    
    /* Setup thread data */
    thread_data_t thread_data = {
        .socket_fd = sfd,
        .should_stop = 0,
    };
    strncpy(thread_data.username, username, MAX_USERNAME_LEN - 1);
    thread_data.username[MAX_USERNAME_LEN - 1] = '\0';
    
    /* Create threads */
    pthread_t sender_tid, receiver_tid;
    
    if (pthread_create(&receiver_tid, NULL, receiver_thread, &thread_data) != 0) {
        perror("pthread_create receiver");
        close(sfd);
        return EXIT_FAILURE;
    }
    
    if (pthread_create(&sender_tid, NULL, sender_thread, &thread_data) != 0) {
        perror("pthread_create sender");
        close(sfd);
        return EXIT_FAILURE;
    }
    
    /* Wait for threads to complete */
    pthread_join(sender_tid, NULL);
    pthread_join(receiver_tid, NULL);
    
    printf("\nDisconnected.\n");
    
    /* Cleanup */
    close(sfd);
    
    return EXIT_SUCCESS;
}
