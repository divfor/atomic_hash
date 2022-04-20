/*
 * atomic_hash.c
 *
 * 2012-2015 Copyright (c)
 * Fred Huang, <divfor@gmail.com>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/*
 * - TODOs:
 *   1. Hashmap:
 *     1.1. allow hash functions to accept hash value as input instead of key that can reduce hash cacalulating.
 *     1.2. enable elastic memory pool for screnaios that run with huge number of hash nodes (approximate 3 million nodes / 100 MB)
 *   2. CMake:
 *     2.1. Integrate unimplemented hash functions
 *     2.2. CMake install option
 *
 *
 * - Changelog (since fork):
 *   - Added `PRINT_DEBUG_MSG` to some `printf`s to omit debug output in non-debug build
 *   - Added `static` keyword to all `inline`d functions (https://stackoverflow.com/a/54875926)
 *
 *   - Fixed compiler warnings:
 *      - unused variables removed
 *      - uninitialized variables initialized to 0
 *      - solved comparison of integer with different signedness
 *
 *   - Compiler error when no hash function was "selected"
 *   - Replace `__asm__("pause")` w/ `usleep(1)` + `#include <unistd.h>` (for more portable code)
 *
 *   - Removed not working hash functions + add support for "selecting" function
 *
 *   - Opaque data type
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <sys/time.h>
#include <sched.h>
#include <unistd.h>

#include "atomic_hash.h"
#include "atomic_hash_debug.h"


/* -- Consts -- */
#define NMHT      2
#define NCLUSTER  4
#define NSEAT     (NMHT * NKEY * NCLUSTER)
#define NNULL     0xFFFFFFFF
#define MAXTAB    NNULL
#define MINTAB    64
#define COLLISION 1000 /* 0.01 ~> avg 25 in seat */
#define MAXSPIN   (1<<20) /* 2^20 loops 40ms with pause + sched_yield on xeon E5645 */

/* - Available hash functions - */
#define CITY3HASH_128   1
#define MD5HASH         2
// #define MPQ3HASH        3
// #define NEWHASH         4
// #define MURMUR3HASH_128 5


/* -- Types -- */
#if HASH_FUNCTION == CITY3HASH_128 || HASH_FUNCTION == MD5HASH // || HASH_FUNCTION == MURMUR3HASH_128
#  define NKEY 4
#  define NCMP 2

typedef uint64_t hvu;
typedef struct { hvu x, y; } hv;
// #elif HASH_FUNCTION == MPQ3HASH || HASH_FUNCTION == NEWHASH
// #  define NKEY 3
// #  define NCMP 3
//
// typedef uint32_t hvu;
// typedef struct hv { hvu x, y, z; } hv_t;
#endif

#define shared __attribute__((aligned(64)))

typedef uint32_t nid;
typedef struct {
    unsigned long expires;
    unsigned long escapes;
    unsigned long add_nomem;
    unsigned long add_nosit;
    unsigned long del_nohit;
    unsigned long get_nohit;
    unsigned long mem_htabs;
    unsigned long mem_nodes;
    unsigned long max_nodes;
    unsigned long key_collided;
} hstats_t;

// typedef struct {
//     unsigned long xadd, xget, xdel, nexp;
// } hc_t; /* 64-bytes cache line */

typedef struct {
    void **ba;
    shared nid mask,
               shift; /* used for i2p() only */
    shared nid max_blocks,
               blk_node_num,
               node_size,
               blk_size;
    volatile nid curr_blocks;
} mem_pool_t;

typedef union {
    struct { nid mi, rfn; } cas;
    uint64_t all;
} cas_t;

typedef struct {
    volatile hv v;
    unsigned long expire; /* expire in ms # of gettimeofday(), 0 = never */
    void *data;
} node_t;

typedef struct {
    nid *b;             /* hash tab (int array as memory index */
    unsigned long ncur,
                  n,
                  nb;  /* nb: buckets #, set by n * r */
    unsigned long nadd,
                  ndup,
                  nget,
                  ndel;
} htab_t;

struct hash_t {
/* hash function */
    shared void (*hash_func)(const void *key, size_t len, void *r);

/* hook func to deal w/ user data in safe zone */
    shared hook on_ttl,
                on_add,
                on_dup,
                on_get,
                on_del;
    shared volatile cas_t freelist; /* free hash node list */
    shared htab_t ht[3]; /* ht[2] for array [MINTAB] */
    shared hstats_t stats;
    shared void **hp;
    shared mem_pool_t *mp;
    shared unsigned long reset_expire; /* if > 0, reset node->expire */
    shared unsigned long nmht,
                         ncmp;
    shared unsigned long nkey,
                         npos,
                         nseat; /* nseat = 2*npos = 4*nkey */

