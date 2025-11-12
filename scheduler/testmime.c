//
// MIME test program for CUPS.
//
// Copyright © 2020-2025 by OpenPrinting.
// Copyright © 2007-2014 by Apple Inc.
// Copyright © 1997-2006 by Easy Software Products, all rights reserved.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include <cups/test-internal.h>
#include <cups/string-private.h>
#include <cups/dir.h>
#include <cups/debug-private.h>
#include <cups/ppd-private.h>
#include "mime-private.h"


//
// Local functions...
//

static bool	add_ppd_filter(mime_t *mime, mime_type_t *filtertype, const char *filter);
static void	add_ppd_filters(mime_t *mime, ppd_file_t *ppd);
static void	get_file_types(mime_t *mime, mime_type_t *dst);
static void	print_rules(mime_magic_t *rules);
static void	test_filter(mime_t *mime, mime_type_t *src, size_t srcsize, mime_type_t *dst);
static void	type_dir(mime_t *mime, const char *dirname);
static mime_type_t *type_file(mime_t *mime, const char *filename);


//
// 'main()' - Main entry for the test program.
//

int					// O - Exit status
main(int  argc,				// I - Number of command-line args
     char *argv[])			// I - Command-line arguments
{
  int		i;			// Looping vars
  const char	*filter_path;		// Filter path
  char		super[MIME_MAX_SUPER],	// Super-type name
		type[MIME_MAX_TYPE];	// Type name
  mime_t	*mime;			// MIME database
  mime_type_t	*src,			// Source type
		*dst = NULL;		// Destination type
  struct stat	srcinfo;		// Source information
  ppd_file_t	*ppd;			// PPD file
  mime_filter_t	*filter;		// Current filter


  mime        = NULL;
  src         = NULL;
  dst         = NULL;
  ppd         = NULL;
  filter_path = "../filter:/usr/lib/cups/filter:/usr/libexec/cups/filter:/usr/local/lib/cups/filter:/usr/local/libexec/cups/filter";

  srcinfo.st_size = 0;

  for (i = 1; i < argc; i ++)
  {
    if (!strcmp(argv[i], "-d"))
    {
      i ++;

      if (i < argc)
      {
        testBegin("mimeLoad(\"%s\", \"%s\")", argv[i], filter_path);
        mime = mimeLoad(argv[i], filter_path);
        testEnd(mime != NULL);

	if (!mime)
	  return (1);

	if (ppd)
	  add_ppd_filters(mime, ppd);
      }
    }
    else if (!strcmp(argv[i], "-f"))
    {
      i ++;

      if (i < argc)
        filter_path = argv[i];
    }
    else if (!strcmp(argv[i], "-p"))
    {
      i ++;

      if (i < argc)
      {
        testBegin("ppdOpenFile(\"%s\")", argv[i]);
        if ((ppd = ppdOpenFile(argv[i])) != NULL)
        {
	  testEnd(true);
	}
	else
	{
	  ppd_status_t	status;	// PPD error
	  int		linenum;// Line number of error

	  status = ppdLastError(&linenum);
	  testEndMessage(false, "%s on line %d", ppdErrorString(status), linenum);
	  return (1);
	}

	if (mime && ppd)
	  add_ppd_filters(mime, ppd);
      }
    }
    else if (!src && !access(argv[i], 0))
    {
      if (!mime)
      {
        testBegin("mimeLoad(\"../conf\", \"%s\")", filter_path);
	mime = mimeLoad("../conf", filter_path);
	testEnd(mime != NULL);
	if (!mime)
	  return (1);
      }

      if (ppd)
        add_ppd_filters(mime, ppd);

      if (stat(argv[i], &srcinfo))
      {
        perror(argv[i]);
        continue;
      }
      else if (S_ISDIR(srcinfo.st_mode))
      {
        type_dir(mime, argv[i]);
      }
      else if (S_ISREG(srcinfo.st_mode))
      {
        src = type_file(mime, argv[i]);
      }
      else
      {
        fprintf(stderr, "%s: Not a file or directory.\n", argv[i]);
        continue;
      }
    }
    else
    {
      sscanf(argv[i], "%15[^/]/%255s", super, type);
      testBegin("mimeType(\"%s/%s\")", super, type);
      dst = mimeType(mime, super, type);
      testEnd(dst != NULL);

      if (src && dst)
        test_filter(mime, src, (size_t)srcinfo.st_size, dst);
      else if (dst)
        get_file_types(mime, dst);
    }
  }

  if (!mime)
  {
    testBegin("mimeLoad(\"../conf\", \"%s\")", filter_path);
    mime = mimeLoad("../conf", filter_path);
    testEnd(mime != NULL);

    if (!mime)
      return (1);

    if (ppd)
      add_ppd_filters(mime, ppd);
  }

  if (ppd)
  {
    if ((dst = mimeType(mime, "printer", "test")) != NULL)
      get_file_types(mime, dst);
  }
  else if (!src && !dst)
  {
    testMessage("MIME database types:");
    for (src = mimeFirstType(mime); src; src = mimeNextType(mime))
    {
      testMessage("\t%s/%s (%d):", src->super, src->type, src->priority);
      print_rules(src->rules);
    }

    testMessage("MIME database filters:");
    for (filter = mimeFirstFilter(mime); filter; filter = mimeNextFilter(mime))
      testMessage("\t%s/%s to %s/%s: %s (%d)", filter->src->super, filter->src->type, filter->dst->super, filter->dst->type, filter->filter, filter->cost);

    type_dir(mime, "../doc");

    if ((dst = mimeType(mime, "application", "pdf")) != NULL)
      get_file_types(mime, dst);

    if ((dst = mimeType(mime, "application", "vnd.cups-postscript")) != NULL)
      get_file_types(mime, dst);

    if ((dst = mimeType(mime, "image", "pwg-raster")) != NULL)
      get_file_types(mime, dst);
  }

  mimeDelete(mime);

  return (testsPassed ? 0 : 1);
}


