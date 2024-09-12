# Design Document: Project 3 - Virtual Memory


## Preliminaries

> If you have any preliminary comments on your submission, notes for the
> TAs, please give them here.

> Please cite any offline or online sources you consulted while
> preparing your submission, other than the Pintos documentation, course
> text, lecture notes, and course staff.



## Page Table Management

### Data Structures

> A1: Copy here the declaration of each new or changed `struct' or
> `struct' member, global or static variable, `typedef', or
> enumeration.  Identify the purpose of each in 25 words or less.

```c
/* Type of a page to differentiate between executables and other other types of files */
enum page_type
{
  VM_BIN, /* For ELF files */
  VM_ANON /* For stack pages and zero initialized pages */
};

/* Supplemental page table entry which contains additional information about pages */
struct page
{                             
  void *vaddr;                /* Virtual address of the page */
  struct frame *frame;        /* Frame that is used by the page */
  struct hash_elem hash_elem; /* For supplemental page hash table. */
  struct thread *thread;      /* Owner process. */
  /* File stuff */
  struct file *file; /* File to be mapped. */
  off_t ofs;         /* Offset in file. */
  size_t read_bytes; /* Bytes to read from file. */
  /* Additional page information */
  bool writable;          /* True if writable, false if read-only. */
  bool pinned;            /* True if pinned, false otherwise. */
  bool swapped;           /* True if swapped out, false otherwise. */
  block_sector_t swap_id; /* Swap ID of swapped data */
  enum page_type type;    /* Type of a page. */
  struct lock lock;       /* Lock for page to prevent simulatneous acccess to the page */
};
```

### Algorithms

> A2: In a few sentences, describe your code for accessing the data
> stored in the SPT about a given page.

To access the data stored in the STP, we need the virtual address of the page.
By using the function `page_lookup(vaddr)`, we receive the pointer to the supplemental page table entry.
As the SPT is a hash table, `page_lookup(vaddr)` compares the entries on their virtual address and returns the one that has the correct virtual address.

> A3: How does your code coordinate accessed and dirty bits between
> kernel and user virtual addresses that alias a single frame, or
> alternatively how do you avoid the issue?

We only access user data through user virtual address to avoid
this issue.

### Synchronization

> A4: When two user processes both need a new frame at the same time,
> how are races avoided?

We avoid this race by having a lock for `frame_table`. Each time a frame needs to be evicted, it acquires `frame_lock`. Thus only one process at a time can evict a frame and get the new frame.
If there is no need to evict a frame for both, we use `palloc_get_page()` to get a new frame.
`palloc_get_page()` uses an exclusive lock to make sure that only one process gets a new frame.

### Rationale

> A5: Why did you choose the data structure(s) that you did for
> representing virtual-to-physical mappings?

We chose to use the struct `page` to extend the page entries of existing page directory.
As the existing implementation did not take care of lazily loaded pages and swapped pages,
adding such a supplemental page table makes sense to be able to find all pages of a process,
as only pages in memory and which possess a frame can be found with the original page table.



## Paging To and From Disk

### Data Structures

> B1: Copy here the declaration of each new or changed `struct' or
> `struct' member, global or static variable, `typedef', or
> enumeration.  Identify the purpose of each in 25 words or less.

### Algorithms

> B2: When a frame is required but none is free, some frame must be
> evicted.  Describe your code for choosing a frame to evict.

> B3: When a process P obtains a frame that was previously used by a
> process Q, how do you adjust the page table (and any other data
> structures) to reflect the frame Q no longer has?

> B4: Explain your heuristic for deciding whether a page fault for an
> invalid virtual address should cause the stack to be extended into
> the page that faulted.

### Synchronization

> B5: Explain the basics of your VM synchronization design.  In
> particular, explain how it prevents deadlock.  (Refer to the
> textbook for an explanation of the necessary conditions for
> deadlock.)

The frame table, which holds all open frames, has a lock to prevent simultaneous changes.
Each process has a supplemental page table. For each supplemental page table entry, there
exists a lock.

Each functionality of the frame table has a wrapped function call which takes care of possible synchronization issues. If frames are searched or changes are made to the frame table, the lock is acquired and only released once the changes are finished.

Additionally, to prevent changes made to the supplemental page table entry, we also acquire its lock when evicting its frame, when loading the data of the page to prevent changes to the data and when destroying the supplemental page table.

The necessary conditions for deadlock are the following:
1. Mutual exclusion
2. Hold and wait
3. No preemption
4. Circular wait

Therefore, we prevent deadlock by ensuring mutual exclusion with our locks.
As all four conditions are necessary, deadlocks can not occur.

> B6: A page fault in process P can cause another process Q's frame
> to be evicted.  How do you ensure that Q cannot access or modify
> the page during the eviction process?  How do you avoid a race
> between P evicting Q's frame and Q faulting the page back in?

> B7: Suppose a page fault in process P causes a page to be read from
> the file system or swap.  How do you ensure that a second process Q
> cannot interfere by e.g. attempting to evict the frame while it is
> still being read in?

> B8: Explain how you handle access to paged-out pages that occur
> during system calls.  Do you use page faults to bring in pages (as
> in user programs), or do you have a mechanism for "locking" frames
> into physical memory, or do you use some other design?  How do you
> gracefully handle attempted accesses to invalid virtual addresses?

Our function `page_load_buffer_pages()` is called before we check the validity of the buffer pointer during system calls.
This function checks if the buffer address is a valid pointer to a page that has been initialized. 

If it is, the page(s) are loaded into memory.
After they have been loaded, their `pinned` attribute is set to true, in which case the eviction function will skip these frames.
After the system call has been done, the `pinned` is set to false and the frames can be evicted again.

If the address is not a valid pointer, the process is exited with an error code of `-1`.

### Rationale

> B9: A single lock for the whole VM system would make
> synchronization easy, but limit parallelism.  On the other hand,
> using many locks complicates synchronization and raises the
> possibility for deadlock but allows for high parallelism.  Explain
> where your design falls along this continuum and why you chose to
> design it this way.



## Survey Questions

Answering these questions is optional, but it will help us improve the
course in future quarters.  Feel free to tell us anything you
want--these questions are just to spur your thoughts.  You may also
choose to respond anonymously in the course evaluations at the end of
the quarter.

> In your opinion, was this assignment, or any one of the three problems
> in it, too easy or too hard?  Did it take too long or too little time?

> Did you find that working on a particular part of the assignment gave
> you greater insight into some aspect of OS design?

> Is there some particular fact or hint we should give students in
> future quarters to help them solve the problems?  Conversely, did you
> find any of our guidance to be misleading?

> Do you have any suggestions for the TAs to more effectively assist
> students, either for future quarters or the remaining projects?

> Any other comments?
