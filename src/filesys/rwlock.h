#include "threads/synch.h"

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

void rwlock_init(struct rw_lock *lock);
void rwlock_acquire_read(struct rw_lock *lock);
void rwlock_release_read(struct rw_lock *lock);
void rwlock_acquire_write(struct rw_lock *lock);
void rwlock_release_write(struct rw_lock *lock);