#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

int allotment[2] = {20, 40};     // Array to check if process has used up its allotment.
int quantum[3] = {5, 10, 20};    // Array to check process's ticks with its time quantum.
int stride_tickets = 0;          // Tickets allocated for stride scheduling. Initially 0.

// Heap for stride scheduling.
int heap[NPROC + 1];
int heap_count = 0;

void
swap(int *a, int *b)
{
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

void
push(int x)
{
    heap[++heap_count] = x;

    int child = heap_count;
    int parent = child / 2;

    while( (child > 1) && (heap[child] < heap[parent]) ) {
        swap(&heap[child], &heap[parent]);
        child = parent;
        parent = child / 2;
    }
}

int
pop(void)
{
    int min_pass = heap[1];

    swap(&heap[1], &heap[heap_count]);
    heap_count -= 1;

    int parent = 1;
    int child = parent * 2;

    if( (child + 1) <= heap_count ) {
        child = (heap[child] < heap[child + 1]) ? child : child + 1;
    }

    while( (child <= heap_count) && (heap[child] < heap[parent]) ) {
        swap(&heap[child], &heap[parent]);
        parent = child;
        child = parent * 2;

        if( (child + 1) <= heap_count )
            child = (heap[child] < heap[child + 1]) ? child : child + 1;
    }

    return min_pass;
}

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->level = 0;         // Process is initially given the highest priority.
  p->ticks = 0;         // Initialize ticks.
  p->runtime = 0;       // Initialize runtime.
  p->pass_value = -1;   // Process initially enters MLFQ. (If pass_value > 0, process is in stride scheduling.)
  p->stride = 0;        // Initialize stride.
  p->portion = 0;       // Initialize number of tickets allocated.
  p->tid = 0;           // Initialize thread ID. (0 if manager process)
  p->nexttid = 1;       // Initialize nexttid(next tid to assign).
  p->stack_count = 0;   // Initialize number of elements in the stack to 0.

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();
  struct proc *mgr;

  acquire(&ptable.lock);    // Acquire lock to prevent race condition.

  // Set mgr as manager process.
  if(curproc->tid > 0)
      mgr = curproc->manager;
  else if(curproc->tid == 0)
      mgr = curproc;
  else {
      release(&ptable.lock);
      return -1;
  }

  // Set sz as manager process's sz.
  sz = mgr->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0) {
      release(&ptable.lock);
      return -1;
    }
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0) {
      release(&ptable.lock);
      return -1;
    }
  }
  // Update manager process's sz to the new sz.
  mgr->sz = sz;

  release(&ptable.lock);

  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  uint sz;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  // When copying the page table,
  // size should be set to the manager process's memory size.
  if(curproc->tid > 0)
      sz = curproc->manager->sz;
  else if(curproc->tid == 0)
      sz = curproc->sz;
  else
      goto bad;

  if((np->pgdir = copyuvm(curproc->pgdir, sz)) == 0)
      goto bad;

  np->sz = sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;

