/*
 * PPD code emission routines for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright 2007-2019 by Apple Inc.
 * Copyright 1997-2007 by Easy Software Products, all rights reserved.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 *
 * PostScript is a trademark of Adobe Systems, Inc.
 */

/*
 * Include necessary headers...
 */

#include "cups-private.h"
#include "debug-internal.h"
#include "ppd.h"
#if defined(_WIN32) || defined(__EMX__)
#  include <io.h>
#else
#  include <unistd.h>
#endif /* _WIN32 || __EMX__ */


/*
 * Local functions...
 */

static int	ppd_compare_cparams(ppd_cparam_t *a, ppd_cparam_t *b);
static void	ppd_handle_media(ppd_file_t *ppd);


/*
 * Local globals...
 */

static const char ppd_custom_code[] =
		"pop pop pop\n"
		"<</PageSize[5 -2 roll]/ImagingBBox null>>setpagedevice\n";


/*
 * 'ppdCollect()' - Collect all marked options that reside in the specified
 *                  section.
 *
 * The choices array should be freed using @code free@ when you are
 * finished with it.
 */

int					/* O - Number of options marked */
ppdCollect(ppd_file_t    *ppd,		/* I - PPD file data */
           ppd_section_t section,	/* I - Section to collect */
           ppd_choice_t  ***choices)	/* O - Pointers to choices */
{
  return (ppdCollect2(ppd, section, 0.0, choices));
}


/*
 * 'ppdCollect2()' - Collect all marked options that reside in the
 *                   specified section and minimum order.
 *
 * The choices array should be freed using @code free@ when you are
 * finished with it.
 *
 * @since CUPS 1.2/macOS 10.5@
 */

int					/* O - Number of options marked */
ppdCollect2(ppd_file_t    *ppd,		/* I - PPD file data */
            ppd_section_t section,	/* I - Section to collect */
	    float         min_order,	/* I - Minimum OrderDependency value */
            ppd_choice_t  ***choices)	/* O - Pointers to choices */
{
  ppd_choice_t	*c;			/* Current choice */
  ppd_section_t	csection;		/* Current section */
  float		corder;			/* Current OrderDependency value */
  int		count;			/* Number of choices collected */
  ppd_choice_t	**collect;		/* Collected choices */
  float		*orders;		/* Collected order values */


  DEBUG_printf(("ppdCollect2(ppd=%p, section=%d, min_order=%f, choices=%p)",
                ppd, section, min_order, choices));

  if (!ppd || !choices)
  {
    if (choices)
      *choices = NULL;

    return (0);
  }

 /*
  * Allocate memory for up to N selected choices...
  */

  count = 0;
  if ((collect = calloc((size_t)cupsArrayCount(ppd->marked),
                        sizeof(ppd_choice_t *))) == NULL)
  {
    *choices = NULL;
    return (0);
  }

  if ((orders = calloc((size_t)cupsArrayCount(ppd->marked), sizeof(float))) == NULL)
  {
    *choices = NULL;
    free(collect);
    return (0);
  }

 /*
  * Loop through all options and add choices as needed...
  */

  for (c = (ppd_choice_t *)cupsArrayFirst(ppd->marked);
       c;
       c = (ppd_choice_t *)cupsArrayNext(ppd->marked))
  {
    csection = c->option->section;
    corder   = c->option->order;

    if (!strcmp(c->choice, "Custom"))
    {
      ppd_attr_t	*attr;		/* NonUIOrderDependency value */
      float		aorder;		/* Order value */
      char		asection[17],	/* Section name */
			amain[PPD_MAX_NAME + 1],
			aoption[PPD_MAX_NAME];
					/* *CustomFoo and True */


      for (attr = ppdFindAttr(ppd, "NonUIOrderDependency", NULL);
           attr;
	   attr = ppdFindNextAttr(ppd, "NonUIOrderDependency", NULL))
        if (attr->value &&
	    sscanf(attr->value, "%f%16s%41s%40s", &aorder, asection, amain,
	           aoption) == 4 &&
	    !strncmp(amain, "*Custom", 7) &&
	    !strcmp(amain + 7, c->option->keyword) && !strcmp(aoption, "True"))
	{
	 /*
	  * Use this NonUIOrderDependency...
	  */

          corder = aorder;

	  if (!strcmp(asection, "DocumentSetup"))
	    csection = PPD_ORDER_DOCUMENT;
	  else if (!strcmp(asection, "ExitServer"))
	    csection = PPD_ORDER_EXIT;
	  else if (!strcmp(asection, "JCLSetup"))
	    csection = PPD_ORDER_JCL;
	  else if (!strcmp(asection, "PageSetup"))
	    csection = PPD_ORDER_PAGE;
	  else if (!strcmp(asection, "Prolog"))
	    csection = PPD_ORDER_PROLOG;
	  else
	    csection = PPD_ORDER_ANY;

	  break;
	}
    }

    if (csection == section && corder >= min_order)
    {
      collect[count] = c;
      orders[count]  = corder;
      count ++;
    }
  }

 /*
  * If we have more than 1 marked choice, sort them...
  */

  if (count > 1)
  {
    int i, j;				/* Looping vars */

    for (i = 0; i < (count - 1); i ++)
      for (j = i + 1; j < count; j ++)
        if (orders[i] > orders[j])
	{
	  c          = collect[i];
	  corder     = orders[i];
	  collect[i] = collect[j];
	  orders[i]  = orders[j];
	  collect[j] = c;
	  orders[j]  = corder;
	}
  }

  free(orders);

  DEBUG_printf(("2ppdCollect2: %d marked choices...", count));

 /*
  * Return the array and number of choices; if 0, free the array since
  * it isn't needed.
  */

  if (count > 0)
  {
    *choices = collect;
    return (count);
  }
  else
  {
    *choices = NULL;
    free(collect);
    return (0);
  }
}


