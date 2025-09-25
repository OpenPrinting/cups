/*
 * String functions for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2007-2019 by Apple Inc.
 * Copyright © 1997-2007 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#define _CUPS_STRING_C_
#include "cups-private.h"
#include "debug-internal.h"
#include <stddef.h>
#include <limits.h>


/*
 * Local globals...
 */

static cups_mutex_t	sp_mutex = CUPS_MUTEX_INITIALIZER;
					/* Mutex to control access to pool */
static cups_array_t	*stringpool = NULL;
					/* Global string pool */


/*
 * Local functions...
 */

static int	compare_sp_items(_cups_sp_item_t *a, _cups_sp_item_t *b, void *data);
static void	validate_end(char *s, char *end);


//
// 'cupsConcatString()' - Safely concatenate two UTF-8 strings.
//
// @since CUPS 2.5@
//

size_t					// O - Length of string
cupsConcatString(char       *dst,	// O - Destination string
                 const char *src,	// I - Source string
	         size_t     dstsize)	// I - Size of destination string buffer
{
  size_t	srclen;			// Length of source string
  size_t	dstlen;			// Length of destination string


  // Range check input...
  if (!dst || !src || dstsize == 0)
    return (0);

  // Figure out how much room is left...
  dstlen = strlen(dst);

  if (dstsize < (dstlen + 1))
    return (dstlen);		        // No room, return immediately...

  dstsize -= dstlen + 1;

  // Figure out how much room is needed...
  srclen = strlen(src);

  // Copy the appropriate amount...
  if (srclen <= dstsize)
  {
    // String fits, just copy over...
    memmove(dst + dstlen, src, srclen);
    dst[dstlen + srclen] = '\0';
  }
  else
  {
    // String too big, copy what we can and clean up the end...
    memmove(dst + dstlen, src, dstsize);
    dst[dstlen + dstsize] = '\0';

    validate_end(dst, dst + dstlen + dstsize);
  }

  return (dstlen + srclen);
}


//
// 'cupsCopyString()' - Safely copy a UTF-8 string.
//
// @since CUPS 2.5@
//

size_t					// O - Length of string
cupsCopyString(char       *dst,		// O - Destination string
               const char *src,		// I - Source string
	       size_t     dstsize)	// I - Size of destination string buffer
{
  size_t	srclen;			// Length of source string


  // Range check input...
  if (!dst || !src || dstsize == 0)
  {
    if (dst)
      *dst = '\0';
    return (0);
  }

  // Figure out how much room is needed...
  dstsize --;

  srclen = strlen(src);

  // Copy the appropriate amount...
  if (srclen <= dstsize)
  {
    // Source string will fit...
    memmove(dst, src, srclen);
    dst[srclen] = '\0';
  }
  else
  {
    // Source string too big, copy what we can and clean up the end...
    memmove(dst, src, dstsize);
    dst[dstsize] = '\0';

    validate_end(dst, dst + dstsize);
  }

  return (srclen);
}


//
// 'cupsFormatString()' - Format a UTF-8 string into a fixed size buffer.
//
// This function formats a UTF-8 string into a fixed size buffer, escaping
// special/control characters as needed so they can be safely displayed or
// logged.
//
// @since CUPS 2.5@
//

ssize_t					// O - Number of bytes formatted
cupsFormatString(
    char       *buffer,			// O - Output buffer
    size_t     bufsize,			// O - Size of output buffer
    const char *format,			// I - `printf`-style format string
    ...)				// I - Additional arguments
{
  va_list	ap;			// Pointer to additional arguments
  ssize_t	ret;			// Return value


  // Range check input...
  if (!buffer || bufsize < 2 || !format)
    return (-1);

  // Format the string...
  va_start(ap, format);
  ret = cupsFormatStringv(buffer, bufsize, format, ap);
  va_end(ap);

  // Return the number of bytes that could have been written...
  return (ret);
}


