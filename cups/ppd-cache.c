/*
 * PPD cache implementation for CUPS.
 *
 * Copyright © 2022-2024 by OpenPrinting.
 * Copyright © 2010-2021 by Apple Inc.
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
#include <math.h>


/*
 * Macro to test for two almost-equal PWG measurements.
 */

#define _PWG_EQUIVALENT(x, y)	(abs((x)-(y)) < 2)


/*
 * Local functions...
 */

static int	cups_connect(http_t **http, const char *url, char *resource, size_t ressize);
static int	cups_get_url(http_t **http, const char *url, char *name, size_t namesize);
static const char *ppd_inputslot_for_keyword(_ppd_cache_t *pc, const char *keyword);
static void	ppd_put_string(cups_file_t *fp, cups_lang_t *lang, cups_array_t *strings, const char *ppd_option, const char *ppd_choice, const char *pwg_msgid);
static void	pwg_add_finishing(cups_array_t *finishings, ipp_finishings_t template, const char *name, const char *value);
static void	pwg_add_message(cups_array_t *a, const char *msg, const char *str);
static int	pwg_compare_finishings(_pwg_finishings_t *a, _pwg_finishings_t *b);
static int	pwg_compare_sizes(cups_size_t *a, cups_size_t *b);
static cups_size_t *pwg_copy_size(cups_size_t *size);
static void	pwg_free_finishings(_pwg_finishings_t *f);
static void	pwg_ppdize_name(const char *ipp, char *name, size_t namesize);
static void	pwg_ppdize_resolution(ipp_attribute_t *attr, int element, int *xres, int *yres, char *name, size_t namesize);
static void	pwg_unppdize_name(const char *ppd, char *name, size_t namesize,
		                  const char *dashchars);


/*
 * '_cupsConvertOptions()' - Convert printer options to standard IPP attributes.
 *
 * This functions converts PPD and CUPS-specific options to their standard IPP
 * attributes and values and adds them to the specified IPP request.
 */

int					/* O - New number of copies */
_cupsConvertOptions(
    ipp_t           *request,		/* I - IPP request */
    ppd_file_t      *ppd,		/* I - PPD file */
    _ppd_cache_t    *pc,		/* I - PPD cache info */
    ipp_attribute_t *media_col_sup,	/* I - media-col-supported values */
    ipp_attribute_t *doc_handling_sup,	/* I - multiple-document-handling-supported values */
    ipp_attribute_t *print_color_mode_sup,
                                	/* I - Printer supports print-color-mode */
    const char    *user,		/* I - User info */
    const char    *format,		/* I - document-format value */
    int           copies,		/* I - Number of copies */
    int           num_options,		/* I - Number of options */
    cups_option_t *options)		/* I - Options */
{
  int		i;			/* Looping var */
  const char	*keyword,		/* PWG keyword */
		*password;		/* Password string */
  pwg_size_t	*size;			/* PWG media size */
  ipp_t		*media_col,		/* media-col value */
		*media_size;		/* media-size value */
  const char	*media_source,		/* media-source value */
		*media_type,		/* media-type value */
		*collate_str,		/* multiple-document-handling value */
		*color_attr_name,	/* Supported color attribute */
		*mandatory,		/* Mandatory attributes */
		*finishing_template;	/* Finishing template */
  int		num_finishings = 0,	/* Number of finishing values */
		finishings[10];		/* Finishing enum values */
  ppd_choice_t	*choice;		/* Marked choice */
  int           finishings_copies = copies,
                                        /* Number of copies for finishings */
                job_pages = 0,		/* job-pages value */
		number_up = 1;		/* number-up value */
  const char	*value;			/* Option value */


 /*
  * Send standard IPP attributes...
  */

  if (pc->password && (password = cupsGetOption("job-password", num_options, options)) != NULL && ippGetOperation(request) != IPP_OP_VALIDATE_JOB)
  {
    ipp_attribute_t	*attr = NULL;	/* job-password attribute */

    if ((keyword = cupsGetOption("job-password-encryption", num_options, options)) == NULL)
      keyword = "none";

    if (!strcmp(keyword, "none"))
    {
     /*
      * Add plain-text job-password...
      */

      attr = ippAddOctetString(request, IPP_TAG_OPERATION, "job-password", password, (int)strlen(password));
    }
    else
    {
     /*
      * Add hashed job-password...
      */

      unsigned char	hash[64];	/* Hash of password */
      ssize_t		hashlen;	/* Length of hash */

      if ((hashlen = cupsHashData(keyword, password, strlen(password), hash, sizeof(hash))) > 0)
        attr = ippAddOctetString(request, IPP_TAG_OPERATION, "job-password", hash, (int)hashlen);
    }

    if (attr)
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "job-password-encryption", NULL, keyword);
  }

  if (pc->account_id)
  {
    if ((keyword = cupsGetOption("job-account-id", num_options, options)) == NULL)
      keyword = cupsGetOption("job-billing", num_options, options);

    if (keyword)
      ippAddString(request, IPP_TAG_JOB, IPP_TAG_NAME, "job-account-id", NULL, keyword);
  }

  if (pc->accounting_user_id)
  {
    if ((keyword = cupsGetOption("job-accounting-user-id", num_options, options)) == NULL)
      keyword = user;

    if (keyword)
      ippAddString(request, IPP_TAG_JOB, IPP_TAG_NAME, "job-accounting-user-id", NULL, keyword);
  }

  for (mandatory = (const char *)cupsArrayFirst(pc->mandatory); mandatory; mandatory = (const char *)cupsArrayNext(pc->mandatory))
  {
    if (strcmp(mandatory, "copies") &&
	strcmp(mandatory, "destination-uris") &&
	strcmp(mandatory, "finishings") &&
	strcmp(mandatory, "finishings-col") &&
	strcmp(mandatory, "finishing-template") &&
	strcmp(mandatory, "job-account-id") &&
	strcmp(mandatory, "job-accounting-user-id") &&
	strcmp(mandatory, "job-password") &&
	strcmp(mandatory, "job-password-encryption") &&
	strcmp(mandatory, "media") &&
	strncmp(mandatory, "media-col", 9) &&
	strcmp(mandatory, "multiple-document-handling") &&
	strcmp(mandatory, "output-bin") &&
	strcmp(mandatory, "print-color-mode") &&
	strcmp(mandatory, "print-quality") &&
	strcmp(mandatory, "sides") &&
	(keyword = cupsGetOption(mandatory, num_options, options)) != NULL)
    {
      _ipp_option_t *opt = _ippFindOption(mandatory);
				    /* Option type */
      ipp_tag_t	value_tag = opt ? opt->value_tag : IPP_TAG_NAME;
				    /* Value type */

      switch (value_tag)
      {
	case IPP_TAG_INTEGER :
	case IPP_TAG_ENUM :
	    ippAddInteger(request, IPP_TAG_JOB, value_tag, mandatory, atoi(keyword));
	    break;
	case IPP_TAG_BOOLEAN :
	    ippAddBoolean(request, IPP_TAG_JOB, mandatory, !_cups_strcasecmp(keyword, "true"));
	    break;
	case IPP_TAG_RANGE :
	    {
	      int lower, upper;	/* Range */

	      if (sscanf(keyword, "%d-%d", &lower, &upper) != 2)
		lower = upper = atoi(keyword);

	      ippAddRange(request, IPP_TAG_JOB, mandatory, lower, upper);
	    }
	    break;
	case IPP_TAG_STRING :
	    ippAddOctetString(request, IPP_TAG_JOB, mandatory, keyword, (int)strlen(keyword));
	    break;
	default :
	    if (!strcmp(mandatory, "print-color-mode") && !strcmp(keyword, "monochrome"))
	    {
	      if (ippContainsString(print_color_mode_sup, "auto-monochrome"))
		keyword = "auto-monochrome";
	      else if (ippContainsString(print_color_mode_sup, "process-monochrome") && !ippContainsString(print_color_mode_sup, "monochrome"))
		keyword = "process-monochrome";
	    }

	    ippAddString(request, IPP_TAG_JOB, value_tag, mandatory, NULL, keyword);
	    break;
      }
    }
  }

  if ((keyword = cupsGetOption("PageSize", num_options, options)) == NULL)
    keyword = cupsGetOption("media", num_options, options);

  media_source = _ppdCacheGetSource(pc, cupsGetOption("InputSlot", num_options, options));
  media_type   = _ppdCacheGetType(pc, cupsGetOption("MediaType", num_options, options));
  size         = _ppdCacheGetSize(pc, keyword);

  if (media_col_sup && (size || media_source || media_type))
  {
   /*
    * Add a media-col value...
    */

    media_col = ippNew();

    if (size)
    {
      media_size = ippNew();
      ippAddInteger(media_size, IPP_TAG_ZERO, IPP_TAG_INTEGER,
                    "x-dimension", size->width);
      ippAddInteger(media_size, IPP_TAG_ZERO, IPP_TAG_INTEGER,
                    "y-dimension", size->length);

      ippAddCollection(media_col, IPP_TAG_ZERO, "media-size", media_size);
    }

    for (i = 0; i < media_col_sup->num_values; i ++)
    {
      if (size && !strcmp(media_col_sup->values[i].string.text, "media-left-margin"))
	ippAddInteger(media_col, IPP_TAG_ZERO, IPP_TAG_INTEGER, "media-left-margin", size->left);
      else if (size && !strcmp(media_col_sup->values[i].string.text, "media-bottom-margin"))
	ippAddInteger(media_col, IPP_TAG_ZERO, IPP_TAG_INTEGER, "media-bottom-margin", size->bottom);
      else if (size && !strcmp(media_col_sup->values[i].string.text, "media-right-margin"))
	ippAddInteger(media_col, IPP_TAG_ZERO, IPP_TAG_INTEGER, "media-right-margin", size->right);
      else if (size && !strcmp(media_col_sup->values[i].string.text, "media-top-margin"))
	ippAddInteger(media_col, IPP_TAG_ZERO, IPP_TAG_INTEGER, "media-top-margin", size->top);
      else if (media_source && !strcmp(media_col_sup->values[i].string.text, "media-source"))
	ippAddString(media_col, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "media-source", NULL, media_source);
      else if (media_type && !strcmp(media_col_sup->values[i].string.text, "media-type"))
	ippAddString(media_col, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "media-type", NULL, media_type);
    }

    ippAddCollection(request, IPP_TAG_JOB, "media-col", media_col);
  }

  if ((keyword = cupsGetOption("output-bin", num_options, options)) == NULL)
  {
    if ((choice = ppdFindMarkedChoice(ppd, "OutputBin")) != NULL)
      keyword = _ppdCacheGetBin(pc, choice->choice);
  }

  if (keyword)
    ippAddString(request, IPP_TAG_JOB, IPP_TAG_KEYWORD, "output-bin", NULL, keyword);

  color_attr_name = print_color_mode_sup ? "print-color-mode" : "output-mode";

 /*
  * If we use PPD with standardized PPD option for color support - ColorModel,
  * prefer it to don't break color/grayscale support for PPDs, either classic
  * or the ones generated from IPP Get-Printer-Attributes response.
  */

  if ((keyword = cupsGetOption("ColorModel", num_options, options)) == NULL)
  {
   /*
    * No ColorModel in options...
    */

    if ((choice = ppdFindMarkedChoice(ppd, "ColorModel")) != NULL)
    {
     /*
      * ColorModel is taken from PPD as its default option.
      */

      if (!strcmp(choice->choice, "Gray") || !strcmp(choice->choice, "FastGray") || !strcmp(choice->choice, "DeviceGray"))
        keyword = "monochrome";
      else
        keyword = "color";
    }
    else
     /*
      * print-color-mode is a default option since 2.4.1, use it as a fallback if there is no
      * ColorModel in options or PPD...
      */
      keyword = cupsGetOption("print-color-mode", num_options, options);
  }
  else
  {
   /*
    * ColorModel found in options...
    */

    if (!strcmp(keyword, "Gray") || !strcmp(keyword, "FastGray") || !strcmp(keyword, "DeviceGray"))
      keyword = "monochrome";
    else
      keyword = "color";
  }

  if (keyword && !strcmp(keyword, "monochrome"))
  {
    if (ippContainsString(print_color_mode_sup, "auto-monochrome"))
      keyword = "auto-monochrome";
    else if (ippContainsString(print_color_mode_sup, "process-monochrome") && !ippContainsString(print_color_mode_sup, "monochrome"))
      keyword = "process-monochrome";
  }

  if (keyword)
    ippAddString(request, IPP_TAG_JOB, IPP_TAG_KEYWORD, color_attr_name, NULL, keyword);

  if ((keyword = cupsGetOption("print-quality", num_options, options)) != NULL)
    ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_ENUM, "print-quality", atoi(keyword));
  else if ((choice = ppdFindMarkedChoice(ppd, "cupsPrintQuality")) != NULL)
  {
    if (!_cups_strcasecmp(choice->choice, "draft"))
      ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_ENUM, "print-quality", IPP_QUALITY_DRAFT);
    else if (!_cups_strcasecmp(choice->choice, "normal"))
      ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_ENUM, "print-quality", IPP_QUALITY_NORMAL);
    else if (!_cups_strcasecmp(choice->choice, "high"))
      ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_ENUM, "print-quality", IPP_QUALITY_HIGH);
  }

  if ((keyword = cupsGetOption("sides", num_options, options)) != NULL)
    ippAddString(request, IPP_TAG_JOB, IPP_TAG_KEYWORD, "sides", NULL, keyword);
  else if (pc->sides_option && (choice = ppdFindMarkedChoice(ppd, pc->sides_option)) != NULL)
  {
    if (pc->sides_1sided && !_cups_strcasecmp(choice->choice, pc->sides_1sided))
      ippAddString(request, IPP_TAG_JOB, IPP_TAG_KEYWORD, "sides", NULL, "one-sided");
    else if (pc->sides_2sided_long && !_cups_strcasecmp(choice->choice, pc->sides_2sided_long))
      ippAddString(request, IPP_TAG_JOB, IPP_TAG_KEYWORD, "sides", NULL, "two-sided-long-edge");
    else if (pc->sides_2sided_short && !_cups_strcasecmp(choice->choice, pc->sides_2sided_short))
      ippAddString(request, IPP_TAG_JOB, IPP_TAG_KEYWORD, "sides", NULL, "two-sided-short-edge");
  }

 /*
  * Copies...
  */

  if ((keyword = cupsGetOption("multiple-document-handling", num_options, options)) != NULL)
  {
    if (strstr(keyword, "uncollated"))
      keyword = "false";
    else
      keyword = "true";
  }
  else if ((keyword = cupsGetOption("collate", num_options, options)) == NULL)
    keyword = "true";

  if (format)
  {
    if (!_cups_strcasecmp(format, "image/gif") ||
	!_cups_strcasecmp(format, "image/jp2") ||
	!_cups_strcasecmp(format, "image/jpeg") ||
	!_cups_strcasecmp(format, "image/png") ||
	!_cups_strcasecmp(format, "image/tiff") ||
	!_cups_strncasecmp(format, "image/x-", 8))
    {
     /*
      * Collation makes no sense for single page image formats...
      */

      keyword = "false";
    }
    else if (!_cups_strncasecmp(format, "image/", 6) ||
	     !_cups_strcasecmp(format, "application/vnd.cups-raster"))
    {
     /*
      * Multi-page image formats will have copies applied by the upstream
      * filters...
      */

      copies = 1;
    }
  }

  if (doc_handling_sup)
  {
    if (!_cups_strcasecmp(keyword, "true"))
      collate_str = "separate-documents-collated-copies";
    else
      collate_str = "separate-documents-uncollated-copies";

    for (i = 0; i < doc_handling_sup->num_values; i ++)
    {
      if (!strcmp(doc_handling_sup->values[i].string.text, collate_str))
      {
	ippAddString(request, IPP_TAG_JOB, IPP_TAG_KEYWORD, "multiple-document-handling", NULL, collate_str);
	break;
      }
    }

    if (i >= doc_handling_sup->num_values)
      copies = 1;
  }

 /*
  * Map finishing options...
  */

  if (copies != finishings_copies)
  {
    // Figure out the proper job-pages-per-set value...
    if ((value = cupsGetOption("job-pages", num_options, options)) == NULL)
      value = cupsGetOption("com.apple.print.PrintSettings.PMTotalBeginPages..n.", num_options, options);

    if (value)
    {
      if ((job_pages = atoi(value)) < 1)
        job_pages = 1;
    }

    // Adjust for number-up
    if ((value = cupsGetOption("number-up", num_options, options)) != NULL)
    {
      if ((number_up = atoi(value)) < 1)
        number_up = 1;
    }

    job_pages = (job_pages + number_up - 1) / number_up;

    // When duplex printing, raster data will include an extra (blank) page to
    // make the total number of pages even.  Make sure this is reflected in the
    // page count...
    if ((job_pages & 1) && (keyword = cupsGetOption("sides", num_options, options)) != NULL && strcmp(keyword, "one-sided"))
      job_pages ++;
  }

  if ((finishing_template = cupsGetOption("cupsFinishingTemplate", num_options, options)) == NULL)
    finishing_template = cupsGetOption("finishing-template", num_options, options);

  if (finishing_template && strcmp(finishing_template, "none"))
  {
    ipp_t *fin_col = ippNew();		/* finishings-col value */

    ippAddString(fin_col, IPP_TAG_JOB, IPP_TAG_KEYWORD, "finishing-template", NULL, finishing_template);
    ippAddCollection(request, IPP_TAG_JOB, "finishings-col", fin_col);
    ippDelete(fin_col);

    if (copies != finishings_copies && job_pages > 0)
    {
     /*
      * Send job-pages-per-set attribute to apply finishings correctly...
      */

      ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-pages-per-set", job_pages);
    }
  }
  else
  {
    num_finishings = _ppdCacheGetFinishingValues(ppd, pc, (int)(sizeof(finishings) / sizeof(finishings[0])), finishings);
    if (num_finishings > 0)
    {
      ippAddIntegers(request, IPP_TAG_JOB, IPP_TAG_ENUM, "finishings", num_finishings, finishings);

      if (copies != finishings_copies && job_pages > 0)
      {
       /*
	* Send job-pages-per-set attribute to apply finishings correctly...
	*/

	ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-pages-per-set", job_pages);
      }
    }
  }

  return (copies);
}


/*
 * '_ppdCacheCreateWithFile()' - Create PPD cache and mapping data from a
 *                               written file.
 *
 * Use the @link _ppdCacheWriteFile@ function to write PWG mapping data to a
 * file.
 */

