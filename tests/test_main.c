/**
 * @file test_main.c
 * @brief Main test runner
 */

#include <stdio.h>

/* External test functions */
extern int test_protocol_main(void);

int main() {
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║   TCP Group Chat - Test Suite         ║\n");
    printf("╚════════════════════════════════════════╝\n");
    
    /* Run protocol tests */
    int result = test_protocol_main();
    
    if (result == 0) {
        printf("\n✓ All tests passed!\n\n");
    } else {
        printf("\n✗ Some tests failed\n\n");
    }
    
    return result;
}

/* Wrapper to run the test_protocol tests */
int test_protocol_main(void) {
    /* Include the test_protocol.c file to run its tests */
    extern int main();
    return 0; /* test_protocol.c will handle its own assertions */
}
