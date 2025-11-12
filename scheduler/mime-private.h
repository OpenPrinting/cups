//
// Private MIME type/conversion database definitions for CUPS.
//
// Copyright © 2020-2025 by OpenPrinting.
// Copyright © 2011-2018 by Apple Inc.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _CUPS_MIME_PRIVATE_H_
#  define _CUPS_MIME_PRIVATE_H_

#  include "mime.h"


//
// C++ magic...
//

#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus


//
// Prototypes...
//

extern int	_mimeCompareTypes(mime_type_t *a, mime_type_t *b, void *data);
extern void	_mimeError(mime_t *mime, const char *format, ...) _CUPS_FORMAT(2, 3);
extern void	_mimeFreeType(mime_type_t *t, void *data);


#  ifdef __cplusplus
}
#  endif // __cplusplus
#endif // !_CUPS_MIME_PRIVATE_H_
