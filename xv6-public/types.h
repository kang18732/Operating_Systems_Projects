typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;
typedef uint pde_t;
typedef int thread_t;
typedef struct __thread_mutex_t {
    int flag;
} thread_mutex_t;
typedef struct __thread_cond_t {
    int waiting_threads;
    thread_mutex_t lock;
} thread_cond_t;
typedef struct __xem_t {
    int value;
    thread_cond_t cond;
    thread_mutex_t lock;
} xem_t;
typedef struct __rwlock_t {
    xem_t lock;
    xem_t writelock;
    int readers;
} rwlock_t;
typedef struct __thread_safe_guard {
    rwlock_t rwlock;
    int fd;
} thread_safe_guard;
