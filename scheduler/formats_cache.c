/*
 * formats_cache.c - Runtime-only document format discovery + in‑memory cache
 *
 * This implementation keeps a lightweight cache to avoid repeating
 * mimeFilter() discovery for identical model tuples
 * within a single scheduler lifetime. There is no persistence layer.
 *
 * PUBLIC API (see formats_cache.h):
 *   void fmts_cache_init(void);
 *   void fmts_cache_note_ppd_hash(cupsd_printer_t *p);
 *   int  fmts_cache_populate_for_printer(...);
 *   int  fmts_cache_add_printer_formats(cupsd_printer_t *p);
 *
 * ENABLE/DISABLE:
 *   Set environment variable CUPS_FORMATS_CACHE_OPT=1 (or true/on/yes/enable)
 *   before cupsd start to enable the runtime cache. If not set, the cache
 *   returns -3 (disabled) and callers fall back to legacy enumeration.
 *
 * RETURN CODES (fmts_cache_populate_for_printer):
 *    0  success
 *   -1  invalid args
 *   -2  allocation failure
 *   -3  cache disabled (callers may use legacy path)
 *
 * THREADING:
 *   Designed for single-threaded startup / attribute construction. If
 *   extended to multi-threading, wrap all global state accesses in a lock.
 *
 * LICENSE: Apache 2.0 (aligned with upstream OpenPrinting / CUPS licensing).
 */

#include "cupsd.h"
#include "formats_cache.h"
#include "mime.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define FMTS_CACHE_HASH_SIZE 1024

/* Forward declaration of cupsHashData to avoid implicit declaration
 * (we removed private headers that normally declare it). */
ssize_t cupsHashData(const char *algorithm,
                     const void *data,
                     size_t datalen,
                     unsigned char *hash,
                     size_t hashsize);

/* ---------------------------------------------------------------------------
 * Runtime cache entry
 * ------------------------------------------------------------------------- */
typedef struct _fmts_runtime_entry_s
{
  char         *model_key;        /* make_model or "(unknown)" */
  int           ntypes;           /* number of supported MIME types */
  mime_type_t **types;            /* owned array of pointers (not deep copies) */
} _fmts_runtime_entry_t;

/* ---------------------------------------------------------------------------
 * Globals
 * ------------------------------------------------------------------------- */
static cups_array_t *g_runtime_cache = NULL;
static int           g_cache_enabled = 0;
static int           g_inited        = 0;
static int           g_cache_toggle_logged = 0;
static int           g_canonical_toggle_logged = 0;
/* NOTE:
 * Previous cache key incorporated mimeNumTypes()/mimeNumFilters() counts.
 * Those counts grow as per-printer filters are added, causing every later
 * printer to miss the runtime cache (different counts -> different key).
 * We now stabilize the key to model_key only, eliminating systematic cache
 * misses when adding multiple queues sharing the same model.
 */

/* ---------------------------------------------------------------------------
 * Forward declarations
 * ------------------------------------------------------------------------- */
static int           env_enabled(const char *env);
static int           env_toggle_enabled(const char *name, int *logged_flag);
static void          log_msg(int level, const char *fmt, ...);
static void          hash_ppd_file(const char *path, char hex[65]);

/* Runtime array helpers */
static int           runtime_compare(void *a, void *b, void *d);
static int           runtime_hash(void *e, void *d);
static void          runtime_free(void *e, void *d);
static _fmts_runtime_entry_t *runtime_entry_create(const char *model_key,
                                                   int ntypes,
                                                   mime_type_t **types);
static _fmts_runtime_entry_t *runtime_find(const char *model_key);
static _fmts_runtime_entry_t *runtime_add(const char *model_key,
                                          int ntypes,
                                          mime_type_t **types,
                                          const char *ppd_hash);
static void          runtime_apply_hit(const char *printer_name,
                                       _fmts_runtime_entry_t *entry,
                                       cups_array_t *target,
                                       int *o_used_cache);

static void          array_move_all(cups_array_t *src, cups_array_t *dst);
static double        elapsed_ms(const struct timeval *start,
                                const struct timeval *end);

/* Discovery */
static int discover_formats(cupsd_printer_t *p,
                            cups_array_t **out_list,
                            int *o_filtered_total,
                            double *o_mf_ms);

