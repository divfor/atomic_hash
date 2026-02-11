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
#include <stdint.h>

#include "utils/mtrand.h"

#include <atomic_hash.h>
#include <atomic_hash_debug.h>

#define EXPANDSTR(str) #str
#define STRINGIFY(str) EXPANDSTR(str)
#define ASSERT(TRUTH, MSG_FMT, ...) do { \
    if (! (TRUTH) ) { \
        fprintf(stderr, "ASSERT FAILED @ " __FILE__ ":" STRINGIFY(__LINE__) ": " MSG_FMT "\n", ##__VA_ARGS__); \
        abort(); \
    } \
 } while(0)


/* -- Consts -- */
//#define EXITTIME 20000
#define NT 16

// #define time_after(a,b) ((b) > (a) || (long)(b) - (long)(a) < 0)
// #define time_before(a,b)  time_after(b,a)
#define TTL_ON_ADD    0
#define TTL_ON_CREATE 0


/* -- Types -- */
typedef struct {
    char *s;
    uint64_t hv[2];
    int len;
} teststr_t;


/* -- Functions -- */
static unsigned long gettime_in_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

/* Hooks */
static int cb_dup(__attribute__((unused))void *data, __attribute__((unused))void *arg) {
    return HOOK_TTL_RESET;
}

static int cb_add(__attribute__((unused))void *data, __attribute__((unused))void *arg) {
    return HOOK_TTL_DONT_CHANGE;
}

static int cb_get(void *data, void *arg) {
    *((void**)arg) = strdup(data);
    return HOOK_TTL_DONT_CHANGE;
}

static int cb_del(void *data, __attribute__((unused))void *arg) {
    free(data);
    return HOOK_NODE_REMOVE;
}

static int cb_ttl(void *data, __attribute__((unused))void *arg) {
    free(data);
    return HOOK_NODE_REMOVE;
}

/*
static int cb_random_loop(__attribute__((unused))void *data, __attribute__((unused))void *arg) {
    unsigned long j = 0;
    //struct timeval tv;  gettimeofday(&tv, NULL);  usleep(tv.tv_usec % 1000);
    for (unsigned long i = mt_rand () % 10000; i > 0; i--) j += i;
    return j % 2;
}
*/


