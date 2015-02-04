/*
 * Copyright (c) 1998-2010 Julien Benoist <julien@benoist.name>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>

#include "lfq.h"
#include "atomic.h"

static size_t iterations;
static volatile unsigned long input = 0;
static volatile unsigned long output = 0;

void *producer(void *arg)
{
	int i = 0;
	unsigned long *ptr;
	queue_t q = (queue_t)arg;
	for (i = 0; i < iterations; ++i) {
		ptr = (unsigned long *)malloc(sizeof(unsigned long));
		assert(ptr);
		*ptr = AAF(&input, 1);
		while (!queue_enqueue(q, (void *)ptr)) 
			;
	}
	return NULL;
}

void *consumer(void *arg)
{
	int i = 0;
	unsigned long *ptr;
	queue_t q = (queue_t)arg;
	for (i = 0; i < iterations; ++i) {
		while (!(ptr = (unsigned long *)queue_dequeue(q)))
			;
		AAF(&output, *ptr);
		*ptr = 0;
		free((void *)ptr);
	}
	return NULL;
}

int main(int argc, char **argv)
{
	int i;
	pthread_t *t;
	unsigned long verif = 0;
	queue_t q = queue_create(2);
	if (argc != 3) {
		fprintf(stderr, "%s: <nthreads> <iterations>\n", argv[0]);
		return 1;
	} 
	if (atoi(argv[1]) % 2) {
		fprintf(stderr, "%s: need an odd number of threads\n", argv[0]);
		return 1;
	}
	fprintf(stdout, "arch:%s, mode:%zu-bit, atomics:%s\n", ARCH, sizeof(void *) * 8, 
#ifdef GCC_ATOMIC_BUILTINS
		"gcc"
#else
		"asm"
#endif
		);
	t = (pthread_t *)calloc(atoi(argv[1]), sizeof(pthread_t));
	iterations = atoi(argv[2]);
	for (i = 0; i < atoi(argv[1]); ++i) 
		assert(!pthread_create(&(t[i]), NULL, 
			(i % 2) ? consumer : producer, q));
	for (i = 0; i < atoi(argv[1]); ++i) 
		assert(!pthread_join(t[i], NULL));
	for (i = 0; i <= input; ++i)
		verif += i;
	printf("input SUM[0..%lu]=%lu output=%lu\n", input, verif, output);
	return 0;
}
