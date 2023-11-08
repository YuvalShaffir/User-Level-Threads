#include "uthreads.h"
#include <queue>
#include <vector>
#include <cstring>
#include <sys/time.h>
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <setjmp.h>
#include <unistd.h>
#include <stdbool.h>
#include <map>
#include <memory>
#include <string>
//#include "thread_manager.h"

/*-------------- definitions: -------------- */
#define SUCCESS 0
#define FAILURE (-1)
#define MAIN_THREAD 0
#define INVALID_TID_MSG "invalid tid."
#define INVALID_INPUT "invalid input."
#define ERROR_MSG_LIB(txt) fprintf(stderr, "thread library error:  %s\n", txt);
#define ERROR_MSG_SYS(txt) fprintf(stderr, "system error:  %s\n", txt);
#define RESTART_TIMER_ERROR "timer restart failed"
#define SIGNAL_CHANGE_ERROR "signal mask change error"
#define SIGEMPTYSET_ERR "sigemptyset error."
#define SIGADDSET_ERR "sigaddset error."
#define SIGACTION_ERR "sigaction error."
#define RESET_TIMER_ERR "reset timer error."
#define ALLOCATION_ERR "allocation failed."
#define CHNG_SIG_MASK_ERR "unable to change signal mask."
#define THREAD_CREATION_ERR "thread creation failed."
#define BLOCKING_MAIN_THREAD_ERROR "blocking main thread is forbidden."
#define NEG_QUANTUMS_ERROR "Negative quantums."
#define MAX_THREADS_ERR "max threads reached."
#define SLEEPING_MAIN_THREAD_ERROR "main thread cannot sleep."
#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
            : "=g" (ret)
            : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5


/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
  address_t ret;
  asm volatile("xor    %%gs:0x18,%0\n"
               "rol    $0x9,%0\n"
  : "=g" (ret)
  : "0" (addr));
  return ret;
}
#endif

#define SECOND 1000000
enum Action {
    RESUME,
    TERMINATE,
    BLOCK,
    SLEEP,
    PREEMPT
};

enum State {
    READY,
    RUNNING,
    BLOCKED,
    SLEEPING,
    SLEEPING_AND_BLOCKED
};

/* -------------- typedefs: -------------- */
typedef struct thread{
    unsigned int tid;
    thread_entry_point entry_point;
    State state;
    char *stack;
    int quantum_counter = 0;
    sigjmp_buf data;
}thread;

typedef std::priority_queue<int, std::vector<int>, std::greater<int>> pr_queue;
typedef std::queue<int> queue;
typedef std::map<int, thread*> map;
typedef std::map<int, int> map_int;

/* -------------- static variables: -------------- */
static map threads;
static pr_queue id_queue;
static map_int sleep_threads;
static queue ready_queue;
int total_quantum_num = 1;
int current_thread;
static struct itimerval timer;
static struct sigaction sa = {0};
static sigset_t set;

/* -------------- timer declarations: -------------- */
int reset_timer();

void free_and_exit();

/* -------------- helper functions -------------- */
void init_id_q()
{
    for (int i = 0; i < MAX_THREAD_NUM; ++i) {
        id_queue.push(i);
    }
}


void free_resources()
{
    if(!threads.empty())
    {
        for (auto &thread : threads) {
            if(thread.second->stack != nullptr)
            {
                delete thread.second->stack;
            }
            if(threads[thread.first] != nullptr)
            {
                delete threads[thread.first];
                threads[thread.first] = nullptr;
            }
        }
    }
}


void free_and_exit() {
    free_resources();
    _exit(1);
}

int set_next_ready_to_running() {
    if(ready_queue.empty())
    {
        ERROR_MSG_LIB(READY_Q_EMPTY_ERROR)
        return FAILURE;
    }
    int id = ready_queue.front();
    thread* t = threads[id];
    ready_queue.pop();
    t->state = RUNNING;

    //set the current thread to the next thread
    current_thread = id;
    t->quantum_counter++;
    return SUCCESS;
}

void set_running_to_state(State state) {
    if (threads[current_thread]->state == RUNNING)
    {
        threads[current_thread]->state = state;

        if (state == READY)
        {
            ready_queue.push(current_thread);
        }
    }
}