/*
 * 'ppdEmit()' - Emit code for marked options to a file.
 */

int					/* O - 0 on success, -1 on failure */
ppdEmit(ppd_file_t    *ppd,		/* I - PPD file record */
        FILE          *fp,		/* I - File to write to */
        ppd_section_t section)		/* I - Section to write */
{
  return (ppdEmitAfterOrder(ppd, fp, section, 0, 0.0));
}


/*
 * 'ppdEmitAfterOrder()' - Emit a subset of the code for marked options to a file.
 *
 * When "limit" is non-zero, this function only emits options whose
 * OrderDependency value is greater than or equal to "min_order".
 *
 * When "limit" is zero, this function is identical to ppdEmit().
 *
 * @since CUPS 1.2/macOS 10.5@
 */

int					/* O - 0 on success, -1 on failure */
ppdEmitAfterOrder(
    ppd_file_t    *ppd,			/* I - PPD file record */
    FILE          *fp,			/* I - File to write to */
    ppd_section_t section,		/* I - Section to write */
    int		  limit,		/* I - Non-zero to use min_order */
    float         min_order)		/* I - Lowest OrderDependency */
{
  char	*buffer;			/* Option code */
  int	status;				/* Return status */


 /*
  * Range check input...
  */

  if (!ppd || !fp)
    return (-1);

 /*
  * Get the string...
  */

  buffer = ppdEmitString(ppd, section, limit ? min_order : 0.0f);

 /*
  * Write it as needed and return...
  */

  if (buffer)
  {
    status = fputs(buffer, fp) < 0 ? -1 : 0;

    free(buffer);
  }
  else
    status = 0;

  return (status);
}


/*
 * 'ppdEmitFd()' - Emit code for marked options to a file.
 */

int					/* O - 0 on success, -1 on failure */
ppdEmitFd(ppd_file_t    *ppd,		/* I - PPD file record */
          int           fd,		/* I - File to write to */
          ppd_section_t section)	/* I - Section to write */
{
  char		*buffer,		/* Option code */
		*bufptr;		/* Pointer into code */
  size_t	buflength;		/* Length of option code */
  ssize_t	bytes;			/* Bytes written */
  int		status;			/* Return status */


 /*
  * Range check input...
  */

  if (!ppd || fd < 0)
    return (-1);

 /*
  * Get the string...
  */

  buffer = ppdEmitString(ppd, section, 0.0);

 /*
  * Write it as needed and return...
  */

  if (buffer)
  {
    buflength = strlen(buffer);
    bufptr    = buffer;
    bytes     = 0;

    while (buflength > 0)
    {
#ifdef _WIN32
      if ((bytes = (ssize_t)write(fd, bufptr, (unsigned)buflength)) < 0)
#else
      if ((bytes = write(fd, bufptr, buflength)) < 0)
#endif /* _WIN32 */
      {
        if (errno == EAGAIN || errno == EINTR)
	  continue;

	break;
      }

      buflength -= (size_t)bytes;
      bufptr    += bytes;
    }

    status = bytes < 0 ? -1 : 0;

    free(buffer);
  }
  else
    status = 0;

  return (status);
}


