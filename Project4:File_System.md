# Milestone 1

## 1. struct inode, struct dinode
```c
#define NDIRECT 10
```
```c
struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count
  struct sleeplock lock; // protects everything below here
  int valid;          // inode has been read from disk?

  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+3];
};
```
```c
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEV only)
  short minor;          // Minor device number (T_DEV only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+3];   // Data block addresses
};
```
Since one double indirect pointer and one triple indirect pointer are added, addrs[NDIRECT+1] is changed to addrs[NDIRECT+3]. Also, NDIRECT is changed from 12 to 10 to preserve the size of addrs.

## 2. Defined constants
```c
#define NINDIRECT (BSIZE / sizeof(uint))
#define N2INDIRECT (NINDIRECT*NINDIRECT)
#define N3INDIRECT (N2INDIRECT*NINDIRECT)
#define MAXFILE (NDIRECT + NINDIRECT + N2INDIRECT + N3INDIRECT)
```
BSIZE is 512 and sizeof(uint) is 4, therefore NINDIRECT is 128, meaning an indirect pointer can point up to 128 data blocks.     
N2INDIRECT and N3INDIRECT are newly defined to indicate the number of data blocks a double indirect pointer and a triple indirect pointer can point to. Double indirect pointers can point up to N2INDIRECT number of data blocks, which is 128x128=16,384 blocks. Triple indirect pointers can point up to N3INDIRECT number of data blocks, which is 128x128x128=2,097,152 blocks.      
MAXFILE is the sum of NDIRECT, NINDIRECT, N2INDIRECT and N3INDIRECT (12 + 128 + 128x128 + 128x128x128), which is the maximum number of files possible.

```c
#define FSSIZE       40000  // size of file system in blocks
```
FSSIZE(size of file system in blocks) is also changed to 40,000.

## 3. bmap
The modified bmap function stretches the original structure of the function to enable access to double and triple indirect blocks. The function receives *bn* as input, and returns the disk block address of the *bn*th block in inode ip.
```c
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }
  bn -= NDIRECT;
```
If *bn* is smaller than NDIRECT, the data block is a direct data block. Therefore, the function directly finds the address of the block and returns it(or allocates one if there is no such block).
```c
  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      a[bn] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }
  bn -= NINDIRECT;
```
If *bn* is bigger than NDIRECT but smaller than NINDIRECT, the data block is an indirect data block. Therefore, the function first reads the indirect block to access the indirect data blocks. Among the indirect data blocks, the function finds the address of the block and returns it(or allocates one if there is no such block).
```c
  if(bn < N2INDIRECT){
    // Load double indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT + 1]) == 0)
      ip->addrs[NDIRECT + 1] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;

    if((addr = a[bn / NINDIRECT]) == 0){
      a[bn / NINDIRECT] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;

    if((addr = a[bn % NINDIRECT]) == 0){
      a[bn % NINDIRECT] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }
  bn -= N2INDIRECT;
```
If *bn* is bigger than NINDIRECT but smaller than N2INDIRECT, the data block is a double indirect data block. Therefore, the function first reads the double indirect block. From the double indirect block, since the total number of pointers in a block is NINDIRECT(=128), *bn* is divided by NINDIRECT to find the indirect block that contains the address to the desired data block. From that indirect block, the remainder of (*bn* / NINDIRECT) is used to find the desired data block. Finally, the function finds the address of the block and returns it(or allocates one if there is no such block).
```c
  if(bn < N3INDIRECT){
    // Load triple indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT + 2]) == 0)
      ip->addrs[NDIRECT + 2] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;

    if((addr = a[bn / N2INDIRECT]) == 0){
      a[bn / N2INDIRECT] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;

    if((addr = a[(bn % N2INDIRECT) / NINDIRECT]) == 0){
      a[(bn % N2INDIRECT) / NINDIRECT] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;

    if((addr = a[(bn % N2INDIRECT) % NINDIRECT]) == 0){
      a[(bn % N2INDIRECT) % NINDIRECT] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}
```
If *bn* is bigger than N2INDIRECT but smaller than N3INDIRECT, the data block is a triple indirect data block. Therefore, the function first reads the triple indirect block. The blocks inside the triple indirect block can each point up to N2INDIRECT data blocks, therefore *bn* is divided by N2INDIRECT to find the double indirect block that contains the address to the desired data block. The remainder of (*bn* / N2INDIRECT) can be bigger than NINDIRECT. Therefore, from the double indirect block, (bn % N2INDIRECT) is divided by NINDIRECT to find the indirect block that contains the address to the desired data block. From that indirect block, the remainder of (bn % N2INDIRECT) divided by NINDIRECT is used to find the desired data block. Finally, the function finds the address of the block and returns it(or allocates one if there is no such block).     
If *bn* is bigger than or equal to N3INDIRECT, then it is out of range.

