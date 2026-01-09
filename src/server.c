/**
 * @file server.c
 * @brief TCP Group Chat Server using poll for cross-platform scalability
 * 
 * This server handles multiple concurrent clients using poll, a portable
 * I/O multiplexing mechanism. It broadcasts messages from any client
 * to all other connected clients.
 * 
 * Architecture:
 * - Uses poll for cross-platform event-driven I/O (works on Linux, macOS, BSD)
 * - Non-blocking sockets for all clients
 * - Supports graceful shutdown when all clients disconnect
 * - Logs all server events with timestamps
 */

/* Feature test macros defined in Makefile */
#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "protocol.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define LISTEN_BACKLOG 32

/* Client state structure */
typedef struct {
    int fd;                    /* Socket file descriptor */
    char buf[BUF_SIZE];        /* Receive buffer */
    ssize_t len;               /* Current buffer length */
    struct sockaddr_in addr;   /* Client address */
    char username[MAX_USERNAME_LEN]; /* Client username */
    int has_username;          /* Whether username is set */
} client_t;

/* Global server state */
static volatile int server_running = 1;
static int server_fd = -1;
static client_t *clients = NULL;
static int max_clients = 0;

/**
 * @brief Signal handler for graceful shutdown
 */
void handle_shutdown(int signum) {
    (void)signum;
    log_message(LOG_INFO, "Received shutdown signal");
    server_running = 0;
}

/**
 * @brief Broadcast a message to all connected clients
 * @param clients Array of client structures
 * @param max_clients Maximum number of clients
 * @param msg Message buffer to broadcast
 * @param msg_len Length of message
 */
void broadcast_message(client_t *clients, int max_clients, const char *msg, ssize_t msg_len) {
    if (!clients || !msg || msg_len <= 0) {
        return;
    }
    
    for (int i = 0; i < max_clients; i++) {
        if (clients[i].fd != -1 && clients[i].fd > 0) {
            ssize_t sent = send_exact(clients[i].fd, (const uint8_t *)msg, msg_len);
            if (sent != msg_len) {
                log_message(LOG_WARN, "Failed to send complete message to client %d", i);
            }
        }
    }
}

/**
 * @brief Send a join notification to all clients
 */
void broadcast_join(client_t *clients, int max_clients, client_t *new_client) {
    char msg[BUF_SIZE];
    int offset = 0;
    
    msg[offset++] = MSG_TYPE_JOIN;
    memcpy(msg + offset, &new_client->addr.sin_addr.s_addr, 4);
    offset += 4;
    memcpy(msg + offset, &new_client->addr.sin_port, 2);
    offset += 2;
    
    uint8_t username_len = (uint8_t)strlen(new_client->username);
    msg[offset++] = username_len;
    memcpy(msg + offset, new_client->username, username_len);
    offset += username_len;
    msg[offset++] = '\n';
    
    broadcast_message(clients, max_clients, msg, offset);
    
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &new_client->addr.sin_addr, ip_str, sizeof(ip_str));
    log_message(LOG_INFO, "Broadcasted join: %s from %s:%d", 
                new_client->username, ip_str, ntohs(new_client->addr.sin_port));
}

/**
 * @brief Remove a client and notify others
 */
void remove_client(client_t *cli, int client_index) {
    (void)client_index;
    if (cli->fd == -1) {
        return;
    }
    
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cli->addr.sin_addr, ip_str, sizeof(ip_str));
    log_message(LOG_INFO, "Client disconnected: %s from %s:%d", 
                cli->has_username ? cli->username : "unknown",
                ip_str, ntohs(cli->addr.sin_port));
    
    /* Save client info before closing */
    int had_username = cli->has_username;
    char username_copy[MAX_USERNAME_LEN];
    struct sockaddr_in addr_copy = cli->addr;
    
    if (had_username) {
        strncpy(username_copy, cli->username, MAX_USERNAME_LEN);
    }
    
    /* Close and mark as disconnected BEFORE broadcasting */
    close(cli->fd);
    cli->fd = -1;
    cli->len = 0;
    cli->has_username = 0;
    
    /* Now send disconnect notification to OTHER clients */
    if (had_username) {
        char msg[BUF_SIZE];
        int offset = 0;
        msg[offset++] = MSG_TYPE_DISCONNECT;
        memcpy(msg + offset, &addr_copy.sin_addr.s_addr, 4);
        offset += 4;
        memcpy(msg + offset, &addr_copy.sin_port, 2);
        offset += 2;
        uint8_t username_len = (uint8_t)strlen(username_copy);
        msg[offset++] = username_len;
        memcpy(msg + offset, username_copy, username_len);
        offset += username_len;
        msg[offset++] = '\n';
        
        /* Broadcast will skip this client since fd is now -1 */
        broadcast_message(clients, max_clients, msg, offset);
    }
}

