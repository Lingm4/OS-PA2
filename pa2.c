/**********************************************************************
 * Copyright (c) 2019-2023
 *  Sang-Hoon Kim <sanghoonkim@ajou.ac.kr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTIABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 **********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "types.h"
#include "list_head.h"

/**
 * The process which is currently running
 */
#include "process.h"
extern struct process *current;

/**
 * List head to hold the processes ready to run
 */
extern struct list_head readyqueue;

/**
 * Resources in the system.
 */
#include "resource.h"
extern struct resource resources[NR_RESOURCES];

/**
 * Monotonically increasing ticks. Do not modify it
 */
extern unsigned int ticks;

/**
 * Quiet mode. True if the program was started with -q option
 */
extern bool quiet;

/***********************************************************************
 * Default FCFS resource acquision function
 *
 * DESCRIPTION
 *   This is the default resource acquision function which is called back
 *   whenever the current process is to acquire resource @resource_id.
 *   The current implementation serves the resource in the requesting order
 *   without considering the priority. See the comments in sched.h
 ***********************************************************************/
static bool fcfs_acquire(int resource_id)
{
	struct resource *r = resources + resource_id;

	if (!r->owner) {
		/* This resource is not owned by any one. Take it! */
		r->owner = current;
		return true;
	}

	/* OK, this resource is taken by @r->owner. */

	/* Update the current process state */
	current->status = PROCESS_BLOCKED;

	/* And append current to waitqueue */
	list_add_tail(&current->list, &r->waitqueue);

	/**
	 * And return false to indicate the resource is not available.
	 * The scheduler framework will soon call schedule() function to
	 * schedule out current and to pick the next process to run.
	 */
	return false;
}

/***********************************************************************
 * Default FCFS resource release function
 *
 * DESCRIPTION
 *   This is the default resource release function which is called back
 *   whenever the current process is to release resource @resource_id.
 *   The current implementation serves the resource in the requesting order
 *   without considering the priority. See the comments in sched.h
 ***********************************************************************/
static void fcfs_release(int resource_id)
{
	struct resource *r = resources + resource_id;

	/* Ensure that the owner process is releasing the resource */
	assert(r->owner == current);

	/* Un-own this resource */
	r->owner = NULL;

	/* Let's wake up ONE waiter (if exists) that came first */
	if (!list_empty(&r->waitqueue)) {
		struct process *waiter = list_first_entry(&r->waitqueue, struct process, list);

		/**
		 * Ensure the waiter is in the wait status
		 */
		assert(waiter->status == PROCESS_BLOCKED);

		/**
		 * Take out the waiter from the waiting queue. Note we use
		 * list_del_init() over list_del() to maintain the list head tidy
		 * (otherwise, the framework will complain on the list head
		 * when the process exits).
		 */
		list_del_init(&waiter->list);

		/* Update the process status */
		waiter->status = PROCESS_READY;

		/**
		 * Put the waiter process into ready queue. The framework will
		 * do the rest.
		 */
		list_add_tail(&waiter->list, &readyqueue);
	}
}

#include "sched.h"

/***********************************************************************
 * FIFO scheduler
 ***********************************************************************/
static int fifo_initialize(void)
{
	return 0;
}

static void fifo_finalize(void)
{
}

static struct process *fifo_schedule(void)
{
	struct process *next = NULL;

	/* You may inspect the situation by calling dump_status() at any time */
	// dump_status();

	/**
	 * When there was no process to run in the previous tick (so does
	 * in the very beginning of the simulation), there will be
	 * no @current process. In this case, pick the next without examining
	 * the current process. Also, the current process can be blocked
	 * while acquiring a resource. In this case just pick the next as well.
	 */
	if (!current || current->status == PROCESS_BLOCKED) {
		goto pick_next;
	}

	/* The current process has remaining lifetime. Schedule it again */
	if (current->age < current->lifespan) {
		return current;
	}

pick_next:
	/* Let's pick a new process to run next */