    shared void *teststr;
    shared unsigned long testidx,
                         teststr_num;
};


/* -- 'Aliases' / 'Fct-like' macros -- */
#define memword                 __attribute__((aligned(sizeof(void *))))

#define atomic_add1(v)          __sync_fetch_and_add(&(v), 1)
#define atomic_sub1(v)          __sync_fetch_and_sub(&(v), 1)
#define add1(v)                 __sync_fetch_and_add(&(v), 1)
#define cas(dst, old, new)      __sync_bool_compare_and_swap((dst), (old), (new))

#define ip(mp, type, i)         (((type *)(mp->ba[(i) >> mp->shift]))[(i) & mp->mask])
#define i2p(mp, type, i)        (i == NNULL ? NULL : &(ip(mp, type, i)))
//#define unhold_bucket(hv, v)    do { if ((hv).y && !(hv).x) (hv).x = (v).x; } while(0)
#define unhold_bucket(hv, v)    while ((hv).y && !cas (&(hv).x, 0, (v).x))
#define hold_bucket_otherwise_return_0(hv, v) do { unsigned long __l = MAXSPIN; \
          while (!cas(&(hv).x, (v).x, 0)) { /* when CAS fails */ \
            if ((hv).x != (v).x) return 0; /* already released or reused */\
            while ((hv).x == 0) { /* wait for unhold */ \
              if ((hv).y == 0) return 0; /* no unhold but released */ \
              if (--__l == 0) { add1 (h->stats.escapes); return 0; } /* give up */ \
              if (__l & 0x0f) usleep(1); else sched_yield(); /* original: __asm__("pause") */ \
          }} \
          if ((hv).y != (v).y || (hv).y == 0) { unhold_bucket (hv, v); return 0; } \
          } while (0)

/* - Debugging macros - */
// #define PRINT_DEBUG_MSGS
#ifdef PRINT_DEBUG_MSGS
#  define PRINT_DEBUG_MSG(format, ...) printf(format, ##__VA_ARGS__)
#else
#  define PRINT_DEBUG_MSG(format, ...) do { } while(0)
#endif



