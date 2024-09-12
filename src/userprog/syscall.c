#include "userprog/syscall.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/directory.h"
#include "filesys/inode.h"
#include "process.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "filesys/rwlock.h"


static void syscall_handler (struct intr_frame *);
/* System calls */
void halt (void);
void exit (int status);
pid_t exec (const char *cmd_line);
int wait (pid_t pid);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
bool chdir (const char *dir);
bool mkdir (const char *dir);
bool readdir (int fd, char *name);
bool isdir (int fd);
int inumber (int fd);

struct lock filesys_lock;
struct rw_lock rw_lock;

/* Returns the filesys_lock (used in process.c) */
struct lock *
get_filesys_lock (void)
{
  return &filesys_lock;
}

/* Allocates next available fd in current thread's fd_table of open files.
   Returns fd if fd_table has availability for new open file, otherwise return
   -1  */
static int
allocate_fd (void)
{
  int fd;
  struct thread *t = thread_current ();

  for (fd = 2; fd < MAX_OPEN_FILES; fd++)
    {
      if (t->fd_table[fd] == NULL)
        {
          break;
        }
    }

  if (fd < MAX_OPEN_FILES)
    {
      return fd;
    }

  return -1;
}

/* Returns file of fd from current thread's fd_table, and returns NULL if fd is
 * an invalid fd for files */
struct file *
get_file (int fd)
{
  struct thread *t = thread_current ();

  if (fd < 2 || fd >= MAX_OPEN_FILES)
    {
      return NULL;
    }

  return t->fd_table[fd];
}

/* Closes open file of fd, and removes open file from current thread's fd_table
 */
static void
close_openfile (int fd)
{

  struct thread *t = thread_current ();
  struct file *file = get_file (fd);

  if (file != NULL)
    {
      file_close (file);
      t->fd_table[fd] = NULL;
    }
}

/* Checks if the pointer is a valid user address */
static void
check_if_valid_ptr (const void *vaddr)
{
  /* Check if the address is not NULL, a user address and not below the VADDR
   * beginning. */
  if (vaddr == NULL || !is_user_vaddr (vaddr) || vaddr < (void *)VADDR_START)
    {
      exit (-1);
    }
  /* Check if the address is mapped in the page directory. */
  if (pagedir_get_page (thread_current ()->pagedir, vaddr) == NULL)
    {
      exit (-1);
    }
}

/* Checks if a string is a valid user address */
static void
check_if_valid_string (const char *str)
{
  check_if_valid_ptr (str);
  while (*str != '\0')
    {
      str++;
      check_if_valid_ptr (str);
    }
}

/* Checks if the amount of bytes are valid user addresses */
static void
check_if_valid_bytes (const void *vaddr, unsigned bytes)
{
  for (unsigned i = 0; i < bytes; i++)
    {
      check_if_valid_ptr (vaddr + i);
    }
}

/* Checks if the arguments are valid user addresses */
static void
check_if_valid_args (const void *esp, int argc)
{
  for (int i = 0; i < argc * 4; i++)
    {
      check_if_valid_ptr (esp + i);
    }
}

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_lock);
  rwlock_init(&rw_lock);
}

