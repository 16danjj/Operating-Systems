# Design Document: Project 4 - File System

## Preliminaries

> If you have any preliminary comments on your submission, notes for the
> TAs, or extra credit, please give them here.

We enabled VM functionality.

> Please cite any offline or online sources you consulted while preparing your
> submission, other than the Pintos documentation, course text, lecture notes,
> and course staff.


## Indexed and Extensible Files

### Data Structures

> A1: Copy here the declaration of each new or changed `struct` or `struct`
> member, global or static variable, `typedef`, or enumeration.  Identify the
> purpose of each in 25 words or less.
```c
#define DIRECT_BLOCKS 10
#define INDIRECT_BLOCKS 1
#define DOUBLE_DIRECT_BLOCKS 1
#define UNUSED_BLOCKS (125 - 2 - DIRECT_BLOCKS - INDIRECT_BLOCKS - DOUBLE_DIRECT_BLOCKS)

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */

    /* New elements */
    block_sector_t direct_block[DIRECT_BLOCKS]; /* Direct block index. */
    block_sector_t indirect_block;              /* Indirect block index. */
    block_sector_t doubly_indirect_block;       /* Doubly indirect block index. */
    bool is_directory;                          /* True if inode is a directory. */
    block_sector_t parent_directory;            /* First parent directory sector. */

    unsigned magic;                             /* Magic number. */
    uint32_t unused[UNUSED_BLOCKS];             /* Not used. */
  };
```
```c
/* In-memory inode to store the inode_disk */
struct inode_data {
  block_sector_t start;               /* First data sector. */
  off_t length;                       /* File size in bytes. */

  block_sector_t direct_block[DIRECT_BLOCKS]; /* Direct block index. */
  block_sector_t indirect_block;              /* Indirect block index. */
  block_sector_t doubly_indirect_block;       /* Doubly indirect block index. */
  bool is_directory;                          /* True if inode is a directory. */
  block_sector_t parent_directory;            /* First parent directory sector. */
};
```
```c
/* In-memory inode. */
struct inode
{
  /* New element */
  struct lock lock;       /* Lock for inode. */
};
```

```c
/* Structure that stores state of a reader-writer lock */
struct rw_lock
{ 
    uint64_t waiting_writers;  /* Number of waiting writers to a file */
    uint64_t active_writers;   /* Number of active writers to a file */
    uint64_t waiting_readers;  /* Number of waiting readers to a file */
    uint64_t active_readers;   /* Number of active readers to a file */
    struct lock mutex;         /* Lock to ensure atomicity of state */
    struct condition OK_write; /* Condition for writing operations */
    struct condition OK_read;  /* Condition for reading operations */
};
```

> A2: What is the maximum size of a file supported by your inode structure?
> Show your work.

10 * 512 = 5120 bytes                (10 direct blocks)\
1 * 512 * 128 = 65536 bytes          (1 indirect block)\
1 * 512 * 128 * 128 = 8388608 bytes  (1 doubly indirect block)\
= 8388608 + 65536 + 5120 = 8457664 bytes or 8.06 MB 

### Synchronization

> A3: Explain how your code avoids a race if two processes attempt to extend a
> file at the same time.

Each inode has a lock. If one process attempts to extend a file, it needs to acquire this lock. If another process wants to extend the same file at the same time, it will need to wait for the other process to finish.

> A4: Suppose processes A and B both have file F open, both positioned at
> end-of-file.  If A reads and B writes F at the same time, A may read all,
> part, or none of what B writes.  However, A may not read data other than what
> B writes, e.g. if B writes nonzero data, A is not allowed to see all zeros.
> Explain how your code avoids this race.

Using a read-write lock, we ensure that while an active writer is writing to a file F, a reader won't be able to 
read the file. So A will only be able to read the file, once B completes writing and releases the read-write lock. 


> A5: Explain how your synchronization design provides "fairness". File access
> is "fair" if readers cannot indefinitely block writers or vice versa.  That
> is, many processes reading from a file cannot prevent forever another process
> from writing the file, and many processes writing to a file cannot prevent
> another process forever from reading the file.

We have tried to make the read-write lock fair for both writers and readers. 

If the lock is currently held by readers, and another reader requests the lock, the request is only granted 
if no writers are currently blocked. So here, fairness is guaranteed for writers.

The lock is biased towards writers, so therefore is a little "unfair" to readers, because if the lock is released 
by a writer, we grant the lock to waiting readers next, only if no other writers are blocked.

### Rationale

> A6: Is your inode structure a multilevel index?  If so, why did you choose
> this particular combination of direct, indirect, and doubly indirect blocks?
> If not, why did you choose an alternative inode structure, and what advantages
> and disadvantages does your structure have, compared to a multilevel index?

Our inode structure is a multilevel index consisting of 10 direct blocks, 1 indirect block and 1 double indirect block. As we needed at least 8 MB of space, we needed at least 1 double indirect block. We chose to use 10 direct blocks to have a quick access for small files while still having a rather small inode. We also only chose to use 1 indirect block as it would also increase the size of our inode.

## Subdirectories

### Data Structures

> B1: Copy here the declaration of each new or changed `struct` or `struct`
> member, global or static variable, `typedef`, or enumeration.  Identify the
> purpose of each in 25 words or less.

```c
struct thread {
	/* New element */
	struct dir *cwd;                     /* Current working directory */
}
```

### Algorithms

> B2: Describe your code for traversing a user-specified path.  How do
> traversals of absolute and relative paths differ?

