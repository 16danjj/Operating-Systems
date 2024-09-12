#include "vm/swap.h"
#include "threads/vaddr.h"
#include "devices/block.h"
#include "lib/kernel/bitmap.h"
#include "lib/stddef.h"
#include "threads/synch.h"

#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

/* Swap map which is used to keep track of the swap slots */
struct bitmap *swap_map;
/* Swap block which is used to store the swapped out pages */
struct block *swap_block;
/* Lock to prevent simultaneous access to the swap block */
struct lock swap_lock;

/* Initialize the swap block and swap map */
void swap_init(void)
{
    swap_block = block_get_role(BLOCK_SWAP);
    if (swap_block == NULL)
    {
        printf("swap_block creation failed!\nSWAP DISABLED\n");
        swap_map = bitmap_create(0);
    }
    else
    {
        swap_map = bitmap_create(block_size(swap_block) / SECTORS_PER_PAGE);
    }

    if (swap_map == NULL)
        PANIC("couldn't create bitmap of swap block\n");

    bitmap_set_all(swap_map, 0);
    lock_init(&swap_lock);
}

/* Swap out the page to the swap block */
block_sector_t
swap_out(void *kaddr)
{
    block_sector_t swap_id;
    lock_acquire(&swap_lock);
    swap_id = bitmap_scan_and_flip(swap_map, 0, 1, false);
    lock_release(&swap_lock);

    if (swap_id == BITMAP_ERROR)
    {
        PANIC("SWAP SPACE FULL!");
    }

    for (int i = 0; i < SECTORS_PER_PAGE; i++)
    {
        block_write(swap_block, swap_id * SECTORS_PER_PAGE + i, kaddr);
        kaddr += BLOCK_SECTOR_SIZE;
    }

    return swap_id;
}

/* Swap in the page from the swap block */
void swap_in(void *kaddr, block_sector_t swap_id)
{
    ASSERT(swap_id != BLOCK_SECTOR_NULL);
    for (int i = 0; i < SECTORS_PER_PAGE; i++)
    {
        block_read(swap_block, swap_id * SECTORS_PER_PAGE + i, kaddr);
        kaddr += BLOCK_SECTOR_SIZE;
    }
    bitmap_reset(swap_map, swap_id);
}