_ppd_cache_t *				/* O  - PPD cache and mapping data */
_ppdCacheCreateWithFile(
    const char *filename,		/* I  - File to read */
    ipp_t      **attrs)			/* IO - IPP attributes, if any */
{
  cups_file_t	*fp;			/* File */
  _ppd_cache_t	*pc;			/* PWG mapping data */
  pwg_size_t	*size;			/* Current size */
  pwg_map_t	*map;			/* Current map */
  _pwg_finishings_t *finishings;	/* Current finishings option */
  int		linenum,		/* Current line number */
		num_bins,		/* Number of bins in file */
		num_sizes,		/* Number of sizes in file */
		num_sources,		/* Number of sources in file */
		num_types;		/* Number of types in file */
  char		line[2048],		/* Current line */
		*value,			/* Pointer to value in line */
		*valueptr,		/* Pointer into value */
		pwg_keyword[128],	/* PWG keyword */
		ppd_keyword[PPD_MAX_NAME];
					/* PPD keyword */
  _pwg_print_color_mode_t print_color_mode;
					/* Print color mode for preset */
  _pwg_print_quality_t print_quality;	/* Print quality for preset */


  DEBUG_printf(("_ppdCacheCreateWithFile(filename=\"%s\")", filename));

 /*
  * Range check input...
  */

  if (attrs)
    *attrs = NULL;

  if (!filename)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (NULL);
  }

 /*
  * Open the file...
  */

  if ((fp = cupsFileOpen(filename, "r")) == NULL)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    return (NULL);
  }

 /*
  * Read the first line and make sure it has "#CUPS-PPD-CACHE-version" in it...
  */

  if (!cupsFileGets(fp, line, sizeof(line)))
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    DEBUG_puts("_ppdCacheCreateWithFile: Unable to read first line.");
    cupsFileClose(fp);
    return (NULL);
  }

  if (strncmp(line, "#CUPS-PPD-CACHE-", 16))
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
    DEBUG_printf(("_ppdCacheCreateWithFile: Wrong first line \"%s\".", line));
    cupsFileClose(fp);
    return (NULL);
  }

  if (atoi(line + 16) != _PPD_CACHE_VERSION)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Out of date PPD cache file."), 1);
    DEBUG_printf(("_ppdCacheCreateWithFile: Cache file has version %s, "
                  "expected %d.", line + 16, _PPD_CACHE_VERSION));
    cupsFileClose(fp);
    return (NULL);
  }

 /*
  * Allocate the mapping data structure...
  */

  if ((pc = calloc(1, sizeof(_ppd_cache_t))) == NULL)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    DEBUG_puts("_ppdCacheCreateWithFile: Unable to allocate _ppd_cache_t.");
    goto create_error;
  }

  pc->max_copies = 9999;

 /*
  * Read the file...
  */

  linenum     = 0;
  num_bins    = 0;
  num_sizes   = 0;
  num_sources = 0;
  num_types   = 0;

  while (cupsFileGetConf(fp, line, sizeof(line), &value, &linenum))
  {
    DEBUG_printf(("_ppdCacheCreateWithFile: line=\"%s\", value=\"%s\", "
                  "linenum=%d", line, value, linenum));

    if (!value)
    {
      DEBUG_printf(("_ppdCacheCreateWithFile: Missing value on line %d.",
                    linenum));
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
      goto create_error;
    }
    else if (!_cups_strcasecmp(line, "Filter"))
    {
      if (!pc->filters)
        pc->filters = cupsArrayNew3(NULL, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free);

      cupsArrayAdd(pc->filters, value);
    }
    else if (!_cups_strcasecmp(line, "PreFilter"))
    {
      if (!pc->prefilters)
        pc->prefilters = cupsArrayNew3(NULL, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free);

      cupsArrayAdd(pc->prefilters, value);
    }
    else if (!_cups_strcasecmp(line, "Product"))
    {
      pc->product = strdup(value);
    }
    else if (!_cups_strcasecmp(line, "SingleFile"))
    {
      pc->single_file = !_cups_strcasecmp(value, "true");
    }
    else if (!_cups_strcasecmp(line, "IPP"))
    {
      off_t	pos = cupsFileTell(fp),	/* Position in file */
		length = strtol(value, NULL, 10);
					/* Length of IPP attributes */

      if (attrs && *attrs)
      {
        DEBUG_puts("_ppdCacheCreateWithFile: IPP listed multiple times.");
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	goto create_error;
      }
      else if (length <= 0)
      {
        DEBUG_puts("_ppdCacheCreateWithFile: Bad IPP length.");
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	goto create_error;
      }

      if (attrs)
      {
       /*
        * Read IPP attributes into the provided variable...
	*/

        *attrs = ippNew();

        if (ippReadIO(fp, (ipp_iocb_t)cupsFileRead, 1, NULL,
		      *attrs) != IPP_STATE_DATA)
	{
	  DEBUG_puts("_ppdCacheCreateWithFile: Bad IPP data.");
	  _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	  goto create_error;
	}
      }
      else
      {
       /*
        * Skip the IPP data entirely...
	*/

        cupsFileSeek(fp, pos + length);
      }

      if (cupsFileTell(fp) != (pos + length))
      {
        DEBUG_puts("_ppdCacheCreateWithFile: Bad IPP data.");
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	goto create_error;
      }
    }
    else if (!_cups_strcasecmp(line, "NumBins"))
    {
      if (num_bins > 0)
      {
        DEBUG_puts("_ppdCacheCreateWithFile: NumBins listed multiple times.");
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	goto create_error;
      }

      if ((num_bins = atoi(value)) <= 0 || num_bins > 65536)
      {
        DEBUG_printf(("_ppdCacheCreateWithFile: Bad NumBins value %d on line "
		      "%d.", num_sizes, linenum));
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	goto create_error;
      }

      if ((pc->bins = calloc((size_t)num_bins, sizeof(pwg_map_t))) == NULL)
      {
        DEBUG_printf(("_ppdCacheCreateWithFile: Unable to allocate %d bins.",
	              num_sizes));
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
	goto create_error;
      }
    }
    else if (!_cups_strcasecmp(line, "Bin"))
    {
      if (sscanf(value, "%127s%40s", pwg_keyword, ppd_keyword) != 2)
      {
        DEBUG_printf(("_ppdCacheCreateWithFile: Bad Bin on line %d.", linenum));
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	goto create_error;
      }

      if (pc->num_bins >= num_bins)
      {
        DEBUG_printf(("_ppdCacheCreateWithFile: Too many Bin's on line %d.",
	              linenum));
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	goto create_error;
      }

      map      = pc->bins + pc->num_bins;
      map->pwg = strdup(pwg_keyword);
      map->ppd = strdup(ppd_keyword);

      pc->num_bins ++;
    }
    else if (!_cups_strcasecmp(line, "NumSizes"))
    {
      if (num_sizes > 0)
      {
        DEBUG_puts("_ppdCacheCreateWithFile: NumSizes listed multiple times.");
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	goto create_error;
      }

      if ((num_sizes = atoi(value)) < 0 || num_sizes > 65536)
      {
        DEBUG_printf(("_ppdCacheCreateWithFile: Bad NumSizes value %d on line "
	              "%d.", num_sizes, linenum));
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	goto create_error;
      }

      if (num_sizes > 0)
      {
	if ((pc->sizes = calloc((size_t)num_sizes, sizeof(pwg_size_t))) == NULL)
	{
	  DEBUG_printf(("_ppdCacheCreateWithFile: Unable to allocate %d sizes.",
			num_sizes));
	  _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
	  goto create_error;
	}
      }
    }
    else if (!_cups_strcasecmp(line, "Size"))
    {
      if (pc->num_sizes >= num_sizes)
      {
        DEBUG_printf(("_ppdCacheCreateWithFile: Too many Size's on line %d.",
	              linenum));
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	goto create_error;
      }

      size = pc->sizes + pc->num_sizes;

      if (sscanf(value, "%127s%40s%d%d%d%d%d%d", pwg_keyword, ppd_keyword,
		 &(size->width), &(size->length), &(size->left),
		 &(size->bottom), &(size->right), &(size->top)) != 8)
      {
        DEBUG_printf(("_ppdCacheCreateWithFile: Bad Size on line %d.",
	              linenum));
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	goto create_error;
      }

      size->map.pwg = strdup(pwg_keyword);
      size->map.ppd = strdup(ppd_keyword);

      pc->num_sizes ++;
    }
    else if (!_cups_strcasecmp(line, "CustomSize"))
    {
      if (pc->custom_max_width > 0)
      {
        DEBUG_printf(("_ppdCacheCreateWithFile: Too many CustomSize's on line "
	              "%d.", linenum));
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	goto create_error;
      }

      if (sscanf(value, "%d%d%d%d%d%d%d%d", &(pc->custom_max_width),
                 &(pc->custom_max_length), &(pc->custom_min_width),
		 &(pc->custom_min_length), &(pc->custom_size.left),
		 &(pc->custom_size.bottom), &(pc->custom_size.right),
		 &(pc->custom_size.top)) != 8)
      {
        DEBUG_printf(("_ppdCacheCreateWithFile: Bad CustomSize on line %d.",
	              linenum));
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	goto create_error;
      }

      pwgFormatSizeName(pwg_keyword, sizeof(pwg_keyword), "custom", "max",
		        pc->custom_max_width, pc->custom_max_length, NULL);
      pc->custom_max_keyword = strdup(pwg_keyword);

      pwgFormatSizeName(pwg_keyword, sizeof(pwg_keyword), "custom", "min",
		        pc->custom_min_width, pc->custom_min_length, NULL);
      pc->custom_min_keyword = strdup(pwg_keyword);
    }
    else if (!_cups_strcasecmp(line, "SourceOption"))
    {
      pc->source_option = strdup(value);
    }
    else if (!_cups_strcasecmp(line, "NumSources"))
    {
      if (num_sources > 0)
      {
        DEBUG_puts("_ppdCacheCreateWithFile: NumSources listed multiple "
	           "times.");
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	goto create_error;
      }

      if ((num_sources = atoi(value)) <= 0 || num_sources > 65536)
      {
        DEBUG_printf(("_ppdCacheCreateWithFile: Bad NumSources value %d on "
	              "line %d.", num_sources, linenum));
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	goto create_error;
      }

      if ((pc->sources = calloc((size_t)num_sources, sizeof(pwg_map_t))) == NULL)
      {
        DEBUG_printf(("_ppdCacheCreateWithFile: Unable to allocate %d sources.",
	              num_sources));
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
	goto create_error;
      }
    }
    else if (!_cups_strcasecmp(line, "Source"))
    {
      if (sscanf(value, "%127s%40s", pwg_keyword, ppd_keyword) != 2)
      {
        DEBUG_printf(("_ppdCacheCreateWithFile: Bad Source on line %d.",
	              linenum));
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	goto create_error;
      }

      if (pc->num_sources >= num_sources)
      {
        DEBUG_printf(("_ppdCacheCreateWithFile: Too many Source's on line %d.",
	              linenum));
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	goto create_error;
      }

      map      = pc->sources + pc->num_sources;
      map->pwg = strdup(pwg_keyword);
      map->ppd = strdup(ppd_keyword);

      pc->num_sources ++;
    }
    else if (!_cups_strcasecmp(line, "NumTypes"))
    {
      if (num_types > 0)
      {
        DEBUG_puts("_ppdCacheCreateWithFile: NumTypes listed multiple times.");
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	goto create_error;
      }

      if ((num_types = atoi(value)) <= 0 || num_types > 65536)
      {
        DEBUG_printf(("_ppdCacheCreateWithFile: Bad NumTypes value %d on "
	              "line %d.", num_types, linenum));
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	goto create_error;
      }

      if ((pc->types = calloc((size_t)num_types, sizeof(pwg_map_t))) == NULL)
      {
        DEBUG_printf(("_ppdCacheCreateWithFile: Unable to allocate %d types.",
	              num_types));
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
	goto create_error;
      }
    }
    else if (!_cups_strcasecmp(line, "Type"))
    {
      if (sscanf(value, "%127s%40s", pwg_keyword, ppd_keyword) != 2)
      {
        DEBUG_printf(("_ppdCacheCreateWithFile: Bad Type on line %d.",
	              linenum));
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	goto create_error;
      }

      if (pc->num_types >= num_types)
      {
        DEBUG_printf(("_ppdCacheCreateWithFile: Too many Type's on line %d.",
	              linenum));
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	goto create_error;
      }

      map      = pc->types + pc->num_types;
      map->pwg = strdup(pwg_keyword);
      map->ppd = strdup(ppd_keyword);

      pc->num_types ++;
    }
    else if (!_cups_strcasecmp(line, "Preset"))
    {
     /*
      * Preset output-mode print-quality name=value ...
      */

      print_color_mode = (_pwg_print_color_mode_t)strtol(value, &valueptr, 10);
      print_quality    = (_pwg_print_quality_t)strtol(valueptr, &valueptr, 10);

      if (print_color_mode < _PWG_PRINT_COLOR_MODE_MONOCHROME ||
          print_color_mode >= _PWG_PRINT_COLOR_MODE_MAX ||
	  print_quality < _PWG_PRINT_QUALITY_DRAFT ||
	  print_quality >= _PWG_PRINT_QUALITY_MAX ||
	  valueptr == value || !*valueptr)
      {
        DEBUG_printf(("_ppdCacheCreateWithFile: Bad Preset on line %d.",
	              linenum));
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
	goto create_error;
      }

      pc->num_presets[print_color_mode][print_quality] =
          cupsParseOptions(valueptr, 0,
	                   pc->presets[print_color_mode] + print_quality);
    }
    else if (!_cups_strcasecmp(line, "SidesOption"))
      pc->sides_option = strdup(value);
    else if (!_cups_strcasecmp(line, "Sides1Sided"))
      pc->sides_1sided = strdup(value);
    else if (!_cups_strcasecmp(line, "Sides2SidedLong"))
      pc->sides_2sided_long = strdup(value);
    else if (!_cups_strcasecmp(line, "Sides2SidedShort"))
      pc->sides_2sided_short = strdup(value);
    else if (!_cups_strcasecmp(line, "Finishings"))
    {
      if (!pc->finishings)
	pc->finishings =
	    cupsArrayNew3((cups_array_func_t)pwg_compare_finishings,
			  NULL, NULL, 0, NULL,
			  (cups_afree_func_t)pwg_free_finishings);

      if ((finishings = calloc(1, sizeof(_pwg_finishings_t))) == NULL)
        goto create_error;

      finishings->value       = (ipp_finishings_t)strtol(value, &valueptr, 10);
      finishings->num_options = cupsParseOptions(valueptr, 0,
                                                 &(finishings->options));

      cupsArrayAdd(pc->finishings, finishings);
    }
    else if (!_cups_strcasecmp(line, "FinishingTemplate"))
    {
      if (!pc->templates)
        pc->templates = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free);

      cupsArrayAdd(pc->templates, value);
    }
    else if (!_cups_strcasecmp(line, "MaxCopies"))
      pc->max_copies = atoi(value);
    else if (!_cups_strcasecmp(line, "ChargeInfoURI"))
      pc->charge_info_uri = strdup(value);
    else if (!_cups_strcasecmp(line, "JobAccountId"))
      pc->account_id = !_cups_strcasecmp(value, "true");
    else if (!_cups_strcasecmp(line, "JobAccountingUserId"))
      pc->accounting_user_id = !_cups_strcasecmp(value, "true");
    else if (!_cups_strcasecmp(line, "JobPassword"))
      pc->password = strdup(value);
    else if (!_cups_strcasecmp(line, "Mandatory"))
    {
      if (pc->mandatory)
        _cupsArrayAddStrings(pc->mandatory, value, ' ');
      else
        pc->mandatory = _cupsArrayNewStrings(value, ' ');
    }
    else if (!_cups_strcasecmp(line, "SupportFile"))
    {
      if (!pc->support_files)
        pc->support_files = cupsArrayNew3(NULL, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free);

      cupsArrayAdd(pc->support_files, value);
    }
    else
    {
      DEBUG_printf(("_ppdCacheCreateWithFile: Unknown %s on line %d.", line,
		    linenum));
    }
  }

  if (pc->num_sizes < num_sizes)
  {
    DEBUG_printf(("_ppdCacheCreateWithFile: Not enough sizes (%d < %d).",
                  pc->num_sizes, num_sizes));
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
    goto create_error;
  }

  if (pc->num_sources < num_sources)
  {
    DEBUG_printf(("_ppdCacheCreateWithFile: Not enough sources (%d < %d).",
                  pc->num_sources, num_sources));
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
    goto create_error;
  }

  if (pc->num_types < num_types)
  {
    DEBUG_printf(("_ppdCacheCreateWithFile: Not enough types (%d < %d).",
                  pc->num_types, num_types));
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad PPD cache file."), 1);
    goto create_error;
  }

  cupsFileClose(fp);

  return (pc);

 /*
  * If we get here the file was bad - free any data and return...
  */

  create_error:

  cupsFileClose(fp);
  _ppdCacheDestroy(pc);

  if (attrs)
  {
    ippDelete(*attrs);
    *attrs = NULL;
  }

  return (NULL);
}


/*
 * '_ppdCacheCreateWithPPD()' - Create PWG mapping data from a PPD file.
 */

