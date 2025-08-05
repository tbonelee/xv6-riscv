#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char * argv[]) {
    uint64 x1 = getreadcount();
    uint64 x2 = getreadcount();
    char buf[100];
    (void) read(4, buf, 1);
    uint64 x3 = getreadcount();
    int i;
    for (i = 0; i < 1000; i++) {
        (void) read(4, buf, 1);
    }
    uint64 x4 = getreadcount();
    printf("XV6_TEST_OUTPUT %lu %lu %lu\n", x2-x1, x3-x2, x4-x3);
    exit(0);
}