/* -- Functions -- */
static inline unsigned long gettime_in_ms (void) {
    struct timeval tv;
    gettimeofday (&tv, NULL);
    return (tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

/* -------------------- -------------------- -------------------- -------------------- -------------------- */
static mem_pool_t *create_mem_pool (unsigned int max_nodes, unsigned int node_size) {
    unsigned int pwr2_max_nodes,
                 pwr2_node_size,
                 pwr2_total_size,
                 pwr2_block_size;
    mem_pool_t *pmp;

#define PW2_MAX_BLK_PTR 9   /* hard code one 4K-page for max 512 block pointers */
#define PW2_MIN_BLK_SIZ 12  /* 2^12 = 4K page size */

    for (pwr2_max_nodes = 0; (1u << pwr2_max_nodes) < max_nodes; pwr2_max_nodes++);
    if (pwr2_max_nodes == 0 || pwr2_max_nodes > 32) /* auto resize for exception, use 1MB as mem index and 1MB block size*/
        pwr2_max_nodes = 32;

    for (pwr2_node_size = 0; (1u << pwr2_node_size) < node_size; pwr2_node_size++);
    if ((1u << pwr2_node_size) != node_size || pwr2_node_size < 5 || pwr2_node_size > 12) {
        PRINT_DEBUG_MSG("node_size should be N power of 2, 5 <= N <= 12(4KB page)");
        return NULL;
    }

    if (posix_memalign ((void **) (&pmp), 64, sizeof (*pmp)))
        return NULL;
    memset (pmp, 0, sizeof (*pmp));

    pwr2_total_size = pwr2_max_nodes + pwr2_node_size;
    pwr2_block_size = (pwr2_total_size <= PW2_MAX_BLK_PTR + PW2_MIN_BLK_SIZ) ? (PW2_MIN_BLK_SIZ) : (pwr2_total_size - PW2_MAX_BLK_PTR);

    pmp->max_blocks =   (nid)(1 << (PW2_MAX_BLK_PTR));
    pmp->node_size =    (nid)(1 << pwr2_node_size);
    pmp->blk_size =     (nid)(1 << pwr2_block_size);
    pmp->blk_node_num = (nid)(1 << (pwr2_block_size - pwr2_node_size));
    pmp->shift =        (nid)pwr2_block_size - pwr2_node_size;
    pmp->mask =         (nid)((1 << pmp->shift) - 1);
    pmp->curr_blocks =  0;

    if (!posix_memalign ((void **) (&pmp->ba), 64, pmp->max_blocks * sizeof (*pmp->ba))) {
        memset (pmp->ba, 0, pmp->max_blocks * sizeof (*pmp->ba));
        return pmp;
    }

    free (pmp);
    return NULL;
}

static int destroy_mem_pool (mem_pool_t * pmp) {
    if (!pmp)
        return -1;

    for (unsigned long i = 0; i < pmp->max_blocks; i++)
        if (pmp->ba[i]) {
            free (pmp->ba[i]);
            pmp->ba[i] = NULL;
            pmp->curr_blocks--;
        }

    free (pmp->ba);
    pmp->ba = NULL;
    free (pmp);

    return 0;
}

static inline nid *new_mem_block (mem_pool_t *pmp, volatile cas_t *recv_queue) {

    void *p = calloc (pmp->blk_node_num, pmp->node_size);
    if (!pmp || !p)
        return NULL;

    nid i;
    for (i = pmp->curr_blocks; i < pmp->max_blocks; i++)
        if (cas (&pmp->ba[i], NULL, p)) {
            atomic_add1 (pmp->curr_blocks);
            break;
        }

    if (i == pmp->max_blocks) {
        free (p);
        return NULL;
    }

    nid sz =   pmp->node_size,
        m =    pmp->mask,
        head = i * (m + 1);
    for (i = 0; i < m; i++)
        *(nid *) ((char *)p + i * sz) = head + i + 1;

    memword cas_t *pn = (cas_t *) ((char *)p + m * sz);
    pn->cas.mi =  NNULL;
    pn->cas.rfn = 0;
    memword cas_t n,
                  x;
    x.cas.mi = head;
    do {
        n.all = recv_queue->all;
        pn->cas.mi = n.cas.mi;
        x.cas.rfn = n.cas.rfn + 1;
    } while (!cas (&recv_queue->all, n.all, x.all));

    return (nid *) ((char *)p + m * sz);
}

/* Default hooks */
static int default_func_reset_ttl (void *hash_data, void *return_data) {
    if (return_data)
        *((void **)return_data) = hash_data;
    return HOOK_SET_TTL_TO_DEFAULT;
}

/* define your own func to return different ttl or removal instructions */
static int default_func_not_change_ttl (void *hash_data, void *return_data) {
    if (return_data)
        *((void **)return_data) = hash_data;
    return HOOK_DONT_CHANGE_TTL;
}

static int default_func_remove_node (void *hash_data, void *return_data) {
    if (return_data)
        *((void **)return_data) = hash_data;
    return HOOK_REMOVE_HASH_NODE;
}

static int init_htab (htab_t *ht, unsigned long num, double ratio) {
    unsigned long i,
                  nb = num * ratio;
    for (i = 134217728; nb > i; i *= 2);

//  nb = (nb >= 134217728) ? i : nb; // improve folding for more than 1/32 of MAXTAB (2^32)
    ht->nb = (i > MAXTAB) ? MAXTAB : ((nb < MINTAB) ? MINTAB : nb);
    ht->n =  num; //if 3rd tab: n <- 0, nb <- MINTAB, r <- COLLISION

    if (!(ht->b = calloc (ht->nb, sizeof (*ht->b))))
        return -1;

    for (i = 0; i < ht->nb; i++)
        ht->b[i] = NNULL;

    PRINT_DEBUG_MSG("expected nb[%lu] = n[%lu] * r[%f]\n", (unsigned long) (num * ratio),
            num, ratio);
    PRINT_DEBUG_MSG("actual   nb[%lu] = n[%lu] * r[%f]\n", ht->nb, ht->n,
            (ht->n == 0 ? ratio : ht->nb * 1.0 / ht->n));

    return 0;
}


hash_t *atomic_hash_create (unsigned int max_nodes, int reset_ttl) {
    if (max_nodes < 2 || max_nodes > MAXTAB) {
        PRINT_DEBUG_MSG("max_nodes range: 2 ~ %lu\n", (unsigned long) MAXTAB);
        return NULL;
    }


    hash_t *h;
    if (posix_memalign ((void **) (&h), 64, sizeof (*h)))
        return NULL;
    memset (h, 0, sizeof (*h));

#if HASH_FUNCTION == MD5HASH
#  include "hash_functions/hash_md5.h"
    h->hash_func = md5hash;
#elif HASH_FUNCTION == CITY3HASH_128
#  include "hash_functions/hash_city.h"
    h->hash_func = cityhash_128;
// #elif HASH_FUNCTION == MPQ3HASH
// #  include "hash_functions/hash_mpq.h"
//   uint32_t ct[0x500];
//   init_crypt_table (ct);
//   h->hash_func = mpq3hash;
// #elif HASH_FUNCTION == NEWHASH
// #  include "hash_functions/hash_newhash.h"
//   h->hash_func = newhash;
// #elif HASH_FUNCTION == MURMUR3HASH_128
// #  include "hash_functions/hash_murmur3.h"
//   h->hash_func = MurmurHash3_x64_128;
#else
#  error "atomic_hash: No hash function has been selected!"
#endif

    h->on_ttl =          default_func_remove_node;
    h->on_del =          default_func_remove_node;
    h->on_add =          default_func_not_change_ttl;
    h->on_get =          default_func_not_change_ttl;
    h->on_dup =          default_func_reset_ttl;

    h->reset_expire =    reset_ttl;
    h->nmht =            NMHT;
    h->ncmp =            NCMP;
    h->nkey =            NKEY;   /* uint32_t # of hash function's output */
    h->npos =            h->nkey * NCLUSTER; /* pos # in one hash table */
    h->nseat =           h->npos * h->nmht; /* pos # in all hash tables */
    h->freelist.cas.mi = NNULL;


    htab_t *ht1 = &h->ht[0],            /* bucket array 1, 2 and collision array */
           *ht2 = &h->ht[1],
           *at1 = &h->ht[NMHT];

/* n1 -> n2 -> 1/tuning
 * nb1 = n1 * r1, r1 = ((n1+2)/tuning/K^2)^(K^2 - 1)
 * nb2 = n2 * r2 == nb1 / K == ((n2+2)/tuning/K))^(K - 1)
 */
    PRINT_DEBUG_MSG("init bucket array 1:\n");
    const double collision = COLLISION; /* collision control, larger is better */
    double K = h->npos + 1;
    unsigned long n1 = max_nodes;
    double r1 = pow ((n1 * collision / (K * K)), (1.0 / (K * K - 1)));
    if (init_htab (ht1, n1, r1) < 0)
        goto calloc_exit;

    PRINT_DEBUG_MSG("init bucket array 2:\n");
    unsigned long n2 = (n1 + 2.0) / (K * pow (r1, K - 1));
    double r2 = pow (((n2 + 2.0) * collision / K), 1.0 / (K - 1));
    if (init_htab (ht2, n2, r2) < 0)
        goto calloc_exit;

    PRINT_DEBUG_MSG("init collision array:\n");
    if (init_htab (at1, 0, collision) < 0)
        goto calloc_exit;

    h->mp = create_mem_pool (max_nodes, sizeof (node_t));
//  h->mp = old_create_mem_pool (ht1->nb + ht2->nb + at1->nb, sizeof (node_t), max_blocks);
    PRINT_DEBUG_MSG("shift=%u; mask=%u\n", h->mp->shift, h->mp->mask);
    PRINT_DEBUG_MSG("mem_blocks:\t%u/%u, %ux%u bytes, %u bytes per block\n", h->mp->curr_blocks, h->mp->max_blocks,
            h->mp->blk_node_num, h->mp->node_size, h->mp->blk_size);
    if (!h->mp)
        goto calloc_exit;

    h->stats.max_nodes = h->mp->max_blocks * h->mp->blk_node_num;
    h->stats.mem_htabs = ((ht1->nb + ht2->nb) * sizeof (nid)) >> 10;
    h->stats.mem_nodes = (h->stats.max_nodes * h->mp->node_size) >> 10;
    return h;


calloc_exit:
    for (unsigned long j = 0; j < h->nmht; j++)
        if (h->ht[j].b)
            free (h->ht[j].b);
    destroy_mem_pool (h->mp);
    free (h);

    return NULL;
}

int atomic_hash_stats (hash_t *h, unsigned long escaped_milliseconds) {
    const hstats_t *t = &h->stats;
    const htab_t *ht1 = &h->ht[0],
                 *ht2 = &h->ht[1];
    mem_pool_t *m = h->mp;
    double d =         1024.0,
           blk_in_kB = m->blk_size / d,
           mem =       m->curr_blocks * blk_in_kB;
    char *log_delim = "    ";

    printf("mem=%.2f, blk_in_kB=%.2f, curr_block=%u, blk_nod_num=%u, node_size=%u\n",
            mem, blk_in_kB, m->curr_blocks, m->blk_node_num, m->node_size);
    printf ("\n");
    printf ("mem_blocks:\t%u/%u, %ux%u bytes, %.2f MB per block\n", m->curr_blocks, m->max_blocks,
            m->blk_node_num, m->node_size, m->blk_size/1048576.0);
    printf ("mem_to_max:\thtabs[%.2f]MB, nodes[%.2f]MB, total[%.2f]MB\n",
            t->mem_htabs / d, t->mem_nodes / d, (t->mem_htabs + t->mem_nodes) / d);
    printf ("mem_in_use:\thtabs[%.2f]MB, nodes[%.2f]MB, total[%.2f]MB\n",
            t->mem_htabs / d, mem / d, (t->mem_htabs + mem) / d);
    printf ("n1[%lu]/n2[%lu]=[%.3f],  nb1[%lu]/nb2[%lu]=[%.2f]\n",
            ht1->n, ht2->n, ht1->n * 1.0 / ht2->n, ht1->nb, ht2->nb,
            ht1->nb * 1.0 / ht2->nb);
    printf ("r1[%f]/r2[%f],  performance_wall[%.1f%%]\n",
            ht1->nb * 1.0 / ht1->n, ht2->nb * 1.0 / ht2->n,
            ht1->n * 100.0 / (ht1->nb + ht2->nb));
    printf ("---------------------------------------------------------------------------\n");
    printf ("tab n_cur %s%sn_add %s%sn_dup %s%sn_get %s%sn_del\n", log_delim, log_delim, log_delim, log_delim, log_delim, log_delim, log_delim, log_delim);

    htab_t *p;
    unsigned long ncur = 0,
                  nadd = 0,
                  ndup = 0,
                  nget = 0,
                  ndel = 0;
    for (unsigned long j = 0; j <= NMHT && (p = &h->ht[j]); j++) {
        ncur += p->ncur;
        nadd += p->nadd;
        ndup += p->ndup;
        nget += p->nget;
        ndel += p->ndel;
        printf ("%-4lu%-14lu%-14lu%-14lu%-14lu%-14lu\n", j, p->ncur, p->nadd, p->ndup, p->nget, p->ndel);
    }
    unsigned long op = ncur + nadd + ndup + nget + ndel + t->get_nohit + t->del_nohit + t->add_nosit + t->add_nomem + t->escapes;

    printf ("sum %-14lu%-14lu%-14lu%-14lu%-14lu\n", ncur, nadd, ndup, nget, ndel);
    printf ("---------------------------------------------------------------------------\n");
    printf ("del_nohit %sget_nohit %sadd_nosit %sadd_nomem %sexpires %sescapes\n", log_delim, log_delim, log_delim, log_delim, log_delim);
    printf ("%-14lu%-14lu%-14lu%-14lu%-12lu%-12lu\n", t->del_nohit,
            t->get_nohit, t->add_nosit, t->add_nomem, t->expires, t->escapes);
    printf ("---------------------------------------------------------------------------\n");
    if (escaped_milliseconds > 0)
        printf ("escaped_time=%.3fs, op=%lu, ops=%.2fM/s\n", escaped_milliseconds * 1.0 / 1000, op,
                (double) op / 1000.0 / escaped_milliseconds);
    printf ("\n");

    fflush (stdout);
    return 0;
}

int atomic_hash_destroy (hash_t *h) {
    if (!h)
        return -1;

    for (unsigned int j = 0; j < h->nmht; j++)
        free (h->ht[j].b);
    destroy_mem_pool (h->mp);
    free (h);

    return 0;
}

void atomic_hash_register_hooks(hash_t *h,
                                hook on_ttl, hook on_add, hook on_dup, hook on_get, hook on_del) {
    if (on_ttl) {
        h->on_ttl = on_ttl;
    }
    if (on_add) {
        h->on_add = on_add;
    }
    if (on_dup) {
        h->on_dup = on_dup;
    }
    if (on_get) {
        h->on_get = on_get;
    }
    if (on_del) {
        h->on_del = on_del;
    }
}


/* -------------------- -------------------- -------------------- -------------------- -------------------- */
static inline nid new_node (hash_t *h) {
    memword cas_t n,
                  m;
    while (h->freelist.cas.mi != NNULL || new_mem_block (h->mp, &h->freelist)) {
        n.all = h->freelist.all;
        if (n.cas.mi == NNULL)
            continue;

        m.cas.mi = ((cas_t *) (i2p (h->mp, node_t, n.cas.mi)))->cas.mi;
        m.cas.rfn = n.cas.rfn + 1;

        if (cas (&h->freelist.all, n.all, m.all))
            return n.cas.mi;
    }

    add1 (h->stats.add_nomem);
    return NNULL;
}

static inline void free_node (hash_t *h, nid mi) {
    cas_t *p = (cas_t *) (i2p (h->mp, node_t, mi));
    p->cas.rfn = 0;

    memword cas_t n,
                  m;
    m.cas.mi = mi;
    do {
        n.all = h->freelist.all;
        m.cas.rfn = n.cas.rfn + 1;
        p->cas.mi = n.cas.mi;
    } while (!cas (&h->freelist.all, n.all, m.all));
}

static inline void set_hash_node (node_t *p, hv v, void *data, unsigned long expire) {
    p->v =      v;
    p->expire = expire;
    p->data =   data;
}

static inline int likely_equal (hv w, hv v) {
    return w.y == v.y;
}

/* only called in atomic_hash_get */
static inline int try_get (hash_t *h, hv v, node_t *p, nid *seat, nid mi, int idx,  hook cbf, void *rtn) {
    hold_bucket_otherwise_return_0 (p->v, v);
    if (*seat != mi) {
        unhold_bucket (p->v, v);
        return 0;
    }

    int result = cbf ? cbf (p->data, rtn) : h->on_get (p->data, rtn);
    if (result == HOOK_REMOVE_HASH_NODE) {
        if (cas (seat, mi, NNULL))
            atomic_sub1 (h->ht[idx].ncur);
        memset (p, 0, sizeof (*p));
        add1 (h->ht[idx].nget);
        free_node (h, mi);
        return 1;
    }
    if (result == HOOK_SET_TTL_TO_DEFAULT)
        result = h->reset_expire;
    if (p->expire > 0 && result > 0)
        p->expire = result + gettime_in_ms();
    unhold_bucket (p->v, v);
    add1 (h->ht[idx].nget);
    return 1;
}

/* only called in atomic_hash_add */
static inline int try_dup (hash_t *h, hv v, node_t *p, nid *seat, nid mi, int idx,  hook cbf, void *rtn) {
    hold_bucket_otherwise_return_0 (p->v, v);
    if (*seat != mi) {
        unhold_bucket (p->v, v);
        return 0;
    }

    int result = cbf ? cbf (p->data, rtn) : h->on_dup (p->data, rtn);
    if (result == HOOK_REMOVE_HASH_NODE) {
        if (cas (seat, mi, NNULL))
            atomic_sub1 (h->ht[idx].ncur);
        memset (p, 0, sizeof (*p));
        add1 (h->ht[idx].ndup);
        free_node (h, mi);
        return 1;
    }
    if (result == HOOK_SET_TTL_TO_DEFAULT)
        result = h->reset_expire;
    if (p->expire > 0 && result > 0)
        p->expire = result + gettime_in_ms();
    unhold_bucket (p->v, v);
    add1 (h->ht[idx].ndup);
    return 1;
}

/* only called in `atomic_hash_add` */
static inline int try_add (hash_t *h, node_t *p, nid *seat, nid mi, int idx, void *rtn) {
    hvu x = p->v.x;
    p->v.x = 0;
    if (!cas (seat, NNULL, mi)) {
        p->v.x = x;
        return 0; /* other thread wins, caller to retry other seats */
    }

    atomic_add1 (h->ht[idx].ncur);
    int result = h->on_add (p->data, rtn);
    if (result == HOOK_REMOVE_HASH_NODE) {
        if (cas (seat, mi, NNULL))
            atomic_sub1 (h->ht[idx].ncur);
        memset (p, 0, sizeof (*p));
        free_node (h, mi);
        return 1; /* abort adding this node */
    }
    if (result == HOOK_SET_TTL_TO_DEFAULT)
        result = h->reset_expire;
    if (p->expire > 0 && result > 0)
        p->expire = result + gettime_in_ms();
    p->v.x = x;
    add1 (h->ht[idx].nadd);
    return 1;
}

/* only called in atomic_hash_del */
static inline int try_del (hash_t *h, hv v, node_t *p, nid *seat, nid mi, int idx,  hook cbf, void *rtn) {
    hold_bucket_otherwise_return_0 (p->v, v);
    if (*seat != mi || !cas (seat, mi, NNULL)) {
        unhold_bucket (p->v, v);
        return 0;
    }

    atomic_sub1 (h->ht[idx].ncur);
    void *user_data = p->data;
    memset (p, 0, sizeof (*p));
    add1 (h->ht[idx].ndel);
    free_node (h, mi);
    if (cbf)
        cbf (user_data, rtn);
    else
        h->on_del (user_data, rtn);
    return 1;
}

static inline int valid_ttl (hash_t *h, unsigned long now, node_t *p, nid *seat, nid mi,
                             int idx, nid *node_rtn, void *data_rtn) {
    unsigned long expire = p->expire;
    /* valid state, quickly skip to call try_action. */
    if (expire == 0 || expire > now)
        return 1;

    hv v = p->v;
    /* hold on or removed by others, skip to call try_action */
    if (v.x == 0 || v.y == 0)
        return 1;

    hold_bucket_otherwise_return_0 (p->v, v);
    /* re-enter valid state, skip to call try_action */
    if (p->expire == 0 || p->expire > now) {
        unhold_bucket (p->v, v);
        return 1;
    }

    /* expired,  now remove it */
    if (*seat != mi || !cas (seat, mi, NNULL)) {
        /* failed to remove. let others do it in the future, skip and go next pos */
        unhold_bucket (p->v, v);
        return 0;
    }

    atomic_sub1 (h->ht[idx].ncur);
    void *user_data = p->data;
    memset (p, 0, sizeof (*p));
    add1 (h->stats.expires);
    /* return this hash node for caller re-use */
    /* strict version: if (!node_rtn || !cas(node_rtn, NNULL, mi)) */
    if (node_rtn && *node_rtn == NNULL)
        *node_rtn = mi;
    else
        free_node (h, mi);
    if (h->on_ttl)
        h->on_ttl (user_data, data_rtn);
    return 0;
}

/*Fibonacci number: 16bit->40543, 32bit->2654435769, 64bit->11400714819323198485 */
#if NKEY == 4
#define collect_hash_pos(d, a)  do { register htab_t *pt; i = 0;\
  for (pt = &h->ht[0]; pt < &h->ht[NMHT]; pt++) { \
    a[i++] = &pt->b[d[0] % pt->nb]; \
    a[i++] = &pt->b[d[1] % pt->nb]; \
    a[i++] = &pt->b[d[2] % pt->nb]; \
    a[i++] = &pt->b[d[3] % pt->nb]; \
    for (j = 1; j < NCLUSTER; j++) { \
      a[i++] = &pt->b[(d[3] + j * d[0]) % pt->nb]; \
      a[i++] = &pt->b[(d[0] + j * d[1]) % pt->nb]; \
      a[i++] = &pt->b[(d[1] + j * d[2]) % pt->nb]; \
      a[i++] = &pt->b[(d[2] + j * d[3]) % pt->nb]; \
    } \
  }}while (0)
#elif NKEY == 3
#define collect_hash_pos(d, a)  do { register htab_t *pt; i = 0;\
  for (pt = &h->ht[0]; pt < &h->ht[NMHT]; pt++) { \
    a[i++] = &pt->b[d[0] % pt->nb]; \
    a[i++] = &pt->b[d[1] % pt->nb]; \
    a[i++] = &pt->b[d[2] % pt->nb]; \
    a[i++] = &pt->b[(d[2] + d[0]) % pt->nb]; \
    a[i++] = &pt->b[(d[0] + d[1]) % pt->nb]; \
    a[i++] = &pt->b[(d[1] + d[2]) % pt->nb]; \
    a[i++] = &pt->b[(d[2] - d[0]) % pt->nb]; \
    a[i++] = &pt->b[(d[0] - d[1]) % pt->nb]; \
    a[i++] = &pt->b[(d[1] - d[2]) % pt->nb]; \
  }}while (0)