//
// 'cupsFormatStringv()' - Format a UTF-8 string into a fixed size buffer (`va_list` version).
//
// This function formats a UTF-8 string into a fixed size buffer using a
// variable argument pointer, escaping special/control characters as needed so
// they can be safely displayed or logged.
//
// @since CUPS 2.5@
//

ssize_t					// O - Number of bytes formatted
cupsFormatStringv(
    char       *buffer,			// O - Output buffer
    size_t     bufsize,			// O - Size of output buffer
    const char *format,			// I - printf-style format string
    va_list    ap)			// I - Pointer to additional arguments
{
  char		*bufptr,		// Pointer to position in buffer
		*bufend,		// Pointer to end of buffer
		size,			// Size character (h, l, L)
		type;			// Format type character
  int		width,			// Width of field
		prec;			// Number of characters of precision
  char		tformat[100],		// Temporary format string for snprintf()
		*tptr,			// Pointer into temporary format
		temp[1024];		// Buffer for formatted numbers
  char		*s;			// Pointer to string
  ssize_t	bytes;			// Total number of bytes needed


  // Range check input...
  if (!buffer || bufsize < 2 || !format)
    return (-1);

  // Loop through the format string, formatting as needed...
  bufptr = buffer;
  bufend = buffer + bufsize - 1;
  bytes  = 0;

  while (*format)
  {
    if (*format == '%')
    {
      // Format character...
      tptr = tformat;
      *tptr++ = *format++;

      if (*format == '%')
      {
        if (bufptr < bufend)
	  *bufptr++ = *format;
        bytes ++;
        format ++;
	continue;
      }
      else if (strchr(" -+#\'", *format))
      {
        *tptr++ = *format++;
      }

      if (*format == '*')
      {
        // Get width from argument...
	format ++;
	width = va_arg(ap, int);

	snprintf(tptr, sizeof(tformat) - (size_t)(tptr - tformat), "%d", width);
	tptr += strlen(tptr);
      }
      else
      {
	width = 0;

	while (isdigit(*format & 255))
	{
	  if (tptr < (tformat + sizeof(tformat) - 1))
	    *tptr++ = *format;

	  width = width * 10 + *format++ - '0';
	}
      }

      if (*format == '.')
      {
	if (tptr < (tformat + sizeof(tformat) - 1))
	  *tptr++ = *format;

        format ++;

        if (*format == '*')
	{
          // Get precision from argument...
	  format ++;
	  prec = va_arg(ap, int);

	  snprintf(tptr, sizeof(tformat) - (size_t)(tptr - tformat), "%d", prec);
	  tptr += strlen(tptr);
	}
	else
	{
	  prec = 0;

	  while (isdigit(*format & 255))
	  {
	    if (tptr < (tformat + sizeof(tformat) - 1))
	      *tptr++ = *format;

	    prec = prec * 10 + *format++ - '0';
	  }
	}
      }

      if (*format == 'l' && format[1] == 'l')
      {
        size = 'L';

	if (tptr < (tformat + sizeof(tformat) - 2))
	{
	  *tptr++ = 'l';
	  *tptr++ = 'l';
	}

	format += 2;
      }
      else if (*format == 'h' || *format == 'l' || *format == 'L')
      {
	if (tptr < (tformat + sizeof(tformat) - 1))
	  *tptr++ = *format;

        size = *format++;
      }
      else
      {
        size = 0;
      }

      if (!*format)
        break;

      if (tptr < (tformat + sizeof(tformat) - 1))
        *tptr++ = *format;

      type  = *format++;
      *tptr = '\0';

      switch (type)
      {
	case 'E' : // Floating point formats
	case 'G' :
	case 'e' :
	case 'f' :
	case 'g' :
	    if ((size_t)(width + 2) > sizeof(temp))
	      break;

	    snprintf(temp, sizeof(temp), tformat, va_arg(ap, double));

            bytes += (int)strlen(temp);

            if (bufptr < bufend)
	    {
	      cupsCopyString(bufptr, temp, (size_t)(bufend - bufptr));
	      bufptr += strlen(bufptr);
	    }
	    break;

        case 'B' : // Integer formats
	case 'X' :
	case 'b' :
        case 'd' :
	case 'i' :
	case 'o' :
	case 'u' :
	case 'x' :
	    if ((size_t)(width + 2) > sizeof(temp))
	      break;

#  ifdef HAVE_LONG_LONG
            if (size == 'L')
	      snprintf(temp, sizeof(temp), tformat, va_arg(ap, long long));
	    else
#  endif // HAVE_LONG_LONG
            if (size == 'l')
	      snprintf(temp, sizeof(temp), tformat, va_arg(ap, long));
	    else
	      snprintf(temp, sizeof(temp), tformat, va_arg(ap, int));

            bytes += (int)strlen(temp);

	    if (bufptr < bufend)
	    {
	      cupsCopyString(bufptr, temp, (size_t)(bufend - bufptr));
	      bufptr += strlen(bufptr);
	    }
	    break;

	case 'p' : // Pointer value
	    if ((size_t)(width + 2) > sizeof(temp))
	      break;

	    snprintf(temp, sizeof(temp), tformat, va_arg(ap, void *));

            bytes += (int)strlen(temp);

	    if (bufptr < bufend)
	    {
	      cupsCopyString(bufptr, temp, (size_t)(bufend - bufptr));
	      bufptr += strlen(bufptr);
	    }
	    break;

        case 'c' : // Character or character array
	    bytes += width;

	    if (bufptr < bufend)
	    {
	      if (width <= 1)
	      {
	        *bufptr++ = (char)va_arg(ap, int);
	      }
	      else
	      {
		if ((bufptr + width) > bufend)
		  width = (int)(bufend - bufptr);

		memcpy(bufptr, va_arg(ap, char *), (size_t)width);
		bufptr += width;
	      }
	    }
	    break;

	case 's' : // String
	    if ((s = va_arg(ap, char *)) == NULL)
	      s = "(null)";

            // Copy the C string, replacing control chars and \ with C character escapes...
            for (; *s && bufptr < bufend; s ++)
	    {
	      if (*s == '\n')
	      {
	        *bufptr++ = '\\';
	        if (bufptr < bufend)
		  *bufptr++ = 'n';
		bytes += 2;
	      }
	      else if (*s == '\r')
	      {
	        *bufptr++ = '\\';
	        if (bufptr < bufend)
		  *bufptr++ = 'r';
		bytes += 2;
	      }
	      else if (*s == '\t')
	      {
	        *bufptr++ = '\\';
	        if (bufptr < bufend)
		  *bufptr++ = 't';
		bytes += 2;
	      }
	      else if (*s == '\\')
	      {
	        *bufptr++ = '\\';
	        if (bufptr < bufend)
		  *bufptr++ = '\\';
		bytes += 2;
	      }
	      else if (*s == '\'')
	      {
	        *bufptr++ = '\\';
	        if (bufptr < bufend)
		  *bufptr++ = '\'';
		bytes += 2;
	      }
	      else if (*s == '\"')
	      {
	        *bufptr++ = '\\';
	        if (bufptr < bufend)
		  *bufptr++ = '\"';
		bytes += 2;
	      }
	      else if ((*s & 255) < ' ')
	      {
	        *bufptr++ = '\\';
	        if (bufptr < bufend)
		  *bufptr++ = '0';
	        if (bufptr < bufend)
		  *bufptr++ = '0' + *s / 8;
	        if (bufptr < bufend)
		  *bufptr++ = '0' + (*s & 7);
		bytes += 4;
	      }
	      else
	      {
	        *bufptr++ = *s;
		bytes ++;
	      }
            }

            if (bufptr >= bufend)
	      bytes += 2 * strlen(s);
	    break;

	case 'n' : // Output number of chars so far
	    *(va_arg(ap, int *)) = (int)bytes;
	    break;
      }
    }
    else
    {
      // Literal character...
      bytes ++;

      if (bufptr < bufend)
        *bufptr++ = *format++;
      else
        format ++;
    }
  }

  // Nul-terminate the string and return the number of characters needed.
  if (bufptr < bufend)
  {
    // Everything fit in the buffer...
    *bufptr = '\0';
  }
  else
  {
    // Make sure the last characters are valid UTF-8...
    *bufend = '\0';

    validate_end(buffer, bufend);
  }

  return (bytes);
}


