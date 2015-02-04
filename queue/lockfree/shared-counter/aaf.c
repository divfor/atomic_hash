#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>

#include "../atomic-queue/atomic.h"

volatile unsigned long counter = 0;

void *worker(void *arg)
{
	int i = 0;
	for (i = 0; i < *((int *)arg); ++i)  
		AAF(&counter, 1);
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
	printf("all thread done -> counter=%lu\n", counter);
	return 0;
}

