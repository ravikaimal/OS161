/*
 * Copyright (c) 2001, 2002, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Driver code for whale mating problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>
#include <current.h>
#include <wchan.h>

/*
 * 08 Feb 2012 : GWA : Driver code is in kern/synchprobs/driver.c. We will
 * replace that file. This file is yours to modify as you see fit.
 *
 * You should implement your solution to the whalemating problem below.
 */

// 13 Feb 2012 : GWA : Adding at the suggestion of Isaac Elbaz. These
// functions will allow you to do local initialization. They are called at
// the top of the corresponding driver code.
struct whale{
	volatile int maleCount ;
	volatile int femaleCount ;
	volatile int matchMakerCount ;

	struct cv *cv ;
	struct lock *malelock ;
	struct lock *femalelock ;
	struct lock *matchMakerlock ;
};

struct whale *whale ;

struct intersection{
	volatile int quad0WaitCount ;
	volatile int quad1WaitCount ;
	volatile int quad2WaitCount ;
	volatile int quad3WaitCount ;

	//struct cv *cv ;

	struct lock *lock_0;
	struct lock *lock_1;
	struct lock *lock_2;
	struct lock *lock_3;

	struct wchan *wchan0 ;
	struct wchan *wchan1 ;
	struct wchan *wchan2 ;
	struct wchan *wchan3 ;

	bool is0Locked ;
	bool is1Locked ;
	bool is2Locked ;
	bool is3Locked ;
};

struct intersection *intersection ;

void get_lock(unsigned long);
void give_lock(unsigned long);
int getIntersectionWaitCount(unsigned long);
void incrementIntersectionWaitCount(unsigned long);
void decrementIntersectionWaitCount(unsigned long);
struct lock* getIntersectionLock(unsigned long);
bool getLockStatus(unsigned long );
void setLockStatus(unsigned long ,bool );
void sleep_wchan(unsigned long ) ;
void wake_wchan(unsigned long ) ;
void get_locks(unsigned long , unsigned long ,unsigned long ) ;
void give_locks(unsigned long , unsigned long ,unsigned long ) ;

void whalemating_init() {
	whale = kmalloc(sizeof(struct whale));
	whale->maleCount = 0 ;
	whale->femaleCount = 0 ;
	whale->matchMakerCount = 0 ;
	whale->cv = cv_create("whalecv") ;
	whale->malelock = lock_create("malelock") ;
	whale->femalelock = lock_create("femalelock") ;
	whale->matchMakerlock = lock_create("matchMakerlock") ;
	return;
}

// 20 Feb 2012 : GWA : Adding at the suggestion of Nikhil Londhe. We don't
// care if your problems leak memory, but if you do, use this to clean up.

void whalemating_cleanup() {
	whale->maleCount = 0 ;
	whale->femaleCount = 0 ;
	whale->matchMakerCount = 0 ;
	cv_destroy(whale->cv) ;
	lock_destroy(whale->malelock) ;
	lock_destroy(whale->femalelock) ;
	lock_destroy(whale->matchMakerlock) ;
	kfree(whale) ;
	return;
}

void
male(void *p, unsigned long which)
{
	struct semaphore * whalematingMenuSemaphore = (struct semaphore *)p;
	(void)which;

	male_start();
	lock_acquire(whale->malelock) ;
	whale->maleCount++ ;

	while(whale->femaleCount == 0 || whale->matchMakerCount == 0  ){
		cv_wait(whale->cv,whale->malelock) ;
	}
	cv_broadcast(whale->cv,whale->malelock) ;
	male_end();
	whale->maleCount-- ;
	lock_release(whale->malelock) ;

	// 08 Feb 2012 : GWA : Please do not change this code. This is so that your
	// whalemating driver can return to the menu cleanly.
	V(whalematingMenuSemaphore);
	return;
}