void update_sleep_quantum()
{
    if(sleep_threads.empty())
    {
        return;
    }
    std::vector<int> to_delete;
    for (auto it = sleep_threads.begin(); it != sleep_threads.end(); ++it)
    {
        it->second--;
        if (it->second <= 0)
        {
            switch(threads[it->first]->state)
            {
                case SLEEPING_AND_BLOCKED:
                    threads[it->first]->state = BLOCKED;
                    to_delete.push_back(it->first);
                    break;
                case SLEEPING:
                    threads[it->first]->state = READY;
                    ready_queue.push(it->first);
                    to_delete.push_back(it->first);
                    break;
                default:
                    break;
            }
        }
    }
    for (auto &i : to_delete) {
        sleep_threads.erase(i);
    }
}

int fill_thread(thread* t, thread_entry_point entry_point = nullptr, State s = RUNNING)
{

    t->tid = id_queue.top();
    id_queue.pop();
    t->state = s;

    if(t->tid != MAIN_THREAD)
    {
        t->stack = new char[STACK_SIZE];
        if(t->stack == nullptr)
        {
            ERROR_MSG_SYS(ALLOCATION_ERR);
            free_and_exit();
            return FAILURE;
        }
        address_t sp = (address_t) (t->stack) + STACK_SIZE - sizeof(address_t);
        address_t pc = (address_t) entry_point;
        sigsetjmp((t->data), 1);
        ((t->data)->__jmpbuf)[JB_SP] = translate_address(sp);
        ((t->data)->__jmpbuf)[JB_PC] = translate_address(pc);
        sigemptyset(&(t->data)->__saved_mask);
        t->entry_point = entry_point;
    }

    return SUCCESS;
}

void remove_from_ready(int tid)
{
    queue temp;
    while (!ready_queue.empty())
    {
        int id = ready_queue.front();
        ready_queue.pop();
        if (id != tid)
        {
            temp.push(id);
        }
    }
    ready_queue = temp;
}

bool valid_tid(int tid) {
    auto it = threads.find(tid);
    if (it == threads.end() || it->second == nullptr){
        return false;
    }
    return true;
}

/* -------------- signal methods -------------- */
int change_signal_mask(int how)
{
    if(sigprocmask(how, &set, NULL) == FAILURE){
        ERROR_MSG_SYS(SIGNAL_CHANGE_ERROR);
        free_and_exit();
        return FAILURE;
    }
    return SUCCESS;
}

int switch_thread(int action= PREEMPT){

    change_signal_mask(SIG_BLOCK);
    int result;
    switch (action) {
        case PREEMPT:
            if(ready_queue.empty())
            {
                threads[current_thread]->quantum_counter++;
                total_quantum_num ++;
                if(!sleep_threads.empty())
                {
                    update_sleep_quantum();
                }
                return SUCCESS;
            }
            result = sigsetjmp(threads[current_thread]->data, 1);
            if (result == 0) {
                set_running_to_state(READY);
                set_next_ready_to_running();
            }
            if(result == 1) // jumped back
            {
                return change_signal_mask(SIG_UNBLOCK);
            }
            break;

        case BLOCK:
            result = sigsetjmp(threads[current_thread]->data, 1);
            if (result == 0) {
                set_running_to_state(BLOCKED);
                set_next_ready_to_running();
            }
            if(result == 1) // jumped back
            {
                return change_signal_mask(SIG_UNBLOCK);
            }
            break;

        case SLEEP:
            result = sigsetjmp(threads[current_thread]->data, 1);
            if (result == 0) {
                set_running_to_state(SLEEPING);
                set_next_ready_to_running();
            }
            if(result == 1) // jumped back
            {
                return change_signal_mask(SIG_UNBLOCK);
            }
            break;

        case TERMINATE:
            set_next_ready_to_running();
            break;

        default:
            break;
    }

    total_quantum_num++;


    reset_timer();

    if(!sleep_threads.empty()) {
        update_sleep_quantum();
    }
    change_signal_mask(SIG_UNBLOCK);
    siglongjmp(threads[current_thread]->data, 1);
}


int init_signal_set()
{
    if (sigemptyset(&sa.sa_mask) == FAILURE) {
        ERROR_MSG_SYS(SIGEMPTYSET_ERR);
        return FAILURE;
    }
    if (sigemptyset(&set) == FAILURE) {
        ERROR_MSG_SYS(SIGEMPTYSET_ERR);
        return FAILURE;
    }

    if(sigaddset(&set, SIGALRM) == FAILURE){
        ERROR_MSG_SYS(SIGADDSET_ERR);
        return FAILURE;
    }

    return SUCCESS;
}

/* -------------- timer methods -------------- */
void timer_handler(int sig)
{
    switch_thread();
}

