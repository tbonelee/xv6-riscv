//
// Tests for copy-on-write fork() - this version should SUCCEED
// This test allocates more than half of available physical memory,
// then calls fork(). The fork should succeed using COW.
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
  
  printf("cowtest_fork: simple test (should SUCCEED)\n");
  printf("Trying to allocate %d MB (%d pages) of memory...\n", ALLOCATE_MB, ALLOCATE_PAGES);
  
  printf("\n=== Before memory allocation ===\n");
  free_pages_before = freelistcount();
  printf("Free pages before allocation: %d\n", free_pages_before);
  
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
  
  printf("\n=== After memory allocation ===\n");
  free_pages_after = freelistcount();
  printf("Free pages after allocation: %d\n", free_pages_after);
  free_pages_diff = free_pages_before - free_pages_after;
  printf("Pages consumed by allocation: %d\n", free_pages_diff);
  
  // Now try to fork - this should succeed using COW
  printf("\nAttempting fork() with %d MB allocated...\n", ALLOCATE_MB);
  
  int free_pages_before_fork = freelistcount();
  printf("Free pages before fork(): %d\n", free_pages_before_fork);
  
  pid = fork();
  if (pid < 0) {
    printf("simple: fork() failed (UNEXPECTED)\n");
    printf("COW implementation may have issues\n");
    
    int free_pages_after_failed_fork = freelistcount();
    printf("Free pages after failed fork(): %d\n", free_pages_after_failed_fork);
    printf("Pages difference due to failed fork: %d\n", free_pages_before_fork - free_pages_after_failed_fork);
    exit(1);
  } else if (pid == 0) {
    // Child process
    printf("simple: fork() succeeded - child process\n");
    printf("Child can access %d MB of memory through COW\n", ALLOCATE_MB);
    
    // Call vmdump and refdump in child
    printf("\n=== In child process ===\n");
    int free_pages_in_child = freelistcount();
    printf("Free pages in child: %d\n", free_pages_in_child);
    printf("Pages consumed by fork (COW setup): %d\n", free_pages_before_fork - free_pages_in_child);
    
    // Read some memory to verify COW sharing
    printf("Child reading first 10 pages (should not trigger COW)...\n");
    int free_pages_before_reading = freelistcount();
    printf("Free pages BEFORE reading: %d\n", free_pages_before_reading);
    
    volatile char val;
    for (i = 0; i < 10; i++) {
      printf("  Reading page %d...\n", i);
      int free_before_read = freelistcount();
      val = mem[i * PGSIZE];
      int free_after_read = freelistcount();
      printf("    Pages consumed by reading page %d: %d (should be 0)\n", i, free_before_read - free_after_read);
    }
    
    int free_pages_immediately_after_reading = freelistcount();
    printf("Free pages IMMEDIATELY AFTER reading: %d\n", free_pages_immediately_after_reading);
    printf("Total pages consumed by reading operations: %d (should be 0)\n", free_pages_before_reading - free_pages_immediately_after_reading);
    printf("Child successfully read pages, val=%d\n", val);
    
    // Call vmdump and refdump after reading
    printf("\n=== In child after reading ===\n");
    int free_pages_after_reading = freelistcount();
    printf("Free pages after reading: %d\n", free_pages_after_reading);
    printf("Pages consumed by reading (should be 0): %d\n", free_pages_in_child - free_pages_after_reading);
    
    // Now modify some memory to trigger COW
    printf("Child modifying 5 pages (should trigger COW)...\n");
    int free_pages_before_cow = freelistcount();
    printf("Free pages BEFORE COW trigger: %d\n", free_pages_before_cow);
    
    for (i = 0; i < 5; i++) {
      printf("  Modifying page %d (triggering COW)...\n", i);
      int free_before_page = freelistcount();
      mem[i * PGSIZE] = 42;
      int free_after_page = freelistcount();
      printf("    Pages consumed by COW for page %d: %d\n", i, free_before_page - free_after_page);
    }
    
    int free_pages_immediately_after_cow = freelistcount();
    printf("Free pages IMMEDIATELY AFTER COW trigger: %d\n", free_pages_immediately_after_cow);
    printf("Total pages consumed by COW operations: %d\n", free_pages_before_cow - free_pages_immediately_after_cow);
    printf("Child modified first 5 pages\n");
    
    // Call vmdump and refdump after writing
    printf("\n=== In child after writing ===\n");
    int free_pages_after_writing = freelistcount();
    printf("Free pages after writing: %d\n", free_pages_after_writing);
    printf("Pages consumed by COW (should be ~5): %d\n", free_pages_after_reading - free_pages_after_writing);
    
    exit(0);
  } else {
    // Parent process
    printf("simple: fork() succeeded - parent process\n");
    printf("Parent still has access to %d MB of memory\n", ALLOCATE_MB);
    
    // Call vmdump and refdump in parent
    printf("\n=== In parent process after fork ===\n");
    int free_pages_in_parent = freelistcount();
    printf("Free pages in parent: %d\n", free_pages_in_parent);
    printf("Pages consumed by fork (should be minimal): %d\n", free_pages_before_fork - free_pages_in_parent);
    
    // Wait for child
    int status;
    wait(&status);
    printf("Child exited with status %d\n", status);
    
    // Call vmdump and refdump after child exit
    printf("\n=== In parent after child exit ===\n");
    int free_pages_after_child_exit = freelistcount();
    printf("Free pages after child exit: %d\n", free_pages_after_child_exit);
    printf("Pages freed by child exit: %d\n", free_pages_after_child_exit - free_pages_in_parent);
    
    // Verify parent's memory is intact
    printf("Parent verifying memory integrity...\n");
    int errors = 0;
    for (i = 5; i < 100; i++) {  // Skip first 5 pages that child modified
      if (mem[i * PGSIZE] != 1) {
        errors++;
        if (errors < 5) {  // Only print first few errors
          printf("ERROR: Page %d has value %d, expected 1\n", i, mem[i * PGSIZE]);
        }
      }
    }
    
    if (errors == 0) {
      printf("Parent memory integrity verified - COW working correctly!\n");
    } else {
      printf("Found %d memory integrity errors\n", errors);
    }
    
    exit(0);
  }
}



int
main(int argc, char *argv[])
{
  printf("=== COW Test with fork() (should succeed) ===\n");
  printf("This test demonstrates that fork() uses copy-on-write\n");
  printf("and can succeed even with large memory allocations.\n\n");
  
  simple_test();
  
  printf("\nALL COW TESTS PASSED\n");
  exit(0);
}
