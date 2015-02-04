#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>

#include "../atomic-queue/atomic.h"

volatile DWORD counter = 0;

void *worker(void *arg)
{
	int i = 0;
	typeof(counter) expectedval, newval;
	for (i = 0; i < *((int *)arg); ++i)  {
		do { /* see what the current value is */
			expectedval = counter;
			*((uint64_t *)&newval) = *((uint64_t *)&expectedval) + 1;
			*(((uint64_t *)&newval) + 1) = *(((uint64_t *)&expectedval + 1)) + 1;
		} while (!DWCAS(&counter, expectedval, newval));
	}

	return NULL;
}

int main(int argc, char **argv)
{
	int i, count;
	pthread_t *t;
	if (argc != 3) {
		fprintf(stderr, "%s <#-threads> <#-iterations>\n", argv[0]);
		return 1;
	}
	count = atoi(argv[2]);
	t = (pthread_t *)calloc(atoi(argv[1]), sizeof(pthread_t));
	/* create # threads */
	for (i = 0; i < atoi(argv[1]); ++i) 
		assert(!pthread_create(&(t[i]), NULL, worker, (void *)&count));
	/* wait for the completion of all the threads */
	for (i = 0; i < atoi(argv[1]); ++i)
		assert(!pthread_join(t[i], NULL));
	/* print counter value */
	printf("all thread done -> counter={ %lu, %lu }\n", 
			*((uint64_t *)&counter), *(((uint64_t *)&counter) + 1));
	return 0;
}