static int
env_enabled(const char *env)
{
  if (!env || !*env)
    return 0;

  return (!strcasecmp(env, "1")    ||
          !strcasecmp(env, "on")   ||
          !strcasecmp(env, "true") ||
          !strcasecmp(env, "yes")  ||
          !strcasecmp(env, "enable") ||
          !strcasecmp(env, "enabled"));
}

static int
env_toggle_enabled(const char *name, int *logged_flag)
{
  const char *value = getenv(name);
  int enabled = env_enabled(value);

  if (!logged_flag || !*logged_flag)
  {
    const char *disp = (value && *value) ? value : "(unset)";
    log_msg(CUPSD_LOG_INFO, "%s=%s (%s)", name, disp, enabled ? "enabled" : "disabled");
    if (logged_flag)
      *logged_flag = 1;
  }

  return enabled;
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */
void
fmts_cache_canonical_mimetype_hash(cupsd_printer_t *p,
                                   char *dest,
                                   size_t dest_len,
                                   const char *dsuper,
                                   const char *dtype)
{
  if (!p || !dest || dest_len == 0)
    return;

  int enabled = env_toggle_enabled("CUPS_CANONICAL_DEST", &g_canonical_toggle_logged);

  fmts_cache_note_ppd_hash(p);

  if (!p->ppd_sha256[0] || !enabled)
    return;

  const char *super_part = (dsuper && *dsuper) ? dsuper : "unknown";
  const char *type_part  = (dtype  && *dtype)  ? dtype  : "unknown";

  char short_hash[9];
  memcpy(short_hash, p->ppd_sha256, 8);
  short_hash[8] = '\0';

  int written = snprintf(dest, dest_len, "_ppd_%s/%s/%s",
                         short_hash, super_part, type_part);
  if (written < 0 || (size_t)written >= dest_len)
  {
    dest[dest_len - 1] = '\0';
    log_msg(CUPSD_LOG_ERROR, "[canonical] destination truncated for %s (len=%zu)", p->name ? p->name : "(unknown)", dest_len);
  }
  else
  {
  log_msg(CUPSD_LOG_DEBUG, "[canonical] add_printer_filter: canonical shared dest=%s", dest);
  }
}


void
fmts_cache_init(void)
{
  if (g_inited)
    return;

  g_cache_enabled = env_toggle_enabled("CUPS_FORMATS_CACHE_OPT", &g_cache_toggle_logged);
  env_toggle_enabled("CUPS_CANONICAL_DEST", &g_canonical_toggle_logged);

  if (g_cache_enabled)
  {
    g_runtime_cache = cupsArrayNew3(runtime_compare,
                                    NULL,
                                    runtime_hash,
                                    FMTS_CACHE_HASH_SIZE,
                                    NULL,
                                    runtime_free);
    if (!g_runtime_cache)
    {
      log_msg(CUPSD_LOG_ERROR, "runtime cache allocation failed – disabling formats cache");
      g_cache_enabled = 0;
    }
  }

  g_inited = 1;
}

void
fmts_cache_note_ppd_hash(cupsd_printer_t *p)
{
  if (!p)
    return;

  /* Only hash if not already present (avoid repeated IO). */
  if (p->ppd_sha256[0])
    return;

  if (!ServerRoot || !*ServerRoot)
    return;

  if (!p->name || !*p->name)
    return;

  char ppd_path[1024];
  int written = snprintf(ppd_path, sizeof(ppd_path), "%s/ppd/%s.ppd",
                         ServerRoot, p->name);
  if (written < 0 || (size_t)written >= sizeof(ppd_path))
  {
    log_msg(CUPSD_LOG_ERROR, "Unable to build PPD path for %s (len=%zu)", p->name ? p->name : "(unknown)", sizeof(ppd_path));
    return;
  }

  hash_ppd_file(ppd_path, p->ppd_sha256);
  if (p->ppd_sha256[0])
    log_msg(CUPSD_LOG_DEBUG, "PPD hash for %s = %s", p->name, p->ppd_sha256);
}

int
fmts_cache_populate_for_printer(cupsd_printer_t *p,
                                cups_array_t **out_filetypes,
                                int mime_types_count,
                                int mime_filters_count,
                                int *o_used_cache,
                                int *o_filtered_total,
                                double *o_total_ms,
                                double *o_mimefilter_ms)
{
  if (o_used_cache)      *o_used_cache = 0;
  if (o_filtered_total)  *o_filtered_total = 0;
  if (o_total_ms)        *o_total_ms = 0.0;
  if (o_mimefilter_ms)   *o_mimefilter_ms = 0.0;

  if (!p || !out_filetypes || mime_types_count < 0 || mime_filters_count < 0)
    return -1;

  fmts_cache_init();
  if (!g_cache_enabled)
    return -3;

  struct timeval tv0, tv1;
  gettimeofday(&tv0, NULL);

  *out_filetypes = cupsArrayNew(NULL, NULL);
  if (!*out_filetypes)
    return -2;

  const char *model_key = (p->make_model && *p->make_model) ? p->make_model : "(unknown)";

  /* Runtime cache lookup */
  _fmts_runtime_entry_t *re = runtime_find(model_key);
  if (re)
    runtime_apply_hit(p->name, re, *out_filetypes, o_used_cache);

  /* Discovery if still empty */
  if (cupsArrayCount(*out_filetypes) == 0)
  {
    double mf_ms = 0.0;
    int filtered_total = 0;
    cups_array_t *discovered = NULL;
    int rc = discover_formats(p, &discovered,
                              &filtered_total, &mf_ms);
    if (rc == 0 && discovered)
    {
      array_move_all(discovered, *out_filetypes);

      int ntypes = cupsArrayCount(*out_filetypes);
      if (ntypes > 0)
      {
        mime_type_t **array = calloc((size_t)ntypes, sizeof(mime_type_t *));
        if (array)
        {
          int idx = 0;
          mime_type_t *it;
          for (it = (mime_type_t *)cupsArrayFirst(*out_filetypes);
               it;
               it = (mime_type_t *)cupsArrayNext(*out_filetypes))
            array[idx++] = it;

          if (!runtime_add(model_key, ntypes, array, p->ppd_sha256))
            free(array);
        }
      }

      if (o_mimefilter_ms)  *o_mimefilter_ms = mf_ms;
      if (o_filtered_total) *o_filtered_total = filtered_total;
    }
    if (discovered)
      cupsArrayDelete(discovered);
  }

  gettimeofday(&tv1, NULL);
  if (o_total_ms)
    *o_total_ms = elapsed_ms(&tv0, &tv1);

  return 0;
}

int
fmts_cache_add_printer_formats(cupsd_printer_t *p)
{
  if (!p)
    return -1;

  fmts_cache_note_ppd_hash(p);

  if (p->raw)
    return -2; /* let caller handle raw legacy path */

  cups_array_t *filetypes = NULL;
  int used_cache = 0, filtered_total = 0;
  double total_ms = 0.0, mf_ms = 0.0;

  int rc = fmts_cache_populate_for_printer(p, &filetypes,
                                           mimeNumTypes(MimeDatabase),
                                           mimeNumFilters(MimeDatabase),
                                           &used_cache,
                                           &filtered_total,
                                           &total_ms,
                                           &mf_ms);
  if (rc < 0)
    return rc; /* fallback to legacy path in caller */

  /* Replace prior list */
  if (p->filetypes)
    cupsArrayDelete(p->filetypes);
  p->filetypes = filetypes;

  /* Remove prior attributes that we rebuild */
  ipp_attribute_t *old;
  if ((old = ippFindAttribute(p->attrs, "document-format-supported", IPP_TAG_MIMETYPE)))
    ippDeleteAttribute(p->attrs, old);
  if ((old = ippFindAttribute(p->attrs, "document-format-preferred", IPP_TAG_ZERO)))
    ippDeleteAttribute(p->attrs, old);

  /* Add application/octet-stream if not already in set */
  int add_octet = 0;
  mime_type_t *octet = mimeType(MimeDatabase, "application", "octet-stream");
  if (!octet || !cupsArrayFind(p->filetypes, octet))
    add_octet = 1;

  const char *preferred = "image/urf";

  if (add_octet)
    ippAddString(p->attrs, IPP_TAG_PRINTER, IPP_TAG_MIMETYPE,
                 "document-format-supported", NULL, "application/octet-stream");

  mime_type_t *t;
  for (t = (mime_type_t *)cupsArrayFirst(p->filetypes);
       t;
       t = (mime_type_t *)cupsArrayNext(p->filetypes))
  {
    char mt[256];
    snprintf(mt, sizeof(mt), "%s/%s", t->super, t->type);
    ippAddString(p->attrs, IPP_TAG_PRINTER, IPP_TAG_MIMETYPE,
                 "document-format-supported", NULL, mt);
    if (!strcasecmp(mt, "application/pdf"))
      preferred = "application/pdf";
  }

  ippAddString(p->attrs, IPP_TAG_PRINTER, IPP_TAG_MIMETYPE,
               "document-format-preferred", NULL, preferred);

  int total_supported = cupsArrayCount(p->filetypes) + add_octet;

  log_msg(CUPSD_LOG_DEBUG, "%s supported=%d cache=%s filtered=%d total=%.3fms mimeFilter=%.3fms", 
                          p->name ? p->name : "(unknown)", total_supported, used_cache ? "hit" : "miss", filtered_total, total_ms, mf_ms);

  return 0;
}

/* ---------------------------------------------------------------------------
 * Utility helpers
 * ------------------------------------------------------------------------- */
static void
array_move_all(cups_array_t *src, cups_array_t *dst)
{
  if (!src || !dst)
    return;

  void *element;
  while ((element = cupsArrayFirst(src)) != NULL)
  {
    cupsArrayRemove(src, element);
    cupsArrayAdd(dst, element);
  }
}

static double
elapsed_ms(const struct timeval *start, const struct timeval *end)
{
  if (!start || !end)
    return 0.0;

  return (end->tv_sec - start->tv_sec) * 1000.0 +
         (end->tv_usec - start->tv_usec) / 1000.0;
}

/* ---------------------------------------------------------------------------
 * Discovery (runtime)
 * ------------------------------------------------------------------------- */
static int
discover_formats(cupsd_printer_t *p,
                 cups_array_t **out_list,
                 int *o_filtered_total,
                 double *o_mf_ms)
{
  if (!p || !out_list)
    return -1;

  *out_list = cupsArrayNew(NULL, NULL);
  if (!*out_list)
    return -2;

  /* Raw printers: nothing to do here (legacy path handles). */
  if (p->raw)
    return 0;

  int filtered_total = 0;
  double mf_sum = 0.0;

  mime_type_t *type;
  for (type = mimeFirstType(MimeDatabase);
       type;
       type = mimeNextType(MimeDatabase))
  {
    if (!strcasecmp(type->super, "printer"))
      continue;

    struct timeval t0, t1;
    gettimeofday(&t0, NULL);
    cups_array_t *filters = mimeFilter(MimeDatabase, type, p->filetype, NULL);
    gettimeofday(&t1, NULL);
    mf_sum += elapsed_ms(&t0, &t1);
    filtered_total++;

    if (filters)
    {
      cupsArrayDelete(filters);
      cupsArrayAdd(*out_list, type);
    }
  }

  if (o_filtered_total) *o_filtered_total = filtered_total;
  if (o_mf_ms)          *o_mf_ms         = mf_sum;
  return 0;
}

/* ---------------------------------------------------------------------------
 * PPD hashing helper
 * ------------------------------------------------------------------------- */
static void
hash_ppd_file(const char *path, char hex[65])
{
  hex[0] = '\0';

  int fd = open(path, O_RDONLY);
  if (fd < 0)
    return;

  cups_file_t *fp = cupsFileOpenFd(fd, "r");
  if (!fp)
  {
    close(fd);
    return;
  }

  unsigned char sha[32];
  char *accum = NULL;
  size_t asize = 0;
  char buffer[8192];
  ssize_t n;

  while ((n = cupsFileRead(fp, buffer, (int)sizeof(buffer))) > 0)
  {
    char *nb = realloc(accum, asize + (size_t)n);
    if (!nb)
    {
      free(accum);
      cupsFileClose(fp);
      close(fd);
      return;
    }
    accum = nb;
    memcpy(accum + asize, buffer, (size_t)n);
    asize += (size_t)n;
  }

  cupsFileClose(fp);
  close(fd);

  if (asize > 0 &&
      cupsHashData("sha2-256", accum, (int)asize, sha, sizeof(sha)) > 0)
  {
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++)
    {
      hex[i*2]   = hx[sha[i] >> 4];
      hex[i*2+1] = hx[sha[i] & 15];
    }
    hex[64] = '\0';
  }

  free(accum);
}

