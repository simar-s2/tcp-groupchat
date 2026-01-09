/**
 * @file protocol.h
 * @brief TCP Group Chat Protocol Definitions
 * 
 * This file defines the message protocol used for communication between
 * the server and clients in the group chat application.
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <sys/types.h>

/* Protocol Constants */
#define BUF_SIZE 1024
#define MAX_USERNAME_LEN 32
#define MAX_MESSAGE_LEN 512

/* Message Types */
#define MSG_TYPE_CHAT 0      /* Regular chat message */
#define MSG_TYPE_DISCONNECT 1 /* Client disconnect notification */
#define MSG_TYPE_JOIN 2       /* Client join notification */
#define MSG_TYPE_USERNAME 3   /* Username registration */

/* Protocol Version */
#define PROTOCOL_VERSION 1

/**
 * @brief Message header structure
 * 
 * All messages begin with this header to identify the message type
 * and protocol version.
 */
typedef struct {
    uint8_t version;    /* Protocol version */
    uint8_t type;       /* Message type (MSG_TYPE_*) */
    uint16_t length;    /* Total message length including header */
} __attribute__((packed)) msg_header_t;

/**
 * @brief Chat message structure
 * 
 * Format: [header][ip][port][username_len][username][message]\n
 */
typedef struct {
    msg_header_t header;
    uint32_t sender_ip;
    uint16_t sender_port;
    uint8_t username_len;
    char username[MAX_USERNAME_LEN];
    char message[MAX_MESSAGE_LEN];
} chat_msg_t;

/**
 * @brief Join notification structure
 * 
 * Sent to all clients when a new client joins the chat.
 */
typedef struct {
    msg_header_t header;
    uint32_t client_ip;
    uint16_t client_port;
    uint8_t username_len;
    char username[MAX_USERNAME_LEN];
} join_msg_t;

/**
 * @brief Helper function to create a message header
 */
static inline void init_msg_header(msg_header_t *header, uint8_t type, uint16_t length) {
    header->version = PROTOCOL_VERSION;
    header->type = type;
    header->length = length;
}

#endif /* PROTOCOL_H */
