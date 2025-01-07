//
// PPD cache benchmarking program for CUPS.
//
// Usage:
//
//   ./cachebench PRINTER-URI
//
// Copyright © 2024 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "ppd-private.h"
#include "file-private.h"
#include <sys/time.h>


//
// Local functions...
//

static ipp_t	*create_attrs(_ppd_cache_t *pc);
static ipp_t	*create_media_col(pwg_size_t *pwg, const char *source, const char *type);
static ipp_t	*create_media_size(int min_width, int max_width, int min_length, int max_length);
static double	get_elapsed(struct timeval *starttime, struct timeval *endtime);


//
// 'main()' - Main entry.
//

int					// O - Exit status
main(int  argc,				// I - Number of command-line args
     char *argv[])			// I - Command-line arguments
{
  cups_dest_t	*dest;			// Destination
  http_t	*http;			// Connection to printer
  char		resource[1024];		// Resource path for printer
  ipp_t		*request,		// IPP request
		*response;		// IPP response
  char		ppd_file[1024],		// Generated PPD file
		cache_file[1024];	// Generated PPD cache
  ppd_file_t	*ppd;			// PPD (loaded)
  _ppd_cache_t	*pc;			// PPD cache
  ipp_t		*pc_attrs;		// PPD cache attributes
  int		i;			// Looping var
  struct timeval starttime,		// Start time
		endtime;		// End time
  double	ppd_secs,		// Average time to load PPD file
		cache_secs;		// Average time to load cache file
  static const char * const requested_attrs[] =
  {					// "requested-attributes"
    "all",
    "media-col-database"
  };


  // Get the printer URI for the test...
  if (argc != 2 || (strncmp(argv[1], "ipp://", 6) && strncmp(argv[1], "ipps://", 7)))
  {
    fputs("Usage: ./cachebench PRINTER-URI\n", stderr);
    return (1);
  }

  dest = cupsGetDestWithURI("bench", argv[1]);

  printf("Connecting to '%s'...\n", argv[1]);

  if ((http = cupsConnectDest(dest, CUPS_DEST_FLAGS_DEVICE, /*msec*/30000, /*cancel*/NULL, resource, sizeof(resource), /*cb*/NULL, /*user_data*/NULL)) == NULL)
  {
    fprintf(stderr, "cachebench: Unable to connect to '%s': %s\n", argv[1], cupsGetErrorString());
    return (1);
  }

  // Get printer attributes...
  printf("Geting printer attributes for '%s'...\n", argv[1]);

  request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, argv[1]);
  ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes", 2, NULL, requested_attrs);

  response = cupsDoRequest(http, request, resource);

  if (cupsGetError() >= IPP_STATUS_ERROR_BAD_REQUEST)
  {
    fprintf(stderr, "cachebench: Unable to get attributes for '%s': %s\n", argv[1], cupsGetErrorString());
    return (1);
  }

  // Generate a PPD file...
  printf("Generating PPD file for '%s'...\n", argv[1]);

  if (!_ppdCreateFromIPP(ppd_file, sizeof(ppd_file), response))
  {
    fprintf(stderr, "cachebench: Unable to create PPD file for '%s': %s\n", argv[1], cupsGetErrorString());
    return (1);
  }

  printf("PPD file: %s\n", ppd_file);

  ippDelete(response);

  snprintf(cache_file, sizeof(cache_file), "%s.cache", ppd_file);

  // Try doing the PPD and cache stuff multiple times...
  for (i = 0, ppd_secs = cache_secs = 0.0; i < 1000; i ++)
  {
    // Generate the PPD cache file
    gettimeofday(&starttime, NULL);

    if ((ppd = ppdOpenFile(ppd_file)) == NULL)
    {
      int	linenum;		// Line number of error
      ppd_status_t status;		// PPD status

      status = ppdLastError(&linenum);
      fprintf(stderr, "cachebench: Unable to open PPD file for '%s': %s on line %d.\n", argv[1], ppdErrorString(status), linenum);
      return (1);
    }

    pc       = _ppdCacheCreateWithPPD(NULL, ppd);
    pc_attrs = create_attrs(pc);

    gettimeofday(&endtime, NULL);

    ppd_secs += get_elapsed(&starttime, &endtime);

    // Save it and free memory...
    _ppdCacheWriteFile(pc, cache_file, pc_attrs);

    _ppdCacheDestroy(pc);
    ppdClose(ppd);
    ippDelete(pc_attrs);

    // Load the cache file
    gettimeofday(&starttime, NULL);
    pc = _ppdCacheCreateWithFile(cache_file, &pc_attrs);
    gettimeofday(&endtime, NULL);

    cache_secs += get_elapsed(&starttime, &endtime);

    _ppdCacheDestroy(pc);
    ippDelete(pc_attrs);
  }

  printf("Total raw PPD time: %.3fsecs\n", ppd_secs);
  printf("Total cached PPD time: %.3fsecs\n", cache_secs);

//  unlink(ppd_file);
  unlink(cache_file);

  return (0);
}


//
// 'create_attrs()' - Create printer attributes from a PPD cache.
//