## 4. itrunc
The modified itrunc function stretches the original structure to deallocate double and triple indirect blocks. The function truncates an inode by deallocating all blocks.
```c
static void
itrunc(struct inode *ip)
{
  int i, j, k;
  struct buf *bp, *bp1, *bp2;
  uint *a, *a1, *a2;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }
```
First, the function deallocates direct data blocks.
```c
  // Loop once to deallocate indirect blocks. 
  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }
```
To deallocate indirect data blocks, it reads the indirect block and loops through the blocks pointed by the indirect pointer to free all indirect data blocks. Lastly, it deallocates the indirect block.
```c
  // Loop twice to deallocate double indirect blocks. 
  if(ip->addrs[NDIRECT + 1]){
    bp = bread(ip->dev, ip->addrs[NDIRECT + 1]);
    a = (uint*)bp->data;
    for(i = 0; i < NINDIRECT; i++){
      if(a[i]) {
          bp1 = bread(ip->dev, a[i]);
          a1 = (uint*)bp1->data;
          for(j = 0; j < NINDIRECT; j++) {
              if(a1[j])
                  bfree(ip->dev, a1[j]);
          }
          brelse(bp1);
          bfree(ip->dev, a[i]);
      }
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT + 1]);
    ip->addrs[NDIRECT + 1] = 0;
  }
```
To deallocate double indirect data blocks, it reads the double indirect block and loops through the indirect blocks pointed by the double indirect pointer. For each indirect block, it loops through the blocks pointed by the indirect pointer to free all double indirect data blocks. Then, it deallocates the indirect block. After freeing all indirect blocks, it finally frees the double indirect block.
```c
  // Loop three times to deallocate triple indirect blocks. 
  if(ip->addrs[NDIRECT + 2]){
    bp = bread(ip->dev, ip->addrs[NDIRECT + 2]);
    a = (uint*)bp->data;
    for(i = 0; i < NINDIRECT; i++){
      if(a[i]) {
          bp1 = bread(ip->dev, a[i]);
          a1 = (uint*)bp1->data;
          for(j = 0; j < NINDIRECT; j++) {
              if(a1[j]) {
                  bp2 = bread(ip->dev, a1[j]);
                  a2 = (uint*)bp2->data;
                  for(k = 0; k < NINDIRECT; k++) {
                      if(a2[k])
                          bfree(ip->dev, a2[k]);
                  }
                  brelse(bp2);
                  bfree(ip->dev, a1[j]);
              }
          }
          brelse(bp1);
          bfree(ip->dev, a[i]);
      }
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT + 2]);
    ip->addrs[NDIRECT + 2] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}
```
To deallocate triple indirect data blocks, it reads the triple indirect block and loops through the double indirect blocks pointed by the triple indirect pointer. For each double indirect block, it loops through the indirect blocks pointed by the double indirect pointer. For each indirect block, it loops through the blocks pointed by the indirect pointer to free all triple indirect data blocks. Then, it deallocates the indirect block. After freeing all indirect blocks, it frees the double indirect block. After freeing all double indirect blocks, it finally frees the triple indirect block.      
Lastly, the size of the inode is updated to 0.

# Milestone 2
## 1. pread
pread function is similar to the *fileread* function. However, pread has an additional offset parameter to specify the data offset of the file. The function reads the file starting from that offset. Also, pread does not update the file offset unlike fileread.
```c
int
pread(struct file *f, void *addr, int n, int off)
{
  int r;

  if(f->readable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return piperead(f->pipe, addr, n);
  if(f->type == FD_INODE){
    ilock(f->ip);
    // Start reading from the specified offset.
    // The file offset is not changed.
    r = readi(f->ip, addr, off, n);
    iunlock(f->ip);
    return r;
  }
  panic("pread");
}
```
pread has an additional offset parameter *off*, and the file offset is not changed.
```c
int
sys_pread(void)
{
    struct file *f;
    int n, off;
    void* addr;

    if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, (void*)&addr, n) < 0 || argint(3, &off) < 0)
        return -1;

    return pread(f, addr, n, off);
}
```
The user passes an integer *fd* instead of a file pointer. It is the job of the sys_pread function to convert fd to a file pointer. It then passes the file pointer to pread.

