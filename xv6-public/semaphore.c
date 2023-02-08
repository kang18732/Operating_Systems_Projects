#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

int TestAndSet(int *ptr, int new) {
    int old = *ptr;
    *ptr = new;
    return old;
}

void Cond_init(thread_cond_t *cond) {
    cond->waiting_threads = 0;
    Mutex_init(&cond->lock);
}

void Cond_wait(thread_cond_t *cond, thread_mutex_t *lock) {
    struct proc *p = myproc();

    if(p == 0)
        panic("sleep");

    if(lock == 0)
        panic("sleep without lock");

    // Increment number of waiting threads by 1.
    Mutex_lock(&cond->lock);
    cond->waiting_threads++;
    Mutex_unlock(&cond->lock);

    acquire(&ptable.lock);
    // Release lock before going to sleep.
    Mutex_unlock(lock);

    // Save condition variable to chan, then go to sleep.
    p->chan = cond;
    p->state = SLEEPING;

    sched();

    // Tidy up.
    p->chan = 0;

    release(&ptable.lock);
    // Reacquire original lock.
    Mutex_lock(lock);
}

void Cond_signal(thread_cond_t *cond) {
    Mutex_lock(&cond->lock);
    // If there are no waiting threads, return immediately.
    if(cond->waiting_threads == 0) {
        Mutex_unlock(&cond->lock);
        return;
    }
    // Decrement the number of waiting threads by 1.
    cond->waiting_threads--;
    Mutex_unlock(&cond->lock);

    struct proc *p;

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        // Search for a thread that is sleeping on the channel which is equal to the condition variable.
        // Return after waking one thread.
        if(p->state == SLEEPING && p->chan == cond) {
            p->state = RUNNABLE;
            break;
        }
}

void Mutex_init(thread_mutex_t *lock) {
    lock->flag = 0;
}

void Mutex_lock(thread_mutex_t *lock) {
    while(TestAndSet(&lock->flag, 1) == 1);
}

void Mutex_unlock(thread_mutex_t *lock) {
    lock->flag = 0;
}

int xem_init(xem_t *semaphore) {
    semaphore->value = 1;
    Cond_init(&semaphore->cond);
    Mutex_init(&semaphore->lock);
    return 0;
}

int xem_wait(xem_t *semaphore) {
    Mutex_lock(&semaphore->lock);
    // If the value of the semaphore is not positive, sleep until the value rises above 0.
    while (semaphore->value <= 0)
        Cond_wait(&semaphore->cond, &semaphore->lock);
    // Decrement the value of the semaphore by 1.
    semaphore->value--;
    Mutex_unlock(&semaphore->lock);
    return 0;
}

int xem_unlock(xem_t *semaphore) {
    Mutex_lock(&semaphore->lock);
    // Increment the value of the semaphore by 1.
    semaphore->value++;
    // Wake up any one thread which is waiting on the condition.
    Cond_signal(&semaphore->cond);
    Mutex_unlock(&semaphore->lock);
    return 0;
}
