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
 * - Revise:
 *   - Replaced `__asm__("pause")` w/ `usleep(1)` + `#include <unistd.h>` (for more portable code)
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
/* - Available hash functions - */
#define CITY3HASH_128   1
#define MD5HASH         2
// #define MPQ3HASH        3
// #define NEWHASH         4
// #define MURMUR3HASH_128 5

#if HASH_FUNCTION == CITY3HASH_128 || HASH_FUNCTION == MD5HASH // || HASH_FUNCTION == MURMUR3HASH_128
#  define NKEY 4
#  define NCMP 2

typedef uint64_t hvu_t;
typedef struct {
    hvu_t x,
          y;
} hv_t;

// #elif HASH_FUNCTION == MPQ3HASH || HASH_FUNCTION == NEWHASH
// #  define NKEY 3
// #  define NCMP 3
//
// typedef uint32_t hvu_t;
// typedef struct {
//     hvu_t x,
//           y,
//           z;
// } hv_t;
//
#endif /* HASH_FUNCTION */


/* - Misc. - */
#define NNULL     0xFFFFFFFF

#define NMHT      2
#define NCLUSTER  4
#define NSEAT     (NMHT * NKEY * NCLUSTER)
#define MINTAB    64
#define MAXTAB    NNULL
#define COLLISION 1000 /* 0.01 ~> avg 25 in seat */
#define MAXSPIN   (1 << 20) /* 2^20 loops 40ms with pause + sched_yield on xeon E5645 */


/* -- Types -- */
#define SHARED __attribute__((aligned(64)))

typedef uint32_t nid_t;

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
//     unsigned long xadd,
//                   xget,
//                   xdel,
//                   nexp;
// } hc_t; /* 64-bytes cache line */

typedef struct {
    void **ba;
    SHARED nid_t mask,
                 shift; /* used for I2P() only */
    SHARED nid_t max_blocks,
                 blk_node_num,
                 node_size,
                 blk_size;
    volatile nid_t curr_blocks;
} mem_pool_t;

typedef union {
    struct {
        nid_t mi,
              rfn;
    } cas;
    uint64_t all;
} cas_t;

typedef struct {
    volatile hv_t v;
    unsigned long expiry_in_ms; /* expire in ms # of gettimeofday(), 0 = never */
    void *data;
} node_t;

typedef struct {
    nid_t *b;             /* hash tab (int array as memory index */
    unsigned long ncur,
                  n,
                  nb;  /* nb: buckets #, set by n * r */
    unsigned long nadd,
                  ndup,
                  nget,
                  ndel;
} htab_t;

struct hash {
/* hash function */
    SHARED void (*hash_func)(const void *key, size_t len, void *r);

/* hook func to deal w/ user data in safe zone */
    SHARED hook_t cb_on_ttl,
                  cb_on_add,
                  cb_on_dup,
                  cb_on_get,
                  cb_on_del;
    SHARED volatile cas_t freelist; /* free hash node list */
    SHARED htab_t ht[3]; /* ht[2] for array [MINTAB] */
    SHARED hstats_t stats;
    SHARED void **hp;
    SHARED mem_pool_t *mp;
    SHARED unsigned long reset_expire; /* if > 0, reset node->expire */
    SHARED unsigned long nmht,
                         ncmp;
    SHARED unsigned long nkey,
                         npos,
                         nseat; /* nseat = 2*npos = 4*nkey */



    SHARED void *teststr;
    SHARED unsigned long teststr_num;
};


/* -- 'Aliases' / 'Fct-like' macros -- */
#define MEMWORD                 __attribute__((aligned(sizeof(void *))))

#define ATOMIC_ADD1(V)          __sync_fetch_and_add(&(V), 1)
#define ATOMIC_SUB1(V)          __sync_fetch_and_sub(&(V), 1)
#define ADD1(V)                 __sync_fetch_and_add(&(V), 1)
#define CAS(DST, OLD, NEW)      __sync_bool_compare_and_swap((DST), (OLD), (NEW))

