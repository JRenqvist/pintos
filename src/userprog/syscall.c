#include "userprog/syscall.h"

#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "devices/timer.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"

#include <stdio.h>
#include <syscall-nr.h>
#include <user/syscall.h>
#include <stdbool.h>
#include <kernel/stdio.h>
#include <string.h>



static void syscall_handler(struct intr_frame*);
int read_from_stdin(char* buffer, int size);
bool is_valid_ptr(const void* ptr);
bool is_valid_string(const char* s);
bool is_valid_buffer(const void* buffer, unsigned length);
bool is_valid_fd(int fd);

void syscall_init(void)
{
	intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler(struct intr_frame* f UNUSED)
{

	if (!is_valid_buffer(f->esp, sizeof(int))) {
		exit(-1);
	}

	switch (*(int*)f->esp) {
		
		case SYS_SLEEP:
			int millis = *(int*)(f->esp + sizeof(int));
			sleep(millis);
			break;
		
		case SYS_HALT:
			halt();
			break;
		
		case SYS_CREATE: {
			const char* file = *(char**)(f->esp + sizeof(char*));
			unsigned initial_size = *(unsigned*) (f->esp + sizeof(char*) + sizeof(unsigned));
			f->eax = create(file, initial_size);
			}
			break;
		
		case SYS_OPEN: {
			const char* file = *(char**)(f->esp + sizeof(char*));
			f->eax = open(file);
			}
			break;
		
		case SYS_CLOSE: {

			// Validate the stack
			if (!is_valid_buffer(f->esp + 4, sizeof(int))) {
				exit(-1);
			}

			int fd = *(int*)(f->esp + sizeof(int));
			close(fd);
			}
			break;
		
		case SYS_WRITE: {

			// Validate the stack
			if (!is_valid_buffer(f->esp, 4 * sizeof(int))) {
				exit(-1);
			}

			int fd = *(int*)(f->esp + sizeof(int));
			const void* buffer = *(void**)(f->esp + sizeof(int) + sizeof(void*));
			unsigned size = *(unsigned*)(f->esp + sizeof(int) + sizeof(void*) + sizeof(unsigned));
			f->eax = write(fd, buffer, size);
			}
			break;
		
		case SYS_READ: {
			int fd = *(int*)(f->esp + sizeof(int));
			void* buffer = *(void**)(f->esp + sizeof(int) + sizeof(void*));
			unsigned size = *(unsigned*)(f->esp + sizeof(int) + sizeof(void*) + sizeof(unsigned));
			f->eax = read(fd, buffer, size);
			}
			break;
		
		case SYS_REMOVE: {
			const char* file_name = *(char**)(f->esp + sizeof(char*));
			f->eax = remove(file_name);
			}
			break;
		
		case SYS_FILESIZE: {
			int fd = *(int*)(f->esp + sizeof(int));
			f->eax = filesize(fd);
			}
			break;
		
		case SYS_SEEK: {
			int fd = *(int*)(f->esp + sizeof(int));
			unsigned position = *(unsigned*)(f->esp + sizeof(int) + sizeof(unsigned));
			seek(fd, position);
			}
			break;
		
		case SYS_TELL: {
			int fd = *(int*)(f->esp + sizeof(int));
			f->eax = tell(fd);
			}
			break;
		
		case SYS_EXIT: {

			// Make sure argument is a valid pointer
			if (!is_valid_ptr(f->esp + sizeof(int))) {
				exit(-1);
			}

			int status = *(int*)(f->esp + sizeof(int));
			exit(status);
			}
			break;

		case SYS_EXEC: {

			// Validate the stack
			if (!is_valid_buffer(f->esp, 2 * sizeof(int))) {
				exit(-1);
			}

			// Get the argument pointer without dereferencing
			char** cmd_line_ptr = (char**)(f->esp + sizeof(char*));
			// Check if cmd_line_ptr itself is valid before dereferencing
			if (!is_valid_ptr(cmd_line_ptr)) {
				exit(-1);
			}

			const char* cmd_line = *(char**)(f->esp + sizeof(char*));
			f->eax = exec(cmd_line);
			}
			break;
		
		case SYS_WAIT: {
			int pid = *(int*)(f->esp + sizeof(int));
			f->eax = wait(pid);
			}
			break;
		
		default: {
			exit(-1);
		}
	}

	//thread_exit();
}

/* Validates if a pointer is below PHYS_BASE and in the current process' page table */
bool is_valid_ptr(const void* ptr) {
	if (ptr >= PHYS_BASE) {
		return false;
	}

	// Validate the first page
	if (pagedir_get_page(thread_current()->pagedir, ptr) == NULL) {
		return false;
	}

	int length = sizeof(void*);
	void* upper_bound = pg_round_up(ptr);
	void* page;

	// Validate the next pages
	while ((ptr + length) > upper_bound) {

		// Make sure the next page is valid in pagedir
		upper_bound++;
		page = pagedir_get_page(thread_current()->pagedir, upper_bound);
		if (page == NULL) {
			return false;
		}

		// Upper bound is valid page in the pagedir, update upper bound
		upper_bound = pg_round_up(page);
	}

	// If we get here, we know all the pages the string spans over are valid
	return true;
}

/* Validates if a string has a terminator character */
bool is_valid_string(const char* s) {
	
	bool valid_ptr = is_valid_ptr(s);
	if (!valid_ptr) return false;

	// Find out the length of the string
	int length = strlen(s);

	// Find the end of the page
	void* upper_bound = pg_round_up(s);
	void* page;

	// Loop while string might be out of the page, but still in a valid page
	while ((void*)(s + (length * sizeof(char))) > upper_bound) {
		
		// Make sure the next page is valid in pagedir
		upper_bound++;
		page = pagedir_get_page(thread_current()->pagedir, upper_bound);
		if (page == NULL) {
			return false;
		}

		// Upper bound is a valid page in the pagedir, update the upper_bound
		upper_bound = pg_round_up(page);
	}

	// If we get here, we know all the pages the string spans over are valid
	return true;
}

bool is_valid_buffer(const void* buffer, unsigned length) {

	bool valid_ptr = is_valid_ptr(buffer);
	if(!valid_ptr) return false;

	// Find out how long the buffer is
	unsigned size = length * sizeof(char);

	void* upper_bound = pg_round_up(buffer); 
	void* page;

	// Loop while buffer might be out of the page, but still in a valid page
	while ((void*)(buffer + size) > upper_bound) {
		
		// Make sure next page is valid in the pagedir
		upper_bound++;
		page = pagedir_get_page(thread_current()->pagedir, upper_bound);
		if (page == NULL) {
			return false;
		}

		// Upper bound is a valid page in the pagedir, update the upper_bound
		upper_bound = pg_round_up(page);
	}

	// If we get here, we know all the pages the buffer spans over are valid
	return true;
}

bool is_valid_fd(int fd) {
	bool result = 2 <= fd && fd <= 129;
	return result;
}

void sleep(int millis) {
	timer_msleep(millis);
}


void halt(void) {
	shutdown_power_off();
}


bool create(const char* file, unsigned initial_size) {

	// Validate the input
	if (!is_valid_string(file)) {
		exit(-1);
	}

	// Handle invalid inputs
	if (file == NULL) {
		return false;
	}
	// Create the file using filesys and return the result
	bool success = filesys_create(file, initial_size);
	return success;
}


int open(const char* file) {

	// Validate the string
	if (!is_valid_string(file)) {
		exit(-1);
	}

	struct thread* thread = thread_current();

	// Open the file from filesys
	struct file* f = filesys_open(file);

	// Assure the file exists
	if (f) {
		
		// Find a null item
		for (int i = 0; i < MAX_FILES; i++) {
			if (thread->OPEN_FILES[i] == NULL) {

				// Add file to array
				thread->OPEN_FILES[i] = f;

				// Return an offset value to account for fd 0 and 1 (STDIN / STDOUT)
				return i + 2;

			}
		}
	}

	// Else return -1
	return -1;

	
}


void close(int fd) {

	// Validate the fd
	if (!is_valid_fd(fd)) {
		exit(-1);
	}

	struct thread* thread = thread_current();

	// re-offset the fd value to get the right index
	fd -= 2;

	// Retrieve the pointer to the file
	struct file* f = thread->OPEN_FILES[fd];

	// Close it
	file_close(f);

	// Mark file in array as NULL
	thread->OPEN_FILES[fd] = NULL; 
	
}


int write(int fd, const void* buffer, unsigned size) {

	// Check if buffer is valid
	if(!is_valid_buffer(buffer, size)) {
		exit(-1);
	}
	
	struct thread* thread = thread_current();

	// We get in here if we want to write to the standard output
	if (fd == 1) {
		putbuf((const char*) buffer, size);
		return size;
	}

	// Validate the fd after stdout call
	if (!is_valid_fd(fd)) {
		exit(-1);
	}

	// Here we are sure we want to write to a file
	// Get the file pointer
	struct file* f = thread->OPEN_FILES[fd - 2];

	// If the file exists, write to it
	if (f) {
		int bytes_written = (int) file_write(f, buffer, size);
		return bytes_written;
	}

	// If file does not exist, return -1
	return -1;

}


int read(int fd, void* buffer, unsigned size) {

	// Check if buffer is valid
	if(!is_valid_buffer(buffer, size)) {
		exit(-1);
	}

	struct thread* thread = thread_current();

	if (fd == 0) {
		int bytes_read = read_from_stdin((char*) buffer, size);
		return bytes_read;
	}

	// Deny reading from STDOUT
	else if (fd == 1) {
		return -1;
	}

	// Validate the fd
	if (!is_valid_fd(fd)) {
		exit(-1);
	}

	// Check if file is closed
	// If so, return -1
	else if (thread->OPEN_FILES[fd - 2] == NULL) {
		return -1;
	}

	struct file* f = thread->OPEN_FILES[fd - 2];

	int bytes_read = file_read(f, buffer, size);
	return bytes_read;

}

int read_from_stdin(char* buffer, int size) {
	
	for (int i = 0; i < size; i++) {
		buffer[i] = input_getc();

		// Replace \r with \n
		if (buffer[i] == '\r') {
			buffer[i] = '\n';
		}
		write(1, &(buffer[i]), 1);
	}
	
	return size;
}


bool remove(const char* file_name) {

	// Validate the string
	if (!is_valid_string(file_name)) {
		exit(-1);
	}

	bool success = filesys_remove(file_name);
	return success;
}


int filesize(int fd) {

	// Validate the fd
	if (!is_valid_fd(fd)) {
		exit(-1);
	}

	struct thread* thread = thread_current();

	struct file* f = thread->OPEN_FILES[fd - 2];
	int size = file_length(f);
	return size;
}


void seek(int fd, unsigned position) {

	// Validate the fd
	if (!is_valid_fd(fd)) {
		exit(-1);
	}

	struct thread* thread = thread_current();

	struct file* f = thread->OPEN_FILES[fd - 2];
	unsigned size = file_length(f);

	// If the position is larger than the file size (position would be out of file)
	// Then we don't want to change position
	if (position < size) {
		file_seek(f, position);
	}
}


unsigned tell(int fd) {

	// Validate the fd
	if (!is_valid_fd(fd)) {
		exit(-1);
	}

	struct thread* thread = thread_current();

	struct file* f = thread->OPEN_FILES[fd - 2];

	unsigned offset = file_tell(f);
	return offset;
}


void exit(int status) {
	struct thread* thread = thread_current();
	if (thread->parent_child != NULL) {
		thread->parent_child->child_exit_status = status;
	}

	thread_exit();
}

pid_t exec(const char* cmd_line) {

	// Validate the string
	if (!is_valid_string(cmd_line)) {
		exit(-1);
	}
	tid_t tid = process_execute(cmd_line);
	return tid;
}

int wait(int pid) {
	return process_wait(pid);
}