## 2. pwrite
pwrite function is similar to the *filewrite* function. However, pwrite has an additional offset parameter to specify the data offset of the file. The function writes to the file starting from that offset. Also, pwrite does not update the file offset unlike filewrite.
```c
int
pwrite(struct file *f, void *addr, int n, int off)
{
  int r;

  if(f->writable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return pipewrite(f->pipe, addr, n);
  if(f->type == FD_INODE){
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * 512;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      // The file offset is not changed.
      if ((r = writei(f->ip, addr + i, off, n1)) > 0)
        off += r;
      iunlock(f->ip);
      end_op();

      if(r < 0)
        break;
      if(r != n1)
        panic("short pwrite");
      i += r;
    }
    return i == n ? n : -1;
  }
  panic("pwrite");
}
```
pwrite has an additional offset parameter *off*, and the file offset is not changed.
```c
int
sys_pwrite(void)
{
    struct file *f;
    int n, off;
    void* addr;

    if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, (void*)&addr, n) < 0 || argint(3, &off) < 0)
        return -1;

    return pwrite(f, addr, n, off);
}
```
The user passes an integer *fd* instead of a file pointer. It is the job of the sys_pwrite function to convert fd to a file pointer. It then passes the file pointer to pwrite.

## 3. Thread-safe read & write library
The implementation of the thread-safe read and write library is done in the ulib.c file.

### 1) thread_safe_guard structure
```c
typedef struct __thread_safe_guard {
    rwlock_t rwlock;
    int fd;
} thread_safe_guard;
```
A thread_safe_guard structure has a rwlock(read-write lock) and a file descriptor.

### 2) thread_safe_guard_init
```c
thread_safe_guard*
thread_safe_guard_init(int fd)
{
    static thread_safe_guard guard;
    guard.fd = fd;
    rwlock_init(&guard.rwlock);

    return &guard;
}
```
A thread_safe_guard is initialized using the static keyword. This way, the declared variable can be used outside of this function. The function receives a file descriptor as a parameter and sets the *guard*'s file descriptor. Then, the *guard*'s rwlock is initialized by the rwlock_init function. Finally, the address of *guard* is returned.

### 3) thread_safe_pread
```c
int
thread_safe_pread(thread_safe_guard* file_guard, void* addr, int n, int off)
{
    int sz;
    rwlock_acquire_readlock(&file_guard->rwlock);
    sz = pread(file_guard->fd, addr, n, off);
    rwlock_release_readlock(&file_guard->rwlock);
    return sz;
}
```
thread_safe_pread uses pread internally, and it receives a thread_safe_guard pointer instead of a file descriptor as the first parameter. First, a read lock is acquired using the rwlock_acquire_readlock function, since we are reading the file. Then, pread is called by passing *file_guard*'s file descriptor as the first parameter. Next, the read lock is released by the rwlock_release_readlock function. Finally, the number of bytes read is returned.

### 4) thread_safe_pwrite
```c
int
thread_safe_pwrite(thread_safe_guard* file_guard, void* addr, int n, int off)
{
    int sz;
    rwlock_acquire_writelock(&file_guard->rwlock);
    sz = pwrite(file_guard->fd, addr, n, off);
    rwlock_release_writelock(&file_guard->rwlock);
    return sz;
}
```
thread_safe_pwrite uses pwrite internally, and it receives a thread_safe_guard pointer instead of a file descriptor as the first parameter. First, a write lock is acquired using the rwlock_acquire_writelock function, since we are writing to the file. Then, pwrite is called by passing *file_guard*'s file descriptor as the first parameter. Next, the write lock is released by the rwlock_release_writelock function. Finally, the number of bytes written is returned.

### 5) thread_safe_guard_destroy
```c
void
thread_safe_guard_destroy(thread_safe_guard* file_guard)
{
    file_guard = 0;
}
```
In C language, 0 is identical to NULL. Therefore, *file_guard* is set to 0 to make it a NULL pointer.

