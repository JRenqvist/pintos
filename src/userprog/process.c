#include "userprog/process.h"

#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "threads/synch.h"
#include "lib/kernel/list.h"
#include "threads/malloc.h"
#include "userprog/syscall.h"

#include <stdlib.h>
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <string.h>

struct shared_variables {
	struct semaphore sema;
	bool success;
	tid_t child_id;
	char* cmd_line_;
};

static thread_func start_process NO_RETURN;
static bool load(const char* file_name, void (**eip)(void), void** esp);
static void dump_stack(const void* esp);
void close_files(struct thread* cur);
void cleanup_children(struct thread* cur);


/* Starts a new thread running a user program loaded from
	CMD_LINE.  The new thread may be scheduled (and may even exit)
	before process_execute() returns.  Returns the new process's
	thread id, or TID_ERROR if the thread cannot be created. */
tid_t process_execute(const char* cmd_line)
{

	char* cl_copy;
	tid_t tid;

	// Context of parent
	struct shared_variables shared;
	sema_init(&shared.sema, 0);
	cl_copy = shared.cmd_line_ = palloc_get_page(0);

	if (cl_copy == NULL)
		return TID_ERROR;
	strlcpy(cl_copy, cmd_line, PGSIZE);

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create(cmd_line, PRI_DEFAULT, start_process, &shared);
	

	if (tid == TID_ERROR) {
		palloc_free_page(cl_copy);
		return -1;
	}

	shared.child_id = tid;

	// Now child has been created and started
	// either successfully or unsuccessfully

	// Ensure child is successfully created
	

	// Initialize parent_child relation
	struct parent_child* parent_child = (struct parent_child*)malloc(sizeof(struct parent_child));
	parent_child->child_id = shared.child_id;
	parent_child->alive_count = 2;
	parent_child->child_exit_status = -1;
	sema_init(&parent_child->sema, 0);	// When child exits, up sema. Parent downs sema while in wait call
	lock_init(&parent_child->alive_lock);

	struct thread* parent = thread_current();

	// Get the child thread and add a pointer to the shared data
	struct thread* child = thread_by_tid(shared.child_id);
	child->parent_child = parent_child;
	list_push_back(&parent->children, &parent_child->elem);
	sema_down(&shared.sema);	// Wait for the child process to start


	if (!shared.success) {
		return -1;
	}
	
	return tid;
}

/* A thread function that loads a user process and starts it
	running. */
static void start_process(void* cmd_line_)
{


	struct shared_variables* shared = (struct shared_variables*) cmd_line_;

	char* cmd_line = shared->cmd_line_;
	struct intr_frame if_;
	bool success;

	/* Initialize interrupt frame and load executable. */
	memset(&if_, 0, sizeof if_);
	if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
	if_.cs = SEL_UCSEG;
	if_.eflags = FLAG_IF | FLAG_MBS;

	//////////////////////////////// 
	//Lab 1:

	// Initialize argv array
	int MAX = 32;
	char* argv[MAX];
	

	// Parse the command line arguments and add to argv array
	char *token, *save_ptr;
	int i = 0;
	for (token = strtok_r(cmd_line, " ", &save_ptr); token != NULL; token = strtok_r(NULL, " ", &save_ptr)) {
		/*int token_size = strlen(token) + 1;	// Calculate token size
		if_.esp -= token_size;				// Change stack pointer
		memcpy(if_.esp, token, token_size);	// Copy to stack
		i++;*/
		argv[i] = token;
		i++;
	}

	char* nullPtr = NULL;
	argv[i] = nullPtr;
	int argc = i;

	int memory_size = 0;

	success = load(argv[0], &if_.eip, &if_.esp);

	
	if (success) {
		// Add to stack
		void* argv_elements_pointers[argc];

		// Add strings to the top of the stack
		for (int i = argc-1; i >= 0; i--) {
			memory_size = (strlen(argv[i]) + 1) * sizeof(char);
			if_.esp -= memory_size;
			strlcpy(if_.esp, argv[i], memory_size);
			argv_elements_pointers[i] = if_.esp;
		}

		// Align
		while ((int)if_.esp % 4 != 0) {
			if_.esp -= 1;
		}

		// Add NULL pointer at the "end" of the argv array
		if_.esp -= sizeof(char*);

		// Add argv item pointers to stack
		for (int i = argc-1; i >= 0; i--) {
			memory_size = sizeof(char*);
			
			if_.esp -= memory_size;
			memcpy(if_.esp, &argv_elements_pointers[i], memory_size);
		}

		// Add the double pointer char** argv to the stack
		char** argv_double_pointer = if_.esp;
		memory_size = sizeof(char**);
		if_.esp -= memory_size;
		memcpy(if_.esp, &argv_double_pointer, memory_size);

		// Add argc to the stack
		memory_size = sizeof(int);
		if_.esp -= memory_size;
		memcpy(if_.esp, &argc, memory_size);
		
		// Add return address to the stack
		if_.esp -= sizeof(void*);

	}


	// End Lab 1
	/////////////////////////////////
//gvb
	

	// Note: load requires the file name only, not the entire cmd_line
	//success = load(t->name, &if_.eip, &if_.esp);

	// Update success value of struct
	shared->success = success;
	sema_up(&shared->sema);

	/* If load failed, quit. */
	palloc_free_page(cmd_line);
	if (!success)
		exit(-1);

	/* Start the user process by simulating a return from an
		interrupt, implemented by intr_exit (in
		threads/intr-stubs.S).  Because intr_exit takes all of its
		arguments on the stack in the form of a `struct intr_frame',
		we just point the stack pointer (%esp) to our stack frame
		and jump to it. */
	asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
	NOT_REACHED();
}

