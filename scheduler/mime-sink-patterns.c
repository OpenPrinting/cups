/*
 * Implementation of printer sink pattern reuse.
 *
 * This cache recognizes when multiple printers share the same MIME filter
 * configuration and reuses the supported format list instead of recomputing
 * it for each printer.The signature includes all filter edges (including
 * printer-specific filters normalized to printer/sink) with their costs,
 * maxsize limits, and program hashes to ensure correct cache sharing.
 * 
 */

#include "cupsd.h"
#include "mime-sink-patterns.h"
#include <stdint.h>

/* FNV-1a hash constants (fast non-cryptographic hash for hash tables) */
#define FNV1A_32_INIT   0x811c9dc5u  /* 2166136261u */
#define FNV1A_32_PRIME  0x01000193u  /* 16777619u */

/* Field separators for hash collision prevention */
#define HASH_SEP_SUPER  0xff  /* Separator after super type */
#define HASH_SEP_TYPE   0xfe  /* Separator after type */

/* Bit masks */
#define UINT32_MASK     0xFFFFFFFFU  /* 32-bit mask for size_t truncation */
#define BYTE_MASK       0xff         /* Byte extraction mask */

/*
 * An edge represents a MIME type conversion filter:
 * from source type (super/type) to destination, with cost and size limits
 */
typedef struct msink_edge_s
{
    const char *super;       /* Source MIME super-type (e.g., "application") */
    const char *type;        /* Source MIME type (e.g., "pdf") */
    int cost;                /* Conversion cost */
    size_t maxsize;          /* Maximum file size for this filter */
    uint32_t prog_hash;      /* Hash of the filter program path */
} msink_edge_t;

/*
 * Cache entry: stores a signature and the list of supported MIME types
 * for printers that share the same filter configuration
 */
typedef struct msink_entry_s
{
    uint32_t sig;                    /* Signature hash of the edge configuration */
    int edge_count;                  /* Number of edges in canonical list */
    msink_edge_t *edges;             /* Canonical sorted edge list */
    cups_array_t *filetypes;         /* Supported MIME types (mime_type_t*) */
    struct msink_entry_s *next;      /* Next entry in hash bucket chain */
} msink_entry_t;

#define MSINK_HT_SIZE 1024

/* Global state */
static msink_entry_t *msink_table[MSINK_HT_SIZE];
static int msink_enabled_checked = 0;
static int msink_enabled = 0;


/*
 * 'msink_env_enabled()' - Check if feature is enabled via environment variable.
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
        
        /* Log feature state once at startup */
        cupsdLogMessage(CUPSD_LOG_INFO, "CUPS_MIME_SINK_REUSE=%s (%s)",
                        env_value ? env_value : "(unset)",
                        msink_enabled ? "enabled" : "disabled");
    }
    
    return msink_enabled;
}


/*
 * 'msink_is_enabled()' - Check if sink pattern reuse is enabled.
 */

int
msink_is_enabled(void)
{
    return msink_env_enabled();
}


/*
 * 'hash_str()' - Hash a string using FNV-1a algorithm.
 */

static uint32_t
hash_str(const char *s)
{
    uint32_t hash = FNV1A_32_INIT;
    unsigned char c;
    
    while (s && (c = (unsigned char)*s++))
    {
        hash ^= c;
        hash *= FNV1A_32_PRIME;
    }
    
    return hash;
}


/*
 * 'edge_cmp()' - Compare two edges for sorting.
 */

static int
edge_cmp(const void *a, const void *b)
{
    const msink_edge_t *edge_a = (const msink_edge_t *)a;
    const msink_edge_t *edge_b = (const msink_edge_t *)b;
    int diff;
    
    /* Compare super type */
    if ((diff = strcmp(edge_a->super, edge_b->super)) != 0)
        return diff;
    
    /* Compare type */
    if ((diff = strcmp(edge_a->type, edge_b->type)) != 0)
        return diff;
    
    /* Compare cost */
    if (edge_a->cost != edge_b->cost)
        return edge_a->cost - edge_b->cost;
    
    /* Compare maxsize */
    if (edge_a->maxsize != edge_b->maxsize)
        return (edge_a->maxsize > edge_b->maxsize) ? 1 : -1;
    
    /* Compare program hash */
    if (edge_a->prog_hash != edge_b->prog_hash)
        return (int)edge_a->prog_hash - (int)edge_b->prog_hash;
    
    return 0;
}


/*
 * 'sig_hash()' - Compute signature hash for edge array using FNV-1a.
 */