#define IP(MP, TYPE, I)         (((TYPE *)((MP)->ba[(I) >> (MP)->shift]))[(I) & (MP)->mask])
#define I2P(MP, TYPE, I)        ((I) == NNULL ? NULL : &(IP(MP, TYPE, I)))
//#define UNHOLD_BUCKET(HV, V)    do { if ((HV).y && !(HV).x) (HV).x = (V).x; } while(0)
#define UNHOLD_BUCKET(HV, V)    while ((HV).y && !CAS (&(HV).x, 0, (V).x))
#define HOLD_BUCKET_OTHERWISE_RETURN_0(HMAP, HV, V) do { unsigned long __l = MAXSPIN; \
          while (!CAS(&(HV).x, (V).x, 0)) { /* when CAS fails */ \
            if ((HV).x != (V).x) return 0; /* already released or reused */\
            while ((HV).x == 0) { /* wait for unhold */ \
              if ((HV).y == 0) return 0; /* no unhold but released */ \
              if (--__l == 0) { ADD1 ((HMAP)->stats.escapes); return 0; } /* give up */ \
              if (__l & 0x0f) usleep(1); else sched_yield(); /* original: __asm__("pause") */ \
          }} \
          if ((HV).y != (V).y || (HV).y == 0) { UNHOLD_BUCKET (HV, V); return 0; } \
          } while (0)


/* -- Debugging macros -- */
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
    unsigned int pwr2_max_nodes;
    for (pwr2_max_nodes = 0; (1u << pwr2_max_nodes) < max_nodes; pwr2_max_nodes++);
    if (pwr2_max_nodes == 0 || pwr2_max_nodes > 32) /* auto resize for exception, use 1MB as mem index and 1MB block size*/
        pwr2_max_nodes = 32;

    unsigned int pwr2_node_size;
    for (pwr2_node_size = 0; (1u << pwr2_node_size) < node_size; pwr2_node_size++);
    if ((1u << pwr2_node_size) != node_size || pwr2_node_size < 5 || pwr2_node_size > 12) {
        PRINT_DEBUG_MSG("node_size should be N power of 2, 5 <= N <= 12(4KB page)");
        return NULL;
    }

#define PW2_MAX_BLK_PTR 9   /* hard code one 4K-page for max 512 block pointers */
#define PW2_MIN_BLK_SIZ 12  /* 2^12 = 4K page size */
    unsigned int pwr2_total_size = pwr2_max_nodes + pwr2_node_size,
                 pwr2_block_size = (pwr2_total_size <= PW2_MAX_BLK_PTR + PW2_MIN_BLK_SIZ) ? (PW2_MIN_BLK_SIZ) : (pwr2_total_size - PW2_MAX_BLK_PTR);

    mem_pool_t *pmp;
    if (posix_memalign ((void **) (&pmp), 64, sizeof (*pmp)))
        return NULL;
    memset (pmp, 0, sizeof (*pmp));

    pmp->max_blocks =   (nid_t)(1 << (PW2_MAX_BLK_PTR));
    pmp->node_size =    (nid_t)(1 << pwr2_node_size);
    pmp->blk_size =     (nid_t)(1 << pwr2_block_size);
    pmp->blk_node_num = (nid_t)(1 << (pwr2_block_size - pwr2_node_size));
    pmp->shift =        (nid_t)pwr2_block_size - pwr2_node_size;
    pmp->mask =         (nid_t)((1 << pmp->shift) - 1);
    pmp->curr_blocks =  0;

    if (!posix_memalign ((void **) (&pmp->ba), 64, pmp->max_blocks * sizeof (*pmp->ba))) {
        memset (pmp->ba, 0, pmp->max_blocks * sizeof (*pmp->ba));
        return pmp;
    }

    free (pmp);
    return NULL;
}