/**
 * @brief Process a complete message from a client
 */
void process_message(client_t *cli, ssize_t msg_len) {
    uint8_t msg_type = (uint8_t)cli->buf[0];
    
    if (msg_type == MSG_TYPE_USERNAME && !cli->has_username) {
        /* Username registration */
        if (msg_len > 2) {
            uint8_t username_len = (uint8_t)cli->buf[1];
            if (username_len > 0 && username_len < MAX_USERNAME_LEN && msg_len >= 2 + username_len) {
                memcpy(cli->username, cli->buf + 2, username_len);
                cli->username[username_len] = '\0';
                cli->has_username = 1;
                
                log_message(LOG_INFO, "Client registered username: %s", cli->username);
                broadcast_join(clients, max_clients, cli);
            }
        }
    } else if (msg_type == MSG_TYPE_CHAT && cli->has_username) {
        /* Chat message - broadcast to all clients */
        ssize_t content_len = msg_len - 1; /* Exclude type byte */
        
        char broadcast_msg[BUF_SIZE + 8];
        int offset = 0;
        
        broadcast_msg[offset++] = MSG_TYPE_CHAT;
        memcpy(broadcast_msg + offset, &cli->addr.sin_addr.s_addr, 4);
        offset += 4;
        memcpy(broadcast_msg + offset, &cli->addr.sin_port, 2);
        offset += 2;
        
        uint8_t username_len = (uint8_t)strlen(cli->username);
        broadcast_msg[offset++] = username_len;
        memcpy(broadcast_msg + offset, cli->username, username_len);
        offset += username_len;
        
        if (content_len > 0) {
            memcpy(broadcast_msg + offset, cli->buf + 1, content_len);
            offset += content_len;
        }
        
        broadcast_message(clients, max_clients, broadcast_msg, offset);
        
        log_message(LOG_DEBUG, "Broadcast message from %s", cli->username);
    } else if (msg_type == MSG_TYPE_DISCONNECT) {
        /* Client requested disconnect */
        remove_client(cli, 0);
    }
}

/**
 * @brief Accept a new client connection
 */
void accept_client(int server_fd) {
    struct sockaddr_in remote_addr;
    socklen_t addrlen = sizeof(remote_addr);
    
    int client_fd = accept(server_fd, (struct sockaddr *)&remote_addr, &addrlen);
    if (client_fd == -1) {
        log_message(LOG_ERROR, "Failed to accept client: %s", strerror(errno));
        return;
    }
    
    if (set_nonblocking(client_fd) == -1) {
        log_message(LOG_ERROR, "Failed to set non-blocking: %s", strerror(errno));
        close(client_fd);
        return;
    }
    
    /* Find available slot */
    int added = 0;
    for (int j = 0; j < max_clients; j++) {
        if (clients[j].fd == -1) {
            clients[j].fd = client_fd;
            clients[j].len = 0;
            clients[j].addr = remote_addr;
            clients[j].has_username = 0;
            
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &remote_addr.sin_addr, ip_str, sizeof(ip_str));
            log_message(LOG_INFO, "New client connected from %s:%d (slot %d)", 
                       ip_str, ntohs(remote_addr.sin_port), j);
            
            added = 1;
            break;
        }
    }
    
    if (!added) {
        log_message(LOG_WARN, "Server full, rejecting client");
        close(client_fd);
    }
}

/**
 * @brief Handle data from a connected client
 */