static void
syscall_handler (struct intr_frame *f)
{

  /* Check for all 4 bytes of the system call number */
  check_if_valid_bytes (f->esp, 4);
  int syscall_number = *(int32_t *)f->esp;

  void *argv = f->esp + 4;

  /* Declaration of variables used in the switch statement */
  char *file;
  const char *dir;
  int fd;
  void *buffer;
  unsigned size;
  int res;

  switch (syscall_number)
    {
    case SYS_HALT:
      halt ();
      break;
    case SYS_EXIT:
      check_if_valid_args (argv, 1);
      int status = *(int32_t *)(argv);
      exit (status);
      break;
    case SYS_EXEC:
      check_if_valid_args (argv, 1);
      const char *cmd_line = *(char **)(argv);
      check_if_valid_string (cmd_line);
      res = exec (cmd_line);
      f->eax = res;
      break;
    case SYS_WAIT:
      check_if_valid_args (argv, 1);
      pid_t pid = *(pid_t *)(argv);
      res = wait (pid);
      f->eax = res;
      break;
    case SYS_CREATE:
      check_if_valid_args (argv, 2);
      file = *(char **)(argv);
      check_if_valid_string (file);
      unsigned initial_size = *(unsigned *)(argv + 4);
      res = create (file, initial_size);
      f->eax = res;
      break;
    case SYS_REMOVE:
      check_if_valid_args (argv, 1);
      file = *(char **)(argv);
      check_if_valid_string (file);
      res = remove (file);
      f->eax = res;
      break;
    case SYS_OPEN:
      check_if_valid_args (argv, 1);
      file = *(char **)(argv);
      check_if_valid_string (file);
      res = open (file);
      f->eax = res;
      break;
    case SYS_FILESIZE:
      check_if_valid_args (argv, 1);
      fd = *(int32_t *)(argv);
      res = filesize (fd);
      f->eax = res;
      break;
    case SYS_READ:
      check_if_valid_args (argv, 3);
      fd = *(int32_t *)(argv);
      buffer = *(void **)(argv + 4);
      size = *(unsigned *)(argv + 8);
      page_load_buffer_pages(buffer, size, true);
      check_if_valid_bytes (buffer, size);
      page_pin_pages(buffer, size, true);
      res = read (fd, buffer, size);
      page_pin_pages(buffer, size, false);
      f->eax = res;
      break;
    case SYS_WRITE:
      check_if_valid_args (argv, 3);
      fd = *(int32_t *)(argv);
      buffer = *(void **)(argv + 4);
      size = *(unsigned *)(argv + 8);
      page_load_buffer_pages(buffer, size, false);
      check_if_valid_bytes (buffer, size);
      page_pin_pages(buffer, size, true);
      res = write (fd, buffer, size);
      page_pin_pages(buffer, size, false);
      f->eax = res;
      break;
    case SYS_SEEK:
      check_if_valid_args (argv, 2);
      fd = *(int32_t *)(argv);
      unsigned position = *(unsigned *)(argv + 4);
      seek (fd, position);
      break;
    case SYS_TELL:
      check_if_valid_args (argv, 1);
      fd = *(int32_t *)(argv);
      res = tell (fd);
      f->eax = res;
      break;
    case SYS_CLOSE:
      {
        check_if_valid_args (argv, 1);
        fd = *(int32_t *)(argv);
        close (fd);
        break;
      }
    case SYS_CHDIR:
      check_if_valid_args (argv, 1);
      dir = *(char **)(argv);
      check_if_valid_string (dir);
      res = chdir(dir);
      f->eax = res;
      break;
    case SYS_MKDIR:
      check_if_valid_args (argv, 1);
      dir = *(char **)(argv);
      check_if_valid_string (dir);
      res = mkdir(dir);
      f->eax = res;
      break;
    case SYS_READDIR:  
      check_if_valid_args (argv, 2);
      fd = *(int32_t *)(argv);
      char *name = *(char **)(argv + 4);
      check_if_valid_string (name);
      res = readdir(fd, name);
      f->eax = res;
      break;
    case SYS_ISDIR:
      check_if_valid_args (argv, 1);
      fd = *(int32_t *)(argv);
      res = isdir (fd);
      f->eax = res;
      break;
    case SYS_INUMBER:
      check_if_valid_args (argv, 1);
      fd = *(int32_t *)(argv);
      res = inumber (fd);
      f->eax = res;
      break;
    default:
      PANIC ("Unknown system call");
      break;
    }
}

/* Halts the operating system. */
void
halt (void)
{
  shutdown_power_off ();
}


/* Exits the current process with status "status". */
void
exit (int status)
{
  struct thread *cur = thread_current ();
  printf ("%s: exit(%d)\n", cur->name, status);
  cur->exit_status = status;
  thread_exit ();
}

/* Executes the process of the command line cmd_line. */
pid_t
exec (const char *cmd_line)
{
  pid_t pid = process_execute (cmd_line);
  return pid;
}

/* Waits for the process with pid to finish. */
int
wait (pid_t pid)
{
  return process_wait (pid);
}

/* Creates a new file called *file that has intial_size size.
   Returns true if successful, and false otherwise. */
bool
create (const char *file, unsigned initial_size)
{
  bool status;
  if (file == NULL || strlen (file) == 0)
    {
      exit (-1);
    }
  lock_acquire (&filesys_lock);
  status = filesys_create (file, initial_size, false);
  lock_release (&filesys_lock);

  return status;
}

/* Deletes the file called *file.
   Returns true if successful, and false otherwise. */
bool
remove (const char *file)
{
  bool status;

  lock_acquire (&filesys_lock);
  status = filesys_remove (file);
  lock_release (&filesys_lock);

  return status;
}

/* Opens the file called *file, assign the opened file a fd
  which the current thread should keep track of in its fd_table

   If the file can be opened, return its fd, otherwise return -1 */
int
open (const char *file)
{
  int fd = -1;
  lock_acquire (&filesys_lock);
  struct file *open_file = filesys_open (file);
  struct thread *t = thread_current ();

  if (open_file != NULL)
    {
      fd = allocate_fd ();
      if (fd != -1)
        {
          t->fd_table[fd] = open_file;
        }
    }

  lock_release (&filesys_lock);

  return fd;
}

/* Returns the size in bytes, of file open as fd. */
int
filesize (int fd)
{
  int size = -1;

  lock_acquire (&filesys_lock);
  struct file *file = get_file (fd);

  if (file != NULL)
    {
      size = file_length (file);
    }
  lock_release (&filesys_lock);

  return size;
}

