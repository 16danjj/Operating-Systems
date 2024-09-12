#include "vm/page.h"
#include "vm/swap.h"
#include <debug.h>
#include "threads/thread.h"
#include "vm/frame.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "vm/swap.h"
#include "threads/synch.h"

/* Returns a hash value for page p. */
unsigned
page_hash(const struct hash_elem *p_, void *aux UNUSED)
{
  const struct page *p = hash_entry(p_, struct page, hash_elem);
  return hash_bytes(&p->vaddr, sizeof p->vaddr);
}

/* Returns true if page a precedes page b. */
bool page_less(const struct hash_elem *a_, const struct hash_elem *b_,
               void *aux UNUSED)
{
  const struct page *a = hash_entry(a_, struct page, hash_elem);
  const struct page *b = hash_entry(b_, struct page, hash_elem);

  return a->vaddr < b->vaddr;
}

/* Initializes the supplemental page table. */
bool page_init_table(void)
{
  struct thread *t = thread_current();
  t->supl_pt = malloc(sizeof *t->supl_pt);
  return t->supl_pt != NULL && hash_init(t->supl_pt, page_hash, page_less, NULL);
}

/* Returns the page containing the given virtual address,
   or a null pointer if no such page exists. */
struct page *
page_lookup(void *address)
{
  struct page p;
  struct hash_elem *e;
  p.vaddr = address;

  e = hash_find(thread_current()->supl_pt, &p.hash_elem);
  return e != NULL ? hash_entry(e, struct page, hash_elem) : NULL;
}

/* Initializes the supplemental page table. */
struct page *page_init(void *vaddr, bool writable)
{
  struct page *p = malloc(sizeof *p);
  if (p == NULL)
    PANIC("out of memory during page initialization");

  p->vaddr = pg_round_down(vaddr);
  p->writable = writable;
  p->thread = thread_current();
  p->pinned = false;
  p->swapped = false;
  p->swap_id = BLOCK_SECTOR_NULL;
  p->frame = NULL;
  p->file = NULL;
  p->ofs = 0;
  p->read_bytes = 0;
  p->type = VM_ANON;
  lock_init(&p->lock);
  hash_insert(thread_current()->supl_pt, &p->hash_elem);
  return p;
}

/* Loads a file into a page */
void page_load_file(struct page *p)
{
  struct thread *cur = thread_current();
  frame_io_wait();
  size_t read_bytes = file_read_at(p->file, p->frame->kaddr, p->read_bytes, p->ofs);
  frame_io_done();
  if (read_bytes != p->read_bytes)
  {
    frame_free_frame(p->frame);
    p->frame = NULL;
    printf("read_bytes: %d, p->read_bytes: %d\n", read_bytes, p->read_bytes);
    return;
  }
  size_t zero_bytes = PGSIZE - read_bytes;
  memset(p->frame->kaddr + p->read_bytes, 0, zero_bytes);
}

/* Loads a zero page */
void page_load_zero(struct page *p)
{
  memset(p->frame->kaddr, 0, PGSIZE);
}

/* Loads a page into memory. */
bool page_load(void *addr)
{
  struct thread *cur = thread_current();
  void *rounded_addr = pg_round_down(addr);
  struct page *p = page_lookup(rounded_addr);
  if (p == NULL)
    return false;

  lock_acquire(&p->lock);
  /* Acquire a frame */
  p->frame = frame_get_frame(PAL_USER);
  if (p->frame == NULL)
    return false;
  p->frame->page = p;

  bool success = pagedir_set_page(cur->pagedir, p->vaddr,
                                  p->frame->kaddr, p->writable);
  if (!success)
  {
    frame_free_frame(p->frame);
    return false;
  }
  pagedir_set_accessed(p->thread->pagedir, p->vaddr, true);

  /* Load the data into the page */
  if (p->swapped)
  {
    frame_io_wait();
    swap_in(p->frame->kaddr, p->swap_id);
    frame_io_done();
    p->swapped = false;
    p->swap_id = BLOCK_SECTOR_NULL;
  }
  else if (p->file != NULL)
  {
    page_load_file(p);
    p->type = VM_BIN;
  }
  else
  {
    page_load_zero(p);
    p->type = VM_ANON;
  }

  lock_release(&p->lock);

  return true;
}

/* Frees a page and set their corresponding frame as free */
void page_destroy(struct hash_elem *e, void *aux UNUSED)
{
  struct page *p = hash_entry(e, struct page, hash_elem);
  lock_acquire(&p->lock);
  if (p->frame != NULL)
  {
    frame_free_frame(p->frame);
  }
  pagedir_clear_page(thread_current()->pagedir, p->vaddr);
  free(p);
}

/* Destroys the supplemental page table. */
void page_exit(void)
{
  struct thread *cur = thread_current();
  if (cur->supl_pt != NULL)
  {
    hash_destroy(cur->supl_pt, page_destroy);
    free(cur->supl_pt);
  }
}

/* Initializes pages of buffer that have not yet been initialized */
void page_load_buffer_pages(void *buffer, size_t size, bool writable)
{
  if (buffer == NULL)
  {
    exit(-1);
  }
  void *start_page = pg_round_down(buffer);
  void *end_page = pg_round_up(buffer + size);
  /* Exits if the page has not a legitimate pointer address
     or if the start of the pointer has not been initialized */
  if (start_page < VADDR_START || end_page > PHYS_BASE || !page_lookup(start_page))
  {
    exit(-1);
  }

  struct page *p;
  for (void *vaddr = start_page; vaddr < end_page; vaddr += PGSIZE)
  {
    p = page_lookup(vaddr);
    if (!p)
    {
      p = page_init(vaddr, true);
    }
    /* Exits if the page is not writable but should be */
    if (writable && !p->writable) 
    {
      exit(-1);
    }
    if (!pagedir_get_page(thread_current()->pagedir, vaddr))
    {
      page_load(vaddr);
    }
  }
}

/* Pins/unpins pages of buffer */
void page_pin_pages(void *buffer, size_t size, bool enable)
{
  if (buffer == NULL)
  {
    exit(-1);
  }
  void *start_page = pg_round_down(buffer);
  void *end_page = pg_round_up(buffer + size);

  for (void *vaddr = start_page; vaddr < end_page; vaddr += PGSIZE)
  {
    page_lookup(vaddr)->pinned = enable;
  }
}