void handle_client_data(client_t *cli) {
    /* Double-check client is still valid */
    if (cli->fd == -1) {
        return;
    }
    
    ssize_t num_read = read(cli->fd, cli->buf + cli->len, BUF_SIZE - cli->len);
    
    if (num_read > 0) {
        cli->len += num_read;
        
        /* Process all complete messages in buffer */
        while (cli->fd != -1) {  /* Check fd is still valid */
            char *newline_ptr = memchr(cli->buf, '\n', cli->len);
            if (newline_ptr == NULL) {
                /* No complete message yet */
                if (cli->len == BUF_SIZE) {
                    /* Buffer full without newline - invalid message */
                    log_message(LOG_WARN, "Buffer overflow, disconnecting client");
                    remove_client(cli, 0);
                }
                break;
            }
            
            ssize_t msg_len = (newline_ptr - cli->buf) + 1;
            process_message(cli, msg_len);
            
            /* Check if client was disconnected during processing */
            if (cli->fd == -1) {
                break;
            }
            
            /* Remove processed message from buffer */
            int remaining = cli->len - msg_len;
            if (remaining > 0) {
                memmove(cli->buf, cli->buf + msg_len, remaining);
            }
            cli->len = remaining;
        }
    } else if (num_read == 0) {
        /* Connection closed */
        remove_client(cli, 0);
    } else {
        /* Error occurred */
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_message(LOG_ERROR, "Read error: %s", strerror(errno));
            remove_client(cli, 0);
        }
    }
}

/**
 * @brief Main server loop
 */
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <port> <max_clients>\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    int port = atoi(argv[1]);
    max_clients = atoi(argv[2]);
    
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port number\n");
        return EXIT_FAILURE;
    }
    
    if (max_clients <= 0 || max_clients > 1024) {
        fprintf(stderr, "Invalid max_clients (must be 1-1024)\n");
        return EXIT_FAILURE;
    }
    
    /* Initialize logging */
    log_init(NULL, LOG_INFO);
    log_message(LOG_INFO, "Starting TCP Group Chat Server on port %d", port);
    log_message(LOG_INFO, "Max clients: %d", max_clients);
    
    /* Setup signal handling */
    signal(SIGINT, handle_shutdown);
    signal(SIGTERM, handle_shutdown);
    signal(SIGPIPE, SIG_IGN); /* Ignore SIGPIPE when writing to closed sockets */
    
    /* Allocate client array */
    clients = calloc(max_clients, sizeof(client_t));
    if (!clients) {
        handle_error("calloc");
    }
    
    for (int i = 0; i < max_clients; i++) {
        clients[i].fd = -1;
        clients[i].len = 0;
        clients[i].has_username = 0;
    }
    
    /* Create server socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        handle_error("socket");
    }
    
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        handle_error("setsockopt");
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        handle_error("bind");
    }
    
    if (listen(server_fd, LISTEN_BACKLOG) == -1) {
        handle_error("listen");
    }
    
    log_message(LOG_INFO, "Server listening on port %d", port);
    
    /* Allocate poll array (server socket + client sockets) */
    struct pollfd *poll_fds = calloc(max_clients + 1, sizeof(struct pollfd));
    if (!poll_fds) {
        handle_error("calloc poll_fds");
    }
    
    /* Initialize poll array */
    poll_fds[0].fd = server_fd;
    poll_fds[0].events = POLLIN;
    
    for (int i = 0; i < max_clients; i++) {
        poll_fds[i + 1].fd = -1;
        poll_fds[i + 1].events = POLLIN;
    }
    
    /* Main event loop */
    while (server_running) {
        /* Update poll array with current client sockets */
        for (int i = 0; i < max_clients; i++) {
            poll_fds[i + 1].fd = clients[i].fd;
        }
        
        int num_ready = poll(poll_fds, max_clients + 1, 1000);
        
        if (num_ready == -1) {
            if (errno == EINTR) {
                continue;
            }
            handle_error("poll");
        }
        
        if (num_ready == 0) {
            /* Timeout, continue loop */
            continue;
        }
        
        /* Check server socket for new connections */
        if (poll_fds[0].revents & POLLIN) {
            accept_client(server_fd);
        }
        
        /* Check client sockets for data */
        for (int i = 0; i < max_clients; i++) {
            if (poll_fds[i + 1].revents & (POLLIN | POLLHUP | POLLERR)) {
                if (clients[i].fd != -1) {
                    handle_client_data(&clients[i]);
                }
            }
        }
    }
    
    /* Cleanup */
    log_message(LOG_INFO, "Shutting down server");
    
    for (int i = 0; i < max_clients; i++) {
        if (clients[i].fd != -1) {
            close(clients[i].fd);
        }
    }
    
    close(server_fd);
    free(clients);
    free(poll_fds);
    log_close();
    
    return EXIT_SUCCESS;
}
