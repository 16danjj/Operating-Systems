#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "filesys/buffer_cache.h"

// 128 indexes per block
#define BLOCK_INDEX_AMOUNT 128
#define BLOCK_INDIRECT_SIZE (BLOCK_INDEX_AMOUNT * BLOCK_SECTOR_SIZE)          // 128 * 512 = 65536 bytes
#define BLOCK_DOUBLE_INDIRECT_SIZE (BLOCK_INDEX_AMOUNT * BLOCK_INDIRECT_SIZE) // 128 * 65536 = 8388608 bytes

/* 10 * 512 = 5120 bytes                (10 direct blocks)
   1 * 512 * 128 = 65536 bytes          (1 indirect block)
   1 * 512 * 128 * 128 = 8388608 bytes  (1 doubly indirect block)
  8388608 + 65536 + 5120 = 8457664 bytes or 8.06 MB */
#define DIRECT_BLOCKS 10
#define INDIRECT_BLOCKS 1
#define DOUBLE_DIRECT_BLOCKS 1
/* -2 because of parent_directory and is_directory */
#define UNUSED_BLOCKS (125 - 2 - DIRECT_BLOCKS - INDIRECT_BLOCKS - DOUBLE_DIRECT_BLOCKS)

#define DIRECT_BLOCK_SIZE (BLOCK_SECTOR_SIZE * DIRECT_BLOCKS)                                                           // Blocks 0-9
#define INDIRECT_BLOCK_SIZE (BLOCK_SECTOR_SIZE * (INDIRECT_BLOCKS * BLOCK_INDEX_AMOUNT))                                // Blocks 10-138
#define DOUBLE_DIRECT_BLOCK_SIZE (BLOCK_SECTOR_SIZE * (DOUBLE_DIRECT_BLOCKS * BLOCK_INDEX_AMOUNT * BLOCK_INDEX_AMOUNT)) // Blocks 139-16267

static uint8_t zeros[BLOCK_SECTOR_SIZE]; /* A block of zeros. */

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
  block_sector_t start; /* First data sector. */
  off_t length;         /* File size in bytes. */

  /* New elements */
  block_sector_t direct_block[DIRECT_BLOCKS]; /* Direct block index. */
  block_sector_t indirect_block;              /* Indirect block index. */
  block_sector_t doubly_indirect_block;       /* Doubly indirect block index. */
  bool is_directory;                          /* True if inode is a directory. */
  block_sector_t parent_directory;            /* First parent directory sector. */

  unsigned magic;                 /* Magic number. */
  uint32_t unused[UNUSED_BLOCKS]; /* Not used. */
};

/* In-memory inode to store the inode_disk */
struct inode_data
{
  block_sector_t start; /* First data sector. */
  off_t length;         /* File size in bytes. */

