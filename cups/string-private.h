/*
 * Private string definitions for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2007-2018 by Apple Inc.
 * Copyright © 1997-2006 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#ifndef _CUPS_STRING_PRIVATE_H_
#  define _CUPS_STRING_PRIVATE_H_

/*
 * Include necessary headers...
 */

#  include "config.h"
#  include <stdio.h>
#  include <stdlib.h>
#  include <stdarg.h>
#  include <string.h>
#  include <ctype.h>
#  include <errno.h>
#  include <locale.h>
#  include <time.h>

#  include <cups/versioning.h>


#  if defined(_WIN32) && !defined(__CUPS_SSIZE_T_DEFINED)
#    define __CUPS_SSIZE_T_DEFINED
#    include <stddef.h>
/* Windows does not support the ssize_t type, so map it to __int64... */
typedef __int64 ssize_t;			/* @private@ */
#  endif /* _WIN32 && !__CUPS_SSIZE_T_DEFINED */


/*
 * C++ magic...
 */

#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */


/*
 * String pool structures...
 */

#  define _CUPS_STR_GUARD	0x12344321

typedef struct _cups_sp_item_s		/**** String Pool Item ****/
{
#  ifdef DEBUG_GUARDS
  unsigned int	guard;			/* Guard word */
#  endif /* DEBUG_GUARDS */
  unsigned int	ref_count;		/* Reference count */
  char		str[1];			/* String */
} _cups_sp_item_t;


/*
 * Replacements for the ctype macros that are not affected by locale, since we
 * really only care about testing for ASCII characters when parsing files, etc.
 *
 * The _CUPS_INLINE definition controls whether we get an inline function body,
 * and external function body, or an external definition.
 */

#  if defined(__GNUC__) || __STDC_VERSION__ >= 199901L
#    define _CUPS_INLINE static inline
#  elif defined(_MSC_VER)
#    define _CUPS_INLINE static __inline
#  elif defined(_CUPS_STRING_C_)
#    define _CUPS_INLINE
#  endif /* __GNUC__ || __STDC_VERSION__ */

#  ifdef _CUPS_INLINE
_CUPS_INLINE int			/* O - 1 on match, 0 otherwise */
_cups_isalnum(int ch)			/* I - Character to test */
{
  return ((ch >= '0' && ch <= '9') ||
          (ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z'));
}

_CUPS_INLINE int			/* O - 1 on match, 0 otherwise */
_cups_isalpha(int ch)			/* I - Character to test */
{
  return ((ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z'));
}

_CUPS_INLINE int			/* O - 1 on match, 0 otherwise */
_cups_islower(int ch)			/* I - Character to test */
{
  return (ch >= 'a' && ch <= 'z');
}

_CUPS_INLINE int			/* O - 1 on match, 0 otherwise */
_cups_isspace(int ch)			/* I - Character to test */
{
  return (ch == ' ' || ch == '\f' || ch == '\n' || ch == '\r' || ch == '\t' ||
          ch == '\v');
}

_CUPS_INLINE int			/* O - 1 on match, 0 otherwise */
_cups_isupper(int ch)			/* I - Character to test */
{
  return (ch >= 'A' && ch <= 'Z');
}

_CUPS_INLINE int			/* O - Converted character */
_cups_tolower(int ch)			/* I - Character to convert */
{
  return (_cups_isupper(ch) ? ch - 'A' + 'a' : ch);
}

_CUPS_INLINE int			/* O - Converted character */
_cups_toupper(int ch)			/* I - Character to convert */
{
  return (_cups_islower(ch) ? ch - 'a' + 'A' : ch);
}
#  else
extern int _cups_isalnum(int ch);
extern int _cups_isalpha(int ch);
extern int _cups_islower(int ch);
extern int _cups_isspace(int ch);
extern int _cups_isupper(int ch);
extern int _cups_tolower(int ch);
extern int _cups_toupper(int ch);
#  endif /* _CUPS_INLINE */


/*
 * Prototypes...
 */

extern ssize_t	_cups_safe_vsnprintf(char *buffer, size_t bufsize, const char *format, va_list args) _CUPS_PRIVATE;
extern void	_cups_strcpy(char *dst, const char *src) _CUPS_PRIVATE;

#  ifndef HAVE_STRDUP
extern char	*_cups_strdup(const char *) _CUPS_PRIVATE;
#    define strdup _cups_strdup
#  endif /* !HAVE_STRDUP */

extern int	_cups_strcasecmp(const char *, const char *) _CUPS_PRIVATE;

extern int	_cups_strncasecmp(const char *, const char *, size_t n) _CUPS_PRIVATE;

#  ifndef HAVE_STRLCAT
extern size_t _cups_strlcat(char *, const char *, size_t) _CUPS_PRIVATE;
#    define strlcat _cups_strlcat
#  endif /* !HAVE_STRLCAT */

#  ifndef HAVE_STRLCPY
extern size_t _cups_strlcpy(char *, const char *, size_t) _CUPS_PRIVATE;
#    define strlcpy _cups_strlcpy
#  endif /* !HAVE_STRLCPY */

#  ifndef HAVE_SNPRINTF
extern int	_cups_snprintf(char *, size_t, const char *, ...) _CUPS_FORMAT(3, 4) _CUPS_PRIVATE;
#    define snprintf _cups_snprintf
#  endif /* !HAVE_SNPRINTF */

#  ifndef HAVE_VSNPRINTF
extern int	_cups_vsnprintf(char *, size_t, const char *, va_list) _CUPS_PRIVATE;
#    define vsnprintf _cups_vsnprintf
#  endif /* !HAVE_VSNPRINTF */

/*
 * String pool functions...
 */

extern char	*_cupsStrAlloc(const char *s) _CUPS_PRIVATE;
extern void	_cupsStrFlush(void) _CUPS_PRIVATE;
extern void	_cupsStrFree(const char *s) _CUPS_PRIVATE;
extern char	*_cupsStrRetain(const char *s) _CUPS_PRIVATE;
extern size_t	_cupsStrStatistics(size_t *alloc_bytes, size_t *total_bytes) _CUPS_PRIVATE;


/*
 * Floating point number functions...
 */

extern char	*_cupsStrFormatd(char *buf, char *bufend, double number,
		                 struct lconv *loc) _CUPS_PRIVATE;
extern double	_cupsStrScand(const char *buf, char **bufptr,
		              struct lconv *loc) _CUPS_PRIVATE;


/*
 * Date function...
 */

extern char	*_cupsStrDate(char *buf, size_t bufsize, time_t timeval) _CUPS_PRIVATE;


/*
 * C++ magic...
 */

#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_CUPS_STRING_H_ */
