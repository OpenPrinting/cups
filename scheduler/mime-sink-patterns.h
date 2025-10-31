/*
 * Printer sink pattern reuse (incoming filter signature) for CUPS scheduler.
 *
 * This cache recognizes when multiple printers share the same MIME filter
 * configuration and reuses the supported format list instead of recomputing
 * it for each printer. The signature includes all filter edges (including
 * printer-specific filters normalized to printer/sink) with their costs,
 * maxsize limits, and program hashes to ensure correct cache sharing.
 *
 */

#ifndef _CUPS_MIME_SINK_PATTERNS_H_
#define _CUPS_MIME_SINK_PATTERNS_H_

#include "mime.h"

#ifdef __cplusplus
extern "C" {
#endif


/*
 * Functions...
 */

extern int	msink_is_enabled(void);
extern int	msink_try_reuse(cupsd_printer_t *printer);
extern void	msink_try_store(mime_t *mime, mime_type_t *sink,cups_array_t *filetypes);

#ifdef __cplusplus
}
#endif

#endif /* !_CUPS_MIME_SINK_PATTERNS_H_ */
