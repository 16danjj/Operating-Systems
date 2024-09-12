#include "threads/synch.h"
#include "devices/block.h"
#include "filesys/off_t.h"

/* Buffer cache entry. */
struct buffer_cache
{
	block_sector_t sector;			 /* Sector number of disk location. */
	uint8_t data[BLOCK_SECTOR_SIZE]; /* Buffer cache data. */
	bool dirty;						 /* True if data has been modified. */
	bool accessed;					 /* True if data has been accessed. */
	bool is_metadata;				 /* True if the data is metadata. */
	unsigned used_cnt;				 /* Number of openers. */
};

void buffer_cache_init(void);
void buffer_cache_entry_init(struct buffer_cache *entry);
struct buffer_cache *buffer_cache_lookup(block_sector_t sector);
struct buffer_cache *buffer_cache_evict(block_sector_t new_sector);
void buffer_cache_write(struct block *block, block_sector_t sector, const void *buffer, off_t offset, int chunk_size);
void buffer_cache_read(struct block *block, block_sector_t sector, void *buffer, off_t offset, int chunk_size);
void buffer_cache_write_to_disk(void);
void buffer_cache_set_read_ahead(bool enable);