void
female(void *p, unsigned long which)
{
	struct semaphore * whalematingMenuSemaphore = (struct semaphore *)p;
	(void)which;

	female_start();
	lock_acquire(whale->femalelock) ;
	whale->femaleCount++ ;

	while(whale->maleCount == 0 || whale->matchMakerCount == 0){

		cv_wait(whale->cv,whale->femalelock) ;

	}
	cv_broadcast(whale->cv,whale->femalelock) ;
	female_end();
	whale->femaleCount-- ;
	lock_release(whale->femalelock) ;

	// 08 Feb 2012 : GWA : Please do not change this code. This is so that your
	// whalemating driver can return to the menu cleanly.
	V(whalematingMenuSemaphore);
	return;
}

void
matchmaker(void *p, unsigned long which)
{
	struct semaphore * whalematingMenuSemaphore = (struct semaphore *)p;
	(void)which;

	matchmaker_start();
	lock_acquire(whale->matchMakerlock) ;
	whale->matchMakerCount++ ;
	while(whale->maleCount == 0 || whale->femaleCount == 0){
		cv_wait(whale->cv,whale->matchMakerlock) ;
	}
	cv_broadcast(whale->cv,whale->matchMakerlock) ;
	matchmaker_end();
	whale->matchMakerCount-- ;
	lock_release(whale->matchMakerlock) ;

	// 08 Feb 2012 : GWA : Please do not change this code. This is so that your
	// whalemating driver can return to the menu cleanly.
	V(whalematingMenuSemaphore);
	return;
}

/*
 * You should implement your solution to the stoplight problem below. The
 * quadrant and direction mappings for reference: (although the problem is,
 * of course, stable under rotation)
 *
 *   | 0 |
 * --     --
 *    0 1
 * 3       1
 *    3 2
 * --     --
 *   | 2 | 
 *
 * As way to think about it, assuming cars drive on the right: a car entering
 * the intersection from direction X will enter intersection quadrant X
 * first.
 *
 * You will probably want to write some helper functions to assist
 * with the mappings. Modular arithmetic can help, e.g. a car passing
 * straight through the intersection entering from direction X will leave to
 * direction (X + 2) % 4 and pass through quadrants X and (X + 3) % 4.
 * Boo-yah.
 *
 * Your solutions below should call the inQuadrant() and leaveIntersection()
 * functions in drivers.c.
 */

// 13 Feb 2012 : GWA : Adding at the suggestion of Isaac Elbaz. These
// functions will allow you to do local initialization. They are called at
// the top of the corresponding driver code.

void stoplight_init() {

	intersection = kmalloc(sizeof(struct intersection));

	intersection->lock_0=lock_create("Quadrant 0 Lock");
	intersection->lock_1=lock_create("Quadrant 1 Lock");
	intersection->lock_2=lock_create("Quadrant 2 Lock");
	intersection->lock_3=lock_create("Quadrant 3 Lock");

	intersection->quad0WaitCount = 0 ;
	intersection->quad1WaitCount = 0 ;
	intersection->quad2WaitCount = 0 ;
	intersection->quad3WaitCount = 0 ;

	intersection->is0Locked  = false;
	intersection->is1Locked = false;
	intersection->is2Locked = false;
	intersection->is3Locked = false;

	intersection->wchan0 = wchan_create("waitchannel 0") ;
	intersection->wchan1 = wchan_create("waitchannel 1") ;
	intersection->wchan2 = wchan_create("waitchannel 2") ;
	intersection->wchan3 = wchan_create("waitchannel 3") ;


	//intersection->cv = cv_create("intersection cv") ;
	return;
}

// 20 Feb 2012 : GWA : Adding at the suggestion of Nikhil Londhe. We don't
// care if your problems leak memory, but if you do, use this to clean up.

void stoplight_cleanup() {
	intersection->quad0WaitCount = 0 ;
	intersection->quad1WaitCount = 0 ;
	intersection->quad2WaitCount = 0 ;
	intersection->quad3WaitCount = 0 ;
	intersection->is0Locked  = false;
	intersection->is1Locked = false;
	intersection->is2Locked = false;
	intersection->is3Locked = false;

//	cv_destroy(intersection->cv) ;
	wchan_destroy(intersection->wchan0) ;
	wchan_destroy(intersection->wchan1) ;
	wchan_destroy(intersection->wchan2) ;
	wchan_destroy(intersection->wchan3) ;

	lock_destroy(intersection->lock_0);
	lock_destroy(intersection->lock_1);
	lock_destroy(intersection->lock_2);
	lock_destroy(intersection->lock_3);

	kfree(intersection) ;

	return;
}