int reset_timer()
{
    if (setitimer(ITIMER_VIRTUAL, &timer, NULL))
    {
        ERROR_MSG_SYS(RESTART_TIMER_ERROR);
        free_and_exit();
        return FAILURE;
    }

    return SUCCESS;
}

int create_timer(int quantum_usecs)
{
    // Install timer_handler as the signal handler for SIGVTALRM.
    sa.sa_handler = &timer_handler;
    if (sigaction(SIGVTALRM, &sa, NULL) < 0) {
        ERROR_MSG_SYS(SIGACTION_ERR);
        free_and_exit();
        return FAILURE;
    }
    // Configure the timer to expire after quantum usecs.
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = quantum_usecs ;

    // configure the timer to expire every quantum usecs after that.
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = quantum_usecs;

    return reset_timer();
}

/* -------------- main functions -------------- */
/**
 * @brief initializes the thread library.
 *
 * Once this function returns, the main thread (tid == 0) will be set as RUNNING. There is no need to
 * provide an entry_point or to create a stack for the main thread - it will be using the "regular" stack and PC.
 * You may assume that this function is called before any other thread library function, and that it is called
 * exactly once.
 * The input to the function is the length of a quantum in micro-seconds.
 * It is an error to call this function with non-positive quantum_usecs.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_init(int quantum_usecs)
{
    if (quantum_usecs <= 0)
    {
        ERROR_MSG_LIB(NEG_QUANTUMS_ERROR);
        return FAILURE;
    }

    init_signal_set();

    init_id_q();
    thread* main_thread = new thread;
    fill_thread(main_thread, nullptr);
    main_thread->quantum_counter = 1;

    threads[0] = main_thread;

    create_timer(quantum_usecs);

    return SUCCESS;
}

/**
 * @brief Creates a new thread, whose entry point is the function entry_point with the signature
 * void entry_point(void).
 *
 * The thread is added to the end of the READY threads list.
 * The uthread_spawn function should fail if it would cause the number of concurrent threads to exceed the
 * limit (MAX_THREAD_NUM).
 * Each thread should be allocated with a stack of size STACK_SIZE bytes.
 * It is an error to call this function with a null entry_point.
 *
 * @return On success, return the ID of the created thread. On failure, return -1.
*/
int uthread_spawn(void (*f)(void))
{
    change_signal_mask(SIG_BLOCK);
    //validate input:
    if(f == nullptr)
    {
        ERROR_MSG_LIB(INVALID_INPUT);
        return FAILURE;
    }
    if(threads.size() == MAX_THREAD_NUM)
    {
        ERROR_MSG_LIB(MAX_THREADS_ERR);
        return FAILURE;
    }

    //create new thread:
    thread* new_thread = new thread;
    fill_thread(new_thread, f, READY);

    //add new thread to ready queue and threads map:
    ready_queue.push(new_thread->tid);
    threads[new_thread->tid] = new_thread;

    change_signal_mask(SIG_UNBLOCK);
    return new_thread->tid;
}

/**
 * @brief Terminates the thread with ID tid and deletes it from all relevant control structures.
 *
 * All the resources allocated by the library for this thread should be released. If no thread with ID tid exists it
 * is considered an error. Terminating the main thread (tid == 0) will result in the termination of the entire
 * process using exit(0) (after releasing the assigned library memory).
 *
 * @return The function returns 0 if the thread was successfully terminated and -1 otherwise. If a thread terminates
 * itself or the main thread is terminated, the function does not return.
*/
int uthread_terminate(int tid)
{
    change_signal_mask(SIG_BLOCK);

    //validate input:
    if(!valid_tid(tid))
    {
        ERROR_MSG_LIB(INVALID_TID_MSG);
        return FAILURE;
    }

    //terminate main thread:
    if(tid == MAIN_THREAD)
    {
        free_resources();
        _exit(0);
    }
    if(threads[tid]->state == READY)
    {
        remove_from_ready(tid);
    }

    bool thread_is_running = (threads[tid]->state == RUNNING);

    if(threads[tid]->state == SLEEPING || threads[tid]->state == SLEEPING_AND_BLOCKED)
    {
        sleep_threads.erase(tid);
    }

    if(threads[tid]->stack != nullptr)
    {
        delete threads[tid]->stack;
        threads[tid]->stack = nullptr;
    }
    delete threads[tid]; //free struct
    threads[tid] = nullptr;

    threads.erase(tid);

    id_queue.push(tid);
    if (thread_is_running) {
        switch_thread(TERMINATE);
    }

    return  change_signal_mask(SIG_UNBLOCK);
}

