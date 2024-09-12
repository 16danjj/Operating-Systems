#include "filesys/buffer_cache.h"
#include "filesys.h"
#include "threads/thread.h"
#include "threads/malloc.h"

#define BUFFER_CACHE_SIZE 64
#define TIME_WRITE_BEHIND 1000

/* Buffer cache entry. */
static struct buffer_cache buffer_cache[BUFFER_CACHE_SIZE];
struct lock buffer_cache_lock;
struct condition buffer_cache_available;

/* Read ahead buffer cache.
	Uses an array with indexes for where to add new
	entries and where to remove entries.
	Uses the condition to sleep when no entries availables. */
block_sector_t read_ahead_sectors[BUFFER_CACHE_SIZE];
unsigned read_ahead_rmv_idx = 0;
unsigned read_ahead_add_idx = 0;
unsigned read_ahead_cnt = 0;
struct lock read_ahead_lock;
struct condition read_ahead_available;
bool read_ahead_enabled = true;

/* Function prototypes. */
static void buffer_cache_read_ahead(void *aux UNUSED);
static void buffer_cache_write_behind(void *aux UNUSED);

/* Initialize the buffer cache entry. */
void buffer_cache_entry_init(struct buffer_cache *entry)
{
	entry->dirty = false;
	entry->sector = BLOCK_SECTOR_NULL;
	entry->is_metadata = false;
	entry->used_cnt = 0;
}

/* Initialize the buffer cache.
	Starts a periodic thread that writes dirty cache entries to disk. */
void buffer_cache_init(void)
{
	for (int i = 0; i < BUFFER_CACHE_SIZE; i++)
	{
		buffer_cache_entry_init(&buffer_cache[i]);
		read_ahead_sectors[i] = BLOCK_SECTOR_NULL;
	}
	lock_init(&buffer_cache_lock);
	cond_init(&buffer_cache_available);

	lock_init(&read_ahead_lock);
	cond_init(&read_ahead_available);

	/* Start the write behind and read ahead threads. */
	thread_create("buffer_cache_write_behind", PRI_DEFAULT, buffer_cache_write_behind, NULL);
	thread_create("buffer_cache_read_ahead", PRI_DEFAULT, buffer_cache_read_ahead, NULL);
}

/* Release the buffer cache entry. */
void buffer_cache_release(struct buffer_cache *entry)
{
	lock_acquire(&buffer_cache_lock);
	entry->used_cnt--;
	cond_signal(&buffer_cache_available, &buffer_cache_lock);
	lock_release(&buffer_cache_lock);
}

/* Look up the buffer cache entry for the given sector.
	Returns the buffer cache if it exists, else NULL. */
struct buffer_cache *
buffer_cache_lookup(block_sector_t sector)
{
	struct buffer_cache *entry = NULL;
	for (int i = 0; i < BUFFER_CACHE_SIZE; i++)
	{
		if (buffer_cache[i].sector == sector)
		{
			entry = &buffer_cache[i];
			buffer_cache[i].accessed = true;
			buffer_cache[i].used_cnt++;
			break;
		}
	}
	return entry;
}

/* Prepare a buffer cache entry for eviction. */
void buffer_cache_prepare_eviction(struct buffer_cache *entry, block_sector_t new_sector, bool init)
{
	if (init)
	{
		buffer_cache_entry_init(entry);
	}
	entry->sector = new_sector;
	entry->used_cnt++;
}

/* Evict a buffer cache entry.
	Returns the evicted buffer cache entry.
	Uses the clock algorithm while trying to not evict metadata blocks. */
struct buffer_cache *
buffer_cache_evict(block_sector_t new_sector)
{
	struct buffer_cache *metadata_entry = NULL;
	while (true)
	{
		for (int i = 0; i < BUFFER_CACHE_SIZE; i++)
		{
			/* If the buffer cache entry is free, use it. */
			if (buffer_cache[i].sector == BLOCK_SECTOR_NULL)
			{
				buffer_cache_prepare_eviction(&buffer_cache[i], new_sector, false);
				return &buffer_cache[i];
			}
			/* If the buffer cache entry is in use, skip it. */
			if (buffer_cache[i].used_cnt > 0)
			{
				continue;
			}
			/* If the buffer cache entry is not accessed, evict it, else set it to false. */
			if (!buffer_cache[i].accessed)
			{
				/* If the buffer cache entry is metadata, save it for later. */
				if (buffer_cache[i].is_metadata)
				{
					metadata_entry = &buffer_cache[i];
					continue;
				}
				/* Write the buffer cache entry to disk if it is dirty. */
				if (buffer_cache[i].dirty)
				{
					block_write(fs_device, buffer_cache[i].sector, buffer_cache[i].data);
				}
				buffer_cache_prepare_eviction(&buffer_cache[i], new_sector, true);
				return &buffer_cache[i];
			}
			else
			{
				buffer_cache[i].accessed = false;
			}
		}
		/* If no buffer cache entry was evicted, evict the metadata entry. */
		if (metadata_entry != NULL)
		{
			/* Write the metadata buffer cache entry to disk if it is dirty. */
			if (metadata_entry->dirty)
			{
				block_write(fs_device, metadata_entry->sector, metadata_entry->data);
			}
			buffer_cache_prepare_eviction(metadata_entry, new_sector, true);
			return metadata_entry;
		}
		cond_wait(&buffer_cache_available, &buffer_cache_lock);
	}
}

