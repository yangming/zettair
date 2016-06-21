/* hawkapi.c implements the hawkapi metric for the zettair query
 * subsystem.  This file was automatically generated from
 * src/hawkapi.metric and src/metric.c
 * by scripts/metric.py on Mon, 02 Mar 2009 06:38:46 GMT.  
 *
 * DO NOT MODIFY THIS FILE, as changes will be lost upon 
 * subsequent regeneration (and this code is repetitive enough 
 * that you probably don't want to anyway).  
 * Go modify src/hawkapi.metric or src/metric.c instead.  
 * 
 * Comments from hawkapi.metric:
 *
 * hawkapi.metric is a functional description in our funny zettair metric
 * language (see metric.py) of how the 'AF1' metric should operate.
 * 
 * AF1 is Dave Hawking's anchor-text specific okapi variant (see 'Toward better
 * weighting of anchors', Hawking, Upstill and Craswell, SIGIR 2004 poster).
 * They called it 'AF1', but i find hawkapi a little less boring ;o).
 * It's basically a stripped-down version of the okapi metric.  No-one
 * really knows, at this stage (including Dave Hawking, when i asked him),
 * what a good value of alpha is.
 * 
 * written nml 2005-07-18
 *
 */

#include "zettair.h"

#include "search.h"
#include "str.h"

#include <stdlib.h>

/* a structure describing the parameters used */
struct hawkapi_param {
    double alpha;
    double k3;
    int dummy;  /* dummy member to ensure non-empty struct */
};

/* Function for converting string args into the params structure. */
static enum search_ret parse(void *ptr, const char *str) {
    /* weak typing may suck sometimes, but it certainly has its uses */
    struct hawkapi_param *param = ptr;
    unsigned int i,
                 parts,
                 read = 0;
    char *dup;
    char **split = NULL;
    char *endptr = NULL;

    if (str == NULL) {
        return SEARCH_EINVAL;
    }

    if ((dup = str_dup(str)) && (split = str_split(dup, ",", &parts)) 
      && parts == 2) {
        for (i = 0; i < parts; i++) {
            if (!str_ncmp(split[i], "alpha=", 6)) {
                if ((param->alpha = (double)strtod(split[i] + 6, &endptr)), !*endptr) {
                    read |= (1 << 0);
                } else {
                    free(split);
                    free(dup);
                    return SEARCH_EINVAL;
                }
            }
            else
            if (!str_ncmp(split[i], "k3=", 3)) {
                if ((param->k3 = (double)strtod(split[i] + 3, &endptr)), !*endptr) {
                    read |= (1 << 1);
                } else {
                    free(split);
                    free(dup);
                    return SEARCH_EINVAL;
                }
            }
        }
        free(split);
        free(dup);
        if (read == ((1 << 0) | (1 << 1))) {
            return SEARCH_OK;
        } else {
            return SEARCH_EINVAL;
        }
    } else {
        if (dup) {
            free(dup);
        }
        if (split) {
            free(split);
        }
        return SEARCH_EINVAL;
    }
}


/* processed contents from src/hawkapi.metric and src/metric.c follows... */


/* XXX: METRIC_STRUCT should possibly be subsumed into METRIC_DECL?  Either way,
 * it's messy */

/* XXX: for the moment, [or,and,thresh]_decode have been copied to an 
 * equivalent _decode_offsets function.  This is pretty nasty, but i expect to
 * be able to remove this duplication once offsets have been de-interleaved 
 * from the inverted lists. */

#include "metric.h"

#include "_index.h"
#include "_docmap.h"
#include "index_querybuild.h"

#include "def.h"
#include "objalloc.h"
#include "docmap.h"
#include "search.h"
#include "vec.h"

#include <assert.h>
#include <float.h>

static enum search_ret pre(struct index *idx, struct query *query, 
  int opts, struct index_search_opt *opt) {
    if (zpthread_mutex_lock(&idx->docmap_mutex) == ZPTHREAD_OK) {
        /* METRIC_PRE */

        zpthread_mutex_unlock(&idx->docmap_mutex);
        return SEARCH_OK;
    } else {
        assert(!CRASH);
        return SEARCH_EINVAL;
    }
}

static enum search_ret post(struct index *idx, struct query *query, 
  struct search_acc_cons *acc, int opts, struct index_search_opt *opt) {
    /* METRIC_POST */


    while (acc) {
        assert(acc->acc.docno < docmap_entries(idx->map));
        /* METRIC_POST_PER_DOC */

        acc = acc->next;
    }

    return SEARCH_OK;
}

/* macro to atomically read the next docno and f_dt from a vector 
 * (note: i also tried a more complicated version that tested for a long vec 
 * and used unchecked reads, but measurements showed no improvement) */
