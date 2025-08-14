#include "kernel/types.h"
#include "user/user.h"

int
main(void) {
    char *p = malloc(4096);
    if (p == 0) {
        fprintf(2, "umalloc failed\n");
        return 1;
    }
    mprotect(p, 1);
    int pid = fork();
    if (pid < -1) {
        fprintf(2, "fork failed\n");
        return 1;
    }
    if (pid == 0) {
        printf("Child process write to read-only memory and should fail\n");
        *p = 'A';
        *(p+1) = 0;
        printf("This line should not be reached in child process\n");
        return 0;
    }
    else {
        wait(0);
        munprotect(p, 1);
        printf("Parent process write to read-write memory\n");
        *p = 'A';
        *(p+1) = 0;
        printf("Successfully wrote to read-write memory\n");
        return 0;
    }
}