  block_sector_t direct_block[DIRECT_BLOCKS]; /* Direct block index. */
  block_sector_t indirect_block;              /* Indirect block index. */
  block_sector_t doubly_indirect_block;       /* Doubly indirect block index. */
  bool is_directory;                          /* True if inode is a directory. */
  block_sector_t parent_directory;            /* First parent directory sector. */
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors(off_t size)
{
  return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
{
  struct list_elem elem;  /* Element in inode list. */
  block_sector_t sector;  /* Sector number of disk location. */
  int open_cnt;           /* Number of openers. */
  bool removed;           /* True if deleted, false otherwise. */
  int deny_write_cnt;     /* 0: writes ok, >0: deny writes. */
  struct inode_data data; /* Inode content. */
  struct lock lock;       /* Lock for inode. */
};

/* Set the inode_data from the inode_disk */
void inode_set_data(struct inode_data *data, struct inode_disk *disk_inode)
{
  data->is_directory = disk_inode->is_directory;
  data->parent_directory = disk_inode->parent_directory;
  data->length = disk_inode->length;
  memcpy(data->direct_block, disk_inode->direct_block, sizeof(data->direct_block));
  data->indirect_block = disk_inode->indirect_block;
  data->doubly_indirect_block = disk_inode->doubly_indirect_block;
}

/* Set the inode_disk from the inode_data */
void inode_set_disk_data(struct inode_disk *disk_inode, struct inode_data *data)
{
  disk_inode->is_directory = data->is_directory;
  disk_inode->parent_directory = data->parent_directory;
  disk_inode->length = data->length;
  memcpy(disk_inode->direct_block, data->direct_block, sizeof(disk_inode->direct_block));
  disk_inode->indirect_block = data->indirect_block;
  disk_inode->doubly_indirect_block = data->doubly_indirect_block;
}

/* Write the inode_data to disk */
void inode_write_data_to_disk(struct inode *inode)
{
  struct inode_data *data = &inode->data;
  struct inode_disk *disk_inode = calloc(1, sizeof *disk_inode);
  inode_set_disk_data(disk_inode, data);
  buffer_cache_write(fs_device, inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
  free(disk_inode);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector(const struct inode *inode, off_t pos)
{
  ASSERT(inode != NULL);
  if (pos >= 0 && pos <= inode->data.length)
  {
    /* Direct block */
    if (pos < DIRECT_BLOCK_SIZE)
    {
      return inode->data.direct_block[pos / BLOCK_SECTOR_SIZE];
      /* Indirect block */
    }
    else if (pos < DIRECT_BLOCK_SIZE + INDIRECT_BLOCK_SIZE)
    {
      block_sector_t indirect_block[BLOCK_INDEX_AMOUNT];
      buffer_cache_read(fs_device, inode->data.indirect_block, indirect_block, 0, BLOCK_SECTOR_SIZE);
      /* Remove the offset of the direct blocks */
      pos -= DIRECT_BLOCK_SIZE;
      /* Calculate the index of the data block */
      off_t index = pos / BLOCK_SECTOR_SIZE;
      return indirect_block[index];
      /* Doubly indirect block */
    }
    else if (pos < DIRECT_BLOCK_SIZE + INDIRECT_BLOCK_SIZE + DOUBLE_DIRECT_BLOCK_SIZE)
    {
      /* Load the first indirect block */
      block_sector_t doubly_indirect_block[BLOCK_INDEX_AMOUNT];
      buffer_cache_read(fs_device, inode->data.doubly_indirect_block, doubly_indirect_block, 0, BLOCK_SECTOR_SIZE);
      /* Calculate the index of the second indirect block */
      pos -= DIRECT_BLOCK_SIZE + INDIRECT_BLOCK_SIZE;
      off_t index = pos / INDIRECT_BLOCK_SIZE;
      block_sector_t indirect_block[BLOCK_INDEX_AMOUNT];
      buffer_cache_read(fs_device, doubly_indirect_block[index], indirect_block, 0, BLOCK_SECTOR_SIZE);
      /* Remove the offset of the previous indirect blocks */
      pos -= index * INDIRECT_BLOCK_SIZE;
      index = pos / BLOCK_SECTOR_SIZE;
      return indirect_block[index];
    }
  }
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void inode_init(void)
{
  list_init(&open_inodes);
}

/* Allocate NUMBER_BLOCKS for an indirect block for the inode growth */
bool inode_growth_block(block_sector_t *block, size_t *remaining_blocks, size_t number_blocks)
{
  for (int i = 0; i < number_blocks; i++)
  {
    if (*remaining_blocks == 0)
    {
      return true;
    }
    if (block[i] == 0)
    {
      if (!free_map_allocate(1, &block[i]))
      {
        return false;
      }
      (*remaining_blocks)--;
      buffer_cache_write(fs_device, block[i], zeros, 0, BLOCK_SECTOR_SIZE);
    }
  }
  return true;
}

/* Allocate an indirect block for the inode growth */
bool inode_alloc_indir_block(block_sector_t *indirect_block, void *buffer)
{
  if (*indirect_block == 0)
  {
    if (!free_map_allocate(1, indirect_block))
    {
      return false;
    }
  }
  buffer_cache_read(fs_device, *indirect_block, buffer, 0, BLOCK_SECTOR_SIZE);
  return true;
}

/* Grow the inode by LENGTH bytes, updates the length variable of inode_data */
bool inode_growth(struct inode_data *disk_inode, off_t old_length, off_t new_length)
{
  size_t sectors = bytes_to_sectors(new_length - old_length);
  /* Reserve direct blocks */
  size_t remaining_blocks = sectors;
  inode_growth_block(disk_inode->direct_block, &remaining_blocks, DIRECT_BLOCKS);

  if (remaining_blocks != 0)
  {
    /* If blocks are still remaining, reserve indirect blocks */
    block_sector_t indirect_block[BLOCK_INDEX_AMOUNT];
    if (!inode_alloc_indir_block(&disk_inode->indirect_block, indirect_block))
    {
      return false;
    }
    /* Grow the file block by block in each block of the indirect block */
    inode_growth_block(indirect_block, &remaining_blocks, BLOCK_INDEX_AMOUNT);
    /* Write the indirect block to disk */
    buffer_cache_write(fs_device, disk_inode->indirect_block, indirect_block, 0, BLOCK_SECTOR_SIZE);
  }

  if (remaining_blocks != 0)
  {
    /* If blocks are still remaining, reserve doubly indirect blocks */
    block_sector_t doubly_indirect_block[BLOCK_INDEX_AMOUNT];
    if (!inode_alloc_indir_block(&disk_inode->doubly_indirect_block, doubly_indirect_block))
    {
      return false;
    }
    for (int i = 0; i < BLOCK_INDEX_AMOUNT; i++)
    {
      if (remaining_blocks == 0)
      {
        break;
      }
      block_sector_t indirect_block[BLOCK_INDEX_AMOUNT];
      if (!inode_alloc_indir_block(&doubly_indirect_block[i], indirect_block))
      {
        return false;
      }
      /* Grow the file block by block in each block of the indirect block which is in a indirect block */
      inode_growth_block(indirect_block, &remaining_blocks, BLOCK_INDEX_AMOUNT);
      /* Write the indirect block to disk */
      buffer_cache_write(fs_device, doubly_indirect_block[i], indirect_block, 0, BLOCK_SECTOR_SIZE);
    }
    buffer_cache_write(fs_device, disk_inode->doubly_indirect_block, doubly_indirect_block, 0, BLOCK_SECTOR_SIZE);
  }

  /* Only go here if all blocks could be allocated */
  if (remaining_blocks == 0)
  {
    /* Write the doubly indirect block to disk */
    disk_inode->length = new_length;
    return true;
  }
  return false;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create(block_sector_t sector, off_t length, bool is_directory)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT(length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc(1, sizeof *disk_inode);
  if (disk_inode != NULL)
  {
    disk_inode->is_directory = is_directory;
    disk_inode->length = length;
    disk_inode->magic = INODE_MAGIC;
    if (inode_growth(disk_inode, 0, length))
    {
      buffer_cache_write(fs_device, sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
      success = true;
    }
    free(disk_inode);
  }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open(block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin(&open_inodes); e != list_end(&open_inodes);
       e = list_next(e))
  {
    inode = list_entry(e, struct inode, elem);
    if (inode->sector == sector)
    {
      inode_reopen(inode);
      return inode;
    }
  }

  /* Allocate memory. */
  inode = malloc(sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front(&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->lock);
  struct inode_disk *disk_inode = calloc(1, sizeof *disk_inode);
  buffer_cache_read(fs_device, inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
  inode_set_data(&inode->data, disk_inode);
  free(disk_inode);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen(struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber(const struct inode *inode)
{
  return inode->sector;
}

/* Set the parent of the inode */
bool inode_set_parent(block_sector_t parent, block_sector_t child)
{
  struct inode *inode = inode_open(child);
  if (inode == NULL)
    return false;
  inode->data.parent_directory = parent;
  inode_write_data_to_disk(inode);
  inode_close(inode);
  return true;
}

/* Get the parent of the inode */
block_sector_t
inode_get_parent(struct inode *inode)
{
  return inode->data.parent_directory;
}

/* Returns true if the inode is a directory */
bool inode_is_dir(struct inode *inode)
{
  return inode->data.is_directory;
}

/* Returns the open count of the inode */
int inode_get_open_cnt(struct inode *inode)
{
  return inode->open_cnt;
}

/* Deallocates blocks in the inode_disk */
void inode_free_block(block_sector_t *block, size_t *remaining_blocks, size_t number_blocks)
{
  for (int i = 0; i < number_blocks; i++)
  {
    if (*remaining_blocks == 0)
    {
      return;
    }
    free_map_release(block[i], 1);
    (*remaining_blocks)--;
  }
}

/* Load the indirect block into the buffer */
void inode_load_indir_block(block_sector_t *indirect_block, void *buffer)
{
  buffer_cache_read(fs_device, *indirect_block, buffer, 0, BLOCK_SECTOR_SIZE);
}

/* Deallocates blocks in the inode_disk */
void inode_free(struct inode *inode)
{
  struct inode_data *disk_inode = &inode->data;
  size_t sectors = bytes_to_sectors(disk_inode->length);
  if (sectors == 0)
  {
    return;
  }
  /* Free direct blocks */
  size_t remaining_blocks = sectors;
  inode_free_block(disk_inode->direct_block, &remaining_blocks, DIRECT_BLOCKS);

  /* Free indirect blocks */
  if (remaining_blocks != 0)
  {
    /* Load the indirect block into "indirect_block[BLOCK_INDEX_AMOUNT]" */
    block_sector_t indirect_block[BLOCK_INDEX_AMOUNT];
    inode_load_indir_block(&disk_inode->indirect_block, indirect_block);
    /* Free block by block in each block of the indirect block */
    inode_free_block(indirect_block, &remaining_blocks, BLOCK_INDEX_AMOUNT);
    /* Free the indirect block sector */
    free_map_release(disk_inode->indirect_block, 1);
  }

  /* Free doubly indirect blocks */
  if (remaining_blocks != 0)
  {
    /* Load the indirect block into "doubly_indirect_block[BLOCK_INDEX_AMOUNT]" */
    block_sector_t doubly_indirect_block[BLOCK_INDEX_AMOUNT];
    inode_load_indir_block(&disk_inode->doubly_indirect_block, doubly_indirect_block);
    for (int i = 0; i < BLOCK_INDEX_AMOUNT; i++)
    {
      if (remaining_blocks == 0)
      {
        break;
      }
      /* Load the indirect block into "indirect_block[BLOCK_INDEX_AMOUNT]" */
      block_sector_t indirect_block[BLOCK_INDEX_AMOUNT];
      inode_load_indir_block(&disk_inode->indirect_block, indirect_block);
      /* Free block by block in each block of the indirect block */
      inode_free_block(indirect_block, &remaining_blocks, BLOCK_INDEX_AMOUNT);
      /* Free the indirect block sector */
      free_map_release(doubly_indirect_block[i], 1);
    }
    /* Free the doubly indirect block sector */
    free_map_release(disk_inode->doubly_indirect_block, 1);
  }
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
  {
    /* Remove from inode list and release lock. */
    list_remove(&inode->elem);

    /* Write the inode to disk */
    inode_write_data_to_disk(inode);

    /* Deallocate blocks if removed. */
    if (inode->removed)
    {
      free_map_release(inode->sector, 1);
      inode_free(inode);
    }

    free(inode);
  }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove(struct inode *inode)
{
  ASSERT(inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at(struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0)
  {
    /* Disk sector to read, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    buffer_cache_read(fs_device, sector_idx, buffer + bytes_read, sector_ofs, chunk_size);
    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_read += chunk_size;
  }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t inode_write_at(struct inode *inode, const void *buffer_, off_t size,
                     off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  /* Grow file if writing past EOF */
  int diff = offset + size - inode->data.length;
  if (diff > 0)
  {
    /* If writing past EOF, only one process can write */
    if (!inode->data.is_directory)
    {
      lock_acquire(&inode->lock);
    }
    if (!inode_growth(&inode->data, inode->data.length, inode->data.length + diff))
    {
      return 0;
    }
    inode_write_data_to_disk(inode);
    if (!inode->data.is_directory)
    {
      lock_release(&inode->lock);
    }
  }

  while (size > 0)
  {
    /* Sector to write, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    buffer_cache_write(fs_device, sector_idx, buffer + bytes_written, sector_ofs, chunk_size);
    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write(struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write(struct inode *inode)
{
  ASSERT(inode->deny_write_cnt > 0);
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length(const struct inode *inode)
{
  return inode->data.length;
}


void inode_lock_acquire(struct inode *inode)
{
  lock_acquire(&inode->lock);
}

void inode_lock_release(struct inode *inode)
{
  lock_release(&inode->lock);
}