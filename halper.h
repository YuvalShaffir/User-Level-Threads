#pragma once
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <setjmp.h>
#include <unistd.h>
#include <stdbool.h>
#include "uthreads.h"


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

typedef void (*thread_entry_point)(void);
typedef void(*handler)(int);

int set_timer_helper(int quantum_usecs, handler timer_handler, struct itimerval* timer,struct sigaction* sa)
{
//  struct sigaction sa = {0};
//  struct itimerval timer;

  // Install timer_handler as the signal handler for SIGVTALRM.
  sa->sa_handler = timer_handler;
  if (sigaction(SIGVTALRM, sa, NULL) < 0)
    {
      printf("sigaction error.");
      return -1;
    }

  // Configure the timer to expire after expires_after sec... */
  (*timer).it_value.tv_sec = 0;        // first time interval, seconds part
  (*timer).it_value.tv_usec = quantum_usecs;        // first time interval, microseconds part

  // configure the timer to expire every 3 sec after that.
  (*timer).it_interval.tv_sec = 0;    // following time intervals, seconds part
  (*timer).it_interval.tv_usec = 2 * quantum_usecs;    // following time intervals, microseconds part

  // Start a virtual timer. It counts down whenever this process is executing.
  if (setitimer(ITIMER_VIRTUAL, timer, NULL))
    {
      printf("setitimer error.");
      return -1;
    }

  return 0;
}




//char stack0[STACK_SIZE];
//char stack1[STACK_SIZE];
////sigjmp_buf env[2];
//int current_thread = -1;
//
//
//void jump_to_thread(int tid)
//{
//  current_thread = tid;
//  siglongjmp(env[tid], 1);
//}
//
///**
// * @brief Saves the current thread state, and jumps to the other thread.
// */
//void yield(void)
//{
//  int ret_val = sigsetjmp(env[current_thread],1);
//  printf("yield: ret_val=%d\n", ret_val);
//  bool did_just_save_bookmark = ret_val == 0;
////    bool did_jump_from_another_thread = ret_val != 0;
//}
//
//
//void setup_thread(int tid, char *stack, thread_entry_point entry_point)
//{
//  // initializes env[tid] to use the right stack, and to run from the function 'entry_point', when we'll use
//  // siglongjmp to jump into the thread.
//  address_t sp = (address_t) stack + STACK_SIZE - sizeof(address_t);
//  address_t pc = (address_t) entry_point;
//  sigsetjmp(env[tid], 1);
//  (env[tid]->__jmpbuf)[JB_SP] = translate_address(sp);
//  (env[tid]->__jmpbuf)[JB_PC] = translate_address(pc);
//  sigemptyset(&env[tid]->__saved_mask);
//}