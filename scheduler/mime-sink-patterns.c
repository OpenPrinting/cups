/*
 * cupsArray-based implementation of printer sink pattern reuse.
 *
 * This cache detects when multiple printers share the same MIME filter
 * configuration and reuses the supported format list instead of
 * recomputing it for each printer. A 64-bit signature is computed over a
 * sorted list of filter "edges" (printer-specific filters are normalized
 * to "printer/sink"); the signature mixes in the edge super/type, cost,
 * maxsize and a program hash to compactly represent the filter graph.
 *
 * The helper build_msink_entry() constructs an heap-allocated
 * msink_entry_t containing edge_count and signature (filetypes is left
 * NULL). Callers attach the `filetypes` cups_array_t* and then add the
 * entry to the global `msink_arr` cupsArray. The array uses copy/free
 * callbacks (msink_arr_copy/msink_arr_free) to duplicate and manage the
 * stored entries and their filetypes. After insertion we verify the add
 * by finding the stored copy and log success or error.
 *
 * The compare/hash callbacks are intentionally lightweight and use the
 * edge_count and signature for fast lookup; extremely unlikely
 * signature collisions are accepted as a trade-off for performance.
 */


#include "cupsd.h"
#include "mime-sink-patterns.h"
#include <cups/array.h>
#include <stdint.h>
#include <string.h>

/* FNV-1a 64-bit constants */
#define FNV1A_64_INIT   0xcbf29ce484222325ULL
#define FNV1A_64_PRIME  0x00000100000001B3ULL

/* Field separator byte values used when mixing fields into the signature
 * Use UINT64_C to ensure they have 64-bit type without needing casts. */
#define FNV1A_64_SEP_SUPER UINT64_C(0xFF)
#define FNV1A_64_SEP_TYPE  UINT64_C(0xFE)
#define FNV1A_64_SEP_PROG  UINT64_C(0xFD)

/* FNV-1a hash constants */
#define FNV1A_32_INIT   0x811c9dc5u
#define FNV1A_32_PRIME  0x01000193u

/* Masks */
#define UINT8_MASK      0xFFu
#define UINT32_MASK     0xFFFFFFFFU

/* Hash table size used for cupsArrayNew2/3 */
#define MSINK_ARR_HASH_SIZE 1024

/* Edge structure (same shape as the original file-local type) */
typedef struct msink_edge_s
{
    const char *super;
    const char *type;
    int cost;
    size_t maxsize;
    const char *prog;
} msink_edge_t;

/* Entry stored in the cupsArray cache */
typedef struct msink_entry_s
{
    int edge_count;            /* edge count per filetype used */
    uint64_t signature;        /* compact signature for the sorted edge list */
    cups_array_t *filetypes;   /* mime_type_t* elements */
} msink_entry_t;

/* Global cups array for cache entries (lazily created) */
static cups_array_t *msink_arr = NULL;
static int msink_enabled_checked = 0;
static int msink_enabled = 0;

/* Forward declarations for cupsArray callbacks */
static int  msink_arr_compare(void *a, void *b, void *user_data);
static int  msink_arr_hash(void *elem, void *user_data);
static void *msink_arr_copy(void *element, void *user_data);
static void msink_arr_free(void *element, void *user_data);

/*
 * msink_env_enabled() - read CUPS_MIME_SINK_REUSE environment switch
 */
static int
msink_env_enabled(void)
{
    if (!msink_enabled_checked)
    {
        const char *env_value = getenv("CUPS_MIME_SINK_REUSE");
        if (env_value &&
            (!_cups_strcasecmp(env_value, "1") ||
             !_cups_strcasecmp(env_value, "yes") ||
             !_cups_strcasecmp(env_value, "true") ||
             !_cups_strcasecmp(env_value, "on")))
        {
            msink_enabled = 1;
        }

        msink_enabled_checked = 1;

        cupsdLogMessage(CUPSD_LOG_INFO, "CUPS_MIME_SINK_REUSE=%s (%s)",
                        env_value ? env_value : "(unset)",
                        msink_enabled ? "enabled" : "disabled");
    }

    return msink_enabled;
}

/*
 * msink_is_enabled() - public wrapper that returns whether feature is enabled
 */
int
msink_is_enabled(void)
{
    return msink_env_enabled();
}


/*
 * edge_cmp() - compare two msink_edge_t entries for sorting
 */
static int
edge_cmp(const void *a, const void *b)
{
    const msink_edge_t *edge_a = (const msink_edge_t *)a;
    const msink_edge_t *edge_b = (const msink_edge_t *)b;
    int diff;

    if ((diff = strcmp(edge_a->super, edge_b->super)) != 0)
        return diff;
    if ((diff = strcmp(edge_a->type, edge_b->type)) != 0)
        return diff;
    if (edge_a->cost != edge_b->cost)
        return edge_a->cost - edge_b->cost;
    if (edge_a->maxsize != edge_b->maxsize)
        return (edge_a->maxsize > edge_b->maxsize) ? 1 : -1;
    {
        const char *pa = edge_a->prog ? edge_a->prog : "";
        const char *pb = edge_b->prog ? edge_b->prog : "";
        if ((diff = strcmp(pa, pb)) != 0)
            return diff;
    }
    return 0;
}