/*
 * 'ppdEmitJCL()' - Emit code for JCL options to a file.
 */

int					/* O - 0 on success, -1 on failure */
ppdEmitJCL(ppd_file_t *ppd,		/* I - PPD file record */
           FILE       *fp,		/* I - File to write to */
           int        job_id,		/* I - Job ID */
	   const char *user,		/* I - Username */
	   const char *title)		/* I - Title */
{
  char		*ptr;			/* Pointer into JCL string */
  char		temp[65],		/* Local title string */
		displaymsg[33];		/* Local display string */


 /*
  * Range check the input...
  */

  if (!ppd || !ppd->jcl_begin || !ppd->jcl_ps)
    return (0);

 /*
  * See if the printer supports HP PJL...
  */

  if (!strncmp(ppd->jcl_begin, "\033%-12345X@", 10))
  {
   /*
    * This printer uses HP PJL commands for output; filter the output
    * so that we only have a single "@PJL JOB" command in the header...
    *
    * To avoid bugs in the PJL implementation of certain vendors' products
    * (Xerox in particular), we add a dummy "@PJL" command at the beginning
    * of the PJL commands to initialize PJL processing.
    */

    ppd_attr_t	*charset;		/* PJL charset */
    ppd_attr_t	*display;		/* PJL display command */


    if ((charset = ppdFindAttr(ppd, "cupsPJLCharset", NULL)) != NULL)
    {
      if (!charset->value || _cups_strcasecmp(charset->value, "UTF-8"))
        charset = NULL;
    }

    if ((display = ppdFindAttr(ppd, "cupsPJLDisplay", NULL)) != NULL)
    {
      if (!display->value)
        display = NULL;
    }

    fputs("\033%-12345X@PJL\n", fp);
    for (ptr = ppd->jcl_begin + 9; *ptr;)
      if (!strncmp(ptr, "@PJL JOB", 8))
      {
       /*
        * Skip job command...
	*/

        for (;*ptr; ptr ++)
	  if (*ptr == '\n')
	    break;

	if (*ptr)
	  ptr ++;
      }
      else
      {
       /*
        * Copy line...
	*/

        for (;*ptr; ptr ++)
	{
	  putc(*ptr, fp);
	  if (*ptr == '\n')
	    break;
	}

	if (*ptr)
	  ptr ++;
      }

   /*
    * Clean up the job title...
    */

    if (!title)
      title = "Unknown";

    if ((ptr = strrchr(title, '/')) != NULL)
    {
     /*
      * Only show basename of file path...
      */

      title = ptr + 1;
    }

    if (!strncmp(title, "smbprn.", 7))
    {
     /*
      * Skip leading smbprn.######## from Samba jobs...
      */

      for (title += 7; *title && isdigit(*title & 255); title ++);
      while (_cups_isspace(*title))
        title ++;

      if ((ptr = strstr(title, " - ")) != NULL)
      {
       /*
	* Skip application name in "Some Application - Title of job"...
	*/

	title = ptr + 3;
      }
    }

   /*
    * Replace double quotes with single quotes and UTF-8 characters with
    * question marks so that the title does not cause a PJL syntax error.
    */

    strlcpy(temp, title, sizeof(temp));

    for (ptr = temp; *ptr; ptr ++)
      if (*ptr == '\"')
        *ptr = '\'';
      else if (!charset && (*ptr & 128))
        *ptr = '?';

   /*
    * CUPS STR #3125: Long PJL JOB NAME causes problems with some printers
    *
    * Generate the display message, truncating at 32 characters + nul to avoid
    * issues with some printer's PJL implementations...
    */

    if (!user)
      user = "anonymous";

    snprintf(displaymsg, sizeof(displaymsg), "%d %s %s", job_id, user, temp);

   /*
    * Send PJL JOB and PJL RDYMSG commands before we enter PostScript mode...
    */

    if (display && strcmp(display->value, "job"))
      fprintf(fp, "@PJL JOB NAME = \"%s\"\n", temp);
    else if (display && !strcmp(display->value, "rdymsg"))
      fprintf(fp, "@PJL RDYMSG DISPLAY = \"%s\"\n", displaymsg);
    else
      fprintf(fp, "@PJL JOB NAME = \"%s\" DISPLAY = \"%s\"\n", temp,
	      displaymsg);

   /*
    * Replace double quotes with single quotes and UTF-8 characters with
    * question marks so that the user does not cause a PJL syntax error.
    */

    strlcpy(temp, user, sizeof(temp));

    for (ptr = temp; *ptr; ptr ++)
      if (*ptr == '\"')
        *ptr = '\'';
      else if (!charset && (*ptr & 128))
        *ptr = '?';

    fprintf(fp, "@PJL SET USERNAME = \"%s\"\n", temp);
  }
  else
    fputs(ppd->jcl_begin, fp);

  ppdEmit(ppd, fp, PPD_ORDER_JCL);
  fputs(ppd->jcl_ps, fp);

  return (0);
}


