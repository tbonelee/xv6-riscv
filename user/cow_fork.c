//
// Tests for copy-on-write fork() - this version should FAIL
// This test allocates more than half of available physical memory,
// then calls fork(). The fork should fail due to insufficient memory.
//

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

// 128MB total memory, kernel uses some, let's try to allocate 80MB
// With 4KB pages, that's about 20480 pages
#define ALLOCATE_MB 20
#define PGSIZE 4096
#define ALLOCATE_PAGES (ALLOCATE_MB * 1024 * 1024 / PGSIZE)

void
simple_test()
{
  char *mem;
  int i;
  int pid;
  int free_pages_before, free_pages_after, free_pages_diff;
  
  printf("cowtest_fork: simple test (should FAIL)\n");
  printf("Trying to allocate %d MB (%d pages) of memory...\n", ALLOCATE_MB, ALLOCATE_PAGES);
  
  // Call vmdump and refdump before allocation
  printf("\n=== Before memory allocation ===\n");
  free_pages_before = freelistcount();
  printf("Free pages before allocation: %d\n", free_pages_before);
  vmdump();
  refdump();
  
  // Allocate large amount of memory
  mem = sbrk(ALLOCATE_MB * 1024 * 1024);
  if (mem == (char*)-1) {
    printf("ERROR: sbrk failed to allocate memory\n");
    exit(1);
  }
  
  // Touch all the allocated pages to ensure they're actually allocated
  printf("Touching allocated pages to force allocation...\n");
  for (i = 0; i < ALLOCATE_PAGES; i++) {
    mem[i * PGSIZE] = 1;
  }
  printf("Successfully allocated and touched %d pages\n", ALLOCATE_PAGES);
  
  // Call vmdump and refdump after allocation
  printf("\n=== After memory allocation ===\n");
  free_pages_after = freelistcount();
  printf("Free pages after allocation: %d\n", free_pages_after);
  free_pages_diff = free_pages_before - free_pages_after;
  printf("Pages consumed by allocation: %d\n", free_pages_diff);
  vmdump();
  refdump();
  
  // Now try to fork - this should fail due to insufficient memory
  printf("\nAttempting fork() with %d MB allocated...\n", ALLOCATE_MB);
  
  int free_pages_before_fork = freelistcount();
  printf("Free pages before fork(): %d\n", free_pages_before_fork);
  
  pid = fork();
  if (pid < 0) {
    printf("simple: fork() failed (EXPECTED BEHAVIOR)\n");
    printf("This demonstrates that regular fork() needs to copy all memory\n");
    printf("and fails when there's insufficient physical memory.\n");
    
    int free_pages_after_failed_fork = freelistcount();
    printf("Free pages after failed fork(): %d\n", free_pages_after_failed_fork);
    printf("Pages difference due to failed fork: %d\n", free_pages_before_fork - free_pages_after_failed_fork);
    exit(0);
  } else if (pid == 0) {
    // Child process
    printf("simple: fork() succeeded unexpectedly - child process\n");
    printf("Child has access to %d MB of memory\n", ALLOCATE_MB);
    
    // Call vmdump and refdump in child
    printf("\n=== In child process ===\n");
    int free_pages_in_child = freelistcount();
    printf("Free pages in child: %d\n", free_pages_in_child);
    vmdump();
    refdump();
    
    // Try to modify some memory to verify it's really copied
    for (i = 0; i < 10; i++) {
      mem[i * PGSIZE] = 42;
    }
    printf("Child modified first 10 pages\n");
    
    exit(0);
  } else {
    // Parent process
    printf("simple: fork() succeeded unexpectedly - parent process\n");
    printf("Parent still has access to %d MB of memory\n", ALLOCATE_MB);
    
    // Call vmdump and refdump in parent
    printf("\n=== In parent process after fork ===\n");
    int free_pages_in_parent = freelistcount();
    printf("Free pages in parent: %d\n", free_pages_in_parent);
    printf("Pages consumed by fork: %d\n", free_pages_before_fork - free_pages_in_parent);
    vmdump();
    refdump();
    
    // Wait for child
    int status;
    wait(&status);
    printf("Child exited with status %d\n", status);
    
    int free_pages_after_child_exit = freelistcount();
    printf("Free pages after child exit: %d\n", free_pages_after_child_exit);
    printf("Pages freed by child exit: %d\n", free_pages_after_child_exit - free_pages_in_parent);
    
    exit(0);
  }
}

int
main(int argc, char *argv[])
{
  printf("=== COW Test with fork() (should fail) ===\n");
  printf("This test demonstrates that regular fork() copies all memory\n");
  printf("and can fail when there's insufficient physical memory.\n\n");
  
  simple_test();
  
  printf("ALL TESTS COMPLETED\n");
  exit(0);
}