#define NEXT_DOC(v, docno, f_dt)                                              \
    (vec_vbyte_read(v, &docno_d)                                              \
      && (((vec_vbyte_read(v, &f_dt) && ((docno += docno_d + 1), 1))          \
        /* second read failed, reposition vec back to start of docno_d */     \
        || (((v)->pos -= vec_vbyte_len(docno_d)), 0))))

/* macro to scan over f_dt offsets from a vector/source */
#define SCAN_OFFSETS(src, v, f_dt)                                            \
    do {                                                                      \
        unsigned int toscan = f_dt,                                           \
                     scanned;                                                 \
        enum search_ret sret;                                                 \
                                                                              \
        do {                                                                  \
            if ((scanned = vec_vbyte_scan(v, toscan, &scanned)) == toscan) {  \
                toscan = 0;                                                   \
                break;                                                        \
            } else if (scanned < toscan) {                                    \
                toscan -= scanned;                                            \
                /* need to read more */                                       \
                if ((sret = src->read(src, VEC_LEN(v),                        \
                    (void **) &(v)->pos, &bytes)) == SEARCH_OK) {             \
                                                                              \
                    (v)->end = (v)->pos + bytes;                              \
                } else if (sret == SEARCH_FINISH) {                           \
                    /* shouldn't end while scanning offsets */                \
                    return SEARCH_EINVAL;                                     \
                } else {                                                      \
                    return sret;                                              \
                }                                                             \
            } else {                                                          \
                assert("can't get here" && 0);                                \
                return SEARCH_EINVAL;                                         \
            }                                                                 \
        } while (toscan);                                                     \
    } while (0)
 
static enum search_ret or_decode(struct index *idx, struct query *query, 
  unsigned int qterm, unsigned long int docno, 
  struct search_metric_results *results, struct search_list_src *src, 
  int opts, struct index_search_opt *opt) {
    /* METRIC_STRUCT */ struct hawkapi_param *param = (void *) &opt->u;
    struct search_acc_cons *acc = results->acc,
                           **prevptr = &results->acc;
    unsigned int accs_added = 0;   /* number of accumulators added */
    unsigned long int f_dt,        /* number of offsets for this document */
                      docno_d;     /* d-gap */
    unsigned int bytes;
    struct vec v = {NULL, NULL};
    enum search_ret ret;
    /* METRIC_DECL */

    const unsigned int N = docmap_entries(idx->map);

    const double w_t = log((N - (query->term[qterm].f_t) + 0.5) / ((query->term[qterm].f_t) + 0.5));

    const double w_qt = (((param->k3) + 1) * (query->term[qterm].f_qt)) / ((param->k3) + (query->term[qterm].f_qt));


    /* METRIC_PER_CALL */


    while (1) {
        while (NEXT_DOC(&v, docno, f_dt)) {

            /* merge into accumulator list */
            while (acc && (docno > acc->acc.docno)) {
                prevptr = &acc->next;
                acc = acc->next;
            }

            if (acc && (docno == acc->acc.docno)) {
                /* METRIC_PER_DOC */
                (acc->acc.weight) += w_qt * (param->alpha) * log(f_dt + 1) * w_t;

            } else {
                struct search_acc_cons *newacc;
                assert(!acc || docno < acc->acc.docno); 

                /* allocate a new accumulator (we have reserved allocators
                 * earlier, so this should never fail) */
                newacc = objalloc_malloc(results->alloc, sizeof(*newacc));
                assert(newacc);
                newacc->next = acc;
                acc = newacc;
                acc->acc.docno = docno;
                acc->acc.weight = 0.0;
                /* METRIC_PER_DOC */
                (acc->acc.weight) += w_qt * (param->alpha) * log(f_dt + 1) * w_t;

                *prevptr = newacc;
                accs_added++;
            }
            assert(acc);

            /* go to next accumulator */
            prevptr = &acc->next;
            acc = acc->next;
        }

        /* need to read more data, preserving bytes that we already have */
        if ((ret = src->read(src, VEC_LEN(&v),
            (void **) &v.pos, &bytes)) == SEARCH_OK) {

            v.end = v.pos + bytes;
        } else if (ret == SEARCH_FINISH) {
            /* finished, update number of accumulators */
            results->accs += accs_added;
            results->total_results += accs_added;

            if (!VEC_LEN(&v)) {
                return SEARCH_OK;
            } else {
                return SEARCH_EINVAL;
            }
        } else {
            param = NULL;   /* avoid 'param not used' warning */
            return ret;
        }
    }
}
 
