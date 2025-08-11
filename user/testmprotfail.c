#include "kernel/types.h"
#include "user/user.h"

// mprotect 시스템 콜 실패 케이스 테스트
int
main(void) {
    printf("Testing mprotect failure cases...\n");
    
    // Test 1: NULL pointer
    printf("Test 1: NULL pointer - ");
    if (mprotect(0, 1) == -1) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
    }
    
    // Test 2: Unaligned address
    printf("Test 2: Unaligned address - ");
    char *p = malloc(4096);
    if (p != 0) {
        // Try with address that's not page-aligned
        if (mprotect(p + 1, 1) == -1) {
            printf("PASS\n");
        } else {
            printf("FAIL\n");
        }
        free(p);
    } else {
        printf("SKIP (malloc failed)\n");
    }
    
    // Test 3: Invalid high address (beyond process size)
    printf("Test 3: Invalid high address - ");
    char *high_addr = (char*)0x80000000;  // Very high address likely outside process space
    if (mprotect(high_addr, 1) == -1) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
    }

    // Test 4: len is 0
    printf("Test 4: len is 0 - ");
    if (mprotect(p, 0) == -1) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
    }
}