//
// Private JSON API definitions for CUPS.
//
// Copyright © 2023-2024 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _CUPS_JSON_PRIVATE_H_
#  define _CUPS_JSON_PRIVATE_H_
#  include "json.h"
#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */


//
// Functions...
//

extern void	_cupsJSONDelete(cups_json_t *json, const char *key) _CUPS_PRIVATE;


#  ifdef __cplusplus
}
#  endif /* __cplusplus */
#endif // !_CUPS_JSON_PRIVATE_H_
