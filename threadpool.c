#include "threadpool.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <assert.h>

int test(void*);
void printError();


void printError()
{
	perror("error: <sys_call>\n");
}
work_t* get_first_work(threadpool ** pool)
{
	if( (*pool)->qhead == NULL )
		return NULL;
	work_t * work_to_return = (*pool)->qhead;
	(*pool)->qhead = (*pool)->qhead->next;
	(*pool)->qsize--;
	return work_to_return;

}
void* do_work(void* p)
{
	threadpool * pool = (threadpool*)p;
	while(true)
	{
		if(pool->shutdown == 1)
			return NULL;

		pthread_mutex_lock(&pool->qlock); // locking the queue
		if(pool->qsize == 0)
		{
			pthread_cond_wait(&pool->q_not_empty,&pool->qlock); // if the queue is empty waiting untill q_not_empty to become true
			if(pool->shutdown == 1)  // if thread got the signal from destroy function than it should exit.
			{
				pthread_mutex_unlock(&pool->qlock);
				return NULL;
			}
		}
		work_t * workToDo = get_first_work(&pool);
		if(workToDo == NULL)
		{
			pthread_mutex_unlock(&pool->qlock);
			continue;
		} // double check that the queue is not empty.
		pthread_mutex_unlock(&pool->qlock); // after taking a the first job now we can unlock the queue.
		workToDo->routine(workToDo->arg);
		free(workToDo);
		if(pool->qsize == 0 && pool->dont_accept == 1)
			pthread_cond_signal(&pool->q_empty);
	}
}
void dispatch(threadpool* from_me, dispatch_fn dispatch_to_here, void *arg)
{
	pthread_mutex_lock(&from_me->qlock); // locking the queue before adding a new job
	if(from_me->dont_accept == 1) // dont accept signal is on so we dont add new job to the queue. 
	{
		pthread_mutex_unlock(&from_me->qlock);
		return;
	}
	work_t* newWork = (work_t*)malloc(sizeof(work_t));
	if(!newWork)
		return; 
	newWork->routine = dispatch_to_here;
	newWork->arg = arg;
	newWork->next = NULL;
	if(from_me->qhead == NULL)
	{
		from_me->qhead = newWork;
		from_me->qtail = newWork;
	}
	else
	{
		from_me->qtail->next = newWork;
		from_me->qtail = newWork;
	}
	from_me->qsize++;
	pthread_mutex_unlock(&from_me->qlock); // unlocking the queue after adding a new job.
	pthread_cond_signal(&from_me->q_not_empty);
}
threadpool* create_threadpool(int num_threads_in_pool)
{
	if(num_threads_in_pool > MAXT_IN_POOL  || num_threads_in_pool <= 0)
		return NULL;
	threadpool * pool = (threadpool*)malloc(sizeof(threadpool));
	if(!pool)
		return NULL;
	pool->num_threads = num_threads_in_pool;
	pool->qsize = 0;
	pool->threads = (pthread_t*)malloc(num_threads_in_pool*sizeof(pthread_t));
	if(!pool->threads)
	{
		free(pool);
		return NULL;
	}
	pool->qhead = pool->qtail = NULL;
	pthread_cond_init(&pool->q_not_empty,NULL);
	pthread_cond_init(&pool->q_empty,NULL);
	pthread_mutex_init(&pool->qlock,NULL);
	pool->shutdown = pool->dont_accept = 0;
	for (int i = 0; i < num_threads_in_pool; ++i)
	{
		pthread_create(&pool->threads[i],NULL,do_work,pool); //
	}
	return pool;

}
void destroy_threadpool(threadpool* destroyme)
{
	pthread_mutex_lock(&destroyme->qlock);

	destroyme->dont_accept = 1;
	if(destroyme->qsize > 0)
	{
		pthread_cond_wait(&destroyme->q_empty,&destroyme->qlock);
	}
	destroyme->shutdown = 1;
	pthread_mutex_unlock(&destroyme->qlock);
	pthread_cond_broadcast(&destroyme->q_not_empty);
	for (int i = 0; i < destroyme->num_threads; ++i)
	{
		pthread_join(destroyme->threads[i],NULL);
	}
	free(destroyme->threads);
	pthread_mutex_destroy(&destroyme->qlock);
	pthread_cond_destroy(&destroyme->q_empty);
	pthread_cond_destroy(&destroyme->q_not_empty);
	free(destroyme);
}
