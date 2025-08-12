#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "pstat.h"

#define MAX_TICKETS 10000

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
  p->tickets = 0;
  p->pass = 0;
  p->ticks = 0;
}

// 0으로 초기화되어도 `get_runnable_min_pass_proc`에서 적절한 값으로 덮어씌워진다.
// 프로그램 초기에 덮어씌워지지 않은 값이 읽혀서 사용되더라도 프로그램 초기에는
// 실제 최소 pass 값이 0이므로 큰 이슈가 없을 것으로 예상?
// _Atomic을 사용하여 멀티코어 환경에서 race condition 방지
static _Atomic uint64 cached_min_pass = 0;

// 주의) proc에 대한 락을 잡은 상태에서 호출하면 중복 acquire로 패닉 발생할 수 있음
static struct proc*
get_runnable_min_pass_proc_locked(void) {
  uint64 min_pass = ((uint64)~0ULL); // UINT64_MAX
  struct proc* min_pass_proc = 0;
  for(int i = 0; i < NPROC; i++) {
    acquire(&proc[i].lock);

    if(proc[i].state == RUNNABLE && proc[i].pass < min_pass) {
      // 이전 min_pass_proc이 존재한다면 락을 해제
      if(min_pass_proc != 0) { release(&min_pass_proc->lock); }

      min_pass = proc[i].pass;
      min_pass_proc = &proc[i];

      // 새 min_pass_proc에 대한 락을 유지한채로, 다음 프로세스 검사
      continue;
    }

    release(&proc[i].lock);
  }
  // 최소 pass 값을 가진 프로세스를 락을 잡은 상태로 반환
  return min_pass_proc;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  p->cwd = namei("/");

  p->state = RUNNABLE;

  // For Stride Scheduling
  p->tickets = 1;
  p->pass = 0;
  p->ticks = 0;

  release(&p->lock);
}

// Shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
shrinkproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  if(n > p->sz)
    return -1;

  sz = p->sz;
  sz = uvmdealloc(p->pagetable, sz, sz - n);
  p->sz = sz;
  return 0;
}

// 주어진 범위의 페이지를 read-only로 설정
// 페이지가 정렬되어 있지 않거나, 할당되지 않은 경우 -1을 반환
// 성공 시 0을 반환
int
set_pages_readonly(uint64 va, uint64 npages) {
  uint64 a, pa0;
  pte_t *pte;
  struct proc *p = myproc();

  if(npages == 0)
    return -1;

  if((va % PGSIZE) != 0)
    return -1;

  if(va < USERVASTART || va >= p->sz || va+sizeof(uint64) > p->sz)
    return -1;

  // 페이지마다 PTE_R은 1, PTE_W는 0으로 설정. 나머지는 변경하지 않음
  for(a = va; a < va + npages * PGSIZE; a += PGSIZE) {
    // a는 이미 PGSIZE로 정렬되어 있으므로 PGROUNDDOWN(a)와 동일
    pa0 = walkaddr(p->pagetable, a);
    if(pa0 == 0) {
      // TODO: vmfault()의 `read` 인자는 함수 내부에서 사용되지 않아서 어떤 값을 넣을지 확신이 없음. 메모리 값을 수정하는 것은 아니므로 read == 1로 일단 설정함.
      if(vmfault(p->pagetable, a, 0) == 0) {
        return -1;
      }
    }
    pte = walk(p->pagetable, a, 0);
    if(pte == 0)
      return -1;
    // 페이지 엔트리 유효 비트 체크
    if((*pte & PTE_V) == 0)
      return -1;
    // 페이지 엔트리가 유저 페이지 엔트리인지 체크
    if((*pte & PTE_U) == 0)
      return -1;
    *pte = (*pte & ~PTE_W) | PTE_R; // PTE_W는 0으로 설정, PTE_R은 1로 설정
  }
  
  // TLB 무효화
  sfence_vma();
  
  return 0;
}

