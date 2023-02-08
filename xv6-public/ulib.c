#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "user.h"
#include "x86.h"

char*
strcpy(char *s, const char *t)
{
  char *os;

  os = s;
  while((*s++ = *t++) != 0)
    ;
  return os;
}

int
strcmp(const char *p, const char *q)
{
  while(*p && *p == *q)
    p++, q++;
  return (uchar)*p - (uchar)*q;
}

uint
strlen(const char *s)
{
  int n;

  for(n = 0; s[n]; n++)
    ;
  return n;
}

void*
memset(void *dst, int c, uint n)
{
  stosb(dst, c, n);
  return dst;
}

char*
strchr(const char *s, char c)
{
  for(; *s; s++)
    if(*s == c)
      return (char*)s;
  return 0;
}

char*
gets(char *buf, int max)
{
  int i, cc;
  char c;

  for(i=0; i+1 < max; ){
    cc = read(0, &c, 1);
    if(cc < 1)
      break;
    buf[i++] = c;
    if(c == '\n' || c == '\r')
      break;
  }
  buf[i] = '\0';
  return buf;
}

int
stat(const char *n, struct stat *st)
{
  int fd;
  int r;

  fd = open(n, O_RDONLY);
  if(fd < 0)
    return -1;
  r = fstat(fd, st);
  close(fd);
  return r;
}

int
atoi(const char *s)
{
  int n;

  n = 0;
  while('0' <= *s && *s <= '9')
    n = n*10 + *s++ - '0';
  return n;
}

void*
memmove(void *vdst, const void *vsrc, int n)
{
  char *dst;
  const char *src;

  dst = vdst;
  src = vsrc;
  while(n-- > 0)
    *dst++ = *src++;
  return vdst;
}

thread_safe_guard*
thread_safe_guard_init(int fd)
{
    static thread_safe_guard guard;
    guard.fd = fd;
    rwlock_init(&guard.rwlock);

    return &guard;
}

int
thread_safe_pread(thread_safe_guard* file_guard, void* addr, int n, int off)
{
    int sz;
    rwlock_acquire_readlock(&file_guard->rwlock);
    sz = pread(file_guard->fd, addr, n, off);
    rwlock_release_readlock(&file_guard->rwlock);
    return sz;
}

int
thread_safe_pwrite(thread_safe_guard* file_guard, void* addr, int n, int off)
{
    int sz;
    rwlock_acquire_writelock(&file_guard->rwlock);
    sz = pwrite(file_guard->fd, addr, n, off);
    rwlock_release_writelock(&file_guard->rwlock);
    return sz;
}

void
thread_safe_guard_destroy(thread_safe_guard* file_guard)
{
    file_guard = 0;
}

