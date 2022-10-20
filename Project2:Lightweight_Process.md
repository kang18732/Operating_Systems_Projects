# Introduction
In this project, I will improve the multi-tasking capabilities of xv6 by implementing an abstraction of LWP.

# Milestone 1
## Step A
- **Process** : A process is a running instance of a program. It has its own address space, and inside the address space it has its own code, data, stack, and heap.
- **Thread** : A thread is an execution flow within a process. Threads within a process share the address space of the process, except for the stack. The code, heap, and data area are shared between threads.
- **Context Switching** : Context switching between processes is slower because it involves switching the whole address space including the code, heap, stack and data. Context switching between threads is faster because it involves switching only the stack area.

## Step B
### POSIX Thread
A standard library used among UNIX-like operating systems to support multi-threading.

```c
int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg);
```
Creates a new thread.
- pthread_t *thread : Handle of created thread.
- const pthread_attr_t *attr : Attributes of thread to be created. NULL if not used.
- void *(*start_routine)(void *) : New thread executes this function.
- void *arg : single argument to function.

```c
int pthread_join(pthread_t thread, void **ret_val);
```
Wait for the termination of another thread. Returns 0 on success, or a positive error number on error.
- pthread_t thread : The thread to wait for.
- void **ret_val : Return value of the terminated thread.

```c
void pthread_exit(void *ret_val);
```
Terminates the thread.
- void *ret_val : Passes the return value to a thread that is waiting.

## Step C
### Milestone 2 Design
Basic LWP operations should be implemented. The operations include create, join, and exit.
- **Create** : Creates a new thread within a process.
- **Join** : Waits for a thread to be terminated, then cleans up its resources including its page table, allocated memories and stacks.
- **Exit** : Explicitly called to terminate a thread.

```c
int thread_create(thread_t * thread, void * (*start_routine)(void *), void *arg);
```
- A newly created thread is assigned a new *tid*, which is a thread id to distinguish between threads. It is also assigned a *manager_pid*, to indicate the process that created the thread. A pointer to the manager is also assigned.
- The function finds an UNUSED process to use as a thread and allocates a kernel stack to it. Then it sets the trap frame and the context for the stack.
- It searches for an empty space in the process's stack. If there is no empty space, it points to the end of the stack. Then, it allocates two pages using the *allocuvm* function. One is the guard page, and the other is the user stack.
- The thread's *pgdir* is set to the process's *pgdir*.
- The *eip* of the thread's trap frame is set to *start_routine* so that the thread can start from the specified function.
- The *esp* of the thread's trap frame points to the user stack allocated to the thread.
- Lastly, the thread's state is changed to RUNNABLE.

```c
void thread_exit(void *retval);
```
- Wakes up the manager process using the *wakeup1* function.
- Changes the state of the thread to ZOMBIE.
- Saves *retval* to the thread.

```c
int thread_join(thread_t thread, void **retval);
```
- Should be called by a manager process.
- Searches the ptable for the specified thread. If it does not exist, return -1. Otherwise, check if the state is ZOMBIE.
- If the specified thread is in a ZOMBIE state, save the *retval* of the thread to the argument ***retval*.
- Lastly, clean up the specified thread's resources in a similar manner as the *wait* function.

------------

### Milestone 3 Design
- A **light-weight process** is a process that shares its resources with other LWPs, and it is much like a thread.
- LWPs within the same process form a LWP group, and LWPs in a LWP group adopt the same scheduling policy.
- Round Robin scheduling is applied to LWPs within the same LWP group.
- Context switching between LWPs within a LWP group occurs every 1 tick in a given time quantum.
- Context switching between LWPs must be direct, meaning there is no context switching between a LWP and the scheduler.
#### 1. MLFQ Scheduling
LWPs within the same LWP group share their time quantum and time allotment.   
  
The time quantum for each level is as follows.
- Highest Priority Queue : 5 ticks
- Middle Priority Queue : 10 ticks
- Lowest Priority Queue : 20 ticks   
      
The time allotment for each level is as follows.
- Highest Priority Queue : 20 ticks
- Middle Priority Queue : 40 ticks
    
Priority boosting is performed every 200 ticks to prevent starvation.