#endif
/*
#define collect_hash_pos(d, a)  do { register htab_t *pt; j = 0;\
  for (pt = &h->ht[0]; pt < &h->ht[NMHT]; pt++){ \
    for(i = 0; i < NKEY; i++) \
      a[j++] = &pt->b[d[i] % pt->nb]; \
    for(i = 0; i < NKEY; i++) \
      a[j++] = &pt->b[(d[i] + d[(i+1)%NKEY]) % pt->nb]; \
  }}while (0)
*/

#define idx(j) (j < (NCLUSTER*NKEY) ? 0 : 1)
int atomic_hash_add (hash_t *h, const void *kwd, int len, void *data,
                     int init_ttl, hook cbf_dup, void *arg) {
    register unsigned int i, j;
    register nid mi;
    register node_t *p;
    memword nid *a[NSEAT];
    memword union { hv v; nid d[NKEY]; } t;
    nid ni = NNULL;
    unsigned long now = gettime_in_ms();

    if (len > 0)
        h->hash_func (kwd, len, &t);
    else if (len == 0)
        memcpy (&t, kwd, sizeof(t));
    else
        return -3; /* key length not defined */

    collect_hash_pos (t.d, a);

    for (j = 0; j < NSEAT; j++)
        if ((mi = *a[j]) != NNULL && (p = i2p (h->mp, node_t, mi)))
            if (valid_ttl (h, now, p, a[j], mi, idx (j), &ni, NULL))
                if (likely_equal (p->v, t.v))
                    if (try_dup (h, t.v, p, a[j], mi, idx (j), cbf_dup, arg))
                        goto hash_value_exists;

    for (i = h->ht[NMHT].ncur, j = 0; i > 0 && j < MINTAB; j++)
        if ((mi = h->ht[NMHT].b[j]) != NNULL && (p = i2p (h->mp, node_t, mi)) && i--)
            if (valid_ttl (h, now, p, &h->ht[NMHT].b[j], mi, NMHT, &ni, NULL))
                if (likely_equal (p->v, t.v))
                    if (try_dup (h, t.v, p, &h->ht[NMHT].b[j], mi, NMHT, cbf_dup, arg))
                        goto hash_value_exists;

    if (ni == NNULL && (ni = new_node (h)) == NNULL)
        return -2;  /* hash node exhausted */

    p = i2p (h->mp, node_t, ni);
    set_hash_node (p, t.v, data, (init_ttl > 0 ? init_ttl + now : 0));

    for (j = 0; j < NSEAT; j++)
        if (*a[j] == NNULL)
            if (try_add (h, p, a[j], ni, idx (j), arg))
                return 0; /* hash value added */

    if (h->ht[NMHT].ncur < MINTAB)
        for (j = 0; j < MINTAB; j++)
            if (h->ht[NMHT].b[j] == NNULL)
                if (try_add (h, p, &h->ht[NMHT].b[j], ni, NMHT, arg))
                    return 0; /* hash value added */

    memset (p, 0, sizeof (*p));
    free_node (h, ni);
    add1 (h->stats.add_nosit);
    return -1; /* add but fail */

hash_value_exists:
    if (ni != NNULL)
        free_node (h, ni);
    return 1; /* hash value exists */
}