/*
 * '_cupsStrAlloc()' - Allocate/reference a string.
 */

char *					/* O - String pointer */
_cupsStrAlloc(const char *s)		/* I - String */
{
  size_t		slen;		/* Length of string */
  _cups_sp_item_t	*item,		/* String pool item */
			*key;		/* Search key */


 /*
  * Range check input...
  */

  if (!s)
    return (NULL);

 /*
  * Get the string pool...
  */

  cupsMutexLock(&sp_mutex);

  if (!stringpool)
    stringpool = cupsArrayNew((cups_array_func_t)compare_sp_items, NULL);

  if (!stringpool)
  {
    cupsMutexUnlock(&sp_mutex);

    return (NULL);
  }

 /*
  * See if the string is already in the pool...
  */

  key = (_cups_sp_item_t *)(s - offsetof(_cups_sp_item_t, str));

  if ((item = (_cups_sp_item_t *)cupsArrayFind(stringpool, key)) != NULL)
  {
   /*
    * Found it, return the cached string...
    */

    item->ref_count ++;

#ifdef DEBUG_GUARDS
    DEBUG_printf("5_cupsStrAlloc: Using string %p(%s) for \"%s\", guard=%08x, ref_count=%d", item, item->str, s, item->guard, item->ref_count);

    if (item->guard != _CUPS_STR_GUARD)
      abort();
#endif /* DEBUG_GUARDS */

    cupsMutexUnlock(&sp_mutex);

    return (item->str);
  }

 /*
  * Not found, so allocate a new one...
  */

  slen = strlen(s);
  item = (_cups_sp_item_t *)calloc(1, sizeof(_cups_sp_item_t) + slen);
  if (!item)
  {
    cupsMutexUnlock(&sp_mutex);

    return (NULL);
  }

  item->ref_count = 1;
  memcpy(item->str, s, slen + 1);

#ifdef DEBUG_GUARDS
  item->guard = _CUPS_STR_GUARD;

  DEBUG_printf("5_cupsStrAlloc: Created string %p(%s) for \"%s\", guard=%08x, ref_count=%d", item, item->str, s, item->guard, item->ref_count);
#endif /* DEBUG_GUARDS */

 /*
  * Add the string to the pool and return it...
  */

  cupsArrayAdd(stringpool, item);

  cupsMutexUnlock(&sp_mutex);

  return (item->str);
}


