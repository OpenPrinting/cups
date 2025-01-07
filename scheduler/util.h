/*
 * Mini-daemon utility definitions for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright 2007-2014 by Apple Inc.
 * Copyright 1997-2005 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more information.
 */

#ifndef _CUPSD_UTIL_H_
#  define _CUPSD_UTIL_H_

/*
 * Include necessary headers...
 */

#  include <cups/array-private.h>
#  include <cups/file-private.h>
#  include <signal.h>


/*
 * C++ magic...
 */

#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */


/*
 * Types...
 */

typedef int (*cupsd_compare_func_t)(const void *, const void *);


/*
 * Prototypes...
 */

extern int		cupsdCompareNames(const char *s, const char *t);
extern cups_array_t	*cupsdCreateStringsArray(const char *s);
extern int		cupsdExec(const char *command, char **argv);
extern cups_file_t	*cupsdPipeCommand(int *pid, const char *command,
			                  char **argv, uid_t user);
extern void		cupsdSendIPPGroup(ipp_tag_t group_tag);
extern void		cupsdSendIPPHeader(ipp_status_t status_code,
			                   int request_id);
extern void		cupsdSendIPPInteger(ipp_tag_t value_tag,
			                    const char *name, int value);
extern void		cupsdSendIPPString(ipp_tag_t value_tag,
			                   const char *name, const char *value);
extern void		cupsdSendIPPTrailer(void);


#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_CUPSD_UTIL_H_ */