int atomic_hash_get (hash_t *h, const void *kwd, int len, hook cbf, void *arg) {
    memword union { hv v; nid d[NKEY]; } t;
    if (len > 0)
        h->hash_func (kwd, len, &t);
    else if (len == 0)
        memcpy (&t, kwd, sizeof(t));
    else
        return -3; /* key length not defined */

    memword nid *a[NSEAT];
    register unsigned int i,
                          j;
    collect_hash_pos (t.d, a);

    register nid mi;
    register node_t *p;
    unsigned long now = gettime_in_ms();
    for (j = 0; j < NSEAT; j++)
        if ((mi = *a[j]) != NNULL && (p = i2p (h->mp, node_t, mi)))
            if (valid_ttl (h, now, p, a[j], mi, idx (j), NULL, NULL))
                if (likely_equal (p->v, t.v))
                    if (try_get (h, t.v, p, a[j], mi, idx (j), cbf, arg))
                        return 0;

    for (j = i = 0; i < h->ht[NMHT].ncur && j < MINTAB; j++)
        if ((mi = h->ht[NMHT].b[j]) != NNULL && (p = i2p (h->mp, node_t, mi)) && ++i)
            if (valid_ttl (h, now, p, &h->ht[NMHT].b[j], mi, NMHT, NULL, NULL))
                if (likely_equal (p->v, t.v))
                    if (try_get (h, t.v, p, &h->ht[NMHT].b[j], mi, NMHT, cbf, arg))
                        return 0;

    add1 (h->stats.get_nohit);
    return -1;
}