/*
 * '_cupsStrDate()' - Return a localized date for a given time value.
 *
 * This function works around the locale encoding issues of strftime...
 */

char *					/* O - Buffer */
_cupsStrDate(char   *buf,		/* I - Buffer */
             size_t bufsize,		/* I - Size of buffer */
	     time_t timeval)		/* I - Time value */
{
  struct tm	date;			/* Local date/time */
  char		temp[1024];		/* Temporary buffer */
  _cups_globals_t *cg = _cupsGlobals();	/* Per-thread globals */


  if (!cg->lang_default)
    cg->lang_default = cupsLangDefault();

  localtime_r(&timeval, &date);

  if (cg->lang_default->encoding != CUPS_UTF8)
  {
    strftime(temp, sizeof(temp), "%c", &date);
    cupsCharsetToUTF8((cups_utf8_t *)buf, temp, (int)bufsize, cg->lang_default->encoding);
  }
  else
    strftime(buf, bufsize, "%c", &date);

  return (buf);
}


/*
 * '_cupsStrFlush()' - Flush the string pool.
 */

void
_cupsStrFlush(void)
{
  _cups_sp_item_t	*item;		/* Current item */


  DEBUG_printf("4_cupsStrFlush: %d strings in array", cupsArrayCount(stringpool));

  cupsMutexLock(&sp_mutex);

  for (item = (_cups_sp_item_t *)cupsArrayFirst(stringpool);
       item;
       item = (_cups_sp_item_t *)cupsArrayNext(stringpool))
    free(item);

  cupsArrayDelete(stringpool);
  stringpool = NULL;

  cupsMutexUnlock(&sp_mutex);
}


