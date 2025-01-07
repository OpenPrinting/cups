/*
 * Localized printf/puts functions for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright 2007-2014 by Apple Inc.
 * Copyright 2002-2007 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more information.
 */

/*
 * Include necessary headers...
 */

#include "cups-private.h"
#include "debug-internal.h"


/*
 * '_cupsLangPrintError()' - Print a message followed by a standard error.
 */

void
_cupsLangPrintError(const char *prefix,	/* I - Non-localized message prefix */
                    const char *message)/* I - Message */
{
  ssize_t	bytes;			/* Number of bytes formatted */
  int		last_errno;		/* Last error */
  char		buffer[2048],		/* Message buffer */
		*bufptr,		/* Pointer into buffer */
		output[8192];		/* Output buffer */
  _cups_globals_t *cg;			/* Global data */


 /*
  * Range check...
  */

  if (!message)
    return;

 /*
  * Save the errno value...
  */

  last_errno = errno;

 /*
  * Get the message catalog...
  */

  cg = _cupsGlobals();

  if (!cg->lang_default)
    cg->lang_default = cupsLangDefault();

 /*
  * Format the message...
  */

  if (prefix)
  {
    snprintf(buffer, sizeof(buffer), "%s:", prefix);
    bufptr = buffer + strlen(buffer);
  }
  else
    bufptr = buffer;

  snprintf(bufptr, sizeof(buffer) - (size_t)(bufptr - buffer),
	   /* TRANSLATORS: Message is "subject: error" */
	   _cupsLangString(cg->lang_default, _("%s: %s")),
	   _cupsLangString(cg->lang_default, message), strerror(last_errno));
  strlcat(buffer, "\n", sizeof(buffer));

 /*
  * Convert and write to stderr...
  */

  bytes = cupsUTF8ToCharset(output, (cups_utf8_t *)buffer, sizeof(output),
                            cg->lang_default->encoding);

  if (bytes > 0)
    fwrite(output, 1, (size_t)bytes, stderr);
}


/*
 * '_cupsLangPrintFilter()' - Print a formatted filter message string to a file.
 */

int					/* O - Number of bytes written */
_cupsLangPrintFilter(
    FILE       *fp,			/* I - File to write to */
    const char *prefix,			/* I - Non-localized message prefix */
    const char *message,		/* I - Message string to use */
    ...)				/* I - Additional arguments as needed */
{
  ssize_t	bytes;			/* Number of bytes formatted */
  char		temp[2048],		/* Temporary format buffer */
		buffer[2048],		/* Message buffer */
		output[8192];		/* Output buffer */
  va_list 	ap;			/* Pointer to additional arguments */
  _cups_globals_t *cg;			/* Global data */


 /*
  * Range check...
  */

  if (!fp || !message)
    return (-1);

  cg = _cupsGlobals();

  if (!cg->lang_default)
    cg->lang_default = cupsLangDefault();

 /*
  * Format the string...
  */

  va_start(ap, message);
  snprintf(temp, sizeof(temp), "%s: %s\n", prefix,
	   _cupsLangString(cg->lang_default, message));
  vsnprintf(buffer, sizeof(buffer), temp, ap);
  va_end(ap);

 /*
  * Transcode to the destination charset...
  */

  bytes = cupsUTF8ToCharset(output, (cups_utf8_t *)buffer, sizeof(output),
                            cg->lang_default->encoding);

 /*
  * Write the string and return the number of bytes written...
  */

  if (bytes > 0)
    return ((int)fwrite(output, 1, (size_t)bytes, fp));
  else
    return ((int)bytes);
}


/*
 * '_cupsLangPrintf()' - Print a formatted message string to a file.
 */

int					/* O - Number of bytes written */
_cupsLangPrintf(FILE       *fp,		/* I - File to write to */
		const char *message,	/* I - Message string to use */
	        ...)			/* I - Additional arguments as needed */
{
  ssize_t	bytes;			/* Number of bytes formatted */
  char		buffer[2048],		/* Message buffer */
		output[8192];		/* Output buffer */
  va_list 	ap;			/* Pointer to additional arguments */
  _cups_globals_t *cg;			/* Global data */


 /*
  * Range check...
  */

  if (!fp || !message)
    return (-1);

  cg = _cupsGlobals();

  if (!cg->lang_default)
    cg->lang_default = cupsLangDefault();

 /*
  * Format the string...
  */

  va_start(ap, message);
  vsnprintf(buffer, sizeof(buffer) - 1,
	    _cupsLangString(cg->lang_default, message), ap);
  va_end(ap);

  strlcat(buffer, "\n", sizeof(buffer));

 /*
  * Transcode to the destination charset...
  */

  bytes = cupsUTF8ToCharset(output, (cups_utf8_t *)buffer, sizeof(output),
                            cg->lang_default->encoding);

 /*
  * Write the string and return the number of bytes written...
  */

  if (bytes > 0)
    return ((int)fwrite(output, 1, (size_t)bytes, fp));
  else
    return ((int)bytes);
}