	if (!list_empty(&readyqueue)) {
		/**
		 * If the ready queue is not empty, pick the first process
		 * in the ready queue
		 */
		next = list_first_entry(&readyqueue, struct process, list);

		/**
		 * Detach the process from the ready queue. Note that we use 
		 * list_del_init() over list_del() to maintain the list head tidy.
		 * Otherwise, the framework will complain (assert) on process exit.
		 */
		list_del_init(&next->list);
	}

	/* Return the process to run next */
	return next;
}

struct scheduler fifo_scheduler = {
	.name = "FIFO",
	.acquire = fcfs_acquire,
	.release = fcfs_release,
	.initialize = fifo_initialize,
	.finalize = fifo_finalize,
	.schedule = fifo_schedule,
};

/***********************************************************************
 * SJF scheduler
 ***********************************************************************/
static struct process *sjf_schedule(void)
{
	struct process *next = NULL;

	if (!current || current->status == PROCESS_BLOCKED) {
		goto pick_next;
	}

	/* The current process has remaining lifetime. Schedule it again */
	if (current->age < current->lifespan) {
		return current;
	}

pick_next:
	/* Let's pick a new process to run next */

	if (!list_empty(&readyqueue)) {
		struct process *shortest_job = list_first_entry(&readyqueue, struct process, list);
		struct list_head *pos = NULL;
		list_for_each(pos, &readyqueue){
			if(list_entry(pos, struct process, list)->lifespan < shortest_job->lifespan) shortest_job = list_entry(pos, struct process, list);
		}
		next = shortest_job;
		list_del_init(&next->list);
	}

	return next;
}

struct scheduler sjf_scheduler = {
	.name = "Shortest-Job First",
	.acquire = fcfs_acquire,	/* Use the default FCFS acquire() */
	.release = fcfs_release,	/* Use the default FCFS release() */
	.schedule = sjf_schedule,	/* TODO: Assign your schedule function  
								   to this function pointer to activate
								   SJF in the simulation system */
};

/***********************************************************************
 * STCF scheduler
 ***********************************************************************/
static struct process *stcf_schedule(void)
{
	struct process *next = NULL;

	/* Let's pick a new process to run next */
	if (!(!current || current->status == PROCESS_BLOCKED) && current->age < current->lifespan) list_add(&(current->list), &readyqueue);
	
	if (!list_empty(&readyqueue)) {
		struct process *shortest_job = list_first_entry(&readyqueue, struct process, list);
		struct list_head *pos = NULL;
		list_for_each(pos, &readyqueue){
			if(list_entry(pos, struct process, list)->lifespan -  list_entry(pos, struct process, list)->age \
				< shortest_job->lifespan - shortest_job->age) \
					shortest_job = list_entry(pos, struct process, list);
		}
		next = shortest_job;
		list_del_init(&next->list);
	}
	
	return next;
}

struct scheduler stcf_scheduler = {
	.name = "Shortest Time-to-Complete First",
	.acquire = fcfs_acquire, /* Use the default FCFS acquire() */
	.release = fcfs_release, /* Use the default FCFS release() */
	.schedule = stcf_schedule,
	/* You need to check the newly created processes to implement STCF.
	 * Have a look at @forked() callback.
	 */

	/* Obviously, you should implement stcf_schedule() and attach it here */
};

/***********************************************************************
 * Round-robin scheduler
 ***********************************************************************/
static struct process *rr_schedule(void)
{
	struct process *next = NULL;

	/* Let's pick a new process to run next */
	if (!(!current || current->status == PROCESS_BLOCKED) && current->age < current->lifespan) list_add_tail(&(current->list), &readyqueue);
	
	if (!list_empty(&readyqueue)) {
		next = list_first_entry(&readyqueue, struct process, list);
		list_del_init(&next->list);
	}
	