// 주어진 범위의 페이지를 read-write로 설정
// 페이지가 정렬되어 있지 않거나, 할당되지 않은 경우 -1을 반환
// 성공 시 0을 반환
int
set_pages_readwrite(uint64 va, uint64 npages) {
  uint64 a, pa0;
  pte_t *pte;
  struct proc *p = myproc();

  if(npages == 0)
    return -1;

  if((va % PGSIZE) != 0)
    return -1;

  if(va < USERVASTART || va >= p->sz || va+sizeof(uint64) > p->sz)
    return -1;

  for(a = va; a < va + npages * PGSIZE; a += PGSIZE) {
    // a는 이미 PGSIZE로 정렬되어 있으므로 PGROUNDDOWN(a)와 동일
    pa0 = walkaddr(p->pagetable, a);
    if(pa0 == 0) {
      // TODO: vmfault()의 `read` 인자는 함수 내부에서 사용되지 않아서 어떤 값을 넣을지 확신이 없음. 메모리 값을 수정하는 것은 아니므로 read == 1로 일단 설정함.
      if(vmfault(p->pagetable, a, 0) == 0) {
        return -1;
      }
    }
    pte = walk(p->pagetable, a, 0);
    if(pte == 0)
      return -1;
    if((*pte & PTE_V) == 0)
      return -1;
    if((*pte & PTE_U) == 0)
      return -1;
    // PTE_R, PTE_W 비트 모두 1로 설정
    *pte = *pte | PTE_R | PTE_W;
  }
  
  // TLB 무효화
  sfence_vma();
  
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  // For Stride Scheduling
  // The child process inherits the number of tickets from the parent
  np->tickets = p->tickets;
  // 자식 프로세스의 pass값은 가장 작은 pass 값으로 설정.
  // 자식 프로세스가 부모 프로세스와 같은 pass 값을 갖는 경우 스케쥴링에서 부모와 동일한 선에서 경쟁하게 된다.
  // 대신 새 자식 프로세스의 pass 값을 글로벌 min pass 값으로 설정하게 되면 최대 한 stride만큼 부모보다 유리한 선에서 스케쥴링
  // (부모 프로세스 pass - 글로벌 min pass)값이 부모의 stride보다 크다면 부모는 pass값이 최소가 아닐 때 스케쥴링된 것이므로 현재 정책과 모순
  np->pass = cached_min_pass;
  np->ticks = 0;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting. Then turn them back off
    // to avoid a possible race between an interrupt
    // and wfi.
    intr_on();
    intr_off();


    p = get_runnable_min_pass_proc_locked();

    if(p == 0) {
      // nothing to run; stop running on this core until an interrupt.
      asm volatile("wfi");
      continue;
    }

    cached_min_pass = p->pass;

    // Switch to chosen process.  It is the process's job
    // to release its lock and then reacquire it
    // before jumping back to us.
    p->state = RUNNING;
    c->proc = p;
    
    swtch(&c->context, &p->context);

    p->pass += MAX_TICKETS / p->tickets;
    p->ticks++;

    // Process is done running for now.
    // It should have changed its p->state before coming back.
    c->proc = 0;
    release(&p->lock);
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched RUNNING");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // Still holding p->lock from scheduler.
  release(&p->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();

    // We can invoke exec() now that file system is initialized.
    // Put the return value (argc) of exec into a0.
    p->trapframe->a0 = exec("/init", (char *[]){ "/init", 0 });
    if (p->trapframe->a0 == -1) {
      panic("exec");
    }
  }

  // return to user space, mimicing usertrap()'s return.
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// Sleep on wait channel chan, releasing condition lock lk.
// Re-acquires lk when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on wait channel chan.
// Caller should hold the condition lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

int
getpinfo(uint64 addr)
{
  struct pstat pstat;
  struct proc *p;
  int i = 0;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state != UNUSED) {
      pstat.inuse[i] = 1;
      pstat.tickets[i] = p->tickets;
      pstat.pid[i] = p->pid;
      pstat.ticks[i] = p->ticks;
    } else {
      pstat.inuse[i] = 0;
      pstat.tickets[i] = 0;
      pstat.pid[i] = 0;
      pstat.ticks[i] = 0;
    }
    release(&p->lock);
    i++;
  }

  // Copy the data to user space
  struct proc *curr = myproc();
  if(copyout(curr->pagetable, addr, (char*)&pstat, sizeof(pstat)) < 0) {
    return -1;
  }
  return 0;
}
