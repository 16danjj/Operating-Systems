# Design Document: Project 2 - User Programs

## Preliminaries

> If you have any preliminary comments on your submission, notes for the
> TAs, or extra credit, please give them here.

> Please cite any offline or online sources you consulted while
> preparing your submission, other than the Pintos documentation, course
> text, lecture notes, and course staff.



## Argument Passing

### Data Structures

> A1: Copy here the declaration of each new or changed `struct' or
> `struct' member, global or static variable, `typedef', or
> enumeration.  Identify the purpose of each in 25 words or less.

None.

### Algorithms

> A2: Briefly describe how you implemented argument parsing.  How do
> you arrange for the elements of argv[] to be in the right order?
> How do you avoid overflowing the stack page?

These are the steps we followed for argument passing as listed here below :
1. Tokenize the filename using `strtok_r()` and store each token into an array. 
2. These tokens are then pushed into the stack in reverse order, from last to first
3. The address corresponding to each argument is right away stored in `argv`
4. Perform memory alignment if needed after pushing arguments onto the stack as pointers must be stored at addresses that are word aligned
5. Push the last argument which consists of four bytes of 0's
6. The addresses are then pushed onto stack in reverse order
7. Push address of `argv[0]` onto the stack
8. Push `argc` (argument count) onto the stack
9. Push return address

We avoid overflowing the stack page by allocating a pointer `fn_copy`of pagesize (4kB) and 
copying the filename to `fn_copy` before pushing it onto the stack.

### Rationale

> A3: Why does Pintos implement strtok_r() but not strtok()?

`strtok_r()` is thread safe, meaning it can be called from multiple threads simultaneously. `strtok_r()` takes
an extra argument that maintains context between successive calls that parse the same string. 

> A4: In Pintos, the kernel separates commands into a executable name
> and arguments.  In Unix-like systems, the shell does this
> separation.  Identify at least two advantages of the Unix approach.

1. It simplifies the kernel, as it no longer has to parse or validate arguments and eliminate bugs that might cause problems
2. Shortens the time inside the kernel


## System Call

### Data Structures

> B1: Copy here the declaration of each new or changed `struct' or
> `struct' member, global or static variable, `typedef', or
> enumeration.  Identify the purpose of each in 25 words or less.

```c
struct thread {
	/* New elements */
	int exit_status;                    /* Exit status of the thread */    
	struct file ** fd_table;            /* Open files of thread */   
	struct list childrens;              /* List of child threads */
	struct file *executable;            /* Executable file of the thread */
	struct process *process;            /* Process of the thread */
	struct semaphore wait_for_exit;     /* Semaphore to wait for a thread to exit*/
	struct semaphore wait_for_load;     /* Semaphore to wait for a thread to load */
};

/* Process structure to keep track of a process that is coupled to a thread */
struct process {
	pid_t pid;                          /* Process identifier. */
	struct list_elem elem;              /* List element for process list. */
	struct thread *thread;              /* Thread of the process. */
	struct thread *parent;              /* Parent thread of the process. */
	bool is_waiting;                    /* Waiting for child to exit */
	bool successful_load;               /* If process successfully loaded */
	int exit_status;                    /* Exit status of the process */
	enum thread_status thread_status;   /* Status of the process */
	bool killed;                        /* True if the process was killed */
};

typedef int pid_t; 										/* Process identifier type (same as tid_t) */
```

> B2: Describe how file descriptors are associated with open files.
> Are file descriptors unique within the entire OS or just within a
> single process?

Each user process will store pointers to its open files in an array `fd_table` upto size 
`MAX_OPEN_FILES` (128). To allocate a new file descriptor (fd), we iterate over the `fd_table` until 
`fd_table[fd] == NULL`. Note that the fd number starts from 2 upto `MAX_OPEN_FILES`, with 0 and 1 reserved for 
STDIN and STDOUT. 

Since each process maintains its own array, the file descriptors are local/unique to a single process
and not within the entire OS.

### Algorithms

> B3: Describe your code for reading and writing user data from the
> kernel.

Reading : 

We first check if the buffer and buffer + size are valid pointers. If fd is `STDIN_FILE`, 
we take the input from the keyboard using `input_getc()`, and copy the received input into 
the buffer. We save the number of bytes received from the keyboard to be later returned when the 
function returns. Else, the thread has to acquire a lock since we are about to call file system code
which needs to be treated as a critical section. Then, find the open file according to the fd 
number, and then call `file_read()` to read the file before returning the actual bytes read. 

Writing : 

Just like the read system call, we need to first validate the buffer and buffer + size. If fd is `STDOUT_FILE`, 
we write to the console using `putbuf()` from the buffer upto size bytes. Else, the thread has to acquire a 
lock for the same reason as mentioned previously. Then, find the open file according to the fd 
number, and then call `file_write()` to write to the file from the buffer before returning the actual bytes written.

> B4: Suppose a system call causes a full page (4,096 bytes) of data
> to be copied from user space into the kernel.  What is the least
> and the greatest possible number of inspections of the page table
> (e.g. calls to pagedir_get_page()) that might result?  What about
> for a system call that only copies 2 bytes of data?  Is there room
> for improvement in these numbers, and how much?

If a system call causes a full page of data to be copied from user space into the kernel,
at least 1 `pagedir_get_page()` is needed, if the data falls into a single page.
At most 2 `pagedir_get_page()` are needed, which is the case if the data spans across page
boundaries.

For 2 bytes of data the same is true, as we still need one inspection if the 2 bytes fall onto
the same page, or 2 inspections if one byte in on one page and the other on another one.