static int destroy_mem_pool (mem_pool_t *pmp) {
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

static inline nid_t *new_mem_block (mem_pool_t *pmp, volatile cas_t *recv_queue) {

    void *p = calloc (pmp->blk_node_num, pmp->node_size);
    if (!pmp || !p)
        return NULL;

    nid_t i;
    for (i = pmp->curr_blocks; i < pmp->max_blocks; i++)
        if (CAS (&pmp->ba[i], NULL, p)) {
            ATOMIC_ADD1 (pmp->curr_blocks);
            break;
        }

    if (i == pmp->max_blocks) {
        free (p);
        return NULL;
    }

    nid_t sz =   pmp->node_size,
          m =    pmp->mask,
          head = i * (m + 1);
    for (i = 0; i < m; i++)
        *(nid_t *) ((char *)p + i * sz) = head + i + 1;

    MEMWORD cas_t *pn = (cas_t *) ((char *)p + m * sz);
    pn->cas.mi =  NNULL;
    pn->cas.rfn = 0;
    MEMWORD cas_t n,
                  x;
    x.cas.mi = head;
    do {
        n.all = recv_queue->all;
        pn->cas.mi = n.cas.mi;
        x.cas.rfn = n.cas.rfn + 1;
    } while (!CAS (&recv_queue->all, n.all, x.all));

    return (nid_t *) ((char *)p + m * sz);
}

/* Default hooks */
static int default_cb_reset_ttl (void *hash_data, void *return_data) {
    if (return_data)
        *((void **)return_data) = hash_data;
    return HOOK_SET_TTL_TO_DEFAULT;
}

/* define your own func to return different ttl or removal instructions */
static int default_cb_ttl_no_change (void *hash_data, void *return_data) {
    if (return_data)
        *((void **)return_data) = hash_data;
    return HOOK_DONT_CHANGE_TTL;
}

static int default_cb_remove_node (void *hash_data, void *return_data) {
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


    hash_t *hmap;
    if (posix_memalign ((void **) (&hmap), 64, sizeof (*hmap)))
        return NULL;
    memset (hmap, 0, sizeof (*hmap));

#if HASH_FUNCTION == MD5HASH
#  include "hash_functions/hash_md5.h"
    hmap->hash_func = md5hash;
#elif HASH_FUNCTION == CITY3HASH_128
#  include "hash_functions/hash_city.h"
    hmap->hash_func = cityhash_128;
// #elif HASH_FUNCTION == MPQ3HASH
// #  include "hash_functions/hash_mpq.h"
//   uint32_t ct[0x500];
//   init_crypt_table (ct);
//   hmap->hash_func = mpq3hash;
// #elif HASH_FUNCTION == NEWHASH
// #  include "hash_functions/hash_newhash.h"
//   hmap->hash_func = newhash;
// #elif HASH_FUNCTION == MURMUR3HASH_128
// #  include "hash_functions/hash_murmur3.h"
//   hmap->hash_func = MurmurHash3_x64_128;
#else
#  error "atomic_hash: No hash function has been selected!"
#endif

    hmap->cb_on_ttl =       default_cb_remove_node;
    hmap->cb_on_del =       default_cb_remove_node;
    hmap->cb_on_add =       default_cb_ttl_no_change;
    hmap->cb_on_get =       default_cb_ttl_no_change;
    hmap->cb_on_dup =       default_cb_reset_ttl;

    hmap->reset_expire =    reset_ttl;
    hmap->nmht =            NMHT;
    hmap->ncmp =            NCMP;
    hmap->nkey =            NKEY;   /* uint32_t # of hash function's output */
    hmap->npos =            hmap->nkey * NCLUSTER; /* pos # in one hash table */
    hmap->nseat =           hmap->npos * hmap->nmht; /* pos # in all hash tables */
    hmap->freelist.cas.mi = NNULL;


    htab_t *ht1 = &hmap->ht[0],            /* bucket array 1, 2 and collision array */
           *ht2 = &hmap->ht[1],
           *at1 = &hmap->ht[NMHT];

/* n1 -> n2 -> 1/tuning
 * nb1 = n1 * r1, r1 = ((n1+2)/tuning/K^2)^(K^2 - 1)
 * nb2 = n2 * r2 == nb1 / K == ((n2+2)/tuning/K))^(K - 1)
 */
    PRINT_DEBUG_MSG("init bucket array 1:\n");
    const double collision = COLLISION; /* collision control, larger is better */
    double K = hmap->npos + 1;
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

    hmap->mp = create_mem_pool (max_nodes, sizeof (node_t));
//  hmap->mp = old_create_mem_pool (ht1->nb + ht2->nb + at1->nb, sizeof (node_t), max_blocks);
    PRINT_DEBUG_MSG("shift=%u; mask=%u\n", hmap->mp->shift, hmap->mp->mask);
    PRINT_DEBUG_MSG("mem_blocks:\t%u/%u, %ux%u bytes, %u bytes per block\n", hmap->mp->curr_blocks, hmap->mp->max_blocks,
                    hmap->mp->blk_node_num, hmap->mp->node_size, hmap->mp->blk_size);
    if (!hmap->mp)
        goto calloc_exit;

    hmap->stats.max_nodes = hmap->mp->max_blocks * hmap->mp->blk_node_num;
    hmap->stats.mem_htabs = ((ht1->nb + ht2->nb) * sizeof (nid_t)) >> 10;
    hmap->stats.mem_nodes = (hmap->stats.max_nodes * hmap->mp->node_size) >> 10;
    return hmap;


calloc_exit:
    for (unsigned long j = 0; j < hmap->nmht; j++)
        if (hmap->ht[j].b)
            free (hmap->ht[j].b);
    destroy_mem_pool (hmap->mp);
    free (hmap);

    return NULL;
}

int atomic_hash_stats (hash_t *hmap, unsigned long escaped_milliseconds) {
    const hstats_t *t = &hmap->stats;
    const htab_t *ht1 = &hmap->ht[0],
                 *ht2 = &hmap->ht[1];
    mem_pool_t *m = hmap->mp;
    double d =         1024.0,
           blk_in_kB = m->blk_size / d,
           mem =       m->curr_blocks * blk_in_kB;
    const char* const log_delim = "    ";

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
    for (unsigned long j = 0; j <= NMHT && (p = &hmap->ht[j]); j++) {
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

int atomic_hash_destroy (hash_t *hmap) {
    if (!hmap)
        return -1;

    for (unsigned int j = 0; j < hmap->nmht; j++)
        free (hmap->ht[j].b);
    destroy_mem_pool (hmap->mp);
    free (hmap);

    return 0;
}

void atomic_hash_register_hooks(hash_t *hmap,
                                hook_t cb_on_ttl, hook_t cb_on_add, hook_t cb_on_dup, hook_t cb_on_get, hook_t cb_on_del) {
    if (cb_on_ttl) {
        hmap->cb_on_ttl = cb_on_ttl;
    }
    if (cb_on_add) {
        hmap->cb_on_add = cb_on_add;
    }
    if (cb_on_dup) {
        hmap->cb_on_dup = cb_on_dup;
    }
    if (cb_on_get) {
        hmap->cb_on_get = cb_on_get;
    }
    if (cb_on_del) {
        hmap->cb_on_del = cb_on_del;
    }
}


/* -------------------- -------------------- -------------------- -------------------- -------------------- */
static inline nid_t new_node (hash_t *hmap) {
    MEMWORD cas_t n,
                  m;
    while (hmap->freelist.cas.mi != NNULL || new_mem_block (hmap->mp, &hmap->freelist)) {
        n.all = hmap->freelist.all;
        if (n.cas.mi == NNULL)
            continue;

        m.cas.mi = ((cas_t *) (I2P (hmap->mp, node_t, n.cas.mi)))->cas.mi;
        m.cas.rfn = n.cas.rfn + 1;

        if (CAS (&hmap->freelist.all, n.all, m.all))
            return n.cas.mi;
    }

    ADD1 (hmap->stats.add_nomem);
    return NNULL;
}

static inline void free_node (hash_t *hmap, nid_t mi) {
    cas_t *p = (cas_t *) (I2P (hmap->mp, node_t, mi));
    p->cas.rfn = 0;

    MEMWORD cas_t n,
                  m;
    m.cas.mi = mi;
    do {
        n.all = hmap->freelist.all;
        m.cas.rfn = n.cas.rfn + 1;
        p->cas.mi = n.cas.mi;
    } while (!CAS (&hmap->freelist.all, n.all, m.all));
}

static inline void set_hash_node (node_t *p, hv_t v, void *data, unsigned long expire) {
    p->v =      v;
    p->expiry_in_ms = expire;
    p->data =   data;
}

static inline int likely_equal (hv_t w, hv_t v) {
    return w.y == v.y;
}

/* only called in atomic_hash_get */
static inline int try_get (hash_t *hmap, hv_t v, node_t *p, nid_t *seat, nid_t mi, int idx, hook_t cb_fct, void *rtn) {
    HOLD_BUCKET_OTHERWISE_RETURN_0 (hmap, p->v, v);
    if (*seat != mi) {
        UNHOLD_BUCKET (p->v, v);
        return 0;
    }

    int result = cb_fct ? cb_fct (p->data, rtn) : hmap->cb_on_get (p->data, rtn);
    if (result == HOOK_REMOVE_HASH_NODE) {
        if (CAS (seat, mi, NNULL))
            ATOMIC_SUB1 (hmap->ht[idx].ncur);
        memset (p, 0, sizeof (*p));
        ADD1 (hmap->ht[idx].nget);
        free_node (hmap, mi);
        return 1;
    }
    if (result == HOOK_SET_TTL_TO_DEFAULT)
        result = hmap->reset_expire;
    if (p->expiry_in_ms > 0 && result > 0)
        p->expiry_in_ms = result + gettime_in_ms();
    UNHOLD_BUCKET (p->v, v);
    ADD1 (hmap->ht[idx].nget);
    return 1;
}

/* only called in atomic_hash_add */
static inline int try_dup (hash_t *hmap, hv_t v, node_t *p, nid_t *seat, nid_t mi, int idx, hook_t cb_fct, void *rtn) {
    HOLD_BUCKET_OTHERWISE_RETURN_0 (hmap, p->v, v);
    if (*seat != mi) {
        UNHOLD_BUCKET (p->v, v);
        return 0;
    }

    int result = cb_fct ? cb_fct (p->data, rtn) : hmap->cb_on_dup (p->data, rtn);
    if (result == HOOK_REMOVE_HASH_NODE) {
        if (CAS (seat, mi, NNULL))
            ATOMIC_SUB1 (hmap->ht[idx].ncur);
        memset (p, 0, sizeof (*p));
        ADD1 (hmap->ht[idx].ndup);
        free_node (hmap, mi);
        return 1;
    }
    if (result == HOOK_SET_TTL_TO_DEFAULT)
        result = hmap->reset_expire;
    if (p->expiry_in_ms > 0 && result > 0)
        p->expiry_in_ms = result + gettime_in_ms();
    UNHOLD_BUCKET (p->v, v);
    ADD1 (hmap->ht[idx].ndup);
    return 1;
}

/* only called in `atomic_hash_add` */
static inline int try_add (hash_t *hmap, node_t *p, nid_t *seat, nid_t mi, int idx, void *rtn) {
    hvu_t x = p->v.x;
    p->v.x = 0;
    if (!CAS (seat, NNULL, mi)) {
        p->v.x = x;
        return 0; /* other thread wins, caller to retry other seats */
    }

    ATOMIC_ADD1 (hmap->ht[idx].ncur);
    int result = hmap->cb_on_add (p->data, rtn);
    if (result == HOOK_REMOVE_HASH_NODE) {
        if (CAS (seat, mi, NNULL))
            ATOMIC_SUB1 (hmap->ht[idx].ncur);
        memset (p, 0, sizeof (*p));
        free_node (hmap, mi);
        return 1; /* abort adding this node */
    }
    if (result == HOOK_SET_TTL_TO_DEFAULT)
        result = hmap->reset_expire;
    if (p->expiry_in_ms > 0 && result > 0)
        p->expiry_in_ms = result + gettime_in_ms();
    p->v.x = x;
    ADD1 (hmap->ht[idx].nadd);
    return 1;
}

/* only called in atomic_hash_del */
static inline int try_del (hash_t *hmap, hv_t v, node_t *p, nid_t *seat, nid_t mi, int idx, hook_t cb_fct, void *rtn) {
    HOLD_BUCKET_OTHERWISE_RETURN_0 (hmap, p->v, v);
    if (*seat != mi || !CAS (seat, mi, NNULL)) {
        UNHOLD_BUCKET (p->v, v);
        return 0;
    }

    ATOMIC_SUB1 (hmap->ht[idx].ncur);
    void *user_data = p->data;
    memset (p, 0, sizeof (*p));
    ADD1 (hmap->ht[idx].ndel);
    free_node (hmap, mi);
    if (cb_fct)
        cb_fct (user_data, rtn);
    else
        hmap->cb_on_del (user_data, rtn);
    return 1;
}

static inline int valid_ttl (hash_t *hmap, unsigned long cur_time_in_ms, node_t *p, nid_t *seat, nid_t mi,
                             int idx, nid_t *node_rtn, void *data_rtn) {
    unsigned long expire = p->expiry_in_ms;
    /* valid state, quickly skip to call try_action. */
    if (expire == 0 || expire > cur_time_in_ms)
        return 1;

    hv_t v = p->v;
    /* hold on or removed by others, skip to call try_action */
    if (v.x == 0 || v.y == 0)
        return 1;

    HOLD_BUCKET_OTHERWISE_RETURN_0 (hmap, p->v, v);
    /* re-enter valid state, skip to call try_action */
    if (p->expiry_in_ms == 0 || p->expiry_in_ms > cur_time_in_ms) {
        UNHOLD_BUCKET (p->v, v);
        return 1;
    }

    /* expired,  cur_time_in_ms remove it */
    if (*seat != mi || !CAS (seat, mi, NNULL)) {
        /* failed to remove. let others do it in the future, skip and go next pos */
        UNHOLD_BUCKET (p->v, v);
        return 0;
    }

    ATOMIC_SUB1 (hmap->ht[idx].ncur);
    void *user_data = p->data;
    memset (p, 0, sizeof (*p));
    ADD1 (hmap->stats.expires);
    /* return this hash node for caller re-use */
    /* strict version: if (!node_rtn || !CAS(node_rtn, NNULL, mi)) */
    if (node_rtn && *node_rtn == NNULL)
        *node_rtn = mi;
    else
        free_node (hmap, mi);
    if (hmap->cb_on_ttl)
        hmap->cb_on_ttl (user_data, data_rtn);

    return 0;
}

/*Fibonacci number: 16bit->40543, 32bit->2654435769, 64bit->11400714819323198485 */
#if NKEY == 4
#define COLLECT_HASH_POS(HMAP, D, A)  do { register unsigned int __i = 0;\
    for (register htab_t *pt = &(HMAP)->ht[0]; pt < &(HMAP)->ht[NMHT]; pt++) { \
        (A)[__i++] = &pt->b[(D)[0] % pt->nb]; \
        (A)[__i++] = &pt->b[(D)[1] % pt->nb]; \
        (A)[__i++] = &pt->b[(D)[2] % pt->nb]; \
        (A)[__i++] = &pt->b[(D)[3] % pt->nb]; \
        for (register unsigned int __j = 1; __j < NCLUSTER; __j++) { \
            (A)[__i++] = &pt->b[((D)[3] + __j * (D)[0]) % pt->nb]; \
            (A)[__i++] = &pt->b[((D)[0] + __j * (D)[1]) % pt->nb]; \
            (A)[__i++] = &pt->b[((D)[1] + __j * (D)[2]) % pt->nb]; \
            (A)[__i++] = &pt->b[((D)[2] + __j * (D)[3]) % pt->nb]; \
        } \
    }}while (0)

#elif NKEY == 3
#define COLLECT_HASH_POS(HMAP, D, A)  do { register unsigned int __i = 0;\
    for (register htab_t *pt = &(HMAP)->ht[0]; pt < &(HMAP)->ht[NMHT]; pt++) { \
        (A)[__i++] = &pt->b[(D)[0] % pt->nb]; \
        (A)[__i++] = &pt->b[(D)[1] % pt->nb]; \
        (A)[__i++] = &pt->b[(D)[2] % pt->nb]; \
        (A)[__i++] = &pt->b[((D)[2] + (D)[0]) % pt->nb]; \
        (A)[__i++] = &pt->b[((D)[0] + (D)[1]) % pt->nb]; \
        (A)[__i++] = &pt->b[((D)[1] + (D)[2]) % pt->nb]; \
        (A)[__i++] = &pt->b[((D)[2] - (D)[0]) % pt->nb]; \
        (A)[__i++] = &pt->b[((D)[0] - (D)[1]) % pt->nb]; \
        (A)[__i++] = &pt->b[((D)[1] - (D)[2]) % pt->nb]; \
    }}while (0)

#endif /* NKEY */

#define IDX(J) (J < (NCLUSTER * NKEY) ? 0 : 1)

int atomic_hash_add (hash_t *hmap, const void *kwd, int len, void *data,
                     int init_ttl, hook_t cb_fct_dup, void *arg) {
    MEMWORD union { hv_t v; nid_t d[NKEY]; } t;
    unsigned long cur_time_in_ms = gettime_in_ms();
    if (len > 0)
        hmap->hash_func (kwd, len, &t);
    else if (len == 0)
        memcpy (&t, kwd, sizeof(t));
    else
        return -3; /* key length not defined */

    MEMWORD nid_t *a[NSEAT];
    COLLECT_HASH_POS (hmap, t.d, a);

    nid_t ni = NNULL;
    register nid_t mi;
    register node_t *p;
    for (register unsigned int j = 0; j < NSEAT; j++)
        if ((mi = *a[j]) != NNULL && (p = I2P (hmap->mp, node_t, mi)))
            if (valid_ttl (hmap, cur_time_in_ms, p, a[j], mi, IDX (j), &ni, NULL))
                if (likely_equal (p->v, t.v))
                    if (try_dup (hmap, t.v, p, a[j], mi, IDX (j), cb_fct_dup, arg))
                        goto hash_value_exists;

    for (register unsigned int i = hmap->ht[NMHT].ncur,
                               j = 0; i > 0 && j < MINTAB; j++)
        if ((mi = hmap->ht[NMHT].b[j]) != NNULL && (p = I2P (hmap->mp, node_t, mi)) && i--)
            if (valid_ttl (hmap, cur_time_in_ms, p, &hmap->ht[NMHT].b[j], mi, NMHT, &ni, NULL))
                if (likely_equal (p->v, t.v))
                    if (try_dup (hmap, t.v, p, &hmap->ht[NMHT].b[j], mi, NMHT, cb_fct_dup, arg))
                        goto hash_value_exists;

    if (ni == NNULL && (ni = new_node (hmap)) == NNULL)
        return -2;  /* hash node exhausted */

    p = I2P (hmap->mp, node_t, ni);
    set_hash_node (p, t.v, data, (init_ttl > 0 ? init_ttl + cur_time_in_ms : 0));

    for (register unsigned int j = 0; j < NSEAT; j++)
        if (*a[j] == NNULL)
            if (try_add (hmap, p, a[j], ni, IDX (j), arg))
                return 0; /* hash value added */

    if (hmap->ht[NMHT].ncur < MINTAB)
        for (register unsigned int j = 0; j < MINTAB; j++)
            if (hmap->ht[NMHT].b[j] == NNULL)
                if (try_add (hmap, p, &hmap->ht[NMHT].b[j], ni, NMHT, arg))
                    return 0; /* hash value added */

    memset (p, 0, sizeof (*p));
    free_node (hmap, ni);
    ADD1 (hmap->stats.add_nosit);
    return -1; /* add but fail */

hash_value_exists:
    if (ni != NNULL)
        free_node (hmap, ni);
    return 1; /* hash value exists */
}


int atomic_hash_get (hash_t *hmap, const void *kwd, int len, hook_t cb_fct, void *arg) {
    MEMWORD union { hv_t v; nid_t d[NKEY]; } t;
    unsigned long cur_time_in_ms = gettime_in_ms();
    if (len > 0)
        hmap->hash_func (kwd, len, &t);
    else if (len == 0)
        memcpy (&t, kwd, sizeof(t));
    else
        return -3; /* key length not defined */

    MEMWORD nid_t *a[NSEAT];
    COLLECT_HASH_POS (hmap, t.d, a);


    register nid_t mi;
    register node_t *p;
    for (register unsigned int j = 0; j < NSEAT; j++)
        if ((mi = *a[j]) != NNULL && (p = I2P (hmap->mp, node_t, mi)))
            if (valid_ttl (hmap, cur_time_in_ms, p, a[j], mi, IDX (j), NULL, NULL))
                if (likely_equal (p->v, t.v))
                    if (try_get (hmap, t.v, p, a[j], mi, IDX (j), cb_fct, arg))
                        return 0;

    for (register unsigned int j = 0,
                               i = 0; i < hmap->ht[NMHT].ncur && j < MINTAB; j++)
        if ((mi = hmap->ht[NMHT].b[j]) != NNULL && (p = I2P (hmap->mp, node_t, mi)) && ++i)
            if (valid_ttl (hmap, cur_time_in_ms, p, &hmap->ht[NMHT].b[j], mi, NMHT, NULL, NULL))
                if (likely_equal (p->v, t.v))
                    if (try_get (hmap, t.v, p, &hmap->ht[NMHT].b[j], mi, NMHT, cb_fct, arg))
                        return 0;

    ADD1 (hmap->stats.get_nohit);
    return -1;
}

int atomic_hash_del (hash_t *hmap, const void *kwd, int len, hook_t cbf, void *arg) {
    MEMWORD union { hv_t v; nid_t d[NKEY]; } t;
    if (len > 0)
        hmap->hash_func (kwd, len, &t);
    else if (len == 0)
        memcpy (&t, kwd, sizeof(t));
    else
        return -3; /* key length not defined */

    MEMWORD nid_t *a[NSEAT];
    COLLECT_HASH_POS (hmap, t.d, a);


    register nid_t mi;
    register node_t *p;
    unsigned long cur_time_in_ms = gettime_in_ms();
    register unsigned int del_matches = 0; /* delete all matches */
    for (register unsigned int j = 0; j < NSEAT; j++)
        if ((mi = *a[j]) != NNULL && (p = I2P (hmap->mp, node_t, mi)))
            if (valid_ttl (hmap, cur_time_in_ms, p, a[j], mi, IDX (j), NULL, NULL))
                if (likely_equal (p->v, t.v))
                    if (try_del (hmap, t.v, p, a[j], mi, IDX (j), cbf, arg))
                        del_matches++;

    if (hmap->ht[NMHT].ncur > 0)
        for (register unsigned int j = 0; j < MINTAB; j++)
            if ((mi = hmap->ht[NMHT].b[j]) != NNULL && (p = I2P (hmap->mp, node_t, mi)))
                if (valid_ttl (hmap, cur_time_in_ms, p, &hmap->ht[NMHT].b[j], mi, NMHT, NULL, NULL))
                    if (likely_equal (p->v, t.v))
                        if (try_del (hmap, t.v, p, &hmap->ht[NMHT].b[j], mi, NMHT, cbf, arg))
                            del_matches++;

    if (del_matches > 0)
        return 0;

    ADD1 (hmap->stats.del_nohit);
    return -1;
}
/* -------------------- -------------------- -------------------- -------------------- -------------------- */


/* - Test functions - */
void (*atomic_hash_debug_get_hash_func(hash_t *hmap))(const void *key, size_t len, void *r) {
    return hmap->hash_func;
}

void *atomic_hash_debug_get_teststr(hash_t *hmap) {
    return hmap->teststr;
}

void atomic_hash_debug_set_teststr(hash_t *hmap, void *teststr) {
    hmap->teststr = teststr;
}

unsigned long atomic_hash_debug_get_teststr_num(hash_t *hmap) {
    return hmap->teststr_num;
}

void atomic_hash_debug_set_teststr_num(hash_t *hmap, unsigned long teststr_num) {
    hmap->teststr_num = teststr_num;
}
