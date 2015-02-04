#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>

#include "../atomic-queue/atomic.h"

static volatile DWORD curval = 0;
volatile unsigned long threads = 0;

struct argument {
	int count;
	unsigned long *buffer;
};

struct object {
	unsigned long *address;
	unsigned long refcount;
};

#define OBJECT(obj) ((volatile struct object *)&(obj))

void *worker(struct argument *arg)
{
	int i = 1;
	volatile DWORD expectedval , newval;
	AAF(&threads, 1);
	for (i = 0; i < arg->count; ++i)  {
		for (;;) {
			expectedval = curval;
			if (OBJECT(expectedval)->address == arg->buffer && threads > 1)
				continue;
			*(arg->buffer) =  !OBJECT(expectedval)->address ? 1 :
				*OBJECT(expectedval)->address + 1;
			OBJECT(newval)->address = arg->buffer;
			OBJECT(newval)->refcount = OBJECT(curval)->refcount + 1;

			if (DWCAS(&curval, expectedval, newval))
				break;
		} 
	}
	AN(&threads, -1);
	return NULL;
}

int main(int argc, char **argv)
{
	int i;
	pthread_t *t;
	struct argument *arg;
	if (argc != 3) {
		fprintf(stderr, "%s <#-threads> <#-iterations>\n", argv[0]);
		return 1;
	}
	t = (pthread_t *)calloc(atoi(argv[1]), sizeof(pthread_t));
	/* create # threads */
	for (i = 0; i < atoi(argv[1]); ++i)  {
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
	printf("all thread done -> curval:%lu [ref:%lu]\n", 
		*OBJECT(curval)->address, OBJECT(curval)->refcount);
	return 0;
}