These numbers can be improved by caching page table entries, for example with a translation lookaside buffer (TLB).
If the data is on pages saved in the TLB, we would need zero inspections, as the TLB would return the reference of the pages
without a need for inspection. 


> B5: Briefly describe your implementation of the "wait" system call
> and how it interacts with process termination.

The `wait` system call returns the result of the function `process_wait(pid)`.
`process_wait(pid_t child_pid)` then gets the child process of the thread with this 
specific pid. If there is none or if `process_wait(pid)` was already called once 
on this child, it will return -1.
The function then checks if the process already finished by checking the `killed` variable.
If it already finished, set the status to the exit status of the child, else it will wait for 
the child to terminate by calling `sema_down()` on the semaphore of the child. If the child process 
is exiting, it will call `process_exit()`, where it will call `sema_up()` at the end of the function,
which signals to the parent thread that it has finished. The parent thread can then get the exit status 
and frees all resources that the process had allocated.

> B6: Any access to user program memory at a user-specified address
> can fail due to a bad pointer value.  Such accesses must cause the
> process to be terminated.  System calls are fraught with such
> accesses, e.g. a "write" system call requires reading the system
> call number from the user stack, then each of the call's three
> arguments, then an arbitrary amount of user memory, and any of
> these can fail at any point.  This poses a design and
> error-handling problem: how do you best avoid obscuring the primary
> function of code in a morass of error-handling?  Furthermore, when
> an error is detected, how do you ensure that all temporarily
> allocated resources (locks, buffers, etc.) are freed?  In a few
> paragraphs, describe the strategy or strategies you adopted for
> managing these issues.  Give an example.

The validity of a pointer is checked by validating the address of the pointer with `check_if_valid_ptr()`. This is done by ensuring that it is below PHYS_BASE, that the address exists in a page of the current thread and that the address is not below the start of virtual addresses (`0x08048000`).


When a system call is handled, we first validate that the next 4 bytes of the esp, where the system call
number is saved, are valid.
From there on, we go to the corresponding system call number and check the validity of the arguments 
by checking all bytes of the arguments with `check_if_valid_ptr()`.
Once the arguments are ensured to be correct, we check for errors in the arguments, if they happen to be pointers.
Buffers are validated by their length that they are written to with `check_if_valid_bytes()`, strings are validated
by checking all bytes till a null terminator is encountered with `check_if_valid_string()`.

If all those checks are passed, the systemcall can start.
If a check does not pass, the process will call `exit(-1)`, 
which will free all of its previously allocated ressources.
As no locks are used or memory allocated before finishing 
between calling the system call handler and encountering an 
error in the our error handling, we also don't need to 
handle more freeing of ressources or locks.

### Synchronization

> B7: The "exec" system call returns -1 if loading the new executable
> fails, so it cannot return before the new executable has completed
> loading.  How does your code ensure this?  How is the load
> success/failure status passed back to the thread that calls "exec"?

`exec(cmd)` returns `process_execute(cmd)`, where the child thread is created.
After `thread_create()` is called, which returns a `tid`, we find the 
newly created child. If the child creation was impossible, we return -1. 
The parent thread then calls `sema_down()` on the semaphore of 
the child thread. The child thread then calls `sema_up()` once it has finished loading.
The success of the load is saved in the variable `successful_load` of the process struct.
Once the parent thread resumes after the `sema_down()` call, it checks if the load was successful
and either returns -1 if unsuccessful or the tid if successful.

> B8: Consider parent process P with child process C.  How do you
> ensure proper synchronization and avoid race conditions when P
> calls wait(C) before C exits?  After C exits?  How do you ensure
> that all resources are freed in each case?  How about when P
> terminates without waiting, before C exits?  After C exits?  Are
> there any special cases?

> B8.1 :How do you ensure proper synchronization and avoid race conditions when P calls wait(C) before C exits?

We ensure proper synchronization by using a semaphore such that P is blocked in the wait(C) call till C exits.

> B8.2: After C exits?

We save if a process has terminated in the process variable `killed`, which is checked before calling `sema_down()`.
If C already exited, we directly give back the exit code of C.

> B8.3: How do you ensure that all resources are freed in each case?
> How about when P terminates without waiting, before C exits?

If P terminates without waiting, before C exits, P calls `process_exit()`, which signals all of its children
that the parent process finished by setting the `parent` variable to `NULL`. Once C then exits, it checks if its parent
is `NULL`, in which case it will free its process struct. 
The thread struct is already freed as part of the `thread_exit()` call, which happens either way.

> B8.4: After C exits?

When C exits, its thread struct is freed, but not the process struct. C will then free C, as it is already finished, when P exits.

> B8.5: Are there any special cases?

A special case would be if both P and C exit at the same time.
This would make no problems for our implementation, as we free the process structs of children that have already finished in `process_exit()` if C runs a bit earlier than P. In the other case, the parent of C would be null, so C would free itself.


### Rationale

> B9: Why did you choose to implement access to user memory from the
> kernel in the way that you did?

We chose to verify the validity of a user-provided pointer, then
dereference it, as it was the simplest way to handle memory user access.
By checking the validity of the address beforehand, we can handle the freeing of the 
resource in an easier way. Even if it is a bit slower than checking for pagefaults,
the simplicity and thus less chance of errors happening was why we chose this approach.

> B10: What advantages or disadvantages can you see to your design
> for file descriptors?

Advantages:
1. Using an array to store open files is a simple approach
2. Opening and closing files won't be time consuming as we search for entries in the array using the appropriate fd as the index

Disadvantages:
1. Memory is wasted if a process does not open 128 files
2. Finding space for new open file in the array is O(n)

> B11: The default tid_t to pid_t mapping is the identity mapping.
> If you changed it, what advantages are there to your approach?

We did not change it.

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
