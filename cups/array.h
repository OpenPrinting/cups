//
// Sorted array definitions for CUPS.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright © 2007-2010 by Apple Inc.
// Copyright © 1997-2007 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _CUPS_ARRAY_H_
#  define _CUPS_ARRAY_H_
#  include "base.h"
#  include <stdlib.h>
#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus


//
// Types and structures...
//

typedef struct _cups_array_s cups_array_t;
					// CUPS array type
typedef int (*cups_array_cb_t)(void *first, void *second, void *data);
					// Array comparison function
typedef int (*cups_ahash_cb_t)(void *element, void *data);
					// Array hash function
typedef void *(*cups_acopy_cb_t)(void *element, void *data);
					// Array element copy function
typedef void (*cups_afree_cb_t)(void *element, void *data);
					// Array element free function

// Old type names
# define cups_array_func_t cups_array_cb_t
# define cups_ahash_func_t cups_ahash_cb_t
# define cups_acopy_func_t cups_acopy_cb_t
# define cups_afree_func_t cups_afree_cb_t


//
// Functions...
//

extern int		cupsArrayAdd(cups_array_t *a, void *e) _CUPS_PUBLIC;
extern bool		cupsArrayAddStrings(cups_array_t *a, const char *s, char delim) _CUPS_PUBLIC;
extern void		cupsArrayClear(cups_array_t *a) _CUPS_PUBLIC;
extern int		cupsArrayCount(cups_array_t *a) _CUPS_DEPRECATED_MSG("Use cupsArrayGetCount instead.");
extern void		*cupsArrayCurrent(cups_array_t *a) _CUPS_DEPRECATED_MSG("Use cupsArrayGetCurrent instead.");
extern void		cupsArrayDelete(cups_array_t *a) _CUPS_PUBLIC;
extern cups_array_t	*cupsArrayDup(cups_array_t *a) _CUPS_PUBLIC;
extern void		*cupsArrayFind(cups_array_t *a, void *e) _CUPS_PUBLIC;
extern void		*cupsArrayFirst(cups_array_t *a) _CUPS_DEPRECATED_MSG("Use cupsArrayGetFirst instead.");
extern int		cupsArrayGetCount(cups_array_t *a) _CUPS_PUBLIC;
extern void		*cupsArrayGetCurrent(cups_array_t *a) _CUPS_PUBLIC;
extern void		*cupsArrayGetElement(cups_array_t *a, int n) _CUPS_PUBLIC;
extern void		*cupsArrayGetFirst(cups_array_t *a) _CUPS_PUBLIC;
extern int		cupsArrayGetIndex(cups_array_t *a) _CUPS_PUBLIC;
extern int		cupsArrayGetInsert(cups_array_t *a) _CUPS_PUBLIC;
extern void		*cupsArrayGetLast(cups_array_t *a) _CUPS_PUBLIC;
extern void		*cupsArrayGetNext(cups_array_t *a) _CUPS_PUBLIC;
extern void		*cupsArrayGetPrev(cups_array_t *a) _CUPS_PUBLIC;
extern void		*cupsArrayGetUserData(cups_array_t *a) _CUPS_PUBLIC;
extern void		*cupsArrayIndex(cups_array_t *a, int n) _CUPS_PUBLIC;
extern int		cupsArrayInsert(cups_array_t *a, void *e) _CUPS_PUBLIC;
extern void		*cupsArrayLast(cups_array_t *a) _CUPS_DEPRECATED_MSG("Use cupsArrayGetLast instead.");
extern cups_array_t	*cupsArrayNew(cups_array_cb_t f, void *d)_CUPS_DEPRECATED_MSG("Use cupsArrayNew3 instead.");
extern cups_array_t	*cupsArrayNew2(cups_array_cb_t f, void *d, cups_ahash_cb_t h, int hsize) _CUPS_DEPRECATED_MSG("Use cupsArrayNew3 instead.");
extern cups_array_t	*cupsArrayNew3(cups_array_cb_t f, void *d, cups_ahash_cb_t h, int hsize, cups_acopy_cb_t cf, cups_afree_cb_t ff) _CUPS_PUBLIC;
extern cups_array_t	*cupsArrayNewStrings(const char *s, char delim) _CUPS_PUBLIC;
extern void		*cupsArrayNext(cups_array_t *a) _CUPS_DEPRECATED_MSG("Use cupsArrayGetNext instead.");
extern void		*cupsArrayPrev(cups_array_t *a) _CUPS_DEPRECATED_MSG("Use cupsArrayGetPrev instead.");
extern int		cupsArrayRemove(cups_array_t *a, void *e) _CUPS_PUBLIC;
extern void		*cupsArrayRestore(cups_array_t *a) _CUPS_PUBLIC;
extern int		cupsArraySave(cups_array_t *a) _CUPS_PUBLIC;
extern void		*cupsArrayUserData(cups_array_t *a) _CUPS_DEPRECATED_MSG("Use cupsArrayGetUserData instead.");

#  ifdef __cplusplus
}
#  endif // __cplusplus
#endif // !_CUPS_ARRAY_H_