/*
 * 'ppdEmitJCLEnd()' - Emit JCLEnd code to a file.
 *
 * @since CUPS 1.2/macOS 10.5@
 */

int					/* O - 0 on success, -1 on failure */
ppdEmitJCLEnd(ppd_file_t *ppd,		/* I - PPD file record */
              FILE       *fp)		/* I - File to write to */
{
 /*
  * Range check the input...
  */

  if (!ppd)
    return (0);

  if (!ppd->jcl_end)
  {
    if (ppd->num_filters == 0)
      putc(0x04, fp);

    return (0);
  }

 /*
  * See if the printer supports HP PJL...
  */

  if (!strncmp(ppd->jcl_end, "\033%-12345X@", 10))
  {
   /*
    * This printer uses HP PJL commands for output; filter the output
    * so that we only have a single "@PJL JOB" command in the header...
    *
    * To avoid bugs in the PJL implementation of certain vendors' products
    * (Xerox in particular), we add a dummy "@PJL" command at the beginning
    * of the PJL commands to initialize PJL processing.
    */

    fputs("\033%-12345X@PJL\n", fp);
    fputs("@PJL RDYMSG DISPLAY = \"\"\n", fp);
    fputs(ppd->jcl_end + 9, fp);
  }
  else
    fputs(ppd->jcl_end, fp);

  return (0);
}


/*
 * 'ppdEmitString()' - Get a string containing the code for marked options.
 *
 * When "min_order" is greater than zero, this function only includes options
 * whose OrderDependency value is greater than or equal to "min_order".
 * Otherwise, all options in the specified section are included in the
 * returned string.
 *
 * The return string is allocated on the heap and should be freed using
 * @code free@ when you are done with it.
 *
 * @since CUPS 1.2/macOS 10.5@
 */