/* ---------------------------------------------------------------------------
 * Runtime cache helpers
 * ------------------------------------------------------------------------- */
static int
runtime_compare(void *a, void *b, void *d)
{
  (void)d;
  _fmts_runtime_entry_t *ea = (_fmts_runtime_entry_t *)a;
  _fmts_runtime_entry_t *eb = (_fmts_runtime_entry_t *)b;
  return strcmp(ea->model_key, eb->model_key);
}

static int
runtime_hash(void *e, void *d)
{
  (void)d;
  const _fmts_runtime_entry_t *re = (const _fmts_runtime_entry_t *)e;
  if (!re)
    return 0;

  unsigned int hash = 2166136261u; /* FNV-1a */

  const unsigned char *sp = (const unsigned char *)(re->model_key ? re->model_key : "");
  while (*sp)
  {
    hash ^= *sp++;
    hash *= 16777619u;
  }

  return (int)(hash % FMTS_CACHE_HASH_SIZE);
}

static void
runtime_free(void *e, void *d)
{
  (void)d;
  _fmts_runtime_entry_t *re = (_fmts_runtime_entry_t *)e;
  if (!re) return;
  free(re->model_key);
  free(re->types);
  free(re);
}

static _fmts_runtime_entry_t *
runtime_entry_create(const char *model_key,
                     int ntypes,
                     mime_type_t **types)
{
  _fmts_runtime_entry_t *entry = calloc(1, sizeof(_fmts_runtime_entry_t));
  if (!entry)
    return NULL;

  entry->model_key = strdup(model_key);
  if (!entry->model_key)
  {
    free(entry);
    return NULL;
  }

  entry->ntypes = ntypes;
  entry->types  = types;
  return entry;
}

