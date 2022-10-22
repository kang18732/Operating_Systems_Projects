# 1. Semaphore / Readers-Writer Lock

### a) Semaphore

A **semaphore** is used to limit the access to a critical section by using an integer value. The value of the semaphore, if bigger than 0, is the number of threads that can concurrently access the critical section. Thus, a thread is allowed to enter the critical section if the value is a positive number. When a thread accesses the critical section, the semaphore value is decreased by 1. If the semaphore value becomes 0, then access is denied. The semaphore value is increased by 1 when a thread exits the critical section.

### b) Readers-Writer Lock

Mutex blocks any thread that tries to access the critical section, and does not consider the situation where there could be readers. Readers do not write anything, therefore concurrent access of readers should be allowed. The **readers-writer** lock is used to resolve this situation. It is consisted of a read-lock and a write-lock. A read-lock is held by a reader, and once it is held, any access from a writer is blocked. However, access by multiple readers are allowed, meaning multiple read-locks can be held concurrently. A write-lock is held by a writer, and once it is held, any access from both reader and writer is blocked. By using readers-writer lock, concurrency is enhanced because unlike mutex, multiple readers are allowed to access the critical section concurrently.

# 2. POSIX Semaphore

```c
typedef struct {
    int value;
    ...
} sem_t;
```

The semaphore structure _sem_t_ consists of an integer value. If the value is positive, it represents the number of threads that can access the critical section concurrently. If the value is 0, access is denied.

```c
int sem_init(sem_t *sem, int pshared, unsigned int value);
```

Initializes the semaphore _sem_ and sets the initial value for the semaphore.

- int pshared : If 0, the semaphore is shared between the threads of a process. If nonzero, the semaphore is shared between processes.
- unsigned int value : The initial value for the semaphore.

Returns 0 on success and -1 on error.

```c
int sem_wait(sem_t *sem);
```

Decrements the value of semaphore _sem_ by 1. If the value is non-negative, the function returns immediately. If the value becomes negative, then execution is suspended until the value becomes non-negative or a signal handler interrupts the call.  
Returns 0 on success and -1 on error(the value of the semaphore remains unchanged).

```c
int sem_post(sem_t *sem);
```

Increments the value of semaphore _sem_ by 1. Then, it wakes up one of the threads that are waiting to be woken up, if any.  
Returns 0 on success and -1 on error(the value of the semaphore remains unchanged).

# 3. POSIX Readers-Writer Lock

```c
typedef struct {
    int readers;
    priority_queue_t queue;
    mutex_t mutex;
} pthread_rwlock_t;
```

The structure consists of a queue of waiting threads, a mutex to provide mutual exclusion, and an integer variable which counts the number of readers in the critical section. By using a priority queue, this implementation resolves the starvation problem among readers and writers. No new reader will acquire the lock if there is a waiting writer which has the same or higher priority than the readers.

```c
int pthread_rwlock_init(pthread_rwlock_t* lock, const pthread_rwlockattr_t* attr);
```

Initializes a new read-write lock with the specified attributes in _attr_. If _attr_ is NULL, all attributes are set to the default attributes.  
Returns 0 on success and an error number on error.

```c
int pthread_rwlock_rdlock(pthread_rwlock_t* lock);
```

Attempts to acquire a read lock on the read-write lock referenced by _lock_. If no write lock is held and there are no writers blocked on the lock, then the calling thread acquires the read lock. Otherwise, it blocks until it can acquire the read lock.  
Returns 0 on success and an error number on error.

```c
int pthread_rwlock_wrlock(pthread_rwlock_t* lock);
```

Attempts to acquire an exclusive write lock on the read-write lock referenced by _lock_. If no other thread(reader or writer) holds _lock_, the calling thread acquires the write lock. Otherwise, it blocks until it can acquire the write lock.  
Returns 0 on success and an error number on error.

```c
int pthread_rwlock_unlock(pthread_rwlock_t* lock);
```

Releases the lock held on the read-write lock referenced by _lock_. If this function releases a read lock for _lock_ but there are still other read locks held on _lock_, then _lock_ remains in the read locked state. If this function releases a write lock or the last read lock for _lock_, _lock_ will be put in the unlocked state with no owners.  
Returns 0 on success and an error number on error.

# 4. Implementation of Basic Semaphore

### 1) Definition of structures

```c
typedef struct __thread_mutex_t {
    int flag;
} thread_mutex_t;
```

This structure represents a mutex. It has an integer variable _flag_ to indicate whether it is locked or not.

```c
typedef struct __thread_cond_t {
    int waiting_threads;
    thread_mutex_t lock;
} thread_cond_t;
```

This structure represents a condition variable. It has an integer variable _waiting_threads_ to indicate the number of waiting threads on the condition. It also has a thread*mutex_t variable \_lock* to protect the integer variable _waiting_threads_.

```c
typedef struct __xem_t {
    int value;
    thread_cond_t cond;
    thread_mutex_t lock;
} xem_t;
```