/* Waits for thread TID to die and returns its exit status.  If
	it was terminated by the kernel (i.e. killed due to an
	exception), returns -1.  If TID is invalid or if it was not a
	child of the calling process, or if process_wait() has already
	been successfully called for the given TID, returns -1
	immediately, without waiting.

	This function will be implemented in problem 2-2.  For now, it
	does nothing. */
int process_wait(tid_t child_tid UNUSED)
{
	struct thread* parent = thread_current();
	struct list_elem* e;
	struct parent_child* pc_relation = NULL;

	// Loop over list of children to this thread. Find child with argument child_tid
	for (e = list_begin(&parent->children); e != list_end(&parent->children); e = list_next(e)) {
		struct parent_child* pc = list_entry(e, struct parent_child, elem);
		if (pc->child_id == child_tid) {
			pc_relation = pc;
			list_remove(e);
			break;
		}
	}

	// If we didn't find a relation, exit.
	if (pc_relation == NULL) {
		return -1;
	}
	// Wait for child to finish
	sema_down(&pc_relation->sema);

	int status = pc_relation->child_exit_status;
	return status;
}

/* Free the current process's resources. */
void process_exit(void)
{
	struct thread* cur = thread_current();

	// Lock while decrementing alive count
	if (cur->parent_child == NULL) {
		// If cur is root node, just clean up its children and close files.
		// Function makes sure alive_count is == 0
		cleanup_children(cur);
		close_files(cur);

	} else {


		// VERY IMPORTANT PRINT DO NOT REMOVE
		printf("%s: exit(%d)\n", cur->name, cur->parent_child->child_exit_status);

		// If cur has a parent, decrease its alive count.
		lock_acquire(&cur->parent_child->alive_lock);
		cur->parent_child->alive_count -= 1;
		lock_release(&cur->parent_child->alive_lock);


		// Signal parent to continue
		sema_up(&cur->parent_child->sema);
		close_files(cur);
		
		// Clean up the children if alive count is 0
		if (cur->parent_child->alive_count == 0) {
			cleanup_children(cur);
			free(cur->parent_child);
		}
	}

	
	uint32_t* pd;

	/* Destroy the current process's page directory and switch back
		to the kernel-only page directory. */
	pd = cur->pagedir;
	if (pd != NULL) {
		/* Correct ordering here is crucial.  We must set
			cur->pagedir to NULL before switching page directories,
			so that a timer interrupt can't switch back to the
			process page directory.  We must activate the base page
			directory before destroying the process's page
			directory, or our active page directory will be one
			that's been freed (and cleared). */
		cur->pagedir = NULL;
		pagedir_activate(NULL);
		pagedir_destroy(pd);
	}
}