static _fmts_runtime_entry_t *
runtime_find(const char *model_key)
{
  if (!g_runtime_cache)
    return NULL;
  _fmts_runtime_entry_t needle;
  memset(&needle, 0, sizeof(needle));
  needle.model_key = (char *)model_key;
  return (_fmts_runtime_entry_t *)cupsArrayFind(g_runtime_cache, &needle);
}

static _fmts_runtime_entry_t *
runtime_add(const char *model_key,
            int ntypes,
            mime_type_t **types,
            const char *ppd_hash)
{
  if (!g_runtime_cache)
    return NULL;

  _fmts_runtime_entry_t *re = runtime_entry_create(model_key, ntypes, types);
  if (!re)
    return NULL;

  if (!cupsArrayAdd(g_runtime_cache, re))
  {
    re->types = NULL;
    runtime_free(re, NULL);
    return NULL;
  }
  const char *hash_disp = (ppd_hash && *ppd_hash) ? ppd_hash : "(none)";
  log_msg(CUPSD_LOG_INFO, "runtime_add model='%s' ntypes=%d ppd_sha256=%s", model_key, ntypes, hash_disp);
  return re;
}

static void
runtime_apply_hit(const char *printer_name,
                  _fmts_runtime_entry_t *entry,
                  cups_array_t *target,
                  int *o_used_cache)
{
  if (!entry || !target)
    return;

  for (int i = 0; i < entry->ntypes; i++)
    if (entry->types[i])
      cupsArrayAdd(target, entry->types[i]);

  if (o_used_cache)
    *o_used_cache = 1;
}

/* ---------------------------------------------------------------------------
 * Logging
 * ------------------------------------------------------------------------- */
static void
log_msg(int level, const char *fmt, ...)
{
  if (LogLevel < level)
    return;

  va_list ap;
  va_start(ap, fmt);
  char buf[1024];
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  cupsdLogMessage(level, "[formats-cache] %s", buf);
}

/* ---------------------------------------------------------------------------
 * End of file
 * ------------------------------------------------------------------------- */