char *					/* O - String containing option code or @code NULL@ if there is no option code */
ppdEmitString(ppd_file_t    *ppd,	/* I - PPD file record */
              ppd_section_t section,	/* I - Section to write */
	      float         min_order)	/* I - Lowest OrderDependency */
{
  int		i, j,			/* Looping vars */
		count;			/* Number of choices */
  ppd_choice_t	**choices;		/* Choices */
  ppd_size_t	*size;			/* Custom page size */
  ppd_coption_t	*coption;		/* Custom option */
  ppd_cparam_t	*cparam;		/* Custom parameter */
  size_t	bufsize;		/* Size of string buffer needed */
  char		*buffer,		/* String buffer */
		*bufptr,		/* Pointer into buffer */
		*bufend;		/* End of buffer */
  struct lconv	*loc;			/* Locale data */


  DEBUG_printf(("ppdEmitString(ppd=%p, section=%d, min_order=%f)",
                ppd, section, min_order));

 /*
  * Range check input...
  */

  if (!ppd)
    return (NULL);

 /*
  * Use PageSize or PageRegion as required...
  */

  ppd_handle_media(ppd);

 /*
  * Collect the options we need to emit...
  */

  if ((count = ppdCollect2(ppd, section, min_order, &choices)) == 0)
    return (NULL);

 /*
  * Count the number of bytes that are required to hold all of the
  * option code...
  */

  for (i = 0, bufsize = 1; i < count; i ++)
  {
    if (section == PPD_ORDER_JCL)
    {
      if (!_cups_strcasecmp(choices[i]->choice, "Custom") &&
	  (coption = ppdFindCustomOption(ppd, choices[i]->option->keyword))
	      != NULL)
      {
       /*
        * Add space to account for custom parameter substitution...
	*/

        for (cparam = (ppd_cparam_t *)cupsArrayFirst(coption->params);
	     cparam;
	     cparam = (ppd_cparam_t *)cupsArrayNext(coption->params))
	{
          switch (cparam->type)
	  {
	    case PPD_CUSTOM_UNKNOWN :
	        break;

	    case PPD_CUSTOM_CURVE :
	    case PPD_CUSTOM_INVCURVE :
	    case PPD_CUSTOM_POINTS :
	    case PPD_CUSTOM_REAL :
	    case PPD_CUSTOM_INT :
	        bufsize += 10;
	        break;

	    case PPD_CUSTOM_PASSCODE :
	    case PPD_CUSTOM_PASSWORD :
	    case PPD_CUSTOM_STRING :
	        if (cparam->current.custom_string)
		  bufsize += strlen(cparam->current.custom_string);
	        break;
          }
	}
      }
    }
    else if (section != PPD_ORDER_EXIT)
    {
      bufsize += 3;			/* [{\n */

      if ((!_cups_strcasecmp(choices[i]->option->keyword, "PageSize") ||
           !_cups_strcasecmp(choices[i]->option->keyword, "PageRegion")) &&
          !_cups_strcasecmp(choices[i]->choice, "Custom"))
      {
        DEBUG_puts("2ppdEmitString: Custom size set!");

        bufsize += 37;			/* %%BeginFeature: *CustomPageSize True\n */
        bufsize += 50;			/* Five 9-digit numbers + newline */
      }
      else if (!_cups_strcasecmp(choices[i]->choice, "Custom") &&
               (coption = ppdFindCustomOption(ppd,
	                                      choices[i]->option->keyword))
	           != NULL)
      {
        bufsize += 23 + strlen(choices[i]->option->keyword) + 6;
					/* %%BeginFeature: *Customkeyword True\n */


        for (cparam = (ppd_cparam_t *)cupsArrayFirst(coption->params);
	     cparam;
	     cparam = (ppd_cparam_t *)cupsArrayNext(coption->params))
	{
          switch (cparam->type)
	  {
	    case PPD_CUSTOM_UNKNOWN :
	        break;

	    case PPD_CUSTOM_CURVE :
	    case PPD_CUSTOM_INVCURVE :
	    case PPD_CUSTOM_POINTS :
	    case PPD_CUSTOM_REAL :
	    case PPD_CUSTOM_INT :
	        bufsize += 10;
	        break;

	    case PPD_CUSTOM_PASSCODE :
	    case PPD_CUSTOM_PASSWORD :
	    case PPD_CUSTOM_STRING :
		bufsize += 3;
	        if (cparam->current.custom_string)
		  bufsize += 4 * strlen(cparam->current.custom_string);
	        break;
          }
	}
      }
      else
        bufsize += 17 + strlen(choices[i]->option->keyword) + 1 +
	           strlen(choices[i]->choice) + 1;
					/* %%BeginFeature: *keyword choice\n */

      bufsize += 13;			/* %%EndFeature\n */
      bufsize += 22;			/* } stopped cleartomark\n */
    }

    if (choices[i]->code)
      bufsize += strlen(choices[i]->code) + 1;
    else
      bufsize += strlen(ppd_custom_code);
  }

 /*
  * Allocate memory...
  */

  DEBUG_printf(("2ppdEmitString: Allocating %d bytes for string...",
                (int)bufsize));

  if ((buffer = calloc(1, bufsize)) == NULL)
  {
    free(choices);
    return (NULL);
  }

  bufend = buffer + bufsize - 1;
  loc    = localeconv();

 /*
  * Copy the option code to the buffer...
  */

  for (i = 0, bufptr = buffer; i < count; i ++, bufptr += strlen(bufptr))
    if (section == PPD_ORDER_JCL)
    {
      if (!_cups_strcasecmp(choices[i]->choice, "Custom") &&
	  choices[i]->code &&
          (coption = ppdFindCustomOption(ppd, choices[i]->option->keyword))
	      != NULL)
      {
       /*
        * Handle substitutions in custom JCL options...
	*/

	char	*cptr;			/* Pointer into code */
	int	pnum;			/* Parameter number */


        for (cptr = choices[i]->code; *cptr && bufptr < bufend;)
	{
	  if (*cptr == '\\')
	  {
	    cptr ++;

	    if (isdigit(*cptr & 255))
	    {
	     /*
	      * Substitute parameter...
	      */

              pnum = *cptr++ - '0';
	      while (isdigit(*cptr & 255))
	        pnum = pnum * 10 + *cptr++ - '0';

              for (cparam = (ppd_cparam_t *)cupsArrayFirst(coption->params);
	           cparam;
		   cparam = (ppd_cparam_t *)cupsArrayNext(coption->params))
		if (cparam->order == pnum)
		  break;

              if (cparam)
	      {
	        switch (cparam->type)
		{
		  case PPD_CUSTOM_UNKNOWN :
		      break;

		  case PPD_CUSTOM_CURVE :
		  case PPD_CUSTOM_INVCURVE :
		  case PPD_CUSTOM_POINTS :
		  case PPD_CUSTOM_REAL :
		      bufptr = _cupsStrFormatd(bufptr, bufend,
					       cparam->current.custom_real,
					       loc);
		      break;

		  case PPD_CUSTOM_INT :
		      snprintf(bufptr, (size_t)(bufend - bufptr), "%d", cparam->current.custom_int);
		      bufptr += strlen(bufptr);
		      break;

		  case PPD_CUSTOM_PASSCODE :
		  case PPD_CUSTOM_PASSWORD :
		  case PPD_CUSTOM_STRING :
		      if (cparam->current.custom_string)
		      {
			strlcpy(bufptr, cparam->current.custom_string, (size_t)(bufend - bufptr));
			bufptr += strlen(bufptr);
		      }
		      break;
		}
	      }
	    }
	    else if (*cptr)
	      *bufptr++ = *cptr++;
	  }
	  else
	    *bufptr++ = *cptr++;
	}
      }
      else if (choices[i]->code)
      {
       /*
        * Otherwise just copy the option code directly...
	*/

        strlcpy(bufptr, choices[i]->code, (size_t)(bufend - bufptr + 1));
        bufptr += strlen(bufptr);
      }
    }
    else if (section != PPD_ORDER_EXIT)
    {
     /*
      * Add wrapper commands to prevent printer errors for unsupported
      * options...
      */

      strlcpy(bufptr, "[{\n", (size_t)(bufend - bufptr + 1));
      bufptr += 3;

     /*
      * Send DSC comments with option...
      */

      DEBUG_printf(("2ppdEmitString: Adding code for %s=%s...",
		    choices[i]->option->keyword, choices[i]->choice));

      if ((!_cups_strcasecmp(choices[i]->option->keyword, "PageSize") ||
           !_cups_strcasecmp(choices[i]->option->keyword, "PageRegion")) &&
          !_cups_strcasecmp(choices[i]->choice, "Custom"))
      {
       /*
        * Variable size; write out standard size options, using the
	* parameter positions defined in the PPD file...
	*/

        ppd_attr_t	*attr;		/* PPD attribute */
	int		pos,		/* Position of custom value */
			orientation;	/* Orientation to use */
	float		values[5];	/* Values for custom command */


        strlcpy(bufptr, "%%BeginFeature: *CustomPageSize True\n", (size_t)(bufend - bufptr + 1));
        bufptr += 37;

        if ((size = ppdPageSize(ppd, "Custom")) == NULL)
        {
          free(buffer);
          free(choices);
          return (NULL);
        }

        memset(values, 0, sizeof(values));

	if ((attr = ppdFindAttr(ppd, "ParamCustomPageSize", "Width")) != NULL)
	{
	  pos = atoi(attr->value) - 1;

          if (pos < 0 || pos > 4)
	    pos = 0;
	}
	else
	  pos = 0;

	values[pos] = size->width;

	if ((attr = ppdFindAttr(ppd, "ParamCustomPageSize", "Height")) != NULL)
	{
	  pos = atoi(attr->value) - 1;

          if (pos < 0 || pos > 4)
	    pos = 1;
	}
	else
	  pos = 1;

	values[pos] = size->length;

       /*
        * According to the Adobe PPD specification, an orientation of 1
	* will produce a print that comes out upside-down with the X
	* axis perpendicular to the direction of feed, which is exactly
	* what we want to be consistent with non-PS printers.
	*
	* We could also use an orientation of 3 to produce output that
	* comes out rightside-up (this is the default for many large format
	* printer PPDs), however for consistency we will stick with the
	* value 1.
	*
	* If we wanted to get fancy, we could use orientations of 0 or
	* 2 and swap the width and length, however we don't want to get
	* fancy, we just want it to work consistently.
	*
	* The orientation value is range limited by the Orientation
	* parameter definition, so certain non-PS printer drivers that
	* only support an Orientation of 0 will get the value 0 as
	* expected.
	*/

        orientation = 1;

	if ((attr = ppdFindAttr(ppd, "ParamCustomPageSize",
	                        "Orientation")) != NULL)
	{
	  int min_orient, max_orient;	/* Minimum and maximum orientations */


          if (sscanf(attr->value, "%d%*s%d%d", &pos, &min_orient,
	             &max_orient) != 3)
	    pos = 4;
	  else
	  {
	    pos --;

            if (pos < 0 || pos > 4)
	      pos = 4;

            if (orientation > max_orient)
	      orientation = max_orient;
	    else if (orientation < min_orient)
	      orientation = min_orient;
	  }
	}
	else
	  pos = 4;

	values[pos] = (float)orientation;

        for (pos = 0; pos < 5; pos ++)
	{
	  bufptr    = _cupsStrFormatd(bufptr, bufend, values[pos], loc);
	  *bufptr++ = '\n';
        }

	if (!choices[i]->code)
	{
	 /*
	  * This can happen with certain buggy PPD files that don't include
	  * a CustomPageSize command sequence...  We just use a generic
	  * Level 2 command sequence...
	  */

	  strlcpy(bufptr, ppd_custom_code, (size_t)(bufend - bufptr + 1));
          bufptr += strlen(bufptr);
	}
      }
      else if (!_cups_strcasecmp(choices[i]->choice, "Custom") &&
               (coption = ppdFindCustomOption(ppd, choices[i]->option->keyword))
	           != NULL)
      {
       /*
        * Custom option...
	*/

        const char	*s;		/* Pointer into string value */
        cups_array_t	*params;	/* Parameters in the correct output order */


        params = cupsArrayNew((cups_array_func_t)ppd_compare_cparams, NULL);

        for (cparam = (ppd_cparam_t *)cupsArrayFirst(coption->params);
	     cparam;
	     cparam = (ppd_cparam_t *)cupsArrayNext(coption->params))
          cupsArrayAdd(params, cparam);

        snprintf(bufptr, (size_t)(bufend - bufptr + 1), "%%%%BeginFeature: *Custom%s True\n", coption->keyword);
        bufptr += strlen(bufptr);

        for (cparam = (ppd_cparam_t *)cupsArrayFirst(params);
	     cparam;
	     cparam = (ppd_cparam_t *)cupsArrayNext(params))
	{
          switch (cparam->type)
	  {
	    case PPD_CUSTOM_UNKNOWN :
	        break;

	    case PPD_CUSTOM_CURVE :
	    case PPD_CUSTOM_INVCURVE :
	    case PPD_CUSTOM_POINTS :
	    case PPD_CUSTOM_REAL :
	        bufptr    = _cupsStrFormatd(bufptr, bufend,
		                            cparam->current.custom_real, loc);
                *bufptr++ = '\n';
	        break;

	    case PPD_CUSTOM_INT :
	        snprintf(bufptr, (size_t)(bufend - bufptr + 1), "%d\n", cparam->current.custom_int);
		bufptr += strlen(bufptr);
	        break;

	    case PPD_CUSTOM_PASSCODE :
	    case PPD_CUSTOM_PASSWORD :
	    case PPD_CUSTOM_STRING :
	        *bufptr++ = '(';

		if (cparam->current.custom_string)
		{
		  for (s = cparam->current.custom_string; *s; s ++)
		  {
		    if (*s < ' ' || *s == '(' || *s == ')' || *s >= 127)
		    {
		      snprintf(bufptr, (size_t)(bufend - bufptr + 1), "\\%03o", *s & 255);
		      bufptr += strlen(bufptr);
		    }
		    else
		      *bufptr++ = *s;
		  }
		}

	        *bufptr++ = ')';
		*bufptr++ = '\n';
	        break;
          }
	}

	cupsArrayDelete(params);
      }
      else
      {
        snprintf(bufptr, (size_t)(bufend - bufptr + 1), "%%%%BeginFeature: *%s %s\n", choices[i]->option->keyword, choices[i]->choice);
	bufptr += strlen(bufptr);
      }

      if (choices[i]->code && choices[i]->code[0])
      {
        j = (int)strlen(choices[i]->code);
	memcpy(bufptr, choices[i]->code, (size_t)j);
	bufptr += j;

	if (choices[i]->code[j - 1] != '\n')
	  *bufptr++ = '\n';
      }

      strlcpy(bufptr, "%%EndFeature\n"
		      "} stopped cleartomark\n", (size_t)(bufend - bufptr + 1));
      bufptr += strlen(bufptr);

      DEBUG_printf(("2ppdEmitString: Offset in string is %d...",
                    (int)(bufptr - buffer)));
    }
    else if (choices[i]->code)
    {
      strlcpy(bufptr, choices[i]->code, (size_t)(bufend - bufptr + 1));
      bufptr += strlen(bufptr);
    }

 /*
  * Nul-terminate, free, and return...
  */

  *bufptr = '\0';

  free(choices);

  return (buffer);
}


