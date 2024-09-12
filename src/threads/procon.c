#include "threads/procon.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* Initialize producer-consumer instance. */
void
procon_init (struct procon *pc, unsigned int buffer_size)
{
  // Allocate buffer
  pc->buffer = (char *) malloc (buffer_size);
  if (pc->buffer == NULL)
    PANIC ("out of memory allocating buffer");

  // Initialize synchronization primitives
  lock_init (&pc->lock);
  cond_init (&pc->not_empty);
  cond_init (&pc->not_full);

  // Initialize variables
  pc->size = buffer_size;
  pc->in = 0;
  pc->out = 0;
  pc->current = 0;
}

/* Put a character into the bounded buffer. Wait if the buffer is full. */
void
procon_produce (struct procon *pc, char c)
{
  // Acquire lock
  lock_acquire(&pc->lock);
  // Check if buffer is full
  while (pc->current == pc->size)
  {
    // If full, wait for not full condition
    cond_wait(&pc->not_full, &pc->lock);
  }
  
  // Add character to buffer and update variables
  pc->buffer[pc->in] = c;
  pc->in = (pc->in + 1) % pc->size;
  pc->current++;
  // Signal not empty condition
  cond_signal(&pc->not_empty, &pc->lock); 
  // Release lock
  lock_release(&pc->lock);

}

/* Pull a character out of the buffer. Wait if the buffer is empty. */
char
procon_consume (struct procon *pc)
{
  // Acquire lock
  lock_acquire(&pc->lock);
  // Check if buffer is empty
  while (pc->current == 0)
  {
    // If empty, wait for not empty condition
    cond_wait(&pc->not_empty, &pc->lock);
  }
  // Remove character from buffer and update variables
  char c = pc->buffer[pc->out];
  pc->out = (pc->out + 1) % pc->size;
  pc->current--;
  // Signal not full condition
  cond_signal(&pc->not_full, &pc->lock); 
  // Release lock
  lock_release(&pc->lock);
  return c;
}
