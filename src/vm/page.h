#ifndef VM_PAGE_H
#define VM_PAGE_H
#include <hash.h>
#include <list.h>
#include "devices/block.h"
#include <debug.h>
#include "filesys/off_t.h"
#include "threads/synch.h"

/* Type of a page. */
enum page_type
{
  VM_BIN, /* For ELF files */
  VM_ANON /* For stack pages and zero initialized pages */
};

struct page
{                             /* Supplemental page table entry */
  void *vaddr;                /* Virtual address of the page */
  struct frame *frame;        /* Frame that is used by the page */
  struct hash_elem hash_elem; /* For supplemental page hash table. */
  struct thread *thread;      /* Owner process. */
  /* File stuff */
  struct file *file; /* File to be mapped. */
  off_t ofs;         /* Offset in file. */
  size_t read_bytes; /* Bytes to read from file. */
  /* Additional page information */
  bool writable;          /* True if writable, false if read-only. */
  bool pinned;            /* True if pinned, false otherwise. */
  bool swapped;           /* True if swapped out, false otherwise. */
  block_sector_t swap_id; /* Swap ID of swapped data */
  enum page_type type;    /* Type of a page. */
  struct lock lock;       /* Lock for page to prevent simulatneous acccess to the page */
};

unsigned page_hash(const struct hash_elem *p_, void *aux UNUSED);
bool page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);

bool page_init_table(void);
struct page *page_init(void *vaddr, bool writable);
struct page *page_lookup(void *address);
void page_exit(void);
bool page_load(void *addr);
void page_load_buffer_pages(void *buffer, size_t size, bool writable);
void page_pin_pages(void *buffer, size_t size, bool enable);

#endif /* vm/page.h */