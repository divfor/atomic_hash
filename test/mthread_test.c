#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <assert.h>
#include "atomic_hash.h"
#include "mtrand.h"


//#define EXITTIME 20000
#define NT 16

#define time_after(a,b) ((b) > (a) || (long)(b) - (long)(a) < 0)
#define time_before(a,b)  time_after(b,a)
#define TTL_ON_ADD 0
#define TTL_ON_CREATE 0

typedef struct teststr {
  char *s;
  uint64_t hv[2];
  int len;
} teststr_t;

unsigned long
now ()
{
  struct timeval tv;
  gettimeofday (&tv, NULL);
  return (tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

int cb_dup (void *data, void *arg)
{
  return PLEASE_SET_TTL_TO_DEFAULT;
}

int cb_add (void *data, void *arg)
{
  return PLEASE_DO_NOT_CHANGE_TTL;
}

int cb_get (void *data, void *arg)
{
  *((void **)arg) = strdup(data);
  return PLEASE_DO_NOT_CHANGE_TTL;
}

int cb_del (void *data, void *arg)
{
  free (data);
  return PLEASE_REMOVE_HASH_NODE;
}

int cb_ttl (void *data, void *arg)
{
  free (data);
  return PLEASE_REMOVE_HASH_NODE;
}

int cb_random_loop (void *data, void *arg)
{
  unsigned long i, j = 0;
  //struct timeval tv;  gettimeofday (&tv, NULL);  usleep (tv.tv_usec % 1000);
  for (i = mt_rand () % 10000; i > 0; i--) j += i;
  return j % 2;
}

void *
thread_function (void *phash)
{
  hash_t *h = (hash_t *) phash;
  teststr_t *p;
  teststr_t * const a = (teststr_t *)h->teststr;
  unsigned long action, rnd, num_strings = h->teststr_num;
#ifdef EXITTIME
  unsigned long t0 = now ();
#endif
  int tid = syscall (SYS_gettid);
  char *str = NULL, *buf = NULL;
  int ret;
  while (1)
    {
      buf = NULL;
      rnd = mt_rand();
      p = &a[rnd % num_strings];
      //p = &a[__sync_fetch_and_add (&h->testidx, 1) % num_strings];
      action = (rnd * tid) % 100;
      if (action < 90)
        {
          str = strdup (p->s);
          ret = atomic_hash_add (h, p->s, p->len, str, TTL_ON_ADD, NULL, &buf);
          //ret = atomic_hash_add (h, p->s, p->len, str, action*90, NULL, &buf);
          //ret = atomic_hash_add (h, p->hv, 0, str, action*90, NULL, &buf);
          if (ret != 0)
            free (str);
        }
      else if (action < 92)
        {
          ret = atomic_hash_del (h, p->s, p->len, cb_del, &buf);
          //ret = atomic_hash_del (h, p->hv, 0, NULL, &buf);
        }
      else 
        {
          ret = atomic_hash_get (h, p->s, p->len, NULL, &buf);
          //ret = atomic_hash_get (h, p->hv, 0, NULL, &buf);
          if (ret == 0)
           {
             //str = strdup (p->s);  free (str);
             ret = strcmp(p->s, buf);
             free (buf);
             assert (ret == 0);
           }
        }
#ifdef EXITTIME
      if (now () >= t0 + EXITTIME)
	break;
#endif
    }
  if (ret)
    return NULL;
}

void *
thread_prints (void *phash)
{
  hash_t *h = (hash_t *) phash;
//  printf ("pid[%ld]: started thread_printf() \n", syscall (SYS_gettid));
  unsigned long t0 = now ();
  while (1)
    {
      sleep (5);
      atomic_hash_stats (h, now () - t0);
#ifdef EXITTIME
      if (now () >= t0 + EXITTIME)
	return NULL;
#endif
    }
  return NULL;
}

int set_cpus (int num_cpus)
{
  cpu_set_t *cpusetp;
  size_t size;
  int cpu;
  cpusetp = CPU_ALLOC(num_cpus);
  if (cpusetp == NULL) {
      perror("CPU_ALLOC");
      exit(EXIT_FAILURE);
  }
  size = CPU_ALLOC_SIZE(num_cpus);
  CPU_ZERO_S(size, cpusetp);
  for (cpu = 1; cpu < num_cpus; cpu += 2)
    CPU_SET_S(cpu, size, cpusetp);
  printf("CPU_COUNT() of set:    %d\n", CPU_COUNT_S(size, cpusetp));
  if (sched_setaffinity(0, size, cpusetp) == -1)
    {
       printf("warning: could not set CPU affinity, exit.\n");
       exit(EXIT_FAILURE);
    }  
  CPU_FREE(cpusetp);
  return (cpu/2);
}

int
main (int argc, char **argv)
{
  unsigned long num_strings = 0, nlines = 0, sl = 0, i = 0;
  char sbuf[1024];
  teststr_t *a = NULL;
  FILE *fp = NULL;
  hash_t *ptmp, *phash = NULL;
  int cpu_num = sysconf(_SC_NPROCESSORS_ONLN);
  int thread_num;
  
  if (argc >= 3)
    nlines = atoi (argv[2]);
  if (nlines <= 0)
    {
      if (!(fp = fopen (argv[1], "r")))
        return -1;
      for (nlines = 0; fgets (sbuf, 1023, fp); nlines++);
      fclose (fp);
    }
  printf ("Plan to read %ld test lines from file '%s'.\n", nlines, argv[1]);
  if (!(a = malloc (nlines * sizeof (*a))))
    return -1;
  printf ("Now reading ... ");
  if (!(fp = fopen (argv[1], "r")))
    return -1;
  num_strings = 0;
  ptmp = phash = atomic_hash_create (100, TTL_ON_CREATE);
  while (num_strings < nlines && fgets (sbuf, 1023, fp))
    { 
      if ((sl = strlen (sbuf)) < 1)
        continue;
      sbuf[sl] = '\0'; /* strip newline char */
      if ((a[num_strings].s = strdup (sbuf)) == NULL)
       {
         printf ("exiting due to no memory after load %ld lines...\n", num_strings);
         for (i = 0; i < num_strings; i++)
           free (a[i].s);
         free (a);
         return -1;
       }
      a[num_strings].len = strlen(a[num_strings].s);
      ptmp->hash_func (a[num_strings].s, a[num_strings].len, a[num_strings].hv);
      num_strings++;
    }
  fclose (fp);
  printf ("%ld lines to memory.\n", num_strings);
  atomic_hash_destroy (ptmp);

  phash = atomic_hash_create (num_strings, TTL_ON_CREATE);
  phash->on_add = cb_add;
  phash->on_ttl = cb_ttl;
  phash->on_dup = cb_dup;
  phash->on_get = cb_get;
  phash->on_del = cb_del;
  if (!phash)
    return -1;
  phash->teststr = a;
  phash->teststr_num = num_strings;
  mt_srand(now());

  thread_num = (NT <= 8 ? NT : set_cpus (cpu_num));
  thread_num = (NT > 16 ? NT : thread_num);
  pthread_t pid[thread_num + 1];
  if (pthread_create (&pid[thread_num], NULL, thread_prints, phash) < 0)
    return -1;
  printf ("Creating %d pthreads ... ", thread_num);
  for (i = 0; i < thread_num; i++)
    if (pthread_create (&pid[i], NULL, thread_function, phash) < 0)
      return -1;
  printf ("done. Now waiting...\n");
  for (i = 0; i <= thread_num; i++)
    pthread_join (pid[i], NULL);
  printf ("All threads closed.\n");
  for (i = 0; i < num_strings; i++)
    free (a[i].s);
  free (a);

  return atomic_hash_destroy (phash);
}