static enum search_ret or_decode_offsets(struct index *idx, struct query *query,
  unsigned int qterm, unsigned long int docno, 
  struct search_metric_results *results, struct search_list_src *src, 
  int opts, struct index_search_opt *opt) {
    /* METRIC_STRUCT */ struct hawkapi_param *param = (void *) &opt->u;
    struct search_acc_cons *acc = results->acc,
                           **prevptr = &results->acc;
    unsigned int accs_added = 0;   /* number of accumulators added */
    unsigned long int f_dt,        /* number of offsets for this document */
                      docno_d;     /* d-gap */
    unsigned int bytes;
    struct vec v = {NULL, NULL};
    enum search_ret ret;
    /* METRIC_DECL */

    const unsigned int N = docmap_entries(idx->map);

    const double w_t = log((N - (query->term[qterm].f_t) + 0.5) / ((query->term[qterm].f_t) + 0.5));

    const double w_qt = (((param->k3) + 1) * (query->term[qterm].f_qt)) / ((param->k3) + (query->term[qterm].f_qt));


    /* METRIC_PER_CALL */


    while (1) {
        while (NEXT_DOC(&v, docno, f_dt)) {
            SCAN_OFFSETS(src, &v, f_dt);

            /* merge into accumulator list */
            while (acc && (docno > acc->acc.docno)) {
                prevptr = &acc->next;
                acc = acc->next;
            }

            if (acc && (docno == acc->acc.docno)) {
                /* METRIC_PER_DOC */
                (acc->acc.weight) += w_qt * (param->alpha) * log(f_dt + 1) * w_t;

            } else {
                struct search_acc_cons *newacc;
                assert(!acc || docno < acc->acc.docno); 

                /* allocate a new accumulator (we have reserved allocators
                 * earlier, so this should never fail) */
                newacc = objalloc_malloc(results->alloc, sizeof(*newacc));
                assert(newacc);
                newacc->next = acc;
                acc = newacc;
                acc->acc.docno = docno;
                acc->acc.weight = 0.0;
                /* METRIC_PER_DOC */
                (acc->acc.weight) += w_qt * (param->alpha) * log(f_dt + 1) * w_t;

                *prevptr = newacc;
                accs_added++;
            }
            assert(acc);

            /* go to next accumulator */
            prevptr = &acc->next;
            acc = acc->next;
        }

        /* need to read more data, preserving bytes that we already have */
        if ((ret = src->read(src, VEC_LEN(&v),
            (void **) &v.pos, &bytes)) == SEARCH_OK) {

            v.end = v.pos + bytes;
        } else if (ret == SEARCH_FINISH) {
            /* finished, update number of accumulators */
            results->accs += accs_added;
            results->total_results += accs_added;

            if (!VEC_LEN(&v)) {
                return SEARCH_OK;
            } else {
                return SEARCH_EINVAL;
            }
        } else {
            param = NULL;   /* avoid 'param not used' warning */
            return ret;
        }
    }
}

static enum search_ret and_decode(struct index *idx, struct query *query, 
  unsigned int qterm, unsigned long int docno, 
  struct search_metric_results *results, struct search_list_src *src,
  int opts, struct index_search_opt *opt) {
    /* METRIC_STRUCT */ struct hawkapi_param *param = (void *) &opt->u;
    struct search_acc_cons *acc = results->acc;
    unsigned long int f_dt,        /* number of offsets for this document */
                      docno_d;     /* d-gap */
    struct vec v = {NULL, NULL};
    unsigned int bytes,
                 missed = 0,       /* number of list entries that didn't match 
                                    * an accumulator */
                 hit = 0,          /* number of entries in both accs and list*/
                 decoded = 0;      /* number of list entries seen */
    enum search_ret ret;
    double cooc_rate;               /* co-occurrance rate for list entries and 
                                    * accumulators */
    /* METRIC_DECL */

    const unsigned int N = docmap_entries(idx->map);

    const double w_t = log((N - (query->term[qterm].f_t) + 0.5) / ((query->term[qterm].f_t) + 0.5));

    const double w_qt = (((param->k3) + 1) * (query->term[qterm].f_qt)) / ((param->k3) + (query->term[qterm].f_qt));


    /* METRIC_PER_CALL */


    while (1) {
        while (NEXT_DOC(&v, docno, f_dt)) {
            decoded++;

            /* merge into accumulator list */
            while (acc && (docno > acc->acc.docno)) {
                acc = acc->next;
            }

            if (acc && (docno == acc->acc.docno)) {
                /* METRIC_PER_DOC */
                (acc->acc.weight) += w_qt * (param->alpha) * log(f_dt + 1) * w_t;


                /* go to next accumulator */
                acc = acc->next;
                hit++;
            } else {
                missed++;
            }
        }

        /* need to read more data, preserving bytes that we already have */
        if ((ret = src->read(src, VEC_LEN(&v),
            (void **) &v.pos, &bytes)) == SEARCH_OK) {

            v.end = v.pos + bytes;
        } else if (ret == SEARCH_FINISH) {
            /* finished, estimate number of results */

            /* list entries now divide up into two portions:
             *   - matching an entry in the acc list (hit)
             *   - missed
             *
             * cooccurrance rate is the percentage of list items hit */
            assert(missed + hit == decoded);
            cooc_rate = hit/(double)decoded;

            /* now have sampled co-occurrance rate, use this to estimate 
             * population co-occurrance rate (assuming unbiased sampling) 
             * and then number of results from unrestricted evaluation */
            assert(results->total_results >= results->accs);
            cooc_rate *= results->total_results/(double)results->accs; 
            assert(cooc_rate >= 0.0);
            if (cooc_rate > 1.0) {
                cooc_rate = 1.0;
            }

            /* add number of things we think would have been added from the
             * things that were missed */
            results->total_results += (1 - cooc_rate) * missed;

            if (missed) {
                results->estimated |= 1;
            }

            if (!VEC_LEN(&v)) {
                return SEARCH_OK;
            } else {
                return SEARCH_EINVAL;
            }
        } else {
            param = NULL;   /* avoid 'param not used' warning */
            return ret;
        }
    }
}