/* Reads size bytes from file open as fd into buffer and returns the number of
   bytes actually read. Fd of 0 will read from the keyboard*/
int
read (int fd, void *buffer, unsigned size)
{
  int status = -1;

  if (fd == STDIN_FILE)
    {
      uint8_t *read_buffer = buffer;
      uint8_t key;
      unsigned count = 0;

      while (count < size && (key = input_getc ()) != 0)
        {
          memcpy (read_buffer, &key, sizeof (uint8_t));
          read_buffer++;
          count++;
        }
      status = count;
    }
  else
    {

      //lock_acquire (&filesys_lock);
      rwlock_acquire_read(&rw_lock);
      struct file *file = get_file (fd);

      if (file != NULL)
        {
          status = file_read (file, buffer, size);
        }
      //lock_release (&filesys_lock);
      rwlock_release_read(&rw_lock);
    }

  return status;
}

/* Writes size bytes from buffer to open file fd and returns the number of
   bytes actually written. Fd of 1 will write to the console*/
int
write (int fd, const void *buffer, unsigned size)
{
  int status = -1;

  if (fd == STDOUT_FILE)
    {
      putbuf (buffer, size);
      return size;
    }
  else
    {
      //lock_acquire (&filesys_lock);
      rwlock_acquire_write(&rw_lock);
      struct file *file = get_file (fd);

      if (file != NULL && !file_is_dir (file))
        {
          status = file_write (file, buffer, size);
        }
      //lock_release (&filesys_lock);
      rwlock_release_write(&rw_lock);
    }

  return status;
}

/* Changes the next byte to be read/written in open fd to position,
   which is expressed in bytes from the beginning of the file. */
void
seek (int fd, unsigned position)
{
  lock_acquire (&filesys_lock);
  struct file *file = get_file (fd);

  if (file != NULL)
    {
      file_seek (file, position);
    }
  lock_release (&filesys_lock);
}

/* Gets the position of the next byte te be read/writen in open fd,
   which is expressed in bytes from the beginning of the file. */
unsigned
tell (int fd)
{
  int status = -1;
  lock_acquire (&filesys_lock);
  struct file *file = get_file (fd);

  if (file != NULL)
    {
      status = file_tell (file);
    }
  lock_release (&filesys_lock);

  return status;
}

/* Closes file of fd */
void
close (int fd)
{
  lock_acquire (&filesys_lock);
  close_openfile (fd);
  lock_release (&filesys_lock);
}

/* Changes the current working directory of the process to dir */
bool
chdir (const char *dir)
{
  bool status = false;
  struct dir *new_dir;
  struct inode *inode;
  bool success = false;
  char *filename = malloc(strlen(dir) + 1);
  lock_acquire (&filesys_lock);

  /* Special case for root directory, "." and ".." */
  if (strcmp (dir, "/") == 0) {
    dir_close (thread_current ()->cwd);
    thread_current ()->cwd = dir_open_root();
    status = true;
    lock_release (&filesys_lock);
    return status;
  } else if (strcmp (dir, ".") == 0 || strcmp (dir, "..") == 0) {
    success = dir_lookup (thread_current ()->cwd, dir, &inode);
  } else {
    success = filesys_lookup(dir, &new_dir, filename);
    success = success && dir_lookup (new_dir, filename, &inode);
    dir_close (new_dir);
  }
    
  if (success)
    {
      if (success && inode_is_dir (inode)) {
        dir_close (thread_current ()->cwd);
        thread_current ()->cwd = dir_open(inode);
        status = true;
      }
    }
  lock_release (&filesys_lock);
  return status;
}

/* Creates a new directory called dir */
bool
mkdir (const char *dir)
{
  bool status = false;
  lock_acquire (&filesys_lock);
  status = filesys_create (dir, 0, true);
  lock_release (&filesys_lock);
  return status;
}

/* Reads the next directory entry in the directory open as fd */
bool
readdir (int fd, char *name)
{
  bool status = false;
  lock_acquire (&filesys_lock);
  struct file *file = get_file (fd);

  if (file != NULL && file_is_dir (file))
    {
      status = dir_readfile (file, name);
    }
  lock_release (&filesys_lock);
  return status;
}

/* Returns true if the file open as fd is a directory */
bool
isdir (int fd)
{
  bool status = false;
  lock_acquire (&filesys_lock);
  struct file *file = get_file (fd);

  if (file != NULL)
    {
      status = file_is_dir (file);
    }
  lock_release (&filesys_lock);
  return status;
}

/* Returns the inode number of the file open as fd */
int
inumber (int fd)
{
  int status = -1;
  lock_acquire (&filesys_lock);
  struct file *file = get_file (fd);

  if (file != NULL)
    {
      status = inode_get_inumber (file_get_inode (file));
    }
  lock_release (&filesys_lock);
  return status;
}