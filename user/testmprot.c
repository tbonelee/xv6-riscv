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

    printf("Attempting to write to read-only memory...\n");
    *p = 'A'; // This should cause a trap due to the read-only protection
    *p = 0;
    printf("Write succeeded. %s\n", p);

    fprintf(2, "This line should not be reached if mprotect worked correctly.\n");
}