We implemented the function `filesys_lookup(const char *path, struct dir **dir, char *filename)`, which looks for the file at the end of the path and saves the directory of the file in `dir` and the filename of the file in `filename`. First we start by choosing our start directory. If it's an absolute path, we start from the root directory, if it's a relative path, we start from the current working directory that is saved in the current thread.
Then we tokenize the path with the delimiter `/` and look if the current token exists in this path. If it does, it's a directory and the next token is not NULL, we set the current directory as the one we found. If that's not the case, we will finish the loop and copy the filename into `filename`, as long as the path exists.

### Synchronization

> B4: How do you prevent races on directory entries?  For example, only one of
> two simultaneous attempts to remove a single file should succeed, as should
> only one of two simultaneous attempts to create a file with the same name, and
> so on.

We prevent races on directory entries by using the lock of the inodes.
When a file is to be added or removed, it needs to acquire the lock of the inode of its current directory.
Concurrent attempts to add or remove files are thus prevented, as only the first one can complete the addition/removal and subsequent calls will fail as the file was then already created/removed.

> B5: Does your implementation allow a directory to be removed if it is open by
> a process or if it is in use as a process's current working directory?  If so,
> what happens to that process's future file system operations?  If not, how do
> you prevent it?

No our implementation doesn't allow removing a directory that is currently opened. We prevent this by checking if the file is a directory and if yes, if its open count is greater than 1, as it is currently opened in the `dir_remove()` function. `dir_remove()` will in this case return false.

### Rationale

> B6: Explain why you chose to represent the current directory of a process the
> way you did.

We chose to save the current directory as a directory pointer in the current thread as it enables easy access to it. Additionally, having it already saved as a directory instead of an inode makes it easier to open it for subsequent calls in `filesys_lookup()` or in `process_execute()` when called from the `exec()` syscall, as the CWD will be inherited.


## Buffer Cache

### Data Structure

> C1: Copy here the declaration of each new or changed `struct` or `struct`
> member, global or static variable, `typedef`, or enumeration.  Identify the
> purpose of each in 25 words or less.

```c
/* Buffer cache entry. */
struct buffer_cache
{
	block_sector_t sector;			 /* Sector number of disk location. */
	uint8_t data[BLOCK_SECTOR_SIZE]; /* Buffer cache data. */
	bool dirty;						 /* True if data has been modified. */
	bool accessed;					 /* True if data has been accessed. */
	bool is_metadata;				 /* True if the data is metadata. */
	unsigned used_cnt;				 /* Number of openers. */
};

static struct buffer_cache buffer_cache[BUFFER_CACHE_SIZE];

/* Function prototypes. */
static void buffer_cache_read_ahead(void *aux UNUSED); /* Used for read_ahead thread */
static void buffer_cache_write_behind(void *aux UNUSED); /* Used for write_behind thread */
```


### Algorithm

> C2: Describe how your cache replacement algorithm chooses a cache block to
> evict.

Our algorithm is a LRU approximation which loops through the array `buffer_cache` to find a good candidate. We will skip the current element if it is currently used. If its accessed bit is set to true, we reset it to false. If the entry we find is a metadata entry (=inode), we save it in the variable `metadata_entry`. If we finish an entire loop without finding a suitable candidate, we use `metadata_entry` if it exists. Otherwise we wait on the condition that a buffer cache is available, which is signaled when a buffer cache is freed.

> C3: Describe your implementation of write-behind.

write-behind is implemented as a thread that is always running. It runs a loop that will sleep for 1000 ms, then will write all dirty buffer caches to disk and reset their dirty bit.

> C4: Describe your implementation of read-ahead.

read-ahead is implemented as a thread that is always running. When `buffer_cache_read()` is called, the next sector will be added to the array `read_ahead_sectors` where the next sectors to be read are saved. By using front and tail variables, we know where we need to add the next sector and from where to take the next sector to read ahead. While the number of read ahead sectors is 0, we will wait with a condition, which is signaled when `buffer_cache_read()` is called. Additionally, concurrent changes to `read_ahead_sectors` are solved by acquiring a lock to it each time changes are made.

### Algorithm

> C5: When one process is actively reading or writing data in a buffer cache
> block, how are other processes prevented from evicting that block?

Each buffer cache has a used_cnt, which saves the number of processes that are currently using the block. As long as this number is greater than 0, the block will not be evicted.

> C6: During the eviction of a block from the cache, how are other processes
> prevented from attempting to access the block?

To make changes to the buffer cache, processes must acquire the `buffer_cache_lock`. Therefore, concurrent accesses are prevented as other processes will need to wait on the lock to release.

### Rationale

> C7: Describe a file workload likely to benefit from buffer caching, and
> workloads likely to benefit from read-ahead and write-behind.

Buffer caching: Processes that are repeatedly reading from the same disk locations, for example the code blocks of an executable that is small enough to stay in the buffer cache.

read-ahead: Sequential reads will benefit greatly from it as the next access will be prepared asynchronously.

write-behind: If the same blocks are modified several times shortly one after another, the additional writes will be saved and thus provide a better efficiency.

## Survey Questions

Answering these questions is optional, but it will help us improve the course in
future semester.  Feel free to tell us anything you want--these questions are
just to spur your thoughts.  You may also choose to respond anonymously in the
course evaluations at the end of the quarter.

> In your opinion, was this assignment, or any one of the three problems in it,
> too easy or too hard?  Did it take too long or too little time?

> Did you find that working on a particular part of the assignment gave you
> greater insight into some aspect of OS design?

> Is there some particular fact or hint we should give students in future
> quarters to help them solve the problems?  Conversely, did you find any of our
> guidance to be misleading?

> Do you have any suggestions for the TAs to more effectively assist students in
> future quarters?

> Any other comments?