/* - Test fct -*/
static void *routine_test (void *arg) {
    hmap_t *hmap = (hmap_t*) arg;

    teststr_t* const hmap_teststr_ptr = (teststr_t*)atomic_hash_debug_get_teststr(hmap);
    unsigned long hmap_teststr_num = atomic_hash_debug_get_teststr_num(hmap);
#ifdef EXITTIME
    unsigned long t0 = gettime_in_ms();
#endif
    int tid = syscall(SYS_gettid);
    char *buf = NULL;
    int ret;
    while (1) {
        buf = NULL;
        unsigned long rnd = mt_rand();
        teststr_t *p = &hmap_teststr_ptr[rnd % hmap_teststr_num];
        //p = &hmap_teststr_ptr[__sync_fetch_and_add(&h->testidx, 1) % hmap_teststr_num];

        unsigned long action = (rnd * tid) % 100;
        if (action < 90) {
            char* str = strdup(p->s);
            ret = atomic_hash_add(hmap, p->s, p->len, str, TTL_ON_ADD, NULL, &buf);
            //ret = atomic_hash_add(h, p->s, p->len, str, action*90, NULL, &buf);
            //ret = atomic_hash_add(h, p->hv, 0, str, action*90, NULL, &buf);
            if (ret != 0)
                free(str);

        } else if (action < 92) {
            ret = atomic_hash_del (hmap, p->s, p->len, cb_del, &buf);
            //ret = atomic_hash_del (h, p->hv, 0, NULL, &buf);

        } else {
            ret = atomic_hash_get(hmap, p->s, p->len, NULL, &buf);
            //ret = atomic_hash_get(h, p->hv, 0, NULL, &buf);
            if (ret == 0) {
                //str = strdup(p->s);  free(str);
                ret = strcmp(p->s, buf);
                free(buf);
                ASSERT(ret == 0, "Fail");
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
    hmap_t *hmap = (hmap_t*) arg;
//    printf("pid[%ld]: started thread_printf() \n", syscall(SYS_gettid));
    unsigned long t0 = gettime_in_ms();
    while (1) {
        sleep(5);
        atomic_hash_stats (hmap, gettime_in_ms() - t0);
#ifdef EXITTIME
        if (gettime_in_ms () >= t0 + EXITTIME)
            return NULL;
#endif
    }

    return NULL;
}


static int set_cpus(const int ncpus) {
    cpu_set_t *cpuset_ptr = CPU_ALLOC(ncpus);
    if (cpuset_ptr == NULL) {
        perror("CPU_ALLOC");
        exit(EXIT_FAILURE);
    }

    size_t size = CPU_ALLOC_SIZE(ncpus);
    CPU_ZERO_S(size, cpuset_ptr);
    int cpu;
    for (cpu = 1; cpu < ncpus; cpu += 2)
        CPU_SET_S(cpu, size, cpuset_ptr);
    printf("CPU_COUNT() of set:    %d\n", CPU_COUNT_S(size, cpuset_ptr));
    if (sched_setaffinity(0, size, cpuset_ptr) == -1) {
        printf("warning: could not set CPU affinity, exit.\n");
        exit(EXIT_FAILURE);
    }
    CPU_FREE(cpuset_ptr);

    return (cpu / 2);
}



int main(int argc, char **argv) {
    const int ncpu = sysconf(_SC_NPROCESSORS_ONLN);

    unsigned long nlines = 0;
    if (argc >= 3)
        nlines = atoi(argv[2]);

    char sbuf[1024];
    FILE *fp = NULL;
    if (nlines <= 0) {
        if (!(fp = fopen(argv[1], "r"))) {
            fprintf(stderr, "Usage: %s file [line-num-to-process]\n", argv[0]);
            return -1;
        }
        for (nlines = 0; fgets(sbuf, 1023, fp); nlines++);
        fclose(fp);
    }
    printf("About to read %lu test lines from file '%s'.\n", nlines, argv[1]);
    teststr_t *teststr_ptr = malloc(nlines * sizeof(*teststr_ptr));
    if (!teststr_ptr)
        return -1;


    printf("Now reading ... ");
    if (!(fp = fopen(argv[1], "r")))
        return -1;

    hmap_t *phash = atomic_hash_create(100, TTL_ON_CREATE),
           *ptmp = phash;
    unsigned long num_strings = 0;
    while (num_strings < nlines && fgets(sbuf, 1023, fp)) {
        unsigned long sl = strlen(sbuf);
        if (sl < 1)
            continue;
        sbuf[sl] = '\0'; /* strip newline char */

        if ((teststr_ptr[num_strings].s = strdup (sbuf)) == NULL) {
            printf("exiting due to no memory after load %lu lines...\n", num_strings);
            for (unsigned long i = 0; i < num_strings; i++)
                free(teststr_ptr[i].s);
            free (teststr_ptr);
            return -1;
        }

        teststr_ptr[num_strings].len = strlen(teststr_ptr[num_strings].s);
        void (*hash_func)(const void *key, size_t len, void *r) = atomic_hash_debug_get_hash_func(ptmp);
        hash_func(teststr_ptr[num_strings].s, teststr_ptr[num_strings].len, teststr_ptr[num_strings].hv);
        num_strings++;
    }
    fclose(fp);
    printf("%lu lines to memory.\n", num_strings);
    atomic_hash_destroy(ptmp);


    phash = atomic_hash_create(num_strings, TTL_ON_CREATE);
    if (!phash)
        return -1;
    atomic_hash_register_hooks(phash,
                               cb_ttl, cb_add, cb_dup, cb_get, cb_del);
    atomic_hash_debug_set_teststr(phash, teststr_ptr);
    atomic_hash_debug_set_teststr_num(phash, num_strings);
    mt_srand(gettime_in_ms());


    unsigned int thread_num;
    thread_num = (NT <= 8 ? NT : set_cpus (ncpu));
    thread_num = (NT > 16 ? NT : thread_num);

    pthread_t threads[thread_num + 1];
    if (pthread_create(&threads[thread_num], NULL, routine_stats, phash) != 0)
        return -1;
    printf("Creating %u pthreads ... ", thread_num);
    for (unsigned long i = 0; i < thread_num; i++)
        if (pthread_create(&threads[i], NULL, routine_test, phash) != 0)
            return -1;
    puts("done. Now waiting for threads to finish...");
    for (unsigned long i = 0; i <= thread_num; i++)
        pthread_join(threads[i], NULL);
    puts ("All threads have finished.");


    for (unsigned long i = 0; i < num_strings; i++)
        free(teststr_ptr[i].s);
    free(teststr_ptr);

    return atomic_hash_destroy (phash);
}