/* Load the buffer cache entry for the given sector.
	First tries to see if it in the buffer cache.
	If not, evicts a buffer cache entry if the buffer cache is full.
	Else, finds a free buffer cache entry.
	Returns the buffer cache entry. */
struct buffer_cache *
buffer_cache_load(struct block *block, block_sector_t sector)
{
	lock_acquire(&buffer_cache_lock);
	struct buffer_cache *entry = buffer_cache_lookup(sector);
	if (entry == NULL)
	{
		entry = buffer_cache_evict(sector);
		block_read(block, sector, entry->data);
	}
	ASSERT(entry != NULL);
	entry->accessed = true;
	lock_release(&buffer_cache_lock);
	return entry;
}

/* Read the buffer cache entry for the given sector. */
void buffer_cache_read(struct block *block, block_sector_t sector, void *buffer, off_t offset, int chunk_size)
{
	struct buffer_cache *entry = buffer_cache_load(block, sector);
	memcpy(buffer, entry->data + offset, chunk_size);
	buffer_cache_release(entry);
	/* Add to the read ahead list to read the next sector asynchonously. */
	lock_acquire(&read_ahead_lock);
	if (read_ahead_enabled && read_ahead_cnt < BUFFER_CACHE_SIZE)
	{
		read_ahead_add_idx = (read_ahead_add_idx + 1) % BUFFER_CACHE_SIZE;
		read_ahead_cnt++;
		read_ahead_sectors[read_ahead_add_idx] = sector + 1;
		cond_signal(&read_ahead_available, &read_ahead_lock);
	}
	lock_release(&read_ahead_lock);
}

/* Write the buffer cache entry for the given sector. */
void buffer_cache_write(struct block *block, block_sector_t sector, const void *buffer, off_t offset, int chunk_size)
{
	struct buffer_cache *entry = buffer_cache_load(block, sector);
	memcpy(entry->data + offset, buffer, chunk_size);
	entry->dirty = true;
	buffer_cache_release(entry);
}

/* Read ahead the buffer cache entry for the given sector. */
static void buffer_cache_read_ahead(void *aux UNUSED)
{
	/* Run while being enable, only disabled when pintos shutdowns. */
	while (read_ahead_enabled)
	{
		lock_acquire(&read_ahead_lock);
		/* Wait for a signal to read ahead. */
		while (read_ahead_cnt == 0)
		{
			cond_wait(&read_ahead_available, &read_ahead_lock);
		}
		/* Extract the next sector. */
		read_ahead_cnt--;
		block_sector_t sector = read_ahead_sectors[read_ahead_rmv_idx];
		read_ahead_sectors[read_ahead_rmv_idx] = BLOCK_SECTOR_NULL;
		read_ahead_rmv_idx = (read_ahead_rmv_idx + 1) % BUFFER_CACHE_SIZE;
		lock_release(&read_ahead_lock);

		/* Load the buffer cache entry for the sector. */
		struct buffer_cache *entry = buffer_cache_load(fs_device, sector);
		buffer_cache_release(entry);
	}
}

/* Write all dirty buffer cache entries to disk. */
void buffer_cache_write_to_disk()
{
	lock_acquire(&buffer_cache_lock);
	for (int i = 0; i < BUFFER_CACHE_SIZE; i++)
	{
		struct buffer_cache *entry = &buffer_cache[i];
		if (entry->dirty)
		{
			block_write(fs_device, entry->sector, entry->data);
			entry->dirty = false;
		}
	}
	lock_release(&buffer_cache_lock);
}

/* Periodically write dirty buffer cache entries to disk. */
static void buffer_cache_write_behind(void *aux UNUSED)
{
	while (true)
	{
		timer_sleep(TIME_WRITE_BEHIND);
		buffer_cache_write_to_disk();
	}
}

/* Set the read ahead to be enabled or disabled. */
void buffer_cache_set_read_ahead(bool enable)
{
	read_ahead_enabled = enable;
}