This structure represents a semaphore. It has an integer variable _value_, which is the value of the semaphore. It also has a thread*cond_t variable \_cond* and a thread_mutex_t variable \*lock, which are respectively a condition variable and a mutex for the semaphore.

### 2) Additional functions needed

```c
int TestAndSet(int *ptr, int new) {
    int old = *ptr;
    *ptr = new;
    return old;
}
```

Atomically assigns the new value to the pointer and returns the old value of the pointer.

---

```c
void Mutex_init(thread_mutex_t *lock) {
    lock->flag = 0;
}
```

Initializes the mutex by setting its flag to 0(unlocked state).

---

```c
void Mutex_lock(thread_mutex_t *lock) {
    while(TestAndSet(&lock->flag, 1) == 1);
}
```

Acquires the lock using the _TestAndSet_ function. If the return value of _TestAndSet_ is 1, it waits because it means the old value of _flag_ is 1(lock is held by some other thread). If the return value of _TestAndSet_ is 0(lock is not held by any other thread), it immediately returns because it has already set the _flag_ to 1(meaning it acquired the lock).

---

```c
void Mutex_unlock(thread_mutex_t *lock) {
    lock->flag = 0;
}
```

Releases the lock by setting _flag_ to 0.

---

```c
void Cond_init(thread_cond_t *cond) {
    cond->waiting_threads = 0;
    Mutex_init(&cond->lock);
}
```

Initializes the condition variable by setting _waiting_threads_ to 0 and initializing the mutex.

---

```c
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

    return 0;
}
```

Atomically releases the lock held by the calling thread and then puts the thread to sleep. First, it increments the number of waiting threads by 1. Then it releases the lock using _Mutex_unlock_ and saves the condition variable _cond_ to _chan_ of the calling thread. After this the thread's state is changed to SLEEPING. When it wakes up, the _chan_ of the thread is set to 0 and the lock is reacquired by _Mutex_lock_.

---

```c
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
```

Wakes a single thread waiting on the condition, if there is any thread sleeping on that condition. It first checks whether there are any waiting threads, and returns immediately if there are no waiting threads. Else, it decrements the number of waiting threads by 1. Then, it loops through the ptable and searches for a thread that is sleeping on the channel which is equal to the condition variable. If found, it wakes the thread and then returns immediately.

### 3) Semaphore related functions

```c
int xem_init(xem_t *semaphore) {
    semaphore->value = 1;
    Cond_init(&semaphore->cond);
    Mutex_init(&semaphore->lock);
    return 0;
}
```

Initializes the semaphore by setting the value to 1. The condition variable and the mutex of the semaphore is also initialized.

---

```c
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
```

Waits for the semaphore value to be positive, then decrements the value by 1. If first checks the value of the semaphore, and if the value is not positive, it puts the calling thread to sleep until the value rises above 0. Once the value becomes positive, it decrements the value of the semaphore by 1.

---

```c
int xem_unlock(xem_t *semaphore) {
    Mutex_lock(&semaphore->lock);
    // Increment the value of the semaphore by 1.
    semaphore->value++;
    // Wake up any one thread which is waiting on the condition.
    Cond_signal(&semaphore->cond);
    Mutex_unlock(&semaphore->lock);
    return 0;
}
```

Increments the value of the semaphore by 1, then calls _Cond_signal_ to wake up a single thread(if any) which is waiting on the condition defined in the semaphore.

# 5. Implementation of Readers-Writer Lock using Semaphores

### a) Definition of structure

```c
typedef struct __rwlock_t {
    xem_t lock;
    xem_t writelock;
    int readers;
} rwlock_t;
```

This structure represents a readers-writer lock. It has an integer variable _readers_ that counts the number of readers accessing the critical section. It also has two semaphores _lock_ and _writelock_. _lock_ protects the integer variable _readers_, and _writelock_ protects the critical section by allowing one writer or multiple readers.

### b) Readers-Writer lock related functions

```c
int rwlock_init(rwlock_t *rwlock)
{
    rwlock->readers = 0;
    xem_init(&rwlock->lock);
    xem_init(&rwlock->writelock);
    return 0;
}
```

Initializes a readers-writer lock. It sets _readers_(number of readers in the critical section) to 0, then initializes the semaphores _lock_ and _writelock_.

---

```c
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
```

Acquires a read lock. First, it waits to acquire the lock that protects _readers_, then increments _readers_ by 1. If the caller is the first reader, then it acquires the write lock as well. Lastly, it releases the lock that protects _readers_.

---

```c
int rwlock_acquire_writelock(rwlock_t *rwlock)
{
    xem_wait(&rwlock->writelock);
    return 0;
}
```

Acquires a write lock.

---

```c
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
```

Releases a read lock. First, it waits to acquire the lock that protects _readers_, then decrements _readers_ by 1. If the caller is the last reader, then it releases the write lock. Lastly, it releases the lock that protects _readers_.

---

```c
int rwlock_release_writelock(rwlock_t *rwlock)
{
    xem_unlock(&rwlock->writelock);
    return 0;
}
```

Releases a write lock.