bad:
  kfree(np->kstack);
  np->kstack = 0;
  np->state = UNUSED;
  return -1;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p, *mgr, *lwp;
  int fd;
  uint sz;

  if(curproc == initproc)
    panic("init exiting");

  // If exiting process is under stride scheduling, return its portion.
  if( (curproc->pid > 0) && (curproc->pass_value != -1) ) {
      pop();
      stride_tickets -= curproc->portion;
  }

  // Set manager and lwp depending on curproc's status.
  if(curproc->tid > 0) {
      // If curproc is a LWP, mgr is set to its manager and lwp is set to curproc.
      mgr = curproc->manager;
      lwp = curproc;
  }
  else if(curproc->tid == 0) {
      // If curproc is a process, mgr is set to curproc and lwp is set to null.
      mgr = curproc;
      lwp = 0;
  }
  else
      return;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      // If p's manager is mgr and if p is not lwp, cleanup its resources.
      if( (p->manager != mgr) || (p == lwp) )
          continue;

      // If p is not ZOMBIE, set killed to 1. Wake up if it is asleep.
      if(p->state != ZOMBIE) {
          p->killed = 1;
          if(p->state == SLEEPING)
              p->state = RUNNABLE;
      }
      else {
          // If p is a ZOMBIE, cleanup its resources.
          kfree(p->kstack);
          p->kstack = 0;
          p->pid = 0;
          p->parent = 0;
          p->name[0] = 0;
          p->killed = 0;
          p->state = UNUSED;

          p->level = 0;
          p->ticks = 0;
          p->runtime = 0;
          p->pass_value = -1;
          p->stride = 0;
          p->portion = 0;
          p->tid = 0;
          p->manager = 0;
          p->nexttid = 1;
          p->stack_count = 0;

          // Clear page allocated to the LWP and push memory address to stack(for future use).
          if((sz = deallocuvm(p->pgdir, p->sz, p->sz - 2*PGSIZE)) == 0) {
              release(&ptable.lock);
              return;
          }
          mgr->stack[(mgr->stack_count)++] = sz;
          mgr->nexttid--;
      }
  }
  release(&ptable.lock);

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  if(curproc->tid == 0)
      wakeup1(curproc->parent);
  else if(curproc->tid > 0) {
      // Manager process must be terminated as well.
      curproc->manager->killed = 1;
      wakeup1(curproc->manager);
  }
  else
      return;

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
    struct proc *p;
    struct cpu *c = mycpu();
    c->proc = 0;

    struct proc *q, *mgr;
    int found;                  // Check if there is a higher level process which is runnable.
    int sched_ticks = 0;        // Internal tick used for scheduler.
    int count = 0;              // Check count to ensure ratio of stride and MLFQ scheduling.

    for(;;){
        // Enable interrupts on this processor.
        sti();

        // Loop over process table looking for process to run.
        acquire(&ptable.lock);
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
            if(p->state != RUNNABLE)
                continue;

            // Execute process every 1000 ticks to avoid infinite loop.
            if(++sched_ticks > 1000) {
                sched_ticks = 1;
                goto execution;
            }

            if(++count > 100)       // Count up to 100, then go back to 1.
                count = 1;          // This is because there are total 100 tickets to distribute.

            // Use stride scheduling while count is smaller than tickets allocated to stride scheduling.
            if(count <= stride_tickets) {
                // Set mgr as the manager process.
                if(p->tid > 0)
                    mgr = p->manager;
                else
                    mgr = p;
                // Execute process if its pass_value == heap[1], meaning it has the minimum pass_value.
                // Pop the value from the heap, then push the newly updated pass_value.
                if( (heap_count > 0) && (mgr->pass_value == heap[1]) ) {
                    pop();
                    mgr->pass_value += mgr->stride;
                    push(mgr->pass_value);
                    goto execution;
                }
                // If the process is not under stride scheduling or does not have the minimum pass_value,
                // decrement count and continue searching.
                else {
                    count--;
                    continue;
                }
            }

            // If count is above stride_tickets, search for mlfq processes.
            // mlfq processes have -1 as their pass_value.
            if(p->pass_value != -1) {
                count--;
                continue;
            }

            // If process's level is 0, it is executed. Otherwise, the scheduler searches for higher level processes.
            if(p->level > 0) {
                found = 0;
                for(q = ptable.proc; q < &ptable.proc[NPROC]; q++) {
                    if( (q->state == RUNNABLE) && (q->pass_value == -1) && (q->level < p->level) ) {
                        found = 1;
                        break;
                    }
                }
                // If a higher level mlfq process is found, the scheduler decrements the count and continues searching.
                if(found == 1) {
                    count--;
                    continue;
                }
            }

execution:
            // Switch to chosen process.  It is the process's job
            // to release ptable.lock and then reacquire it
            // before jumping back to us.
            c->proc = p;
            switchuvm(p);
            p->state = RUNNING;

            swtch(&(c->scheduler), p->context);
            switchkvm();

            // Process is done running for now.
            // It should have changed its p->state before coming back.
            c->proc = 0;
        }
        release(&ptable.lock);
    }
}

void
priority_boost(void)
{
    struct proc *p;

    // Loop over ptable and change every process's level to 0.
    // Also initialize ticks and runtime to 0.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        p->level = 0;
        p->ticks = 0;
        p->runtime = 0;
    }
    release(&ptable.lock);
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// Request for CPU time under stride scheduling.
int
set_cpu_share(int percent)
{
    if(percent <= 0)    // Percent cannot be less than or equal to 0.
        return -1;

    // Only allocate share if total tickets allocated to stride scheduling does not exceed 80.
    if( (percent + stride_tickets) <= 80 ) {
        stride_tickets += percent;
        if(heap_count == 0)         // Push 0 if heap is empty.
            push(0);
        else                        // else, push minimum pass value to heap.
            push(heap[1]);
        myproc()->pass_value = heap[1];         // Process is given the minimum pass value as its pass value.
        myproc()->stride = 1000 / percent;      // Stride is calculated as (1000 / allocated tickets).
        myproc()->portion = percent;            // Save the number of tickets allocated.
        return 0;
    }
    // If total allocated tickets exceed 80, return -1.
    else
        return -1;
}