/*
 * compute_edges_signature() - compute 64-bit FNV-1a signature for edges
 */
static uint64_t
compute_edges_signature(const msink_edge_t *edges, int count)
{
    uint64_t h = FNV1A_64_INIT;
    for (int i = 0; i < count; i++)
    {
        const msink_edge_t *e = &edges[i];

        /* Mix in super and a separator */
        for (const unsigned char *s = (const unsigned char *)e->super; s && *s; s++)
        {
            h ^= (uint64_t)*s;
            h *= FNV1A_64_PRIME;
        }
        h ^= FNV1A_64_SEP_SUPER;
        h *= FNV1A_64_PRIME;

        /* Mix in type and separator */
        for (const unsigned char *s = (const unsigned char *)e->type; s && *s; s++)
        {
            h ^= (uint64_t)*s;
            h *= FNV1A_64_PRIME;
        }
        h ^= FNV1A_64_SEP_TYPE;
        h *= FNV1A_64_PRIME;

        /* Mix in numeric fields deterministically */
        uint64_t mix = (uint64_t)(uint32_t)e->cost;
        mix = (mix << 32) ^ (uint64_t)(e->maxsize & UINT32_MASK);
        for (int b = 0; b < 4; b++)
        {
            unsigned char byte = (unsigned char)((mix >> (b*8)) & UINT8_MASK);
            h ^= (uint64_t)byte;
            h *= FNV1A_64_PRIME;
        }

        /* Mix in program string (if any) and separator */
        if (e->prog)
        {
            for (const unsigned char *s = (const unsigned char *)e->prog; s && *s; s++)
            {
                h ^= (uint64_t)*s;
                h *= FNV1A_64_PRIME;
            }
        }
        h ^= FNV1A_64_SEP_PROG;
        h *= FNV1A_64_PRIME;
    }
    return h;
}

/*
 * build_msink_entry() - construct a temporary msink_entry_t with signature
 *
 * Allocates and returns a heap msink_entry_t with edge_count and signature
 * populated. filetypes is left NULL and callers must free the returned
 * entry when finished.
 */
static msink_entry_t *
build_msink_entry(mime_t *mime, mime_type_t *sink)
{
    if (!mime || !sink) return NULL;

    int cap = 8, cnt = 0;
    msink_edge_t *all = (msink_edge_t *)malloc(cap * sizeof(msink_edge_t));
    if (!all) return NULL;

    mime_filter_t *flt;
    for (flt = mimeFirstFilter(mime); flt; flt = mimeNextFilter(mime))
    {
        if (flt->dst == sink)
        {
            if (cnt == cap)
            {
                cap *= 2;
                msink_edge_t *ne = (msink_edge_t *)realloc(all, cap * sizeof(msink_edge_t));
                if (!ne)
                {
                    free(all);
                    return NULL;
                }
                all = ne;
            }
            all[cnt].super = flt->src->super;
            all[cnt].type = flt->src->type;
            all[cnt].cost = flt->cost;
            all[cnt].maxsize = flt->maxsize;
            all[cnt].prog = flt->filter;
            cnt++;
        }
    }
    if (cnt == 0)
    {
        free(all);
        return NULL;
    }

    msink_edge_t *gen = (msink_edge_t *)malloc(cnt * sizeof(msink_edge_t));
    if (!gen)
    {
        free(all);
        return NULL;
    }
    int gcnt = 0;
    for (int i = 0; i < cnt; i++)
    {
        gen[gcnt] = all[i];
        if (!_cups_strcasecmp(all[i].super, "printer"))
        {
            gen[gcnt].super = "printer";
            gen[gcnt].type = "sink";
        }
        gcnt++;
    }
    qsort(gen, gcnt, sizeof(msink_edge_t), edge_cmp);

    uint64_t sig = compute_edges_signature(gen, gcnt);

    free(all);
    free(gen);

    msink_entry_t *entry = (msink_entry_t *)calloc(1, sizeof(msink_entry_t));
    if (!entry) return NULL;
    entry->edge_count = gcnt;
    entry->signature = sig;
    entry->filetypes = NULL;
    return entry;
}


/* cupsArray msink_arr ---------------------------------------------------- */

/*
 * msink_arr_init() - lazy initializer for the global msink_arr
 */
static void
msink_arr_init(void)
{
    if (!msink_arr)
        msink_arr = cupsArrayNew3((cups_array_func_t)msink_arr_compare, NULL,
                                 (cups_ahash_func_t)msink_arr_hash, MSINK_ARR_HASH_SIZE,
                                 (cups_acopy_func_t)msink_arr_copy, (cups_afree_func_t)msink_arr_free);
}

/* cupsArray callbacks ---------------------------------------------------- */

/*
 * msink_arr_compare() - cupsArray compare callback for msink entries
 */
