/*
 * PPD model-specific attribute routines for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright 2007-2015 by Apple Inc.
 * Copyright 1997-2006 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "cups-private.h"
#include "ppd-private.h"
#include "debug-internal.h"


/*
 * 'ppdFindAttr()' - Find the first matching attribute.
 *
 * @since CUPS 1.1.19/macOS 10.3@
 */

ppd_attr_t *				/* O - Attribute or @code NULL@ if not found */
ppdFindAttr(ppd_file_t *ppd,		/* I - PPD file data */
            const char *name,		/* I - Attribute name */
            const char *spec)		/* I - Specifier string or @code NULL@ */
{
  ppd_attr_t	key,			/* Search key */
		*attr;			/* Current attribute */


  DEBUG_printf(("2ppdFindAttr(ppd=%p, name=\"%s\", spec=\"%s\")", ppd, name,
                spec));

 /*
  * Range check input...
  */

  if (!ppd || !name || ppd->num_attrs == 0)
    return (NULL);

 /*
  * Search for a matching attribute...
  */

  memset(&key, 0, sizeof(key));
  strlcpy(key.name, name, sizeof(key.name));

 /*
  * Return the first matching attribute, if any...
  */

  if ((attr = (ppd_attr_t *)cupsArrayFind(ppd->sorted_attrs, &key)) != NULL)
  {
    if (spec)
    {
     /*
      * Loop until we find the first matching attribute for "spec"...
      */

      while (attr && _cups_strcasecmp(spec, attr->spec))
      {
        if ((attr = (ppd_attr_t *)cupsArrayNext(ppd->sorted_attrs)) != NULL &&
	    _cups_strcasecmp(attr->name, name))
	  attr = NULL;
      }
    }
  }

  return (attr);
}


/*
 * 'ppdFindNextAttr()' - Find the next matching attribute.
 *
 * @since CUPS 1.1.19/macOS 10.3@
 */

ppd_attr_t *				/* O - Attribute or @code NULL@ if not found */
ppdFindNextAttr(ppd_file_t *ppd,	/* I - PPD file data */
                const char *name,	/* I - Attribute name */
		const char *spec)	/* I - Specifier string or @code NULL@ */
{
  ppd_attr_t	*attr;			/* Current attribute */


 /*
  * Range check input...
  */

  if (!ppd || !name || ppd->num_attrs == 0)
    return (NULL);

 /*
  * See if there are more attributes to return...
  */

  while ((attr = (ppd_attr_t *)cupsArrayNext(ppd->sorted_attrs)) != NULL)
  {
   /*
    * Check the next attribute to see if it is a match...
    */

    if (_cups_strcasecmp(attr->name, name))
    {
     /*
      * Nope, reset the current pointer to the end of the array...
      */

      cupsArrayIndex(ppd->sorted_attrs, cupsArrayCount(ppd->sorted_attrs));

      return (NULL);
    }

    if (!spec || !_cups_strcasecmp(attr->spec, spec))
      break;
  }

 /*
  * Return the next attribute's value...
  */

  return (attr);
}


/*
 * '_ppdNormalizeMakeAndModel()' - Normalize a product/make-and-model string.
 *
 * This function tries to undo the mistakes made by many printer manufacturers
 * to produce a clean make-and-model string we can use.
 */