static uint32_t
sig_hash(msink_edge_t *edges, int edge_count)
{
    uint32_t hash = FNV1A_32_INIT;
    int i;
    
    for (i = 0; i < edge_count; i++)
    {
        const msink_edge_t *edge = &edges[i];
        const unsigned char *str;
        
        /* Hash the super type string */
        for (str = (const unsigned char *)edge->super; str && *str; str++)
        {
            hash ^= *str;
            hash *= FNV1A_32_PRIME;
        }
        
        /* Field separator to prevent collisions (e.g., "ab"+"c" vs "a"+"bc") */
        hash ^= HASH_SEP_SUPER;
        hash *= FNV1A_32_PRIME;
        
        /* Hash the type string */
        for (str = (const unsigned char *)edge->type; str && *str; str++)
        {
            hash ^= *str;
            hash *= FNV1A_32_PRIME;
        }
        
        /* Field separator */
        hash ^= HASH_SEP_TYPE;
        hash *= FNV1A_32_PRIME;
        
        /* Hash numeric fields: cost, maxsize, prog_hash */
        uint32_t numeric_mix = (uint32_t)edge->cost ^
                               (uint32_t)(edge->maxsize & UINT32_MASK) ^
                               edge->prog_hash;
        
        /* Mix in each byte of the numeric fields */
        int byte;
        for (byte = 0; byte < 4; byte++)
        {
            hash ^= (unsigned char)((numeric_mix >> (byte * 8)) & BYTE_MASK);
            hash *= FNV1A_32_PRIME;
        }
    }
    
    return hash;
}


/*
 * 'edges_equal()' - Compare two edge arrays for equality.
 */

static int
edges_equal(msink_edge_t *a, msink_edge_t *b, int count)
{
    int i;
    
    for (i = 0; i < count; i++)
    {
        if (strcmp(a[i].super, b[i].super) != 0 ||
            strcmp(a[i].type, b[i].type) != 0 ||
            a[i].cost != b[i].cost ||
            a[i].maxsize != b[i].maxsize ||
            a[i].prog_hash != b[i].prog_hash)
        {
            return 0;
        }
    }
    
    return 1;
}


/*
 * 'msink_reuse()' - Try to reuse cached filetypes for a sink.
 */

int
msink_reuse(mime_t *mime, mime_type_t *sink, cups_array_t **out_filetypes)
{
    if (out_filetypes) *out_filetypes = NULL;
    if (!mime || !sink) return 0;

    /* Collect all incoming edges */
    int cap = 8, acnt = 0;
    msink_edge_t *all = (msink_edge_t *)malloc(cap * sizeof(msink_edge_t));
    if (!all) return 0;

    mime_filter_t *flt;
    for (flt = mimeFirstFilter(mime); flt; flt = mimeNextFilter(mime))
    {
        if (flt->dst == sink)
        {
            if (acnt == cap)
            {
                cap *= 2;
                msink_edge_t *ne = (msink_edge_t *)realloc(all, cap * sizeof(msink_edge_t));
                if (!ne)
                {
                    free(all);
                    return 0;
                }
                all = ne;
            }
            all[acnt].super = flt->src->super;
            all[acnt].type = flt->src->type;
            all[acnt].cost = flt->cost;
            all[acnt].maxsize = flt->maxsize;
            all[acnt].prog_hash = hash_str(flt->filter);
            acnt++;
        }
    }
    if (acnt == 0)
    {
        free(all);
        return 0;
    }
    /* Build signature edges (normalize printer/asterix to printer/sink) */
    msink_edge_t *gen = (msink_edge_t *)malloc(acnt * sizeof(msink_edge_t));
    if (!gen)
    {
        free(all);
        return 0;
    }
    int gcnt = 0;
    for (int i = 0; i < acnt; i++)
    {
        gen[gcnt] = all[i];  /* Copy the edge */
        
        /* Normalize printer/asterix sources to printer/sink for signature calculation
         * This ensures printers with different printer-specific filter chains
         * (different costs, programs, or maxsize) get different signatures,
         * while still allowing sharing when the filter behavior is identical. */
        if (!_cups_strcasecmp(all[i].super, "printer"))
        {
            gen[gcnt].super = "printer";
            gen[gcnt].type = "sink";
        }
        
        gcnt++;
    }
    qsort(gen, gcnt, sizeof(msink_edge_t), edge_cmp);
    uint32_t sig = sig_hash(gen, gcnt);
    unsigned bucket = (unsigned)(sig % MSINK_HT_SIZE);
    
    /* Search the hash bucket for a matching entry */
    msink_entry_t *ent;
    for (ent = msink_table[bucket]; ent; ent = ent->next)
    {
        if (ent->edge_count == gcnt && edges_equal(ent->edges, gen, gcnt))
        {
            /* Found a match - copy the cached filetypes */
            if (out_filetypes)
            {
                *out_filetypes = cupsArrayNew(NULL, NULL);
                if (*out_filetypes)
                {
                    mime_type_t *mt;
                    for (mt = (mime_type_t *)cupsArrayFirst(ent->filetypes); 
                         mt; 
                         mt = (mime_type_t *)cupsArrayNext(ent->filetypes))
                    {
                        cupsArrayAdd(*out_filetypes, mt);
                    }
                }
            }
            
            free(gen);
            free(all);
            cupsdLogMessage(CUPSD_LOG_DEBUG2, 
                            "sink-pattern: cache hit signature=%u edges=%d (printer/* normalized)", 
                            sig, gcnt);
            return 1;
        }
    }
    
    /* No match found in cache */
    free(gen);
    free(all);
    return 0;
}