#### 2. Stride Scheduling
LWPs within the same LWP group share their time quantum.   
When *set_cpu_share* is called by a LWP, all LWPs within the LWP group adopt the stride scheduling policy.     
The total sum of CPU share allocated to stride scheduling must not exceed 80% of the CPU time.   
- Default time quantum : 5 ticks    

#### 3. System Calls
- **Basic Operations** : LWPs share their address space while having their own context and stack. They are managed through system calls *thread_create*, *thread_join*, and *thread_exit*.
- **Exit** : All LWPs including the LWP that called exit must be terminated, and the resources used by LWPs should be cleaned up.
- **Fork** : Fork called by a LWP should work the same as the original fork. The address space of the LWP should be copied normally.
- **Exec** : All other LWPs must be terminated and their resources must be cleaned up. The process executed by exec must be a general process.
- **Sbrk** : When a LWP calls sbrk, the *sz* of the manager process must be updated. Extended memory area of LWPs must not overlap with each other. Also, the extended memory area must be shared among LWPs. A lock must be used to prevent multiple threads from extending the memory area simultaneously.
- **Kill** : If more than one LWP is killed, all LWPs must be terminated and their resources should be cleaned up.
- **Pipe** : All LWPs must share a pipe, and reading or writing data should be synchronized and not be duplicated.
- **Sleep** : Only the requested LWP must be sleeping for the requested time. If a LWP is terminated, the sleeping LWP should also be terminated.

# Milestone 2

## 1. thread_create
*thread_create* is similar to the *fork* and *exec* function.
```c
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
```
First, a new LWP is allocated using *allocproc*. If the caller is a LWP, change to its manager process. This is because it is easier to manage LWP creation with the manager process as the caller.
```c
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
```
Set the manager of the newly created LWP as *curproc*(the caller of the function), then assign a new *tid* to the new LWP after incrementing *nexttid* of the manager process. Once this is done, copy the trap frame of the manager process to the new LWP, and share the manager process's page table. Then, duplicate file references and copy the name of the manager process to the new LWP. Lastly, the new *tid* is saved to *thread*.
```c
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
```
We need to find an empty space to allocate the new LWP. If the manager process's stack is empty, it means there is no empty space in the memory. Therefore, the starting memory address for the LWP is set as the size of the manager process's memory. Then, the memory size of the manager process is increased by 2*PGSIZE. If the manager process's stack is not empty, it means there is empty space in the memory. In this case, the memory address is popped from the stack and this address is used as the starting memory address of the LWP.
```c
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
```
Two pages are allocated to the new LWP using *allocuvm*, one being the guard page and the other being the user stack. *clearpteu* creates an inaccessible page beneath the user stack. Then set *sp*(stack pointer) as *sz* of the new LWP. This way, *sp* points to the top of the new LWP. Next, decrease *sp* by two bytes to copy *ustack* to the LWP using *copyout*. *ustack* has a fake return PC and *arg*(the argument of this function). Finally, *esp*(stack pointer of the LWP) is set to *sp*, which points to the user stack of the LWP, and *eip*(instruction pointer of the LWP) is set to *start_routine*, which is the starting point of the LWP. Also, the state of the LWP must be set to RUNNABLE.
```c
bad:
  kfree(np->kstack);
  np->kstack = 0;
  np->state = UNUSED;
  return -1;
```
On error, the function directly goes to *bad*. *kfree* frees the page of physical memory pointed at by *np->kstack*. Then *np->kstack* is set to 0 and *np->state* is changed to UNUSED.

## 2. thread_exit
*thread_exit* is similar to the *exit* function.
```c
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
```
Only a LWP can call *thread_exit*.
```c
  // Close all open files.
  ...

  // Save the return value to the LWP.
  curproc->retval = retval;

  // Wake the manager process.
  wakeup1(curproc->manager); 

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
```
Similar to the *exit* function, it closes all open files and saves the return value(the argument) to the LWP. Then, it wakes up the LWP's manager process and changes the LWP's state to ZOMBIE. Lastly, it jumps into the scheduler never to return.