void close_files(struct thread* cur) {
	// close any open file related to current thread
	for (int i = 0; i < MAX_FILES; i++){

		if(cur->OPEN_FILES[i] != NULL){
			file_close(cur->OPEN_FILES[i]);
		}
	}
}

void cleanup_children(struct thread* cur) {
	// Loop over the list of children to this thread, and decrement the alive_count by 1
	struct list_elem* e;
    struct parent_child* pc;
    
    // Iterate through the list of children
    for (e = list_begin(&cur->children); e != list_end(&cur->children); e = list_next(e)) {
        pc = list_entry(e, struct parent_child, elem);
        // Decrement the alive count 
		// TODO: maybe lock this line?
		lock_acquire(&pc->alive_lock);
        pc->alive_count--;
		lock_release(&pc->alive_lock);

        // Check if the child has finished (alive_count == 0)
        if (pc->alive_count == 0) {
            // Child has finished, perform cleanup
            // This could involve freeing memory, closing files, etc.
            // Freeing the struct parent_child
            list_remove(e);
            free(pc);
        }
    }
}

/* Sets up the CPU for running user code in the current
	thread.
	This function is called on every context switch. */
void process_activate(void)
{
	struct thread* t = thread_current();

	/* Activate thread's page tables. */
	pagedir_activate(t->pagedir);

	/* Set thread's kernel stack for use in processing
		interrupts. */
	tss_update();
}

/* We load ELF binaries.  The following definitions are taken
	from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32 /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32 /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32 /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16 /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
	This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr {
	unsigned char e_ident[16];
	Elf32_Half e_type;
	Elf32_Half e_machine;
	Elf32_Word e_version;
	Elf32_Addr e_entry;
	Elf32_Off e_phoff;
	Elf32_Off e_shoff;
	Elf32_Word e_flags;
	Elf32_Half e_ehsize;
	Elf32_Half e_phentsize;
	Elf32_Half e_phnum;
	Elf32_Half e_shentsize;
	Elf32_Half e_shnum;
	Elf32_Half e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
	There are e_phnum of these, starting at file offset e_phoff
	(see [ELF1] 1-6). */
