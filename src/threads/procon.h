#ifndef THREADS_PROCON_H
#define THREADS_PROCON_H

#include "threads/synch.h"
#include <stdint.h>

/* State for producer-consumer mechanism */
struct procon
  {
    struct lock lock;           /* Lock for Mutual exclusion */
    struct condition not_empty; /* Not empty condition */
    struct condition not_full;  /* Not full condition */
    char *buffer;               /* Bounded buffer */
    unsigned size;              /* Buffer size */
    unsigned in;                /* Next free space in buffer */
    unsigned out;               /* Next consumed space in buffer */
    unsigned current;           /* Number of currently used spaces in buffer */
  };

void print_procon (struct procon *pc);

void procon_init (struct procon *, unsigned buffer_size);
void procon_produce (struct procon *, char c);
char procon_consume (struct procon *);

#endif /* threads/procon.h */