## 3. thread_join
*thread_join* is similar to the *wait* function.
```c
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
```
Only a manager process can call *thread_join*.
```c
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
```
Scan through the ptable for the designated LWP. The designated LWP's manager process must be the caller of this function. If the designated LWP is in the ZOMBIE state, save the return value of the LWP to the argument of *thread_join*. Then, clean up resources of the LWP.
```c
        // Clear page allocated to the LWP and push memory address to stack(for future use).
        if((sz = deallocuvm(p->pgdir, p->sz, p->sz - 2*PGSIZE)) == 0) {
            release(&ptable.lock);
            return -1;
        }
        curproc->stack[(curproc->stack_count)++] = sz;
        curproc->nexttid--;

        release(&ptable.lock);
        return 0;
        ...
```
Next, it deallocates the page allocated to the LWP by using *deallocuvm*, and saves the starting memory address(p->sz - 2*PGSIZE) to *sz*. This memory address is then pushed to the manager process's stack. *nexttid* is also decremented by 1. If everything is done successfully, the function returns 0.
```c
    // No point waiting if we don't have any LWP.
    if(!havelwp || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for LWP to exit.
    sleep(curproc, &ptable.lock);
```
If there are no LWPs to wait for, or the manager process is killed, then the function returns -1. Finally, it waits for the LWP to exit by putting the manager process to sleep.

# Milestone 3
## Interaction with system calls in xv6

### 1. Exit
```c
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p, *mgr, *lwp;
  int fd;
  uint sz;

  ...

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
```
If the caller is a LWP, set *mgr* as its manager process and *lwp* as itself.
If the caller is a manager process, set *mgr* as itself and *lwp* as null.

```c
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
  ...
```
All LWPs under *mgr* except *lwp* are terminated. If the LWP is not a ZOMBIE, set *killed* to 1 and wake it up if it is asleep. If the LWP is a ZOMBIE, cleanup its resources as in *thread_join*.

```c
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
  ...
```
If the caller is a LWP, kill its manager process by setting *killed* to 1. Then, wake the manager process so that it can cleanup all LWPs that it manages. The rest is the same as the original *exit*.

--------------------

### 2. Fork
```c
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
  ...
bad:
  kfree(np->kstack);
  np->kstack = 0;
  np->state = UNUSED;
  return -1;
```
Since *fork* creates a new process, even when a LWP calls fork, the entire *pgdir* of the manager process must be copied. Therefore, *sz* is set to the manager process's *sz*. Then, the page table is copied by *copyuvm*. The new process's *sz* is set to *sz*, which is the manager process's *sz*. If there is an error, it falls into *bad*, where the allocated *kstack* is freed. The rest is the same as the original *fork*.

--------------------

### 3. Exec
```c
int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;
  struct proc *curproc = myproc(); 

  ...

  int tid = curproc->tid;
  if(tid > 0)           // If curproc is a LWP, cleanup all LWPs of the same group except itself.
      cleanup_lwp(curproc->manager, curproc);
  else if(tid == 0)     // If curproc is a process, cleanup all LWPs it manages.
      cleanup_lwp(curproc, 0);
  else
      goto bad;
  ...
```
If the caller is a LWP, cleanup all LWPs in the same group except itself. If the caller is a manager process, cleanup all LWPs that it manages. Cleanup is done by the *cleanup_lwp* function.
```c
  // Commit to the user image.
  oldpgdir = curproc->pgdir;
  curproc->pgdir = pgdir;
  curproc->sz = sz;
  curproc->tf->eip = elf.entry;  // main
  curproc->tf->esp = sp;
  switchuvm(curproc);
  // If curproc is a process, free oldpgdir.
  // If curproc is a LWP, do not free since it shares oldpgdir with its manager process.
  if(tid == 0)
      freevm(oldpgdir);

  return 0;
  ...
```
Then, *curproc* is assigned a new *pgdir*. After committing to the new user image, *oldpgdir* must be freed. However, if the caller is a LWP, *oldpgdir* should not be freed because the caller(LWP) shares its *pgdir* with its manager process. Therefore, *oldpgdir* is only freed if the caller is a normal process. The rest is the same as the original *exec*.

--------------------

### 4. Sbrk
#### 4.1 growproc
```c
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
```
First, *&ptable.lock* must be acquired to prevent the race condition where multiple LWPs access *sz* at the same time. *mgr* is set to the manager process, and *sz* is set to the manager process's *sz*. Then, *sz* is updated by either *allocuvm* or *deallocuvm* depending on the value of *n*. Finally, the newly updated *sz* is assigned to the manager process's sz. The lock is released before the end of the function.

