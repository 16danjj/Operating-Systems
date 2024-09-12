# Design Document: Project 1 - Threads

## Preliminaries

> If you have any preliminary comments on your submission, notes for the
> TAs, or extra credit, please give them here.

> Please cite any offline or online sources you consulted while
> preparing your submission, other than the Pintos documentation, course
> text, lecture notes, and course staff.



## Alarm Clock

### Data Structures

> A1: Copy here the declaration of each new or changed `struct` or
> `struct` member, global or static variable, `typedef`, or
> enumeration.  Identify the purpose of each in 25 words or less.
>
`struct members`
`thread.h`
```c
struct thread {
	/* New elements */
  int64_t wake_time;      /* Time to wake up in ticks */
}
```

`static variables`
`thread.c`
```c
static struct list sleep_list; /*This list contains threads that have been put to sleep. 
It's also where the timer_interrupt checks for threads that should be woken up*/
```

### Algorithms

> A2: Briefly describe what happens in a call to `timer_sleep()`,
> including the effects of the timer interrupt handler.
>
In a call to `timer_sleep()`, the current thread's `wake_time` is set to be the given sleep ticks plus the 
current ticks. Then a call to `thread_sleep()` is made where interrupts are disabled, the thread is
inserted into the `sleep_list` before calling `thread_block()`

In the timer interrupt handler, check the `sleep_list` to see if any thread needs to be woken up. If yes, 
call `thread_wake()`, where the thread is removed from the `sleep_list`. Then `thread_unblock()` is called,
which transitions the thread back to the `ready_list`

> A3: What steps are taken to minimize the amount of time spent in
> the timer interrupt handler?
> 
`sleep_list` is ordered as a list, by earliest wake time first. This way the threads that 
need to be woken up first are at the beginning of the queue. We return as soon as we get
to the point in the queue where the threads don't need to be woken up anymore or if the `sleep_list`
is empty.

### Synchronization

> A4: How are race conditions avoided when multiple threads call
> `timer_sleep()` simultaneously?
> 
We made the process of adding the thread to the `sleep_list` and then putting the thread to sleep
atomic, so even if multiple threads call `timer_sleep()` simultaneously, the executing of these 
processes will not be interleaved.

> A5: How are race conditions avoided when a timer interrupt occurs
> during a call to `timer_sleep()`?
> 
Interrupts are disabled for the time it takes to add the thread to the `sleep_list` and then put the 
thread to sleep. This means there will never be a case where the thread is added to the `sleep_list`,
but never actually put to sleep. 

### Rationale

> A6: Why did you choose this design?  In what ways is it superior to
> another design you considered?
>
The use of a queue avoids the previous need of a busy waiting while loop. Furthermore, the use of 
an ordered `sleep_list` by minimum wake up time, saves time in the interrupt handler to only check the 
top thread rather than iterating throughout the list. We did not consider other alternatives.



## Producer-Consumer

### Synchronization

> B1: How does your solution guarantee that consumers will hold until
> there is something to consume in the buffer?
>
 Consumers will hold until there is something in the buffer due to the while 
 loop which checks if the buffer is not empty. As long it is empty, 
 the consumer will hold.

> B2: How does your solution guarantee that producers will hold until
> there is some free space in the buffer?
>
 Producers will hold until there is free space in the buffer due to the while 
 loop which checks if the buffer is full. As long it is full, 
 the producer will hold.

> B3: How does your solution preserve a FIFO semantics i.e., the first
> character produced will be the first to be consumed?
>
By having the two variables `in` and `out` in the procon struct, we can save 
where a new character should be added and where an existing character should 
be removed. This way, we follow the FIFO semantics as the first produced 
character, which is saved in 0, which is the same for `ìn` and `out`, will 
also then be the first removed character, as `out` is only changed when 
removing a character

### Rationale

> B4: Give an intuition for why your program preserves safety.
>
Our program preserves safety through locks and conditions. A lock is aquired
before making changes to the buffer. This ensures that there are no race 
conditions or simultaneous accesses to the buffer. Additionally, the 
conditions ensure that the producers/consumers don't under/overflow the buffer.

> B5: Why did you choose this design? Did you consider other design
> alternatives? In what ways is it superior to another design you considered?

We chose this design as it was the most straightforward implementation. We had this implementation in the lectures, it was also the easiest one to implement.
We did not consider other alternatives.


## Priority Scheduling

### Data Structures