/*
 * 'ppd_compare_cparams()' - Compare the order of two custom parameters.
 */

static int				/* O - Result of comparison */
ppd_compare_cparams(ppd_cparam_t *a,	/* I - First parameter */
                    ppd_cparam_t *b)	/* I - Second parameter */
{
  return (a->order - b->order);
}


/*
 * 'ppd_handle_media()' - Handle media selection...
 */

static void
ppd_handle_media(ppd_file_t *ppd)	/* I - PPD file */
{
  ppd_choice_t	*manual_feed,		/* ManualFeed choice, if any */
		*input_slot;		/* InputSlot choice, if any */
  ppd_size_t	*size;			/* Current media size */
  ppd_attr_t	*rpr;			/* RequiresPageRegion value */


 /*
  * This function determines what page size code to use, if any, for the
  * current media size, InputSlot, and ManualFeed selections.
  *
  * We use the PageSize code if:
  *
  * 1. A custom media size is selected.
  * 2. ManualFeed and InputSlot are not selected (or do not exist).
  * 3. ManualFeed is selected but is False and InputSlot is not selected or
  *    the selection has no code - the latter check done to support "auto" or
  *    "printer default" InputSlot options.
  *
  * We use the PageRegion code if:
  *
  * 4. RequiresPageRegion does not exist and the PPD contains cupsFilter
  *    keywords, indicating this is a CUPS-based driver.
  * 5. RequiresPageRegion exists for the selected InputSlot (or "All" for any
  *    InputSlot or ManualFeed selection) and is True.
  *
  * If none of the 5 conditions are true, no page size code is used and we
  * unmark any existing PageSize or PageRegion choices.
  */

  if ((size = ppdPageSize(ppd, NULL)) == NULL)
    return;

  manual_feed = ppdFindMarkedChoice(ppd, "ManualFeed");
  input_slot  = ppdFindMarkedChoice(ppd, "InputSlot");

  if (input_slot != NULL)
    rpr = ppdFindAttr(ppd, "RequiresPageRegion", input_slot->choice);
  else
    rpr = NULL;

  if (!rpr)
    rpr = ppdFindAttr(ppd, "RequiresPageRegion", "All");

  if (!_cups_strcasecmp(size->name, "Custom") ||
      (!manual_feed && !input_slot) ||
      (manual_feed && !_cups_strcasecmp(manual_feed->choice, "False") &&
       (!input_slot || (input_slot->code && !input_slot->code[0]))) ||
      (!rpr && ppd->num_filters > 0))
  {
   /*
    * Use PageSize code...
    */

    ppdMarkOption(ppd, "PageSize", size->name);
  }
  else if (rpr && rpr->value && !_cups_strcasecmp(rpr->value, "True"))
  {
   /*
    * Use PageRegion code...
    */

    ppdMarkOption(ppd, "PageRegion", size->name);
  }
  else
  {
   /*
    * Do not use PageSize or PageRegion code...
    */

    ppd_choice_t	*page;		/* PageSize/Region choice, if any */

    if ((page = ppdFindMarkedChoice(ppd, "PageSize")) != NULL)
    {
     /*
      * Unmark PageSize...
      */

      page->marked = 0;
      cupsArrayRemove(ppd->marked, page);
    }

    if ((page = ppdFindMarkedChoice(ppd, "PageRegion")) != NULL)
    {
     /*
      * Unmark PageRegion...
      */

      page->marked = 0;
      cupsArrayRemove(ppd->marked, page);
    }
  }
}
