# Pintos
This repository is an implementation of the Pintos operating system developed at Stanford University for the x86 architechture. I did not follow the standardized exercises provided by Stanford, and instead followed the instructions provided by my own university. Therefore, the implementation might differ a bit from a standard Pintos implementation. But, it is still quite similar.

## Exercises
Here is a VERY SHORT SUMMARY of all the exercises and features I have added

### Exercise 1: Setting up the stack
Every process uses a stack. The stack will store data, return addresses and manage the execution of processes. This first exercise was to set up the "base" of the stack.

When writing a program in the C programming language, the *main* function will be called with two arguments, *argv* and *argc*.

 - **argv** is of type `char**` and is a pointer to an array of pointers pointing to the actual data.
 - **argc** is of type `int` and is how long the **argv** argument is.

This exercise mainly involved setting up this stack and the *argv* and *argc* parameters correctly.

The code that handles this is under `src/userprog/process.c` in the `start_process()` function on line 120.

### Exercise 2: Basic system calls
At any time, a process can make a system call (syscall). A syscall means that the process wants to do something *"dangerous"* that should be performed in a controlled manner. This could be writing, reading, creating, removing a file on the disk, terminating the process etc.

Here is a list of all the syscalls I have added in this exercise:
 - **create**: creates a file.
 - **open**: opens a file.
 - **close**: closes a file.
 - **read**: reads from a file or the console (the keyboard)
 - **write**: writes to a file or the console(the monitor)
 - **halt**: halts the processor.
 - **sleep**: pause the execution of a process for a given time.
 - **exit**: terminates a program and deallocates resources occupied by the program, for example, closes all files opened up the program
 - **seek**: sets position in a file for read and write system calls.
 - **tell**: returns the position in a file.
 - **filesize**: returns the file size.
 - **remove**: removes a file from the file system

These syscalls were all added in the file `src/userprog/syscall.c`
Note that some other syscalls are also added, like **exec** and **wait**, as well as some validation functions, e.g `is_valid_ptr()` and so on. These were all added in later exercises.

### Exercise 3: Basic synchronization
This exercise gave an introduction to synchronization. Previously, when a process called the **sleep** syscall the running thread would do a "[busy wait](https://en.wikipedia.org/wiki/Busy_waiting)" which blocks the CPU from performing other operations during this sleep time. I implemented a sorted list that keeps track of all the *threads* currently sleeping, with the thread to wake up the soonest in the front of the list. This way, isntead of a busy wait, all the threads would allow the CPU to perform other operations while they were sleeping.

This implementation was added in the file `src/devices/timer.c` in the functions `timer_sleep()` and `timer_interrupt()`.

### Exercise 4: The **exec** syscall
This syscall is used when a process wants to create a child process, essentially a new *thread*. This exercise also focused more on synchronization where I used [semaphores](https://en.wikipedia.org/wiki/Semaphore_(programming)), a *synchronization primitive*. The main crux of this exercise is: when a parent creates a child, the parent has to know **when** the child has successfully been created. The parent will know this with the use of a semaphore.

Part of this exercise was implemented in `src/userprog/syscall.c` to handle the **exec** syscall, but also in `src/userprog/process.c` in the function `process_execute()`. The `struct shared_variables` was used to keep this synchronization between the parent and child

### Exercise 5: Implementing the **wait** syscall and extending the **exit** syscall
This exercise built on the previously implemented **exec** syscall, when a process wants to create a child process. The parent has to know about the status of the child, such as if it has terminated, its return value etc. I kept track of this using the `struct parent_child` in the file `src/threads/thread.h`

Here is what the **wait** and **exit** syscalls do:
 - **wait**: Waits for the given child process to terminate. Returns the status of the child process
 - **exit**: Terminates the process. Before, this just meant closing all related files, but now we need might need to clear up some memory due to the `malloc()` that was used in `src/userprog/process.c` on line 78.

I also added validation of the input arguments to syscalls in `src/userprog/syscall.c` (all the `_valid_` functions), which was a huge headache ヽ(ﾟДﾟ)ﾉ

This exerciese added the new **wait** syscall in `src/userprog/syscall.c` and extended the `process_execute()`, `process_wait()` and `process/exit()` functions in `src/userprog/process.c`.

### Exercise 6: File system synchronization
The final exercise. This exercise made me synchronize access to the file system. Say many different threads wants to read and write to a single file at practically the same time; of course that needs to be synchronized somehow. This problem is based on the [readers-writers problem](https://en.wikipedia.org/wiki/Readers%E2%80%93writers_problem). My implementation, as instructed, uses the *first* reader-writers problem, prioritizing *readers* over *writers*.

All of this solution was added to the `src/filesys/inode.c` file. The `struct synch` was added to keep readers and writers synchronized, and the `static struct lock list_lock` was added to keep the list of open inode's synchronized.

## Summary
Implementing this version of pintos made me understand the underlying concepts of operating systems and synchronization. There is A LOT im leaving out of this README, simply because it is too much.

Debugging this was a nightmare (ᗒᗣᗕ)՞