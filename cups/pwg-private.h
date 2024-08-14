//
// Private PWG media API definitions for CUPS.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright © 2009-2016 by Apple Inc.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _CUPS_PWG_PRIVATE_H_
#  define _CUPS_PWG_PRIVATE_H_
#  include <cups/cups.h>
#  include <cups/pwg.h>
#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus


//
// Constants...
//

#define _PWG_EPSILON	50		// Matching tolerance in hundredths of millimeters


//
// Functions...
//

extern void		_pwgGenerateSize(char *keyword, size_t keysize, const char *prefix, const char *name, int width, int length) _CUPS_INTERNAL_MSG("Use pwgFormatSizeName instead.");
extern int		_pwgInitSize(pwg_size_t *size, ipp_t *job, int *margins_set) _CUPS_INTERNAL_MSG("Use pwgInitSize instead.");
extern const pwg_media_t *_pwgMediaTable(size_t *num_media) _CUPS_PRIVATE;
extern pwg_media_t	*_pwgMediaNearSize(pwg_media_t *pwg, char *keyword, size_t keysize, char *ppdname, size_t ppdsize, int width, int length, int epsilon) _CUPS_PRIVATE;


#  ifdef __cplusplus
}
#  endif // __cplusplus
#endif // !_CUPS_PWG_PRIVATE_H_