//
// 'add_printer_filter()' - Add a printer filter from a PPD.
//

static bool				// O - `true` on success, `false` on error
add_ppd_filter(mime_t      *mime,	// I - MIME database
               mime_type_t *filtertype,	// I - Filter or prefilter MIME type
	       const char  *filter)	// I - Filter to add
{
  char		super[MIME_MAX_SUPER],	// Super-type for filter
		type[MIME_MAX_TYPE],	// Type for filter
		dsuper[MIME_MAX_SUPER],	// Destination super-type for filter
		dtype[MIME_MAX_TYPE],	// Destination type for filter
		dest[MIME_MAX_SUPER + MIME_MAX_TYPE + 2],
					// Destination super/type
		program[1024];		// Program/filter name
  int		cost;			// Cost of filter
  size_t	maxsize = 0;		// Maximum supported file size
  mime_type_t	*temptype,		// MIME type looping var
		*desttype;		// Destination MIME type
  mime_filter_t	*filterptr;		// MIME filter


  // Parse the filter string; it should be in one of the following formats:
  //
  //   source/type cost program
  //   source/type cost maxsize(nnnn) program
  //   source/type dest/type cost program
  //   source/type dest/type cost maxsize(nnnn) program
  if (sscanf(filter, "%15[^/]/%255s%*[ \t]%15[^/]/%255s%d%*[ \t]%1023[^\n]", super, type, dsuper, dtype, &cost, program) == 6)
  {
    snprintf(dest, sizeof(dest), "test/%s/%s", dsuper, dtype);

    if ((desttype = mimeType(mime, "printer", dest)) == NULL)
      desttype = mimeAddType(mime, "printer", dest);
  }
  else if (sscanf(filter, "%15[^/]/%255s%d%*[ \t]%1023[^\n]", super, type, &cost, program) == 4)
  {
    desttype = filtertype;
  }
  else
  {
    testEndMessage(false, "Invalid filter string \"%s\"", filter);
    return (false);
  }

  if (!strncmp(program, "maxsize(", 8))
  {
    char	*ptr;			// Pointer into maxsize(nnnn) program

    maxsize = (size_t)strtoll(program + 8, &ptr, 10);

    if (*ptr != ')')
    {
      testEndMessage(false, "Invalid filter string \"%s\"", filter);
      return (false);
    }

    ptr ++;
    while (_cups_isspace(*ptr))
      ptr ++;

    _cups_strcpy(program, ptr);
  }

  // Add the filter to the MIME database, supporting wildcards as needed...
  for (temptype = mimeFirstType(mime); temptype; temptype = mimeNextType(mime))
  {
    if (((super[0] == '*' && _cups_strcasecmp(temptype->super, "printer")) || !_cups_strcasecmp(temptype->super, super)) && (type[0] == '*' || !_cups_strcasecmp(temptype->type, type)))
    {
      if (desttype != filtertype)
      {
        filterptr = mimeAddFilter(mime, temptype, desttype, cost, program);

        if (!mimeFilterLookup(mime, desttype, filtertype))
          mimeAddFilter(mime, desttype, filtertype, 0, "-");
      }
      else
      {
        filterptr = mimeAddFilter(mime, temptype, filtertype, cost, program);
      }

      if (filterptr)
	filterptr->maxsize = maxsize;
    }
  }

  return (true);
}


