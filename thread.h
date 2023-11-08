#include "uthreads.h"
#include "halper.h"
//#include "thread_manager.h"
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <setjmp.h>
#include <stdbool.h>


#ifndef OS_EX2_THREAD_H
#define OS_EX2_THREAD_H

#define ERROR_MSG(txt) fprintf(stderr, "system error: %s\n", txt);

enum State {
    READY,
    RUNNING,
    BLOCKED,
    SLEEPING,
    SLEEPING_AND_BLOCKED
};


class thread {
public:
    thread(int tid, thread_entry_point entry_point)
            : tid(tid), entry_point(entry_point), state(READY), is_sleeping(false)
    {
        this->stack = new char[STACK_SIZE];
        if(this->stack == nullptr){
            ERROR_MSG("error allocating memory for the stack")
            return;
        }
        setup_thread();
    }



    ~thread()
    {
        printf("thread %d is deleted\n", this->tid);
        delete[] this->stack;
    }

    void set_state(State state)
    {
        this->state = state;
    }

    int get_tid()
    {
        return this->tid;
    }



    sigjmp_buf& get_data()
    {
        return this->data;
    }

    void raise_quantum_num()
    {
        this->quantum_counter++;
    }

    State get_state()
    {
        return this->state;
    }
    int get_qua_counter(){
        return this->quantum_counter;
    }

private:
    unsigned int tid;
    thread_entry_point entry_point;
    State state;
    bool is_sleeping;
    char *stack;
    int quantum_counter = 1;
    sigjmp_buf data; //holds the signal mask
    //todo: check how to save the data of the thread


    void setup_thread() {
//      thread t = thread(tid, stack, entry_point);

        address_t sp = (address_t) stack + STACK_SIZE - sizeof(address_t);
        address_t pc = (address_t) entry_point;
        sigsetjmp(data, 1);
        (data->__jmpbuf)[JB_SP] = translate_address(sp);
        (data->__jmpbuf)[JB_PC] = translate_address(pc);
//        sigemptyset(&threads[tid]->__saved_mask);
    }
};


#endif //OS_EX2_THREAD_H