static enum search_ret and_decode_offsets(struct index *idx, 
  struct query *query, 
  unsigned int qterm, unsigned long int docno, 
  struct search_metric_results *results, struct search_list_src *src,
  int opts, struct index_search_opt *opt) {
    /* METRIC_STRUCT */ struct hawkapi_param *param = (void *) &opt->u;
    struct search_acc_cons *acc = results->acc;
    unsigned long int f_dt,        /* number of offsets for this document */
                      docno_d;     /* d-gap */
    struct vec v = {NULL, NULL};
    unsigned int bytes,
                 missed = 0,       /* number of list entries that didn't match 
                                    * an accumulator */
                 hit = 0,          /* number of entries in both accs and list*/
                 decoded = 0;      /* number of list entries seen */
    enum search_ret ret;
    double cooc_rate;               /* co-occurrance rate for list entries and 
                                    * accumulators */
    /* METRIC_DECL */

    const unsigned int N = docmap_entries(idx->map);

    const double w_t = log((N - (query->term[qterm].f_t) + 0.5) / ((query->term[qterm].f_t) + 0.5));

    const double w_qt = (((param->k3) + 1) * (query->term[qterm].f_qt)) / ((param->k3) + (query->term[qterm].f_qt));


    /* METRIC_PER_CALL */


    while (1) {
        while (NEXT_DOC(&v, docno, f_dt)) {
            SCAN_OFFSETS(src, &v, f_dt);
            decoded++;

            /* merge into accumulator list */
            while (acc && (docno > acc->acc.docno)) {
                acc = acc->next;
            }

            if (acc && (docno == acc->acc.docno)) {
                /* METRIC_PER_DOC */
                (acc->acc.weight) += w_qt * (param->alpha) * log(f_dt + 1) * w_t;


                /* go to next accumulator */
                acc = acc->next;
                hit++;
            } else {
                missed++;
            }
        }

        /* need to read more data, preserving bytes that we already have */
        if ((ret = src->read(src, VEC_LEN(&v),
            (void **) &v.pos, &bytes)) == SEARCH_OK) {

            v.end = v.pos + bytes;
        } else if (ret == SEARCH_FINISH) {
            /* finished, estimate number of results */

            /* list entries now divide up into two portions:
             *   - matching an entry in the acc list (hit)
             *   - missed
             *
             * cooccurrance rate is the percentage of list items hit */
            assert(missed + hit == decoded);
            cooc_rate = hit/(double)decoded;

            /* now have sampled co-occurrance rate, use this to estimate 
             * population co-occurrance rate (assuming unbiased sampling) 
             * and then number of results from unrestricted evaluation */
            assert(results->total_results >= results->accs);
            cooc_rate *= results->total_results/(double)results->accs; 
            assert(cooc_rate >= 0.0);
            if (cooc_rate > 1.0) {
                cooc_rate = 1.0;
            }

            /* add number of things we think would have been added from the
             * things that were missed */
            results->total_results += (1 - cooc_rate) * missed;

            if (missed) {
                results->estimated |= 1;
            }

            if (!VEC_LEN(&v)) {
                return SEARCH_OK;
            } else {
                return SEARCH_EINVAL;
            }
        } else {
            param = NULL;   /* avoid 'param not used' warning */
            return ret;
        }
    }
}

/* tolerance value for thresholding estimates.  Should be >= 1.0.  Make higher
 * for stabler, but higher memory usage, processing. */
#define TOLERANCE 1.2

/* low-ish approximation of infinity, to make counting up to it acceptable */
#define INF 2000

