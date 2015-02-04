#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>

#include "../atomic-queue/atomic.h"

volatile unsigned long *curval = NULL;
volatile unsigned long threads = 0;

struct argument {
	int count;
	unsigned long *buffer;
};

void *worker(struct argument *arg)
{
	int i = 0;
	volatile unsigned long *expectedval_ref;
	AAF(&threads, 1);
	for (i = 0; i < arg->count; ++i)  {
		do { 		
			expectedval_ref = curval;
			if (expectedval_ref == arg->buffer && threads > 1) 
				continue;
			*(arg->buffer) = !expectedval_ref ? 1 : (*expectedval_ref + 1);
		} while (!CAS((unsigned long *)&curval, (uintptr_t)expectedval_ref, (uintptr_t)arg->buffer));
	}
	AN(&threads, -1);
	return NULL;
}

int main(int argc, char **argv)
{
	int i, count;
	pthread_t *t;
	struct argument *arg;
	if (argc != 3) {
		fprintf(stderr, "%s <#-threads> <#-iterations>\n", argv[0]);
		return 1;
	}
	count = atoi(argv[2]);
	t = (pthread_t *)calloc(atoi(argv[1]), sizeof(pthread_t));
	/* create # threads */
	for (i = 0; i < atoi(argv[1]); ++i) {
		arg = (struct argument *)calloc(1, 
			sizeof(struct argument) + sizeof(unsigned long));
		arg->count = atoi(argv[2]);
		arg->buffer = (unsigned long *)(arg + 1);
		assert(!pthread_create(&(t[i]), NULL, (void *(*)(void *))worker, arg));
	}
	/* wait for the completion of all the threads */
	for (i = 0; i < atoi(argv[1]); ++i)
		assert(!pthread_join(t[i], NULL));
	/* print counter value */
	printf("all thread done -> values=%lu\n", *curval);
	return 0;
}

