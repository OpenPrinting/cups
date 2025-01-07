/*
 * Private MD5 definitions for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright 2007-2010 by Apple Inc.
 * Copyright 2005 by Easy Software Products
 *
 * Copyright (C) 1999 Aladdin Enterprises.  All rights reserved.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * L. Peter Deutsch
 * ghost@aladdin.com
 */

/*
  Independent implementation of MD5 (RFC 1321).

  This code implements the MD5 Algorithm defined in RFC 1321.
  It is derived directly from the text of the RFC and not from the
  reference implementation.

  The original and principal author of md5.h is L. Peter Deutsch
  <ghost@aladdin.com>.  Other authors are noted in the change history
  that follows (in reverse chronological order):

  1999-11-04 lpd Edited comments slightly for automatic TOC extraction.
  1999-10-18 lpd Fixed typo in header comment (ansi2knr rather than md5);
	added conditionalization for C++ compilation from Martin
	Purschke <purschke@bnl.gov>.
  1999-05-03 lpd Original version.
 */

#ifndef _CUPS_MD5_INTERNAL_H_
#  define _CUPS_MD5_INTERNAL_H_

#  include <cups/versioning.h>

/* Define the state of the MD5 Algorithm. */
typedef struct _cups_md5_state_s {
    unsigned int count[2];		/* message length in bits, lsw first */
    unsigned int abcd[4];		/* digest buffer */
    unsigned char buf[64];		/* accumulate block */
} _cups_md5_state_t;

#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */

/* Initialize the algorithm. */
void _cupsMD5Init(_cups_md5_state_t *pms) _CUPS_INTERNAL;

/* Append a string to the message. */
void _cupsMD5Append(_cups_md5_state_t *pms, const unsigned char *data, int nbytes) _CUPS_INTERNAL;

/* Finish the message and return the digest. */
void _cupsMD5Finish(_cups_md5_state_t *pms, unsigned char digest[16]) _CUPS_INTERNAL;

#  ifdef __cplusplus
}  /* end extern "C" */
#  endif /* __cplusplus */
#endif /* !_CUPS_MD5_INTERNAL_H_ */
