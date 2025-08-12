// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange_on_kinit(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct user_physical_page_ref {
  uint32 ref;
  struct spinlock lock;
};

// Total number of physical pages in the system
#define TOTAL_PAGES ((PHYSTOP - KERNBASE) / PGSIZE)

#define PA_TO_INDEX(pa) (((uint64)(pa) - KERNBASE) / PGSIZE)

// Reference count for each physical page
struct user_physical_page_ref user_physical_page_refs[TOTAL_PAGES];

void
physical_page_ref_init()
{
  for (int i = 0; i < TOTAL_PAGES; i++) {
    initlock(&user_physical_page_refs[i].lock, "user_physical_page_ref");
  }
}

// kalloc으로 할당되고 mappages로 맵핑된 페이지가 kfree되는 경우에만 호출되는 함수
uint32
decrement_ref(void *pa)
{
  uint32 ref;
  int idx;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("decrement_ref");

  idx = PA_TO_INDEX(pa);
  acquire(&user_physical_page_refs[idx].lock);
  ref = user_physical_page_refs[idx].ref--;
  release(&user_physical_page_refs[idx].lock);
  return ref;
}

// kalloc으로 할당된 페이지를 맵핑하는 경우에만 호출되는 함수
uint32
increment_ref(void *pa)
{
  uint32 ref;
  int idx;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("increment_ref");

  idx = PA_TO_INDEX(pa);
  acquire(&user_physical_page_refs[idx].lock);
  ref = user_physical_page_refs[idx].ref++;
  release(&user_physical_page_refs[idx].lock);
  return ref;
}

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange_on_kinit(end, (void*)PHYSTOP);
}

// 부팅 초기 kinit 함수에서만 호출되는 함수
// freelist 초기화 및 user_physical_page_refs 초기화
void
freerange_on_kinit(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    kfree(p);
    initlock(&user_physical_page_refs[PA_TO_INDEX(p)].lock, "user_physical_page_ref");
    user_physical_page_refs[PA_TO_INDEX(p)].ref = 0;
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
