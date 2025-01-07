/*
 * snprintf functions for CUPS.
 *
 * Copyright © 2021-2023 by OpenPrinting
 * Copyright © 2007-2019 by Apple Inc.
 * Copyright © 1997-2007 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "string-private.h"


#ifndef HAVE_VSNPRINTF
/*
 * '_cups_vsnprintf()' - Format a string into a fixed size buffer.
 */

int					/* O - Number of bytes formatted */
_cups_vsnprintf(char       *buffer,	/* O - Output buffer */
                size_t     bufsize,	/* O - Size of output buffer */
	        const char *format,	/* I - printf-style format string */
	        va_list    ap)		/* I - Pointer to additional arguments */
{
  char		*bufptr,		/* Pointer to position in buffer */
		*bufend,		/* Pointer to end of buffer */
		sign,			/* Sign of format width */
		size,			/* Size character (h, l, L) */
		type;			/* Format type character */
  int		width,			/* Width of field */
		prec;			/* Number of characters of precision */
  char		tformat[100],		/* Temporary format string for sprintf() */
		*tptr,			/* Pointer into temporary format */
		temp[1024];		/* Buffer for formatted numbers */
  size_t	templen;		/* Length of "temp" */
  char		*s;			/* Pointer to string */
  int		slen;			/* Length of string */
  int		bytes;			/* Total number of bytes needed */


 /*
  * Loop through the format string, formatting as needed...
  */

  bufptr = buffer;
  bufend = buffer + bufsize - 1;
  bytes  = 0;

  while (*format)
  {
    if (*format == '%')
    {
      tptr = tformat;
      *tptr++ = *format++;

      if (*format == '%')
      {
        if (bufptr && bufptr < bufend) *bufptr++ = *format;
        bytes ++;
        format ++;
	continue;
      }
      else if (strchr(" -+#\'", *format))
      {
        *tptr++ = *format;
        sign = *format++;
      }
      else
        sign = 0;

      if (*format == '*')
      {
       /*
        * Get width from argument...
	*/

	format ++;
	width = va_arg(ap, int);

        /* Note: Can't use snprintf here since we are implementing this function... */
	sprintf(tptr, "%d", width);
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
         /*
	  * Get precision from argument...
	  */

	  format ++;
	  prec = va_arg(ap, int);

          /* Note: Can't use snprintf here since we are implementing this function... */
	  sprintf(tptr, "%d", prec);
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
      else
        prec = -1;

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

      if (!*format)
        break;

      if (tptr < (tformat + sizeof(tformat) - 1))
        *tptr++ = *format;

      type  = *format++;
      *tptr = '\0';

      switch (type)
      {
	case 'E' : /* Floating point formats */
	case 'G' :
	case 'e' :
	case 'f' :
	case 'g' :
	    if ((width + 2) > sizeof(temp))
	      break;

            /* Note: Can't use snprintf here since we are implementing this function... */
	    sprintf(temp, tformat, va_arg(ap, double));
	    templen = strlen(temp);

            bytes += (int)templen;

            if (bufptr)
	    {
	      if ((bufptr + templen) > bufend)
	      {
		strlcpy(bufptr, temp, (size_t)(bufend - bufptr));
		bufptr = bufend;
	      }
	      else
	      {
		memcpy(bufptr, temp, templen + 1);
		bufptr += templen;
	      }
	    }
	    break;

        case 'B' : /* Integer formats */
	case 'X' :
	case 'b' :
        case 'd' :
	case 'i' :
	case 'o' :
	case 'u' :
	case 'x' :
	    if ((width + 2) > sizeof(temp))
	      break;

	    /* Note: Can't use snprintf here since we are implementing this function... */
	    sprintf(temp, tformat, va_arg(ap, int));
	    templen = strlen(temp);

            bytes += (int)templen;

	    if (bufptr)
	    {
	      if ((bufptr + templen) > bufend)
	      {
		strlcpy(bufptr, temp, (size_t)(bufend - bufptr));
		bufptr = bufend;
	      }
	      else
	      {
		memcpy(bufptr, temp, templen + 1);
		bufptr += templen;
	      }
	    }
	    break;

	case 'p' : /* Pointer value */
	    if ((width + 2) > sizeof(temp))
	      break;

	    /* Note: Can't use snprintf here since we are implementing this function... */
	    sprintf(temp, tformat, va_arg(ap, void *));
	    templen = strlen(temp);

            bytes += (int)templen;

	    if (bufptr)
	    {
	      if ((bufptr + templen) > bufend)
	      {
		strlcpy(bufptr, temp, (size_t)(bufend - bufptr));
		bufptr = bufend;
	      }
	      else
	      {
		memcpy(bufptr, temp, templen + 1);
		bufptr += templen;
	      }
	    }
	    break;

        case 'c' : /* Character or character array */
	    bytes += width;

	    if (bufptr)
	    {
	      if (width <= 1)
	        *bufptr++ = va_arg(ap, int);
	      else
	      {
		if ((bufptr + width) > bufend)
		  width = (int)(bufend - bufptr);

		memcpy(bufptr, va_arg(ap, char *), (size_t)width);
		bufptr += width;
	      }
	    }
	    break;

	case 's' : /* String */
	    if ((s = va_arg(ap, char *)) == NULL)
	      s = "(null)";

	    slen = (int)strlen(s);
	    if (slen > width && prec != width)
	      width = slen;

            bytes += width;

	    if (bufptr)
	    {
	      if ((bufptr + width) > bufend)
	        width = (int)(bufend - bufptr);

              if (slen > width)
	        slen = width;

	      if (sign == '-')
	      {
		memcpy(bufptr, s, (size_t)slen);
		memset(bufptr + slen, ' ', (size_t)(width - slen));
	      }
	      else
	      {
		memset(bufptr, ' ', (size_t)(width - slen));
		memcpy(bufptr + width - slen, s, (size_t)slen);
	      }

	      bufptr += width;
	    }
	    break;

	case 'n' : /* Output number of chars so far */
	    *(va_arg(ap, int *)) = bytes;
	    break;
      }
    }
    else
    {
      bytes ++;

      if (bufptr && bufptr < bufend)
        *bufptr++ = *format;

      format ++;
    }
  }

 /*
  * Nul-terminate the string and return the number of characters needed.
  */

  if (bufptr && bufptr < bufend)
    *bufptr = '\0';

  return (bytes);
}
#endif /* !HAVE_VSNPRINT */


#ifndef HAVE_SNPRINTF
/*
 * '_cups_snprintf()' - Format a string into a fixed size buffer.
 */

int					/* O - Number of bytes formatted */
_cups_snprintf(char       *buffer,	/* O - Output buffer */
               size_t     bufsize,	/* O - Size of output buffer */
               const char *format,	/* I - printf-style format string */
	       ...)			/* I - Additional arguments as needed */
{
  int		bytes;			/* Number of bytes formatted */
  va_list 	ap;			/* Pointer to additional arguments */


  va_start(ap, format);
  bytes = vsnprintf(buffer, bufsize, format, ap);
  va_end(ap);

  return (bytes);
}
#endif /* !HAVE_SNPRINTF */