/*
 * '_cupsStrFormatd()' - Format a floating-point number.
 */

char *					/* O - Pointer to end of string */
_cupsStrFormatd(char         *buf,	/* I - String */
                char         *bufend,	/* I - End of string buffer */
		double       number,	/* I - Number to format */
                struct lconv *loc)	/* I - Locale data */
{
  char		*bufptr,		/* Pointer into buffer */
		temp[1024],		/* Temporary string */
		*tempdec,		/* Pointer to decimal point */
		*tempptr;		/* Pointer into temporary string */
  const char	*dec;			/* Decimal point */
  int		declen;			/* Length of decimal point */


 /*
  * Format the number using the "%.12f" format and then eliminate
  * unnecessary trailing 0's.
  */

  snprintf(temp, sizeof(temp), "%.12f", number);
  for (tempptr = temp + strlen(temp) - 1;
       tempptr > temp && *tempptr == '0';
       *tempptr-- = '\0');

 /*
  * Next, find the decimal point...
  */

  if (loc && loc->decimal_point)
  {
    dec    = loc->decimal_point;
    declen = (int)strlen(dec);
  }
  else
  {
    dec    = ".";
    declen = 1;
  }

  if (declen == 1)
    tempdec = strchr(temp, *dec);
  else
    tempdec = strstr(temp, dec);

 /*
  * Copy everything up to the decimal point...
  */

  if (tempdec)
  {
    for (tempptr = temp, bufptr = buf;
         tempptr < tempdec && bufptr < bufend;
	 *bufptr++ = *tempptr++);

    tempptr += declen;

    if (*tempptr && bufptr < bufend)
    {
      *bufptr++ = '.';

      while (*tempptr && bufptr < bufend)
        *bufptr++ = *tempptr++;
    }

    *bufptr = '\0';
  }
  else
  {
    cupsCopyString(buf, temp, (size_t)(bufend - buf + 1));
    bufptr = buf + strlen(buf);
  }

  return (bufptr);
}


/*
 * '_cupsStrFree()' - Free/dereference a string.
 */

void
_cupsStrFree(const char *s)		/* I - String to free */
{
  _cups_sp_item_t	*item,		/* String pool item */
			*key;		/* Search key */


 /*
  * Range check input...
  */

  if (!s)
    return;

 /*
  * See if the string is already in the pool...
  */

  cupsMutexLock(&sp_mutex);

  key = (_cups_sp_item_t *)(s - offsetof(_cups_sp_item_t, str));

  if ((item = (_cups_sp_item_t *)cupsArrayFind(stringpool, key)) != NULL && item == key)
  {
   /*
    * Found it, dereference...
    */

#ifdef DEBUG_GUARDS
    if (key->guard != _CUPS_STR_GUARD)
    {
      DEBUG_printf("5_cupsStrFree: Freeing string %p(%s), guard=%08x, ref_count=%d", key, key->str, key->guard, key->ref_count);
      abort();
    }
#endif /* DEBUG_GUARDS */

    item->ref_count --;

    if (!item->ref_count)
    {
     /*
      * Remove and free...
      */

      cupsArrayRemove(stringpool, item);

      free(item);
    }
  }

  cupsMutexUnlock(&sp_mutex);
}


/*
 * '_cupsStrRetain()' - Increment the reference count of a string.
 *
 * Note: This function does not verify that the passed pointer is in the
 *       string pool, so any calls to it MUST know they are passing in a
 *       good pointer.
 */

