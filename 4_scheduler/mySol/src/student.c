/*
 * student.c
 * Multithreaded OS Simulation for CS 2200
 *
 * This file contains the CPU scheduler for the simulation.
 */

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os-sim.h"

extern void idle(unsigned int cpu_id);
extern void preempt(unsigned int cpu_id);
extern void yield(unsigned int cpu_id);
extern void terminate(unsigned int cpu_id);
extern void wake_up(pcb_t *process);

typedef enum{
	FIFO,
	RR,
	SRTF
}algorithm;
static algorithm alg;
static pcb_t *head = NULL;
static pcb_t *tail = NULL;
int size = 0;
void enQ(pcb_t* process);
pcb_t* deQ(void);
pcb_t* deQPri(void);

/*
 * current[] is an array of pointers to the currently running processes.r
 * There is one array element corresponding to each CPU in the simulation.
 *
 * current[] should be updated by schedule() each time a process is scheduled
 * on a CPU.  Since the current[] array is accessed by multiple threads, you
 * will need to use a mutex to protect it.  current_mutex has been provided
 * for your use.
 */
static pcb_t **current;
static pthread_mutex_t current_mutex;
static pthread_mutex_t readyQ_mutex;
static int timeslice;

pthread_cond_t check_idle;
unsigned int cpu_count;
void enQ(pcb_t* ps){
    if (size){
        tail->next = ps;
        tail = ps;
		size++;
    }else{
        head = ps;
        tail = ps;
		size = 1;
    }

}

pcb_t* deQ(void)
{
	if (head == tail)
		tail = NULL;
    if (size){
        pcb_t* tmp = head;
        head = head -> next;
        tmp -> next = NULL;
		size--;
		return tmp;
    }else{
		return NULL;
	}
}

pcb_t* deQPri(void){

	pcb_t *ptr = head;
	if(!ptr) return NULL;
	pcb_t *tgt = head;
	while(ptr){
		if(ptr->time_remaining < tgt -> time_remaining){
			tgt = ptr;
		}
		ptr = ptr->next;
	}
	pcb_t* ite = head;
	if(tgt == head){
		head = head -> next;
		//pcb_t* ite = head;
		while(ite -> next != NULL){
			ite = ite -> next;
		}
		tail = ite;
		tgt -> next = NULL;
		size--;
		return tgt;
	}
		
	ptr = head;
	while(ptr -> next != tgt){
		ptr = ptr -> next;
	}
	ptr -> next = tgt -> next;
	while(ite -> next != NULL){
		ite = ite -> next;
	}
	tail = ite;
	tgt->next = NULL;
	size--;
	return tgt;
	
}
/*
 * schedule() is your CPU scheduler.  It should perform the following tasks:
 *
 *   1. Select and remove a runnable process from your ready queue which
 *    you will have to implement with a linked list or something of the sort.
 *
 *   2. Set the process state to RUNNING
 *
 *   3. Call context_switch(), to tell the simulator which process to execute
 *      next on the CPU.  If no process is runnable, call context_switch()
 *      with a pointer to NULL to select the idle process.
 *    The current array (see above) is how you access the currently running process indexed by the cpu id.
 *    See above for full description.
 *    context_switch() is prototyped in os-sim.h. Look there for more information
 *    about it and its parameters.
 */
static void schedule(unsigned int cpu_id)
{
    pcb_t* ready_process;
	if(alg == SRTF)
		ready_process = deQPri();
	else
		ready_process = deQ();

	pthread_mutex_lock(&current_mutex);
	if(ready_process)
		ready_process->state = PROCESS_RUNNING;
	current[cpu_id] = ready_process;
	if(alg == RR)
		context_switch(cpu_id, ready_process, timeslice);
	else
		context_switch(cpu_id, ready_process, -1);
	pthread_mutex_unlock(&current_mutex);
}

extern void idle(unsigned int cpu_id)
{
    pthread_mutex_lock(&readyQ_mutex);
    while (!head)
        pthread_cond_wait(&check_idle, &readyQ_mutex);
    pthread_mutex_unlock(&readyQ_mutex);
    schedule(cpu_id);
}

extern void preempt(unsigned int cpu_id)
{
    pthread_mutex_lock(&current_mutex);
    pcb_t* current_process = current[cpu_id];
    current_process -> state = PROCESS_READY;
    pthread_mutex_unlock(&current_mutex);
    pthread_mutex_lock(&readyQ_mutex);
    enQ(current_process);
    pthread_mutex_unlock(&readyQ_mutex);
    schedule(cpu_id);
}

extern void yield(unsigned int cpu_id)
{
    pthread_mutex_lock(&current_mutex);
    pcb_t* current_process = current[cpu_id];
    current_process->state = PROCESS_WAITING;
    pthread_mutex_unlock(&current_mutex);
    schedule(cpu_id);
}
extern void terminate(unsigned int cpu_id)
{
    pthread_mutex_lock(&current_mutex);
    pcb_t* current_process = current[cpu_id];
    current_process->state = PROCESS_TERMINATED;
    pthread_mutex_unlock(&current_mutex);
    schedule(cpu_id);
}

extern void wake_up(pcb_t *process)
{
    process -> state = PROCESS_READY;
	pthread_mutex_lock(&readyQ_mutex);
    enQ(process);
	pthread_mutex_unlock(&readyQ_mutex);

	if(alg == SRTF){
		unsigned int min = 9999;
		unsigned int max = 0;
		int flag = 0;
		unsigned int idx_max;
		unsigned int i;
		pthread_mutex_lock(&current_mutex);
		for(i=0; i<cpu_count; i++){
			if(!current[i]){
				flag = 1;
				break;
			}
			if(current[i]->time_remaining < min){
				min = current[i]->time_remaining;
			}
			if(current[i] -> time_remaining > max){
				max = current[i] -> time_remaining;
				idx_max = i;
			}
		}
		pthread_mutex_unlock(&current_mutex);	
		if(!flag){
			if(min < process->time_remaining){
				force_preempt(idx_max);
			}
		}else{
			pthread_cond_signal(&check_idle);
		}
	}else{
		pthread_cond_signal(&check_idle);
	}
}

int main(int argc, char *argv[])
{
    timeslice = -1;
	if (argc < 2 || argc > 4){
		fprintf(stderr, "CS 2200 OS Sim -- Multithreaded OS Simulator\n"
                "Usage: ./os-sim <# CPUs> [ -r <time slice> | -s ]\n"
                "    Default : FIFO Scheduler\n"
                "         -r : Round-Robin Scheduler\n"
                "         -s : Shortest Remaining Time First Scheduler\n\n");
		return -1;
	}

    if (argc == 2)
		alg = FIFO;
    if (argc == 3 && strcmp(argv[2], "-s") == 0)
		alg = SRTF;
    if(argc == 4 && strcmp(argv[2], "-r") == 0){
		alg = RR;
        timeslice = (int)strtol(argv[3], NULL, 0);
	}
    cpu_count = (unsigned int)strtoul(argv[1], NULL, 0);
    printf("algorithm: %u\n", alg);
    printf("timeslice: %d\n", timeslice);

    current = malloc(sizeof(pcb_t*) * cpu_count);
    assert(current != NULL);
    pthread_mutex_init(&current_mutex, NULL);
    pthread_mutex_init(&readyQ_mutex, NULL);
    pthread_cond_init(&check_idle, NULL);
    start_simulator(cpu_count);
    
    return 0;
}