struct Elf32_Phdr {
	Elf32_Word p_type;
	Elf32_Off p_offset;
	Elf32_Addr p_vaddr;
	Elf32_Addr p_paddr;
	Elf32_Word p_filesz;
	Elf32_Word p_memsz;
	Elf32_Word p_flags;
	Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL	 0				/* Ignore. */
#define PT_LOAD	 1				/* Loadable segment. */
#define PT_DYNAMIC 2				/* Dynamic linking info. */
#define PT_INTERP	 3				/* Name of dynamic loader. */
#define PT_NOTE	 4				/* Auxiliary info. */
#define PT_SHLIB	 5				/* Reserved. */
#define PT_PHDR	 6				/* Program header table. */
#define PT_STACK	 0x6474e551 /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

static bool setup_stack(void** esp);
static bool validate_segment(const struct Elf32_Phdr*, struct file*);
static bool load_segment(
	 struct file* file,
	 off_t ofs,
	 uint8_t* upage,
	 uint32_t read_bytes,
	 uint32_t zero_bytes,
	 bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
	Stores the executable's entry point into *EIP
	and its initial stack pointer into *ESP.
	Returns true if successful, false otherwise. */
bool load(const char* file_name, void (**eip)(void), void** esp)
{
	struct thread* t = thread_current();
	struct Elf32_Ehdr ehdr;
	struct file* file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pagedir = pagedir_create();
	if (t->pagedir == NULL)
		goto done;
	process_activate();

	/* Open executable file. */
	file = filesys_open(file_name);
	if (file == NULL) {
		printf("load: %s: open failed\n", file_name);
		goto done;
	}

	/* Read and verify executable header. */
	if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr
		 || memcmp(ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2
		 || ehdr.e_machine != 3 || ehdr.e_version != 1
		 || ehdr.e_phentsize != sizeof(struct Elf32_Phdr) || ehdr.e_phnum > 1024) {
		printf("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Elf32_Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length(file))
			goto done;
		file_seek(file, file_ofs);

		if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment(&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint32_t file_page = phdr.p_offset & ~PGMASK;
					uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint32_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
							Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes
							 = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
					}
					else {
						/* Entirely zero.
							Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment(
							  file,
							  file_page,
							  (void*) mem_page,
							  read_bytes,
							  zero_bytes,
							  writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack(esp))
		goto done;

	/* Start address. */
	*eip = (void (*)(void)) ehdr.e_entry;

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	file_close(file);
	return success;
}

/* load() helpers. */

static bool install_page(void* upage, void* kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
	FILE and returns true if so, false otherwise. */
static bool validate_segment(const struct Elf32_Phdr* phdr, struct file* file)
{
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (Elf32_Off) file_length(file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
		user address space range. */
	if (!is_user_vaddr((void*) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr((void*) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
		address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
		Not only is it a bad idea to map page 0, but if we allowed
		it then user code that passed a null pointer to system calls
		could quite likely panic the kernel by way of null pointer
		assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

/* Loads a segment starting at offset OFS in FILE at address
	UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
	memory are initialized, as follows:

		  - READ_BYTES bytes at UPAGE must be read from FILE
			 starting at offset OFS.

		  - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

	The pages initialized by this function must be writable by the
	user process if WRITABLE is true, read-only otherwise.

	Return true if successful, false if a memory allocation error
	or disk read error occurs. */
static bool load_segment(
	 struct file* file,
	 off_t ofs,
	 uint8_t* upage,
	 uint32_t read_bytes,
	 uint32_t zero_bytes,
	 bool writable)
{
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(upage) == 0);
	ASSERT(ofs % PGSIZE == 0);

	file_seek(file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Calculate how to fill this page.
			We will read PAGE_READ_BYTES bytes from FILE
			and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t* kpage = palloc_get_page(PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read(file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page(kpage);
			return false;
		}
		memset(kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page(upage, kpage, writable)) {
			palloc_free_page(kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
	user virtual memory. */
static bool setup_stack(void** esp)
{
	uint8_t* kpage;
	bool success = false;

	kpage = palloc_get_page(PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page(((uint8_t*) PHYS_BASE) - PGSIZE, kpage, true);
		if (success)
			*esp = PHYS_BASE;
		else
			palloc_free_page(kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
	virtual address KPAGE to the page table.
	If WRITABLE is true, the user process may modify the page;
	otherwise, it is read-only.
	UPAGE must not already be mapped.
	KPAGE should probably be a page obtained from the user pool
	with palloc_get_page().
	Returns true on success, false if UPAGE is already mapped or
	if memory allocation fails. */
static bool install_page(void* upage, void* kpage, bool writable)
{
	struct thread* t = thread_current();

	/* Verify that there's not already a page at that virtual
		address, then map our page there. */
	return (
		 pagedir_get_page(t->pagedir, upage) == NULL
		 && pagedir_set_page(t->pagedir, upage, kpage, writable));
}

// Don't raise a warning about unused function.
// We know that dump_stack might not be called, this is fine.

#pragma GCC diagnostic ignored "-Wunused-function"
/* With the given stack pointer, will try and output the stack to STDOUT. */
static void dump_stack(const void* esp)
{
	printf("*esp is %p\nstack contents:\n", esp);
	hex_dump((int) esp, esp, PHYS_BASE - esp + 16, true);
	/* The same information, only more verbose: */
	/* It prints every byte as if it was a char and every 32-bit aligned
		data as if it was a pointer. */
	void* ptr_save = PHYS_BASE;
	int i = -15;
	while (ptr_save - i >= esp) {
		char* whats_there = (char*) (ptr_save - i);
		// show the address ...
		printf("%x\t", (uint32_t) whats_there);
		// ... printable byte content ...
		if (*whats_there >= 32 && *whats_there < 127)
			printf("%c\t", *whats_there);
		else
			printf(" \t");
		// ... and 32-bit aligned content
		if (i % 4 == 0) {
			uint32_t* wt_uint32 = (uint32_t*) (ptr_save - i);
			printf("%x\t", *wt_uint32);
			printf("\n-------");
			if (i != 0)
				printf("------------------------------------------------");
			else
				printf(" the border between KERNEL SPACE and USER SPACE ");
			printf("-------");
		}
		printf("\n");
		i++;
	}
}
#pragma GCC diagnostic pop
