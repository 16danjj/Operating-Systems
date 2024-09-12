#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/buffer_cache.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format(void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void filesys_init(bool format)
{
  fs_device = block_get_role(BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC("No file system device found, can't initialize file system.");

  inode_init();
  free_map_init();

  buffer_cache_init();

  if (format)
    do_format();

  free_map_open();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void filesys_done(void)
{
  free_map_close();
  buffer_cache_set_read_ahead(false);
  buffer_cache_write_to_disk();
}

/* Parses the path and returns the directory and filename. 
  Returns true if the file exists else false. If the path to 
  the file is not possible, dir will be set to NULL. */
bool filesys_lookup(const char *path, struct dir **dir, char *filename)
{
  bool success = true;
  char *curr, *next, *save_ptr;
  char *path_copy = malloc(strlen(path) + 1);
  strlcpy(path_copy, path, strlen(path) + 1);

  /* If the path is absolute, start from the root directory 
    else from the current working directory*/
  if (path_copy[0] == '/' || thread_current()->cwd == NULL)
  {
    *dir = dir_open_root();
  }
  else
  {
    *dir = dir_reopen(thread_current()->cwd);
  }

  /* Loop through the path and search if the current name exists in 
    the current directory. If yes, check if it's a directory and the loop 
    is not finished, then set the current directory to itself, else check 
    that the loop should be finished. */
  curr = strtok_r(path_copy, "/", &save_ptr);
  next = curr;
  bool should_finish = false;
  while (next != NULL)
  {
    if (should_finish)
    {
      dir_close(*dir);
      *dir = NULL;
      break;
    }
    curr = next;
    next = strtok_r(NULL, "/", &save_ptr);
    struct inode *inode;
    if (!dir_lookup(*dir, curr, &inode))
    {
      success = false;
      should_finish = true;
    }
    else
    {
      if (next != NULL && inode_is_dir(inode))
      {
        dir_close(*dir);
        *dir = dir_open(inode);
      }
      else
      {
        inode_close(inode);
        should_finish = true;
      }
    }
  }

  /* Special case for '/' as curr will be NULL */
  if (*dir != NULL)
  {
    if (curr == NULL)
    {
      strlcpy(filename, path_copy, 1);
    }
    else
    {
      strlcpy(filename, curr, strlen(curr) + 1);
    }
  }

  free(path_copy);
  return success;
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool filesys_create(const char *name, off_t initial_size, bool is_dir)
{
  if (name == NULL || strlen(name) == 0)
    return false;
  block_sector_t inode_sector = 0;
  struct dir *dir;
  bool success = false;
  char *filename = malloc(strlen(name) + 1);
  bool exists = filesys_lookup(name, &dir, filename);
  if (!exists)
  {
    success = (dir != NULL && free_map_allocate(1, &inode_sector) && inode_create(inode_sector, initial_size, is_dir) && dir_add(dir, filename, inode_sector, is_dir));
    if (!success && inode_sector != 0)
      free_map_release(inode_sector, 1);
  }
  dir_close(dir);

  free(filename);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open(const char *name)
{
  if (name == NULL || strlen(name) == 0)
    return NULL;
  struct dir *dir;
  struct inode *inode = NULL;
  char *filename = malloc(strlen(name) + 1);
  bool success = filesys_lookup(name, &dir, filename);

  if (!success || dir == NULL)
  {
    free(filename);
    return NULL;
  }

  if (dir != NULL)
  {
    /* If the file can't be found in the directory, it is the directory itself */
    if (!dir_lookup(dir, filename, &inode))
    {
      inode = dir_get_inode(dir);
    }
  }

  dir_close(dir);
  free(filename);

  return file_open(inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool filesys_remove(const char *name)
{
  struct dir *dir;
  char *filename = malloc(strlen(name) + 1);
  bool success = filesys_lookup(name, &dir, filename);

  success = dir != NULL && dir_remove(dir, filename);
  dir_close(dir);
  free(filename);

  return success;
}

/* Formats the file system. */
static void
do_format(void)
{
  printf("Formatting file system...");
  free_map_create();
  if (!dir_create(ROOT_DIR_SECTOR, 16))
    PANIC("root directory creation failed");
  free_map_close();
  printf("done.\n");
}
