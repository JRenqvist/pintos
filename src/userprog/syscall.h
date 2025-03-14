#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>
#include <user/syscall.h>

void syscall_init(void);

void sleep(int millis);
void halt(void);
bool create(const char* file, unsigned initial_size);
int open(const char* file);
void close(int fd);
int write(int fd, const void* buffer, unsigned size);
int read(int fd, void* buffer, unsigned size);
bool remove(const char* file_name);
int filesize(int fd);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void exit(int status);
pid_t exec(const char* cmd_line);
int wait(int pid);

#endif /* userprog/syscall.h */
