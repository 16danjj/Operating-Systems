#include "filesys/rwlock.h"

void rwlock_init(struct rw_lock *lock)
{
    lock->active_writers = 0;
    lock->active_readers = 0;
    lock->waiting_readers = 0;
    lock->waiting_writers = 0;

    lock_init(&lock->mutex);
	cond_init(&lock->OK_read);
	cond_init(&lock->OK_write);
}

void rwlock_acquire_read(struct rw_lock *lock)
{
    lock_acquire(&lock->mutex);
    while (lock->active_writers > 0 || lock->waiting_writers > 0) {
        lock->waiting_readers+=1;
        cond_wait(&lock->OK_read, &lock->mutex);
        lock->waiting_readers-=1;
    }
    lock->active_readers +=1;
    lock_release(&lock->mutex);
}

void rwlock_release_read(struct rw_lock *lock)
{
    lock_acquire(&lock->mutex);
    lock->active_readers-=1;
    if(lock->active_readers == 0 && lock->waiting_writers > 0)
    {
        cond_signal(&lock->OK_write, &lock->mutex);
    }

    lock_release(&lock->mutex);
}

void rwlock_acquire_write(struct rw_lock *lock)
{
    lock_acquire(&lock->mutex);
    while (lock->active_writers > 0 || lock->active_readers > 0) {
        lock->waiting_writers+=1;
        cond_wait(&lock->OK_write, &lock->mutex);
        lock->waiting_writers-=1;
    }
    lock->active_writers +=1;
    lock_release(&lock->mutex);
}

void rwlock_release_write(struct rw_lock *lock)
{
    lock_acquire(&lock->mutex);
    lock->active_writers-=1;
    if(lock->waiting_writers > 0)
    {
        cond_signal(&lock->OK_write, &lock->mutex);
    }
    else 
    {
        cond_broadcast(&lock->OK_read, &lock->mutex);
    }

    lock_release(&lock->mutex);
}