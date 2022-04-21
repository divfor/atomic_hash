/*
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
    volatile nid_t cur_blocks;
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
    unsigned long expiry_in_ms; /* expire in ms # of `gettimeofday`(2), 0 = never */
    void *data;
} node_t;

typedef struct {
    nid_t *b;             /* hash tab (int array as memory index) */
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
    SHARED mem_pool_t *mpool;
    SHARED unsigned long node_expiry_in_ms_reset_val; /* if > 0, reset node->expire */
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
#define I2P(MP, TYPE, I)        (NNULL == (I) ? NULL : &(IP(MP, TYPE, I)))
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
static mem_pool_t *mem_pool_create (unsigned int max_nodes, unsigned int node_size) {
    unsigned int pwr2_max_nodes;
    for (pwr2_max_nodes = 0; (1u << pwr2_max_nodes) < max_nodes; pwr2_max_nodes++);
    if (pwr2_max_nodes == 0 || pwr2_max_nodes > 32) { /* auto resize for exception, use 1MB as mem index and 1MB block size*/
        pwr2_max_nodes = 32;
    }

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

    mem_pool_t *mpool;
    if (posix_memalign ((void **) (&mpool), 64, sizeof (*mpool))) {
        return NULL;
    }
    memset (mpool, 0, sizeof (*mpool));

    mpool->max_blocks =   (nid_t)(1 << (PW2_MAX_BLK_PTR));
    mpool->node_size =    (nid_t)(1 << pwr2_node_size);
    mpool->blk_size =     (nid_t)(1 << pwr2_block_size);
    mpool->blk_node_num = (nid_t)(1 << (pwr2_block_size - pwr2_node_size));
    mpool->shift =        (nid_t)pwr2_block_size - pwr2_node_size;
    mpool->mask =         (nid_t)((1 << mpool->shift) - 1);
    mpool->cur_blocks =   0;

    if (!posix_memalign ((void **) (&mpool->ba), 64, mpool->max_blocks * sizeof (*mpool->ba))) {
        memset (mpool->ba, 0, mpool->max_blocks * sizeof (*mpool->ba));
        return mpool;
    }

    free (mpool);
    return NULL;
}

static int mem_pool_destroy (mem_pool_t *mpool) {
    if (!mpool) {
        return -1;
    }

    for (unsigned long i = 0; i < mpool->max_blocks; i++) {
        if (mpool->ba[i]) {
            free (mpool->ba[i]);
            mpool->ba[i] = NULL;
            mpool->cur_blocks--;
        }
    }

    free (mpool->ba);
    mpool->ba = NULL;
    free (mpool);

    return 0;
}

static inline nid_t *mem_block_new (mem_pool_t *mpool, volatile cas_t *recv_queue) {

    void *p = calloc (mpool->blk_node_num, mpool->node_size);
    if (!mpool || !p) {
        return NULL;
    }

    nid_t cur_block;
    for (cur_block = mpool->cur_blocks; cur_block < mpool->max_blocks; cur_block++) {
        if (CAS (&mpool->ba[cur_block], NULL, p)) {
            ATOMIC_ADD1 (mpool->cur_blocks);
            break;
        }
    }

    if (cur_block == mpool->max_blocks) {
        free (p);
        return NULL;
    }

    nid_t mpool_node_size = mpool->node_size,
          mpool_mask = mpool->mask,
          head = cur_block * (mpool_mask + 1);
    for (nid_t i = 0; i < mpool_mask; i++) {
        *(nid_t *) ((char *)p + i * mpool_node_size) = head + i + 1;
    }

    MEMWORD cas_t *pn = (cas_t *) ((char *)p + mpool_mask * mpool_node_size);
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

    return (nid_t *) ((char *)p + mpool_mask * mpool_node_size);
}

/* Default hooks */
static int default_cb_reset_ttl (void *hash_data, void *return_data) {
    if (return_data) {
        *((void **)return_data) = hash_data;
    }
    return HOOK_RESET_TTL;
}

/* define your own func to return different ttl or removal instructions */
static int default_cb_ttl_no_change (void *hash_data, void *return_data) {
    if (return_data) {
        *((void **)return_data) = hash_data;
    }
    return HOOK_DONT_CHANGE_TTL;
}

static int default_cb_remove_node (void *hash_data, void *return_data) {
    if (return_data) {
        *((void **)return_data) = hash_data;
    }
    return HOOK_REMOVE_HASH_NODE;
}

static int htab_init (htab_t *ht, unsigned long num, double ratio) {
    unsigned long i,
                  nb = num * ratio;
    for (i = 134217728; nb > i; i *= 2);

//  nb = (nb >= 134217728) ? i : nb; // improve folding for more than 1/32 of MAXTAB (2^32)
    ht->nb = (i > MAXTAB) ? MAXTAB : ((nb < MINTAB) ? MINTAB : nb);
    ht->n =  num; //if 3rd tab: n <- 0, nb <- MINTAB, r <- COLLISION

    if (!(ht->b = calloc (ht->nb, sizeof (*ht->b)))) {
        return -1;
    }

    for (i = 0; i < ht->nb; i++) {
        ht->b[i] = NNULL;
    }

    PRINT_DEBUG_MSG("expected nb[%lu] = n[%lu] * r[%f]\n", (unsigned long) (num * ratio),
                    num, ratio);
    PRINT_DEBUG_MSG("actual   nb[%lu] = n[%lu] * r[%f]\n", ht->nb, ht->n,
                    (0 == ht->n ? ratio : ht->nb * 1.0 / ht->n));

    return 0;
}


hash_t *atomic_hash_create (unsigned int max_nodes, int reset_ttl) {
    if (max_nodes < 2 || max_nodes > MAXTAB) {
        PRINT_DEBUG_MSG("max_nodes range: 2 ~ %lu\n", (unsigned long) MAXTAB);
        return NULL;
    }


    hash_t *hmap;
    if (posix_memalign ((void **) (&hmap), 64, sizeof (*hmap))) {
        return NULL;
    }
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

    hmap->node_expiry_in_ms_reset_val = reset_ttl;
    hmap->nmht =            NMHT;
    hmap->ncmp =            NCMP;
    hmap->nkey =            NKEY;   /* uint32_t # of hash function's output */
    hmap->npos =            hmap->nkey * NCLUSTER; /* pos # in one hash table */
    hmap->nseat =           hmap->npos * hmap->nmht; /* pos # in all hash tables */
    hmap->freelist.cas.mi = NNULL;


    htab_t *hmap_ht1 = &hmap->ht[0],            /* bucket array 1, 2 and collision array */
           *hmap_ht2 = &hmap->ht[1],
           *hmap_at1 = &hmap->ht[NMHT];

/* n1 -> n2 -> 1/tuning
 * nb1 = n1 * r1, r1 = ((n1+2)/tuning/K^2)^(K^2 - 1)
 * nb2 = n2 * r2 == nb1 / K == ((n2+2)/tuning/K))^(K - 1)
 */
    PRINT_DEBUG_MSG("init bucket array 1:\n");
    const double collision = COLLISION; /* collision control, larger is better */
    const double K = hmap->npos + 1;
    const unsigned long n1 = max_nodes;
    const double r1 = pow ((n1 * collision / (K * K)), (1.0 / (K * K - 1)));
    if (htab_init(hmap_ht1, n1, r1) < 0) {
        goto calloc_exit;
    }

    PRINT_DEBUG_MSG("init bucket array 2:\n");
    const unsigned long n2 = (n1 + 2.0) / (K * pow (r1, K - 1));
    const double r2 = pow (((n2 + 2.0) * collision / K), 1.0 / (K - 1));
    if (htab_init(hmap_ht2, n2, r2) < 0) {
        goto calloc_exit;
    }

    PRINT_DEBUG_MSG("init collision array:\n");
    if (htab_init(hmap_at1, 0, collision) < 0) {
        goto calloc_exit;
    }

    hmap->mpool = mem_pool_create(max_nodes, sizeof(node_t));
//  hmap->mp = old_create_mem_pool (hmap_ht1->nb + hmap_ht2->nb + hmap_at1->nb, sizeof (node_t), max_blocks);
    PRINT_DEBUG_MSG("shift=%u; mask=%u\n", hmap->mpool->shift, hmap->mpool->mask);
    PRINT_DEBUG_MSG("mem_blocks:\t%u/%u, %ux%u bytes, %u bytes per block\n", hmap->mpool->cur_blocks, hmap->mpool->max_blocks,
                    hmap->mpool->blk_node_num, hmap->mpool->node_size, hmap->mpool->blk_size);
    if (!hmap->mpool) {
        goto calloc_exit;
    }

    hmap->stats.max_nodes = hmap->mpool->max_blocks * hmap->mpool->blk_node_num;
    hmap->stats.mem_htabs = ((hmap_ht1->nb + hmap_ht2->nb) * sizeof (nid_t)) >> 10;
    hmap->stats.mem_nodes = (hmap->stats.max_nodes * hmap->mpool->node_size) >> 10;
    return hmap;


calloc_exit:
    for (unsigned long j = 0; j < hmap->nmht; j++) {
        if (hmap->ht[j].b) {
            free (hmap->ht[j].b);
        }
    }
    mem_pool_destroy(hmap->mpool);
    free (hmap);

    return NULL;
}

int atomic_hash_stats (hash_t *hmap, unsigned long escaped_milliseconds) {
    static const char* const log_delim = "    ";

    const hstats_t *hmap_stats = &hmap->stats;
    const htab_t *hmap_ht1 = &hmap->ht[0],
                 *hmap_ht2 = &hmap->ht[1];
    mem_pool_t *hmap_mpool = hmap->mpool;

    double d =         1024.0,
           blk_in_kB = hmap_mpool->blk_size / d,
           mem =       hmap_mpool->cur_blocks * blk_in_kB;

    printf("mem=%.2f, blk_in_kB=%.2f, curr_block=%u, blk_nod_num=%u, node_size=%u\n",
           mem, blk_in_kB, hmap_mpool->cur_blocks, hmap_mpool->blk_node_num, hmap_mpool->node_size);
    printf ("\n");
    printf ("mem_blocks:\t%u/%u, %ux%u bytes, %.2f MB per block\n", hmap_mpool->cur_blocks, hmap_mpool->max_blocks,
            hmap_mpool->blk_node_num, hmap_mpool->node_size, hmap_mpool->blk_size / 1048576.0);
    printf ("mem_to_max:\thtabs[%.2f]MB, nodes[%.2f]MB, total[%.2f]MB\n",
            hmap_stats->mem_htabs / d, hmap_stats->mem_nodes / d, (hmap_stats->mem_htabs + hmap_stats->mem_nodes) / d);
    printf ("mem_in_use:\thtabs[%.2f]MB, nodes[%.2f]MB, total[%.2f]MB\n",
            hmap_stats->mem_htabs / d, mem / d, (hmap_stats->mem_htabs + mem) / d);
    printf ("n1[%lu]/n2[%lu]=[%.3f],  nb1[%lu]/nb2[%lu]=[%.2f]\n",
            hmap_ht1->n, hmap_ht2->n, hmap_ht1->n * 1.0 / hmap_ht2->n, hmap_ht1->nb, hmap_ht2->nb,
            hmap_ht1->nb * 1.0 / hmap_ht2->nb);
    printf ("r1[%f]/r2[%f],  performance_wall[%.1f%%]\n",
            hmap_ht1->nb * 1.0 / hmap_ht1->n, hmap_ht2->nb * 1.0 / hmap_ht2->n,
            hmap_ht1->n * 100.0 / (hmap_ht1->nb + hmap_ht2->nb));
    printf ("---------------------------------------------------------------------------\n");
    printf ("tab n_cur %s%sn_add %s%sn_dup %s%sn_get %s%sn_del\n", log_delim, log_delim, log_delim, log_delim, log_delim, log_delim, log_delim, log_delim);

    htab_t *htab;
    unsigned long ncur = 0,
                  nadd = 0,
                  ndup = 0,
                  nget = 0,
                  ndel = 0;
    for (unsigned long j = 0; j <= NMHT && (htab = &hmap->ht[j]); j++) {
        ncur += htab->ncur;
        nadd += htab->nadd;
        ndup += htab->ndup;
        nget += htab->nget;
        ndel += htab->ndel;
        printf ("%-4lu%-14lu%-14lu%-14lu%-14lu%-14lu\n", j, htab->ncur, htab->nadd, htab->ndup, htab->nget, htab->ndel);
    }
    unsigned long op = ncur + nadd + ndup + nget + ndel + hmap_stats->get_nohit + hmap_stats->del_nohit + hmap_stats->add_nosit + hmap_stats->add_nomem + hmap_stats->escapes;

    printf ("sum %-14lu%-14lu%-14lu%-14lu%-14lu\n", ncur, nadd, ndup, nget, ndel);
    printf ("---------------------------------------------------------------------------\n");
    printf ("del_nohit %sget_nohit %sadd_nosit %sadd_nomem %sexpires %sescapes\n", log_delim, log_delim, log_delim, log_delim, log_delim);
    printf ("%-14lu%-14lu%-14lu%-14lu%-12lu%-12lu\n", hmap_stats->del_nohit,
            hmap_stats->get_nohit, hmap_stats->add_nosit, hmap_stats->add_nomem, hmap_stats->expires, hmap_stats->escapes);
    printf ("---------------------------------------------------------------------------\n");
    if (escaped_milliseconds > 0) {
        printf ("escaped_time=%.3fs, op=%lu, ops=%.2fM/s\n", escaped_milliseconds * 1.0 / 1000, op,
                (double) op / 1000.0 / escaped_milliseconds);
    }
    printf ("\n");

    fflush (stdout);
    return 0;
}

int atomic_hash_destroy (hash_t *hmap) {
    if (!hmap) {
        return -1;
    }

    for (unsigned int j = 0; j < hmap->nmht; j++) {
        free (hmap->ht[j].b);
    }
    mem_pool_destroy(hmap->mpool);
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
static inline nid_t node_new (hash_t *hmap) {
    MEMWORD cas_t n,
                  m;
    while (hmap->freelist.cas.mi != NNULL || mem_block_new(hmap->mpool, &hmap->freelist)) {
        n.all = hmap->freelist.all;
        if (NNULL == n.cas.mi) {
            continue;
        }

        m.cas.mi = ((cas_t *) (I2P (hmap->mpool, node_t, n.cas.mi)))->cas.mi;
        m.cas.rfn = n.cas.rfn + 1;

        if (CAS (&hmap->freelist.all, n.all, m.all)) {
            return n.cas.mi;
        }
    }

    ADD1 (hmap->stats.add_nomem);
    return NNULL;
}

static inline void node_free (hash_t *hmap, nid_t mi) {
    cas_t *p = (cas_t *) (I2P (hmap->mpool, node_t, mi));
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

static inline void node_set (node_t *node, hv_t v, void *data, unsigned long expiry_in_ms) {
    node->v = v;
    node->expiry_in_ms = expiry_in_ms;
    node->data = data;
}

static inline int likely_equal (hv_t w, hv_t v) {
    return w.y == v.y;
}


/* only called in `atomic_hash_get` */
static inline int try_get (hash_t *hmap, hv_t v, node_t *node, nid_t *seat, nid_t mi, int idx, hook_t cb_fct, void *rtn) {
    HOLD_BUCKET_OTHERWISE_RETURN_0 (hmap, node->v, v);
    if (*seat != mi) {
        UNHOLD_BUCKET (node->v, v);
        return 0;
    }

    int result = cb_fct ? cb_fct (node->data, rtn) : hmap->cb_on_get (node->data, rtn);
    if (HOOK_REMOVE_HASH_NODE == result) {
        if (CAS (seat, mi, NNULL)) {
            ATOMIC_SUB1 (hmap->ht[idx].ncur);
        }
        memset (node, 0, sizeof (*node));
        ADD1 (hmap->ht[idx].nget);
        node_free(hmap, mi);
        return 1;
    }
    if (HOOK_RESET_TTL == result) {
        result = hmap->node_expiry_in_ms_reset_val;
    }
    if (node->expiry_in_ms > 0 && result > 0) {
        node->expiry_in_ms = result + gettime_in_ms();
    }
    UNHOLD_BUCKET (node->v, v);
    ADD1 (hmap->ht[idx].nget);
    return 1;
}

/* only called in `atomic_hash_add` */
static inline int try_dup (hash_t *hmap, hv_t v, node_t *node, nid_t *seat, nid_t mi, int idx, hook_t cb_fct, void *rtn) {
    HOLD_BUCKET_OTHERWISE_RETURN_0 (hmap, node->v, v);
    if (*seat != mi) {
        UNHOLD_BUCKET (node->v, v);
        return 0;
    }

    int result = cb_fct ? cb_fct (node->data, rtn) : hmap->cb_on_dup (node->data, rtn);
    if (HOOK_REMOVE_HASH_NODE == result) {
        if (CAS (seat, mi, NNULL)) {
            ATOMIC_SUB1 (hmap->ht[idx].ncur);
        }
        memset (node, 0, sizeof (*node));
        ADD1 (hmap->ht[idx].ndup);
        node_free(hmap, mi);
        return 1;
    }
    if (HOOK_RESET_TTL == result) {
        result = hmap->node_expiry_in_ms_reset_val;
    }
    if (node->expiry_in_ms > 0 && result > 0) {
        node->expiry_in_ms = result + gettime_in_ms();
    }
    UNHOLD_BUCKET (node->v, v);
    ADD1 (hmap->ht[idx].ndup);
    return 1;
}

/* only called in `atomic_hash_add` */
static inline int try_add (hash_t *hmap, node_t *node, nid_t *seat, nid_t mi, int idx, void *rtn) {
    hvu_t x = node->v.x;
    node->v.x = 0;
    if (!CAS (seat, NNULL, mi)) {
        node->v.x = x;
        return 0; /* other thread wins, caller to retry other seats */
    }

    ATOMIC_ADD1 (hmap->ht[idx].ncur);
    int result = hmap->cb_on_add (node->data, rtn);
    if (HOOK_REMOVE_HASH_NODE == result) {
        if (CAS (seat, mi, NNULL)) {
            ATOMIC_SUB1 (hmap->ht[idx].ncur);
        }
        memset (node, 0, sizeof (*node));
        node_free(hmap, mi);
        return 1; /* abort adding this node */
    }
    if (HOOK_RESET_TTL == result) {
        result = hmap->node_expiry_in_ms_reset_val;
    }
    if (node->expiry_in_ms > 0 && result > 0) {
        node->expiry_in_ms = result + gettime_in_ms();
    }
    node->v.x = x;
    ADD1 (hmap->ht[idx].nadd);
    return 1;
}

/* only called in atomic_hash_del */
static inline int try_del (hash_t *hmap, hv_t v, node_t *node, nid_t *seat, nid_t mi, int idx, hook_t cb_fct, void *rtn) {
    HOLD_BUCKET_OTHERWISE_RETURN_0 (hmap, node->v, v);
    if (*seat != mi || !CAS (seat, mi, NNULL)) {
        UNHOLD_BUCKET (node->v, v);
        return 0;
    }

    ATOMIC_SUB1 (hmap->ht[idx].ncur);
    void *user_data = node->data;
    memset (node, 0, sizeof (*node));
    ADD1 (hmap->ht[idx].ndel);
    node_free(hmap, mi);

    if (cb_fct) {
        cb_fct (user_data, rtn);
    } else {
        hmap->cb_on_del (user_data, rtn);
    }

    return 1;
}

static inline int valid_ttl (hash_t *hmap, unsigned long cur_time_in_ms, node_t *node, nid_t *seat, nid_t mi,
                             int idx, nid_t *node_rtn, void *data_rtn) {
    unsigned long node_expiry_in_ms = node->expiry_in_ms;
    /* valid state, quickly skip to call try_action. */
    if (0 == node_expiry_in_ms || node_expiry_in_ms > cur_time_in_ms) {
        return 1;
    }

    hv_t v = node->v;
    /* hold on or removed by others, skip to call try_action */
    if (0 == v.x || 0 == v.y) {
        return 1;
    }

    HOLD_BUCKET_OTHERWISE_RETURN_0 (hmap, node->v, v);
    /* re-enter valid state, skip to call try_action */
    if (0 == node->expiry_in_ms || node->expiry_in_ms > cur_time_in_ms) {
        UNHOLD_BUCKET (node->v, v);
        return 1;
    }

    /* expired,  cur_time_in_ms remove it */
    if (*seat != mi || !CAS (seat, mi, NNULL)) {
        /* failed to remove. let others do it in the future, skip and go next pos */
        UNHOLD_BUCKET (node->v, v);
        return 0;
    }

    ATOMIC_SUB1 (hmap->ht[idx].ncur);
    void *user_data = node->data;
    memset (node, 0, sizeof (*node));
    ADD1 (hmap->stats.expires);
    /* return this hash node for caller re-use */
    /* strict version: if (!node_rtn || !CAS(node_rtn, NNULL, mi)) */
    if (node_rtn && NNULL == *node_rtn) {
        *node_rtn = mi;
    } else {
        node_free(hmap, mi);
    }

    if (hmap->cb_on_ttl) {
        hmap->cb_on_ttl (user_data, data_rtn);
    }

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

#define IDX(J) ((J) < (NCLUSTER * NKEY) ? 0 : 1)

int atomic_hash_add (hash_t *hmap, const void *kwd, int len, void *data,
                     int init_ttl, hook_t cb_fct_dup, void *arg) {
    MEMWORD union { hv_t v; nid_t d[NKEY]; } t;
    unsigned long cur_time_in_ms = gettime_in_ms();
    if (len > 0) {
        hmap->hash_func (kwd, len, &t);
    } else if (0 == len) {
        memcpy (&t, kwd, sizeof(t));
    } else {
        return -3; /* key length not defined */
    }

    MEMWORD nid_t *a[NSEAT];
    COLLECT_HASH_POS (hmap, t.d, a);

    nid_t ni = NNULL;
    register nid_t mi;
    register node_t *node;
    for (register unsigned int j = 0; j < NSEAT; j++) {
        if ((mi = *a[j]) != NNULL && (node = I2P (hmap->mpool, node_t, mi))) {
            if (valid_ttl (hmap, cur_time_in_ms, node, a[j], mi, IDX (j), &ni, NULL)) {
                if (likely_equal (node->v, t.v)) {
                    if (try_dup (hmap, t.v, node, a[j], mi, IDX (j), cb_fct_dup, arg)) {
                        goto hash_value_exists;
                    }
                }
            }
        }
    }

    for (register unsigned int i = hmap->ht[NMHT].ncur,
                               j = 0; i > 0 && j < MINTAB; j++) {
        if ((mi = hmap->ht[NMHT].b[j]) != NNULL && (node = I2P (hmap->mpool, node_t, mi)) && i--) {
            if (valid_ttl (hmap, cur_time_in_ms, node, &hmap->ht[NMHT].b[j], mi, NMHT, &ni, NULL)) {
                if (likely_equal (node->v, t.v)) {
                    if (try_dup (hmap, t.v, node, &hmap->ht[NMHT].b[j], mi, NMHT, cb_fct_dup, arg)) {
                        goto hash_value_exists;
                    }
                }
            }
        }
    }

    if (NNULL == ni && NNULL == (ni = node_new(hmap))) {
        return -2;  /* hash node exhausted */
    }

    node = I2P (hmap->mpool, node_t, ni);
    node_set(node, t.v, data, (init_ttl > 0 ? init_ttl + cur_time_in_ms : 0));

    for (register unsigned int j = 0; j < NSEAT; j++) {
        if (NNULL == *a[j]) {
            if (try_add (hmap, node, a[j], ni, IDX (j), arg)) {
                return 0; /* hash value added */
            }
        }
    }

    if (hmap->ht[NMHT].ncur < MINTAB) {
        for (register unsigned int j = 0; j < MINTAB; j++) {
            if (NNULL == hmap->ht[NMHT].b[j]) {
                if (try_add (hmap, node, &hmap->ht[NMHT].b[j], ni, NMHT, arg)) {
                    return 0; /* hash value added */
                }
            }
        }
    }

    memset (node, 0, sizeof (*node));
    node_free(hmap, ni);
    ADD1 (hmap->stats.add_nosit);
    return -1; /* add but fail */

hash_value_exists:
    if (ni != NNULL) {
        node_free(hmap, ni);
    }
    return 1; /* hash value exists */
}


int atomic_hash_get (hash_t *hmap, const void *kwd, int len, hook_t cb_fct, void *arg) {
    MEMWORD union { hv_t v; nid_t d[NKEY]; } t;
    unsigned long cur_time_in_ms = gettime_in_ms();
    if (len > 0) {
        hmap->hash_func (kwd, len, &t);
    } else if (0 == len) {
        memcpy (&t, kwd, sizeof(t));
    } else {
        return -3; /* key length not defined */
    }

    MEMWORD nid_t *a[NSEAT];
    COLLECT_HASH_POS (hmap, t.d, a);


    register nid_t mi;
    register node_t *node;
    for (register unsigned int j = 0; j < NSEAT; j++) {
        if ((mi = *a[j]) != NNULL && (node = I2P (hmap->mpool, node_t, mi))) {
            if (valid_ttl (hmap, cur_time_in_ms, node, a[j], mi, IDX (j), NULL, NULL)) {
                if (likely_equal (node->v, t.v)) {
                    if (try_get (hmap, t.v, node, a[j], mi, IDX (j), cb_fct, arg)) {
                        return 0;
                    }
                }
            }
        }
    }

    for (register unsigned int j = 0,
                               i = 0; i < hmap->ht[NMHT].ncur && j < MINTAB; j++) {
        if ((mi = hmap->ht[NMHT].b[j]) != NNULL && (node = I2P (hmap->mpool, node_t, mi)) && ++i) {
            if (valid_ttl (hmap, cur_time_in_ms, node, &hmap->ht[NMHT].b[j], mi, NMHT, NULL, NULL)) {
                if (likely_equal (node->v, t.v)) {
                    if (try_get (hmap, t.v, node, &hmap->ht[NMHT].b[j], mi, NMHT, cb_fct, arg)) {
                        return 0;
                    }
                }
            }
        }
    }

    ADD1 (hmap->stats.get_nohit);
    return -1;
}

int atomic_hash_del (hash_t *hmap, const void *kwd, int len, hook_t cb_fct, void *arg) {
    MEMWORD union { hv_t v; nid_t d[NKEY]; } t;
    if (len > 0) {
        hmap->hash_func (kwd, len, &t);
    } else if (0 == len) {
        memcpy (&t, kwd, sizeof(t));
    } else {
        return -3; /* key length not defined */
    }

    MEMWORD nid_t *a[NSEAT];
    COLLECT_HASH_POS (hmap, t.d, a);


    register nid_t mi;
    register node_t *node;
    unsigned long cur_time_in_ms = gettime_in_ms();
    register unsigned int del_matches = 0; /* delete all matches */
    for (register unsigned int j = 0; j < NSEAT; j++) {
        if ((mi = *a[j]) != NNULL && (node = I2P (hmap->mpool, node_t, mi))) {
            if (valid_ttl (hmap, cur_time_in_ms, node, a[j], mi, IDX (j), NULL, NULL)) {
                if (likely_equal (node->v, t.v)) {
                    if (try_del (hmap, t.v, node, a[j], mi, IDX (j), cb_fct, arg)) {
                        del_matches++;
                    }
                }
            }
        }
    }

    if (hmap->ht[NMHT].ncur > 0) {
        for (register unsigned int j = 0; j < MINTAB; j++) {
            if ((mi = hmap->ht[NMHT].b[j]) != NNULL && (node = I2P (hmap->mpool, node_t, mi))) {
                if (valid_ttl (hmap, cur_time_in_ms, node, &hmap->ht[NMHT].b[j], mi, NMHT, NULL, NULL)) {
                    if (likely_equal (node->v, t.v)) {
                        if (try_del (hmap, t.v, node, &hmap->ht[NMHT].b[j], mi, NMHT, cb_fct, arg)) {
                            del_matches++;
                        }
                    }
                }
            }
        }
    }

    if (del_matches > 0) {
        return 0;
    }

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