#### 4.2 sys_sbrk
```c
int
sys_sbrk(void)
{     
  int addr;
  int n;
    
  if(argint(0, &n) < 0)
    return -1;
    
  // Set addr as manager process's sz.
  if(myproc()->tid > 0)
      addr = myproc()->manager->sz;
  else if(myproc()->tid == 0)
      addr = myproc()->sz;
  else
      return -1;

  if(growproc(n) < 0)
    return -1;
  return addr;
}
```
Only thing modified in *sys_sbrk* is the *addr*. *addr* must be set to the manager process's *sz* since *growproc* updates the *sz* of the manager process.

--------------------

### 5. Kill, Pipe, Sleep
These 3 functions are not modified.

--------------------

### 6. Cleanup_lwp
This is an additional function to terminate LWPs of the same group.
```c
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
```
It takes two arguments, one is *mgr* and the other *lwp*. *mgr* is the manager process of *lwp*. All LWPs under *mgr* except *lwp* are terminated. If they are not a ZOMBIE, their *killed* value is set to 1, and they are woken up if asleep.

## Interaction with the scheduler
### 1. Change time quantum and time allotment
```c
int allotment[2] = {20, 40};     // Array to check if process has used up its allotment.
int quantum[3] = {5, 10, 20};    // Array to check process's ticks with its time quantum.
```
The values in the *allotment* and *quantum* array at the start of proc.c are changed to meet the new requirements.    

Time quantum for each level
- Highest Priority Queue : 5 ticks
- Middle Priority Queue : 10 ticks
- Lowest Priority Queue : 20 ticks   
      
Time allotment for each level
- Highest Priority Queue : 20 ticks
- Middle Priority Queue : 40 ticks

### 2. Change trap function
```c
void
trap(struct trapframe *tf)
{
  ...
  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      if(ticks % 200 == 0)
          priority_boost();         // Priority boost every 200 ticks.
  ...
```
Priority boost is performed every 200 ticks.

```c
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER) {
      if(myproc()->tid == 0) {
          if(myproc()->pass_value != -1) {     // If process is in stride mode, yield every 5 ticks.
              if(myproc()->ticks >= 5) {
                  myproc()->ticks = 0;
                  myproc()->runtime = 0;
                  yield();
              }
          }
          else if(myproc()->ticks >= quantum[myproc()->level]) {    // If process is in MLFQ mode, yield only if quantum has passed.
              myproc()->ticks = 0;
              // Increment process's level if its runtime becomes the same or bigger than its allotment.
              // Only for processes in level 0 and 1.
              if( (myproc()->level < 2) && (myproc()->runtime >= allotment[myproc()->level]) ) {
                  myproc()->level++;
                  myproc()->runtime = 0;
              }
              yield();
          }
          myproc()->ticks++;
          myproc()->runtime++;
      } else {
          // If myproc() is a LWP, yield immediately.
          // Increment its manager process's ticks and runtime.
          yield();
          myproc()->manager->ticks++;
          myproc()->manager->runtime++;
      }
  }
  ... 
```
If *myproc()* is a normal process, follow the original procedure used in Project 1. However, this time the process yields every 5 ticks in stride mode. If *myproc()* is a LWP, yield immediately and increment its manager process's ticks and runtime.

### 3. Change scheduler function
```c
void
scheduler(void)
{
    struct proc *p;
    struct cpu *c = mycpu();
    c->proc = 0;

    struct proc *q, *mgr;
    ...
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
    ... 
```
Only a normal process can have a pass value and be pushed into the heap. Therefore, if a LWP is selected by the scheduler, it sets *mgr* as the manager process of the LWP, and updates the pass value of *mgr* instead of the LWP itself.

## Test Code Result
![스크린샷_2022-05-24_오전_3.16.46](uploads/b3b391e41ca0950bfa2eba0d5da78e86/스크린샷_2022-05-24_오전_3.16.46.png)
![스크린샷_2022-05-24_오전_3.17.07](uploads/9d9810483135559a2efb9c29902cdd08/스크린샷_2022-05-24_오전_3.17.07.png)
![스크린샷_2022-05-24_오전_3.17.20](uploads/3b57d52cb2634db6debc4e807c1c64bb/스크린샷_2022-05-24_오전_3.17.20.png)
![스크린샷_2022-05-24_오전_3.17.38](uploads/7b4e7c3170417c57cb4d808543a88b0a/스크린샷_2022-05-24_오전_3.17.38.png)
All tests except stride test work well.