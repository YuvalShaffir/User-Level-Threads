#include <queue>
#include <vector>
#include <sys/time.h>
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <setjmp.h>
#include <unistd.h>
#include <stdbool.h>
#include <map>
#include <memory>

#include "thread.h"
#include "halper.h"

#define FAILURE (-1)
#define SUCCESS 0
#ifndef OS_EX2_THREAD_MANAGER_H

#define OS_EX2_THREAD_MANAGER_H
//definitions

#define RESTART_TIMER_ERROR "timer restart failed"
#define SIGNAL_CHANGE_ERROR "signal mask change error"
enum action {
    RESUME,
    TERMINATE,
    BLOCK,
    SLEEP,
    PREEMPT
};

static struct itimerval timer;
static struct sigaction sa = {0};
static sigset_t set;


class thread_manager {
 public:
  static thread_manager* stat_manager;
  static thread_manager* start_and_get_manager(int quantum_usec, thread_manager* manager)
  {
    manager = new thread_manager(0);
    manager->set_quantum_usec (quantum_usec);
    sa.sa_handler = &main_switch_threads;
    manager->set_timer();
    stat_manager = manager;
    return stat_manager;
  }
  static void main_switch_threads(int a = PREEMPT)
  {
    stat_manager->switch_thread (a);
  }

public:

    typedef std::priority_queue<int, std::vector<int>, std::greater<int>> PR_queue;
    typedef std::queue<int> queue;
    typedef std::map<int, std::shared_ptr<thread>> map;
    typedef std::map<int, int> map_int;

    thread_manager(int quantum_usec) : quantum_usec(quantum_usec) {
        // Install switch_thread as the signal handler for SIGVTALRM.
//        sa.sa_handler = &thread_manager::switch_thread;
        if (sigaction(SIGVTALRM, &sa, NULL) < 0) {
            printf("sigaction error.");
            error_flag = true;
            return;
        }

        //empties the signals set
        if (sigemptyset(&sa.sa_mask) == FAILURE) {
            printf("sigemptyset error.");
            error_flag = true;
            return;
        }
        if (sigemptyset(&set) == FAILURE) {
            printf("sigemptyset error.");
            error_flag = true;
            return;
        }

        sigaddset(&set, SIGALRM); // adds the set to the signal set

        // Initialize the id queue.
        for (int i = 0; i < MAX_THREAD_NUM; ++i) {
            id_queue.push(i);
        }

//        set_timer();

    }

    ~thread_manager() {
        free_memory();
    }

    void free_memory() {
        threads.clear();

        for (int i = 0; i < ready_queue.size(); i++) {
            ready_queue.pop();
        }

        for (int i = 0; i < id_queue.size(); i++) {
            id_queue.pop();
        }
    }

    void create_thread(thread_entry_point entry_point) {
        int id = id_queue.top();
        id_queue.pop();
        std::shared_ptr<thread> t = std::make_shared<thread>(id, entry_point);
        threads[id] = t;
        ready_queue.push(id);
    }


    void delete_thread(int tid) {
        if (tid == this->current_thread)
        {
            threads.erase(tid); //calls the dtor of thread
            id_queue.push(tid);
            switch_thread (TERMINATE);
        }
        else
        {
            if (threads[tid]->get_state() == READY)
            {
                remove_from_q (tid);
            }
            if (threads[tid]->get_state () == SLEEPING || threads[tid]->get_state () == SLEEPING_AND_BLOCKED )
            {
                sleep_threads.erase (tid);
            }
            threads.erase(tid); //calls the dtor of thread
            id_queue.push(tid);
        }
    }


    int change_signal_mask(int how) {
        if (sigprocmask(how, &set, NULL) == FAILURE) {
            printf("sigprocmask error.");
            return FAILURE;
        }
        return SUCCESS;
    }


    void set_timer() {
        set_timer_helper(this->quantum_usec, &(thread_manager::main_switch_threads), &timer, &sa);
    }

    int restart_timer() {
        timer.it_value.tv_usec = this->quantum_usec;
        if (setitimer(ITIMER_VIRTUAL, &timer, NULL)) {
            printf("setitimer error.");
            return FAILURE;
        }
        return SUCCESS;
    }

    void set_quantum_usec(int quantum_usec)
    {
      this->quantum_usec = quantum_usec;
    }