char *					/* O - Normalized make-and-model string or NULL on error */
_ppdNormalizeMakeAndModel(
    const char *make_and_model,		/* I - Original make-and-model string */
    char       *buffer,			/* I - String buffer */
    size_t     bufsize)			/* I - Size of string buffer */
{
  char	*bufptr;			/* Pointer into buffer */


  if (!make_and_model || !buffer || bufsize < 1)
  {
    if (buffer)
      *buffer = '\0';

    return (NULL);
  }

 /*
  * Skip leading whitespace...
  */

  while (_cups_isspace(*make_and_model))
    make_and_model ++;

 /*
  * Remove parenthesis and add manufacturers as needed...
  */

  if (make_and_model[0] == '(')
  {
    strlcpy(buffer, make_and_model + 1, bufsize);

    if ((bufptr = strrchr(buffer, ')')) != NULL)
      *bufptr = '\0';
  }
  else if (!_cups_strncasecmp(make_and_model, "XPrint ", 7))
  {
   /*
    * Xerox XPrint...
    * Note: We check for the space after XPrint to ensure we do not display
    * Xerox for Xprinter devices, which are NOT by Xerox.
    */

    snprintf(buffer, bufsize, "Xerox %s", make_and_model);
  }
  else if (!_cups_strncasecmp(make_and_model, "Eastman", 7))
  {
   /*
    * Kodak...
    */

    snprintf(buffer, bufsize, "Kodak %s", make_and_model + 7);
  }
  else if (!_cups_strncasecmp(make_and_model, "laserwriter", 11))
  {
   /*
    * Apple LaserWriter...
    */

    snprintf(buffer, bufsize, "Apple LaserWriter%s", make_and_model + 11);
  }
  else if (!_cups_strncasecmp(make_and_model, "colorpoint", 10))
  {
   /*
    * Seiko...
    */

    snprintf(buffer, bufsize, "Seiko %s", make_and_model);
  }
  else if (!_cups_strncasecmp(make_and_model, "fiery", 5))
  {
   /*
    * EFI...
    */

    snprintf(buffer, bufsize, "EFI %s", make_and_model);
  }
  else if (!_cups_strncasecmp(make_and_model, "ps ", 3) ||
	   !_cups_strncasecmp(make_and_model, "colorpass", 9))
  {
   /*
    * Canon...
    */

    snprintf(buffer, bufsize, "Canon %s", make_and_model);
  }
  else if (!_cups_strncasecmp(make_and_model, "designjet", 9) ||
           !_cups_strncasecmp(make_and_model, "deskjet", 7))
  {
   /*
    * HP...
    */

    snprintf(buffer, bufsize, "HP %s", make_and_model);
  }
  else
    strlcpy(buffer, make_and_model, bufsize);

 /*
  * Clean up the make...
  */

  if (!_cups_strncasecmp(buffer, "agfa", 4))
  {
   /*
    * Replace with AGFA (all uppercase)...
    */

    buffer[0] = 'A';
    buffer[1] = 'G';
    buffer[2] = 'F';
    buffer[3] = 'A';
  }
  else if (!_cups_strncasecmp(buffer, "Hewlett-Packard hp ", 19))
  {
   /*
    * Just put "HP" on the front...
    */

    buffer[0] = 'H';
    buffer[1] = 'P';
    _cups_strcpy(buffer + 2, buffer + 18);
  }
  else if (!_cups_strncasecmp(buffer, "Hewlett-Packard ", 16))
  {
   /*
    * Just put "HP" on the front...
    */

    buffer[0] = 'H';
    buffer[1] = 'P';
    _cups_strcpy(buffer + 2, buffer + 15);
  }
  else if (!_cups_strncasecmp(buffer, "Lexmark International", 21))
  {
   /*
    * Strip "International"...
    */

    _cups_strcpy(buffer + 8, buffer + 21);
  }
  else if (!_cups_strncasecmp(buffer, "herk", 4))
  {
   /*
    * Replace with LHAG...
    */

    buffer[0] = 'L';
    buffer[1] = 'H';
    buffer[2] = 'A';
    buffer[3] = 'G';
  }
  else if (!_cups_strncasecmp(buffer, "linotype", 8))
  {
   /*
    * Replace with LHAG...
    */

    buffer[0] = 'L';
    buffer[1] = 'H';
    buffer[2] = 'A';
    buffer[3] = 'G';
    _cups_strcpy(buffer + 4, buffer + 8);
  }

 /*
  * Remove trailing whitespace and return...
  */

  for (bufptr = buffer + strlen(buffer) - 1;
       bufptr >= buffer && _cups_isspace(*bufptr);
       bufptr --);

  bufptr[1] = '\0';

  return (buffer[0] ? buffer : NULL);
}
