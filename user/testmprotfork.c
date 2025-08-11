#include "kernel/types.h"
#include "user/user.h"

int
main(void) {
    // Allocate a page of memory
    char *p = malloc(40960);
    if (p == 0) {
        fprintf(2, "umalloc failed\n");
        return 1;
    }

    // Attempt to change the protection of the page to read-only
    if (mprotect(p, 1) < 0) {
        fprintf(2, "mprotect failed\n");
        return 1;
    }

    // Fork a new process to check if the read-only protections are inherited
    int pid = fork();
    if (pid < 0) {
        fprintf(2, "fork failed\n");
        return 1;
    } else if (pid == 0) {
        // Child process
        printf("Child process attempting to write to read-only memory...\n");
        *p = 'A'; // This should cause a trap due to the read-only protection
        *p = 0;
        fprintf(2, "Child process succeeded in writing to read-only memory, which is unexpected.\n");
    } else {
        // Parent process
        wait(0); // Wait for the child to finish
        printf("Parent process finished.\n");
    }
}