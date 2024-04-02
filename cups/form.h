//
// Form API definitions for CUPS.
//
// Copyright © 2023-2024 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _CUPS_FORM_H_
#  define _CUPS_FORM_H_
#  include "cups.h"
#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus


//
// Functions...
//

extern int	cupsFormDecode(const char *data, cups_option_t **vars) _CUPS_PUBLIC;
extern char	*cupsFormEncode(const char *url, int num_vars, cups_option_t *vars) _CUPS_PUBLIC;


#  ifdef __cplusplus
}
#  endif // __cplusplus
#endif // !_CUPS_FORM_H_
