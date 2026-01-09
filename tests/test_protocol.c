/**
 * @file test_protocol.c
 * @brief Unit tests for protocol and common functions
 */

#include "common.h"
#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Test bytes_to_hex function */
void test_bytes_to_hex() {
    printf("Testing bytes_to_hex... ");
    
    uint8_t bytes[] = {0xDE, 0xAD, 0xBE, 0xEF};
    char hex[9];
    
    int result = bytes_to_hex(bytes, 4, hex, sizeof(hex));
    assert(result == 0);
    assert(strcmp(hex, "DEADBEEF") == 0);
    
    /* Test null pointer */
    result = bytes_to_hex(NULL, 4, hex, sizeof(hex));
    assert(result == -1);
    
    /* Test buffer too small */
    char small[5];
    result = bytes_to_hex(bytes, 4, small, sizeof(small));
    assert(result == -1);
    
    printf("PASSED\n");
}

/* Test message header initialization */
void test_init_msg_header() {
    printf("Testing init_msg_header... ");
    
    msg_header_t header;
    init_msg_header(&header, MSG_TYPE_CHAT, 100);
    
    assert(header.version == PROTOCOL_VERSION);
    assert(header.type == MSG_TYPE_CHAT);
    assert(header.length == 100);
    
    printf("PASSED\n");
}

/* Test protocol constants */
void test_protocol_constants() {
    printf("Testing protocol constants... ");
    
    assert(MSG_TYPE_CHAT == 0);
    assert(MSG_TYPE_DISCONNECT == 1);
    assert(MSG_TYPE_JOIN == 2);
    assert(MSG_TYPE_USERNAME == 3);
    
    assert(MAX_USERNAME_LEN == 32);
    assert(MAX_MESSAGE_LEN == 512);
    assert(BUF_SIZE == 1024);
    
    printf("PASSED\n");
}

/* Run all tests */
int main() {
    printf("\n=== Running Protocol Tests ===\n\n");
    
    test_bytes_to_hex();
    test_init_msg_header();
    test_protocol_constants();
    
    printf("\n=== All Tests Passed ===\n\n");
    return 0;
}
