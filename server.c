#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUF_SIZE 1024
#define LISTEN_BACKLOG 32

#define handle_error(msg)                                                      \
  do {                                                                         \
    perror(msg);                                                               \
    exit(EXIT_FAILURE);                                                        \
  } while (0)

typedef struct {
  int fd;
  char buf[BUF_SIZE];
  ssize_t len;
} client_t;

void broadcasting(client_t *clients, int max_clients, char *msg,
                  ssize_t msg_len) {
  for (int i = 0; i < max_clients; i++) {
    if (clients[i].fd != -1) {
      ssize_t total_sent = 0;
      ssize_t bytes_left = msg_len;

      while (total_sent < msg_len) {
        ssize_t sent = send(clients[i].fd, msg + total_sent, bytes_left, 0);
        if (sent == -1) {
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            usleep(50);
            continue;
          }
          break;
        }
        total_sent += sent;
        bytes_left -= sent;
      }
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    printf("Usage: ./server <port> <num_clients>");
    return -1;
  }

  int port = strtol(argv[1], NULL, 10);
  int max_clients = strtol(argv[2], NULL, 10);

  client_t *clients = malloc(max_clients * sizeof(client_t));

  for (int i = 0; i < max_clients; i++) {
    clients[i].fd = -1;
    clients[i].len = 0;
  }

  int type1_count = 0;
  int client_count = 0;

  struct sockaddr_in addr, remote_addr;
  int server_fd, epoll_fd;
  int num_fds;
  socklen_t addrlen = sizeof(struct sockaddr_in);
  struct epoll_event ev, events[max_clients];

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1)
    handle_error("socket");

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  memset(&addr, 0, sizeof(struct sockaddr_in));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(server_fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) ==
      -1)
    handle_error("bind");

  if (listen(server_fd, LISTEN_BACKLOG) == -1)
    handle_error("listen");

  epoll_fd = epoll_create1(0);
  if (epoll_fd == -1)
    handle_error("epoll_create1");

  ev.events = EPOLLIN;
  ev.data.fd = server_fd;

  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev))
    handle_error("epoll_ctl");

  for (;;) {
    num_fds = epoll_wait(epoll_fd, events, max_clients, -1);
    if (num_fds == -1)
      handle_error("epoll_wait");

    for (int i = 0; i < num_fds; i++) {
      if (events[i].data.fd == server_fd) {
        memset(&remote_addr, 0, sizeof(struct sockaddr_in));
        int client_fd =
            accept(server_fd, (struct sockaddr *)&remote_addr, &addrlen);
        if (client_fd == -1)
          handle_error("accept");

        int flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

        int added = 0;
        for (int j = 0; j < max_clients; j++) {
          if (clients[j].fd == -1) {
            clients[j].fd = client_fd;
            clients[j].len = 0;
            client_count++;

            ev.events = EPOLLIN;
            ev.data.ptr = &clients[j];

            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1)
              handle_error("epoll_ctl");

            added = 1;
            break;
          }
        }
        if (!added) {
          close(client_fd);
        }
      } else {
        client_t *cli = (client_t *)events[i].data.ptr;

        ssize_t num_read =
            read(cli->fd, cli->buf + cli->len, BUF_SIZE - cli->len);

        if (num_read > 0) {
          cli->len += num_read;

          while (1) {
            char *newline_ptr = memchr(cli->buf, '\n', cli->len);

            if (newline_ptr == NULL) {
              if (cli->len == BUF_SIZE) {
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, cli->fd, NULL);
                close(cli->fd);
                cli->fd = -1;
                client_count--;
              }
              break;
            }

            ssize_t msg_len = (newline_ptr - cli->buf) + 1;
            uint8_t msg_type = (uint8_t)cli->buf[0];

            if (msg_type == 0) {
              struct sockaddr_in sender_addr;
              socklen_t sender_len = sizeof(sender_addr);
              getpeername(cli->fd, (struct sockaddr *)&sender_addr,
                          &sender_len);

              uint32_t sender_ip = sender_addr.sin_addr.s_addr;
              uint16_t sender_port = sender_addr.sin_port;

              ssize_t content_len = msg_len - 1;

              char broadcast_msg[BUF_SIZE + 8];
              broadcast_msg[0] = 0;

              memcpy(&broadcast_msg[1], &sender_ip, 4);
              memcpy(&broadcast_msg[5], &sender_port, 2);

              if (content_len > 0)
                memcpy(&broadcast_msg[7], &cli->buf[1], content_len);

              broadcasting(clients, max_clients, broadcast_msg,
                           7 + content_len);
            } else if (msg_type == 1) {
              type1_count++;

              if (type1_count == client_count) {
                char type1_msg[2] = {1, '\n'};
                broadcasting(clients, max_clients, type1_msg, 2);

                for (int k = 0; k < max_clients; k++) {
                  if (clients[k].fd != -1)
                    close(clients[k].fd);
                }

                close(server_fd);
                close(epoll_fd);
                free(clients);
                exit(EXIT_SUCCESS);
              }
              break;
            }

            int remaining = cli->len - msg_len;
            if (remaining > 0) {
              memmove(cli->buf, cli->buf + msg_len, remaining);
            }
            cli->len = remaining;
          }
        } else if (num_read == 0) {
          epoll_ctl(epoll_fd, EPOLL_CTL_DEL, cli->fd, NULL);
          close(cli->fd);
          cli->fd = -1;
          client_count--;
        } else {
          if (errno != EAGAIN && errno != EWOULDBLOCK) {
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, cli->fd, NULL);
            close(cli->fd);
            cli->fd = -1;
            client_count--;
          }
        }
      }
    }
  }
  return 0;
}
