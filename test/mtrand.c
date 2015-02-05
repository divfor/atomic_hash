/*
   Copyright (C) 1997--2004, Makoto Matsumoto, Takuji Nishimura, and
   Eric Landry; All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

     1. Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.

     2. Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer
        in the documentation and/or other materials provided with the
        distribution.

     3. The names of its contributors may not be used to endorse or
        promote products derived from this software without specific
        prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   Any feedback is very welcome.
   http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/emt.html
   email: m-mat @ math.sci.hiroshima-u.ac.jp (remove space)

   Reference: M. Matsumoto and T. Nishimura, "Mersenne Twister:
   A 623-Dimensionally Equidistributed Uniform Pseudo-Random Number
   Generator", ACM Transactions on Modeling and Computer Simulation,
   Vol. 8, No. 1, January 1998, pp 3--30.
*/

/**************************************************
 *  Modified by Fred Huang to enable thread-safe
 *  ***********************************************
*/

#include "mtrand.h"

/* Period parameters */
#define N 624
#define M 397
#define MATRIX_A 0x9908b0dfUL	/* constant vector a */
#define UPPER_MASK 0x80000000UL	/* most significant w-r bits */
#define LOWER_MASK 0x7fffffffUL	/* least significant r bits */

static unsigned long x[N];	/* the array for the state vector  */
static unsigned long n;

void
mt_srand (unsigned long s)
{
  int i;
  n = 0;
  x[0] = s & MT_RAND_MAX;
  for (i = 1; i < N; ++i) /* for >32 bit machines */
    x[i] = (1812433253UL * (x[i-1] ^ (x[i-1] >> 30)) + i) & MT_RAND_MAX;
}

void mt_seed(void)
{
  mt_srand (5489UL);  /* Default seed */
}

/* generates a random number on the interval [0,0xffffffff] */
unsigned long
mt_rand (void)
{
  register unsigned long y, i, j;
  i = __sync_fetch_and_add (&n, 1) % N;
  j = (i == N - 1 ? 0 : i + 1);
  /* Twisted feedback */
  y = x[i] = x[i<N-M ? i+M : i+M-N] ^ \
    (((x[i] & UPPER_MASK) | (x[j] & LOWER_MASK)) >> 1) ^ \
    ((~(x[j] & 1) + 1) & MATRIX_A);
  /* Temper */
  y ^= y >> 11;
  y ^= y << 7 & 0x9d2c5680UL;
  y ^= y << 15 & 0xefc60000UL;
  y ^= y >> 18;
  return y;
}

/* generates a random number on the interval [0,1]. */
float
mt_rand_1 (void)
{
  return ((float) mt_rand () / (float) MT_RAND_MAX);
}

/* generates a random number on the interval [0,1). */
float
mt_rand_lt1 (void)
{
  /* MT_RAND_MAX must be a float before adding one to it! */
  return ((float) mt_rand () / ((float) MT_RAND_MAX + 1.0f));
}