/*
 * 'msink_try_store()' - Store filetypes in cache for future reuse.
 */

void
msink_try_store(mime_t *mime, mime_type_t *sink, cups_array_t *filetypes)
{
    if (!mime || !sink || !filetypes) return;
    
    int cap = 8, cnt = 0;
    msink_edge_t *all = (msink_edge_t *)malloc(cap * sizeof(msink_edge_t));
    if (!all) return;

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
                    return;
                }
                all = ne;
            }
            all[cnt].super = flt->src->super;
            all[cnt].type = flt->src->type;
            all[cnt].cost = flt->cost;
            all[cnt].maxsize = flt->maxsize;
            all[cnt].prog_hash = hash_str(flt->filter);
            cnt++;
        }
    }
    if (cnt == 0)
    {
        free(all);
        return;
    }
    msink_edge_t *gen = (msink_edge_t *)malloc(cnt * sizeof(msink_edge_t));
    if (!gen)
    {
        free(all);
        return;
    }
    int gcnt = 0;
    for (int i = 0; i < cnt; i++)
    {
        gen[gcnt] = all[i];  /* Copy the edge */
        
        /* Normalize printer/asterix sources to printer/sink for signature calculation */
        if (!_cups_strcasecmp(all[i].super, "printer"))
        {
            gen[gcnt].super = "printer";
            gen[gcnt].type = "sink";
        }
        
        gcnt++;
    }
    qsort(gen, gcnt, sizeof(msink_edge_t), edge_cmp);
    uint32_t sig = sig_hash(gen, gcnt);
    unsigned bucket = (unsigned)(sig % MSINK_HT_SIZE);
    
    /* Check if this configuration already exists in the cache */
    msink_entry_t *ent;
    for (ent = msink_table[bucket]; ent; ent = ent->next)
    {
        if (ent->edge_count == gcnt && edges_equal(ent->edges, gen, gcnt))
        {
            /* Already cached - nothing to do */
            free(gen);
            free(all);
            return;
        }
    }
    
    /* Create new cache entry */
    ent = (msink_entry_t *)calloc(1, sizeof(msink_entry_t));
    if (!ent)
    {
        free(gen);
        free(all);
        return;
    }
    ent->sig = sig;
    ent->edge_count = gcnt;
    ent->edges = (msink_edge_t *)malloc(gcnt * sizeof(msink_edge_t));
    if (!ent->edges)
    {
        free(gen);
        free(all);
        free(ent);
        return;
    }
    memcpy(ent->edges, gen, gcnt * sizeof(msink_edge_t));
    free(gen);
    free(all);

    /* retain a copy of filetypes */
    ent->filetypes = cupsArrayNew(NULL, NULL);
    if (ent->filetypes)
    {
        mime_type_t *mt;
        for (mt = (mime_type_t *)cupsArrayFirst(filetypes); mt; mt = (mime_type_t *)cupsArrayNext(filetypes))
            cupsArrayAdd(ent->filetypes, mt);
    }
    ent->next = msink_table[bucket];
    msink_table[bucket] = ent;
    cupsdLogMessage(CUPSD_LOG_INFO, "sink-pattern: store signature=%u edges=%d (printer/* normalized) supported=%d", sig, gcnt, cupsArrayCount(ent->filetypes));
    
    /* Only log detailed edge info if debug level is high enough */
    if (LogLevel >= CUPSD_LOG_DEBUG)
    {
        for (int di = 0; di < gcnt; di++)
        {
            cupsdLogMessage(CUPSD_LOG_DEBUG,
                            "sink-pattern:   edge[%d]: %s/%s cost=%d max=%zu prog_hash=%u",
                            di, ent->edges[di].super, ent->edges[di].type, ent->edges[di].cost,
                            ent->edges[di].maxsize, ent->edges[di].prog_hash);
        }
    }
}


/*
 * 'msink_try_reuse()' - Try to reuse cached filetypes for a printer.
 */

int
msink_try_reuse(cupsd_printer_t *printer)
{
    if (!printer) return 0;
    
    if (!msink_is_enabled()) return 0;

    cups_array_t *reuse_filetypes = NULL;

    /* Try to find cached filetypes for this printer's sink configuration */
    if (msink_reuse(MimeDatabase, printer->filetype, &reuse_filetypes))
    {
        /* Cache hit - set the filetypes, caller will handle IPP attributes */
        printer->filetypes = reuse_filetypes;
        return 1;
    }

    return 0;
}
