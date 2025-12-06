/*
 * formats_cache.h - Runtime document format support cache (no persistence).
 *
 * Lightweight runtime (in‑memory) cache + helper functions remain.
 * The intent is to avoid complexity and I/O overhead while still de-duplicating
 * mimeFilter() discovery work within a single scheduler process lifetime.
 *
 * REMAINING RUNTIME-ONLY API:
 *   - fmts_cache_init()
 *   - fmts_cache_note_ppd_hash()
 *   - fmts_cache_populate_for_printer()
 *   - fmts_cache_add_printer_formats()
 *
 * USAGE NOTES
 * -----------
 * 1. Call fmts_cache_init() once early (idempotent).
 * 2. During PPD load, call fmts_cache_note_ppd_hash() (optional; allows
 *    per-PPD grouping in the runtime layer if implemented).
 * 3. When building printer attributes, call fmts_cache_add_printer_formats().
 *    If it returns 0 the cache logic succeeded. If it returns <0 fall back
 *    to legacy enumeration (the helper already does that internally in the
 *    current implementation, but callers should treat non-zero as fallback
 *    if future refactors change behavior).
 *
 * THREADING
 * ---------
 * Still assumed single-threaded during startup. If multi-threaded usage
 * is introduced later, a locking layer must be added around internal
 * structures (implementation file).
 *
 * ENVIRONMENT VARIABLES (MAY STILL BE HONORED)
 * --------------------------------------------
 * LICENSE
 * -------
 *   Apache 2.0 (aligned with the rest of the OpenPrinting / CUPS code).
 */

#ifndef CUPS_FORMATS_CACHE_H
#define CUPS_FORMATS_CACHE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations: require that any .c including this header
 * already pulled in cupsd.h so that cupsd_printer_t, cups_array_t,
 * mime_type_t, etc. are defined. We intentionally do not #include
 * cupsd.h here to avoid circular includes or macro side-effects.
 */


/*
 * API: Canonical MIME destination helper.
 * When CUPS_CANONICAL_DEST is enabled and the printer has a PPD hash,
 * rewrites the destination string in-place to use the shared namespace
 * "_ppd_<hash8>/<super>/<type>". The caller provides the current
 * destination buffer and its size. The function is a no-op when inputs
 * are incomplete or the feature toggle is disabled.
 */
void fmts_cache_canonical_mimetype_hash(cupsd_printer_t *p,
                                        char *dest,
                                        size_t dest_len,
                                        const char *dsuper,
                                        const char *dtype);

/*
 * API: Initialize the runtime formats cache subsystem (idempotent).
 * Safe to call multiple times; subsequent calls are no-ops.
 */
void fmts_cache_init(void);

/*
 * API: Compute + store the SHA256 hash of the printer's PPD
 * (ServerRoot/ppd/<name>.ppd) in p->ppd_sha256. If hashing fails
 * (file missing/unreadable), the field remains empty. Callers may
 * invoke this opportunistically to warm the hash; other helpers in
 * this module will call it on-demand as needed.
 */
void fmts_cache_note_ppd_hash(cupsd_printer_t *p);

/*
 * Low-level population entry point:
 *   - Discovers supported MIME types for printer 'p' given current global
 *     MIME database (MimeDatabase).
 *   - May reuse the internal runtime cache keyed by model.
 *
 * Parameters:
 *   p                  (IN)  Printer
 *   out_filetypes      (OUT) Newly allocated cups_array_t* of mime_type_t*
 *   mime_types_count   (IN)  Snapshot of mimeNumTypes(MimeDatabase)
 *   mime_filters_count (IN)  Snapshot of mimeNumFilters(MimeDatabase)
 *   o_used_cache       (OUT, opt) 1 if runtime cache used, else 0
 *   o_filtered_total   (OUT, opt) Number of candidate MIME types tested
 *   o_total_ms         (OUT, opt) Total elapsed time in ms
 *   o_mimefilter_ms    (OUT, opt) Summed time spent in mimeFilter() calls
 *
 * Returns:
 *    0  success
 *   -1  invalid arguments
 *   -2  allocation failure
 *   -3  (reserved / feature disabled - legacy fallback recommended)
 */
int fmts_cache_populate_for_printer(cupsd_printer_t *p,
                                    cups_array_t **out_filetypes,
                                    int mime_types_count,
                                    int mime_filters_count,
                                    int *o_used_cache,
                                    int *o_filtered_total,
                                    double *o_total_ms,
                                    double *o_mimefilter_ms);

/*
 * High-level helper:
 * Builds document-format-supported, document-format-preferred (and PDL if
 * applicable) for the printer using the runtime cache logic. If the cache
 * logic cannot proceed it returns a negative value and the caller may
 * choose to apply legacy/manual enumeration (current implementation
 * already integrates fallback internally, but callers should not rely
 * on that for future revisions).
 *
 * Returns:
 *   0  success
 *  <0  failure / fallback path signaled
 */
int fmts_cache_add_printer_formats(cupsd_printer_t *p);


#ifdef __cplusplus
}
#endif

#endif /* CUPS_FORMATS_CACHE_H */