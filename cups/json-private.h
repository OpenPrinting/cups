//
// Private JSON API definitions for CUPS.
//
// Copyright © 2023 by OpenPrinting.
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

extern void	_cupsJSONAdd(cups_json_t *parent, cups_json_t *after, cups_json_t *node) _CUPS_PRIVATE;
extern void	_cupsJSONDelete(cups_json_t *json, const char *key) _CUPS_PRIVATE;


#  ifdef __cplusplus
}
#  endif /* __cplusplus */
#endif // !_CUPS_JSON_PRIVATE_H_