> C1: Copy here the declaration of each new or changed `struct` or
> `struct' member, global or static variable, `typedef`, or
> enumeration.  Identify the purpose of each in 25 words or less.
>
`struct members`
`thread.h`
```c
struct thread {
	/* New elements */
	struct lock *waiting_lock;          /* Lock on which the thread is waiting to be released. */
	struct list donations;              /* List of donations of priority from other threads. */
	struct list_elem donationelem;      /* List element for donations list. */
	int base_priority;                  /* Base priority of thread. */
}
```

> C2: Explain the data structure used to track priority donation.
> Use ASCII art to diagram a nested donation.  (Alternately, submit a
> .png file.)

We use the new elements that were added to the thread struct to track nested donations.
When a thread is blocked, it donates its priority to the lock holder, where the corresponding lock is saved in `waiting_lock`. The blocked thread is then saved in the `donations` list of the lock holder with the `donationelem`, such that he knows which thread donated which priority. 

```
Thread H (base_priority 63, priority 63, waiting_lock A[held by: M], donations [])
  |
  | Donates to
  v
Thread M (base_priority 31, priority 63, waiting_lock B[held by: L], donations [H (priority 63)])
  |
  | Donates to
  v
Thread L (priority 1) (base_priority 1, priority 63, waiting_lock NULL, donations [M (priority 5)])
```

Thread L (priority = 1) runs and acquires a lock B. Then Thread M (priority = 31) is scheduled, acquires a lock A, then tries to acquire lock B. Thread M is blocked and donates its priority to L (priority = 31). Thread H (priority = 63) is scheduled, tries to acquire lock A, is blocked and donates its priority to Thread M (priority = 63), which in turn donates its new higher priority to the holder of its waiting lock, L (priority = 63).

### Algorithms

> C3: How do you ensure that the highest priority thread waiting for
> a lock, semaphore, or condition variable wakes up first?
>
The threads are inserted in the order of the highest priority.
Additionally, the waiting lists are sorted before popping the first element,
as the priorities may have changed in between the insertion and popping due to donations. 

> C4: Describe the sequence of events when a call to `lock_acquire()`
> causes a priority donation.  How is nested donation handled?
>
When `lock_acquire()` is called and the current lock already has a holder, it means that the current_thread will be blocked.
Therefore, the current thread will be added to the `donations` list of the lock holder. Additionally, the lock will be set as the `waiting_lock` for the current thread.
In `sema_down()`, as the sema value will be 0, `thread_update_priority()` will be called, where the priority donations of other threads will be applied. This in turn calls `thread_donate_priority()`. In this function, the current thread donates its current priority to the thread that holds the lock the current thread is waiting on, which will be propagated to the nested holders.

> C5: Describe the sequence of events when `lock_release()` is called
> on a lock that a higher-priority thread is waiting for.
>
When `lock_release()` is called, the donation of the higher priority thread that was blocked by this lock that the current thread received will be removed by `thread_remove_donations()`.
Afterwards, a call to `thread_update_priority()` will update the current threads priority to its priority without this donation. In `sema_up()`, the waiting thread with the higher priority will be unblocked and added to the `ready_list`. Afterwards, the current thread will yield. As the waiting thread has a higher priority and is in the `ready_list`, it will be executed.

### Synchronization

> C6: Describe a potential race in `thread_set_priority()` and explain
> how your implementation avoids it.  Can you use a lock to avoid
> this race?

If a thread donates its priority to a thread during `thread_set_priority()` in the function call of `thread_update_priority()` just before `thread_donate_priority()` is called, the previously calculated priority is now wrong.
We avoid this race condition by disabling interrupts during `thread_set_priority()`.

A lock can be used to avoid this race condition, but it would create more overhead as locks themselves disable interrupts to perform their task. Disabling interrupts is a more direct and efficient way to ensure atomicity in this case.

### Rationale

> C7: Why did you choose this design?  In what ways is it superior to
> another design you considered?

We chose this design as it was the most straightforward way to implement a priority scheduler. By saving the priority donations in an ordered list, retrieval of the highest donation is easy.
Additionally, by saving the lock a thread is waiting on, it is easy to donate the priority to the holder of the lock, as a direct connection is there.
Finally, by always first updating the priority of the thread from its donations and then only donating its priority, we ensure that the order of priority donation is correct.
We first tried to implement a priority scheduler without a donation list, but this proved impossible when donations needed to be removed.

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
