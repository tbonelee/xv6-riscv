// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "kalloc.h"

void freerange_on_kinit(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

// Total number of physical pages in the system
#define TOTAL_PAGES ((PHYSTOP - KERNBASE) / PGSIZE)

#define PA_TO_INDEX(pa) (((uint64)(pa) - KERNBASE) / PGSIZE)
#define INDEX_TO_PA(idx) (KERNBASE + (idx * PGSIZE))

// Reference count for each physical page
struct user_physical_page_ref user_physical_page_refs[TOTAL_PAGES];

void
physical_page_ref_init()
{
  for (int i = 0; i < TOTAL_PAGES; i++) {
    initlock(&user_physical_page_refs[i].lock, "user_physical_page_ref");
  }
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

// pa가 가리키는 물리 메모리 페이지를 해제하고 프리 리스트에 추가한다.
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

// pa가 가리키는 물리 메모리 페이지의 참조 카운트를 감소시키고,
// 참조 카운트가 0이 되면 해당 페이지를 프리 리스트에 반환한다.
// kalloc()으로 할당받은 페이지를 더 이상 참조하지 않는 경우에 호출된다.
void
decrement_ref(void *pa)
{
  uint32 ref;
  int idx;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("decrement_ref");

  idx = PA_TO_INDEX(pa);
  acquire(&user_physical_page_refs[idx].lock);
  if (user_physical_page_refs[idx].ref == 0) 
    panic("decrement_ref: ref is 0");
  ref = --(user_physical_page_refs[idx].ref);
  release(&user_physical_page_refs[idx].lock);
  
  if (ref == 0)
    kfree(pa);
}

void
decrement_ref_withheld_lock(void *pa)
{
  uint32 ref;
  int idx;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("decrement_ref_withheld_lock");

  idx = PA_TO_INDEX(pa);
  if (user_physical_page_refs[idx].ref == 0) 
    panic("decrement_ref_withheld_lock: ref is 0");
  ref = user_physical_page_refs[idx].ref--;
  
  if (ref == 0)
    kfree(pa);
}

struct user_physical_page_ref *
get_user_physical_page_ref_locked(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("get_user_physical_page_ref_locked");

  int idx = PA_TO_INDEX(pa);
  acquire(&user_physical_page_refs[idx].lock);
  return &user_physical_page_refs[idx];
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
  if(r) {
    kmem.freelist = r->next;
    increment_ref(r);
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

// kmem.freelist를 덤프하는 디버깅 함수
void
dump_freelist(void)
{
  printf("Kmem Free List:\n");
  printf("Address           Next\n");
  printf("---------------   ---------------\n");
  
  acquire(&kmem.lock);
  struct run *r = kmem.freelist;
  int count = 0;
  
  while (r) {
    printf("0x%-13lx   0x%-13lx\n", (uint64)r, (uint64)r->next);
    r = r->next;
    count++;
  }
  
  if (count == 0) {
    printf("(empty)\n");
  }
  
  release(&kmem.lock);
  printf("---------------   ---------------\n");
  printf("Total free pages: %d\n", count);
}

// 사용 중인 물리 페이지들의 정보를 락 없이 출력하는 디버깅 함수
// 연속된 페이지들은 범위로 묶어서 출력
void
print_physical_page_refs(void)
{
  printf("Physical Page Reference Counts:\n");
  printf("Index Range          Physical Address Range               Ref Count\n");
  printf("-------------------  -----------------------------------  ---------\n");
  
  int i = 0;
  int total_pages_with_refs = 0;
  
  while (i < TOTAL_PAGES) {
    uint32 ref = user_physical_page_refs[i].ref;
    
    if (ref > 0) {
      int start_idx = i;
      uint64 start_pa = INDEX_TO_PA(i);
      
      // 같은 참조 카운트를 가진 연속된 페이지들 찾기
      while (i + 1 < TOTAL_PAGES && 
             user_physical_page_refs[i + 1].ref == ref) {
        i++;
      }
      
      int end_idx = i;
      uint64 end_pa = INDEX_TO_PA(i);
      
      // 참조 카운트가 있는 페이지 수 카운트
      total_pages_with_refs += (end_idx - start_idx + 1);
      
      if (start_idx == end_idx) {
        // 단일 페이지
        printf("%-19d  0x%-35lx  %d\n", start_idx, start_pa, ref);
      } else {
        // 페이지 범위
        printf("%-8d-%-10d  0x%lx-0x%-24lx  %d\n", 
               start_idx, end_idx, start_pa, end_pa, ref);
      }
    }
    i++;
  }
  
  printf("-------------------  -----------------------------------  ---------\n");
  printf("Total pages with ref count > 0: %d\n", total_pages_with_refs);
}

// kmem.freelist에 있는 자유 페이지 수를 반환하는 함수
int
count_freelist(void)
{
  int count = 0;
  struct run *r;
  
  acquire(&kmem.lock);
  r = kmem.freelist;
  while (r) {
    count++;
    r = r->next;
  }
  release(&kmem.lock);
  
  return count;
}
