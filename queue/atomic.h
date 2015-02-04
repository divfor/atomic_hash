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

#ifndef _ATOMIC_H
#define _ATOMIC_H

/* double-word - as in machine word - primitive
 * used for the double-compare-and-swap operations */
#ifdef __x86_64__
typedef __uint128_t DWORD;
#else
typedef __uint64_t DWORD;
#endif

/* need:
 *   add-and-fetch
 *   compare-and-swap
 *   double-compare-and-swap */
#ifdef GCC_ATOMIC_BUILTINS			/* recent GCC has atomic builtins */
#define AAF __sync_add_and_fetch
#define CAS __sync_bool_compare_and_swap
#define DWCAS __sync_bool_compare_and_swap
#else 						/* if unavailable, use some inline assembly */

#ifdef __x86_64__
#define SHIFT 	  64
#define XADDx	  "xaddq"
#define CMPXCHGxB "cmpxchg16b"
#else
#define SHIFT 	  32
#define XADDx	  "xaddl"
#define CMPXCHGxB "cmpxchg8b"
#endif
/* add-and-fetch: atomically adds @add to @mem
 *   @mem: pointer to value
 *   @add: value to add
 *
 *   returns: new value */
static inline 
unsigned int AAF(volatile unsigned long *mem, unsigned long add)
{
	unsigned long __tmp = add;
	__asm__ __volatile__("lock " XADDx " %0,%1"
			:"+r" (add),
			"+m" (*mem)
			: : "memory");
	return add + __tmp;
}

/* add-negative: atomically adds @del to @mem
 *
 *   @mem: pointer to value
 *   @mem: value to substract
 *
 *   returns: true if 
 */

static inline 
char AN(volatile unsigned long *mem, long del)
{
	char c = 0;

	asm volatile("lock " XADDx " %2,%0; sets %1"
		     : "+m" (*mem), "=qm" (c)
		     : "r" (del) : "memory");
	return c;
}

/* compare-and-swap: atomically sets @mem to @new if value at @mem equals @old
 *   @mem: pointer to value
 *   @old: old value 
 *   @new: new value
 *
 *   returns: 0 on failure, non-zero on success */
static inline
char CAS(volatile unsigned long *mem, unsigned long old, unsigned long new)
{
	unsigned long r;
	asm volatile("lock cmpxchgl %k2,%1"
			: "=a" (r), "+m" (*mem)
			: "r" (new), "0" (old)
			: "memory");

	return r == old ? 1 : 0;
}
/* double-compare-and-swap: atomically sets the two-word data at address @mem
 *                          to the two-word value of @new if value at @mem
 *                          equals @old
 *   @mem: pointer to value
 *   @old: old value 
 *   @new: new value
 *
 *   returns: 0 on failure, non-zero on success */
static inline 
char DWCAS(volatile DWORD *mem, DWORD old, DWORD new)
{
	unsigned long old_h = old >> SHIFT, old_l = old;
	unsigned long new_h = new >> SHIFT, new_l = new;

	char r = 0;
	asm volatile("lock; " CMPXCHGxB " (%6);"
		     "setz %7; "
		     : "=a" (old_l),
		       "=d" (old_h)
		     : "0" (old_l),
		       "1" (old_h),
		       "b" (new_l),
		       "c" (new_h),
		       "r" (mem),
		       "m" (r)
		     : "cc", "memory");
	return r;
}

#endif

#endif /* _ATOMIC_H  */