    void switch_thread(int b= PREEMPT){

        if (change_signal_mask(SIG_BLOCK) == FAILURE) {
            ERROR_MSG(SIGNAL_CHANGE_ERROR);
        }
        int ret_val;
        action a = (action)b;
        switch (a) {
            case PREEMPT:
                ret_val = sigsetjmp(threads[current_thread]->get_data(), 1);
                if (ret_val == 0) {
                    set_running_to_state(READY);
                    set_next_ready_to_running();

                }
                break;

            case BLOCK:
                ret_val = sigsetjmp(threads[current_thread]->get_data(), 1);
                if (ret_val == 0) {
                    set_running_to_state(BLOCKED);
                    set_next_ready_to_running();
                }
                break;


            case SLEEP:
                ret_val = sigsetjmp(threads[current_thread]->get_data(), 1);
                if (ret_val == 0) {
                    set_running_to_state(SLEEPING);
                    set_next_ready_to_running();

                }
                break;

            case TERMINATE:
//                delete_thread(current_thread);
                set_next_ready_to_running();
                break;

            default:
                break;
        }

        this->total_quantum_num++;

        if (restart_timer() == FAILURE) {
            ERROR_MSG(RESTART_TIMER_ERROR)
        }

        update_sleep_quantum();

        if (change_signal_mask(SIG_UNBLOCK) == FAILURE) {
            ERROR_MSG(SIGNAL_CHANGE_ERROR);
        }

        siglongjmp(threads[this->current_thread]->get_data(), 1);
    }


    void move_to_sleep(int num_of_quantum)
    {
        sleep_threads[current_thread] = num_of_quantum + 1;
    }

    void update_sleep_quantum()
    {
        for (auto it = sleep_threads.begin(); it != sleep_threads.end(); it++)
        {
            it->second--;
            if (it->second == 0)
            {
                switch(threads[it->first]->get_state())
                {
                    case SLEEPING_AND_BLOCKED:
                        threads[it->first]->set_state(BLOCKED);
                        break;
                    case SLEEPING:
                        threads[it->first]->set_state(READY);
                        ready_queue.push(it->first);
                        break;
                    default:
                        break;
                }
                sleep_threads.erase(it);
            }
        }
    }

    void set_running_to_state(State s) {
        if (threads[current_thread]->get_state() == RUNNING) {
            threads[current_thread]->set_state(s);
            if (s == READY) {
                ready_queue.push(current_thread);
            }//push the current thread to the end of the ready queue
        }
    }

    void set_next_ready_to_running() {
        int id = ready_queue.front();
        std::shared_ptr<thread> t = threads[id];
        ready_queue.pop();
        t->set_state(RUNNING);

        //set the current thread to the next thread
        current_thread = id;
        t->raise_quantum_num();
    }


    bool get_error_flag() {
        return this->error_flag;
    }


    bool is_full() {
        return id_queue.empty();
    }


    int get_current_thread() {
        return this->current_thread;
    }

    bool valid_tid(int tid) {
        if (threads.find(tid) == threads.end()) {
            return false;
        }
        return true;
    }


    void block_thread(int tid)
    {
        if (tid == this->current_thread)
        {
            switch_thread (BLOCK);
        }
        else
        {
            if (threads[tid]->get_state() == SLEEPING)
            {
                threads[tid]->set_state(SLEEPING_AND_BLOCKED);
            }
            if (threads[tid]->get_state() == READY)
            {
                remove_from_q (tid);
                threads[tid]->set_state(BLOCKED);
            }
        }
    }


    int resume_thread(int tid)
    {
        switch (threads[tid]->get_state())
        {
            case BLOCKED:
                threads[tid]->set_state(READY);
                ready_queue.push (tid);
                break;
            case SLEEPING_AND_BLOCKED:
                threads[tid]->set_state(SLEEPING);
                break;
            default:
                break;
        }
        return SUCCESS;
    }


    void remove_from_q(int tid)
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


    int get_total_qua()
    {
        return total_quantum_num;
    }

    int get_thread_qua(int tid)
    {
        return threads[tid]->get_qua_counter();
    }


private:
    bool error_flag = false;
    int current_thread;
    PR_queue id_queue;
    queue ready_queue;
    int quantum_usec;
    int total_quantum_num = 0;
    map threads; //save all the threads with id's as keys
    map_int sleep_threads; //save all the sleeping threads with id's as keys and num of quantoms
};


#endif //OS_EX2_THREAD_MANAGER_H


// id queue: 4,5,6,7,8,9,10
//map: 0,1,2,3 tid > exits.size()