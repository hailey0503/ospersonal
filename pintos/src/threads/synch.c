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
#include "threads/interrupt.h"
#include "threads/thread.h"

bool priority_comparator(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
bool priority_cond_comparator(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
bool priority_donor_comparator(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);

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
  if (!list_empty (&sema->waiters))
    thread_unblock (list_entry (list_return_remove (list_max (&sema->waiters, priority_comparator, NULL)), struct thread, elem));
  sema->value++;
  intr_set_level (old_level);
  thread_yield();
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
  enum intr_level old_level;
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));
  old_level = intr_disable ();
  // check if lock is already acquired
  if (lock->holder != NULL) {
    //Push the new donor onto the lock holder's list of donors, update
    //the priority of the lock holder, assign the lock holder's donor variable to its new donor,
    //and assign the current thread's blocking lock variable to the lock it's
    //trying to acquire, since it is trying to acquire an acquired lock.
    list_push_back(&lock->holder->donors, &thread_current()->donor_elem);
    struct list_elem *max_elem = list_max(&lock->holder->donors, priority_donor_comparator, NULL);
    struct thread *max_thread = list_entry(max_elem, struct thread, donor_elem);
    lock->holder->priority = max_thread->priority;
    lock->holder->donor = max_thread;

    // This part is to check if there is a chain. For example There are threads 0-5 (with priority = thread number)
    // and locks 0-4 thread[i] will acquire lock[i] (except thread 5), and then thread[i] will try to acquire lock[i-1]
    // (except thread 0). Then, thread 5 will donate to thread 4, which will cause a chain reaction
    // such that thread 0 will have priority 5. If that doesn't make sense try to draw out the chain on paper.
    struct lock *head_lock = lock->holder->blocking_lock;
    struct lock *tail_lock = lock;
    while (head_lock != NULL && head_lock->holder->donor == tail_lock->holder) {
      head_lock->holder->priority = tail_lock->holder->priority;
      head_lock = head_lock->holder->blocking_lock;
      tail_lock = tail_lock->holder->blocking_lock;
    }
    thread_current()->blocking_lock = lock;
  }
  intr_set_level (old_level);
  sema_down (&lock->semaphore);
  old_level = intr_disable ();
  // Since the lock has been acquired by the current thread, we change the
  //current thread's blocking_lock variable to NULL
  thread_current()->blocking_lock = NULL;
  lock->holder = thread_current ();
  // New holder of the lock, so we must populate it's donor's list with all of its contenders
  for (struct list_elem *e = list_begin(&(&lock->semaphore)->waiters); e != list_end(&(&lock->semaphore)->waiters); e = list_next(e)) {
    struct thread *t = list_entry(e, struct thread, elem);
    list_push_back(&thread_current()->donors, &t->donor_elem);
  }
  intr_set_level (old_level);
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

bool priority_donor_comparator(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
  struct thread *t1 = list_entry(a, struct thread, donor_elem);
  struct thread *t2 = list_entry(b, struct thread, donor_elem);
  return t1->priority < t2->priority;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */

void
lock_release (struct lock *lock)
{
  enum intr_level old_level;
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));
  old_level = intr_disable ();

  // We have to remove every waiter for the lock that is getting released from the donor's list.
  struct list_elem *e;
  for (e = list_begin(&(&lock->semaphore)->waiters); e != list_end(&(&lock->semaphore)->waiters); e = list_next(e)) {
    struct thread *t = list_entry(e, struct thread, elem);
    list_remove(&t->donor_elem);
  }

  //Check if there is an existing donor for this lock.
  if (thread_current()->donor != NULL) {
    //Check if the list of donors is empty. If its not empty, then we set the current
    //thread's priority to the highest priority amongst it's donors and reasign the lock's donor
    //variable to the new donor. If the donor's list is empty, we just set the current thread's
    //priority to its original priority.
    if (!list_empty(&thread_current()->donors)) {
      struct list_elem *max_elem = list_max(&thread_current()->donors, priority_donor_comparator, NULL);
      struct thread *new_donor = list_entry(max_elem, struct thread, donor_elem);
      thread_current()->priority = new_donor->priority;
      thread_current()->donor = new_donor;
    } else {
      thread_current()->priority = thread_current()->original_priority;
      thread_current()->donor = NULL;
    }
  }

  lock->holder = NULL;
  intr_set_level (old_level);
  sema_up (&lock->semaphore);
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

/* One semaphore in a list. */
struct semaphore_elem
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
  };

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

  sema_init (&waiter.semaphore, 0);
  list_push_back (&cond->waiters, &waiter.elem);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

bool priority_cond_comparator(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
  struct semaphore_elem *se1 = list_entry(a, struct semaphore_elem, elem);
  struct semaphore_elem *se2 = list_entry(b, struct semaphore_elem, elem);
  struct semaphore *s1 = &se1->semaphore;
  struct semaphore *s2 = &se2->semaphore;
  struct thread *t1 = list_entry(list_begin(&s1->waiters), struct thread, elem);
  struct thread *t2 = list_entry(list_begin(&s2->waiters), struct thread, elem);
  if (t1->priority < t2->priority) {
    return true;
  } else {
    return false;
  }


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
    struct list_elem *max = list_max (&cond->waiters, priority_cond_comparator, NULL);
    list_remove(max);
    struct semaphore_elem *s = list_entry (max, struct semaphore_elem, elem);
    sema_up(&s->semaphore);

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
