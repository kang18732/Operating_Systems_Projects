#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

int rwlock_init(rwlock_t *rwlock)
{
    rwlock->readers = 0;
    xem_init(&rwlock->lock);
    xem_init(&rwlock->writelock);
    return 0;
}

int rwlock_acquire_readlock(rwlock_t *rwlock)
{
    // Increment readers by 1.
    xem_wait(&rwlock->lock);
    rwlock->readers++;
    // If the caller is the first reader, acquire the write lock as well.
    if(rwlock->readers == 1)
        xem_wait(&rwlock->writelock);
    xem_unlock(&rwlock->lock);
    return 0;
}

int rwlock_acquire_writelock(rwlock_t *rwlock)
{
    xem_wait(&rwlock->writelock);
    return 0;
}

int rwlock_release_readlock(rwlock_t *rwlock)
{
    // Decrement readers by 1.
    xem_wait(&rwlock->lock);
    rwlock->readers--;
    // If the caller is the last reader, release the write lock.
    if(rwlock->readers == 0)
        xem_unlock(&rwlock->writelock);
    xem_unlock(&rwlock->lock);
    return 0;
}

int rwlock_release_writelock(rwlock_t *rwlock)
{
    xem_unlock(&rwlock->writelock);
    return 0;
}