	return next;
}
struct scheduler rr_scheduler = {
	.name = "Round-Robin",
	.acquire = fcfs_acquire, /* Use the default FCFS acquire() */
	.release = fcfs_release, /* Use the default FCFS release() */
	.schedule = rr_schedule,
	/* Obviously, ... */
};

/***********************************************************************
 * Priority scheduler
 ***********************************************************************/
static struct process *prio_schedule(void)
{
	struct process *next = NULL;

	/* Let's pick a new process to run next */
	if (!(!current || current->status == PROCESS_BLOCKED) && current->age < current->lifespan) list_add_tail(&(current->list), &readyqueue);
	
	if (!list_empty(&readyqueue)) {
		struct process *highest_priority_job = list_first_entry(&readyqueue, struct process, list);
		struct list_head *pos = NULL;
		list_for_each(pos, &readyqueue){
			if(list_entry(pos, struct process, list)->prio > highest_priority_job->prio) highest_priority_job = list_entry(pos, struct process, list);
		}
		next = highest_priority_job;
		list_del_init(&next->list);
	}
	
	return next;
}

static void prio_release(int resource_id)
{
	struct resource *r = resources + resource_id;

	assert(r->owner == current);

	r->owner = NULL;

	if (!list_empty(&r->waitqueue)) {
		struct process *highest_priority_waiter = list_first_entry(&r->waitqueue, struct process, list);
		struct list_head *pos = NULL;
		list_for_each(pos, &r->waitqueue){
			if(list_entry(pos, struct process, list)->prio > highest_priority_waiter->prio) highest_priority_waiter = list_entry(pos, struct process, list);
		}

		assert(highest_priority_waiter->status == PROCESS_BLOCKED);

		list_del_init(&highest_priority_waiter->list);
		highest_priority_waiter->status = PROCESS_READY;
		list_add_tail(&highest_priority_waiter->list, &readyqueue);
	}
}
struct scheduler prio_scheduler = {
	.name = "Priority",
	.acquire = fcfs_acquire,
	.release = prio_release,
	.schedule = prio_schedule,
	/**
	 * Implement your own acqure/release function to make the priority
	 * scheduler correct.
	 */
	/* Implement your own prio_schedule() and attach it here */
};

/***********************************************************************
 * Priority scheduler with aging
 ***********************************************************************/
static struct process *pa_schedule(void)
{
	struct process *next = NULL;

	/* Let's pick a new process to run next */
	if (!(!current || current->status == PROCESS_BLOCKED) && current->age < current->lifespan){
		list_add_tail(&(current->list), &readyqueue);
		current->prio = current->prio_orig;
	}
	if (!list_empty(&readyqueue)) {
		struct process *highest_priority_job = list_first_entry(&readyqueue, struct process, list);
		struct list_head *pos = NULL;
		list_for_each(pos, &readyqueue){
			if((list_entry(pos, struct process, list)->prio) < MAX_PRIO) (list_entry(pos, struct process, list)->prio)++;
			if(list_entry(pos, struct process, list)->prio > highest_priority_job->prio) highest_priority_job = list_entry(pos, struct process, list);
		}
		next = highest_priority_job;
		list_del_init(&next->list);
	}
	
	return next;
}

struct scheduler pa_scheduler = {
	.name = "Priority + aging",
	.acquire = fcfs_acquire,
	.release = prio_release,
	.schedule = pa_schedule,
};

/***********************************************************************
 * Priority scheduler with priority ceiling protocol
 ***********************************************************************/

static bool pcp_acquire(int resource_id)
{
	struct resource *r = resources + resource_id;

	if (!r->owner) {
		/* This resource is not owned by any one. Take it! */
		r->owner = current;
		current->prio = MAX_PRIO;
		return true;
	}

	/* OK, this resource is taken by @r->owner. */

	/* Update the current process state */
	current->status = PROCESS_BLOCKED;

	/* And append current to waitqueue */
	list_add_tail(&current->list, &r->waitqueue);
	
	/**
	 * And return false to indicate the resource is not available.
	 * The scheduler framework will soon call schedule() function to
	 * schedule out current and to pick the next process to run.
	 */
	return false;
}