//
// 'add_ppd_filters()' - Add all filters from a PPD.
//

static void
add_ppd_filters(mime_t     *mime,	// I - MIME database
                ppd_file_t *ppd)	// I - PPD file
{
  bool		result = true;		// Result of adding filters
  _ppd_cache_t	*pc;			// Cache data for PPD
  const char	*value;			// Filter definition value
  mime_type_t	*filter,		// Filter type
		*prefilter;		// Pre-filter type


  pc = _ppdCacheCreateWithPPD(NULL, ppd);
  if (!pc)
    return;

  testBegin("mimeAddType(\"printer/test\")");
  filter = mimeAddType(mime, "printer", "test");
  testEnd(filter != NULL);

  testBegin("Add PPD Filters");
  if (pc->filters)
  {
    for (value = (const char *)cupsArrayFirst(pc->filters); value; value = (const char *)cupsArrayNext(pc->filters))
      result &= add_ppd_filter(mime, filter, value);
  }
  else
  {
    result &= add_ppd_filter(mime, filter, "application/vnd.cups-raw 0 -");
    result &= add_ppd_filter(mime, filter, "application/vnd.cups-postscript 0 -");
  }

  if (pc->prefilters)
  {
    prefilter = mimeAddType(mime, "prefilter", "test");

    for (value = (const char *)cupsArrayFirst(pc->prefilters); value; value = (const char *)cupsArrayNext(pc->prefilters))
      result &= add_ppd_filter(mime, prefilter, value);
  }

  if (result)
    testEnd(true);
}


//
// 'get_file_types()' - Get the list of source types for a given destination type.
//

static void
get_file_types(mime_t      *mime,	// I - MIME database
               mime_type_t *dst)	// I - Destination MIME media type
{
  cups_array_t	*ftypes;		// Array of filter types
  cups_array_t	*filters;               // Filters
  mime_filter_t	*filter;		// Filter
  cups_array_t	*types;			// Array of supported types
  mime_type_t	*type;			// Current type
  double	start, end;		// Start and end time


  // Scan source types...
  testBegin("mimeGetFilterTypes(%s/%s)", dst->super, dst->type);
  start = cupsGetClock();
  ftypes = mimeGetFilterTypes(mime, dst, /*srcs*/NULL);
  end = cupsGetClock();

  testEndMessage(cupsArrayGetCount(ftypes) > 0, "%d types, %.6f seconds", (int)cupsArrayGetCount(ftypes), end - start);

  // Look for supported formats "the old way"...
  types = cupsArrayNew((cups_array_cb_t)_mimeCompareTypes, /*cb_data*/NULL);

  testBegin("mimeFilter(%s/%s)", dst->super, dst->type);
  start = cupsGetClock();
  for (type = mimeFirstType(mime); type; type = mimeNextType(mime))
  {
    if (!_cups_strcasecmp(type->super, "printer"))
      continue;

    if ((filters = mimeFilter(mime, type, dst, NULL)) != NULL)
    {
      cupsArrayAdd(types, type);
      cupsArrayDelete(filters);
    }
  }
  end = cupsGetClock();

  testEndMessage(cupsArrayGetCount(types) > 0, "%d types, %.6f seconds", (int)cupsArrayGetCount(types), end - start);

  // Compare results...
  testBegin("Compare mimeGetFilterTypes with mimeFilter");
  if (cupsArrayGetCount(types) == cupsArrayGetCount(ftypes))
  {
    testEnd(true);
  }
  else
  {
    testEndMessage(false, "mimeGetFilterTypes returned %d, mimeFilter returned %d", (int)cupsArrayGetCount(ftypes), (int)cupsArrayGetCount(types));
  }

  for (type = (mime_type_t *)cupsArrayGetFirst(ftypes); type; type = (mime_type_t *)cupsArrayGetNext(ftypes))
  {
    if (!cupsArrayFind(types, type))
      testMessage("    %s/%s (only mimeGetFilterTypes)", type->super, type->type);
    else
      testMessage("    %s/%s", type->super, type->type);
  }

  for (type = (mime_type_t *)cupsArrayGetFirst(types); type; type = (mime_type_t *)cupsArrayGetNext(types))
  {
    if (!cupsArrayFind(ftypes, type))
    {
      testMessage("    %s/%s (only mimeFilters)", type->super, type->type);
      if ((filters = mimeFilter(mime, type, dst, NULL)) != NULL)
      {
        for (filter = (mime_filter_t *)cupsArrayGetFirst(filters); filter; filter = (mime_filter_t *)cupsArrayGetNext(filters))
          testMessage("        %s (%s/%s to %s/%s)", filter->filter, filter->src->super, filter->src->type, filter->dst->super, filter->dst->type);

	cupsArrayDelete(filters);
      }
    }
  }

  cupsArrayDelete(types);
  cupsArrayDelete(ftypes);
}


