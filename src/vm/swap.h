#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stddef.h>
#include "devices/block.h"

void swap_init(void);
block_sector_t swap_out(void *kaddr);
void swap_in(void *kaddr, block_sector_t swap_id);

#endif