#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char * argv[]) {
    uint64 x1 = getreadcount();

    int rc = fork();

    uint64 total = 0;
    int i;
    for (i = 0; i < 100000; i++) {
        char buf[100];
        (void) read(4, buf, 1);
    }

    if (rc > 0) {
        (void) wait(0);
        uint64 x2 = getreadcount();
        total += (x2 - x1);
        printf("XV6_TEST_OUTPUT %lu\n", total);
    }
    exit(0);
}