//
// 'print_rules()' - Print the rules for a file type...
//

static void
print_rules(mime_magic_t *rules)	// I - Rules to print
{
  int	i;				// Looping var
  char	line[1024],			// Output line
	*lineptr;			// Pointer into line
  static char	indent[255] = "\t\t";	// Indentation for rules


  if (rules == NULL)
    return;

  while (rules != NULL)
  {
    snprintf(line, sizeof(line), "%s[%p] %s", indent, (void *)rules, rules->invert ? "NOT " : "");
    lineptr = line + strlen(line);

    switch (rules->op)
    {
      case MIME_MAGIC_MATCH :
          snprintf(lineptr, sizeof(line) - (size_t)(lineptr - line), "match(%s)", rules->value.matchv);
          lineptr += strlen(lineptr);
	  break;
      case MIME_MAGIC_LOCALE :
          snprintf(lineptr, sizeof(line) - (size_t)(lineptr - line), "locale(%s)", rules->value.localev);
          lineptr += strlen(lineptr);
	  break;
      case MIME_MAGIC_ASCII :
          snprintf(lineptr, sizeof(line) - (size_t)(lineptr - line), "ascii(%d,%d)", rules->offset, rules->length);
          lineptr += strlen(lineptr);
	  break;
      case MIME_MAGIC_PRINTABLE :
          snprintf(lineptr, sizeof(line) - (size_t)(lineptr - line), "printable(%d,%d)", rules->offset, rules->length);
          lineptr += strlen(lineptr);
	  break;
      case MIME_MAGIC_STRING :
          snprintf(lineptr, sizeof(line) - (size_t)(lineptr - line), "string(%d,", rules->offset);
          lineptr += strlen(lineptr);
	  for (i = 0; i < rules->length; i ++)
	  {
	    if (rules->value.stringv[i] < ' ' || rules->value.stringv[i] > 126)
	    {
	      snprintf(lineptr, sizeof(line) - (size_t)(lineptr - line), "<%02X>", rules->value.stringv[i]);
	      lineptr += strlen(lineptr);
	    }
	    else if (lineptr < (line + sizeof(line) - 1))
	      *lineptr++ = rules->value.stringv[i];
	  }
	  if (lineptr < (line + sizeof(line) - 1))
	    *lineptr++ = ')';
	  *lineptr = '\0';
	  break;
      case MIME_MAGIC_CHAR :
          snprintf(lineptr, sizeof(line) - (size_t)(lineptr - line), "char(%d,%d)", rules->offset, rules->value.charv);
          lineptr += strlen(lineptr);
	  break;
      case MIME_MAGIC_SHORT :
          snprintf(lineptr, sizeof(line) - (size_t)(lineptr - line), "short(%d,%d)", rules->offset, rules->value.shortv);
          lineptr += strlen(lineptr);
	  break;
      case MIME_MAGIC_INT :
          snprintf(lineptr, sizeof(line) - (size_t)(lineptr - line), "int(%d,%d)", rules->offset, rules->value.intv);
          lineptr += strlen(lineptr);
	  break;
      case MIME_MAGIC_CONTAINS :
          snprintf(lineptr, sizeof(line) - (size_t)(lineptr - line), "contains(%d,%d,", rules->offset, rules->region);
          lineptr += strlen(lineptr);
	  for (i = 0; i < rules->length; i ++)
	  {
	    if (rules->value.stringv[i] < ' ' || rules->value.stringv[i] > 126)
	    {
	      snprintf(lineptr, sizeof(line) - (size_t)(lineptr - line), "<%02X>", rules->value.stringv[i]);
	      lineptr += strlen(lineptr);
	    }
	    else if (lineptr < (line + sizeof(line) - 1))
	      *lineptr++ = rules->value.stringv[i];
	  }
	  if (lineptr < (line + sizeof(line) - 1))
	    *lineptr++ = ')';
	  *lineptr = '\0';
	  break;
      default :
	  break;
    }

    if (rules->child != NULL)
    {
      if (rules->op == MIME_MAGIC_OR)
	testMessage("%sOR (", line);
      else
	testMessage("%sAND (", line);

      cupsConcatString(indent, "\t", sizeof(indent));
      print_rules(rules->child);
      indent[strlen(indent) - 1] = '\0';
      testMessage("%s)", indent);
    }
    else
    {
      testMessage("%s", line);
    }

    rules = rules->next;
  }
}


