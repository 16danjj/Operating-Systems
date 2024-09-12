#include "vm/frame.h"
#include "threads/thread.h"
#include "vm/page.h"
#include "vm/swap.h"
#include <stdlib.h>
#include "userprog/pagedir.h"
#include "vm/swap.h"

struct list frame_table;
struct lock frame_lock;
struct semaphore io_sema;

struct list_elem *last_frame;

/* Wait for I/O to complete. */
void frame_io_wait(void)
{
	sema_down(&io_sema);
}

/* I/O is done. */
void frame_io_done(void)
{
	sema_up(&io_sema);
}

/* Initialize the frame table and lock. */
void frame_init(void)
{
	list_init(&frame_table);
	lock_init(&frame_lock);
	sema_init(&io_sema, 1);
}

/* Returns true if a frame is equal to the given kernel address. */
bool frame_is_equal(const struct list_elem *a, const struct list_elem *b,
					void *aux)
{
	struct frame *frame = list_entry(a, struct frame, elem);
	void *kaddr = aux;
	return frame->kaddr == kaddr;
}

/* Free the frame and remove it from the frame table. */
void frame_free_frame(struct frame *entry)
{
	lock_acquire(&frame_lock);
	lock_acquire(&entry->lock);
	if (list_find(&frame_table, NULL, &frame_is_equal, entry->kaddr))
	{
		list_remove(&entry->elem);
		void *kaddr = entry->kaddr;
		palloc_free_page(kaddr);
	}
	entry->page->frame = NULL;

	free(entry);
	lock_release(&frame_lock);
}

/* Initialize a frame with the given kernel address. */
struct frame *
frame_init_frame(void *kaddr)
{
	struct frame *entry = calloc(1, sizeof(struct frame));
	entry->kaddr = kaddr;
	entry->page = NULL;
	lock_init(&entry->lock);

	lock_acquire(&frame_lock);
	list_push_back(&frame_table, &entry->elem);
	lock_release(&frame_lock);

	return entry;
}

/* Find a frame that can be evicted. */
struct frame *
frame_get_evicted_frame(void)
{
	bool flag = true;
	struct list_elem *e;
	struct frame *frame_to_remove;
	struct page *page;

	lock_acquire(&frame_lock);

	bool found_frame_to_remove = false;

	// Start from the last frame if it's valid; otherwise, start from the beginning
	e = (last_frame == NULL || last_frame == list_end(&frame_table)) ? list_front(&frame_table) : last_frame;

	// Iterate over the frame table until a suitable frame is found
	while (!found_frame_to_remove)
	{
		for (; e != list_end(&frame_table); e = list_next(e))
		{
			frame_to_remove = list_entry(e, struct frame, elem);
			page = frame_to_remove->page;

			// Attempt to acquire the lock for the current frame
			if (!lock_try_acquire(&frame_to_remove->lock))
			{
				continue; // Skip this frame if the lock cannot be acquired
			}

			// Attempt to acquire the lock for the current frame
			if (!lock_try_acquire(&page->lock))
			{
				lock_release(&frame_to_remove->lock);
				continue; // Skip this frame if the lock cannot be acquired
			}

			// Check if the frame is pinned; if so, release the lock and skip it
			if (page->pinned)
			{
				lock_release(&frame_to_remove->lock);
				lock_release(&page->lock);
				continue;
			}

			// Check if the page was accessed recently; if so, clear the accessed bit, release the lock, and skip it
			if (pagedir_is_accessed(page->thread->pagedir, page->vaddr))
			{
				pagedir_set_accessed(page->thread->pagedir, page->vaddr, false);
				lock_release(&frame_to_remove->lock);
				lock_release(&page->lock);
				continue;
			}

			// A suitable frame to remove has been found
			found_frame_to_remove = true;
			break; // Exit the loop
		}

		// If the end of the list is reached without finding a frame, start from the beginning
		if (!found_frame_to_remove)
		{
			e = list_front(&frame_table);
		}
	}
	last_frame = list_next(e);
	lock_release(&frame_lock);
	return frame_to_remove;
}

/* Reset the frame so that it can be used by another page. */
void frame_reset_frame(struct frame *frame)
{
	frame->page = NULL;
}

/* Get a frame by allocating a new page or evicting another page. */
struct frame *
frame_get_frame(enum palloc_flags flags)
{
	struct frame *entry;
	void *kaddr;

	/* Allocate a new page. */
	kaddr = palloc_get_page(flags);
	if (kaddr != NULL)
	{
		entry = frame_init_frame(kaddr);
	}

	else
	{
		/* If not possible, evict another page */
		struct frame *frame_to_remove = frame_get_evicted_frame();
		struct page *page = frame_to_remove->page;
		void *kaddr = frame_to_remove->kaddr;
		bool is_dirty = pagedir_is_dirty(page->thread->pagedir, page->vaddr);
		pagedir_clear_page(page->thread->pagedir, page->vaddr);
		page->pinned = true;
		/* Save the page to the swap space if needed */
		switch (page->type)
		{
		case VM_BIN:
			if (is_dirty || page->writable)
			{
				frame_io_wait();
				page->swap_id = swap_out(kaddr);
				frame_io_done();
				page->swapped = true;
			}
			break;

		case VM_ANON:
			frame_io_wait();
			page->swap_id = swap_out(kaddr);
			frame_io_done();
			page->swapped = true;
			break;
		}

		page->frame = NULL;
		frame_reset_frame(frame_to_remove);
		entry = frame_to_remove;
		page->pinned = false;
		lock_release(&frame_to_remove->lock);
		lock_release(&page->lock);
	}

	return entry;
}
