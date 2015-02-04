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

#include <stdlib.h>

#include "lfq.h"
#include "atomic.h"

#define E(val) ((element_t *)&val)

typedef struct {
	unsigned long ptr;
	unsigned long ref;
} element_t;

struct _queue_t {
	size_t depth;
	element_t *e;
	unsigned long rear;
	unsigned long front;
};

queue_t queue_create(size_t depth)
{
	queue_t q = (queue_t)malloc(sizeof(struct _queue_t));
	if (q) {
		q->depth = depth;
		q->rear = q->front = 0;
		q->e = (element_t *)calloc(depth, sizeof(element_t));
	}
	return q;
}

int queue_enqueue(volatile queue_t q, void *data)
{
	DWORD old, new;
	unsigned long rear, front;
	do {
		rear = q->rear;	
		old = *((DWORD *)&(q->e[rear % q->depth]));
		front = q->front;
		if (rear != q->rear) 
			continue;
		if (rear == (q->front + q->depth))  {
			if (q->e[front % q->depth].ptr && front == q->front) 
				return 0;
			CAS(&(q->front), front, front + 1);
			continue;
		}
		if (!E(old)->ptr) {
			E(new)->ptr = (uintptr_t)data;
			E(new)->ref  = ((element_t *)&old)->ref + 1;
			if (DWCAS((DWORD *)&(q->e[rear % q->depth]), old, new)) {
				CAS(&(q->rear), rear, rear + 1);
				return 1;
			}
		} else if (q->e[rear % q->depth].ptr)
			CAS(&(q->rear), rear, rear + 1);
	} while(1);
}

void *queue_dequeue(volatile queue_t q)
{
	DWORD old, new;
	unsigned long front, rear;
	do {
		front = q->front;
		old = *((DWORD *)&(q->e[front % q->depth]));
		rear = q->rear;
		if (front != q->front) 
			continue;
		if (front == q->rear) {
			if (!q->e[rear % q->depth].ptr && rear == q->rear)
				return NULL;
			CAS(&(q->rear), rear, rear + 1);
			continue;
		}
		if (E(old)->ptr) {
			E(new)->ptr = 0;
			E(new)->ref = E(old)->ref + 1;
			if (DWCAS((DWORD *)&(q->e[front % q->depth]), old, new)) {
				CAS(&(q->front), front, front + 1);
				return (void *)((element_t *)&old)->ptr;
			}
		} else if (!q->e[front % q->depth].ptr)
			CAS(&(q->front), front, front + 1);
	} while(1);
}
