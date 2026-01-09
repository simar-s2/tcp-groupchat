#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUF_SIZE 1024
#define RAND_BYTES 10

#define handle_error(msg)                                                      \
  do {                                                                         \
    perror(msg);                                                               \
    exit(EXIT_FAILURE);                                                        \
  } while (0)

// convert random bytes
int convert(uint8_t *buf, ssize_t buf_size, char *str, ssize_t str_size) {
  if (buf == NULL || str == NULL || buf_size <= 0 ||
      str_size < (buf_size * 2 + 1)) {
    return -1;
  }

  for (int i = 0; i < buf_size; i++)
    sprintf(str + i * 2, "%02X", buf[i]);
  str[buf_size * 2] = '\0';

  return 0;
}

// shared data between threads
typedef struct {
  int socket_fd;
  int num_messages;
  FILE *log_file;
  int should_stop;
} thread_data_t;

ssize_t recv_exact(int sock, uint8_t *buf, size_t len) {
  size_t total = 0;
  while (total < len) {
    ssize_t r = recv(sock, buf + total, len - total, 0);
    if (r <= 0) {
      return r;
    }
    total += (size_t)r;
  }
  return (ssize_t)total;
}

void *sender_thread(void *arg) {
  thread_data_t *data = (thread_data_t *)arg;

  for (int i = 0; i < data->num_messages; i++) {
    uint8_t random_bytes[RAND_BYTES];
    char hex_str[RAND_BYTES * 2 + 1];

    if (getentropy(random_bytes, sizeof(random_bytes) != 0)) {
      handle_error("getentropy");
    }

    char hex_msg[RAND_BYTES * 2 + 1];
    if (convert(random_bytes, RAND_BYTES, hex_str, sizeof(hex_str)) != 0) {
      handle_error("convert");
    }

    uint8_t send_buf[BUF_SIZE];
    send_buf[0] = 0;

    int msg_len = (int)strlen(hex_msg);
    memcpy(&send_buf[1], hex_msg, msg_len);
    send_buf[1 + msg_len] = '\n';

    if (send(data->socket_fd, send_buf, 1 + msg_len + 1, 0) < 0)
      handle_error("send");

    usleep(1000);
  }

  uint8_t end_msg[2];
  end_msg[0] = 1;
  end_msg[1] = '\n';

  if (send(data->socket_fd, end_msg, 2, 0) < 0)
    handle_error("send");

  return NULL;
}

void *receiver_thread(void *arg) {
  thread_data_t *data = (thread_data_t *)arg;

  while (!data->should_stop) {
    uint8_t type;
    ssize_t r = recv_exact(data->socket_fd, &type, 1);
    if (r <= 0)
      break;

    if (type == 0) {
      uint32_t ip_net;
      uint16_t port_net;

      if (recv_exact(data->socket_fd, (uint8_t *)&ip_net, sizeof(ip_net)) <= 0)
        break;

      if (recv_exact(data->socket_fd, (uint8_t *)&port_net, sizeof(port_net)) <=
          0)
        break;

      char msg_buf[BUF_SIZE];
      int idx = 0;
      while (1) {
        uint8_t ch;
        r = recv(data->socket_fd, &ch, 1, 0);
        if (r <= 0) {
          data->should_stop = 1;
          break;
        }
        if (ch == '\n')
          break;
        if (idx < (int)sizeof(msg_buf) - 1) {
          msg_buf[idx++] = (char)ch;
        }
      }

      if (data->should_stop)
        break;

      msg_buf[idx] = '\0';

      struct in_addr addr;
      addr.s_addr = ip_net;
      char ip_str[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));

      uint16_t port_host = ntohs(port_net);

      fprintf(data->log_file, "%-15s%-10u%s\n", ip_str, port_host, msg_buf);
      fflush(data->log_file);
    } else if (type == 1) {
      data->should_stop = 1;
      break;
    } else {
      data->should_stop = 1;
      break;
    }
  }
  return NULL;
}

int main(int argc, char *argv[]) {
  if (argc != 5) {
    fprintf(stderr, "Usage: %s <IP> <port> <#messages> <log file>\n", argv[0]);
    return EXIT_FAILURE;
  }

  char *ip_addr = argv[1];
  int port = strtol(argv[2], NULL, 10);
  int num_messages = strtol(argv[3], NULL, 10);
  char *log_file_path = argv[4];

  FILE *log_file = fopen(log_file_path, "w");
  if (log_file == NULL)
    handle_error("fopen");

  int sfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sfd == -1) {
    fclose(log_file);
    handle_error("socket");
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(struct sockaddr_in));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  if (inet_pton(AF_INET, ip_addr, &addr.sin_addr) <= 0) {
    close(sfd);
    fclose(log_file);
    handle_error("inet_pton");
  }

  if (connect(sfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) ==
      -1) {
    close(sfd);
    fclose(log_file);
    handle_error("connect");
  }

  thread_data_t thread_data = {
      .socket_fd = sfd,
      .num_messages = num_messages,
      .log_file = log_file,
      .should_stop = 0,
  };

  pthread_t sender_tid, receiver_tid;

  if (pthread_create(&receiver_tid, NULL, receiver_thread, &thread_data) != 0) {
    close(sfd);
    fclose(log_file);
    handle_error("pthread create receiver");
  }

  if (pthread_create(&sender_tid, NULL, sender_thread, &thread_data) != 0) {
    close(sfd);
    fclose(log_file);
    handle_error("pthread create sender");
  }

  pthread_join(sender_tid, NULL);
  pthread_join(receiver_tid, NULL);

  close(sfd);
  fclose(log_file);

  exit(EXIT_SUCCESS);
}