char *					/* O - Pointer to string */
_cupsStrRetain(const char *s)		/* I - String to retain */
{
  _cups_sp_item_t	*item;		/* Pointer to string pool item */


  if (s)
  {
    item = (_cups_sp_item_t *)(s - offsetof(_cups_sp_item_t, str));

    cupsMutexLock(&sp_mutex);

#ifdef DEBUG_GUARDS
    if (item->guard != _CUPS_STR_GUARD)
    {
      DEBUG_printf("5_cupsStrRetain: Retaining string %p(%s), guard=%08x, ref_count=%d", item, s, item->guard, item->ref_count);
      abort();
    }
#endif /* DEBUG_GUARDS */

    item->ref_count ++;

    cupsMutexUnlock(&sp_mutex);
  }

  return ((char *)s);
}


/*
 * '_cupsStrScand()' - Scan a string for a floating-point number.
 *
 * This function handles the locale-specific BS so that a decimal
 * point is always the period (".")...
 */

double					/* O - Number */
_cupsStrScand(const char   *buf,	/* I - Pointer to number */
              char         **bufptr,	/* O - New pointer or NULL on error */
              struct lconv *loc)	/* I - Locale data */
{
  char	temp[1024],			/* Temporary buffer */
	*tempptr;			/* Pointer into temporary buffer */


 /*
  * Range check input...
  */

  if (!buf)
    return (0.0);

 /*
  * Skip leading whitespace...
  */

  while (_cups_isspace(*buf))
    buf ++;

 /*
  * Copy leading sign, numbers, period, and then numbers...
  */

  tempptr = temp;
  if (*buf == '-' || *buf == '+')
    *tempptr++ = *buf++;

  while (isdigit(*buf & 255))
    if (tempptr < (temp + sizeof(temp) - 1))
      *tempptr++ = *buf++;
    else
    {
      if (bufptr)
	*bufptr = NULL;

      return (0.0);
    }

  if (*buf == '.')
  {
   /*
    * Read fractional portion of number...
    */

    buf ++;

    if (loc && loc->decimal_point)
    {
      cupsCopyString(tempptr, loc->decimal_point, sizeof(temp) - (size_t)(tempptr - temp));
      tempptr += strlen(tempptr);
    }
    else if (tempptr < (temp + sizeof(temp) - 1))
      *tempptr++ = '.';
    else
    {
      if (bufptr)
        *bufptr = NULL;

      return (0.0);
    }

    while (isdigit(*buf & 255))
      if (tempptr < (temp + sizeof(temp) - 1))
	*tempptr++ = *buf++;
      else
      {
	if (bufptr)
	  *bufptr = NULL;

	return (0.0);
      }
  }

  if (*buf == 'e' || *buf == 'E')
  {
   /*
    * Read exponent...
    */

    if (tempptr < (temp + sizeof(temp) - 1))
      *tempptr++ = *buf++;
    else
    {
      if (bufptr)
	*bufptr = NULL;

      return (0.0);
    }

    if (*buf == '+' || *buf == '-')
    {
      if (tempptr < (temp + sizeof(temp) - 1))
	*tempptr++ = *buf++;
      else
      {
	if (bufptr)
	  *bufptr = NULL;

	return (0.0);
      }
    }

    while (isdigit(*buf & 255))
      if (tempptr < (temp + sizeof(temp) - 1))
	*tempptr++ = *buf++;
      else
      {
	if (bufptr)
	  *bufptr = NULL;

	return (0.0);
      }
  }

 /*
  * Nul-terminate the temporary string and return the value...
  */

  if (bufptr)
    *bufptr = (char *)buf;

  *tempptr = '\0';

  return (atof(temp));
}


/*
 * '_cupsStrStatistics()' - Return allocation statistics for string pool.
 */