/**
 * @brief Blocks the thread with ID tid. The thread may be resumed later using uthread_resume.
 *
 * If no thread with ID tid exists it is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0).
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_block(int tid)
{
    change_signal_mask(SIG_BLOCK);

    if (!(valid_tid(tid)))
    {
        ERROR_MSG_LIB(INVALID_TID_MSG)
        return FAILURE;
    }

    if(tid == MAIN_THREAD)
    {
        ERROR_MSG_LIB(BLOCKING_MAIN_THREAD_ERROR);
        return FAILURE;
    }

    if (tid == current_thread)
    {
        switch_thread (BLOCK);
    }
    else
    {
        if (threads[tid]->state == SLEEPING)
        {
            threads[tid]->state = SLEEPING_AND_BLOCKED;
        }
        if (threads[tid]->state == READY)
        {
            remove_from_ready(tid);
            threads[tid]->state = BLOCKED;
        }
    }

    return change_signal_mask(SIG_UNBLOCK);
}

/**
 * @brief Resumes the thread with ID tid.
 *
 * If no thread with ID tid exists it is considered as an error. In addition, it is an error to try resuming the
 * main thread (tid == 0). If a thread resumes itself, a scheduling decision should be made. Resuming a thread in
 * READY state has no effect and is not considered an error.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_resume(int tid)
{
    change_signal_mask(SIG_BLOCK);

    if (!(valid_tid(tid)))
    {
        ERROR_MSG_LIB(INVALID_TID_MSG)
        return FAILURE;
    }
    switch (threads[tid]->state)
    {
        case BLOCKED:
            threads[tid]->state = READY;
            ready_queue.push (tid);
            break;
        case SLEEPING_AND_BLOCKED:
            threads[tid]->state = SLEEPING;
            break;
        default:
            break;
    }

    return change_signal_mask(SIG_UNBLOCK);
}

/**
 * @brief Blocks the RUNNING thread for num_quantums quantums.
 *
 * Immediately after the RUNNING thread transitions to the BLOCKED state a scheduling decision should be made.
 * After the sleeping time is over, the thread should go back to the end of the READY queue.
 * If the thread which was just RUNNING should also be added to the READY queue, or if multiple threads wake up
 * at the same time, the order in which they're added to the end of the READY queue doesn't matter.
 * The number of quantums refers to the number of times a new quantum starts, regardless of the reason. Specifically,
 * the quantum of the thread which has made the call to uthread_sleep isn’t counted.
 * It is considered an error if the main thread (tid == 0) calls this function.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_sleep(int num_quantums)
{
    change_signal_mask(SIG_BLOCK);
    if(current_thread == MAIN_THREAD)
    {
        ERROR_MSG_LIB(SLEEPING_MAIN_THREAD_ERROR);
        return FAILURE;
    }
    if(num_quantums < 0) // invalid input
    {
        ERROR_MSG_LIB(NEG_QUANTUMS_ERROR)
        return FAILURE;
    }
    if(num_quantums == 0) // move to ready state and schedule immediately
    {
        threads[current_thread]->state = READY;
        ready_queue.push(current_thread);
        return change_signal_mask(SIG_UNBLOCK);
    }

    sleep_threads[current_thread] = num_quantums;
    switch_thread(SLEEP);

    return change_signal_mask(SIG_UNBLOCK);
}

/**
 * @brief Returns the thread ID of the calling thread.
 *
 * @return The ID of the calling thread.
*/
int uthread_get_tid()
{
    return threads[current_thread]->tid;
}

/**
 * @brief Returns the total number of quantums since the library was initialized, including the current quantum.
 *
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number should be increased by 1.
 *
 * @return The total number of quantums.
*/
int uthread_get_total_quantums()
{
    return total_quantum_num;
}

/**
 * @brief Returns the number of quantums the thread with ID tid was in RUNNING state.
 *
 * On the first time a thread runs, the function should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state when this function is called, include
 * also the current quantum). If no thread with ID tid exists it is considered an error.
 *
 * @return On success, return the number of quantums of the thread with ID tid. On failure, return -1.
*/
int uthread_get_quantums(int tid)
{
    if(!valid_tid(tid))
    {
        ERROR_MSG_LIB(INVALID_TID_MSG);
        return FAILURE;
    }
    return threads[tid]->quantum_counter;
}





