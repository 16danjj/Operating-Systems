#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

/* Predefined file handle macros */
#define STDIN_FILE 0
#define STDOUT_FILE 1


void syscall_init (void);
void close (int fd); 

struct lock *get_filesys_lock(void);

#endif /* userprog/syscall.h */