static void pcp_release(int resource_id)
{
	struct resource *r = resources + resource_id;

	assert(r->owner == current);

	r->owner->prio = r->owner->prio_orig;
	r->owner = NULL;

	if (!list_empty(&r->waitqueue)) {
		struct process *highest_priority_waiter = list_first_entry(&r->waitqueue, struct process, list);
		struct list_head *pos = NULL;
		list_for_each(pos, &r->waitqueue){
			if(list_entry(pos, struct process, list)->prio > highest_priority_waiter->prio) highest_priority_waiter = list_entry(pos, struct process, list);
		}

		assert(highest_priority_waiter->status == PROCESS_BLOCKED);

		list_del_init(&highest_priority_waiter->list);
		highest_priority_waiter->status = PROCESS_READY;
		list_add_tail(&highest_priority_waiter->list, &readyqueue);
	}
}

struct scheduler pcp_scheduler = {
	.name = "Priority + PCP Protocol",
	.acquire = pcp_acquire,
	.release = pcp_release,
	.schedule = prio_schedule,
};

/***********************************************************************
 * Priority scheduler with priority inheritance protocol
 ***********************************************************************/

static bool pip_acquire(int resource_id)
{
	struct resource *r = resources + resource_id;

	if (!r->owner) {
		/* This resource is not owned by any one. Take it! */
		r->owner = current;
		return true;
	}

	/* OK, this resource is taken by @r->owner. */

	/* Update the current process state */
	current->status = PROCESS_BLOCKED;

	/* And append current to waitqueue */
	list_add_tail(&current->list, &r->waitqueue);
	if(r->owner->prio < current->prio) r->owner->prio = current->prio;
	/**
	 * And return false to indicate the resource is not available.
	 * The scheduler framework will soon call schedule() function to
	 * schedule out current and to pick the next process to run.
	 */
	return false;
}

static void pip_release(int resource_id)
{
	struct resource *r = resources + resource_id;

	assert(r->owner == current);
	struct process *org_owner = r->owner;
	r->owner = NULL;

	if (!list_empty(&r->waitqueue)) {
		struct process *highest_priority_waiter = list_first_entry(&r->waitqueue, struct process, list);
		struct list_head *pos = NULL;
		list_for_each(pos, &r->waitqueue){
			if(list_entry(pos, struct process, list)->prio > highest_priority_waiter->prio) highest_priority_waiter = list_entry(pos, struct process, list);
		}

		assert(highest_priority_waiter->status == PROCESS_BLOCKED);

		list_del_init(&highest_priority_waiter->list);
		highest_priority_waiter->status = PROCESS_READY;
		list_add_tail(&highest_priority_waiter->list, &readyqueue);
	}
	struct process *highest_priority_waiter = NULL;
	for(int i = 0; i < NR_RESOURCES; i++){
		if (!list_empty(&resources[i].waitqueue) && resources[i].owner == org_owner) {
			if(highest_priority_waiter == NULL) highest_priority_waiter = list_first_entry(&resources[i].waitqueue, struct process, list);
			struct list_head *pos = NULL;
			list_for_each(pos, &resources[i].waitqueue){
				if(list_entry(pos, struct process, list)->prio > highest_priority_waiter->prio) highest_priority_waiter = list_entry(pos, struct process, list);
			}
		}
	}
	if(highest_priority_waiter != NULL && org_owner->prio_orig < highest_priority_waiter->prio) org_owner->prio = highest_priority_waiter->prio;
	else org_owner->prio = org_owner->prio_orig;
}

struct scheduler pip_scheduler = {
	.name = "Priority + PIP Protocol",
	.acquire = pip_acquire,
	.release = pip_release,
	.schedule = prio_schedule,
};
