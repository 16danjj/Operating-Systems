#ifndef VM_FRAME_H
#define VM_FRAME_H
#include "threads/palloc.h"
#include <list.h>
#include "threads/synch.h"

struct frame
{
	void *kaddr;		   /* Kernel virtual address */
	struct list_elem elem; /* List element for frame table list */
	struct lock lock;	   /* Lock to prevent simultaneous changes to frames */
	struct page *page;	   /* Page that is mapped to this frame */
};

void frame_init(void);
void frame_io_wait(void);
void frame_io_done(void);
struct frame *frame_get_frame(enum palloc_flags flags);
void frame_free_frame(struct frame *entry);

#endif /* vm/frame.h */