static ipp_t *				// O - Printer attributes
create_attrs(_ppd_cache_t *pc)		// I - PPD cache
{
  int		i, j;			// Looping vars
  ipp_t		*attrs;			// Printer attributes
//  ipp_attribute_t *attr;		// Current attribute
  pwg_size_t	*size;			// Current media size
  int		num_values;		// Number of values
  ipp_t		*cvalues[256];		// Collection values
  int		ivalues[256];		// Integer values
  const char	*svalues[256];		// String values
  static const char * const media_col_supported[] =
  {					// "media-col-supported" values
    "media-bottom-margin",
    "media-left-margin",
    "media-right-margin",
    "media-size",
    "media-source",
    "media-top-margin",
    "media-type"
  };
  static const char * const sides_supported[] =
  {					// "sides-supported" values
    "one-sided",
    "two-sided-long-edge",
    "two-sided-short-edge"
  };


  attrs = ippNew();

  // media-supported
  for (i = 0, size = pc->sizes; i < pc->num_sizes && i < (int)(sizeof(svalues) / sizeof(svalues[0])); i ++)
    svalues[i] = size->map.pwg;
  num_values = i;

  if (pc->custom_max_keyword && num_values < (int)(sizeof(svalues) / sizeof(svalues[0])))
    svalues[num_values ++] = pc->custom_max_keyword;

  if (pc->custom_min_keyword && num_values < (int)(sizeof(svalues) / sizeof(svalues[0])))
    svalues[num_values ++] = pc->custom_min_keyword;

  ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-supported", num_values, NULL, svalues);

  // media-bottom-margin-supported
  for (i = 0, size = pc->sizes, num_values = 0; i < pc->num_sizes; i ++)
  {
    for (j = 0; j < num_values; j ++)
    {
      if (size->bottom == ivalues[j])
      {
        break;
      }
      else if (size->bottom < ivalues[j])
      {
        memmove(ivalues + j + 1, ivalues + j, (size_t)(num_values - j) * sizeof(int));
        ivalues[j] = size->bottom;
        break;
      }
    }

    if (j >= num_values && num_values < (int)(sizeof(ivalues) / sizeof(ivalues[0])))
      ivalues[num_values ++] = size->bottom;
  }

  ippAddIntegers(attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "media-bottom-margin-supported", num_values, ivalues);

  // media-col-database
  for (i = 0, size = pc->sizes; i < pc->num_sizes && i < (int)(sizeof(cvalues) / sizeof(cvalues[0])); i ++)
    cvalues[i] = create_media_col(size, NULL, NULL);

  num_values = i;

  if ((pc->custom_max_width || pc->custom_max_length) && num_values < (int)(sizeof(cvalues) / sizeof(cvalues[0])))
  {
    ipp_t *media_col,			// "media-col" value
          *media_size;			// "media-size" value

    media_col = ippNew();
    media_size = create_media_size(pc->custom_min_width, pc->custom_max_width, pc->custom_min_length, pc->custom_max_length);
    ippAddCollection(media_col, IPP_TAG_ZERO, "media-size", media_size);
    ippDelete(media_size);

    cvalues[num_values ++] = media_col;
  }

  ippAddCollections(attrs, IPP_TAG_PRINTER, "media-col-database", num_values, (const ipp_t **)cvalues);
  for (i = 0; i < num_values; i ++)
    ippDelete(cvalues[i]);

  // media-col-supported
  ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-col-supported", (int)(sizeof(media_col_supported) / sizeof(media_col_supported[0])), NULL, media_col_supported);

  // media-left-margin-supported
  for (i = 0, size = pc->sizes, num_values = 0; i < pc->num_sizes; i ++)
  {
    for (j = 0; j < num_values; j ++)
    {
      if (size->left == ivalues[j])
      {
        break;
      }
      else if (size->left < ivalues[j])
      {
        memmove(ivalues + j + 1, ivalues + j, (size_t)(num_values - j) * sizeof(int));
        ivalues[j] = size->left;
        break;
      }
    }

    if (j >= num_values && num_values < (int)(sizeof(ivalues) / sizeof(ivalues[0])))
      ivalues[num_values ++] = size->left;
  }

  ippAddIntegers(attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "media-left-margin-supported", num_values, ivalues);

  // media-right-margin-supported
  for (i = 0, size = pc->sizes, num_values = 0; i < pc->num_sizes; i ++)
  {
    for (j = 0; j < num_values; j ++)
    {
      if (size->right == ivalues[j])
      {
        break;
      }
      else if (size->right < ivalues[j])
      {
        memmove(ivalues + j + 1, ivalues + j, (size_t)(num_values - j) * sizeof(int));
        ivalues[j] = size->right;
        break;
      }
    }

    if (j >= num_values && num_values < (int)(sizeof(ivalues) / sizeof(ivalues[0])))
      ivalues[num_values ++] = size->right;
  }

  ippAddIntegers(attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "media-right-margin-supported", num_values, ivalues);

  // media-size-supported
  for (i = 0, size = pc->sizes; i < pc->num_sizes && i < (int)(sizeof(cvalues) / sizeof(cvalues[0])); i ++)
    cvalues[i] = create_media_size(size->width, 0, size->length, 0);

  num_values = i;

  if ((pc->custom_max_width || pc->custom_max_length) && num_values < (int)(sizeof(cvalues) / sizeof(cvalues[0])))
    cvalues[num_values ++] = create_media_size(pc->custom_min_width, pc->custom_max_width, pc->custom_min_length, pc->custom_max_length);

  ippAddCollections(attrs, IPP_TAG_PRINTER, "media-size-supported", num_values, (const ipp_t **)cvalues);
  for (i = 0; i < num_values; i ++)
    ippDelete(cvalues[i]);

  // media-source-supported
  for (i = 0; i < pc->num_sources && i < (int)(sizeof(svalues) / sizeof(svalues[0])); i ++)
    svalues[i] = pc->sources[i].pwg;
  num_values = i;

  ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-source-supported", num_values, NULL, svalues);

  // media-top-margin-supported
  for (i = 0, size = pc->sizes, num_values = 0; i < pc->num_sizes; i ++)
  {
    for (j = 0; j < num_values; j ++)
    {
      if (size->top == ivalues[j])
      {
        break;
      }
      else if (size->top < ivalues[j])
      {
        memmove(ivalues + j + 1, ivalues + j, (size_t)(num_values - j) * sizeof(int));
        ivalues[j] = size->top;
        break;
      }
    }

    if (j >= num_values && num_values < (int)(sizeof(ivalues) / sizeof(ivalues[0])))
      ivalues[num_values ++] = size->top;
  }

  ippAddIntegers(attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "media-top-margin-supported", num_values, ivalues);

  // media-type-supported
  for (i = 0; i < pc->num_types && i < (int)(sizeof(svalues) / sizeof(svalues[0])); i ++)
    svalues[i] = pc->types[i].pwg;
  num_values = i;

  ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-type-supported", num_values, NULL, svalues);

  // output-bin-supported
  for (i = 0; i < pc->num_bins && i < (int)(sizeof(svalues) / sizeof(svalues[0])); i ++)
    svalues[i] = pc->bins[i].pwg;
  num_values = i;

  if (num_values > 0)
    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "output-bin-supported", num_values, NULL, svalues);

  // sides-supported
  if (pc->sides_2sided_long)
    ippAddStrings(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "sides-supported", 3, NULL, sides_supported);
  else
    ippAddString(attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "sides-supported", NULL, sides_supported[0]);

  return (attrs);
}