static int
msink_arr_compare(void *a, void *b, void *user_data)
{
    msink_entry_t *ea = (msink_entry_t *)a;
    msink_entry_t *eb = (msink_entry_t *)b;

    if (ea->edge_count < eb->edge_count) return -1;
    if (ea->edge_count > eb->edge_count) return 1;

    /* Fast path: compare signatures */
    if (ea->signature < eb->signature) return -1;
    if (ea->signature > eb->signature) return 1;

    /* Signatures equal and edge_count equal -> treat as equal.
     * If an extremely unlikely collision occurs this will treat as equal.
     */
    return 0;
}

/*
* msink_arr_hash() - cupsArray hash callback (reduce 64-bit sig to bucket)
*
* Use the precomputed 64-bit signature when available. 
* Reduce it to the hash table size.
*/
static int
msink_arr_hash(void *elem, void *user_data)
{
    msink_entry_t *e = (msink_entry_t *)elem;

    /* Fold 64-bit signature to 32-bit and modulo table size */
    uint64_t sig = e->signature;
    uint32_t folded = (uint32_t)(sig ^ (sig >> 32));
    return (int)(folded % MSINK_ARR_HASH_SIZE);
}

/*
 * msink_arr_copy() - cupsArray copy callback for msink_entry_t
 */
static void *
msink_arr_copy(void *element, void *user_data)
{
    msink_entry_t *src = (msink_entry_t *)element;
    msink_entry_t *dst = (msink_entry_t *)calloc(1, sizeof(msink_entry_t));
    if (!dst) return NULL;
    dst->edge_count = src->edge_count;
    dst->signature = src->signature;

    dst->filetypes = src->filetypes ? cupsArrayDup(src->filetypes) : NULL;

    return dst;
}

/*
 * msink_arr_free() - cupsArray free callback for msink_entry_t
 */
static void
msink_arr_free(void *element, void *user_data)
{
    msink_entry_t *e = (msink_entry_t *)element;
    if (!e) return;
    if (e->filetypes) cupsArrayDelete(e->filetypes);
    free(e);
}

/* Private API ---------------------------------------------------- */

/*
 * msink_reuse() - try to reuse a cached filetypes list for a sink
 */
int
msink_reuse(mime_t *mime, mime_type_t *sink, cups_array_t **out_filetypes)
{
    if (out_filetypes) *out_filetypes = NULL;
    if (!mime || !sink) return 0;

    /* Build an msink_entry for lookup */
    msink_entry_t *lookup_sink = build_msink_entry(mime, sink);
    if (!lookup_sink) return 0;

    /* Ensure array exists */
    msink_arr_init();

    msink_entry_t *found = (msink_entry_t *)cupsArrayFind(msink_arr, lookup_sink);
    free(lookup_sink);
    
    if (found)
    {
        if (out_filetypes && found->filetypes)
            *out_filetypes = cupsArrayDup(found->filetypes);

        cupsdLogMessage(CUPSD_LOG_DEBUG2,
                        "sink-pattern-arr: cache hit edges=%d (printer/* normalized)",
                        found->edge_count);
        return 1;
    }
    return 0;
}

/* Public API ---------------------------------------------------- */

/*
 * msink_try_store() - try to store a filetypes list in the cache
 */
void
msink_try_store(mime_t *mime, mime_type_t *sink, cups_array_t *filetypes)
{
    if (!mime || !sink || !filetypes) return;
    if (!msink_is_enabled()) return;
    
    /* Generating the edge signature */
    msink_entry_t *lookup_sink = build_msink_entry(mime, sink);
    if (!lookup_sink) return;

    msink_arr_init();

    /* Sanity, nothing to do if aleady exists */
    if (cupsArrayFind(msink_arr, lookup_sink))
    {
        free(lookup_sink);
        return;
    }

    /* Assign caller filetypes and add - copy callback will duplicate */
    lookup_sink->filetypes = filetypes;
    cupsArrayAdd(msink_arr, lookup_sink);

    /* Verify insertion succeeded by finding the stored copy and log accordingly */
    {
        msink_entry_t *found = (msink_entry_t *)cupsArrayFind(msink_arr, lookup_sink);
        if (found)
        {
            cupsdLogMessage(CUPSD_LOG_INFO, "sink-pattern-arr: store edges=%d supported=%d",
                            lookup_sink->edge_count, cupsArrayCount(found->filetypes));
        }
        else
        {
            cupsdLogMessage(CUPSD_LOG_ERROR, "sink-pattern-arr: failed to store edges=%d",
                            lookup_sink->edge_count);
        }
    }

    lookup_sink->filetypes = NULL;
    free(lookup_sink);
}

/*
 * msink_try_reuse() - public helper to attempt reusing sink filetypes
 */
int
msink_try_reuse(cupsd_printer_t *printer)
{
    if (!printer) return 0;
    if (!msink_is_enabled()) return 0;

    cups_array_t *reuse_filetypes = NULL;
    if (msink_reuse(MimeDatabase, printer->filetype, &reuse_filetypes))
    {
        printer->filetypes = reuse_filetypes;
        return 1;
    }
    return 0;
}