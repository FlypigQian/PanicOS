/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include <lib/kernel/list.h>
#include <vm/frame.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "vm/frame.h"
#include "vm/swap.h"

#define MAX(a,b)  (((a)>(b))?(a):(b))

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
    {
      list_push_back (&sema->waiters, &thread_current ()->elem);
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
	if (!list_empty (&sema->waiters)) {
		/* Task 2. priority may change due to donation. */
		list_sort(&sema->waiters, list_less_thread_priority, NULL);
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
		                            struct thread, elem));
	}
	sema->value++;
	intr_set_level (old_level);

	/* Task 2. Task 3. if unblocked thread has higher priority, yield.
	 * cannot yield here, cause kernel panic during booting.
	 * Since lock-release and sema-up both may create threads with higher priority,
	 * yield in sema. */
	if (thread_start_flag)  {
		if (intr_context())
			intr_yield_on_return();
		else
			thread_yield();
	}
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) 
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) 
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) 
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  if (!thread_mlfqs) {
	  /* Task 2. */
	  if (!lock_try_acquire(lock)) {
		  // wait for lock, boost locker's priority, recursively.
		  thread_current()->lock_waiting = lock;
		  lock_priority_nested_donation(thread_current());

		  sema_down(&lock->semaphore);

		  thread_current()->lock_waiting = NULL;
		  lock->holder = thread_current ();
	  }
	  // manage to acquire lock. boost self's priority using locks' waiters.
	  list_push_back(&thread_current()->locks_acquired, &lock->elem);
	  struct thread *cur = thread_current();
	  int donate = lock_waiters_donation(lock);
	  cur->donate_priority = MAX (cur->donate_priority, donate);
	  cur->priority = MAX (cur->priority, cur->donate_priority);
  }
  else {
	  sema_down(&lock->semaphore);
	  lock->holder = thread_current ();
  }
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  if (!thread_mlfqs) {
	  /* Task 2*/
	  ASSERT(thread_current() == lock->holder);
	  struct thread *cur = thread_current();
	  list_remove(&lock->elem);
	  // recalculate this thread's priority
	  cur->priority = cur->base_priority;
	  cur->donate_priority = 0;

	  cur->donate_priority = MAX (cur->donate_priority, locks_list_donation(&cur->locks_acquired));
	  cur->priority = MAX (cur->base_priority, cur->donate_priority);
  }
  lock->holder = NULL;
  sema_up(&lock->semaphore);

}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}


/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

	/* Task 2. */
	waiter.wait_thread = thread_current();

  sema_init (&waiter.semaphore, 0);
  list_push_back (&cond->waiters, &waiter.elem);
  lock_release (lock);
  sema_down (&waiter.semaphore);

  lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters)) {
	  /* Task 2. priority may change due to donation. */
		list_sort(&cond->waiters, list_less_cond_waiter_priority, NULL);
	  sema_up (&list_entry (list_pop_front (&cond->waiters),
	                        struct semaphore_elem, elem)->semaphore);
  }
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}


/* Task 2. Priority scheduling.
 * Boost holder's priority with waiter's. Do it iteratively until holder is no longer
 * a waiter for other locks. */
void
lock_priority_nested_donation (struct thread *waiter) {
	struct thread *holder = waiter->lock_waiting->holder;
	while (1) {
		holder->donate_priority = MAX (holder->donate_priority, waiter->priority);
		holder->priority = MAX (holder->priority, holder->donate_priority);

		if (holder->lock_waiting == NULL) return;

		waiter = holder;
		holder = holder->lock_waiting->holder;
	}
}

/* Task 2. Priority scheduling.
 * Calculate maximum effective donation made by a lock's waiters.
 * Return minimum priority if no waiter present. */
int
lock_waiters_donation (struct lock *lock) {
	if (list_empty(&lock->semaphore.waiters))
		return PRI_MIN;
	list_sort(&lock->semaphore.waiters, list_less_thread_priority, NULL);
	return list_entry(list_front(&lock->semaphore.waiters), struct thread, elem)->priority;
}

/* Task 2. Priority scheduling.
 * Utility for getting donation from a list of locks. */
int
locks_list_donation (struct list *locks) {
	int tot_donate = PRI_MIN;
	if (!list_empty(locks))
		for (struct list_elem *e = list_begin(locks); e != list_end(locks); e = e->next) {
			struct lock *lock_acq = list_entry(e, struct lock, elem);
			tot_donate = MAX (tot_donate, lock_waiters_donation(lock_acq));
		}
	return tot_donate;
}

/* Task 2. condition variable waiter sort cmp */
bool list_less_cond_waiter_priority (const struct list_elem *a,
                                     const struct list_elem *b,
                                     void *aux) {
	return list_entry(a, struct semaphore_elem, elem)->wait_thread->priority >
				 list_entry(b, struct semaphore_elem, elem)->wait_thread->priority;
}