void
gostraight(void *p, unsigned long direction)
{
	//kprintf("gostraight : entry\n") ;
	struct semaphore * stoplightMenuSemaphore = (struct semaphore *)p;
	(void)direction;
	unsigned long nextdirection = (direction+3)%4 ;

	get_locks(direction,nextdirection,5) ;

	inQuadrant(direction);
	inQuadrant(nextdirection);

	leaveIntersection();

	give_locks(direction,nextdirection,5) ;

	V(stoplightMenuSemaphore);
	return;
}

void sleep_wchan(unsigned long direction)
{
	switch(direction)
	{
	case 0 : wchan_lock(intersection->wchan0) ; wchan_sleep(intersection->wchan0) ;  break;
	case 1 : wchan_lock(intersection->wchan1) ; wchan_sleep(intersection->wchan1) ;  break;
	case 2 : wchan_lock(intersection->wchan2) ; wchan_sleep(intersection->wchan2) ;  break;
	case 3 : wchan_lock(intersection->wchan3) ; wchan_sleep(intersection->wchan3) ;  break;
	}
}

void wake_wchan(unsigned long direction)
{
	switch(direction)
	{
	case 0 : wchan_wakeone(intersection->wchan0) ;  break;
	case 1 : wchan_wakeone(intersection->wchan1) ;  break;
	case 2 : wchan_wakeone(intersection->wchan2) ;  break;
	case 3 : wchan_wakeone(intersection->wchan3) ;  break;
	}
}

void
turnleft(void *p, unsigned long direction)
{
	//kprintf("turnleft : entry\n") ;
	struct semaphore * stoplightMenuSemaphore = (struct semaphore *)p;
	(void)direction;
	unsigned long nextdirection = (direction+3)%4 ;
	unsigned long nexttonextdirection = (direction+2)%4 ;

	get_locks(direction,nextdirection,nexttonextdirection) ;

	inQuadrant(direction);
	inQuadrant(nextdirection);
	inQuadrant(nexttonextdirection);

	leaveIntersection();

	give_locks(direction,nextdirection,nexttonextdirection) ;

	//kprintf("turnleft : exit\n") ;

	// 08 Feb 2012 : GWA : Please do not change this code. This is so that your
	// stoplight driver can return to the menu cleanly.
	V(stoplightMenuSemaphore);
	return;
}

void
turnright(void *p, unsigned long direction)
{
	//kprintf("turnright : entry\n") ;
	struct semaphore * stoplightMenuSemaphore = (struct semaphore *)p;
	(void)direction;

	get_locks(direction,5,5) ;

	inQuadrant(direction);

	leaveIntersection();

	give_locks(direction,5,5) ;



	// 08 Feb 2012 : GWA : Please do not change this code. This is so that your
	// stoplight driver can return to the menu cleanly.
	V(stoplightMenuSemaphore);
	return;
}

void get_locks(unsigned long direction1, unsigned long direction2,unsigned long direction3)
{
	unsigned long order1 ;
	unsigned long order2 ;
	unsigned long order3 ;
	//kprintf("US %lu,%lu,%lu\n",direction1,direction2,direction3) ;
	if (direction1 < direction2)
	{
		if (direction1 < direction3)
		{
			order1 = direction1 ;
			if (direction2 < direction3)
			{
				order2 = direction2 ;
				order3 = direction3 ;

			}
			else
			{
				order2 = direction3 ;
				order3 = direction2 ;
			}
		}
		else
		{
			order1 = direction3 ;
			order2 = direction1 ;
			order3 = direction2 ;
		}
	}
	else if (direction1 < direction3)
	{
		if (direction2 < direction3)
		{
			order1 = direction2 ;
			order2 = direction1 ;
			order3 = direction3 ;

		}
		else
		{
			order1 = direction2 ;
			order2 = direction3 ;
			order3 = direction1 ;
		}
	}
	else
	{
		order1 = direction3 ;
		order2 = direction2 ;
		order3 = direction1 ;
	}

	//kprintf("SO %lu,%lu,%lu\n",order1,order2,order3) ;
	get_lock(order1) ;
	if (order2 != 5)
	{
		get_lock(order2) ;
	}

	if (order3 != 5)
	{
		get_lock(order3) ;
	}

}