int atomic_hash_del (hash_t *h, const void *kwd, int len, hook cbf, void *arg) {
    memword union { hv v; nid d[NKEY]; } t;
    if (len > 0)
        h->hash_func (kwd, len, &t);
    else if (len == 0)
        memcpy (&t, kwd, sizeof(t));
    else
        return -3; /* key length not defined */

    register unsigned int i,
                          j;
    memword nid *a[NSEAT];
    collect_hash_pos (t.d, a);

    register nid mi;
    register node_t *p;
    unsigned long now = gettime_in_ms();
    i = 0; /* delete all matches */
    for (j = 0; j < NSEAT; j++)
        if ((mi = *a[j]) != NNULL && (p = i2p (h->mp, node_t, mi)))
            if (valid_ttl (h, now, p, a[j], mi, idx (j), NULL, NULL))
                if (likely_equal (p->v, t.v))
                    if (try_del (h, t.v, p, a[j], mi, idx (j), cbf, arg))
                        i++;

    if (h->ht[NMHT].ncur > 0)
        for (j = 0; j < MINTAB; j++)
            if ((mi = h->ht[NMHT].b[j]) != NNULL && (p = i2p (h->mp, node_t, mi)))
                if (valid_ttl (h, now, p, &h->ht[NMHT].b[j], mi, NMHT, NULL, NULL))
                    if (likely_equal (p->v, t.v))
                        if (try_del (h, t.v, p, &h->ht[NMHT].b[j], mi, NMHT, cbf, arg))
                            i++;

    if (i > 0)
        return 0;

    add1 (h->stats.del_nohit);
    return -1;
}
/* -------------------- -------------------- -------------------- -------------------- -------------------- */


/* - Debug/Test functions - */
void (*atomic_hash_debug_get_hash_func(hash_t *h))(const void *key, size_t len, void *r) {
    return h->hash_func;
}

void *atomic_hash_debug_get_teststr(hash_t *h) {
    return h->teststr;
}

void atomic_hash_debug_set_teststr(hash_t *h, void *teststr) {
    h->teststr = teststr;
}

unsigned long atomic_hash_debug_get_teststr_num(hash_t *h) {
    return h->teststr_num;
}

void atomic_hash_debug_set_teststr_num(hash_t *h, unsigned long teststr_num) {
    h->teststr_num = teststr_num;
}