//
// 'create_media_col()' - Create a media-col collection.
//

static ipp_t *				// O - "media-col" collection
create_media_col(pwg_size_t *pwg,	// I - PWG media size/margins
                 const char *source,	// I - "media-source" value or `NULL` for none
                 const char *type)	// I - "media-type" value or `NULL` for none
{
  ipp_t	*media_col,			// "media-col" collection value
	*media_size;			// "media-size" sub-collection value


  media_col = ippNew();

  media_size = create_media_size(pwg->width, 0, pwg->length, 0);
  ippAddCollection(media_col, IPP_TAG_ZERO, "media-size", media_size);
  ippDelete(media_size);

  if (source)
    ippAddString(media_col, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "media-source", NULL, source);

  if (type)
    ippAddString(media_col, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "media-type", NULL, type);

  ippAddInteger(media_col, IPP_TAG_ZERO, IPP_TAG_INTEGER, "media-bottom-margin", pwg->bottom);
  ippAddInteger(media_col, IPP_TAG_ZERO, IPP_TAG_INTEGER, "media-left-margin", pwg->left);
  ippAddInteger(media_col, IPP_TAG_ZERO, IPP_TAG_INTEGER, "media-right-margin", pwg->right);
  ippAddInteger(media_col, IPP_TAG_ZERO, IPP_TAG_INTEGER, "media-top-margin", pwg->top);

  return (media_col);
}


//
// 'create_media_size()' - Create a media-size collection.
//

static ipp_t *				// O - "media-size" collection
create_media_size(int min_width,	// I - Minimum width
                  int max_width,	// I - Maximum width
                  int min_length,	// I - Minimum length
                  int max_length)	// I - Maximum length
{
  ipp_t	*media_size;			// "media-size" sub-collection value


  media_size = ippNew();

  if (max_width)
    ippAddRange(media_size, IPP_TAG_ZERO, "x-dimension", min_width, max_width);
  else
    ippAddInteger(media_size, IPP_TAG_ZERO, IPP_TAG_INTEGER, "x-dimension", min_width);

  if (max_length)
    ippAddRange(media_size, IPP_TAG_ZERO, "y-dimension", min_length, max_length);
  else
    ippAddInteger(media_size, IPP_TAG_ZERO, IPP_TAG_INTEGER, "y-dimension", min_length);

  return (media_size);
}


//
// 'get_elapsed()' - Get the elapsed time in seconds.
//

static double				// O - Elapsed time
get_elapsed(struct timeval *starttime,	// I - Start time
            struct timeval *endtime)	// I - End time
{
  return ((double)(endtime->tv_sec - starttime->tv_sec) + 0.000001 * (double)(endtime->tv_usec - starttime->tv_usec));
}