_ppd_cache_t *				/* O - PPD cache and mapping data */
_ppdCacheCreateWithPPD(ppd_file_t *ppd)	/* I - PPD file */
{
  int			i, j, k;	/* Looping vars */
  _ppd_cache_t		*pc;		/* PWG mapping data */
  ppd_option_t		*input_slot,	/* InputSlot option */
			*media_type,	/* MediaType option */
			*output_bin,	/* OutputBin option */
			*color_model,	/* ColorModel option */
			*duplex,	/* Duplex option */
			*ppd_option;	/* Other PPD option */
  ppd_choice_t		*choice;	/* Current InputSlot/MediaType */
  pwg_map_t		*map;		/* Current source/type map */
  ppd_attr_t		*ppd_attr;	/* Current PPD preset attribute */
  int			num_options;	/* Number of preset options and props */
  cups_option_t		*options;	/* Preset options and properties */
  ppd_size_t		*ppd_size;	/* Current PPD size */
  pwg_size_t		*pwg_size;	/* Current PWG size */
  char			pwg_keyword[3 + PPD_MAX_NAME + 1 + 12 + 1 + 12 + 3],
					/* PWG keyword string */
			ppd_name[PPD_MAX_NAME];
					/* Normalized PPD name */
  const char		*pwg_name;	/* Standard PWG media name */
  pwg_media_t		*pwg_media,	/* PWG media data */
			pwg_mediatemp;	/* Temporary PWG media data */
  _pwg_print_color_mode_t pwg_print_color_mode;
					/* print-color-mode index */
  _pwg_print_quality_t	pwg_print_quality;
					/* print-quality index */
  int			similar;	/* Are the old and new size similar? */
  pwg_size_t		*old_size;	/* Current old size */
  int			old_imageable,	/* Old imageable length in 2540ths */
			old_borderless,	/* Old borderless state */
			old_known_pwg;	/* Old PWG name is well-known */
  int			new_width,	/* New width in 2540ths */
			new_length,	/* New length in 2540ths */
			new_left,	/* New left margin in 2540ths */
			new_bottom,	/* New bottom margin in 2540ths */
			new_right,	/* New right margin in 2540ths */
			new_top,	/* New top margin in 2540ths */
			new_imageable,	/* New imageable length in 2540ths */
			new_borderless,	/* New borderless state */
			new_known_pwg;	/* New PWG name is well-known */
  pwg_size_t		*new_size;	/* New size to add, if any */
  const char		*filter;	/* Current filter */
  _pwg_finishings_t	*finishings;	/* Current finishings value */
  char			msg_id[256];	/* Message identifier */


  DEBUG_printf(("_ppdCacheCreateWithPPD(ppd=%p)", ppd));

 /*
  * Range check input...
  */

  if (!ppd)
    return (NULL);

 /*
  * Allocate memory...
  */

  if ((pc = calloc(1, sizeof(_ppd_cache_t))) == NULL)
  {
    DEBUG_puts("_ppdCacheCreateWithPPD: Unable to allocate _ppd_cache_t.");
    goto create_error;
  }

  pc->strings = _cupsMessageNew(NULL);

 /*
  * Copy and convert size data...
  */

  if (ppd->num_sizes > 0)
  {
    if ((pc->sizes = calloc((size_t)ppd->num_sizes, sizeof(pwg_size_t))) == NULL)
    {
      DEBUG_printf(("_ppdCacheCreateWithPPD: Unable to allocate %d "
		    "pwg_size_t's.", ppd->num_sizes));
      goto create_error;
    }

    for (i = ppd->num_sizes, pwg_size = pc->sizes, ppd_size = ppd->sizes;
	 i > 0;
	 i --, ppd_size ++)
    {
     /*
      * Don't copy over custom size...
      */

      if (!_cups_strcasecmp(ppd_size->name, "Custom"))
	continue;

     /*
      * Convert the PPD size name to the corresponding PWG keyword name.
      */

      if ((pwg_media = pwgMediaForSize(PWG_FROM_POINTS(ppd_size->width), PWG_FROM_POINTS(ppd_size->length))) != NULL)
      {
       /*
	* Standard name, do we have conflicts?
	*/

	for (j = 0; j < pc->num_sizes; j ++)
	  if (!strcmp(pc->sizes[j].map.pwg, pwg_media->pwg))
	  {
	    pwg_media = NULL;
	    break;
	  }
      }

      if (pwg_media)
      {
       /*
	* Standard name and no conflicts, use it!
	*/

	pwg_name      = pwg_media->pwg;
	new_known_pwg = 1;
      }
      else
      {
       /*
	* Not a standard name; convert it to a PWG vendor name of the form:
	*
	*     pp_lowerppd_WIDTHxHEIGHTuu
	*/

	pwg_name      = pwg_keyword;
	new_known_pwg = 0;

	pwg_unppdize_name(ppd_size->name, ppd_name, sizeof(ppd_name), "_.");
	pwgFormatSizeName(pwg_keyword, sizeof(pwg_keyword), NULL, ppd_name,
			  PWG_FROM_POINTS(ppd_size->width),
			  PWG_FROM_POINTS(ppd_size->length), NULL);
      }

     /*
      * If we have a similar paper with non-zero margins then we only want to
      * keep it if it has a larger imageable area length.  The NULL check is for
      * dimensions that are <= 0...
      */

      if ((pwg_media = _pwgMediaNearSize(&pwg_mediatemp, /*keyword*/NULL, /*keysize*/0, /*ppdname*/NULL, /*ppdsize*/0, PWG_FROM_POINTS(ppd_size->width), PWG_FROM_POINTS(ppd_size->length), /*epsilon*/0)) == NULL)
	continue;

      new_width      = pwg_media->width;
      new_length     = pwg_media->length;
      new_left       = PWG_FROM_POINTS(ppd_size->left);
      new_bottom     = PWG_FROM_POINTS(ppd_size->bottom);
      new_right      = PWG_FROM_POINTS(ppd_size->width - ppd_size->right);
      new_top        = PWG_FROM_POINTS(ppd_size->length - ppd_size->top);
      new_imageable  = new_length - new_top - new_bottom;
      new_borderless = new_bottom == 0 && new_top == 0 &&
		       new_left == 0 && new_right == 0;

      for (k = pc->num_sizes, similar = 0, old_size = pc->sizes, new_size = NULL;
	   k > 0 && !similar;
	   k --, old_size ++)
      {
	old_imageable  = old_size->length - old_size->top - old_size->bottom;
	old_borderless = old_size->left == 0 && old_size->bottom == 0 &&
			 old_size->right == 0 && old_size->top == 0;
	old_known_pwg  = strncmp(old_size->map.pwg, "oe_", 3) &&
			 strncmp(old_size->map.pwg, "om_", 3);

	similar = old_borderless == new_borderless &&
		  _PWG_EQUIVALENT(old_size->width, new_width) &&
		  _PWG_EQUIVALENT(old_size->length, new_length);

	if (similar &&
	    (new_known_pwg || (!old_known_pwg && new_imageable > old_imageable)))
	{
	 /*
	  * The new paper has a larger imageable area so it could replace
	  * the older paper.  Regardless of the imageable area, we always
	  * prefer the size with a well-known PWG name.
	  */

	  new_size = old_size;
	  free(old_size->map.ppd);
	  free(old_size->map.pwg);
	}
      }

      if (!similar)
      {
       /*
	* The paper was unique enough to deserve its own entry so add it to the
	* end.
	*/

	new_size = pwg_size ++;
	pc->num_sizes ++;
      }

      if (new_size)
      {
       /*
	* Save this size...
	*/

	new_size->map.ppd = strdup(ppd_size->name);
	new_size->map.pwg = strdup(pwg_name);
	new_size->width   = new_width;
	new_size->length  = new_length;
	new_size->left    = new_left;
	new_size->bottom  = new_bottom;
	new_size->right   = new_right;
	new_size->top     = new_top;
      }
    }
  }

  if (ppd->variable_sizes)
  {
   /*
    * Generate custom size data...
    */

    pwgFormatSizeName(pwg_keyword, sizeof(pwg_keyword), "custom", "max",
		      PWG_FROM_POINTS(ppd->custom_max[0]),
		      PWG_FROM_POINTS(ppd->custom_max[1]), NULL);
    pc->custom_max_keyword = strdup(pwg_keyword);
    pc->custom_max_width   = PWG_FROM_POINTS(ppd->custom_max[0]);
    pc->custom_max_length  = PWG_FROM_POINTS(ppd->custom_max[1]);

    pwgFormatSizeName(pwg_keyword, sizeof(pwg_keyword), "custom", "min",
		      PWG_FROM_POINTS(ppd->custom_min[0]),
		      PWG_FROM_POINTS(ppd->custom_min[1]), NULL);
    pc->custom_min_keyword = strdup(pwg_keyword);
    pc->custom_min_width   = PWG_FROM_POINTS(ppd->custom_min[0]);
    pc->custom_min_length  = PWG_FROM_POINTS(ppd->custom_min[1]);

    pc->custom_size.left   = PWG_FROM_POINTS(ppd->custom_margins[0]);
    pc->custom_size.bottom = PWG_FROM_POINTS(ppd->custom_margins[1]);
    pc->custom_size.right  = PWG_FROM_POINTS(ppd->custom_margins[2]);
    pc->custom_size.top    = PWG_FROM_POINTS(ppd->custom_margins[3]);
  }

 /*
  * Copy and convert InputSlot data...
  */

  if ((input_slot = ppdFindOption(ppd, "InputSlot")) == NULL)
    input_slot = ppdFindOption(ppd, "HPPaperSource");

  if (input_slot)
  {
    pc->source_option = strdup(input_slot->keyword);

    if ((pc->sources = calloc((size_t)input_slot->num_choices, sizeof(pwg_map_t))) == NULL)
    {
      DEBUG_printf(("_ppdCacheCreateWithPPD: Unable to allocate %d "
                    "pwg_map_t's for InputSlot.", input_slot->num_choices));
      goto create_error;
    }

    pc->num_sources = input_slot->num_choices;

    for (i = input_slot->num_choices, choice = input_slot->choices,
             map = pc->sources;
	 i > 0;
	 i --, choice ++, map ++)
    {
      if (!_cups_strncasecmp(choice->choice, "Auto", 4) ||
          !_cups_strncasecmp(choice->text, "Auto", 4) ||
          !_cups_strcasecmp(choice->choice, "Default") ||
          !_cups_strcasecmp(choice->text, "Default"))
        pwg_name = "auto";
      else if (!_cups_strcasecmp(choice->choice, "Cassette"))
        pwg_name = "main";
      else if (!_cups_strcasecmp(choice->choice, "PhotoTray"))
        pwg_name = "photo";
      else if (!_cups_strcasecmp(choice->choice, "CDTray"))
        pwg_name = "disc";
      else if (!_cups_strncasecmp(choice->choice, "Multipurpose", 12) ||
               !_cups_strcasecmp(choice->choice, "MP") ||
               !_cups_strcasecmp(choice->choice, "MPTray"))
        pwg_name = "by-pass-tray";
      else if (!_cups_strcasecmp(choice->choice, "LargeCapacity"))
        pwg_name = "large-capacity";
      else if (!_cups_strncasecmp(choice->choice, "Lower", 5))
        pwg_name = "bottom";
      else if (!_cups_strncasecmp(choice->choice, "Middle", 6))
        pwg_name = "middle";
      else if (!_cups_strncasecmp(choice->choice, "Upper", 5))
        pwg_name = "top";
      else if (!_cups_strncasecmp(choice->choice, "Side", 4))
        pwg_name = "side";
      else if (!_cups_strcasecmp(choice->choice, "Roll"))
        pwg_name = "main-roll";
      else if (!_cups_strcasecmp(choice->choice, "0"))
        pwg_name = "tray-1";
      else if (!_cups_strcasecmp(choice->choice, "1"))
        pwg_name = "tray-2";
      else if (!_cups_strcasecmp(choice->choice, "2"))
        pwg_name = "tray-3";
      else if (!_cups_strcasecmp(choice->choice, "3"))
        pwg_name = "tray-4";
      else if (!_cups_strcasecmp(choice->choice, "4"))
        pwg_name = "tray-5";
      else if (!_cups_strcasecmp(choice->choice, "5"))
        pwg_name = "tray-6";
      else if (!_cups_strcasecmp(choice->choice, "6"))
        pwg_name = "tray-7";
      else if (!_cups_strcasecmp(choice->choice, "7"))
        pwg_name = "tray-8";
      else if (!_cups_strcasecmp(choice->choice, "8"))
        pwg_name = "tray-9";
      else if (!_cups_strcasecmp(choice->choice, "9"))
        pwg_name = "tray-10";
      else
      {
       /*
        * Convert PPD name to lowercase...
	*/

        pwg_name = pwg_keyword;
	pwg_unppdize_name(choice->choice, pwg_keyword, sizeof(pwg_keyword),
	                  "_");
      }

      map->pwg = strdup(pwg_name);
      map->ppd = strdup(choice->choice);

     /*
      * Add localized text for PWG keyword to message catalog...
      */

      snprintf(msg_id, sizeof(msg_id), "media-source.%s", pwg_name);
      pwg_add_message(pc->strings, msg_id, choice->text);
    }
  }

 /*
  * Copy and convert MediaType data...
  */

  if ((media_type = ppdFindOption(ppd, "MediaType")) != NULL)
  {
    static const struct
    {
      const char *ppd_name;		/* PPD MediaType name or prefix to match */
      int        match_length;		/* Length of prefix, or -1 to match entire string */
      const char *pwg_name;		/* Registered PWG media-type name to use */
    } standard_types[] = {
      {"Auto", 4, "auto"},
      {"Any", -1, "auto"},
      {"Default", -1, "auto"},
      {"Card", 4, "cardstock"},
      {"Env", 3, "envelope"},
      {"Gloss", 5, "photographic-glossy"},
      {"HighGloss", -1, "photographic-high-gloss"},
      {"Matte", -1, "photographic-matte"},
      {"Plain", 5, "stationery"},
      {"Coated", 6, "stationery-coated"},
      {"Inkjet", -1, "stationery-inkjet"},
      {"Letterhead", -1, "stationery-letterhead"},
      {"Preprint", 8, "stationery-preprinted"},
      {"Recycled", -1, "stationery-recycled"},
      {"Transparen", 10, "transparency"},
    };
    const size_t num_standard_types = sizeof(standard_types) / sizeof(standard_types[0]);
					/* Length of the standard_types array */
    int match_counts[sizeof(standard_types) / sizeof(standard_types[0])] = {0};
					/* Number of matches for each standard type */

    if ((pc->types = calloc((size_t)media_type->num_choices, sizeof(pwg_map_t))) == NULL)
    {
      DEBUG_printf(("_ppdCacheCreateWithPPD: Unable to allocate %d "
                    "pwg_map_t's for MediaType.", media_type->num_choices));
      goto create_error;
    }

    pc->num_types = media_type->num_choices;

    for (i = media_type->num_choices, choice = media_type->choices,
             map = pc->types;
	 i > 0;
	 i --, choice ++, map ++)
    {
      pwg_name = NULL;

      for (j = 0; j < num_standard_types; j ++)
      {
        if (standard_types[j].match_length <= 0)
        {
          if (!_cups_strcasecmp(choice->choice, standard_types[j].ppd_name))
          {
            pwg_name = standard_types[j].pwg_name;
            match_counts[j] ++;
          }
        }
        else if (!_cups_strncasecmp(choice->choice, standard_types[j].ppd_name, standard_types[j].match_length))
        {
          pwg_name = standard_types[j].pwg_name;
          match_counts[j] ++;
        }
      }

      if (!pwg_name)
      {
       /*
        * Convert PPD name to lowercase...
	*/

        pwg_name = pwg_keyword;
	pwg_unppdize_name(choice->choice, pwg_keyword, sizeof(pwg_keyword),
	                  "_");
      }

      map->pwg = strdup(pwg_name);
      map->ppd = strdup(choice->choice);
    }

   /*
    * Since three PPD name patterns can map to "auto", their match counts
    * should each be the count of all three combined.
    */

    i = match_counts[0] + match_counts[1] + match_counts[2];
    match_counts[0] = match_counts[1] = match_counts[2] = i;

    for (i = 0, choice = media_type->choices, map = pc->types;
      i < media_type->num_choices;
      i ++, choice ++, map ++)
    {
     /*
      * If there are two matches for any standard PWG media type, don't give
      * the PWG name to either one.
      */

      for (j = 0; j < num_standard_types; j ++)
      {
        if (match_counts[j] > 1 && !strcmp(map->pwg, standard_types[j].pwg_name))
        {
          free(map->pwg);
          pwg_unppdize_name(choice->choice, pwg_keyword, sizeof(pwg_keyword), "_");
          map->pwg = strdup(pwg_keyword);
        }
      }

     /*
      * Add localized text for PWG keyword to message catalog...
      */

      snprintf(msg_id, sizeof(msg_id), "media-type.%s", map->pwg);
      pwg_add_message(pc->strings, msg_id, choice->text);
    }
  }

 /*
  * Copy and convert OutputBin data...
  */

  if ((output_bin = ppdFindOption(ppd, "OutputBin")) != NULL)
  {
    if ((pc->bins = calloc((size_t)output_bin->num_choices, sizeof(pwg_map_t))) == NULL)
    {
      DEBUG_printf(("_ppdCacheCreateWithPPD: Unable to allocate %d "
                    "pwg_map_t's for OutputBin.", output_bin->num_choices));
      goto create_error;
    }

    pc->num_bins = output_bin->num_choices;

    for (i = output_bin->num_choices, choice = output_bin->choices,
             map = pc->bins;
	 i > 0;
	 i --, choice ++, map ++)
    {
      pwg_unppdize_name(choice->choice, pwg_keyword, sizeof(pwg_keyword), "_");

      map->pwg = strdup(pwg_keyword);
      map->ppd = strdup(choice->choice);

     /*
      * Add localized text for PWG keyword to message catalog...
      */

      snprintf(msg_id, sizeof(msg_id), "output-bin.%s", pwg_keyword);
      pwg_add_message(pc->strings, msg_id, choice->text);
    }
  }

  if ((ppd_attr = ppdFindAttr(ppd, "APPrinterPreset", NULL)) != NULL)
  {
   /*
    * Copy and convert APPrinterPreset (output-mode + print-quality) data...
    */

    const char	*quality,		/* com.apple.print.preset.quality value */
		*output_mode,		/* com.apple.print.preset.output-mode value */
		*color_model_val,	/* ColorModel choice */
		*graphicsType,		/* com.apple.print.preset.graphicsType value */
		*media_front_coating;	/* com.apple.print.preset.media-front-coating value */

    do
    {
     /*
      * Add localized text for PWG keyword to message catalog...
      */

      snprintf(msg_id, sizeof(msg_id), "preset-name.%s", ppd_attr->spec);
      pwg_add_message(pc->strings, msg_id, ppd_attr->text);

     /*
      * Get the options for this preset...
      */

      num_options = _ppdParseOptions(ppd_attr->value, 0, &options,
                                     _PPD_PARSE_ALL);

      if ((quality = cupsGetOption("com.apple.print.preset.quality",
                                   num_options, options)) != NULL)
      {
       /*
        * Get the print-quality for this preset...
	*/

	if (!strcmp(quality, "low"))
	  pwg_print_quality = _PWG_PRINT_QUALITY_DRAFT;
	else if (!strcmp(quality, "high"))
	  pwg_print_quality = _PWG_PRINT_QUALITY_HIGH;
	else
	  pwg_print_quality = _PWG_PRINT_QUALITY_NORMAL;

       /*
	* Ignore graphicsType "Photo" presets that are not high quality.
	*/

	graphicsType = cupsGetOption("com.apple.print.preset.graphicsType",
				      num_options, options);

	if (pwg_print_quality != _PWG_PRINT_QUALITY_HIGH && graphicsType &&
	    !strcmp(graphicsType, "Photo"))
	  continue;

       /*
	* Ignore presets for normal and draft quality where the coating
	* isn't "none" or "autodetect".
	*/

	media_front_coating = cupsGetOption(
	                          "com.apple.print.preset.media-front-coating",
			          num_options, options);

        if (pwg_print_quality != _PWG_PRINT_QUALITY_HIGH &&
	    media_front_coating &&
	    strcmp(media_front_coating, "none") &&
	    strcmp(media_front_coating, "autodetect"))
	  continue;

       /*
        * Get the output mode for this preset...
	*/

        output_mode     = cupsGetOption("com.apple.print.preset.output-mode",
	                                num_options, options);
        color_model_val = cupsGetOption("ColorModel", num_options, options);

        if (output_mode)
	{
	  if (!strcmp(output_mode, "monochrome"))
	    pwg_print_color_mode = _PWG_PRINT_COLOR_MODE_MONOCHROME;
	  else
	    pwg_print_color_mode = _PWG_PRINT_COLOR_MODE_COLOR;
	}
	else if (color_model_val)
	{
	  if (!_cups_strcasecmp(color_model_val, "Gray"))
	    pwg_print_color_mode = _PWG_PRINT_COLOR_MODE_MONOCHROME;
	  else
	    pwg_print_color_mode = _PWG_PRINT_COLOR_MODE_COLOR;
	}
	else
	  pwg_print_color_mode = _PWG_PRINT_COLOR_MODE_COLOR;

       /*
        * Save the options for this combination as needed...
	*/

        if (!pc->num_presets[pwg_print_color_mode][pwg_print_quality])
	  pc->num_presets[pwg_print_color_mode][pwg_print_quality] =
	      _ppdParseOptions(ppd_attr->value, 0,
	                       pc->presets[pwg_print_color_mode] +
			           pwg_print_quality, _PPD_PARSE_OPTIONS);
      }

      cupsFreeOptions(num_options, options);
    }
    while ((ppd_attr = ppdFindNextAttr(ppd, "APPrinterPreset", NULL)) != NULL);
  }

  if (!pc->num_presets[_PWG_PRINT_COLOR_MODE_MONOCHROME][_PWG_PRINT_QUALITY_DRAFT] &&
      !pc->num_presets[_PWG_PRINT_COLOR_MODE_MONOCHROME][_PWG_PRINT_QUALITY_NORMAL] &&
      !pc->num_presets[_PWG_PRINT_COLOR_MODE_MONOCHROME][_PWG_PRINT_QUALITY_HIGH])
  {
   /*
    * Try adding some common color options to create grayscale presets.  These
    * are listed in order of popularity...
    */

    const char	*color_option = NULL,	/* Color control option */
		*gray_choice = NULL;	/* Choice to select grayscale */

    if ((color_model = ppdFindOption(ppd, "ColorModel")) != NULL &&
        ppdFindChoice(color_model, "Gray"))
    {
      color_option = "ColorModel";
      gray_choice  = "Gray";
    }
    else if ((color_model = ppdFindOption(ppd, "HPColorMode")) != NULL &&
             ppdFindChoice(color_model, "grayscale"))
    {
      color_option = "HPColorMode";
      gray_choice  = "grayscale";
    }
    else if ((color_model = ppdFindOption(ppd, "BRMonoColor")) != NULL &&
             ppdFindChoice(color_model, "Mono"))
    {
      color_option = "BRMonoColor";
      gray_choice  = "Mono";
    }
    else if ((color_model = ppdFindOption(ppd, "CNIJSGrayScale")) != NULL &&
             ppdFindChoice(color_model, "1"))
    {
      color_option = "CNIJSGrayScale";
      gray_choice  = "1";
    }
    else if ((color_model = ppdFindOption(ppd, "HPColorAsGray")) != NULL &&
             ppdFindChoice(color_model, "True"))
    {
      color_option = "HPColorAsGray";
      gray_choice  = "True";
    }

    if (color_option && gray_choice)
    {
     /*
      * Copy and convert ColorModel (output-mode) data...
      */

      cups_option_t	*coption,	/* Color option */
			  *moption;	/* Monochrome option */

      for (pwg_print_quality = _PWG_PRINT_QUALITY_DRAFT;
	   pwg_print_quality < _PWG_PRINT_QUALITY_MAX;
	   pwg_print_quality ++)
      {
	if (pc->num_presets[_PWG_PRINT_COLOR_MODE_COLOR][pwg_print_quality])
	{
	 /*
	  * Copy the color options...
	  */

	  num_options = pc->num_presets[_PWG_PRINT_COLOR_MODE_COLOR]
					[pwg_print_quality];
	  options     = calloc((size_t)num_options, sizeof(cups_option_t));

	  if (options)
	  {
	    for (i = num_options, moption = options,
		     coption = pc->presets[_PWG_PRINT_COLOR_MODE_COLOR]
					   [pwg_print_quality];
		 i > 0;
		 i --, moption ++, coption ++)
	    {
	      moption->name  = _cupsStrRetain(coption->name);
	      moption->value = _cupsStrRetain(coption->value);
	    }

	    pc->num_presets[_PWG_PRINT_COLOR_MODE_MONOCHROME][pwg_print_quality] =
		num_options;
	    pc->presets[_PWG_PRINT_COLOR_MODE_MONOCHROME][pwg_print_quality] =
		options;
	  }
	}
	else if (pwg_print_quality != _PWG_PRINT_QUALITY_NORMAL)
	  continue;

       /*
	* Add the grayscale option to the preset...
	*/

	pc->num_presets[_PWG_PRINT_COLOR_MODE_MONOCHROME][pwg_print_quality] =
	    cupsAddOption(color_option, gray_choice,
			  pc->num_presets[_PWG_PRINT_COLOR_MODE_MONOCHROME]
					  [pwg_print_quality],
			  pc->presets[_PWG_PRINT_COLOR_MODE_MONOCHROME] +
			      pwg_print_quality);
      }
    }
  }

 /*
  * Copy and convert Duplex (sides) data...
  */

  if ((duplex = ppdFindOption(ppd, "Duplex")) == NULL)
    if ((duplex = ppdFindOption(ppd, "JCLDuplex")) == NULL)
      if ((duplex = ppdFindOption(ppd, "EFDuplex")) == NULL)
        if ((duplex = ppdFindOption(ppd, "EFDuplexing")) == NULL)
	  duplex = ppdFindOption(ppd, "KD03Duplex");

  if (duplex)
  {
    pc->sides_option = strdup(duplex->keyword);

    for (i = duplex->num_choices, choice = duplex->choices;
         i > 0;
	 i --, choice ++)
    {
      if ((!_cups_strcasecmp(choice->choice, "None") ||
	   !_cups_strcasecmp(choice->choice, "False")) && !pc->sides_1sided)
        pc->sides_1sided = strdup(choice->choice);
      else if ((!_cups_strcasecmp(choice->choice, "DuplexNoTumble") ||
	        !_cups_strcasecmp(choice->choice, "LongEdge") ||
	        !_cups_strcasecmp(choice->choice, "Top")) && !pc->sides_2sided_long)
        pc->sides_2sided_long = strdup(choice->choice);
      else if ((!_cups_strcasecmp(choice->choice, "DuplexTumble") ||
	        !_cups_strcasecmp(choice->choice, "ShortEdge") ||
	        !_cups_strcasecmp(choice->choice, "Bottom")) &&
	       !pc->sides_2sided_short)
        pc->sides_2sided_short = strdup(choice->choice);
    }
  }

 /*
  * Copy filters and pre-filters...
  */

  pc->filters = cupsArrayNew3(NULL, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free);

  cupsArrayAdd(pc->filters,
               "application/vnd.cups-raw application/octet-stream 0 -");

  if ((ppd_attr = ppdFindAttr(ppd, "cupsFilter2", NULL)) != NULL)
  {
    do
    {
      cupsArrayAdd(pc->filters, ppd_attr->value);
    }
    while ((ppd_attr = ppdFindNextAttr(ppd, "cupsFilter2", NULL)) != NULL);
  }
  else if (ppd->num_filters > 0)
  {
    for (i = 0; i < ppd->num_filters; i ++)
      cupsArrayAdd(pc->filters, ppd->filters[i]);
  }
  else
    cupsArrayAdd(pc->filters, "application/vnd.cups-postscript 0 -");

 /*
  * See if we have a command filter...
  */

  for (filter = (const char *)cupsArrayFirst(pc->filters);
       filter;
       filter = (const char *)cupsArrayNext(pc->filters))
    if (!_cups_strncasecmp(filter, "application/vnd.cups-command", 28) &&
        _cups_isspace(filter[28]))
      break;

  if (!filter &&
      ((ppd_attr = ppdFindAttr(ppd, "cupsCommands", NULL)) == NULL ||
       _cups_strcasecmp(ppd_attr->value, "none")))
  {
   /*
    * No command filter and no cupsCommands keyword telling us not to use one.
    * See if this is a PostScript printer, and if so add a PostScript command
    * filter...
    */

    for (filter = (const char *)cupsArrayFirst(pc->filters);
	 filter;
	 filter = (const char *)cupsArrayNext(pc->filters))
      if (!_cups_strncasecmp(filter, "application/vnd.cups-postscript", 31) &&
	  _cups_isspace(filter[31]))
	break;

    if (filter)
      cupsArrayAdd(pc->filters,
                   "application/vnd.cups-command application/postscript 100 "
                   "commandtops");
  }

  if ((ppd_attr = ppdFindAttr(ppd, "cupsPreFilter", NULL)) != NULL)
  {
    pc->prefilters = cupsArrayNew3(NULL, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free);

    do
    {
      cupsArrayAdd(pc->prefilters, ppd_attr->value);
    }
    while ((ppd_attr = ppdFindNextAttr(ppd, "cupsPreFilter", NULL)) != NULL);
  }

  if ((ppd_attr = ppdFindAttr(ppd, "cupsSingleFile", NULL)) != NULL)
    pc->single_file = !_cups_strcasecmp(ppd_attr->value, "true");

 /*
  * Copy the product string, if any...
  */

  if (ppd->product)
    pc->product = strdup(ppd->product);

 /*
  * Copy finishings mapping data...
  */

  if ((ppd_attr = ppdFindAttr(ppd, "cupsIPPFinishings", NULL)) != NULL)
  {
   /*
    * Have proper vendor mapping of IPP finishings values to PPD options...
    */

    pc->finishings = cupsArrayNew3((cups_array_func_t)pwg_compare_finishings,
                                   NULL, NULL, 0, NULL,
                                   (cups_afree_func_t)pwg_free_finishings);

    do
    {
      if ((finishings = calloc(1, sizeof(_pwg_finishings_t))) == NULL)
        goto create_error;

      finishings->value       = (ipp_finishings_t)atoi(ppd_attr->spec);
      finishings->num_options = _ppdParseOptions(ppd_attr->value, 0,
                                                 &(finishings->options),
                                                 _PPD_PARSE_OPTIONS);

      cupsArrayAdd(pc->finishings, finishings);
    }
    while ((ppd_attr = ppdFindNextAttr(ppd, "cupsIPPFinishings",
                                       NULL)) != NULL);
  }
  else
  {
   /*
    * No IPP mapping data, try to map common/standard PPD keywords...
    */

    pc->finishings = cupsArrayNew3((cups_array_func_t)pwg_compare_finishings, NULL, NULL, 0, NULL, (cups_afree_func_t)pwg_free_finishings);

    if ((ppd_option = ppdFindOption(ppd, "StapleLocation")) != NULL)
    {
     /*
      * Add staple finishings...
      */

      if (ppdFindChoice(ppd_option, "SinglePortrait"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_STAPLE_TOP_LEFT, "StapleLocation", "SinglePortrait");
      if (ppdFindChoice(ppd_option, "UpperLeft")) /* Ricoh extension */
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_STAPLE_TOP_LEFT, "StapleLocation", "UpperLeft");
      if (ppdFindChoice(ppd_option, "UpperRight")) /* Ricoh extension */
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_STAPLE_TOP_RIGHT, "StapleLocation", "UpperRight");
      if (ppdFindChoice(ppd_option, "SingleLandscape"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_STAPLE_BOTTOM_LEFT, "StapleLocation", "SingleLandscape");
      if (ppdFindChoice(ppd_option, "DualLandscape"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_STAPLE_DUAL_LEFT, "StapleLocation", "DualLandscape");
    }

    if ((ppd_option = ppdFindOption(ppd, "RIPunch")) != NULL)
    {
     /*
      * Add (Ricoh) punch finishings...
      */

      if (ppdFindChoice(ppd_option, "Left2"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_PUNCH_DUAL_LEFT, "RIPunch", "Left2");
      if (ppdFindChoice(ppd_option, "Left3"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_PUNCH_TRIPLE_LEFT, "RIPunch", "Left3");
      if (ppdFindChoice(ppd_option, "Left4"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_PUNCH_QUAD_LEFT, "RIPunch", "Left4");
      if (ppdFindChoice(ppd_option, "Right2"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_PUNCH_DUAL_RIGHT, "RIPunch", "Right2");
      if (ppdFindChoice(ppd_option, "Right3"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_PUNCH_TRIPLE_RIGHT, "RIPunch", "Right3");
      if (ppdFindChoice(ppd_option, "Right4"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_PUNCH_QUAD_RIGHT, "RIPunch", "Right4");
      if (ppdFindChoice(ppd_option, "Upper2"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_PUNCH_DUAL_TOP, "RIPunch", "Upper2");
      if (ppdFindChoice(ppd_option, "Upper3"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_PUNCH_TRIPLE_TOP, "RIPunch", "Upper3");
      if (ppdFindChoice(ppd_option, "Upper4"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_PUNCH_QUAD_TOP, "RIPunch", "Upper4");
    }

    if ((ppd_option = ppdFindOption(ppd, "BindEdge")) != NULL)
    {
     /*
      * Add bind finishings...
      */

      if (ppdFindChoice(ppd_option, "Left"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_BIND_LEFT, "BindEdge", "Left");
      if (ppdFindChoice(ppd_option, "Right"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_BIND_RIGHT, "BindEdge", "Right");
      if (ppdFindChoice(ppd_option, "Top"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_BIND_TOP, "BindEdge", "Top");
      if (ppdFindChoice(ppd_option, "Bottom"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_BIND_BOTTOM, "BindEdge", "Bottom");
    }

    if ((ppd_option = ppdFindOption(ppd, "FoldType")) != NULL)
    {
     /*
      * Add (Adobe) fold finishings...
      */

      if (ppdFindChoice(ppd_option, "ZFold"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_FOLD_Z, "FoldType", "ZFold");
      if (ppdFindChoice(ppd_option, "Saddle"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_FOLD_HALF, "FoldType", "Saddle");
      if (ppdFindChoice(ppd_option, "DoubleGate"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_FOLD_DOUBLE_GATE, "FoldType", "DoubleGate");
      if (ppdFindChoice(ppd_option, "LeftGate"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_FOLD_LEFT_GATE, "FoldType", "LeftGate");
      if (ppdFindChoice(ppd_option, "RightGate"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_FOLD_RIGHT_GATE, "FoldType", "RightGate");
      if (ppdFindChoice(ppd_option, "Letter"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_FOLD_LETTER, "FoldType", "Letter");
      if (ppdFindChoice(ppd_option, "XFold"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_FOLD_POSTER, "FoldType", "XFold");
    }

    if ((ppd_option = ppdFindOption(ppd, "RIFoldType")) != NULL)
    {
     /*
      * Add (Ricoh) fold finishings...
      */

      if (ppdFindChoice(ppd_option, "OutsideTwoFold"))
        pwg_add_finishing(pc->finishings, IPP_FINISHINGS_FOLD_LETTER, "RIFoldType", "OutsideTwoFold");
    }

    if (cupsArrayCount(pc->finishings) == 0)
    {
      cupsArrayDelete(pc->finishings);
      pc->finishings = NULL;
    }
  }

  if ((ppd_option = ppdFindOption(ppd, "cupsFinishingTemplate")) != NULL)
  {
    pc->templates = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free);

    for (choice = ppd_option->choices, i = ppd_option->num_choices; i > 0; choice ++, i --)
    {
      cupsArrayAdd(pc->templates, (void *)choice->choice);

     /*
      * Add localized text for PWG keyword to message catalog...
      */

      snprintf(msg_id, sizeof(msg_id), "finishing-template.%s", choice->choice);
      pwg_add_message(pc->strings, msg_id, choice->text);
    }
  }

 /*
  * Max copies...
  */

  if ((ppd_attr = ppdFindAttr(ppd, "cupsMaxCopies", NULL)) != NULL)
    pc->max_copies = atoi(ppd_attr->value);
  else if (ppd->manual_copies)
    pc->max_copies = 1;
  else
    pc->max_copies = 9999;

 /*
  * cupsChargeInfoURI, cupsJobAccountId, cupsJobAccountingUserId,
  * cupsJobPassword, and cupsMandatory.
  */

  if ((ppd_attr = ppdFindAttr(ppd, "cupsChargeInfoURI", NULL)) != NULL)
    pc->charge_info_uri = strdup(ppd_attr->value);

  if ((ppd_attr = ppdFindAttr(ppd, "cupsJobAccountId", NULL)) != NULL)
    pc->account_id = !_cups_strcasecmp(ppd_attr->value, "true");

  if ((ppd_attr = ppdFindAttr(ppd, "cupsJobAccountingUserId", NULL)) != NULL)
    pc->accounting_user_id = !_cups_strcasecmp(ppd_attr->value, "true");

  if ((ppd_attr = ppdFindAttr(ppd, "cupsJobPassword", NULL)) != NULL)
    pc->password = strdup(ppd_attr->value);

  if ((ppd_attr = ppdFindAttr(ppd, "cupsMandatory", NULL)) != NULL)
    pc->mandatory = _cupsArrayNewStrings(ppd_attr->value, ' ');

 /*
  * Support files...
  */

  pc->support_files = cupsArrayNew3(NULL, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free);

  for (ppd_attr = ppdFindAttr(ppd, "cupsICCProfile", NULL);
       ppd_attr;
       ppd_attr = ppdFindNextAttr(ppd, "cupsICCProfile", NULL))
    cupsArrayAdd(pc->support_files, ppd_attr->value);

#ifdef HAVE_APPLICATIONSERVICES_H
  if ((ppd_attr = ppdFindAttr(ppd, "APPrinterIconPath", NULL)) != NULL)
    cupsArrayAdd(pc->support_files, ppd_attr->value);
#endif

 /*
  * Return the cache data...
  */

  return (pc);

 /*
  * If we get here we need to destroy the PWG mapping data and return NULL...
  */

  create_error:

  _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Out of memory."), 1);
  _ppdCacheDestroy(pc);

  return (NULL);
}


/*
 * '_ppdCacheDestroy()' - Free all memory used for PWG mapping data.
 */

void
_ppdCacheDestroy(_ppd_cache_t *pc)	/* I - PPD cache and mapping data */
{
  int		i;			/* Looping var */
  pwg_map_t	*map;			/* Current map */
  pwg_size_t	*size;			/* Current size */


 /*
  * Range check input...
  */

  if (!pc)
    return;

 /*
  * Free memory as needed...
  */

  if (pc->bins)
  {
    for (i = pc->num_bins, map = pc->bins; i > 0; i --, map ++)
    {
      free(map->pwg);
      free(map->ppd);
    }

    free(pc->bins);
  }

  if (pc->sizes)
  {
    for (i = pc->num_sizes, size = pc->sizes; i > 0; i --, size ++)
    {
      free(size->map.pwg);
      free(size->map.ppd);
    }

    free(pc->sizes);
  }

  free(pc->source_option);

  if (pc->sources)
  {
    for (i = pc->num_sources, map = pc->sources; i > 0; i --, map ++)
    {
      free(map->pwg);
      free(map->ppd);
    }

    free(pc->sources);
  }

  if (pc->types)
  {
    for (i = pc->num_types, map = pc->types; i > 0; i --, map ++)
    {
      free(map->pwg);
      free(map->ppd);
    }

    free(pc->types);
  }

  free(pc->custom_max_keyword);
  free(pc->custom_min_keyword);

  free(pc->product);
  cupsArrayDelete(pc->filters);
  cupsArrayDelete(pc->prefilters);
  cupsArrayDelete(pc->finishings);

  free(pc->charge_info_uri);
  free(pc->password);

  cupsArrayDelete(pc->mandatory);

  cupsArrayDelete(pc->support_files);

  cupsArrayDelete(pc->strings);

  free(pc);
}


/*
 * '_ppdCacheGetBin()' - Get the PWG output-bin keyword associated with a PPD
 *                  OutputBin.
 */

const char *				/* O - output-bin or NULL */
_ppdCacheGetBin(
    _ppd_cache_t *pc,			/* I - PPD cache and mapping data */
    const char   *output_bin)		/* I - PPD OutputBin string */
{
  int	i;				/* Looping var */


 /*
  * Range check input...
  */

  if (!pc || !output_bin)
    return (NULL);

 /*
  * Look up the OutputBin string...
  */


  for (i = 0; i < pc->num_bins; i ++)
    if (!_cups_strcasecmp(output_bin, pc->bins[i].ppd) || !_cups_strcasecmp(output_bin, pc->bins[i].pwg))
      return (pc->bins[i].pwg);

  return (NULL);
}


/*
 * '_ppdCacheGetFinishingOptions()' - Get PPD finishing options for the given
 *                                    IPP finishings value(s).
 */

int					/* O  - New number of options */
_ppdCacheGetFinishingOptions(
    _ppd_cache_t     *pc,		/* I  - PPD cache and mapping data */
    ipp_t            *job,		/* I  - Job attributes or NULL */
    ipp_finishings_t value,		/* I  - IPP finishings value of IPP_FINISHINGS_NONE */
    int              num_options,	/* I  - Number of options */
    cups_option_t    **options)		/* IO - Options */
{
  int			i;		/* Looping var */
  _pwg_finishings_t	*f,		/* PWG finishings options */
			key;		/* Search key */
  ipp_attribute_t	*attr;		/* Finishings attribute */
  cups_option_t		*option;	/* Current finishings option */


 /*
  * Range check input...
  */

  if (!pc || cupsArrayCount(pc->finishings) == 0 || !options ||
      (!job && value == IPP_FINISHINGS_NONE))
    return (num_options);

 /*
  * Apply finishing options...
  */

  if (job && (attr = ippFindAttribute(job, "finishings", IPP_TAG_ENUM)) != NULL)
  {
    int	num_values = ippGetCount(attr);	/* Number of values */

    for (i = 0; i < num_values; i ++)
    {
      key.value = (ipp_finishings_t)ippGetInteger(attr, i);

      if ((f = cupsArrayFind(pc->finishings, &key)) != NULL)
      {
        int	j;			/* Another looping var */

        for (j = f->num_options, option = f->options; j > 0; j --, option ++)
          num_options = cupsAddOption(option->name, option->value,
                                      num_options, options);
      }
    }
  }
  else if (value != IPP_FINISHINGS_NONE)
  {
    key.value = value;

    if ((f = cupsArrayFind(pc->finishings, &key)) != NULL)
    {
      int	j;			/* Another looping var */

      for (j = f->num_options, option = f->options; j > 0; j --, option ++)
	num_options = cupsAddOption(option->name, option->value,
				    num_options, options);
    }
  }

  return (num_options);
}


/*
 * '_ppdCacheGetFinishingValues()' - Get IPP finishings value(s) from the given
 *                                   PPD options.
 */

int					/* O - Number of finishings values */
_ppdCacheGetFinishingValues(
    ppd_file_t    *ppd,			/* I - Marked PPD file */
    _ppd_cache_t  *pc,			/* I - PPD cache and mapping data */
    int           max_values,		/* I - Maximum number of finishings values */
    int           *values)		/* O - Finishings values */
{
  int			i,		/* Looping var */
			num_values = 0;	/* Number of values */
  _pwg_finishings_t	*f;		/* Current finishings option */
  cups_option_t		*option;	/* Current option */
  ppd_choice_t		*choice;	/* Marked PPD choice */


 /*
  * Range check input...
  */

  DEBUG_printf(("_ppdCacheGetFinishingValues(ppd=%p, pc=%p, max_values=%d, values=%p)", ppd, pc, max_values, values));

  if (!ppd || !pc || max_values < 1 || !values)
  {
    DEBUG_puts("_ppdCacheGetFinishingValues: Bad arguments, returning 0.");
    return (0);
  }
  else if (!pc->finishings)
  {
    DEBUG_puts("_ppdCacheGetFinishingValues: No finishings support, returning 0.");
    return (0);
  }

 /*
  * Go through the finishings options and see what is set...
  */

  for (f = (_pwg_finishings_t *)cupsArrayFirst(pc->finishings);
       f;
       f = (_pwg_finishings_t *)cupsArrayNext(pc->finishings))
  {
    DEBUG_printf(("_ppdCacheGetFinishingValues: Checking %d (%s)", (int)f->value, ippEnumString("finishings", (int)f->value)));

    for (i = f->num_options, option = f->options; i > 0; i --, option ++)
    {
      DEBUG_printf(("_ppdCacheGetFinishingValues: %s=%s?", option->name, option->value));

      if ((choice = ppdFindMarkedChoice(ppd, option->name)) == NULL || _cups_strcasecmp(option->value, choice->choice))
      {
        DEBUG_puts("_ppdCacheGetFinishingValues: NO");
        break;
      }
    }

    if (i == 0)
    {
      DEBUG_printf(("_ppdCacheGetFinishingValues: Adding %d (%s)", (int)f->value, ippEnumString("finishings", (int)f->value)));

      values[num_values ++] = (int)f->value;

      if (num_values >= max_values)
        break;
    }
  }

  if (num_values == 0)
  {
   /*
    * Always have at least "finishings" = 'none'...
    */

    DEBUG_puts("_ppdCacheGetFinishingValues: Adding 3 (none).");
    values[0] = IPP_FINISHINGS_NONE;
    num_values ++;
  }

  DEBUG_printf(("_ppdCacheGetFinishingValues: Returning %d.", num_values));

  return (num_values);
}


/*
 * 'ppd_inputslot_for_keyword()' - Return the PPD InputSlot associated
 *                                a keyword string, or NULL if no mapping
 *                                exists.
 */
static const char *			/* O - PPD InputSlot or NULL */
ppd_inputslot_for_keyword(
    _ppd_cache_t *pc,			/* I - PPD cache and mapping data */
    const char   *keyword)		/* I - Keyword string */
{
  int	i;				/* Looping var */

  if (!pc || !keyword)
    return (NULL);

  for (i = 0; i < pc->num_sources; i ++)
    if (!_cups_strcasecmp(keyword, pc->sources[i].pwg) || !_cups_strcasecmp(keyword, pc->sources[i].ppd))
      return (pc->sources[i].ppd);

  return (NULL);
}

/*
 * '_ppdCacheGetInputSlot()' - Get the PPD InputSlot associated with the job
 *                        attributes or a keyword string.
 */

const char *				/* O - PPD InputSlot or NULL */
_ppdCacheGetInputSlot(
    _ppd_cache_t *pc,			/* I - PPD cache and mapping data */
    ipp_t        *job,			/* I - Job attributes or NULL */
    const char   *keyword)		/* I - Keyword string or NULL */
{
 /*
  * Range check input...
  */

  if (!pc || pc->num_sources == 0 || (!job && !keyword))
    return (NULL);

  if (job && !keyword)
  {
   /*
    * Lookup the media-col attribute and any media-source found there...
    */

    ipp_attribute_t	*media_col,	/* media-col attribute */
			*media_source;	/* media-source attribute */
    pwg_size_t		size;		/* Dimensional size */
    int			margins_set;	/* Were the margins set? */

    media_col = ippFindAttribute(job, "media-col", IPP_TAG_BEGIN_COLLECTION);
    if (media_col &&
        (media_source = ippFindAttribute(ippGetCollection(media_col, 0),
                                         "media-source",
	                                 IPP_TAG_KEYWORD)) != NULL)
    {
     /*
      * Use the media-source value from media-col...
      */

      keyword = ippGetString(media_source, 0, NULL);
    }
    else if (pwgInitSize(&size, job, &margins_set))
    {
     /*
      * For media <= 5x7, try to ask for automatic selection so the printer can
      * pick the photo tray.  If auto isn't available, fall back to explicitly
      * asking for the photo tray.
      */

      if (size.width <= (5 * 2540) && size.length <= (7 * 2540)) {
        const char* match;
        if ((match = ppd_inputslot_for_keyword(pc, "auto")) != NULL)
          return (match);
        keyword = "photo";
      }
    }
  }

  return (ppd_inputslot_for_keyword(pc, keyword));
}


/*
 * '_ppdCacheGetMediaType()' - Get the PPD MediaType associated with the job
 *                        attributes or a keyword string.
 */

const char *				/* O - PPD MediaType or NULL */
_ppdCacheGetMediaType(
    _ppd_cache_t *pc,			/* I - PPD cache and mapping data */
    ipp_t        *job,			/* I - Job attributes or NULL */
    const char   *keyword)		/* I - Keyword string or NULL */
{
 /*
  * Range check input...
  */

  if (!pc || pc->num_types == 0 || (!job && !keyword))
    return (NULL);

  if (job && !keyword)
  {
   /*
    * Lookup the media-col attribute and any media-source found there...
    */

    ipp_attribute_t	*media_col,	/* media-col attribute */
			*media_type;	/* media-type attribute */

    media_col = ippFindAttribute(job, "media-col", IPP_TAG_BEGIN_COLLECTION);
    if (media_col)
    {
      if ((media_type = ippFindAttribute(media_col->values[0].collection,
                                         "media-type",
	                                 IPP_TAG_KEYWORD)) == NULL)
	media_type = ippFindAttribute(media_col->values[0].collection,
				      "media-type", IPP_TAG_NAME);

      if (media_type)
	keyword = media_type->values[0].string.text;
    }
  }

  if (keyword)
  {
    int	i;				/* Looping var */

    for (i = 0; i < pc->num_types; i ++)
      if (!_cups_strcasecmp(keyword, pc->types[i].pwg) || !_cups_strcasecmp(keyword, pc->types[i].ppd))
        return (pc->types[i].ppd);
  }

  return (NULL);
}


/*
 * '_ppdCacheGetOutputBin()' - Get the PPD OutputBin associated with the keyword
 *                        string.
 */

const char *				/* O - PPD OutputBin or NULL */
_ppdCacheGetOutputBin(
    _ppd_cache_t *pc,			/* I - PPD cache and mapping data */
    const char   *output_bin)		/* I - Keyword string */
{
  int	i;				/* Looping var */


 /*
  * Range check input...
  */

  if (!pc || !output_bin)
    return (NULL);

 /*
  * Look up the OutputBin string...
  */


  for (i = 0; i < pc->num_bins; i ++)
    if (!_cups_strcasecmp(output_bin, pc->bins[i].pwg) || !_cups_strcasecmp(output_bin, pc->bins[i].ppd))
      return (pc->bins[i].ppd);

  return (NULL);
}


/*
 * '_ppdCacheGetPageSize()' - Get the PPD PageSize associated with the job
 *                       attributes or a keyword string.
 */

const char *				/* O - PPD PageSize or NULL */
_ppdCacheGetPageSize(
    _ppd_cache_t *pc,			/* I - PPD cache and mapping data */
    ipp_t        *job,			/* I - Job attributes or NULL */
    const char   *keyword,		/* I - Keyword string or NULL */
    int          *exact)		/* O - 1 if exact match, 0 otherwise */
{
  int		i;			/* Looping var */
  pwg_size_t	*size,			/* Current size */
		*closest,		/* Closest size */
		jobsize;		/* Size data from job */
  int		margins_set,		/* Were the margins set? */
		dwidth,			/* Difference in width */
		dlength,		/* Difference in length */
		dleft,			/* Difference in left margins */
		dright,			/* Difference in right margins */
		dbottom,		/* Difference in bottom margins */
		dtop,			/* Difference in top margins */
		dmin,			/* Minimum difference */
		dclosest;		/* Closest difference */
  const char	*ppd_name;		/* PPD media name */


  DEBUG_printf(("_ppdCacheGetPageSize(pc=%p, job=%p, keyword=\"%s\", exact=%p)",
	        pc, job, keyword, exact));

 /*
  * Range check input...
  */

  if (!pc || (!job && !keyword))
    return (NULL);

  if (exact)
    *exact = 0;

  ppd_name = keyword;

  if (job)
  {
   /*
    * Try getting the PPD media name from the job attributes...
    */

    ipp_attribute_t	*attr;		/* Job attribute */

    if ((attr = ippFindAttribute(job, "PageSize", IPP_TAG_ZERO)) == NULL)
      if ((attr = ippFindAttribute(job, "PageRegion", IPP_TAG_ZERO)) == NULL)
        attr = ippFindAttribute(job, "media", IPP_TAG_ZERO);

#ifdef DEBUG
    if (attr)
      DEBUG_printf(("1_ppdCacheGetPageSize: Found attribute %s (%s)",
                    attr->name, ippTagString(attr->value_tag)));
    else
      DEBUG_puts("1_ppdCacheGetPageSize: Did not find media attribute.");
#endif /* DEBUG */

    if (attr && (attr->value_tag == IPP_TAG_NAME ||
                 attr->value_tag == IPP_TAG_KEYWORD))
      ppd_name = attr->values[0].string.text;
  }

  DEBUG_printf(("1_ppdCacheGetPageSize: ppd_name=\"%s\"", ppd_name));

  if (ppd_name)
  {
   /*
    * Try looking up the named PPD size first...
    */

    for (i = pc->num_sizes, size = pc->sizes; i > 0; i --, size ++)
    {
      DEBUG_printf(("2_ppdCacheGetPageSize: size[%d]=[\"%s\" \"%s\"]",
                    (int)(size - pc->sizes), size->map.pwg, size->map.ppd));

      if (!_cups_strcasecmp(ppd_name, size->map.ppd) ||
          !_cups_strcasecmp(ppd_name, size->map.pwg))
      {
	if (exact)
	  *exact = 1;

        DEBUG_printf(("1_ppdCacheGetPageSize: Returning \"%s\"", ppd_name));

        return (size->map.ppd);
      }
    }
  }

  if (job && !keyword)
  {
   /*
    * Get the size using media-col or media, with the preference being
    * media-col.
    */

    if (!pwgInitSize(&jobsize, job, &margins_set))
      return (NULL);
  }
  else
  {
   /*
    * Get the size using a media keyword...
    */

    pwg_media_t	*media;		/* Media definition */


    if ((media = pwgMediaForPWG(keyword)) == NULL)
      if ((media = pwgMediaForLegacy(keyword)) == NULL)
        if ((media = pwgMediaForPPD(keyword)) == NULL)
	  return (NULL);

    jobsize.width  = media->width;
    jobsize.length = media->length;
    margins_set    = 0;
  }

 /*
  * Now that we have the dimensions and possibly the margins, look at the
  * available sizes and find the match...
  */

  closest  = NULL;
  dclosest = 999999999;

  if (!ppd_name || _cups_strncasecmp(ppd_name, "Custom.", 7) ||
      _cups_strncasecmp(ppd_name, "custom_", 7))
  {
    for (i = pc->num_sizes, size = pc->sizes; i > 0; i --, size ++)
    {
     /*
      * Adobe uses a size matching algorithm with an epsilon of 5 points, which
      * is just about 176/2540ths...
      */

      dwidth  = size->width - jobsize.width;
      dlength = size->length - jobsize.length;

      if (dwidth <= -176 || dwidth >= 176 || dlength <= -176 || dlength >= 176)
	continue;

      if (margins_set)
      {
       /*
	* Use a tighter epsilon of 1 point (35/2540ths) for margins...
	*/

	dleft   = size->left - jobsize.left;
	dright  = size->right - jobsize.right;
	dtop    = size->top - jobsize.top;
	dbottom = size->bottom - jobsize.bottom;

	if (dleft <= -35 || dleft >= 35 || dright <= -35 || dright >= 35 ||
	    dtop <= -35 || dtop >= 35 || dbottom <= -35 || dbottom >= 35)
	{
	  dleft   = dleft < 0 ? -dleft : dleft;
	  dright  = dright < 0 ? -dright : dright;
	  dbottom = dbottom < 0 ? -dbottom : dbottom;
	  dtop    = dtop < 0 ? -dtop : dtop;
	  dmin    = dleft + dright + dbottom + dtop;

	  if (dmin < dclosest)
	  {
	    dclosest = dmin;
	    closest  = size;
	  }

	  continue;
	}
      }

      if (exact)
	*exact = 1;

      DEBUG_printf(("1_ppdCacheGetPageSize: Returning \"%s\"", size->map.ppd));

      return (size->map.ppd);
    }
  }

  if (closest)
  {
    DEBUG_printf(("1_ppdCacheGetPageSize: Returning \"%s\" (closest)",
                  closest->map.ppd));

    return (closest->map.ppd);
  }

 /*
  * If we get here we need to check for custom page size support...
  */

  if (jobsize.width >= pc->custom_min_width &&
      jobsize.width <= pc->custom_max_width &&
      jobsize.length >= pc->custom_min_length &&
      jobsize.length <= pc->custom_max_length)
  {
   /*
    * In range, format as Custom.WWWWxLLLL (points).
    */

    snprintf(pc->custom_ppd_size, sizeof(pc->custom_ppd_size), "Custom.%dx%d",
             (int)PWG_TO_POINTS(jobsize.width), (int)PWG_TO_POINTS(jobsize.length));

    if (margins_set && exact)
    {
      dleft   = pc->custom_size.left - jobsize.left;
      dright  = pc->custom_size.right - jobsize.right;
      dtop    = pc->custom_size.top - jobsize.top;
      dbottom = pc->custom_size.bottom - jobsize.bottom;

      if (dleft > -35 && dleft < 35 && dright > -35 && dright < 35 &&
          dtop > -35 && dtop < 35 && dbottom > -35 && dbottom < 35)
	*exact = 1;
    }
    else if (exact)
      *exact = 1;

    DEBUG_printf(("1_ppdCacheGetPageSize: Returning \"%s\" (custom)",
                  pc->custom_ppd_size));

    return (pc->custom_ppd_size);
  }

 /*
  * No custom page size support or the size is out of range - return NULL.
  */

  DEBUG_puts("1_ppdCacheGetPageSize: Returning NULL");

  return (NULL);
}


/*
 * '_ppdCacheGetSize()' - Get the PWG size associated with a PPD PageSize.
 */

pwg_size_t *				/* O - PWG size or NULL */
_ppdCacheGetSize(
    _ppd_cache_t *pc,			/* I - PPD cache and mapping data */
    const char   *page_size)		/* I - PPD PageSize */
{
  int		i;			/* Looping var */
  pwg_media_t	*media;			/* Media */
  pwg_size_t	*size;			/* Current size */


 /*
  * Range check input...
  */

  if (!pc || !page_size)
    return (NULL);

  if (!_cups_strncasecmp(page_size, "Custom.", 7))
  {
   /*
    * Custom size; size name can be one of the following:
    *
    *    Custom.WIDTHxLENGTHin    - Size in inches
    *    Custom.WIDTHxLENGTHft    - Size in feet
    *    Custom.WIDTHxLENGTHcm    - Size in centimeters
    *    Custom.WIDTHxLENGTHmm    - Size in millimeters
    *    Custom.WIDTHxLENGTHm     - Size in meters
    *    Custom.WIDTHxLENGTH[pt]  - Size in points
    */

    double		w, l;		/* Width and length of page */
    char		*ptr;		/* Pointer into PageSize */
    struct lconv	*loc;		/* Locale data */

    loc = localeconv();
    w   = (float)_cupsStrScand(page_size + 7, &ptr, loc);
    if (!ptr || *ptr != 'x')
      return (NULL);

    l = (float)_cupsStrScand(ptr + 1, &ptr, loc);
    if (!ptr)
      return (NULL);

    if (!_cups_strcasecmp(ptr, "in"))
    {
      w *= 2540.0;
      l *= 2540.0;
    }
    else if (!_cups_strcasecmp(ptr, "ft"))
    {
      w *= 12.0 * 2540.0;
      l *= 12.0 * 2540.0;
    }
    else if (!_cups_strcasecmp(ptr, "mm"))
    {
      w *= 100.0;
      l *= 100.0;
    }
    else if (!_cups_strcasecmp(ptr, "cm"))
    {
      w *= 1000.0;
      l *= 1000.0;
    }
    else if (!_cups_strcasecmp(ptr, "m"))
    {
      w *= 100000.0;
      l *= 100000.0;
    }
    else
    {
      w *= 2540.0 / 72.0;
      l *= 2540.0 / 72.0;
    }

    pc->custom_size.width  = (int)w;
    pc->custom_size.length = (int)l;

    return (&(pc->custom_size));
  }

 /*
  * Not a custom size - look it up...
  */

  for (i = pc->num_sizes, size = pc->sizes; i > 0; i --, size ++)
    if (!_cups_strcasecmp(page_size, size->map.ppd) ||
        !_cups_strcasecmp(page_size, size->map.pwg))
      return (size);

 /*
  * Look up standard sizes...
  */

  if ((media = pwgMediaForPPD(page_size)) == NULL)
    if ((media = pwgMediaForLegacy(page_size)) == NULL)
      media = pwgMediaForPWG(page_size);

  if (media)
  {
    pc->custom_size.width  = media->width;
    pc->custom_size.length = media->length;

    return (&(pc->custom_size));
  }

  return (NULL);
}


/*
 * '_ppdCacheGetSource()' - Get the PWG media-source associated with a PPD
 *                          InputSlot.
 */

const char *				/* O - PWG media-source keyword */
_ppdCacheGetSource(
    _ppd_cache_t *pc,			/* I - PPD cache and mapping data */
    const char   *input_slot)		/* I - PPD InputSlot */
{
  int		i;			/* Looping var */
  pwg_map_t	*source;		/* Current source */


 /*
  * Range check input...
  */

  if (!pc || !input_slot)
    return (NULL);

  for (i = pc->num_sources, source = pc->sources; i > 0; i --, source ++)
    if (!_cups_strcasecmp(input_slot, source->ppd) || !_cups_strcasecmp(input_slot, source->pwg))
      return (source->pwg);

  return (NULL);
}


/*
 * '_ppdCacheGetType()' - Get the PWG media-type associated with a PPD
 *                        MediaType.
 */

const char *				/* O - PWG media-type keyword */
_ppdCacheGetType(
    _ppd_cache_t *pc,			/* I - PPD cache and mapping data */
    const char   *media_type)		/* I - PPD MediaType */
{
  int		i;			/* Looping var */
  pwg_map_t	*type;			/* Current type */


 /*
  * Range check input...
  */

  if (!pc || !media_type)
    return (NULL);

  for (i = pc->num_types, type = pc->types; i > 0; i --, type ++)
    if (!_cups_strcasecmp(media_type, type->ppd) || !_cups_strcasecmp(media_type, type->pwg))
      return (type->pwg);

  return (NULL);
}


/*
 * '_ppdCacheWriteFile()' - Write PWG mapping data to a file.
 */

int					/* O - 1 on success, 0 on failure */
_ppdCacheWriteFile(
    _ppd_cache_t *pc,			/* I - PPD cache and mapping data */
    const char   *filename,		/* I - File to write */
    ipp_t        *attrs)		/* I - Attributes to write, if any */
{
  int			i, j, k;	/* Looping vars */
  cups_file_t		*fp;		/* Output file */
  pwg_size_t		*size;		/* Current size */
  pwg_map_t		*map;		/* Current map */
  _pwg_finishings_t	*f;		/* Current finishing option */
  cups_option_t		*option;	/* Current option */
  const char		*value;		/* String value */
  char			newfile[1024];	/* New filename */


 /*
  * Range check input...
  */

  if (!pc || !filename)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (0);
  }

 /*
  * Open the file and write with compression...
  */

  snprintf(newfile, sizeof(newfile), "%s.N", filename);
  if ((fp = cupsFileOpen(newfile, "w9")) == NULL)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    return (0);
  }

 /*
  * Standard header...
  */

  cupsFilePrintf(fp, "#CUPS-PPD-CACHE-%d\n", _PPD_CACHE_VERSION);

 /*
  * Output bins...
  */

  if (pc->num_bins > 0)
  {
    cupsFilePrintf(fp, "NumBins %d\n", pc->num_bins);
    for (i = pc->num_bins, map = pc->bins; i > 0; i --, map ++)
      cupsFilePrintf(fp, "Bin %s %s\n", map->pwg, map->ppd);
  }

 /*
  * Media sizes...
  */

  cupsFilePrintf(fp, "NumSizes %d\n", pc->num_sizes);
  for (i = pc->num_sizes, size = pc->sizes; i > 0; i --, size ++)
    cupsFilePrintf(fp, "Size %s %s %d %d %d %d %d %d\n", size->map.pwg,
		   size->map.ppd, size->width, size->length, size->left,
		   size->bottom, size->right, size->top);
  if (pc->custom_max_width > 0)
    cupsFilePrintf(fp, "CustomSize %d %d %d %d %d %d %d %d\n",
                   pc->custom_max_width, pc->custom_max_length,
		   pc->custom_min_width, pc->custom_min_length,
		   pc->custom_size.left, pc->custom_size.bottom,
		   pc->custom_size.right, pc->custom_size.top);

 /*
  * Media sources...
  */

  if (pc->source_option)
    cupsFilePrintf(fp, "SourceOption %s\n", pc->source_option);

  if (pc->num_sources > 0)
  {
    cupsFilePrintf(fp, "NumSources %d\n", pc->num_sources);
    for (i = pc->num_sources, map = pc->sources; i > 0; i --, map ++)
      cupsFilePrintf(fp, "Source %s %s\n", map->pwg, map->ppd);
  }

 /*
  * Media types...
  */

  if (pc->num_types > 0)
  {
    cupsFilePrintf(fp, "NumTypes %d\n", pc->num_types);
    for (i = pc->num_types, map = pc->types; i > 0; i --, map ++)
      cupsFilePrintf(fp, "Type %s %s\n", map->pwg, map->ppd);
  }

 /*
  * Presets...
  */

  for (i = _PWG_PRINT_COLOR_MODE_MONOCHROME; i < _PWG_PRINT_COLOR_MODE_MAX; i ++)
    for (j = _PWG_PRINT_QUALITY_DRAFT; j < _PWG_PRINT_QUALITY_MAX; j ++)
      if (pc->num_presets[i][j])
      {
	cupsFilePrintf(fp, "Preset %d %d", i, j);
	for (k = pc->num_presets[i][j], option = pc->presets[i][j];
	     k > 0;
	     k --, option ++)
	  cupsFilePrintf(fp, " %s=%s", option->name, option->value);
	cupsFilePutChar(fp, '\n');
      }

 /*
  * Duplex/sides...
  */

  if (pc->sides_option)
    cupsFilePrintf(fp, "SidesOption %s\n", pc->sides_option);

  if (pc->sides_1sided)
    cupsFilePrintf(fp, "Sides1Sided %s\n", pc->sides_1sided);

  if (pc->sides_2sided_long)
    cupsFilePrintf(fp, "Sides2SidedLong %s\n", pc->sides_2sided_long);

  if (pc->sides_2sided_short)
    cupsFilePrintf(fp, "Sides2SidedShort %s\n", pc->sides_2sided_short);

 /*
  * Product, cupsFilter, cupsFilter2, and cupsPreFilter...
  */

  if (pc->product)
    cupsFilePutConf(fp, "Product", pc->product);

  for (value = (const char *)cupsArrayFirst(pc->filters);
       value;
       value = (const char *)cupsArrayNext(pc->filters))
    cupsFilePutConf(fp, "Filter", value);

  for (value = (const char *)cupsArrayFirst(pc->prefilters);
       value;
       value = (const char *)cupsArrayNext(pc->prefilters))
    cupsFilePutConf(fp, "PreFilter", value);

  cupsFilePrintf(fp, "SingleFile %s\n", pc->single_file ? "true" : "false");

 /*
  * Finishing options...
  */

  for (f = (_pwg_finishings_t *)cupsArrayFirst(pc->finishings);
       f;
       f = (_pwg_finishings_t *)cupsArrayNext(pc->finishings))
  {
    cupsFilePrintf(fp, "Finishings %d", f->value);
    for (i = f->num_options, option = f->options; i > 0; i --, option ++)
      cupsFilePrintf(fp, " %s=%s", option->name, option->value);
    cupsFilePutChar(fp, '\n');
  }

  for (value = (const char *)cupsArrayFirst(pc->templates); value; value = (const char *)cupsArrayNext(pc->templates))
    cupsFilePutConf(fp, "FinishingTemplate", value);

 /*
  * Max copies...
  */

  cupsFilePrintf(fp, "MaxCopies %d\n", pc->max_copies);

 /*
  * Accounting/quota/PIN/managed printing values...
  */

  if (pc->charge_info_uri)
    cupsFilePutConf(fp, "ChargeInfoURI", pc->charge_info_uri);

  cupsFilePrintf(fp, "JobAccountId %s\n", pc->account_id ? "true" : "false");
  cupsFilePrintf(fp, "JobAccountingUserId %s\n",
                 pc->accounting_user_id ? "true" : "false");

  if (pc->password)
    cupsFilePutConf(fp, "JobPassword", pc->password);

  for (value = (char *)cupsArrayFirst(pc->mandatory);
       value;
       value = (char *)cupsArrayNext(pc->mandatory))
    cupsFilePutConf(fp, "Mandatory", value);

 /*
  * Support files...
  */

  for (value = (char *)cupsArrayFirst(pc->support_files);
       value;
       value = (char *)cupsArrayNext(pc->support_files))
    cupsFilePutConf(fp, "SupportFile", value);

 /*
  * IPP attributes, if any...
  */

  if (attrs)
  {
    cupsFilePrintf(fp, "IPP " CUPS_LLFMT "\n", CUPS_LLCAST ippLength(attrs));

    attrs->state = IPP_STATE_IDLE;
    ippWriteIO(fp, (ipp_iocb_t)cupsFileWrite, 1, NULL, attrs);
  }

 /*
  * Close and return...
  */

  if (cupsFileClose(fp))
  {
    unlink(newfile);
    return (0);
  }

  unlink(filename);
  return (!rename(newfile, filename));
}


/*
 * '_ppdCreateFromIPP()' - Create a PPD file describing the capabilities
 *                         of an IPP printer.
 */

char *					/* O - PPD filename or @code NULL@ on error */
_ppdCreateFromIPP(char   *buffer,	/* I - Filename buffer */
                  size_t bufsize,	/* I - Size of filename buffer */
		  ipp_t  *supported)	/* I - Get-Printer-Attributes response */
{
  return (_ppdCreateFromIPP2(buffer, bufsize, supported, cupsLangDefault()));
}


/*
 * '_ppdCreateFromIPP()' - Create a PPD file describing the capabilities
 *                         of an IPP printer.
 */


char *
_ppdCreateFromIPP2(
    char        *buffer,		/* I - Filename buffer */
    size_t      bufsize,		/* I - Size of filename buffer */
    ipp_t       *supported,		/* I - Get-Printer-Attributes response */
    cups_lang_t *lang)			/* I - Language */
{
  cups_file_t		*fp;		/* PPD file */
  cups_array_t		*sizes;		/* Media sizes supported by printer */
  cups_size_t		*size;		/* Current media size */
  ipp_attribute_t	*attr,		/* xxx-supported */
			*lang_supp,	/* printer-strings-languages-supported */
			*defattr,	/* xxx-default */
                        *quality,	/* print-quality-supported */
			*x_dim, *y_dim;	/* Media dimensions */
  ipp_t			*media_col,	/* Media collection */
			*media_size;	/* Media size collection */
  char			make[256],	/* Make and model */
			*mptr,		/* Pointer into make and model */
			ppdname[PPD_MAX_NAME];
		    			/* PPD keyword */
  const char		*model;		/* Model name */
  int			i, j,		/* Looping vars */
			count,		/* Number of values */
			bottom,		/* Largest bottom margin */
			left,		/* Largest left margin */
			right,		/* Largest right margin */
			top,		/* Largest top margin */
			max_length = 0,	/* Maximum custom size */
			max_width = 0,
			min_length = INT_MAX,
					/* Minimum custom size */
			min_width = INT_MAX,
			is_apple = 0,	/* Does the printer support Apple raster? */
			is_pdf = 0,	/* Does the printer support PDF? */
			is_pwg = 0;	/* Does the printer support PWG Raster? */
  pwg_media_t		*pwg;		/* PWG media size */
  int			xres, yres;	/* Resolution values */
  int                   resolutions[1000];
                                        /* Array of resolution indices */
  int			have_qdraft = 0,/* Have draft quality? */
			have_qhigh = 0;	/* Have high quality? */
  char			msgid[256];	/* Message identifier (attr.value) */
  const char		*keyword;	/* Keyword value */
  cups_array_t		*strings = NULL;/* Printer strings file */
  struct lconv		*loc = localeconv();
					/* Locale data */
  cups_array_t		*fin_options = NULL;
					/* Finishing options */


 /*
  * Range check input...
  */

  if (buffer)
    *buffer = '\0';

  if (!buffer || bufsize < 1)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (NULL);
  }

  if (!supported)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("No IPP attributes."), 1);
    return (NULL);
  }

 /*
  * Open a temporary file for the PPD...
  */

  if ((fp = cupsTempFile2(buffer, (int)bufsize)) == NULL)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    return (NULL);
  }

 /*
  * Get a sanitized make and model...
  */

  if ((attr = ippFindAttribute(supported, "printer-make-and-model", IPP_TAG_TEXT)) != NULL && ippValidateAttribute(attr))
  {
   /*
    * Sanitize the model name to only contain PPD-safe characters.
    */

    strlcpy(make, ippGetString(attr, 0, NULL), sizeof(make));

    for (mptr = make; *mptr; mptr ++)
    {
      if (*mptr < ' ' || *mptr >= 127 || *mptr == '\"')
      {
       /*
	* Truncate the make and model on the first bad character...
	*/

	*mptr = '\0';
	break;
      }
    }

    while (mptr > make)
    {
     /*
      * Strip trailing whitespace...
      */

      mptr --;
      if (*mptr == ' ')
	*mptr = '\0';
      else
        break;
    }

    if (!make[0])
    {
     /*
      * Use a default make and model if nothing remains...
      */

      strlcpy(make, "Unknown", sizeof(make));
    }
  }
  else
  {
   /*
    * Use a default make and model...
    */

    strlcpy(make, "Unknown", sizeof(make));
  }

  if (!_cups_strncasecmp(make, "Hewlett Packard ", 16) || !_cups_strncasecmp(make, "Hewlett-Packard ", 16))
  {
   /*
    * Normalize HP printer make and model...
    */

    model = make + 16;
    strlcpy(make, "HP", sizeof(make));

    if (!_cups_strncasecmp(model, "HP ", 3))
      model += 3;
  }
  else if ((mptr = strchr(make, ' ')) != NULL)
  {
   /*
    * Separate "MAKE MODEL"...
    */

    while (*mptr && *mptr == ' ')
      *mptr++ = '\0';

    model = mptr;
  }
  else
  {
   /*
    * No separate model name...
    */

    model = "Printer";
  }

 /*
  * Standard stuff for PPD file...
  */

  cupsFilePuts(fp, "*PPD-Adobe: \"4.3\"\n");
  cupsFilePuts(fp, "*FormatVersion: \"4.3\"\n");
  cupsFilePrintf(fp, "*FileVersion: \"%d.%d\"\n", CUPS_VERSION_MAJOR, CUPS_VERSION_MINOR);
  cupsFilePuts(fp, "*LanguageVersion: English\n");
  cupsFilePuts(fp, "*LanguageEncoding: ISOLatin1\n");
  cupsFilePuts(fp, "*PSVersion: \"(3010.000) 0\"\n");
  cupsFilePuts(fp, "*LanguageLevel: \"3\"\n");
  cupsFilePuts(fp, "*FileSystem: False\n");
  cupsFilePuts(fp, "*PCFileName: \"ippeve.ppd\"\n");
  cupsFilePrintf(fp, "*Manufacturer: \"%s\"\n", make);
  cupsFilePrintf(fp, "*ModelName: \"%s\"\n", model);
  cupsFilePrintf(fp, "*Product: \"(%s)\"\n", model);
  cupsFilePrintf(fp, "*NickName: \"%s - IPP Everywhere\"\n", model);
  cupsFilePrintf(fp, "*ShortNickName: \"%s - IPP Everywhere\"\n", model);

  if ((attr = ippFindAttribute(supported, "color-supported", IPP_TAG_BOOLEAN)) != NULL && ippGetBoolean(attr, 0))
    cupsFilePuts(fp, "*ColorDevice: True\n");
  else
    cupsFilePuts(fp, "*ColorDevice: False\n");

  cupsFilePrintf(fp, "*cupsVersion: %d.%d\n", CUPS_VERSION_MAJOR, CUPS_VERSION_MINOR);
#ifdef __APPLE__
  cupsFilePrintf(fp, "*APAirPrint: True\n");
#endif // __APPLE__
  cupsFilePuts(fp, "*cupsSNMPSupplies: False\n");
  cupsFilePrintf(fp, "*cupsLanguages: \"%s", lang->language);
  if ((lang_supp = ippFindAttribute(supported, "printer-strings-languages-supported", IPP_TAG_LANGUAGE)) != NULL)
  {
    for (i = 0, count = ippGetCount(lang_supp); i < count; i ++)
    {
      keyword = ippGetString(lang_supp, i, NULL);

      if (strcmp(keyword, lang->language))
        cupsFilePrintf(fp, " %s", keyword);
    }
  }
  cupsFilePuts(fp, "\"\n");

  if ((attr = ippFindAttribute(supported, "printer-more-info", IPP_TAG_URI)) != NULL && ippValidateAttribute(attr))
    cupsFilePrintf(fp, "*APSupplies: \"%s\"\n", ippGetString(attr, 0, NULL));

  if ((attr = ippFindAttribute(supported, "printer-charge-info-uri", IPP_TAG_URI)) != NULL && ippValidateAttribute(attr))
    cupsFilePrintf(fp, "*cupsChargeInfoURI: \"%s\"\n", ippGetString(attr, 0, NULL));

  if ((attr = ippFindAttribute(supported, "printer-strings-uri", IPP_TAG_URI)) != NULL && ippValidateAttribute(attr))
  {
    http_t	*http = NULL;		/* Connection to printer */
    char	stringsfile[1024];	/* Temporary strings file */

    if (cups_get_url(&http, ippGetString(attr, 0, NULL), stringsfile, sizeof(stringsfile)))
    {
      const char	*printer_uri = ippGetString(ippFindAttribute(supported, "printer-uri-supported", IPP_TAG_URI), 0, NULL);
					// Printer URI
      char		resource[256];	// Resource path
      ipp_t		*request,	// Get-Printer-Attributes request
			*response;	// Response to request

     /*
      * Load strings and save the URL for clients using the destination API
      * instead of this PPD file...
      */

      cupsFilePrintf(fp, "*cupsStringsURI: \"%s\"\n", ippGetString(attr, 0, NULL));

      strings = _cupsMessageLoad(stringsfile, _CUPS_MESSAGE_STRINGS | _CUPS_MESSAGE_UNQUOTE);

      unlink(stringsfile);

      if (lang_supp && printer_uri && cups_connect(&http, printer_uri, resource, sizeof(resource)))
      {
       /*
	* Loop through all of the languages and save their URIs...
	*/

	for (i = 0, count = ippGetCount(lang_supp); i < count; i ++)
	{
	  keyword = ippGetString(lang_supp, i, NULL);

	  request = ippNew();
	  ippSetOperation(request, IPP_OP_GET_PRINTER_ATTRIBUTES);
	  ippSetRequestId(request, i + 1);
	  ippAddString(request, IPP_TAG_OPERATION, IPP_CONST_TAG(IPP_TAG_CHARSET), "attributes-charset", NULL, "utf-8");
	  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE, "attributes-natural-language", NULL, keyword);
	  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, printer_uri);
	  ippAddString(request, IPP_TAG_OPERATION, IPP_CONST_TAG(IPP_TAG_KEYWORD), "requested-attributes", NULL, "printer-strings-uri");

	  response = cupsDoRequest(http, request, resource);

	  if ((attr = ippFindAttribute(response, "printer-strings-uri", IPP_TAG_URI)) != NULL && ippValidateAttribute(attr))
	    cupsFilePrintf(fp, "*cupsStringsURI %s: \"%s\"\n", keyword, ippGetString(attr, 0, NULL));

	  ippDelete(response);
	}
      }
    }

    if (http)
      httpClose(http);
  }

 /*
  * Accounting...
  */

  if (ippGetBoolean(ippFindAttribute(supported, "job-account-id-supported", IPP_TAG_BOOLEAN), 0))
    cupsFilePuts(fp, "*cupsJobAccountId: True\n");

  if (ippGetBoolean(ippFindAttribute(supported, "job-accounting-user-id-supported", IPP_TAG_BOOLEAN), 0))
    cupsFilePuts(fp, "*cupsJobAccountingUserId: True\n");

  if ((attr = ippFindAttribute(supported, "printer-privacy-policy-uri", IPP_TAG_URI)) != NULL && ippValidateAttribute(attr))
    cupsFilePrintf(fp, "*cupsPrivacyURI: \"%s\"\n", ippGetString(attr, 0, NULL));

  if ((attr = ippFindAttribute(supported, "printer-mandatory-job-attributes", IPP_TAG_KEYWORD)) != NULL && ippValidateAttribute(attr))
  {
    char	prefix = '\"';		// Prefix for string

    cupsFilePuts(fp, "*cupsMandatory: \"");
    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      keyword = ippGetString(attr, i, NULL);

      if (strcmp(keyword, "attributes-charset") && strcmp(keyword, "attributes-natural-language") && strcmp(keyword, "printer-uri"))
      {
        cupsFilePrintf(fp, "%c%s", prefix, keyword);
        prefix = ',';
      }
    }
    cupsFilePuts(fp, "\"\n");
  }

  if ((attr = ippFindAttribute(supported, "printer-requested-job-attributes", IPP_TAG_KEYWORD)) != NULL && ippValidateAttribute(attr))
  {
    char	prefix = '\"';		// Prefix for string

    cupsFilePuts(fp, "*cupsRequested: \"");
    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      keyword = ippGetString(attr, i, NULL);

      if (strcmp(keyword, "attributes-charset") && strcmp(keyword, "attributes-natural-language") && strcmp(keyword, "printer-uri"))
      {
        cupsFilePrintf(fp, "%c%s", prefix, keyword);
        prefix = ',';
      }
    }
    cupsFilePuts(fp, "\"\n");
  }

 /*
  * Password/PIN printing...
  */

  if ((attr = ippFindAttribute(supported, "job-password-supported", IPP_TAG_INTEGER)) != NULL)
  {
    char	pattern[33];		/* Password pattern */
    int		maxlen = ippGetInteger(attr, 0);
					/* Maximum length */
    const char	*repertoire = ippGetString(ippFindAttribute(supported, "job-password-repertoire-configured", IPP_TAG_KEYWORD), 0, NULL);
					/* Type of password */

    if (maxlen > (int)(sizeof(pattern) - 1))
      maxlen = (int)sizeof(pattern) - 1;

    if (!repertoire || !strcmp(repertoire, "iana_us-ascii_digits"))
      memset(pattern, '1', (size_t)maxlen);
    else if (!strcmp(repertoire, "iana_us-ascii_letters"))
      memset(pattern, 'A', (size_t)maxlen);
    else if (!strcmp(repertoire, "iana_us-ascii_complex"))
      memset(pattern, 'C', (size_t)maxlen);
    else if (!strcmp(repertoire, "iana_us-ascii_any"))
      memset(pattern, '.', (size_t)maxlen);
    else if (!strcmp(repertoire, "iana_utf-8_digits"))
      memset(pattern, 'N', (size_t)maxlen);
    else if (!strcmp(repertoire, "iana_utf-8_letters"))
      memset(pattern, 'U', (size_t)maxlen);
    else
      memset(pattern, '*', (size_t)maxlen);

    pattern[maxlen] = '\0';

    cupsFilePrintf(fp, "*cupsJobPassword: \"%s\"\n", pattern);
  }

 /*
  * Filters...
  */

  if ((attr = ippFindAttribute(supported, "document-format-supported", IPP_TAG_MIMETYPE)) != NULL)
  {
    is_apple = ippContainsString(attr, "image/urf") && (ippFindAttribute(supported, "urf-supported", IPP_TAG_KEYWORD) != NULL);
    is_pdf   = ippContainsString(attr, "application/pdf");
    is_pwg   = ippContainsString(attr, "image/pwg-raster") && !is_apple &&
	       (ippFindAttribute(supported, "pwg-raster-document-resolution-supported", IPP_TAG_KEYWORD) != NULL) &&
	       (ippFindAttribute(supported, "pwg-raster-document-type-supported", IPP_TAG_KEYWORD) != NULL);

    if (ippContainsString(attr, "image/jpeg"))
      cupsFilePuts(fp, "*cupsFilter2: \"image/jpeg image/jpeg 0 -\"\n");
    if (ippContainsString(attr, "image/png"))
      cupsFilePuts(fp, "*cupsFilter2: \"image/png image/png 0 -\"\n");
    if (is_pdf)
    {
     /*
      * Don't locally filter PDF content when printing to a CUPS shared
      * printer, otherwise the options will be applied twice...
      */

      if (ippContainsString(attr, "application/vnd.cups-pdf"))
        cupsFilePuts(fp, "*cupsFilter2: \"application/pdf application/pdf 0 -\"\n");
      else
        cupsFilePuts(fp, "*cupsFilter2: \"application/vnd.cups-pdf application/pdf 10 -\"\n");
    }
    else
      cupsFilePuts(fp, "*cupsManualCopies: True\n");
    if (is_apple)
      cupsFilePuts(fp, "*cupsFilter2: \"image/urf image/urf 100 -\"\n");
    if (is_pwg)
      cupsFilePuts(fp, "*cupsFilter2: \"image/pwg-raster image/pwg-raster 100 -\"\n");
  }

  if (!is_apple && !is_pdf && !is_pwg)
    goto bad_ppd;

 /*
  * cupsUrfSupported
  */
  if ((attr = ippFindAttribute(supported, "urf-supported", IPP_TAG_KEYWORD)) != NULL)
  {
    cupsFilePuts(fp, "*cupsUrfSupported: \"");
    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      keyword = ippGetString(attr, i, NULL);
      cupsFilePrintf(fp, "%s%s", keyword, i != count - 1 ? "," : "");
    }
    cupsFilePuts(fp, "\"\n");
  }

 /*
  * PageSize/PageRegion/ImageableArea/PaperDimension
  */

  if ((attr = ippFindAttribute(supported, "media-bottom-margin-supported", IPP_TAG_INTEGER)) != NULL)
  {
    for (i = 1, bottom = ippGetInteger(attr, 0), count = ippGetCount(attr); i < count; i ++)
      if (ippGetInteger(attr, i) > bottom)
        bottom = ippGetInteger(attr, i);
  }
  else
    bottom = 1270;

  if ((attr = ippFindAttribute(supported, "media-left-margin-supported", IPP_TAG_INTEGER)) != NULL)
  {
    for (i = 1, left = ippGetInteger(attr, 0), count = ippGetCount(attr); i < count; i ++)
      if (ippGetInteger(attr, i) > left)
        left = ippGetInteger(attr, i);
  }
  else
    left = 635;

  if ((attr = ippFindAttribute(supported, "media-right-margin-supported", IPP_TAG_INTEGER)) != NULL)
  {
    for (i = 1, right = ippGetInteger(attr, 0), count = ippGetCount(attr); i < count; i ++)
      if (ippGetInteger(attr, i) > right)
        right = ippGetInteger(attr, i);
  }
  else
    right = 635;

  if ((attr = ippFindAttribute(supported, "media-top-margin-supported", IPP_TAG_INTEGER)) != NULL)
  {
    for (i = 1, top = ippGetInteger(attr, 0), count = ippGetCount(attr); i < count; i ++)
      if (ippGetInteger(attr, i) > top)
        top = ippGetInteger(attr, i);
  }
  else
    top = 1270;

  if ((defattr = ippFindAttribute(supported, "media-col-default", IPP_TAG_BEGIN_COLLECTION)) != NULL)
  {
    if ((attr = ippFindAttribute(ippGetCollection(defattr, 0), "media-size", IPP_TAG_BEGIN_COLLECTION)) != NULL)
    {
      media_size = ippGetCollection(attr, 0);
      x_dim      = ippFindAttribute(media_size, "x-dimension", IPP_TAG_INTEGER);
      y_dim      = ippFindAttribute(media_size, "y-dimension", IPP_TAG_INTEGER);

      if (x_dim && y_dim && (pwg = pwgMediaForSize(ippGetInteger(x_dim, 0), ippGetInteger(y_dim, 0))) != NULL)
	strlcpy(ppdname, pwg->ppd, sizeof(ppdname));
      else
	strlcpy(ppdname, "Unknown", sizeof(ppdname));
    }
    else
      strlcpy(ppdname, "Unknown", sizeof(ppdname));
  }
  else if ((pwg = pwgMediaForPWG(ippGetString(ippFindAttribute(supported, "media-default", IPP_TAG_ZERO), 0, NULL))) != NULL)
    strlcpy(ppdname, pwg->ppd, sizeof(ppdname));
  else
    strlcpy(ppdname, "Unknown", sizeof(ppdname));

  sizes = cupsArrayNew3((cups_array_func_t)pwg_compare_sizes, NULL, NULL, 0, (cups_acopy_func_t)pwg_copy_size, (cups_afree_func_t)free);

  if ((attr = ippFindAttribute(supported, "media-col-database", IPP_TAG_BEGIN_COLLECTION)) != NULL)
  {
    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      cups_size_t	temp;		/* Current size */
      ipp_attribute_t	*margin;	/* media-xxx-margin attribute */

      media_col   = ippGetCollection(attr, i);
      media_size  = ippGetCollection(ippFindAttribute(media_col, "media-size", IPP_TAG_BEGIN_COLLECTION), 0);
      x_dim       = ippFindAttribute(media_size, "x-dimension", IPP_TAG_ZERO);
      y_dim       = ippFindAttribute(media_size, "y-dimension", IPP_TAG_ZERO);
      pwg         = pwgMediaForSize(ippGetInteger(x_dim, 0), ippGetInteger(y_dim, 0));

      if (pwg)
      {
	temp.width  = pwg->width;
	temp.length = pwg->length;

	if ((margin = ippFindAttribute(media_col, "media-bottom-margin", IPP_TAG_INTEGER)) != NULL)
	  temp.bottom = ippGetInteger(margin, 0);
	else
	  temp.bottom = bottom;

	if ((margin = ippFindAttribute(media_col, "media-left-margin", IPP_TAG_INTEGER)) != NULL)
	  temp.left = ippGetInteger(margin, 0);
	else
	  temp.left = left;

	if ((margin = ippFindAttribute(media_col, "media-right-margin", IPP_TAG_INTEGER)) != NULL)
	  temp.right = ippGetInteger(margin, 0);
	else
	  temp.right = right;

	if ((margin = ippFindAttribute(media_col, "media-top-margin", IPP_TAG_INTEGER)) != NULL)
	  temp.top = ippGetInteger(margin, 0);
	else
	  temp.top = top;

	if (temp.bottom == 0 && temp.left == 0 && temp.right == 0 && temp.top == 0)
	  snprintf(temp.media, sizeof(temp.media), "%s.Borderless", pwg->ppd);
	else
	  strlcpy(temp.media, pwg->ppd, sizeof(temp.media));

	if (!cupsArrayFind(sizes, &temp))
	  cupsArrayAdd(sizes, &temp);
      }
      else if (ippGetValueTag(x_dim) == IPP_TAG_RANGE || ippGetValueTag(y_dim) == IPP_TAG_RANGE)
      {
       /*
	* Custom size - record the min/max values...
	*/

	int lower, upper;		/* Range values */

	if (ippGetValueTag(x_dim) == IPP_TAG_RANGE)
	  lower = ippGetRange(x_dim, 0, &upper);
	else
	  lower = upper = ippGetInteger(x_dim, 0);

	if (lower < min_width)
	  min_width = lower;
	if (upper > max_width)
	  max_width = upper;

	if (ippGetValueTag(y_dim) == IPP_TAG_RANGE)
	  lower = ippGetRange(y_dim, 0, &upper);
	else
	  lower = upper = ippGetInteger(y_dim, 0);

	if (lower < min_length)
	  min_length = lower;
	if (upper > max_length)
	  max_length = upper;
      }
    }

    if ((max_width == 0 || max_length == 0) && (attr = ippFindAttribute(supported, "media-size-supported", IPP_TAG_BEGIN_COLLECTION)) != NULL)
    {
     /*
      * Some printers don't list custom size support in media-col-database...
      */

      for (i = 0, count = ippGetCount(attr); i < count; i ++)
      {
	media_size  = ippGetCollection(attr, i);
	x_dim       = ippFindAttribute(media_size, "x-dimension", IPP_TAG_ZERO);
	y_dim       = ippFindAttribute(media_size, "y-dimension", IPP_TAG_ZERO);

	if (ippGetValueTag(x_dim) == IPP_TAG_RANGE || ippGetValueTag(y_dim) == IPP_TAG_RANGE)
	{
	 /*
	  * Custom size - record the min/max values...
	  */

	  int lower, upper;		/* Range values */

	  if (ippGetValueTag(x_dim) == IPP_TAG_RANGE)
	    lower = ippGetRange(x_dim, 0, &upper);
	  else
	    lower = upper = ippGetInteger(x_dim, 0);

	  if (lower < min_width)
	    min_width = lower;
	  if (upper > max_width)
	    max_width = upper;

	  if (ippGetValueTag(y_dim) == IPP_TAG_RANGE)
	    lower = ippGetRange(y_dim, 0, &upper);
	  else
	    lower = upper = ippGetInteger(y_dim, 0);

	  if (lower < min_length)
	    min_length = lower;
	  if (upper > max_length)
	    max_length = upper;
	}
      }
    }
  }
  else if ((attr = ippFindAttribute(supported, "media-size-supported", IPP_TAG_BEGIN_COLLECTION)) != NULL)
  {
    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      cups_size_t	temp;		/* Current size */

      media_size  = ippGetCollection(attr, i);
      x_dim       = ippFindAttribute(media_size, "x-dimension", IPP_TAG_ZERO);
      y_dim       = ippFindAttribute(media_size, "y-dimension", IPP_TAG_ZERO);
      pwg         = pwgMediaForSize(ippGetInteger(x_dim, 0), ippGetInteger(y_dim, 0));

      if (pwg)
      {
	temp.width  = pwg->width;
	temp.length = pwg->length;
	temp.bottom = bottom;
	temp.left   = left;
	temp.right  = right;
	temp.top    = top;

	if (temp.bottom == 0 && temp.left == 0 && temp.right == 0 && temp.top == 0)
	  snprintf(temp.media, sizeof(temp.media), "%s.Borderless", pwg->ppd);
	else
	  strlcpy(temp.media, pwg->ppd, sizeof(temp.media));

	if (!cupsArrayFind(sizes, &temp))
	  cupsArrayAdd(sizes, &temp);
      }
      else if (ippGetValueTag(x_dim) == IPP_TAG_RANGE || ippGetValueTag(y_dim) == IPP_TAG_RANGE)
      {
       /*
	* Custom size - record the min/max values...
	*/

	int lower, upper;		/* Range values */

	if (ippGetValueTag(x_dim) == IPP_TAG_RANGE)
	  lower = ippGetRange(x_dim, 0, &upper);
	else
	  lower = upper = ippGetInteger(x_dim, 0);

	if (lower < min_width)
	  min_width = lower;
	if (upper > max_width)
	  max_width = upper;

	if (ippGetValueTag(y_dim) == IPP_TAG_RANGE)
	  lower = ippGetRange(y_dim, 0, &upper);
	else
	  lower = upper = ippGetInteger(y_dim, 0);

	if (lower < min_length)
	  min_length = lower;
	if (upper > max_length)
	  max_length = upper;
      }
    }
  }
  else if ((attr = ippFindAttribute(supported, "media-supported", IPP_TAG_ZERO)) != NULL)
  {
    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      const char	*pwg_size = ippGetString(attr, i, NULL);
    					/* PWG size name */
      cups_size_t	temp;		/* Current size */

      if ((pwg = pwgMediaForPWG(pwg_size)) != NULL)
      {
        if (strstr(pwg_size, "_max_") || strstr(pwg_size, "_max."))
        {
          if (pwg->width > max_width)
            max_width = pwg->width;
          if (pwg->length > max_length)
            max_length = pwg->length;
        }
        else if (strstr(pwg_size, "_min_") || strstr(pwg_size, "_min."))
        {
          if (pwg->width < min_width)
            min_width = pwg->width;
          if (pwg->length < min_length)
            min_length = pwg->length;
        }
        else
        {
	  temp.width  = pwg->width;
	  temp.length = pwg->length;
	  temp.bottom = bottom;
	  temp.left   = left;
	  temp.right  = right;
	  temp.top    = top;

	  if (temp.bottom == 0 && temp.left == 0 && temp.right == 0 && temp.top == 0)
	    snprintf(temp.media, sizeof(temp.media), "%s.Borderless", pwg->ppd);
	  else
	    strlcpy(temp.media, pwg->ppd, sizeof(temp.media));

	  if (!cupsArrayFind(sizes, &temp))
	    cupsArrayAdd(sizes, &temp);
	}
      }
    }
  }

  if (cupsArrayCount(sizes) > 0)
  {
   /*
    * List all of the standard sizes...
    */

    char	tleft[256],		/* Left string */
		tbottom[256],		/* Bottom string */
		tright[256],		/* Right string */
		ttop[256],		/* Top string */
		twidth[256],		/* Width string */
		tlength[256];		/* Length string */

    cupsFilePrintf(fp, "*OpenUI *PageSize: PickOne\n"
		       "*OrderDependency: 10 AnySetup *PageSize\n"
                       "*DefaultPageSize: %s\n", ppdname);
    for (size = (cups_size_t *)cupsArrayFirst(sizes); size; size = (cups_size_t *)cupsArrayNext(sizes))
    {
      _cupsStrFormatd(twidth, twidth + sizeof(twidth), size->width * 72.0 / 2540.0, loc);
      _cupsStrFormatd(tlength, tlength + sizeof(tlength), size->length * 72.0 / 2540.0, loc);

      cupsFilePrintf(fp, "*PageSize %s: \"<</PageSize[%s %s]>>setpagedevice\"\n", size->media, twidth, tlength);
    }
    cupsFilePuts(fp, "*CloseUI: *PageSize\n");

    cupsFilePrintf(fp, "*OpenUI *PageRegion: PickOne\n"
                       "*OrderDependency: 10 AnySetup *PageRegion\n"
                       "*DefaultPageRegion: %s\n", ppdname);
    for (size = (cups_size_t *)cupsArrayFirst(sizes); size; size = (cups_size_t *)cupsArrayNext(sizes))
    {
      _cupsStrFormatd(twidth, twidth + sizeof(twidth), size->width * 72.0 / 2540.0, loc);
      _cupsStrFormatd(tlength, tlength + sizeof(tlength), size->length * 72.0 / 2540.0, loc);

      cupsFilePrintf(fp, "*PageRegion %s: \"<</PageSize[%s %s]>>setpagedevice\"\n", size->media, twidth, tlength);
    }
    cupsFilePuts(fp, "*CloseUI: *PageRegion\n");

    cupsFilePrintf(fp, "*DefaultImageableArea: %s\n"
		       "*DefaultPaperDimension: %s\n", ppdname, ppdname);

    for (size = (cups_size_t *)cupsArrayFirst(sizes); size; size = (cups_size_t *)cupsArrayNext(sizes))
    {
      _cupsStrFormatd(tleft, tleft + sizeof(tleft), size->left * 72.0 / 2540.0, loc);
      _cupsStrFormatd(tbottom, tbottom + sizeof(tbottom), size->bottom * 72.0 / 2540.0, loc);
      _cupsStrFormatd(tright, tright + sizeof(tright), (size->width - size->right) * 72.0 / 2540.0, loc);
      _cupsStrFormatd(ttop, ttop + sizeof(ttop), (size->length - size->top) * 72.0 / 2540.0, loc);
      _cupsStrFormatd(twidth, twidth + sizeof(twidth), size->width * 72.0 / 2540.0, loc);
      _cupsStrFormatd(tlength, tlength + sizeof(tlength), size->length * 72.0 / 2540.0, loc);

      cupsFilePrintf(fp, "*ImageableArea %s: \"%s %s %s %s\"\n", size->media, tleft, tbottom, tright, ttop);
      cupsFilePrintf(fp, "*PaperDimension %s: \"%s %s\"\n", size->media, twidth, tlength);
    }

    cupsArrayDelete(sizes);

   /*
    * Custom size support...
    */

    if (max_width > 0 && min_width < INT_MAX && max_length > 0 && min_length < INT_MAX)
    {
      char	tmax[256], tmin[256];	/* Min/max values */

      _cupsStrFormatd(tleft, tleft + sizeof(tleft), left * 72.0 / 2540.0, loc);
      _cupsStrFormatd(tbottom, tbottom + sizeof(tbottom), bottom * 72.0 / 2540.0, loc);
      _cupsStrFormatd(tright, tright + sizeof(tright), right * 72.0 / 2540.0, loc);
      _cupsStrFormatd(ttop, ttop + sizeof(ttop), top * 72.0 / 2540.0, loc);

      cupsFilePrintf(fp, "*HWMargins: \"%s %s %s %s\"\n", tleft, tbottom, tright, ttop);

      _cupsStrFormatd(tmax, tmax + sizeof(tmax), max_width * 72.0 / 2540.0, loc);
      _cupsStrFormatd(tmin, tmin + sizeof(tmin), min_width * 72.0 / 2540.0, loc);
      cupsFilePrintf(fp, "*ParamCustomPageSize Width: 1 points %s %s\n", tmin, tmax);

      _cupsStrFormatd(tmax, tmax + sizeof(tmax), max_length * 72.0 / 2540.0, loc);
      _cupsStrFormatd(tmin, tmin + sizeof(tmin), min_length * 72.0 / 2540.0, loc);
      cupsFilePrintf(fp, "*ParamCustomPageSize Height: 2 points %s %s\n", tmin, tmax);

      cupsFilePuts(fp, "*ParamCustomPageSize WidthOffset: 3 points 0 0\n");
      cupsFilePuts(fp, "*ParamCustomPageSize HeightOffset: 4 points 0 0\n");
      cupsFilePuts(fp, "*ParamCustomPageSize Orientation: 5 int 0 3\n");
      cupsFilePuts(fp, "*CustomPageSize True: \"pop pop pop <</PageSize[5 -2 roll]/ImagingBBox null>>setpagedevice\"\n");
    }
  }
  else
  {
    cupsArrayDelete(sizes);
    goto bad_ppd;
  }

 /*
  * InputSlot...
  */

  if ((attr = ippFindAttribute(ippGetCollection(defattr, 0), "media-source", IPP_TAG_ZERO)) != NULL)
    pwg_ppdize_name(ippGetString(attr, 0, NULL), ppdname, sizeof(ppdname));
  else
    ppdname[0] = '\0';

  if ((attr = ippFindAttribute(supported, "media-source-supported", IPP_TAG_ZERO)) != NULL && (count = ippGetCount(attr)) > 1)
  {
    int have_default = ppdname[0] != '\0';
					/* Do we have a default InputSlot? */
    static const char * const sources[] =
    {					/* Standard "media-source" strings */
      "auto",
      "main",
      "alternate",
      "large-capacity",
      "manual",
      "envelope",
      "disc",
      "photo",
      "hagaki",
      "main-roll",
      "alternate-roll",
      "top",
      "middle",
      "bottom",
      "side",
      "left",
      "right",
      "center",
      "rear",
      "by-pass-tray",
      "tray-1",
      "tray-2",
      "tray-3",
      "tray-4",
      "tray-5",
      "tray-6",
      "tray-7",
      "tray-8",
      "tray-9",
      "tray-10",
      "tray-11",
      "tray-12",
      "tray-13",
      "tray-14",
      "tray-15",
      "tray-16",
      "tray-17",
      "tray-18",
      "tray-19",
      "tray-20",
      "roll-1",
      "roll-2",
      "roll-3",
      "roll-4",
      "roll-5",
      "roll-6",
      "roll-7",
      "roll-8",
      "roll-9",
      "roll-10"
    };

    cupsFilePuts(fp, "*OpenUI *InputSlot: PickOne\n"
                     "*OrderDependency: 10 AnySetup *InputSlot\n");
    if (have_default)
      cupsFilePrintf(fp, "*DefaultInputSlot: %s\n", ppdname);

    for (i = 0; i < count; i ++)
    {
      keyword = ippGetString(attr, i, NULL);

      pwg_ppdize_name(keyword, ppdname, sizeof(ppdname));

      if (i == 0 && !have_default)
	cupsFilePrintf(fp, "*DefaultInputSlot: %s\n", ppdname);

      for (j = 0; j < (int)(sizeof(sources) / sizeof(sources[0])); j ++)
      {
        if (!strcmp(sources[j], keyword))
	{
	  snprintf(msgid, sizeof(msgid), "media-source.%s", keyword);

	  cupsFilePrintf(fp, "*InputSlot %s: \"<</MediaPosition %d>>setpagedevice\"\n", ppdname, j);
	  ppd_put_string(fp, lang, strings, "InputSlot", ppdname, msgid);
	  break;
	}
      }
    }
    cupsFilePuts(fp, "*CloseUI: *InputSlot\n");
  }

 /*
  * MediaType...
  */

  if ((attr = ippFindAttribute(ippGetCollection(defattr, 0), "media-type", IPP_TAG_ZERO)) != NULL)
    pwg_ppdize_name(ippGetString(attr, 0, NULL), ppdname, sizeof(ppdname));
  else
    strlcpy(ppdname, "Unknown", sizeof(ppdname));

  if ((attr = ippFindAttribute(supported, "media-type-supported", IPP_TAG_ZERO)) != NULL && (count = ippGetCount(attr)) > 1)
  {
    cupsFilePrintf(fp, "*OpenUI *MediaType: PickOne\n"
                       "*OrderDependency: 10 AnySetup *MediaType\n"
                       "*DefaultMediaType: %s\n", ppdname);
    for (i = 0; i < count; i ++)
    {
      keyword = ippGetString(attr, i, NULL);

      pwg_ppdize_name(keyword, ppdname, sizeof(ppdname));

      snprintf(msgid, sizeof(msgid), "media-type.%s", keyword);

      cupsFilePrintf(fp, "*MediaType %s: \"<</MediaType(%s)>>setpagedevice\"\n", ppdname, ppdname);
      ppd_put_string(fp, lang, strings, "MediaType", ppdname, msgid);
    }
    cupsFilePuts(fp, "*CloseUI: *MediaType\n");
  }

 /*
  * cupsPrintQuality and DefaultResolution...
  */

  quality = ippFindAttribute(supported, "print-quality-supported", IPP_TAG_ENUM);

  if ((attr = ippFindAttribute(supported, "urf-supported", IPP_TAG_KEYWORD)) != NULL)
  {
    int lowdpi = 0, hidpi = 0;    /* Lower and higher resolution */

    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      const char *rs = ippGetString(attr, i, NULL);
          /* RS value */

      if (_cups_strncasecmp(rs, "RS", 2))
        continue;

      lowdpi = atoi(rs + 2);
      if ((rs = strrchr(rs, '-')) != NULL)
        hidpi = atoi(rs + 1);
      else
        hidpi = lowdpi;
      break;
    }

    if (lowdpi == 0)
    {
     /*
      * Invalid "urf-supported" value...
      */

      goto bad_ppd;
    }
    else
    {
     /*
      * Generate print qualities based on low and high DPIs...
      */

      cupsFilePrintf(fp, "*DefaultResolution: %ddpi\n", lowdpi);

      cupsFilePrintf(fp, "*OpenUI *cupsPrintQuality: PickOne\n"
			 "*OrderDependency: 10 AnySetup *cupsPrintQuality\n"
			 "*%s.Translation cupsPrintQuality/%s: \"\"\n"
			 "*DefaultcupsPrintQuality: Normal\n", lang->language, _cupsLangString(lang, _("Print Quality")));
      if ((lowdpi & 1) == 0)
      {
	cupsFilePrintf(fp, "*cupsPrintQuality Draft: \"<</HWResolution[%d %d]>>setpagedevice\"\n*%s.cupsPrintQuality Draft/%s: \"\"\n", lowdpi, lowdpi / 2, lang->language, _cupsLangString(lang, _("Draft")));
	have_qdraft = 1;
      }
      else if (ippContainsInteger(quality, IPP_QUALITY_DRAFT))
      {
	cupsFilePrintf(fp, "*cupsPrintQuality Draft: \"<</HWResolution[%d %d]>>setpagedevice\"\n*%s.cupsPrintQuality Draft/%s: \"\"\n", lowdpi, lowdpi, lang->language, _cupsLangString(lang, _("Draft")));
	have_qdraft = 1;
      }

      cupsFilePrintf(fp, "*cupsPrintQuality Normal: \"<</HWResolution[%d %d]>>setpagedevice\"\n*%s.cupsPrintQuality Normal/%s: \"\"\n", lowdpi, lowdpi, lang->language, _cupsLangString(lang, _("Normal")));

      if (hidpi > lowdpi || ippContainsInteger(quality, IPP_QUALITY_HIGH))
      {
	cupsFilePrintf(fp, "*cupsPrintQuality High: \"<</HWResolution[%d %d]>>setpagedevice\"\n*%s.cupsPrintQuality High/%s: \"\"\n", hidpi, hidpi, lang->language, _cupsLangString(lang, _("High")));
	have_qhigh = 1;
      }

      cupsFilePuts(fp, "*CloseUI: *cupsPrintQuality\n");
    }
  }
  else if ((attr = ippFindAttribute(supported, "pwg-raster-document-resolution-supported", IPP_TAG_RESOLUTION)) != NULL)
  {
   /*
    * Make a sorted list of resolutions.
    */

    count = ippGetCount(attr);
    if (count > (int)(sizeof(resolutions) / sizeof(resolutions[0])))
      count = (int)(sizeof(resolutions) / sizeof(resolutions[0]));

    resolutions[0] = 0; /* Not in loop to silence Clang static analyzer... */
    for (i = 1; i < count; i ++)
      resolutions[i] = i;

    for (i = 0; i < (count - 1); i ++)
    {
      for (j = i + 1; j < count; j ++)
      {
        int       ix, iy,               /* First X and Y resolution */
                  jx, jy,               /* Second X and Y resolution */
                  temp;                 /* Swap variable */
        ipp_res_t units;                /* Resolution units */

        ix = ippGetResolution(attr, resolutions[i], &iy, &units);
        jx = ippGetResolution(attr, resolutions[j], &jy, &units);

        if (ix > jx || (ix == jx && iy > jy))
        {
         /*
          * Swap these two resolutions...
          */

          temp           = resolutions[i];
          resolutions[i] = resolutions[j];
          resolutions[j] = temp;
        }
      }
    }

   /*
    * Generate print quality options...
    */

    pwg_ppdize_resolution(attr, resolutions[count / 2], &xres, &yres, ppdname, sizeof(ppdname));
    cupsFilePrintf(fp, "*DefaultResolution: %s\n", ppdname);

    cupsFilePrintf(fp, "*OpenUI *cupsPrintQuality: PickOne\n"
		       "*OrderDependency: 10 AnySetup *cupsPrintQuality\n"
		       "*%s.Translation cupsPrintQuality/%s: \"\"\n"
		       "*DefaultcupsPrintQuality: Normal\n", lang->language, _cupsLangString(lang, _("Print Quality")));
    if (count > 2 || ippContainsInteger(quality, IPP_QUALITY_DRAFT))
    {
      pwg_ppdize_resolution(attr, resolutions[0], &xres, &yres, NULL, 0);
      cupsFilePrintf(fp, "*cupsPrintQuality Draft: \"<</HWResolution[%d %d]>>setpagedevice\"\n", xres, yres);
      cupsFilePrintf(fp, "*%s.cupsPrintQuality Draft/%s: \"\"\n", lang->language, _cupsLangString(lang, _("Draft")));
      have_qdraft = 1;
    }

    pwg_ppdize_resolution(attr, resolutions[count / 2], &xres, &yres, NULL, 0);
    cupsFilePrintf(fp, "*cupsPrintQuality Normal: \"<</HWResolution[%d %d]>>setpagedevice\"\n", xres, yres);
    cupsFilePrintf(fp, "*%s.cupsPrintQuality Normal/%s: \"\"\n", lang->language, _cupsLangString(lang, _("Normal")));

    if (count > 1 || ippContainsInteger(quality, IPP_QUALITY_HIGH))
    {
      pwg_ppdize_resolution(attr, resolutions[count - 1], &xres, &yres, NULL, 0);
      cupsFilePrintf(fp, "*cupsPrintQuality High: \"<</HWResolution[%d %d]>>setpagedevice\"\n", xres, yres);
      cupsFilePrintf(fp, "*%s.cupsPrintQuality High/%s: \"\"\n", lang->language, _cupsLangString(lang, _("High")));
      have_qhigh = 1;
    }

    cupsFilePuts(fp, "*CloseUI: *cupsPrintQuality\n");
  }
  else if (is_apple || is_pwg)
    goto bad_ppd;
  else
  {
    if ((attr = ippFindAttribute(supported, "printer-resolution-default", IPP_TAG_RESOLUTION)) != NULL)
    {
      pwg_ppdize_resolution(attr, 0, &xres, &yres, ppdname, sizeof(ppdname));
    }
    else
    {
      xres = yres = 300;
      strlcpy(ppdname, "300dpi", sizeof(ppdname));
    }

    cupsFilePrintf(fp, "*DefaultResolution: %s\n", ppdname);

    cupsFilePrintf(fp, "*OpenUI *cupsPrintQuality: PickOne\n"
                       "*OrderDependency: 10 AnySetup *cupsPrintQuality\n"
                       "*%s.Translation cupsPrintQuality/%s: \"\"\n"
                       "*DefaultcupsPrintQuality: Normal\n", lang->language, _cupsLangString(lang, _("Print Quality")));
    if (ippContainsInteger(quality, IPP_QUALITY_DRAFT))
    {
      cupsFilePrintf(fp, "*cupsPrintQuality Draft: \"<</HWResolution[%d %d]>>setpagedevice\"\n*%s.cupsPrintQuality Draft/%s: \"\"\n", xres, yres, lang->language, _cupsLangString(lang, _("Draft")));
      have_qdraft = 1;
    }

    cupsFilePrintf(fp, "*cupsPrintQuality Normal: \"<</HWResolution[%d %d]>>setpagedevice\"\n*%s.cupsPrintQuality Normal/%s: \"\"\n", xres, yres, lang->language, _cupsLangString(lang, _("Normal")));

    if (ippContainsInteger(quality, IPP_QUALITY_HIGH))
    {
      cupsFilePrintf(fp, "*cupsPrintQuality High: \"<</HWResolution[%d %d]>>setpagedevice\"\n*%s.cupsPrintQuality High/%s: \"\"\n", xres, yres, lang->language, _cupsLangString(lang, _("High")));
      have_qhigh = 1;
    }
    cupsFilePuts(fp, "*CloseUI: *cupsPrintQuality\n");
  }

 /*
  * ColorModel...
  */

  if ((defattr = ippFindAttribute(supported, "print-color-mode-default", IPP_TAG_KEYWORD)) == NULL)
    defattr = ippFindAttribute(supported, "output-mode-default", IPP_TAG_KEYWORD);

  if ((attr = ippFindAttribute(supported, "urf-supported", IPP_TAG_KEYWORD)) == NULL)
    if ((attr = ippFindAttribute(supported, "pwg-raster-document-type-supported", IPP_TAG_KEYWORD)) == NULL)
      if ((attr = ippFindAttribute(supported, "print-color-mode-supported", IPP_TAG_KEYWORD)) == NULL)
        attr = ippFindAttribute(supported, "output-mode-supported", IPP_TAG_KEYWORD);

  if (attr)
  {
    int wrote_color = 0;
    const char *default_color = NULL;	/* Default */

    if ((keyword = ippGetString(defattr, 0, NULL)) != NULL &&
	strcmp(keyword, "auto"))
    {
      if (!strcmp(keyword, "bi-level"))
        default_color = "FastGray";
      else if (!strcmp(keyword, "monochrome") || !strcmp(keyword, "auto-monochrome"))
        default_color = "Gray";
      else
        default_color = "RGB";
    }

    cupsFilePrintf(fp, "*%% ColorModel from %s\n", ippGetName(attr));

    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      keyword = ippGetString(attr, i, NULL);

#define PRINTF_COLORMODEL if (!wrote_color) { cupsFilePrintf(fp, "*OpenUI *ColorModel: PickOne\n*OrderDependency: 10 AnySetup *ColorModel\n*%s.Translation ColorModel/%s: \"\"\n", lang->language, _cupsLangString(lang, _("Color Mode"))); wrote_color = 1; }
#define PRINTF_COLOROPTION(name,text,cspace,bpp) { cupsFilePrintf(fp, "*ColorModel %s: \"<</cupsColorSpace %d/cupsBitsPerColor %d/cupsColorOrder 0/cupsCompression 0>>setpagedevice\"\n", name, cspace, bpp); cupsFilePrintf(fp, "*%s.ColorModel %s/%s: \"\"\n", lang->language, name, _cupsLangString(lang, text)); }

      if (!strcasecmp(keyword, "black_1") || !strcmp(keyword, "bi-level") || !strcmp(keyword, "process-bi-level"))
      {
	PRINTF_COLORMODEL

	PRINTF_COLOROPTION("FastGray", _("Fast Grayscale"), CUPS_CSPACE_K, 1)

	if (!default_color)
	  default_color = "FastGray";
      }
      else if (!strcasecmp(keyword, "sgray_8") || !strcmp(keyword, "W8") || !strcmp(keyword, "monochrome") || !strcmp(keyword, "process-monochrome"))
      {
	PRINTF_COLORMODEL

	PRINTF_COLOROPTION("Gray", _("Grayscale"), CUPS_CSPACE_SW, 8)

	if (!default_color || (!defattr && !strcmp(default_color, "FastGray")))
	  default_color = "Gray";
      }
      else if (!strcasecmp(keyword, "sgray_16") || !strcmp(keyword, "W8-16"))
      {
	PRINTF_COLORMODEL

	if (!strcmp(keyword, "W8-16"))
	{
	  PRINTF_COLOROPTION("Gray", _("Grayscale"), CUPS_CSPACE_SW, 8)

	  if (!default_color || (!defattr && !strcmp(default_color, "FastGray")))
	    default_color = "Gray";
	}

	PRINTF_COLOROPTION("Gray16", _("Deep Gray"), CUPS_CSPACE_SW, 16)
      }
      else if (!strcasecmp(keyword, "srgb_8") || !strncmp(keyword, "SRGB24", 6) || !strcmp(keyword, "color"))
      {
	PRINTF_COLORMODEL

	PRINTF_COLOROPTION("RGB", _("Color"), CUPS_CSPACE_SRGB, 8)

        if (!default_color)
	  default_color = "RGB";

        // Apparently some printers only advertise color support, so make sure
        // we also do grayscale for these printers...
	if (!ippContainsString(attr, "sgray_8") && !ippContainsString(attr, "black_1") && !ippContainsString(attr, "black_8") && !ippContainsString(attr, "W8") && !ippContainsString(attr, "W8-16"))
	  PRINTF_COLOROPTION("Gray", _("GrayScale"), CUPS_CSPACE_SW, 8)
      }
      else if (!strcasecmp(keyword, "adobe-rgb_16") || !strcmp(keyword, "ADOBERGB48") || !strcmp(keyword, "ADOBERGB24-48"))
      {
	PRINTF_COLORMODEL

	PRINTF_COLOROPTION("AdobeRGB", _("Deep Color"), CUPS_CSPACE_ADOBERGB, 16)

	if (!default_color)
	  default_color = "AdobeRGB";
      }
      else if ((!strcasecmp(keyword, "adobe-rgb_8") && !ippContainsString(attr, "adobe-rgb_16")) || !strcmp(keyword, "ADOBERGB24"))
      {
	PRINTF_COLORMODEL

	PRINTF_COLOROPTION("AdobeRGB", _("Deep Color"), CUPS_CSPACE_ADOBERGB, 8)

	if (!default_color)
	  default_color = "AdobeRGB";
      }
      else if ((!strcasecmp(keyword, "black_8") && !ippContainsString(attr, "black_16")) || !strcmp(keyword, "DEVW8"))
      {
	PRINTF_COLORMODEL

	PRINTF_COLOROPTION("DeviceGray", _("Device Gray"), CUPS_CSPACE_W, 8)
      }
      else if (!strcasecmp(keyword, "black_16") || !strcmp(keyword, "DEVW16") || !strcmp(keyword, "DEVW8-16"))
      {
	PRINTF_COLORMODEL

	PRINTF_COLOROPTION("DeviceGray", _("Device Gray"), CUPS_CSPACE_W, 16)
      }
      else if ((!strcasecmp(keyword, "cmyk_8") && !ippContainsString(attr, "cmyk_16")) || !strcmp(keyword, "DEVCMYK32"))
      {
	PRINTF_COLORMODEL

	PRINTF_COLOROPTION("CMYK", _("Device CMYK"), CUPS_CSPACE_CMYK, 8)
      }
      else if (!strcasecmp(keyword, "cmyk_16") || !strcmp(keyword, "DEVCMYK32-64") || !strcmp(keyword, "DEVCMYK64"))
      {
	PRINTF_COLORMODEL

	PRINTF_COLOROPTION("CMYK", _("Device CMYK"), CUPS_CSPACE_CMYK, 16)
      }
      else if ((!strcasecmp(keyword, "rgb_8") && ippContainsString(attr, "rgb_16")) || !strcmp(keyword, "DEVRGB24"))
      {
	PRINTF_COLORMODEL

	PRINTF_COLOROPTION("DeviceRGB", _("Device RGB"), CUPS_CSPACE_RGB, 8)
      }
      else if (!strcasecmp(keyword, "rgb_16") || !strcmp(keyword, "DEVRGB24-48") || !strcmp(keyword, "DEVRGB48"))
      {
	PRINTF_COLORMODEL

	PRINTF_COLOROPTION("DeviceRGB", _("Device RGB"), CUPS_CSPACE_RGB, 16)
      }
    }

    if (default_color)
      cupsFilePrintf(fp, "*DefaultColorModel: %s\n", default_color);
    if (wrote_color)
      cupsFilePuts(fp, "*CloseUI: *ColorModel\n");

    if (default_color)
    {
      // Standard presets for color mode and quality...
      if (have_qdraft)
	cupsFilePuts(fp,
		     "*APPrinterPreset Gray_with_Paper_Auto-Detect_-_Draft/Draft B&W: \"\n"
		     "  *cupsPrintQuality Draft *ColorModel Gray\n"
		     "  com.apple.print.preset.graphicsType General\n"
		     "  com.apple.print.preset.quality low\n"
		     "  com.apple.print.preset.media-front-coating autodetect\n"
		     "  com.apple.print.preset.output-mode monochrome\"\n"
		     "*End\n");
      cupsFilePuts(fp,
                   "*APPrinterPreset Gray_with_Paper_Auto-Detect/Black and White: \"\n"
		   "  *cupsPrintQuality Normal *ColorModel Gray\n"
		   "  com.apple.print.preset.graphicsType General\n"
		   "  com.apple.print.preset.quality mid\n"
		   "  com.apple.print.preset.media-front-coating autodetect\n"
		   "  com.apple.print.preset.output-mode monochrome\"\n"
		   "*End\n");
      if (strcmp(default_color, "Gray"))
	cupsFilePuts(fp,
		     "*APPrinterPreset Color_with_Paper_Auto-Detect/Color: \"\n"
		     "  *cupsPrintQuality Normal *ColorModel RGB\n"
		     "  com.apple.print.preset.graphicsType General\n"
		     "  com.apple.print.preset.quality mid\n"
		     "  com.apple.print.preset.media-front-coating autodetect\n"
		     "  com.apple.print.preset.output-mode color\"\n"
		     "*End\n");
      if (!strcmp(default_color, "AdobeRGB") || have_qhigh)
	cupsFilePrintf(fp,
		       "*APPrinterPreset Photo_with_Paper_Auto-Detect/Photo: \"\n"
		       "  *cupsPrintQuality %s *ColorModel %s\n"
		       "  com.apple.print.preset.graphicsType Photo\n"
		       "  com.apple.print.preset.quality %s\n"
		       "  com.apple.print.preset.media-front-coating autodetect\n"
		       "  com.apple.print.preset.output-mode color\"\n"
		       "*End\n", have_qhigh ? "High" : "Normal", default_color, have_qhigh ? "high" : "mid");
    }
  }

 /*
  * Duplex...
  */

  if ((attr = ippFindAttribute(supported, "sides-supported", IPP_TAG_KEYWORD)) != NULL && ippContainsString(attr, "two-sided-long-edge"))
  {
    cupsFilePrintf(fp, "*OpenUI *Duplex: PickOne\n"
		       "*OrderDependency: 10 AnySetup *Duplex\n"
		       "*%s.Translation Duplex/%s: \"\"\n"
		       "*DefaultDuplex: None\n"
		       "*Duplex None: \"<</Duplex false>>setpagedevice\"\n"
		       "*%s.Duplex None/%s: \"\"\n"
		       "*Duplex DuplexNoTumble: \"<</Duplex true/Tumble false>>setpagedevice\"\n"
		       "*%s.Duplex DuplexNoTumble/%s: \"\"\n"
		       "*Duplex DuplexTumble: \"<</Duplex true/Tumble true>>setpagedevice\"\n"
		       "*%s.Duplex DuplexTumble/%s: \"\"\n"
		       "*CloseUI: *Duplex\n", lang->language, _cupsLangString(lang, _("2-Sided Printing")), lang->language, _cupsLangString(lang, _("Off (1-Sided)")), lang->language, _cupsLangString(lang, _("Long-Edge (Portrait)")), lang->language, _cupsLangString(lang, _("Short-Edge (Landscape)")));

    if ((attr = ippFindAttribute(supported, "urf-supported", IPP_TAG_KEYWORD)) != NULL)
    {
      for (i = 0, count = ippGetCount(attr); i < count; i ++)
      {
        const char *dm = ippGetString(attr, i, NULL);
                                        /* DM value */

        if (!_cups_strcasecmp(dm, "DM1"))
        {
          cupsFilePuts(fp, "*cupsBackSide: Normal\n");
          break;
        }
        else if (!_cups_strcasecmp(dm, "DM2"))
        {
          cupsFilePuts(fp, "*cupsBackSide: Flipped\n");
          break;
        }
        else if (!_cups_strcasecmp(dm, "DM3"))
        {
          cupsFilePuts(fp, "*cupsBackSide: Rotated\n");
          break;
        }
        else if (!_cups_strcasecmp(dm, "DM4"))
        {
          cupsFilePuts(fp, "*cupsBackSide: ManualTumble\n");
          break;
        }
      }
    }
    else if ((attr = ippFindAttribute(supported, "pwg-raster-document-sheet-back", IPP_TAG_KEYWORD)) != NULL)
    {
      keyword = ippGetString(attr, 0, NULL);

      if (!strcmp(keyword, "flipped"))
        cupsFilePuts(fp, "*cupsBackSide: Flipped\n");
      else if (!strcmp(keyword, "manual-tumble"))
        cupsFilePuts(fp, "*cupsBackSide: ManualTumble\n");
      else if (!strcmp(keyword, "normal"))
        cupsFilePuts(fp, "*cupsBackSide: Normal\n");
      else
        cupsFilePuts(fp, "*cupsBackSide: Rotated\n");
    }
  }

 /*
  * Output bin...
  */

  if ((attr = ippFindAttribute(supported, "output-bin-default", IPP_TAG_ZERO)) != NULL)
    pwg_ppdize_name(ippGetString(attr, 0, NULL), ppdname, sizeof(ppdname));
  else
    strlcpy(ppdname, "Unknown", sizeof(ppdname));

  if ((attr = ippFindAttribute(supported, "output-bin-supported", IPP_TAG_ZERO)) != NULL && (count = ippGetCount(attr)) > 0)
  {
    ipp_attribute_t	*trays = ippFindAttribute(supported, "printer-output-tray", IPP_TAG_STRING);
					/* printer-output-tray attribute, if any */
    const char		*tray_ptr;	/* printer-output-tray value */
    int			tray_len;	/* Len of printer-output-tray value */
    char		tray[IPP_MAX_OCTETSTRING];
					/* printer-output-tray string value */

    cupsFilePrintf(fp, "*OpenUI *OutputBin: PickOne\n"
                       "*OrderDependency: 10 AnySetup *OutputBin\n"
                       "*DefaultOutputBin: %s\n", ppdname);
    if (!strcmp(ppdname, "FaceUp"))
      cupsFilePuts(fp, "*DefaultOutputOrder: Reverse\n");
    else
      cupsFilePuts(fp, "*DefaultOutputOrder: Normal\n");

    for (i = 0; i < count; i ++)
    {
      keyword = ippGetString(attr, i, NULL);

      pwg_ppdize_name(keyword, ppdname, sizeof(ppdname));

      snprintf(msgid, sizeof(msgid), "output-bin.%s", keyword);

      cupsFilePrintf(fp, "*OutputBin %s: \"\"\n", ppdname);
      ppd_put_string(fp, lang, strings, "OutputBin", ppdname, msgid);

      if ((tray_ptr = ippGetOctetString(trays, i, &tray_len)) != NULL)
      {
        if (tray_len >= (int)sizeof(tray))
          tray_len = (int)sizeof(tray) - 1;

        memcpy(tray, tray_ptr, (size_t)tray_len);
        tray[tray_len] = '\0';

        if (strstr(tray, "stackingorder=lastToFirst;"))
          cupsFilePrintf(fp, "*PageStackOrder %s: Reverse\n", ppdname);
        else
          cupsFilePrintf(fp, "*PageStackOrder %s: Normal\n", ppdname);
      }
      else if (!strcmp(ppdname, "FaceUp"))
	cupsFilePrintf(fp, "*PageStackOrder %s: Reverse\n", ppdname);
      else
	cupsFilePrintf(fp, "*PageStackOrder %s: Normal\n", ppdname);
    }
    cupsFilePuts(fp, "*CloseUI: *OutputBin\n");
  }

 /*
  * Finishing options...
  */

  if ((attr = ippFindAttribute(supported, "finishings-supported", IPP_TAG_ENUM)) != NULL)
  {
    int			value;		/* Enum value */
    const char		*ppd_keyword;	/* PPD keyword for enum */
    cups_array_t	*names;		/* Names we've added */
    static const char * const base_keywords[] =
    {					/* Base STD 92 keywords */
      NULL,				/* none */
      "SingleAuto",			/* staple */
      "SingleAuto",			/* punch */
      NULL,				/* cover */
      "BindAuto",			/* bind */
      "SaddleStitch",			/* saddle-stitch */
      "EdgeStitchAuto",			/* edge-stitch */
      "Auto",				/* fold */
      NULL,				/* trim */
      NULL,				/* bale */
      NULL,				/* booklet-maker */
      NULL,				/* jog-offset */
      NULL,				/* coat */
      NULL				/* laminate */
    };

    count       = ippGetCount(attr);
    names       = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free);
    fin_options = cupsArrayNew((cups_array_func_t)strcmp, NULL);

   /*
    * Staple/Bind/Stitch
    */

    for (i = 0; i < count; i ++)
    {
      value   = ippGetInteger(attr, i);
      keyword = ippEnumString("finishings", value);

      if (!strncmp(keyword, "staple-", 7) || !strncmp(keyword, "bind-", 5) || !strncmp(keyword, "edge-stitch-", 12) || !strcmp(keyword, "saddle-stitch"))
        break;
    }

    if (i < count)
    {
      static const char * const staple_keywords[] =
      {					/* StapleLocation keywords */
	"SinglePortrait",
	"SingleRevLandscape",
	"SingleLandscape",
	"SingleRevPortrait",
	"EdgeStitchPortrait",
	"EdgeStitchLandscape",
	"EdgeStitchRevPortrait",
	"EdgeStitchRevLandscape",
	"DualPortrait",
	"DualLandscape",
	"DualRevPortrait",
	"DualRevLandscape",
	"TriplePortrait",
	"TripleLandscape",
	"TripleRevPortrait",
	"TripleRevLandscape"
      };
      static const char * const bind_keywords[] =
      {					/* StapleLocation binding keywords */
	"BindPortrait",
	"BindLandscape",
	"BindRevPortrait",
	"BindRevLandscape"
      };

      cupsArrayAdd(fin_options, "*StapleLocation");

      cupsFilePuts(fp, "*OpenUI *StapleLocation: PickOne\n");
      cupsFilePuts(fp, "*OrderDependency: 10 AnySetup *StapleLocation\n");
      cupsFilePrintf(fp, "*%s.Translation StapleLocation/%s: \"\"\n", lang->language, _cupsLangString(lang, _("Staple")));
      cupsFilePuts(fp, "*DefaultStapleLocation: None\n");
      cupsFilePuts(fp, "*StapleLocation None: \"\"\n");
      cupsFilePrintf(fp, "*%s.StapleLocation None/%s: \"\"\n", lang->language, _cupsLangString(lang, _("None")));

      for (; i < count; i ++)
      {
        value   = ippGetInteger(attr, i);
        keyword = ippEnumString("finishings", value);

        if (strncmp(keyword, "staple-", 7) && strncmp(keyword, "bind-", 5) && strncmp(keyword, "edge-stitch-", 12) && strcmp(keyword, "saddle-stitch"))
          continue;

        if (cupsArrayFind(names, (char *)keyword))
          continue;			/* Already did this finishing template */

        cupsArrayAdd(names, (char *)keyword);

	snprintf(msgid, sizeof(msgid), "finishings.%d", value);

        if (value >= IPP_FINISHINGS_NONE && value <= IPP_FINISHINGS_LAMINATE)
          ppd_keyword = base_keywords[value - IPP_FINISHINGS_NONE];
        else if (value >= IPP_FINISHINGS_STAPLE_TOP_LEFT && value <= IPP_FINISHINGS_STAPLE_TRIPLE_BOTTOM)
          ppd_keyword = staple_keywords[value - IPP_FINISHINGS_STAPLE_TOP_LEFT];
        else if (value >= IPP_FINISHINGS_BIND_LEFT && value <= IPP_FINISHINGS_BIND_BOTTOM)
          ppd_keyword = bind_keywords[value - IPP_FINISHINGS_BIND_LEFT];
        else
          ppd_keyword = NULL;

        if (!ppd_keyword)
          continue;

	cupsFilePrintf(fp, "*StapleLocation %s: \"\"\n", ppd_keyword);
	ppd_put_string(fp, lang, strings, "StapleLocation", ppd_keyword, msgid);
	cupsFilePrintf(fp, "*cupsIPPFinishings %d/%s: \"*StapleLocation %s\"\n", value, keyword, ppd_keyword);
      }

      cupsFilePuts(fp, "*CloseUI: *StapleLocation\n");
    }

   /*
    * Fold
    */

    for (i = 0; i < count; i ++)
    {
      value   = ippGetInteger(attr, i);
      keyword = ippEnumString("finishings", value);

      if (!strncmp(keyword, "cups-fold-", 10) || !strcmp(keyword, "fold") || !strncmp(keyword, "fold-", 5))
        break;
    }

    if (i < count)
    {
      static const char * const fold_keywords[] =
      {					/* FoldType keywords */
	"Accordion",
	"DoubleGate",
	"Gate",
	"Half",
	"HalfZ",
	"LeftGate",
	"Letter",
	"Parallel",
	"XFold",
	"RightGate",
	"ZFold",
	"EngineeringZ"
      };

      cupsArrayAdd(fin_options, "*FoldType");

      cupsFilePuts(fp, "*OpenUI *FoldType: PickOne\n");
      cupsFilePuts(fp, "*OrderDependency: 10 AnySetup *FoldType\n");
      cupsFilePrintf(fp, "*%s.Translation FoldType/%s: \"\"\n", lang->language, _cupsLangString(lang, _("Fold")));
      cupsFilePuts(fp, "*DefaultFoldType: None\n");
      cupsFilePuts(fp, "*FoldType None: \"\"\n");
      cupsFilePrintf(fp, "*%s.FoldType None/%s: \"\"\n", lang->language, _cupsLangString(lang, _("None")));

      for (; i < count; i ++)
      {
        value   = ippGetInteger(attr, i);
        keyword = ippEnumString("finishings", value);

        if (!strncmp(keyword, "cups-fold-", 10))
          keyword += 5;
        else if (strcmp(keyword, "fold") && strncmp(keyword, "fold-", 5))
          continue;

        if (cupsArrayFind(names, (char *)keyword))
          continue;			/* Already did this finishing template */

        cupsArrayAdd(names, (char *)keyword);

	snprintf(msgid, sizeof(msgid), "finishings.%d", value);

        if (value >= IPP_FINISHINGS_NONE && value <= IPP_FINISHINGS_LAMINATE)
          ppd_keyword = base_keywords[value - IPP_FINISHINGS_NONE];
        else if (value >= IPP_FINISHINGS_FOLD_ACCORDION && value <= IPP_FINISHINGS_FOLD_ENGINEERING_Z)
          ppd_keyword = fold_keywords[value - IPP_FINISHINGS_FOLD_ACCORDION];
        else if (value >= IPP_FINISHINGS_CUPS_FOLD_ACCORDION && value <= IPP_FINISHINGS_CUPS_FOLD_Z)
          ppd_keyword = fold_keywords[value - IPP_FINISHINGS_CUPS_FOLD_ACCORDION];
        else
          ppd_keyword = NULL;

        if (!ppd_keyword)
          continue;

	cupsFilePrintf(fp, "*FoldType %s: \"\"\n", ppd_keyword);
	ppd_put_string(fp, lang, strings, "FoldType", ppd_keyword, msgid);
	cupsFilePrintf(fp, "*cupsIPPFinishings %d/%s: \"*FoldType %s\"\n", value, keyword, ppd_keyword);
      }

      cupsFilePuts(fp, "*CloseUI: *FoldType\n");
    }

   /*
    * Punch
    */

    for (i = 0; i < count; i ++)
    {
      value   = ippGetInteger(attr, i);
      keyword = ippEnumString("finishings", value);

      if (!strcmp(keyword, "punch") || !strncmp(keyword, "cups-punch-", 11) || !strncmp(keyword, "punch-", 6))
        break;
    }

    if (i < count)
    {
      static const char * const punch_keywords[] =
      {					/* PunchMedia keywords */
	"SinglePortrait",
	"SingleRevLandscape",
	"SingleLandscape",
	"SingleRevPortrait",
	"DualPortrait",
	"DualLandscape",
	"DualRevPortrait",
	"DualRevLandscape",
	"TriplePortrait",
	"TripleLandscape",
	"TripleRevPortrait",
	"TripleRevLandscape",
	"QuadPortrait",
	"QuadLandscape",
	"QuadRevPortrait",
	"QuadRevLandscape",
	"MultiplePortrait",
	"MultipleLandscape",
	"MultipleRevPortrait",
	"MultipleRevLandscape"
      };

      cupsArrayAdd(fin_options, "*PunchMedia");

      cupsFilePuts(fp, "*OpenUI *PunchMedia: PickOne\n");
      cupsFilePuts(fp, "*OrderDependency: 10 AnySetup *PunchMedia\n");
      cupsFilePrintf(fp, "*%s.Translation PunchMedia/%s: \"\"\n", lang->language, _cupsLangString(lang, _("Punch")));
      cupsFilePuts(fp, "*DefaultPunchMedia: None\n");
      cupsFilePuts(fp, "*PunchMedia None: \"\"\n");
      cupsFilePrintf(fp, "*%s.PunchMedia None/%s: \"\"\n", lang->language, _cupsLangString(lang, _("None")));

      for (i = 0; i < count; i ++)
      {
        value   = ippGetInteger(attr, i);
        keyword = ippEnumString("finishings", value);

        if (!strncmp(keyword, "cups-punch-", 11))
          keyword += 5;
        else if (strcmp(keyword, "punch") && strncmp(keyword, "punch-", 6))
          continue;

        if (cupsArrayFind(names, (char *)keyword))
          continue;			/* Already did this finishing template */

        cupsArrayAdd(names, (char *)keyword);

	snprintf(msgid, sizeof(msgid), "finishings.%d", value);

        if (value >= IPP_FINISHINGS_NONE && value <= IPP_FINISHINGS_LAMINATE)
          ppd_keyword = base_keywords[value - IPP_FINISHINGS_NONE];
        else if (value >= IPP_FINISHINGS_PUNCH_TOP_LEFT && value <= IPP_FINISHINGS_PUNCH_MULTIPLE_BOTTOM)
          ppd_keyword = punch_keywords[value - IPP_FINISHINGS_PUNCH_TOP_LEFT];
        else if (value >= IPP_FINISHINGS_CUPS_PUNCH_TOP_LEFT && value <= IPP_FINISHINGS_CUPS_PUNCH_QUAD_BOTTOM)
          ppd_keyword = punch_keywords[value - IPP_FINISHINGS_CUPS_PUNCH_TOP_LEFT];
        else
          ppd_keyword = NULL;

        if (!ppd_keyword)
          continue;

	cupsFilePrintf(fp, "*PunchMedia %s: \"\"\n", ppd_keyword);
	ppd_put_string(fp, lang, strings, "PunchMedia", ppd_keyword, msgid);
	cupsFilePrintf(fp, "*cupsIPPFinishings %d/%s: \"*PunchMedia %s\"\n", value, keyword, ppd_keyword);
      }

      cupsFilePuts(fp, "*CloseUI: *PunchMedia\n");
    }

   /*
    * Booklet
    */

    if (ippContainsInteger(attr, IPP_FINISHINGS_BOOKLET_MAKER))
    {
      cupsArrayAdd(fin_options, "*Booklet");

      cupsFilePuts(fp, "*OpenUI *Booklet: Boolean\n");
      cupsFilePuts(fp, "*OrderDependency: 10 AnySetup *Booklet\n");
      cupsFilePrintf(fp, "*%s.Translation Booklet/%s: \"\"\n", lang->language, _cupsLangString(lang, _("Booklet")));
      cupsFilePuts(fp, "*DefaultBooklet: False\n");
      cupsFilePuts(fp, "*Booklet False: \"\"\n");
      cupsFilePuts(fp, "*Booklet True: \"\"\n");
      cupsFilePrintf(fp, "*cupsIPPFinishings %d/booklet-maker: \"*Booklet True\"\n", IPP_FINISHINGS_BOOKLET_MAKER);
      cupsFilePuts(fp, "*CloseUI: *Booklet\n");
    }

   /*
    * CutMedia
    */

    for (i = 0; i < count; i ++)
    {
      value   = ippGetInteger(attr, i);
      keyword = ippEnumString("finishings", value);

      if (!strcmp(keyword, "trim") || !strncmp(keyword, "trim-", 5))
        break;
    }

    if (i < count)
    {
      static const char * const trim_keywords[] =
      {				/* CutMedia keywords */
        "EndOfPage",
        "EndOfDoc",
        "EndOfSet",
        "EndOfJob"
      };

      cupsArrayAdd(fin_options, "*CutMedia");

      cupsFilePuts(fp, "*OpenUI *CutMedia: PickOne\n");
      cupsFilePuts(fp, "*OrderDependency: 10 AnySetup *CutMedia\n");
      cupsFilePrintf(fp, "*%s.Translation CutMedia/%s: \"\"\n", lang->language, _cupsLangString(lang, _("Cut")));
      cupsFilePuts(fp, "*DefaultCutMedia: None\n");
      cupsFilePuts(fp, "*CutMedia None: \"\"\n");
      cupsFilePrintf(fp, "*%s.CutMedia None/%s: \"\"\n", lang->language, _cupsLangString(lang, _("None")));

      for (i = 0; i < count; i ++)
      {
        value   = ippGetInteger(attr, i);
        keyword = ippEnumString("finishings", value);

	if (strcmp(keyword, "trim") && strncmp(keyword, "trim-", 5))
          continue;

        if (cupsArrayFind(names, (char *)keyword))
          continue;			/* Already did this finishing template */

        cupsArrayAdd(names, (char *)keyword);

	snprintf(msgid, sizeof(msgid), "finishings.%d", value);

        if (value == IPP_FINISHINGS_TRIM)
          ppd_keyword = "Auto";
	else
	  ppd_keyword = trim_keywords[value - IPP_FINISHINGS_TRIM_AFTER_PAGES];

	cupsFilePrintf(fp, "*CutMedia %s: \"\"\n", ppd_keyword);
	ppd_put_string(fp, lang, strings, "CutMedia", ppd_keyword, msgid);
	cupsFilePrintf(fp, "*cupsIPPFinishings %d/%s: \"*CutMedia %s\"\n", value, keyword, ppd_keyword);
      }

      cupsFilePuts(fp, "*CloseUI: *CutMedia\n");
    }

    cupsArrayDelete(names);
  }

  if ((attr = ippFindAttribute(supported, "finishings-col-database", IPP_TAG_BEGIN_COLLECTION)) != NULL)
  {
    ipp_t	*finishing_col;		/* Current finishing collection */
    ipp_attribute_t *finishing_attr;	/* Current finishing member attribute */
    cups_array_t *templates;		/* Finishing templates */

    cupsFilePuts(fp, "*OpenUI *cupsFinishingTemplate: PickOne\n");
    cupsFilePuts(fp, "*OrderDependency: 10 AnySetup *cupsFinishingTemplate\n");
    cupsFilePrintf(fp, "*%s.Translation cupsFinishingTemplate/%s: \"\"\n", lang->language, _cupsLangString(lang, _("Finishing Preset")));
    cupsFilePuts(fp, "*DefaultcupsFinishingTemplate: none\n");
    cupsFilePuts(fp, "*cupsFinishingTemplate none: \"\"\n");
    cupsFilePrintf(fp, "*%s.cupsFinishingTemplate none/%s: \"\"\n", lang->language, _cupsLangString(lang, _("None")));

    templates = cupsArrayNew((cups_array_func_t)strcmp, NULL);
    count     = ippGetCount(attr);

    for (i = 0; i < count; i ++)
    {
      finishing_col = ippGetCollection(attr, i);
      keyword       = ippGetString(ippFindAttribute(finishing_col, "finishing-template", IPP_TAG_ZERO), 0, NULL);

      if (!keyword || cupsArrayFind(templates, (void *)keyword))
        continue;

      if (!strcmp(keyword, "none"))
        continue;

      cupsArrayAdd(templates, (void *)keyword);

      pwg_ppdize_name(keyword, ppdname, sizeof(ppdname));

      snprintf(msgid, sizeof(msgid), "finishing-template.%s", keyword);

      cupsFilePrintf(fp, "*cupsFinishingTemplate %s: \"\n", ppdname);
      for (finishing_attr = ippFirstAttribute(finishing_col); finishing_attr; finishing_attr = ippNextAttribute(finishing_col))
      {
        if (ippGetValueTag(finishing_attr) == IPP_TAG_BEGIN_COLLECTION)
        {
	  const char *name = ippGetName(finishing_attr);
					/* Member attribute name */

          if (strcmp(name, "media-size"))
            cupsFilePrintf(fp, "%% %s\n", name);
	}
      }
      cupsFilePuts(fp, "\"\n");
      ppd_put_string(fp, lang, strings, "cupsFinishingTemplate", ppdname, msgid);
      cupsFilePuts(fp, "*End\n");
    }

    cupsFilePuts(fp, "*CloseUI: *cupsFinishingTemplate\n");

    if (cupsArrayCount(fin_options))
    {
      const char	*fin_option;	/* Current finishing option */

      cupsFilePuts(fp, "*cupsUIConstraint finishing-template: \"*cupsFinishingTemplate");
      for (fin_option = (const char *)cupsArrayFirst(fin_options); fin_option; fin_option = (const char *)cupsArrayNext(fin_options))
        cupsFilePrintf(fp, " %s", fin_option);
      cupsFilePuts(fp, "\"\n");

      cupsFilePuts(fp, "*cupsUIResolver finishing-template: \"*cupsFinishingTemplate None");
      for (fin_option = (const char *)cupsArrayFirst(fin_options); fin_option; fin_option = (const char *)cupsArrayNext(fin_options))
        cupsFilePrintf(fp, " %s None", fin_option);
      cupsFilePuts(fp, "\"\n");
    }

    cupsArrayDelete(templates);
  }

  cupsArrayDelete(fin_options);

 /*
  * Presets...
  */

  if ((attr = ippFindAttribute(supported, "job-presets-supported", IPP_TAG_BEGIN_COLLECTION)) != NULL)
  {
    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      ipp_t	*preset = ippGetCollection(attr, i);
					/* Preset collection */
      const char *preset_name = ippGetString(ippFindAttribute(preset, "preset-name", IPP_TAG_ZERO), 0, NULL);
					/* Preset name */
      ipp_attribute_t *member;		/* Member attribute in preset */
      const char *member_name;		/* Member attribute name */
      char      	member_value[256];	/* Member attribute value */

      if (!preset || !preset_name)
        continue;

      pwg_ppdize_name(preset_name, ppdname, sizeof(ppdname));
      cupsFilePrintf(fp, "*APPrinterPreset %s: \"\n", ppdname);
      for (member = ippFirstAttribute(preset); member; member = ippNextAttribute(preset))
      {
        member_name = ippGetName(member);

        if (!member_name || !strcmp(member_name, "preset-name"))
          continue;

        if (!strcmp(member_name, "finishings"))
        {
	  for (i = 0, count = ippGetCount(member); i < count; i ++)
	  {
	    const char *option = NULL;	/* PPD option name */

	    keyword = ippEnumString("finishings", ippGetInteger(member, i));

	    if (!strcmp(keyword, "booklet-maker"))
	    {
	      option  = "Booklet";
	      keyword = "True";
	    }
	    else if (!strncmp(keyword, "fold-", 5))
	      option = "FoldType";
	    else if (!strncmp(keyword, "punch-", 6))
	      option = "PunchMedia";
	    else if (!strncmp(keyword, "bind-", 5) || !strncmp(keyword, "edge-stitch-", 12) || !strcmp(keyword, "saddle-stitch") || !strncmp(keyword, "staple-", 7))
	      option = "StapleLocation";

	    if (option && keyword)
	      cupsFilePrintf(fp, "*%s %s\n", option, keyword);
	  }
        }
        else if (!strcmp(member_name, "finishings-col"))
        {
          ipp_t *fin_col;		/* finishings-col value */

          for (i = 0, count = ippGetCount(member); i < count; i ++)
          {
            fin_col = ippGetCollection(member, i);

            if ((keyword = ippGetString(ippFindAttribute(fin_col, "finishing-template", IPP_TAG_ZERO), 0, NULL)) != NULL)
            {
              pwg_ppdize_name(keyword, ppdname, sizeof(ppdname));
              cupsFilePrintf(fp, "*cupsFinishingTemplate %s\n", ppdname);
            }
          }
        }
        else if (!strcmp(member_name, "media"))
        {
         /*
          * Map media to PageSize...
          */

          if ((pwg = pwgMediaForPWG(ippGetString(member, 0, NULL))) != NULL && pwg->ppd)
            cupsFilePrintf(fp, "*PageSize %s\n", pwg->ppd);
        }
        else if (!strcmp(member_name, "media-col"))
        {
          media_col = ippGetCollection(member, 0);

          if ((media_size = ippGetCollection(ippFindAttribute(media_col, "media-size", IPP_TAG_BEGIN_COLLECTION), 0)) != NULL)
          {
            x_dim = ippFindAttribute(media_size, "x-dimension", IPP_TAG_INTEGER);
            y_dim = ippFindAttribute(media_size, "y-dimension", IPP_TAG_INTEGER);
            if ((pwg = pwgMediaForSize(ippGetInteger(x_dim, 0), ippGetInteger(y_dim, 0))) != NULL && pwg->ppd)
	      cupsFilePrintf(fp, "*PageSize %s\n", pwg->ppd);
          }

          if ((keyword = ippGetString(ippFindAttribute(media_col, "media-source", IPP_TAG_ZERO), 0, NULL)) != NULL)
          {
            pwg_ppdize_name(keyword, ppdname, sizeof(ppdname));
            cupsFilePrintf(fp, "*InputSlot %s\n", ppdname);
	  }

          if ((keyword = ippGetString(ippFindAttribute(media_col, "media-type", IPP_TAG_ZERO), 0, NULL)) != NULL)
          {
            pwg_ppdize_name(keyword, ppdname, sizeof(ppdname));
            cupsFilePrintf(fp, "*MediaType %s\n", ppdname);
	  }
        }
        else if (!strcmp(member_name, "print-quality"))
        {
	 /*
	  * Map print-quality to cupsPrintQuality...
	  */

          int qval = ippGetInteger(member, 0);
					/* print-quality value */
	  static const char * const qualities[] = { "Draft", "Normal", "High" };
					/* cupsPrintQuality values */

          if (qval >= IPP_QUALITY_DRAFT && qval <= IPP_QUALITY_HIGH)
            cupsFilePrintf(fp, "*cupsPrintQuality %s\n", qualities[qval - IPP_QUALITY_DRAFT]);
        }
        else if (!strcmp(member_name, "output-bin"))
        {
          pwg_ppdize_name(ippGetString(member, 0, NULL), ppdname, sizeof(ppdname));
          cupsFilePrintf(fp, "*OutputBin %s\n", ppdname);
        }
        else if (!strcmp(member_name, "sides"))
        {
          keyword = ippGetString(member, 0, NULL);
          if (keyword && !strcmp(keyword, "one-sided"))
            cupsFilePuts(fp, "*Duplex None\n");
	  else if (keyword && !strcmp(keyword, "two-sided-long-edge"))
	    cupsFilePuts(fp, "*Duplex DuplexNoTumble\n");
	  else if (keyword && !strcmp(keyword, "two-sided-short-edge"))
	    cupsFilePuts(fp, "*Duplex DuplexTumble\n");
        }
        else
        {
         /*
          * Add attribute name and value as-is...
          */

          ippAttributeString(member, member_value, sizeof(member_value));
          cupsFilePrintf(fp, "*%s %s\n", member_name, member_value);
	}
      }

      cupsFilePuts(fp, "\"\n*End\n");

      snprintf(msgid, sizeof(msgid), "preset-name.%s", preset_name);
      pwg_ppdize_name(preset_name, ppdname, sizeof(ppdname));
      ppd_put_string(fp, lang, strings, "APPrinterPreset", ppdname, msgid);
    }
  }

 /*
  * Add cupsSingleFile to support multiple files printing on printers
  * which don't support multiple files in its firmware...
  *
  * Adding the keyword degrades printing performance (there is 1-2 seconds
  * pause between files).
  */

  cupsFilePuts(fp, "*cupsSingleFile: true\n");

 /*
  * Close up and return...
  */

  cupsFileClose(fp);

  _cupsMessageFree(strings);

  return (buffer);

 /*
  * If we get here then there was a problem creating the PPD...
  */

  bad_ppd:

  cupsFileClose(fp);
  unlink(buffer);
  *buffer = '\0';

  _cupsMessageFree(strings);

  _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Printer does not support required IPP attributes or document formats."), 1);

  return (NULL);
}


/*
 * '_pwgInputSlotForSource()' - Get the InputSlot name for the given PWG
 *                              media-source.
 */

const char *				/* O - InputSlot name */
_pwgInputSlotForSource(
    const char *media_source,		/* I - PWG media-source */
    char       *name,			/* I - Name buffer */
    size_t     namesize)		/* I - Size of name buffer */
{
 /*
  * Range check input...
  */

  if (!media_source || !name || namesize < PPD_MAX_NAME)
    return (NULL);

  if (_cups_strcasecmp(media_source, "main"))
    strlcpy(name, "Cassette", namesize);
  else if (_cups_strcasecmp(media_source, "alternate"))
    strlcpy(name, "Multipurpose", namesize);
  else if (_cups_strcasecmp(media_source, "large-capacity"))
    strlcpy(name, "LargeCapacity", namesize);
  else if (_cups_strcasecmp(media_source, "bottom"))
    strlcpy(name, "Lower", namesize);
  else if (_cups_strcasecmp(media_source, "middle"))
    strlcpy(name, "Middle", namesize);
  else if (_cups_strcasecmp(media_source, "top"))
    strlcpy(name, "Upper", namesize);
  else if (_cups_strcasecmp(media_source, "rear"))
    strlcpy(name, "Rear", namesize);
  else if (_cups_strcasecmp(media_source, "side"))
    strlcpy(name, "Side", namesize);
  else if (_cups_strcasecmp(media_source, "envelope"))
    strlcpy(name, "Envelope", namesize);
  else if (_cups_strcasecmp(media_source, "main-roll"))
    strlcpy(name, "Roll", namesize);
  else if (_cups_strcasecmp(media_source, "alternate-roll"))
    strlcpy(name, "Roll2", namesize);
  else
    pwg_ppdize_name(media_source, name, namesize);

  return (name);
}


/*
 * '_pwgMediaTypeForType()' - Get the MediaType name for the given PWG
 *                            media-type.
 */

const char *				/* O - MediaType name */
_pwgMediaTypeForType(
    const char *media_type,		/* I - PWG media-type */
    char       *name,			/* I - Name buffer */
    size_t     namesize)		/* I - Size of name buffer */
{
 /*
  * Range check input...
  */

  if (!media_type || !name || namesize < PPD_MAX_NAME)
    return (NULL);

  if (_cups_strcasecmp(media_type, "auto"))
    strlcpy(name, "Auto", namesize);
  else if (_cups_strcasecmp(media_type, "cardstock"))
    strlcpy(name, "Cardstock", namesize);
  else if (_cups_strcasecmp(media_type, "envelope"))
    strlcpy(name, "Envelope", namesize);
  else if (_cups_strcasecmp(media_type, "photographic-glossy"))
    strlcpy(name, "Glossy", namesize);
  else if (_cups_strcasecmp(media_type, "photographic-high-gloss"))
    strlcpy(name, "HighGloss", namesize);
  else if (_cups_strcasecmp(media_type, "photographic-matte"))
    strlcpy(name, "Matte", namesize);
  else if (_cups_strcasecmp(media_type, "stationery"))
    strlcpy(name, "Plain", namesize);
  else if (_cups_strcasecmp(media_type, "stationery-coated"))
    strlcpy(name, "Coated", namesize);
  else if (_cups_strcasecmp(media_type, "stationery-inkjet"))
    strlcpy(name, "Inkjet", namesize);
  else if (_cups_strcasecmp(media_type, "stationery-letterhead"))
    strlcpy(name, "Letterhead", namesize);
  else if (_cups_strcasecmp(media_type, "stationery-preprinted"))
    strlcpy(name, "Preprinted", namesize);
  else if (_cups_strcasecmp(media_type, "transparency"))
    strlcpy(name, "Transparency", namesize);
  else
    pwg_ppdize_name(media_type, name, namesize);

  return (name);
}


/*
 * '_pwgPageSizeForMedia()' - Get the PageSize name for the given media.
 */

const char *				/* O - PageSize name */
_pwgPageSizeForMedia(
    pwg_media_t *media,			/* I - Media */
    char        *name,			/* I - PageSize name buffer */
    size_t      namesize)		/* I - Size of name buffer */
{
  const char	*sizeptr,		/* Pointer to size in PWG name */
		*dimptr;		/* Pointer to dimensions in PWG name */


 /*
  * Range check input...
  */

  if (!media || !name || namesize < PPD_MAX_NAME)
    return (NULL);

 /*
  * Copy or generate a PageSize name...
  */

  if (media->ppd)
  {
   /*
    * Use a standard Adobe name...
    */

    strlcpy(name, media->ppd, namesize);
  }
  else if (!media->pwg || !strncmp(media->pwg, "custom_", 7) ||
           (sizeptr = strchr(media->pwg, '_')) == NULL ||
	   (dimptr = strchr(sizeptr + 1, '_')) == NULL ||
	   (size_t)(dimptr - sizeptr) > namesize)
  {
   /*
    * Use a name of the form "wNNNhNNN"...
    */

    snprintf(name, namesize, "w%dh%d", (int)PWG_TO_POINTS(media->width),
             (int)PWG_TO_POINTS(media->length));
  }
  else
  {
   /*
    * Copy the size name from class_sizename_dimensions...
    */

    memcpy(name, sizeptr + 1, (size_t)(dimptr - sizeptr - 1));
    name[dimptr - sizeptr - 1] = '\0';
  }

  return (name);
}


/*
 * 'cups_connect()' - Connect to a URL and get the resource path.
 */

static int				/* O  - 1 on success, 0 on failure */
cups_connect(http_t     **http,		/* IO - Current HTTP connection */
             const char *url,		/* I  - URL to connect */
             char       *resource,	/* I  - Resource path buffer */
             size_t     ressize)	/* I  - Size of resource path buffer */
{
  char			scheme[32],	/* URL scheme */
			userpass[256],	/* URL username:password */
			host[256],	/* URL host */
			curhost[256];	/* Current host */
  int			port;		/* URL port */
  http_encryption_t	encryption;	/* Type of encryption to use */


  // Separate the URI...
  if (httpSeparateURI(HTTP_URI_CODING_ALL, url, scheme, sizeof(scheme), userpass, sizeof(userpass), host, sizeof(host), &port, resource, ressize) < HTTP_URI_STATUS_OK)
    return (0);

  // Use encryption as needed..
  if (port == 443 || !strcmp(scheme, "https") || !strcmp(scheme, "ipps"))
    encryption = HTTP_ENCRYPTION_ALWAYS;
  else
    encryption = HTTP_ENCRYPTION_IF_REQUESTED;

  if (!*http || strcasecmp(host, httpGetHostname(*http, curhost, sizeof(curhost))) || httpAddrPort(httpGetAddress(*http)) != port || httpIsEncrypted(*http) != (encryption == HTTP_ENCRYPTION_ALWAYS))
  {
    httpClose(*http);
    *http = httpConnect2(host, port, NULL, AF_UNSPEC, encryption, 1, 5000, NULL);
  }

  return (*http != NULL);
}


/*
 * 'cups_get_url()' - Get a copy of the file at the given URL.
 */

static int				/* O  - 1 on success, 0 on failure */
cups_get_url(http_t     **http,		/* IO - Current HTTP connection */
             const char *url,		/* I  - URL to get */
             char       *name,		/* I  - Temporary filename */
             size_t     namesize)	/* I  - Size of temporary filename buffer */
{
  char			resource[256];	/* URL resource */
  http_status_t		status;		/* Status of GET request */
  int			fd;		/* Temporary file */


  if (!cups_connect(http, url, resource, sizeof(resource)))
    return (0);

  if ((fd = cupsTempFd(name, (int)namesize)) < 0)
    return (0);

  status = cupsGetFd(*http, resource, fd);

  close(fd);

  if (status != HTTP_STATUS_OK)
  {
    unlink(name);
    *name = '\0';
    return (0);
  }

  return (1);
}


/*
 * 'ppd_put_strings()' - Write localization attributes to a PPD file.
 */

static void
ppd_put_string(cups_file_t  *fp,	/* I - PPD file */
               cups_lang_t  *lang,	/* I - Language */
               cups_array_t *strings,	/* I - Strings */
	       const char   *ppd_option,/* I - PPD option */
	       const char   *ppd_choice,/* I - PPD choice */
	       const char   *pwg_msgid)	/* I - PWG message ID */
{
  const char	*text;			/* Localized text */


  if ((text = _cupsLangString(lang, pwg_msgid)) == pwg_msgid || !strcmp(pwg_msgid, text))
  {
    if ((text = _cupsMessageLookup(strings, pwg_msgid)) == pwg_msgid)
      return;
  }

  // Add the first line of localized text...
  cupsFilePrintf(fp, "*%s.%s %s/", lang->language, ppd_option, ppd_choice);
  while (*text && *text != '\n')
  {
    // Escape ":" and "<"...
    if (*text == ':' || *text == '<')
      cupsFilePrintf(fp, "<%02X>", *text);
    else
      cupsFilePutChar(fp, *text);

    text ++;
  }
  cupsFilePuts(fp, ": \"\"\n");
}


/*
 * 'pwg_add_finishing()' - Add a finishings value.
 */

static void
pwg_add_finishing(
    cups_array_t     *finishings,	/* I - Finishings array */
    ipp_finishings_t template,		/* I - Finishing template */
    const char       *name,		/* I - PPD option */
    const char       *value)		/* I - PPD choice */
{
  _pwg_finishings_t	*f;		/* New finishings value */


  if ((f = (_pwg_finishings_t *)calloc(1, sizeof(_pwg_finishings_t))) != NULL)
  {
    f->value       = template;
    f->num_options = cupsAddOption(name, value, 0, &f->options);

    cupsArrayAdd(finishings, f);
  }
}


/*
 * 'pwg_add_message()' - Add a message to the PPD cached strings.
 */

static void
pwg_add_message(cups_array_t *a,	/* I - Message catalog */
                const char   *msg,	/* I - Message identifier */
                const char   *str)	/* I - Localized string */
{
  _cups_message_t	*m;		/* New message */


  if ((m = calloc(1, sizeof(_cups_message_t))) != NULL)
  {
    m->msg = strdup(msg);
    m->str = strdup(str);
    cupsArrayAdd(a, m);
  }
}


/*
 * 'pwg_compare_finishings()' - Compare two finishings values.
 */

static int				/* O - Result of comparison */
pwg_compare_finishings(
    _pwg_finishings_t *a,		/* I - First finishings value */
    _pwg_finishings_t *b)		/* I - Second finishings value */
{
  return ((int)b->value - (int)a->value);
}


/*
 * 'pwg_compare_sizes()' - Compare two media sizes...
 */

static int				/* O - Result of comparison */
pwg_compare_sizes(cups_size_t *a,	/* I - First media size */
                  cups_size_t *b)	/* I - Second media size */
{
  return (strcmp(a->media, b->media));
}


/*
 * 'pwg_copy_size()' - Copy a media size.
 */

static cups_size_t *			/* O - New media size */
pwg_copy_size(cups_size_t *size)	/* I - Media size to copy */
{
  cups_size_t	*newsize = (cups_size_t *)calloc(1, sizeof(cups_size_t));
					/* New media size */

  if (newsize)
    memcpy(newsize, size, sizeof(cups_size_t));

  return (newsize);
}


/*
 * 'pwg_free_finishings()' - Free a finishings value.
 */

static void
pwg_free_finishings(
    _pwg_finishings_t *f)		/* I - Finishings value */
{
  cupsFreeOptions(f->num_options, f->options);
  free(f);
}


/*
 * 'pwg_ppdize_name()' - Convert an IPP keyword to a PPD keyword.
 */

static void
pwg_ppdize_name(const char *ipp,	/* I - IPP keyword */
                char       *name,	/* I - Name buffer */
		size_t     namesize)	/* I - Size of name buffer */
{
  char	*ptr,				/* Pointer into name buffer */
	*end;				/* End of name buffer */


  if (!ipp || !_cups_isalnum(*ipp))
  {
    *name = '\0';
    return;
  }

  *name = (char)toupper(*ipp++);

  for (ptr = name + 1, end = name + namesize - 1; *ipp && ptr < end;)
  {
    if (*ipp == '-' && _cups_isalnum(ipp[1]))
    {
      ipp ++;
      *ptr++ = (char)toupper(*ipp++ & 255);
    }
    else if (*ipp == '_' || *ipp == '.' || *ipp == '-' || _cups_isalnum(*ipp))
    {
      *ptr++ = *ipp++;
    }
    else
    {
      ipp ++;
    }
  }

  *ptr = '\0';
}


/*
 * 'pwg_ppdize_resolution()' - Convert PWG resolution values to PPD values.
 */

static void
pwg_ppdize_resolution(
    ipp_attribute_t *attr,		/* I - Attribute to convert */
    int             element,		/* I - Element to convert */
    int             *xres,		/* O - X resolution in DPI */
    int             *yres,		/* O - Y resolution in DPI */
    char            *name,		/* I - Name buffer */
    size_t          namesize)		/* I - Size of name buffer */
{
  ipp_res_t units;			/* Units for resolution */


  *xres = ippGetResolution(attr, element, yres, &units);

  if (units == IPP_RES_PER_CM)
  {
    *xres = (int)(*xres * 2.54);
    *yres = (int)(*yres * 2.54);
  }

  if (name && namesize > 4)
  {
    if (*xres == *yres)
      snprintf(name, namesize, "%ddpi", *xres);
    else
      snprintf(name, namesize, "%dx%ddpi", *xres, *yres);
  }
}


/*
 * 'pwg_unppdize_name()' - Convert a PPD keyword to a lowercase IPP keyword.
 */

static void
pwg_unppdize_name(const char *ppd,	/* I - PPD keyword */
		  char       *name,	/* I - Name buffer */
                  size_t     namesize,	/* I - Size of name buffer */
                  const char *dashchars)/* I - Characters to be replaced by dashes */
{
  char	*ptr,				/* Pointer into name buffer */
	*end;				/* End of name buffer */
  int   nodash = 1;                     /* Next char in IPP name cannot be a
                                           dash (first char or after a dash) */


  if (_cups_islower(*ppd))
  {
   /*
    * Already lowercase name, use as-is?
    */

    const char *ppdptr;			/* Pointer into PPD keyword */

    for (ppdptr = ppd + 1; *ppdptr; ppdptr ++)
      if (_cups_isupper(*ppdptr) || strchr(dashchars, *ppdptr) ||
	  (*ppdptr == '-' && *(ppdptr - 1) == '-') ||
	  (*ppdptr == '-' && *(ppdptr + 1) == '\0'))
        break;

    if (!*ppdptr)
    {
      strlcpy(name, ppd, namesize);
      return;
    }
  }

  for (ptr = name, end = name + namesize - 1; *ppd && ptr < end; ppd ++)
  {
    if (_cups_isalnum(*ppd))
    {
      *ptr++ = (char)tolower(*ppd & 255);
      nodash = 0;
    }
    else if (*ppd == '-' || strchr(dashchars, *ppd))
    {
      if (nodash == 0)
      {
	*ptr++ = '-';
	nodash = 1;
      }
    }
    else
    {
      *ptr++ = *ppd;
      nodash = 0;
    }

    if (nodash == 0)
    {
      if (!_cups_isupper(*ppd) && _cups_isalnum(*ppd) &&
	  _cups_isupper(ppd[1]) && ptr < end)
      {
	*ptr++ = '-';
	nodash = 1;
      }
      else if (!isdigit(*ppd & 255) && isdigit(ppd[1] & 255))
      {
	*ptr++ = '-';
	nodash = 1;
      }
    }
  }

  /* Remove trailing dashes */
  while (ptr > name && *(ptr - 1) == '-')
    ptr --;

  *ptr = '\0';
}