/*
 * '_cupsLangPuts()' - Print a static message string to a file.
 */

int					/* O - Number of bytes written */
_cupsLangPuts(FILE       *fp,		/* I - File to write to */
              const char *message)	/* I - Message string to use */
{
  ssize_t	bytes;			/* Number of bytes formatted */
  char		output[8192];		/* Message buffer */
  _cups_globals_t *cg;			/* Global data */


 /*
  * Range check...
  */

  if (!fp || !message)
    return (-1);

  cg = _cupsGlobals();

  if (!cg->lang_default)
    cg->lang_default = cupsLangDefault();

 /*
  * Transcode to the destination charset...
  */

  bytes = cupsUTF8ToCharset(output,
			    (cups_utf8_t *)_cupsLangString(cg->lang_default,
							   message),
			    sizeof(output) - 4, cg->lang_default->encoding);
  bytes += cupsUTF8ToCharset(output + bytes, (cups_utf8_t *)"\n", (int)(sizeof(output) - (size_t)bytes), cg->lang_default->encoding);

 /*
  * Write the string and return the number of bytes written...
  */

  if (bytes > 0)
    return ((int)fwrite(output, 1, (size_t)bytes, fp));
  else
    return ((int)bytes);
}


/*
 * '_cupsSetLocale()' - Set the current locale and transcode the command-line.
 */

void
_cupsSetLocale(char *argv[])		/* IO - Command-line arguments */
{
  int		i;			/* Looping var */
  char		buffer[8192];		/* Command-line argument buffer */
  _cups_globals_t *cg;			/* Global data */
#ifdef LC_TIME
  const char	*lc_time;		/* Current LC_TIME value */
  char		new_lc_time[255],	/* New LC_TIME value */
		*charset;		/* Pointer to character set */
#endif /* LC_TIME */


 /*
  * Set the locale so that times, etc. are displayed properly.
  *
  * Unfortunately, while we need the localized time value, we *don't*
  * want to use the localized charset for the time value, so we need
  * to set LC_TIME to the locale name with .UTF-8 on the end (if
  * the locale includes a character set specifier...)
  */

  setlocale(LC_ALL, "");

#ifdef LC_TIME
  if ((lc_time = setlocale(LC_TIME, NULL)) == NULL)
    lc_time = setlocale(LC_ALL, NULL);

  if (lc_time)
  {
    strlcpy(new_lc_time, lc_time, sizeof(new_lc_time));
    if ((charset = strchr(new_lc_time, '.')) == NULL)
      charset = new_lc_time + strlen(new_lc_time);

    strlcpy(charset, ".UTF-8", sizeof(new_lc_time) - (size_t)(charset - new_lc_time));
  }
  else
    strlcpy(new_lc_time, "C", sizeof(new_lc_time));

  setlocale(LC_TIME, new_lc_time);
#endif /* LC_TIME */

 /*
  * Initialize the default language info...
  */

  cg = _cupsGlobals();

  if (!cg->lang_default)
    cg->lang_default = cupsLangDefault();

 /*
  * Transcode the command-line arguments from the locale charset to
  * UTF-8...
  */

  if (cg->lang_default->encoding != CUPS_US_ASCII &&
      cg->lang_default->encoding != CUPS_UTF8)
  {
    for (i = 1; argv[i]; i ++)
    {
     /*
      * Try converting from the locale charset to UTF-8...
      */

      if (cupsCharsetToUTF8((cups_utf8_t *)buffer, argv[i], sizeof(buffer),
                            cg->lang_default->encoding) < 0)
        continue;

     /*
      * Save the new string if it differs from the original...
      */

      if (strcmp(buffer, argv[i]))
        argv[i] = strdup(buffer);
    }
  }
}