void give_locks(unsigned long direction1, unsigned long direction2,unsigned long direction3)
{
	unsigned long order1 ;
	unsigned long order2 ;
	unsigned long order3 ;
	if (direction1 < direction2)
	{
		if (direction1 < direction3)
		{
			order1 = direction1 ;
			if (direction2 < direction3)
			{
				order2 = direction2 ;
				order3 = direction3 ;

			}
			else
			{
				order2 = direction3 ;
				order3 = direction2 ;
			}
		}
		else
		{
			order1 = direction3 ;
			order2 = direction1 ;
			order3 = direction2 ;
		}
	}
	else if (direction1 < direction3)
	{
		if (direction2 < direction3)
		{
			order1 = direction2 ;
			order2 = direction1 ;
			order3 = direction3 ;

		}
		else
		{
			order1 = direction2 ;
			order2 = direction3 ;
			order3 = direction1 ;
		}
	}
	else
	{
		order1 = direction3 ;
		order2 = direction2 ;
		order3 = direction1 ;
	}


	give_lock(order1) ;
	if (order2 != 5)
	{
		give_lock(order2) ;
	}

	if (order3 != 5)
	{
		give_lock(order3) ;
	}

}


void get_lock(unsigned long direction)
{
	switch(direction)
	{
	case 0 : lock_acquire(intersection->lock_0); break;
	case 1 : lock_acquire(intersection->lock_1); break;
	case 2 : lock_acquire(intersection->lock_2); break;
	case 3 : lock_acquire(intersection->lock_3); break;
	}

}

void give_lock(unsigned long direction)
{
	switch(direction)
	{
	case 0 : lock_release(intersection->lock_0); break;
	case 1 : lock_release(intersection->lock_1); break;
	case 2 : lock_release(intersection->lock_2); break;
	case 3 : lock_release(intersection->lock_3); break;
	}

}

int getIntersectionWaitCount(unsigned long direction)
{
	switch(direction)
	{
	case 0:return intersection->quad0WaitCount ;
	case 1:return intersection->quad1WaitCount ;
	case 2:return intersection->quad2WaitCount ;
	case 3:return intersection->quad3WaitCount ;
	}

	return 0 ;
}

bool getLockStatus(unsigned long direction)
{
	switch(direction)
	{
	case 0:return intersection->is0Locked ;
	case 1:return intersection->is1Locked ;
	case 2:return intersection->is2Locked ;
	case 3:return intersection->is3Locked ;
	}

	return false ;
}

void setLockStatus(unsigned long direction,bool value)
{
	switch(direction)
	{
	case 0:intersection->is0Locked = value  ; break ;
	case 1:intersection->is1Locked = value  ; break ;
	case 2:intersection->is2Locked = value  ; break ;
	case 3:intersection->is3Locked = value  ; break ;
	}
}

void incrementIntersectionWaitCount(unsigned long direction)
{
	switch(direction)
	{
	case 0:intersection->quad0WaitCount++ ; break ;
	case 1:intersection->quad1WaitCount++ ; break ;
	case 2:intersection->quad2WaitCount++ ; break ;
	case 3:intersection->quad3WaitCount++ ; break ;
	}
}

void decrementIntersectionWaitCount(unsigned long direction)
{
	switch(direction)
	{
	case 0:intersection->quad0WaitCount-- ; break ;
	case 1:intersection->quad1WaitCount-- ; break ;
	case 2:intersection->quad2WaitCount-- ; break ;
	case 3:intersection->quad3WaitCount-- ; break ;
	}
}

struct lock* getIntersectionLock(unsigned long direction)
{
	switch(direction)
	{
	case 0:return intersection->lock_0 ;
	case 1:return intersection->lock_1 ;
	case 2:return intersection->lock_2 ;
	case 3:return intersection->lock_3 ;
	}

	return NULL;
}