//
// 'test_filter()' - Test filtering.
//

static void
test_filter(mime_t      *mime,		// I - MIME database
            mime_type_t *src,		// I - Source MIME media type
            size_t      srcsize,	// I - Size of source
            mime_type_t *dst)		// I - Destination MIME media type
{
  cups_array_t	*filters;		// Filters
  int		cost;			// Cost


  testBegin("mimeFilter2(src=\"%s/%s\", %ld, dst=\"%s/%s\")", src->super, src->type, (long)srcsize, dst->super, dst->type);
  if ((filters = mimeFilter2(mime, src, srcsize, dst, &cost)) != NULL)
  {
    mime_filter_t *filter;		// Current filter
    char	line[1024],		// Output line
		*lineptr;		// Pointer into line

    for (filter = (mime_filter_t *)cupsArrayGetFirst(filters), lineptr = line; filter; filter = (mime_filter_t *)cupsArrayGetNext(filters))
    {
      if (!strcmp(filter->filter, "-"))
	continue;

      if (lineptr == line)
	cupsCopyString(line, filter->filter, sizeof(line));
      else
	snprintf(lineptr, sizeof(line) - (size_t)(lineptr - line), " | %s", filter->filter);

      lineptr += strlen(lineptr);
    }

    testEndMessage(true, "%d filters, cost %d, %s", (int)cupsArrayGetCount(filters), cost, line);

    cupsArrayDelete(filters);
  }
  else
  {
    testEndMessage(false, "no filters found");
  }
}


//
// 'type_dir()' - Show the MIME types for a given directory.
//

static void
type_dir(mime_t     *mime,		// I - MIME database
         const char *dirname)		// I - Directory
{
  cups_dir_t	*dir;			// Directory
  cups_dentry_t	*dent;			// Directory entry
  char		filename[1024];		// File to type


  dir = cupsDirOpen(dirname);
  if (!dir)
    return;

  while ((dent = cupsDirRead(dir)) != NULL)
  {
    if (dent->filename[0] == '.')
      continue;

    snprintf(filename, sizeof(filename), "%s/%s", dirname, dent->filename);

    if (S_ISDIR(dent->fileinfo.st_mode))
      type_dir(mime, filename);
    else if (S_ISREG(dent->fileinfo.st_mode))
      type_file(mime, filename);
  }

  cupsDirClose(dir);
}


//
// 'type_file()' - Determine the MIME media type of a file.
//

static mime_type_t *			// O - MIME media type
type_file(mime_t     *mime,		// I - MIME database
          const char *filename)		// I - Filename
{
  mime_type_t	*src;			// MIME type
  int		compression;		// Is the file gzipped?


  testBegin("mimeFileType(\"%s\")", filename);
  src = mimeFileType(mime, filename, NULL, &compression);

  if (src)
    testEndMessage(true, "%s/%s%s", src->super, src->type, compression ? " (gzipped)" : "");
  else if ((src = mimeType(mime, "application", "octet-stream")) != NULL)
    testEndMessage(true, "application/octet-stream");
  else
    testEndMessage(false, "unknown");

  return (src);
}
