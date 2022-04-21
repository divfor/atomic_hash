#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <assert.h>
#include <stdint.h>

#include "utils/mtrand.h"

#include "atomic_hash.h"
#include "atomic_hash_debug.h"


/* -- Consts -- */
//#define EXITTIME 20000
#define NT 16

// #define time_after(a,b) ((b) > (a) || (long)(b) - (long)(a) < 0)
// #define time_before(a,b)  time_after(b,a)
#define TTL_ON_ADD    0
#define TTL_ON_CREATE 0


/* -- Types -- */
typedef struct teststr {
    char *s;
    uint64_t hv[2];
    int len;
} teststr_t;


/* -- Functions -- */
static unsigned long gettime_in_ms (void) {
    struct timeval tv;
    gettimeofday (&tv, NULL);
    return (tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

/* Hooks */
static int cb_dup (__attribute__((unused))void *data, __attribute__((unused))void *arg) {
    return HOOK_RESET_TTL;
}

static int cb_add (__attribute__((unused))void *data, __attribute__((unused))void *arg) {
    return HOOK_DONT_CHANGE_TTL;
}

static int cb_get (void *data, void *arg) {
    *((void **)arg) = strdup(data);
    return HOOK_DONT_CHANGE_TTL;
}

static int cb_del (void *data, __attribute__((unused))void *arg) {
    free (data);
    return HOOK_REMOVE_HASH_NODE;
}

static int cb_ttl (void *data, __attribute__((unused))void *arg) {
    free (data);
    return HOOK_REMOVE_HASH_NODE;
}

/*
static int cb_random_loop (__attribute__((unused))void *data, __attribute__((unused))void *arg) {
    unsigned long j = 0;
    //struct timeval tv;  gettimeofday (&tv, NULL);  usleep (tv.tv_usec % 1000);
    for (unsigned long i = mt_rand () % 10000; i > 0; i--) j += i;
    return j % 2;
}
*/


/* - Test fct -*/
static void *routine_test (void *arg) {
    hash_t *h = (hash_t *) arg;

    teststr_t * const a = (teststr_t*)atomic_hash_debug_get_teststr(h);
    unsigned long num_strings = atomic_hash_debug_get_teststr_num(h);
#ifdef EXITTIME
    unsigned long t0 = gettime_in_ms ();
#endif
    int tid = syscall (SYS_gettid);
    char *buf = NULL;
    int ret;
    while (1) {
        buf = NULL;
        unsigned long rnd = mt_rand();
        teststr_t *p = &a[rnd % num_strings];
        //p = &a[__sync_fetch_and_add (&h->testidx, 1) % num_strings];

        unsigned long action = (rnd * tid) % 100;
        if (action < 90) {
            char* str = strdup (p->s);
            ret = atomic_hash_add (h, p->s, p->len, str, TTL_ON_ADD, NULL, &buf);
            //ret = atomic_hash_add (h, p->s, p->len, str, action*90, NULL, &buf);
            //ret = atomic_hash_add (h, p->hv, 0, str, action*90, NULL, &buf);
            if (ret != 0)
                free (str);

        } else if (action < 92) {
            ret = atomic_hash_del (h, p->s, p->len, cb_del, &buf);
            //ret = atomic_hash_del (h, p->hv, 0, NULL, &buf);

        } else {
            ret = atomic_hash_get (h, p->s, p->len, NULL, &buf);
            //ret = atomic_hash_get (h, p->hv, 0, NULL, &buf);
            if (ret == 0) {
                //str = strdup (p->s);  free (str);
                ret = strcmp(p->s, buf);
                free (buf);
                assert (ret == 0);
            }
        }

#ifdef EXITTIME
        if (gettime_in_ms () >= t0 + EXITTIME)
            break;
#endif
    }

    if (ret)
        return NULL;
}

static void * routine_stats (void *arg) {
    hash_t *h = (hash_t *) arg;
//    printf ("pid[%ld]: started thread_printf() \n", syscall (SYS_gettid));
    unsigned long t0 = gettime_in_ms();
    while (1) {
        sleep (5);
        atomic_hash_stats (h, gettime_in_ms() - t0);
#ifdef EXITTIME
        if (gettime_in_ms () >= t0 + EXITTIME)
            return NULL;
#endif
    }

    return NULL;
}


static int set_cpus (int num_cpus) {
    cpu_set_t *cpusetp = CPU_ALLOC(num_cpus);
    if (cpusetp == NULL) {
        perror("CPU_ALLOC");
        exit(EXIT_FAILURE);
    }

    size_t size = CPU_ALLOC_SIZE(num_cpus);
    CPU_ZERO_S(size, cpusetp);
    int cpu;
    for (cpu = 1; cpu < num_cpus; cpu += 2)
        CPU_SET_S(cpu, size, cpusetp);
    printf("CPU_COUNT() of set:    %d\n", CPU_COUNT_S(size, cpusetp));
    if (sched_setaffinity(0, size, cpusetp) == -1) {
        printf("warning: could not set CPU affinity, exit.\n");
        exit(EXIT_FAILURE);
    }
    CPU_FREE(cpusetp);

    return (cpu/2);
}



int main (int argc, char **argv) {
    int cpu_num = sysconf(_SC_NPROCESSORS_ONLN);

    unsigned long nlines = 0;
    if (argc >= 3)
        nlines = atoi (argv[2]);

    char sbuf[1024];
    FILE *fp = NULL;
    if (nlines <= 0) {
        if (!(fp = fopen (argv[1], "r"))) {
            fprintf(stderr, "Usage: %s file [line-num-to-process]\n", argv[0]);
            return -1;
        }
        for (nlines = 0; fgets (sbuf, 1023, fp); nlines++);
        fclose (fp);
    }
    printf ("About to read %lu test lines from file '%s'.\n", nlines, argv[1]);
    teststr_t *a = malloc (nlines * sizeof (*a));
    if (!a)
        return -1;


    printf ("Now reading ... ");
    if (!(fp = fopen (argv[1], "r")))
        return -1;

    hash_t *phash = atomic_hash_create (100, TTL_ON_CREATE),
           *ptmp = phash;
    unsigned long num_strings = 0;
    while (num_strings < nlines && fgets (sbuf, 1023, fp)) {
        unsigned long sl = strlen (sbuf);
        if (sl < 1)
            continue;
        sbuf[sl] = '\0'; /* strip newline char */

        if ((a[num_strings].s = strdup (sbuf)) == NULL) {
            printf ("exiting due to no memory after load %lu lines...\n", num_strings);
            for (unsigned long i = 0; i < num_strings; i++)
                free (a[i].s);
            free (a);
            return -1;
        }

        a[num_strings].len = strlen(a[num_strings].s);
        void (*hash_func)(const void *key, size_t len, void *r) = atomic_hash_debug_get_hash_func(ptmp);
        hash_func (a[num_strings].s, a[num_strings].len, a[num_strings].hv);
        num_strings++;
    }
    fclose (fp);
    printf ("%lu lines to memory.\n", num_strings);
    atomic_hash_destroy (ptmp);


    phash = atomic_hash_create (num_strings, TTL_ON_CREATE);
    if (!phash)
        return -1;
    atomic_hash_register_hooks(phash,
                               cb_ttl, cb_add, cb_dup, cb_get, cb_del);
    atomic_hash_debug_set_teststr(phash, a);
    atomic_hash_debug_set_teststr_num(phash, num_strings);
    mt_srand(gettime_in_ms());

    unsigned int thread_num;
    thread_num = (NT <= 8 ? NT : set_cpus (cpu_num));
    thread_num = (NT > 16 ? NT : thread_num);

    pthread_t pid[thread_num + 1];
    if (pthread_create(&pid[thread_num], NULL, routine_stats, phash) < 0)
        return -1;
    printf ("Creating %u pthreads ... ", thread_num);
    for (unsigned long i = 0; i < thread_num; i++)
        if (pthread_create(&pid[i], NULL, routine_test, phash) < 0)
            return -1;
    puts ("done. Now waiting...");
    for (unsigned long i = 0; i <= thread_num; i++)
        pthread_join (pid[i], NULL);

    puts ("All threads terminated.");
    for (unsigned long i = 0; i < num_strings; i++)
        free (a[i].s);
    free (a);

    return atomic_hash_destroy (phash);
}
