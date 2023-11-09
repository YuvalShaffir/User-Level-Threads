# User-Level-Threads
![image](https://github.com/YuvalShaffir/User-Level-Threads/assets/34415892/6549a7f2-6c36-4fbd-8de2-4a08a59ab33f)

An implementation of user-level threads in C++.
This library is able to create and manage user-level threads. 

## Features
- The user is able to include the library and use its public interface (shown in uthreads header file).
- At any given time during the running of the user's program, each thread in the program is in one of the states shown in the following state diagram. Transitions from state to state occur as a result of calling one of the library functions, or from elapsing of time, as explained below.
  ![image](https://github.com/YuvalShaffir/User-Level-Threads/assets/34415892/eaea6f98-90a3-4784-a5e5-cc40b0b037c7)
- The scheduler uses an implementation of the Round-Robin algorithm.

## Functions
uthread_init(int quantum_usecs);
- initializes the thread library.
  
uthread_spawn(thread_entry_point entry_point);
- Creates a new thread, whose entry point is the function entry_point with the signature: *void entry_point(void).

uthread_terminate(int tid);
- Terminates the thread with ID tid and deletes it from all relevant control structures.

uthread_block(int tid);
-  Blocks the thread with ID tid. The thread may be resumed later using uthread_resume.

uthread_resume(int tid);
- Resumes a blocked thread with ID tid and moves it to the READY state.

uthread_sleep(int num_quantums);
- Blocks the RUNNING thread for num_quantums quantums.

uthread_get_tid();
- Returns the thread ID of the calling thread.

uthread_get_total_quantums();
- Returns the total number of quantums since the library was initialized, including the current quantum.

uthread_get_quantums(int tid);
- Returns the number of quantums the thread with ID tid was in RUNNING state.
- 
## How to use
- Include the library in your work file.
  
