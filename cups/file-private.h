/*
 * Private file definitions for CUPS.
 *
 * Since stdio files max out at 256 files on many systems, we have to
 * write similar functions without this limit.  At the same time, using
 * our own file functions allows us to provide transparent support of
 * different line endings, gzip'd print files, PPD files, etc.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2007-2018 by Apple Inc.
 * Copyright © 1997-2007 by Easy Software Products, all rights reserved.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#ifndef _CUPS_FILE_PRIVATE_H_
#  define _CUPS_FILE_PRIVATE_H_

/*
 * Include necessary headers...
 */

#  include "cups-private.h"
#  include <stdio.h>
#  include <stdlib.h>
#  include <stdarg.h>
#  include <fcntl.h>

#  ifdef _WIN32
#    include <io.h>
#    include <sys/locking.h>
#  endif /* _WIN32 */


/*
 * Some operating systems support large files via open flag O_LARGEFILE...
 */

#  ifndef O_LARGEFILE
#    define O_LARGEFILE 0
#  endif /* !O_LARGEFILE */


/*
 * Some operating systems don't define O_BINARY, which is used by Microsoft
 * and IBM to flag binary files...
 */

#  ifndef O_BINARY
#    define O_BINARY 0
#  endif /* !O_BINARY */


#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */


/*
 * Types and structures...
 */

typedef enum				/**** _cupsFileCheck return values ****/
{
  _CUPS_FILE_CHECK_OK = 0,		/* Everything OK */
  _CUPS_FILE_CHECK_MISSING = 1,		/* File is missing */
  _CUPS_FILE_CHECK_PERMISSIONS = 2,	/* File (or parent dir) has bad perms */
  _CUPS_FILE_CHECK_WRONG_TYPE = 3,	/* File has wrong type */
  _CUPS_FILE_CHECK_RELATIVE_PATH = 4	/* File contains a relative path */
} _cups_fc_result_t;

typedef enum				/**** _cupsFileCheck file type values ****/
{
  _CUPS_FILE_CHECK_FILE = 0,		/* Check the file and parent directory */
  _CUPS_FILE_CHECK_PROGRAM = 1,		/* Check the program and parent directory */
  _CUPS_FILE_CHECK_FILE_ONLY = 2,	/* Check the file only */
  _CUPS_FILE_CHECK_DIRECTORY = 3	/* Check the directory */
} _cups_fc_filetype_t;

typedef void (*_cups_fc_func_t)(void *context, _cups_fc_result_t result,
				const char *message);

/*
 * Prototypes...
 */

extern _cups_fc_result_t	_cupsFileCheck(const char *filename, _cups_fc_filetype_t filetype, int dorootchecks, _cups_fc_func_t cb, void *context) _CUPS_PRIVATE;
extern void			_cupsFileCheckFilter(void *context, _cups_fc_result_t result, const char *message) _CUPS_PRIVATE;
extern int			_cupsFilePeekAhead(cups_file_t *fp, int ch);

#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_CUPS_FILE_PRIVATE_H_ */