int
thread_create(thread_t *thread, void *(*start_routine)(void *), void *arg)
{
  int i;
  struct proc *np;
  struct proc *curproc = myproc();
  uint sp, sz, ustack[2];

  // Allocate light-weight process.
  if((np = allocproc()) == 0){
    return -1;
  }
 
  if(curproc->tid > 0)       // If curproc is a LWP, change to its manager.
      curproc = curproc->manager;
  else if(curproc->tid < 0)
      goto bad;

  // Set the manager of new LWP as curproc and assign a new tid to the new LWP.
  np->manager = curproc;
  np->tid = (curproc->nexttid)++;

  *np->tf = *curproc->tf;       // Copy trap frame.
  np->pgdir = curproc->pgdir;   // Share page table.

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  // Assign tid to the thread id.
  *thread = np->tid;

  // If the manager process's stack is empty, there is no empty space in the memory.
  // Thus, increase the memory size of the manager process.
  if(curproc->stack_count == 0) {
      sz = curproc->sz;                     // Set starting memory address for the LWP as the size of the manager process.
      curproc->sz += 2*PGSIZE;              // Increase manager process memory by 2*PGSIZE.
  }
  else if(curproc->stack_count > 0)         // If it is not empty, there is empty space in the memory.
      sz = curproc->stack[--(curproc->stack_count)];   // Pop the memory address from the stack.
  else
      goto bad;

  // Allocate two pages to the LWP. The first one is the guard page, and the second is the user stack.
  sz = PGROUNDUP(sz);
  if((np->sz = allocuvm(np->pgdir, sz, sz + 2*PGSIZE)) == 0)
      goto bad;
  // Creates inaccessible page beneath the user stack.
  clearpteu(np->pgdir, (char*)(np->sz - 2*PGSIZE));
  sp = np->sz;      // Set stack pointer.
  sp -= 2*4;        // Decrease sp by 2 * 4(two bytes).

  ustack[0] = 0xffffffff;       // Fake return PC.
  ustack[1] = (uint)arg;        // Set arg to user stack.
  // Copy ustack(8 bytes) to the user address sp in the LWP's pgdir.
  if(copyout(np->pgdir, sp, ustack, 2*4) < 0)
      goto bad;

  // Set stack pointer and instruction pointer of the LWP.
  np->tf->esp = sp;                     // Points to the user stack of the LWP.
  np->tf->eip = (uint)start_routine;    // Starting point of the LWP.

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return 0;

bad:
  kfree(np->kstack);
  np->kstack = 0;
  np->state = UNUSED;
  return -1;
}

void
thread_exit(void *retval)
{
  struct proc *curproc = myproc();
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // If curproc is a manager process, return.
  if(curproc->tid == 0) {
      cprintf("Manager process cannot call thread_exit.\n");
      return;
  }

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Save the return value to the LWP.
  curproc->retval = retval;

  // Wake the manager process.
  wakeup1(curproc->manager);

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

int
thread_join(thread_t thread, void **retval)
{
  struct proc *p;
  struct proc *curproc = myproc();
  int havelwp;
  uint sz;

  // Only manager process can call thread_join.
  if(curproc->tid != 0) {
      cprintf("Only manager process can call thread_join\n");
      return -1;
  }
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited LWP.
    havelwp = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if( (p->manager != curproc) || (p->tid != thread) )
        continue;
      havelwp = 1;
      if(p->state == ZOMBIE){
        // Found one.
        *retval = p->retval;    // Save retval of the LWP to the argument in thread_join.

        // Clean up resources.
        kfree(p->kstack);
        p->kstack = 0;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;

        p->level = 0;
        p->ticks = 0;
        p->runtime = 0;
        p->pass_value = -1;
        p->stride = 0;
        p->portion = 0;
        p->tid = 0;
        p->manager = 0;
        p->nexttid = 1;
        p->stack_count = 0;

        // Clear page allocated to the LWP and push memory address to stack(for future use).
        if((sz = deallocuvm(p->pgdir, p->sz, p->sz - 2*PGSIZE)) == 0) {
            release(&ptable.lock);
            return -1;
        }
        curproc->stack[(curproc->stack_count)++] = sz;
        curproc->nexttid--;

        release(&ptable.lock);
        return 0;
      }
    }

    // No point waiting if we don't have any LWP.
    if(!havelwp || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for LWP to exit.
    sleep(curproc, &ptable.lock);
  }
}

void
cleanup_lwp(struct proc *mgr, struct proc *lwp)
{
    // mgr is the manager process of lwp.
    struct proc *p;

    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        // For all LWPs under mgr except lwp, set killed to 1 if it is not a ZOMBIE.
        // Wake up if sleeping.
        if( (p != lwp) && (p->manager == mgr) && (p->state != ZOMBIE) ) {
            p->killed = 1;
            if(p->state == SLEEPING)
                p->state = RUNNABLE;
        }
    }
    release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
