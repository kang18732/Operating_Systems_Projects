#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_getppid(void)
{
	//struct proc* p;
	//p = myproc();
  return myproc()->parent->pid;
}

int
sys_yield(void)     // This function is called when user calls yield(), not timer interrupt.
{
    if(myproc()) {
        // Increment ticks and runtime.
        myproc()->ticks++;
        myproc()->runtime++;
        // If the process's level is lower than 2 and its runtime reaches its allotment, increment its level.
        if( (myproc()->level < 2) && (myproc()->runtime >= allotment[myproc()->level]) ) {
            myproc()->level++;
            myproc()->runtime = 0;
            myproc()->ticks = 0;
        }
    }
    yield();
    return 0;
}

int
sys_getlev(void)
{
    // Return level if process is in MLFQ mode.
    if(myproc()->pass_value == -1)
        return myproc()->level;
    // Otherwise return a negative value.
    else
        return -1;
}

int
sys_set_cpu_share(void)
{
    int percent;
    if(argint(0, &percent) < 0)
        return -1;
    return set_cpu_share(percent);
}

int
sys_thread_create(void)
{
    int thread, start_routine, arg;

    if( (argint(0, &thread) < 0) || (argint(1, &start_routine) < 0) || (argint(2, &arg) < 0) )
        return -1;

    return thread_create((thread_t*)thread, (void*)start_routine, (void*)arg);
}

int
sys_thread_exit(void)
{
    int retval;

    if(argint(0, &retval) < 0)
        return -1;

    thread_exit((void*)retval);
    return 0;
}

int
sys_thread_join(void)
{
    int thread, retval;

    if( (argint(0, &thread) < 0) || (argint(1, &retval) < 0) )
        return -1;

    return thread_join((thread_t)thread, (void**)retval);
}

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

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int
sys_TestAndSet(void)
{
    int *ptr, new;

    if(argptr(0, (void*)&ptr, sizeof(int*)) < 0)
        return -1;
    if(argint(1, &new) < 0)
        return -1;

    return TestAndSet(ptr, new);
}

int
sys_Cond_init(void)
{
    thread_cond_t *cond;

    if(argptr(0, (void*)&cond, sizeof(thread_cond_t*)) < 0)
        return -1;

    Cond_init(cond);
    return 0;
}

int
sys_Cond_wait(void)
{
    thread_cond_t *cond;
    thread_mutex_t *lock;

    if(argptr(0, (void*)&cond, sizeof(thread_cond_t*)) < 0)
        return -1;
    if(argptr(1, (void*)&lock, sizeof(thread_mutex_t*)) < 0)
        return -1;

    Cond_wait(cond, lock);
    return 0;
}

int
sys_Cond_signal(void)
{
    thread_cond_t *cond;

    if(argptr(0, (void*)&cond, sizeof(thread_cond_t*)) < 0)
        return -1;

    Cond_signal(cond);
    return 0;
}

int
sys_Mutex_init(void)
{
    thread_mutex_t *lock;

    if(argptr(0, (void*)&lock, sizeof(thread_mutex_t*)) < 0)
        return -1;

    Mutex_init(lock);
    return 0;
}

int
sys_Mutex_lock(void)
{
    thread_mutex_t *lock;

    if(argptr(0, (void*)&lock, sizeof(thread_mutex_t*)) < 0)
        return -1;

    Mutex_lock(lock);
    return 0;
}

int
sys_Mutex_unlock(void)
{
    thread_mutex_t *lock;

    if(argptr(0, (void*)&lock, sizeof(thread_mutex_t*)) < 0)
        return -1;

    Mutex_unlock(lock);
    return 0;
}

int
sys_xem_init(void)
{
    xem_t *semaphore;

    if(argptr(0, (void*)&semaphore, sizeof(xem_t*)) < 0)
        return -1;

    return xem_init(semaphore);
}

int
sys_xem_wait(void)
{
    xem_t *semaphore;

    if(argptr(0, (void*)&semaphore, sizeof(xem_t*)) < 0)
        return -1;

    return xem_wait(semaphore);
}

int
sys_xem_unlock(void)
{
    xem_t *semaphore;

    if(argptr(0, (void*)&semaphore, sizeof(xem_t*)) < 0)
        return -1;

    return xem_unlock(semaphore);
}

int
sys_rwlock_init(void)
{
    rwlock_t *rwlock;

    if(argptr(0, (void*)&rwlock, sizeof(rwlock_t*)) < 0)
        return -1;

    return rwlock_init(rwlock);
}

int
sys_rwlock_acquire_readlock(void)
{
    rwlock_t *rwlock;

    if(argptr(0, (void*)&rwlock, sizeof(rwlock_t*)) < 0)
        return -1;

    return rwlock_acquire_readlock(rwlock);
}

int
sys_rwlock_acquire_writelock(void)
{
    rwlock_t *rwlock;

    if(argptr(0, (void*)&rwlock, sizeof(rwlock_t*)) < 0)
        return -1;

    return rwlock_acquire_writelock(rwlock);
}

int
sys_rwlock_release_readlock(void)
{
    rwlock_t *rwlock;

    if(argptr(0, (void*)&rwlock, sizeof(rwlock_t*)) < 0)
        return -1;

    return rwlock_release_readlock(rwlock);
}

int
sys_rwlock_release_writelock(void)
{
    rwlock_t *rwlock;

    if(argptr(0, (void*)&rwlock, sizeof(rwlock_t*)) < 0)
        return -1;

    return rwlock_release_writelock(rwlock);
}