static enum search_ret thresh_decode(struct index *idx, struct query *query,
  unsigned int qterm, unsigned long int docno, 
  struct search_metric_results *results, 
  struct search_list_src *src, unsigned int postings, 
  int opts, struct index_search_opt *opt) {
    /* METRIC_STRUCT */ struct hawkapi_param *param = (void *) &opt->u;
    struct search_acc_cons *acc, dummy, **prevptr = &results->acc;
    unsigned long int f_dt,           /* number of offsets for this document */
                      docno_d;        /* d-gap */

    /* initial number of accumulators */
    unsigned int initial_accs = results->accs,

                 decoded = 0,         /* number of postings decoded */
                 thresh,              /* current discrete threshold */
                 rethresh,            /* distance to recalculation of the threshold */
                 rethresh_dist,
                 bytes,
                 step,
                 missed = 0,          /* number of list entries that didn't match an accumulator */
                 hit = 0;             /* number of entries in both accs and list*/
 
    struct vec v = {NULL, NULL};
    enum search_ret ret = SEARCH_EIO;
    int infinite = 0;                 /* whether threshold is infinite */
    double cooc_rate;
    /* METRIC_DECL */

    const unsigned int N = docmap_entries(idx->map);

    const double w_t = log((N - (query->term[qterm].f_t) + 0.5) / ((query->term[qterm].f_t) + 0.5));

    const double w_qt = (((param->k3) + 1) * (query->term[qterm].f_qt)) / ((param->k3) + (query->term[qterm].f_qt));


    /* METRIC_PER_CALL */

    rethresh_dist = rethresh = (postings + results->acc_limit - 1) 
      / results->acc_limit;

    if (results->v_t == FLT_MIN) {
        unsigned long int docno_copy = docno;

        /* this should be the first thresholded list, need to estimate threshold */
        assert(rethresh && rethresh < postings);
        thresh = 0;

        assert(rethresh < postings);
        while (rethresh) {
            while (rethresh && NEXT_DOC(&v, docno, f_dt)) {
                rethresh--;
                if (f_dt > thresh) {
                    thresh = f_dt;
                }
            }

            /* need to read more data, preserving bytes that we already have */
            if (rethresh && (ret = src->read(src, VEC_LEN(&v),
                (void **) &v.pos, &bytes)) == SEARCH_OK) {

                v.end = v.pos + bytes;
            } else if (rethresh) {
                assert(ret != SEARCH_FINISH);
                return ret;
            }
        }
        thresh--;

        acc = &dummy;
        acc->acc.docno = UINT_MAX;   /* shouldn't be used */
        acc->acc.weight = 0.0;
        f_dt = thresh;
        /* METRIC_CONTRIB */
        (acc->acc.weight) += w_qt * (param->alpha) * log(f_dt + 1) * w_t;

        results->v_t = acc->acc.weight;

        /* reset source/vector to start */
        v.pos = v.end = NULL;
        if ((ret = src->reset(src)) != SEARCH_OK) {
            return ret;
        }

        acc = *prevptr;
        docno = docno_copy;
        rethresh = rethresh_dist;
    } else {
        /* translate the existing v_t threshold to an f_dt */
        acc = &dummy;
        acc->acc.docno = UINT_MAX;   /* shouldn't be used */
        f_dt = 0;
        do {
            acc->acc.weight = 0.0;
            f_dt++;
            /* METRIC_CONTRIB */
            (acc->acc.weight) += w_qt * (param->alpha) * log(f_dt + 1) * w_t;

        } while (acc->acc.weight < results->v_t && f_dt < INF);
        thresh = f_dt; 
        acc = *prevptr;

        if (thresh == INF) {
            /* this is not a sensible term */
            infinite = 1;
            rethresh = postings + 1;
        }
    }

    /* set step to 1/2 of the threshold */
    step = (thresh + 1) / 2;
    step += !step; /* but don't let it become 0 */

    while (1) {
        while (NEXT_DOC(&v, docno, f_dt)) {
            decoded++;

            /* merge into accumulator list */
            while (acc && (docno > acc->acc.docno)) {
                /* perform threshold test */
                if (acc->acc.weight < results->v_t) {
                    /* remove this accumulator */
                    *prevptr = acc->next;
                    objalloc_free(results->alloc, acc);
                    acc = (*prevptr);
                    results->accs--;
                } else {
                    /* retain this accumulator */
                    prevptr = &acc->next;
                    acc = acc->next;
                }
            }

            if (acc && (docno == acc->acc.docno)) {
                /* METRIC_PER_DOC */
                (acc->acc.weight) += w_qt * (param->alpha) * log(f_dt + 1) * w_t;


                if (acc->acc.weight < results->v_t) {
                    /* remove this accumulator */
                    *prevptr = acc->next;
                    objalloc_free(results->alloc, acc);
                    acc = *prevptr;
                    results->accs--;
                } else {
                    /* go to next accumulator */
                    prevptr = &acc->next;
                    acc = acc->next;
                }
                hit++;
            } else {
                if (f_dt > thresh) {
                    struct search_acc_cons *newacc;
                    assert(!acc || docno < acc->acc.docno); 

                    if ((newacc = objalloc_malloc(results->alloc, 
                      sizeof(*newacc)))) {
                        newacc->acc.docno = docno;
                        newacc->acc.weight = 0.0;
                        newacc->next = acc;
                        acc = newacc;
                        /* note that we have to be careful around here to 
                         * assign newacc to acc before using PER_DOC, 
                         * otherwise we end up with nonsense in some 
                         * accumulators */
                        /* METRIC_PER_DOC */
                        (acc->acc.weight) += w_qt * (param->alpha) * log(f_dt + 1) * w_t;

                        *prevptr = newacc;
                        results->accs++;
                    } else {
                        return SEARCH_ENOMEM;
                    }

                    /* go to next accumulator */
                    prevptr = &acc->next;
                    acc = acc->next;
                } else {
                    missed++;
                }
            }

            if (!--rethresh) {
                int estimate,
                    prev_thresh = thresh;

                estimate = results->accs 
                  + ((postings - decoded) 
                    * ((double)results->accs - initial_accs)) / decoded;

                if (estimate > TOLERANCE * results->acc_limit) {
                    thresh += step;
                } else if ((estimate < results->acc_limit / TOLERANCE) 
                  && thresh) {
                    if (thresh >= step) {
                        thresh -= step;
                    } else {
                        thresh = 0;
                    }
                }

                step = (step + 1) / 2;
                assert(step);

                /* note that we don't want to recalculate the threshold if it
                 * doesn't change because this involves re-discretising it */
                if (prev_thresh != thresh) {
                    /* recalculate contribution that corresponds to the new 
                     * threshold */
                    f_dt = thresh;
                    if (f_dt) {
                        acc = &dummy;
                        acc->acc.docno = UINT_MAX;   /* shouldn't be used */
                        acc->acc.weight = 0.0;
                        /* METRIC_CONTRIB */
                        (acc->acc.weight) += w_qt * (param->alpha) * log(f_dt + 1) * w_t;

                        results->v_t = acc->acc.weight;
                        acc = *prevptr;
                    } else {
                        results->v_t = FLT_MIN;
                    }
                }

                rethresh_dist *= 2;
                rethresh = rethresh_dist;
            }
        }

        /* need to read more data, preserving bytes that we already have */
        if ((ret = src->read(src, VEC_LEN(&v),
            (void **) &v.pos, &bytes)) == SEARCH_OK) {

            v.end = v.pos + bytes;
        } else if (ret == SEARCH_FINISH) {
            /* finished, estimate total results count */
            assert(postings == decoded);

            results->total_results += (int) (results->accs - initial_accs);

            /* list entries now divide up into three portions:
             *   - matching an entry in the acc list (hit)
             *   - missed
             *   - added
             *
             * cooccurrance rate is the percentage of list items hit */
            cooc_rate = hit/(double)decoded;

            /* now have sampled co-occurrance rate, use this to estimate 
             * population co-occurrance rate (assuming unbiased sampling) 
             * and then number of results from unrestricted evaluation */
            assert(results->total_results >= results->accs);
            cooc_rate *= results->total_results/(double)results->accs; 
            assert(cooc_rate >= 0.0);
            if (cooc_rate > 1.0) {
                cooc_rate = 1.0;
            }

            /* add number of things we think would have been added from the
             * things that were missed */
            results->total_results += (1 - cooc_rate) * missed;

            /* note that the total results are not an estimate if either there
             * were no accumulators in the list when we started (in which case
             * missed records exactly the number, uh, missing from the
             * accumulators) or there were none missed, in which case the
             * accumulators have fully accounted for everything in this list.
             * In either case, the (1 - cooc_rate) * missed maths above handles
             * it exactly (modulo doubleing point errors of course). */
            if (initial_accs && missed) {
                results->estimated |= 1;
            }

            if (!VEC_LEN(&v)) {
                if (!infinite) {
                    /* continue threshold evaluation */
                    return SEARCH_OK;
                } else {
                    /* switch to AND processing */
                    return SEARCH_FINISH;
                }
            } else {
                return SEARCH_EINVAL;
            }
        } else {
            param = NULL;   /* avoid 'param not used' warning */
            return ret;
        }
    }
}