size_t					/* O - Number of strings */
_cupsStrStatistics(size_t *alloc_bytes,	/* O - Allocated bytes */
                   size_t *total_bytes)	/* O - Total string bytes */
{
  size_t		count,		/* Number of strings */
			abytes,		/* Allocated string bytes */
			tbytes,		/* Total string bytes */
			len;		/* Length of string */
  _cups_sp_item_t	*item;		/* Current item */


 /*
  * Loop through strings in pool, counting everything up...
  */

  cupsMutexLock(&sp_mutex);

  for (count = 0, abytes = 0, tbytes = 0,
           item = (_cups_sp_item_t *)cupsArrayFirst(stringpool);
       item;
       item = (_cups_sp_item_t *)cupsArrayNext(stringpool))
  {
   /*
    * Count allocated memory, using a 64-bit aligned buffer as a basis.
    */

    count  += item->ref_count;
    len    = (strlen(item->str) + 8) & (size_t)~7;
    abytes += sizeof(_cups_sp_item_t) + len;
    tbytes += item->ref_count * len;
  }

  cupsMutexUnlock(&sp_mutex);

 /*
  * Return values...
  */

  if (alloc_bytes)
    *alloc_bytes = abytes;

  if (total_bytes)
    *total_bytes = tbytes;

  return (count);
}


/*
 * '_cups_strcpy()' - Copy a string allowing for overlapping strings.
 */

void
_cups_strcpy(char       *dst,		/* I - Destination string */
             const char *src)		/* I - Source string */
{
  while (*src)
    *dst++ = *src++;

  *dst = '\0';
}


/*
 * '_cups_strcasecmp()' - Do a case-insensitive comparison.
 */

int				/* O - Result of comparison (-1, 0, or 1) */
_cups_strcasecmp(const char *s,	/* I - First string */
                 const char *t)	/* I - Second string */
{
  int diff;


  while (*s != '\0' && *t != '\0')
  {
    diff = _cups_tolower(*s) - _cups_tolower(*t);

    if (diff < 0)
      return (-1);
    else if (diff > 0)
      return (1);

    s ++;
    t ++;
  }

  if (*s == '\0' && *t == '\0')
    return (0);
  else if (*s != '\0')
    return (1);
  else
    return (-1);
}


/*
 * '_cups_strncasecmp()' - Do a case-insensitive comparison on up to N chars.
 */

int					/* O - Result of comparison (-1, 0, or 1) */
_cups_strncasecmp(const char *s,	/* I - First string */
                  const char *t,	/* I - Second string */
		  size_t     n)		/* I - Maximum number of characters to compare */
{
  int diff;


  while (*s != '\0' && *t != '\0' && n > 0)
  {
    diff = _cups_tolower(*s) - _cups_tolower(*t);
    if (diff < 0)
      return (-1);
    else if (diff > 0)
      return (1);

    s ++;
    t ++;
    n --;
  }

  if (n == 0)
    return (0);
  else if (*s == '\0' && *t == '\0')
    return (0);
  else if (*s != '\0')
    return (1);
  else
    return (-1);
}


/*
 * 'compare_sp_items()' - Compare two string pool items...
 */

static int				/* O - Result of comparison */
compare_sp_items(_cups_sp_item_t *a,	/* I - First item */
                 _cups_sp_item_t *b,	/* I - Second item */
                 void            *data)         /* I - Unused */
{
  (void)data;
 
 return (strcmp(a->str, b->str));
}


//
// 'validate_end()' - Validate the last UTF-8 character in a buffer.
//

static void
validate_end(char *s,			// I - Pointer to start of string
             char *end)			// I - Pointer to end of string
{
  char *ptr = end - 1;			// Pointer into string


  if (ptr > s && *ptr & 0x80)
  {
    while ((*ptr & 0xc0) == 0x80 && ptr > s)
      ptr --;

    if ((*ptr & 0xe0) == 0xc0)
    {
      // Verify 2-byte UTF-8 sequence...
      if ((end - ptr) != 2)
        *ptr = '\0';
    }
    else if ((*ptr & 0xf0) == 0xe0)
    {
      // Verify 3-byte UTF-8 sequence...
      if ((end - ptr) != 3)
        *ptr = '\0';
    }
    else if ((*ptr & 0xf8) == 0xf0)
    {
      // Verify 4-byte UTF-8 sequence...
      if ((end - ptr) != 4)
        *ptr = '\0';
    }
    else if (*ptr & 0x80)
    {
      // Invalid sequence at end...
      *ptr = '\0';
    }
  }
}