static enum search_ret thresh_decode_offsets(struct index *idx, 
  struct query *query, unsigned int qterm, unsigned long int docno, 
  struct search_metric_results *results, 
  struct search_list_src *src, unsigned int postings, 
  int opts, struct index_search_opt *opt) {
    /* METRIC_STRUCT */ struct hawkapi_param *param = (void *) &opt->u;
    struct search_acc_cons *acc, dummy, **prevptr = &results->acc;
    unsigned long int f_dt,           /* number of offsets for this document */
                      docno_d;        /* d-gap */

    /* initial number of accumulators */
    unsigned int initial_accs = results->accs,

                 decoded = 0,         /* number of postings decoded */
                 thresh,              /* current discrete threshold */
                 rethresh,            /* distance to recalculation of the threshold */
                 rethresh_dist,
                 bytes,
                 step,
                 missed = 0,          /* number of list entries that didn't match an accumulator */
                 hit = 0;             /* number of entries in both accs and list*/
 
    struct vec v = {NULL, NULL};
    enum search_ret ret = SEARCH_EIO;
    int infinite = 0;                 /* whether threshold is infinite */
    double cooc_rate;
    /* METRIC_DECL */

    const unsigned int N = docmap_entries(idx->map);

    const double w_t = log((N - (query->term[qterm].f_t) + 0.5) / ((query->term[qterm].f_t) + 0.5));

    const double w_qt = (((param->k3) + 1) * (query->term[qterm].f_qt)) / ((param->k3) + (query->term[qterm].f_qt));


    /* METRIC_PER_CALL */

    rethresh_dist = rethresh = (postings + results->acc_limit - 1) 
      / results->acc_limit;

    if (results->v_t == FLT_MIN) {
        unsigned long int docno_copy = docno;

        /* this should be the first thresholded list, need to estimate threshold */
        assert(rethresh && rethresh < postings);
        thresh = 0;

        assert(rethresh < postings);
        while (rethresh) {
            while (rethresh && NEXT_DOC(&v, docno, f_dt)) {
                rethresh--;
                SCAN_OFFSETS(src, &v, f_dt);
                if (f_dt > thresh) {
                    thresh = f_dt;
                }
            }

            /* need to read more data, preserving bytes that we already have */
            if (rethresh && (ret = src->read(src, VEC_LEN(&v),
                (void **) &v.pos, &bytes)) == SEARCH_OK) {

                v.end = v.pos + bytes;
            } else if (rethresh) {
                assert(ret != SEARCH_FINISH);
                return ret;
            }
        }
        thresh--;

        acc = &dummy;
        acc->acc.docno = UINT_MAX;   /* shouldn't be used */
        acc->acc.weight = 0.0;
        f_dt = thresh;
        /* METRIC_CONTRIB */
        (acc->acc.weight) += w_qt * (param->alpha) * log(f_dt + 1) * w_t;

        results->v_t = acc->acc.weight;

        /* reset source/vector to start */
        v.pos = v.end = NULL;
        if ((ret = src->reset(src)) != SEARCH_OK) {
            return ret;
        }

        acc = *prevptr;
        docno = docno_copy;
        rethresh = rethresh_dist;
    } else {
        /* translate the existing v_t threshold to an f_dt */
        acc = &dummy;
        acc->acc.docno = UINT_MAX;   /* shouldn't be used */
        f_dt = 0;
        do {
            acc->acc.weight = 0.0;
            f_dt++;
            /* METRIC_CONTRIB */
            (acc->acc.weight) += w_qt * (param->alpha) * log(f_dt + 1) * w_t;

        } while (acc->acc.weight < results->v_t && f_dt < INF);
        thresh = f_dt; 
        acc = *prevptr;

        if (thresh == INF) {
            /* this is not a sensible term */
            infinite = 1;
            rethresh = postings + 1;
        }
    }

    /* set step to 1/2 of the threshold */
    step = (thresh + 1) / 2;
    step += !step; /* but don't let it become 0 */

    while (1) {
        while (NEXT_DOC(&v, docno, f_dt)) {
            SCAN_OFFSETS(src, &v, f_dt);
            decoded++;

            /* merge into accumulator list */
            while (acc && (docno > acc->acc.docno)) {
                /* perform threshold test */
                if (acc->acc.weight < results->v_t) {
                    /* remove this accumulator */
                    *prevptr = acc->next;
                    objalloc_free(results->alloc, acc);
                    acc = (*prevptr);
                    results->accs--;
                } else {
                    /* retain this accumulator */
                    prevptr = &acc->next;
                    acc = acc->next;
                }
            }

            if (acc && (docno == acc->acc.docno)) {
                /* METRIC_PER_DOC */
                (acc->acc.weight) += w_qt * (param->alpha) * log(f_dt + 1) * w_t;


                if (acc->acc.weight < results->v_t) {
                    /* remove this accumulator */
                    *prevptr = acc->next;
                    objalloc_free(results->alloc, acc);
                    acc = *prevptr;
                    results->accs--;
                } else {
                    /* go to next accumulator */
                    prevptr = &acc->next;
                    acc = acc->next;
                }
                hit++;
            } else {
                if (f_dt > thresh) {
                    struct search_acc_cons *newacc;
                    assert(!acc || docno < acc->acc.docno); 

                    if ((newacc = objalloc_malloc(results->alloc, 
                      sizeof(*newacc)))) {
                        newacc->acc.docno = docno;
                        newacc->acc.weight = 0.0;
                        newacc->next = acc;
                        acc = newacc;
                        /* note that we have to be careful around here to 
                         * assign newacc to acc before using PER_DOC, 
                         * otherwise we end up with nonsense in some 
                         * accumulators */
                        /* METRIC_PER_DOC */
                        (acc->acc.weight) += w_qt * (param->alpha) * log(f_dt + 1) * w_t;

                        *prevptr = newacc;
                        results->accs++;
                    } else {
                        return SEARCH_ENOMEM;
                    }

                    /* go to next accumulator */
                    prevptr = &acc->next;
                    acc = acc->next;
                } else {
                    missed++;
                }
            }

            if (!--rethresh) {
                int estimate,
                    prev_thresh = thresh;

                estimate = results->accs 
                  + ((postings - decoded) 
                    * ((double)results->accs - initial_accs)) / decoded;

                if (estimate > TOLERANCE * results->acc_limit) {
                    thresh += step;
                } else if ((estimate < results->acc_limit / TOLERANCE) 
                  && thresh) {
                    if (thresh >= step) {
                        thresh -= step;
                    } else {
                        thresh = 0;
                    }
                }

                step = (step + 1) / 2;
                assert(step);

                /* note that we don't want to recalculate the threshold if it
                 * doesn't change because this involves re-discretising it */
                if (prev_thresh != thresh) {
                    /* recalculate contribution that corresponds to the new 
                     * threshold */
                    f_dt = thresh;
                    if (f_dt) {
                        acc = &dummy;
                        acc->acc.docno = UINT_MAX;   /* shouldn't be used */
                        acc->acc.weight = 0.0;
                        /* METRIC_CONTRIB */
                        (acc->acc.weight) += w_qt * (param->alpha) * log(f_dt + 1) * w_t;

                        results->v_t = acc->acc.weight;
                        acc = *prevptr;
                    } else {
                        results->v_t = FLT_MIN;
                    }
                }

                rethresh_dist *= 2;
                rethresh = rethresh_dist;
            }
        }

        /* need to read more data, preserving bytes that we already have */
        if ((ret = src->read(src, VEC_LEN(&v),
            (void **) &v.pos, &bytes)) == SEARCH_OK) {

            v.end = v.pos + bytes;
        } else if (ret == SEARCH_FINISH) {
            /* finished, estimate total results count */
            assert(postings == decoded);

            results->total_results += (int) (results->accs - initial_accs);

            /* list entries now divide up into three portions:
             *   - matching an entry in the acc list (hit)
             *   - missed
             *   - added
             *
             * cooccurrance rate is the percentage of list items hit */
            cooc_rate = hit/(double)decoded;

            /* now have sampled co-occurrance rate, use this to estimate 
             * population co-occurrance rate (assuming unbiased sampling) 
             * and then number of results from unrestricted evaluation */
            assert(results->total_results >= results->accs);
            cooc_rate *= results->total_results/(double)results->accs; 
            assert(cooc_rate >= 0.0);
            if (cooc_rate > 1.0) {
                cooc_rate = 1.0;
            }

            /* add number of things we think would have been added from the
             * things that were missed */
            results->total_results += (1 - cooc_rate) * missed;

            /* note that the total results are not an estimate if either there
             * were no accumulators in the list when we started (in which case
             * missed records exactly the number, uh, missing from the
             * accumulators) or there were none missed, in which case the
             * accumulators have fully accounted for everything in this list.
             * In either case, the (1 - cooc_rate) * missed maths above handles
             * it exactly (modulo doubleing point errors of course). */
            if (initial_accs && missed) {
                results->estimated |= 1;
            }

            if (!VEC_LEN(&v)) {
                if (!infinite) {
                    /* continue threshold evaluation */
                    return SEARCH_OK;
                } else {
                    /* switch to AND processing */
                    return SEARCH_FINISH;
                }
            } else {
                return SEARCH_EINVAL;
            }
        } else {
            param = NULL;   /* avoid 'param not used' warning */
            return ret;
        }
    }
}

/* Declare a function named the same as the metric that returns a structure 
 * containing function pointers */
const struct search_metric * /* METRIC_NAME */ hawkapi 
  (struct search_metric *sm, int offsets) {
    sm->pre = pre;
    sm->post = post;
    sm->name = /* METRIC_QUOTED_NAME */ "hawkapi";
    sm->parse_params = parse;
    if (offsets) {
        sm->or_decode = or_decode_offsets;
        sm->and_decode = and_decode_offsets;
        sm->thresh_decode = thresh_decode_offsets;
    } else {
        sm->or_decode = or_decode;
        sm->and_decode = and_decode;
        sm->thresh_decode = thresh_decode;
    }

    return sm;
}

