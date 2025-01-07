/*
 * CUPS destination API test program for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2012-2018 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more information.
 */

/*
 * Include necessary headers...
 */

#include <stdio.h>
#include <errno.h>
#include "cups.h"


/*
 * Local functions...
 */

static int	enum_cb(void *user_data, unsigned flags, cups_dest_t *dest);
static void	localize(http_t *http, cups_dest_t *dest, cups_dinfo_t *dinfo, const char *option, const char *value);
static void	print_file(http_t *http, cups_dest_t *dest, cups_dinfo_t *dinfo, const char *filename, int num_options, cups_option_t *options);
static void	show_conflicts(http_t *http, cups_dest_t *dest, cups_dinfo_t *dinfo, int num_options, cups_option_t *options);
static void	show_default(http_t *http, cups_dest_t *dest, cups_dinfo_t *dinfo, const char *option);
static void	show_media(http_t *http, cups_dest_t *dest, cups_dinfo_t *dinfo, unsigned flags, const char *name);
static void	show_supported(http_t *http, cups_dest_t *dest, cups_dinfo_t *dinfo, const char *option, const char *value);
static void	usage(const char *arg) _CUPS_NORETURN;


/*
 * 'main()' - Main entry.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line arguments */
     char *argv[])			/* I - Command-line arguments */
{
  int		i;			/* Looping var */
  http_t	*http;			/* Connection to destination */
  cups_dest_t	*dest = NULL;		/* Destination */
  cups_dinfo_t	*dinfo;			/* Destination info */
  unsigned	dflags = CUPS_DEST_FLAGS_NONE;
					/* Destination flags */


  if (argc < 2)
    return (0);

  if (!strcmp(argv[1], "--get"))
  {
    cups_dest_t	*dests;			/* Destinations */
    int		num_dests = cupsGetDests2(CUPS_HTTP_DEFAULT, &dests);
					/* Number of destinations */

    for (i = 0; i < num_dests; i ++)
      enum_cb(NULL, 0, dests + i);

    cupsFreeDests(num_dests, dests);
    return (0);
  }
  else if (!strcmp(argv[1], "--enum"))
  {
    cups_ptype_t	type = 0,	/* Printer type filter */
			mask = 0;	/* Printer type mask */


    for (i = 2; i < argc; i ++)
    {
      if (!strcmp(argv[i], "grayscale"))
      {
        type |= CUPS_PRINTER_BW;
	mask |= CUPS_PRINTER_BW;
      }
      else if (!strcmp(argv[i], "color"))
      {
        type |= CUPS_PRINTER_COLOR;
	mask |= CUPS_PRINTER_COLOR;
      }
      else if (!strcmp(argv[i], "duplex"))
      {
        type |= CUPS_PRINTER_DUPLEX;
	mask |= CUPS_PRINTER_DUPLEX;
      }
      else if (!strcmp(argv[i], "staple"))
      {
        type |= CUPS_PRINTER_STAPLE;
	mask |= CUPS_PRINTER_STAPLE;
      }
      else if (!strcmp(argv[i], "small"))
      {
        type |= CUPS_PRINTER_SMALL;
	mask |= CUPS_PRINTER_SMALL;
      }
      else if (!strcmp(argv[i], "medium"))
      {
        type |= CUPS_PRINTER_MEDIUM;
	mask |= CUPS_PRINTER_MEDIUM;
      }
      else if (!strcmp(argv[i], "large"))
      {
        type |= CUPS_PRINTER_LARGE;
	mask |= CUPS_PRINTER_LARGE;
      }
      else
        usage(argv[i]);
    }

    cupsEnumDests(CUPS_DEST_FLAGS_NONE, 5000, NULL, type, mask, enum_cb, NULL);

    return (0);
  }

  i = 1;
  if (!strcmp(argv[i], "--device"))
  {
    dflags = CUPS_DEST_FLAGS_DEVICE;
    i ++;
  }

  if (!strncmp(argv[i], "ipp://", 6) || !strncmp(argv[i], "ipps://", 7))
    dest = cupsGetDestWithURI(NULL, argv[i]);
  else if (!strcmp(argv[i], "default"))
  {
    dest = cupsGetNamedDest(CUPS_HTTP_DEFAULT, NULL, NULL);
    if (dest && dest->instance)
      printf("default is \"%s/%s\".\n", dest->name, dest->instance);
    else if (dest)
      printf("default is \"%s\".\n", dest->name);
    else
      puts("no default destination.");
  }
  else
    dest = cupsGetNamedDest(CUPS_HTTP_DEFAULT, argv[i], NULL);

  if (!dest)
  {
    printf("testdest: Unable to get destination \"%s\": %s\n", argv[i], cupsLastErrorString());
    return (1);
  }

  i ++;

  if ((http = cupsConnectDest(dest, dflags, 30000, NULL, NULL, 0, NULL, NULL)) == NULL)
  {
    printf("testdest: Unable to connect to destination \"%s\": %s\n", dest->name, cupsLastErrorString());
    return (1);
  }

  if ((dinfo = cupsCopyDestInfo(http, dest)) == NULL)
  {
    printf("testdest: Unable to get information for destination \"%s\": %s\n", dest->name, cupsLastErrorString());
    return (1);
  }

  if (i == argc || !strcmp(argv[i], "supported"))
  {
    i ++;

    if ((i + 1) < argc)
      show_supported(http, dest, dinfo, argv[i], argv[i + 1]);
    else if (argc > 2)
      show_supported(http, dest, dinfo, argv[i], NULL);
    else
      show_supported(http, dest, dinfo, NULL, NULL);
  }
  else if (!strcmp(argv[i], "conflicts") && (i + 1) < argc)
  {
    int			num_options = 0;/* Number of options */
    cups_option_t	*options = NULL;/* Options */

    for (i ++; i < argc; i ++)
      num_options = cupsParseOptions(argv[i], num_options, &options);

    show_conflicts(http, dest, dinfo, num_options, options);
  }
  else if (!strcmp(argv[i], "default") && (i + 1) < argc)
  {
    show_default(http, dest, dinfo, argv[i + 1]);
  }
  else if (!strcmp(argv[i], "localize"))
  {
    i ++;
    if ((i + 1) < argc)
      localize(http, dest, dinfo, argv[i], argv[i + 1]);
    else if (argc > 2)
      localize(http, dest, dinfo, argv[i], NULL);
    else
      localize(http, dest, dinfo, NULL, NULL);
  }
  else if (!strcmp(argv[i], "media"))
  {
    const char	*name = NULL;		/* Media name, if any */
    unsigned	flags = CUPS_MEDIA_FLAGS_DEFAULT;
					/* Media selection flags */

    for (i ++; i < argc; i ++)
    {
      if (!strcmp(argv[i], "borderless"))
	flags = CUPS_MEDIA_FLAGS_BORDERLESS;
      else if (!strcmp(argv[i], "duplex"))
	flags = CUPS_MEDIA_FLAGS_DUPLEX;
      else if (!strcmp(argv[i], "exact"))
	flags = CUPS_MEDIA_FLAGS_EXACT;
      else if (!strcmp(argv[i], "ready"))
	flags = CUPS_MEDIA_FLAGS_READY;
      else if (name)
        usage(argv[i]);
      else
        name = argv[i];
    }

    show_media(http, dest, dinfo, flags, name);
  }
  else if (!strcmp(argv[i], "print") && (i + 1) < argc)
  {
    int			num_options = 0;/* Number of options */
    cups_option_t	*options = NULL;/* Options */
    const char		*filename = argv[i + 1];

    for (i += 2; i < argc; i ++)
      num_options = cupsParseOptions(argv[i], num_options, &options);

    print_file(http, dest, dinfo, filename, num_options, options);
  }
  else
    usage(argv[i]);

  return (0);
}


/*
 * 'enum_cb()' - Print the results from the enumeration of destinations.
 */

static int				/* O - 1 to continue */
enum_cb(void        *user_data,		/* I - User data (unused) */
        unsigned    flags,		/* I - Flags */
	cups_dest_t *dest)		/* I - Destination */
{
  int	i;				/* Looping var */


  (void)user_data;
  (void)flags;

  if (dest->instance)
    printf("%s%s/%s%s:\n", (flags & CUPS_DEST_FLAGS_REMOVED) ? "REMOVE " : "", dest->name, dest->instance, dest->is_default ? " (Default)" : "");
  else
    printf("%s%s%s:\n", (flags & CUPS_DEST_FLAGS_REMOVED) ? "REMOVE " : "", dest->name, dest->is_default ? " (Default)" : "");

  for (i = 0; i < dest->num_options; i ++)
    printf("    %s=\"%s\"\n", dest->options[i].name, dest->options[i].value);

  puts("");

  return (1);
}


/*
 * 'localize()' - Localize an option and value.
 */

static void
localize(http_t       *http,		/* I - Connection to destination */
         cups_dest_t  *dest,		/* I - Destination */
	 cups_dinfo_t *dinfo,		/* I - Destination information */
         const char   *option,		/* I - Option */
	 const char   *value)		/* I - Value, if any */
{
  ipp_attribute_t	*attr;		/* Attribute */
  int			i,		/* Looping var */
			count;		/* Number of values */


  if (!option)
  {
    attr = cupsFindDestSupported(http, dest, dinfo, "job-creation-attributes");
    if (attr)
    {
      count = ippGetCount(attr);
      for (i = 0; i < count; i ++)
        localize(http, dest, dinfo, ippGetString(attr, i, NULL), NULL);
    }
    else
    {
      static const char * const options[] =
      {					/* List of standard options */
        CUPS_COPIES,
	CUPS_FINISHINGS,
	CUPS_MEDIA,
	CUPS_NUMBER_UP,
	CUPS_ORIENTATION,
	CUPS_PRINT_COLOR_MODE,
	CUPS_PRINT_QUALITY,
	CUPS_SIDES
      };

      puts("No job-creation-attributes-supported attribute, probing instead.");

      for (i = 0; i < (int)(sizeof(options) / sizeof(options[0])); i ++)
        if (cupsCheckDestSupported(http, dest, dinfo, options[i], NULL))
	  localize(http, dest, dinfo, options[i], NULL);
    }
  }
  else if (!value)
  {
    printf("%s (%s)\n", option, cupsLocalizeDestOption(http, dest, dinfo, option));

    if ((attr = cupsFindDestSupported(http, dest, dinfo, option)) != NULL)
    {
      count = ippGetCount(attr);

      switch (ippGetValueTag(attr))
      {
        case IPP_TAG_INTEGER :
	    for (i = 0; i < count; i ++)
              printf("  %d\n", ippGetInteger(attr, i));
	    break;

        case IPP_TAG_ENUM :
	    for (i = 0; i < count; i ++)
              printf("  %s\n", ippEnumString(option, ippGetInteger(attr, i)));
	    break;

        case IPP_TAG_RANGE :
	    for (i = 0; i < count; i ++)
	    {
	      int upper, lower = ippGetRange(attr, i, &upper);

              printf("  %d-%d\n", lower, upper);
	    }
	    break;

        case IPP_TAG_RESOLUTION :
	    for (i = 0; i < count; i ++)
	    {
	      int xres, yres;
	      ipp_res_t units;
	      xres = ippGetResolution(attr, i, &yres, &units);

              if (xres == yres)
                printf("  %d%s\n", xres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
	      else
                printf("  %dx%d%s\n", xres, yres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
	    }
	    break;

	case IPP_TAG_TEXTLANG :
	case IPP_TAG_NAMELANG :
	case IPP_TAG_TEXT :
	case IPP_TAG_NAME :
	case IPP_TAG_KEYWORD :
	case IPP_TAG_URI :
	case IPP_TAG_URISCHEME :
	case IPP_TAG_CHARSET :
	case IPP_TAG_LANGUAGE :
	case IPP_TAG_MIMETYPE :
	    for (i = 0; i < count; i ++)
              printf("  %s (%s)\n", ippGetString(attr, i, NULL), cupsLocalizeDestValue(http, dest, dinfo, option, ippGetString(attr, i, NULL)));
	    break;

        case IPP_TAG_STRING :
	    for (i = 0; i < count; i ++)
	    {
	      int j, len;
	      unsigned char *data = ippGetOctetString(attr, i, &len);

              fputs("  ", stdout);
	      for (j = 0; j < len; j ++)
	      {
	        if (data[j] < ' ' || data[j] >= 0x7f)
		  printf("<%02X>", data[j]);
		else
		  putchar(data[j]);
              }
              putchar('\n');
	    }
	    break;

        case IPP_TAG_BOOLEAN :
	    break;

        default :
	    printf("  %s\n", ippTagString(ippGetValueTag(attr)));
	    break;
      }
    }

  }
  else
    puts(cupsLocalizeDestValue(http, dest, dinfo, option, value));
}


/*
 * 'print_file()' - Print a file.
 */

static void
print_file(http_t        *http,		/* I - Connection to destination */
           cups_dest_t   *dest,		/* I - Destination */
	   cups_dinfo_t  *dinfo,	/* I - Destination information */
           const char    *filename,	/* I - File to print */
	   int           num_options,	/* I - Number of options */
	   cups_option_t *options)	/* I - Options */
{
  cups_file_t	*fp;			/* File to print */
  int		job_id;			/* Job ID */
  const char	*title;			/* Title of job */
  char		buffer[32768];		/* File buffer */
  ssize_t	bytes;			/* Bytes read/to write */


  if ((fp = cupsFileOpen(filename, "r")) == NULL)
  {
    printf("Unable to open \"%s\": %s\n", filename, strerror(errno));
    return;
  }

  if ((title = strrchr(filename, '/')) != NULL)
    title ++;
  else
    title = filename;

  if (cupsCreateDestJob(http, dest, dinfo, &job_id, title, num_options, options) > IPP_STATUS_OK_IGNORED_OR_SUBSTITUTED)
  {
    printf("Unable to create job: %s\n", cupsLastErrorString());
    cupsFileClose(fp);
    return;
  }

  printf("Created job ID: %d\n", job_id);

  if (cupsStartDestDocument(http, dest, dinfo, job_id, title, CUPS_FORMAT_AUTO, 0, NULL, 1) != HTTP_STATUS_CONTINUE)
  {
    printf("Unable to send document: %s\n", cupsLastErrorString());
    cupsFileClose(fp);
    return;
  }

  while ((bytes = cupsFileRead(fp, buffer, sizeof(buffer))) > 0)
  {
    if (cupsWriteRequestData(http, buffer, (size_t)bytes) != HTTP_STATUS_CONTINUE)
    {
      printf("Unable to write document data: %s\n", cupsLastErrorString());
      break;
    }
  }

  cupsFileClose(fp);

  if (cupsFinishDestDocument(http, dest, dinfo) > IPP_STATUS_OK_IGNORED_OR_SUBSTITUTED)
  {
    printf("Unable to send document: %s\n", cupsLastErrorString());
    return;
  }

  puts("Job queued.");
}


/*
 * 'show_conflicts()' - Show conflicts for selected options.
 */

static void
show_conflicts(
    http_t        *http,		/* I - Connection to destination */
    cups_dest_t   *dest,		/* I - Destination */
    cups_dinfo_t  *dinfo,		/* I - Destination information */
    int           num_options,		/* I - Number of options */
    cups_option_t *options)		/* I - Options */
{
  (void)http;
  (void)dest;
  (void)dinfo;
  (void)num_options;
  (void)options;
}


/*
 * 'show_default()' - Show default value for option.
 */

static void
show_default(http_t       *http,	/* I - Connection to destination */
	     cups_dest_t  *dest,	/* I - Destination */
	     cups_dinfo_t *dinfo,	/* I - Destination information */
	     const char  *option)	/* I - Option */
{
  if (!strcmp(option, "media"))
  {
   /*
    * Show default media option...
    */

    cups_size_t size;                   /* Media size information */

    if (cupsGetDestMediaDefault(http, dest, dinfo, CUPS_MEDIA_FLAGS_DEFAULT, &size))
      printf("%s (%.2fx%.2fmm, margins=[%.2f %.2f %.2f %.2f])\n", size.media, size.width * 0.01, size.length * 0.01, size.left * 0.01, size.bottom * 0.01, size.right * 0.01, size.top * 0.01);
     else
       puts("FAILED");
  }
  else
  {
   /*
    * Show default other option...
    */

    ipp_attribute_t *defattr;           /* Default attribute */

    if ((defattr = cupsFindDestDefault(http, dest, dinfo, option)) != NULL)
    {
      char value[1024];                 /* Value of default attribute */

      ippAttributeString(defattr, value, sizeof(value));
      puts(value);
    }
    else
      puts("FAILED");
  }
}


/*
 * 'show_media()' - Show available media.
 */

static void
show_media(http_t       *http,		/* I - Connection to destination */
	   cups_dest_t  *dest,		/* I - Destination */
	   cups_dinfo_t *dinfo,		/* I - Destination information */
	   unsigned     flags,		/* I - Media flags */
	   const char   *name)		/* I - Size name */
{
  int		i,			/* Looping var */
		count;			/* Number of sizes */
  cups_size_t	size;			/* Media size info */


  if (name)
  {
    double	dw, dl;			/* Width and length from name */
    char	units[32];		/* Units */
    int		width,			/* Width in 100ths of millimeters */
		length;			/* Length in 100ths of millimeters */


    if (sscanf(name, "%lfx%lf%31s", &dw, &dl, units) == 3)
    {
      if (!strcmp(units, "in"))
      {
        width  = (int)(dw * 2540.0);
	length = (int)(dl * 2540.0);
      }
      else if (!strcmp(units, "mm"))
      {
        width  = (int)(dw * 100.0);
        length = (int)(dl * 100.0);
      }
      else
      {
        puts("  bad units in size");
	return;
      }

      if (cupsGetDestMediaBySize(http, dest, dinfo, width, length, flags, &size))
      {
	printf("  %s (%s) %dx%d B%d L%d R%d T%d\n", size.media, cupsLocalizeDestMedia(http, dest, dinfo, flags, &size), size.width, size.length, size.bottom, size.left, size.right, size.top);
      }
      else
      {
	puts("  not supported");
      }
    }
    else if (cupsGetDestMediaByName(http, dest, dinfo, name, flags, &size))
    {
      printf("  %s (%s) %dx%d B%d L%d R%d T%d\n", size.media, cupsLocalizeDestMedia(http, dest, dinfo, flags, &size), size.width, size.length, size.bottom, size.left, size.right, size.top);
    }
    else
    {
      puts("  not supported");
    }
  }
  else
  {
    count = cupsGetDestMediaCount(http, dest, dinfo, flags);
    printf("%d size%s:\n", count, count == 1 ? "" : "s");

    for (i = 0; i < count; i ++)
    {
      if (cupsGetDestMediaByIndex(http, dest, dinfo, i, flags, &size))
        printf("  %s (%s) %dx%d B%d L%d R%d T%d\n", size.media, cupsLocalizeDestMedia(http, dest, dinfo, flags, &size), size.width, size.length, size.bottom, size.left, size.right, size.top);
      else
        puts("  error");
    }
  }
}


/*
 * 'show_supported()' - Show supported options, values, etc.
 */

static void
show_supported(http_t       *http,	/* I - Connection to destination */
	       cups_dest_t  *dest,	/* I - Destination */
	       cups_dinfo_t *dinfo,	/* I - Destination information */
	       const char   *option,	/* I - Option, if any */
	       const char   *value)	/* I - Value, if any */
{
  ipp_attribute_t	*attr;		/* Attribute */
  int			i,		/* Looping var */
			count;		/* Number of values */


  if (!option)
  {
    attr = cupsFindDestSupported(http, dest, dinfo, "job-creation-attributes");
    if (attr)
    {
      count = ippGetCount(attr);
      for (i = 0; i < count; i ++)
        show_supported(http, dest, dinfo, ippGetString(attr, i, NULL), NULL);
    }
    else
    {
      static const char * const options[] =
      {					/* List of standard options */
        CUPS_COPIES,
	CUPS_FINISHINGS,
	CUPS_MEDIA,
	CUPS_NUMBER_UP,
	CUPS_ORIENTATION,
	CUPS_PRINT_COLOR_MODE,
	CUPS_PRINT_QUALITY,
	CUPS_SIDES
      };

      puts("No job-creation-attributes-supported attribute, probing instead.");

      for (i = 0; i < (int)(sizeof(options) / sizeof(options[0])); i ++)
        if (cupsCheckDestSupported(http, dest, dinfo, options[i], NULL))
	  show_supported(http, dest, dinfo, options[i], NULL);
    }
  }
  else if (!value)
  {
    printf("%s (%s - %s)\n", option, cupsLocalizeDestOption(http, dest, dinfo, option), cupsCheckDestSupported(http, dest, dinfo, option, NULL) ? "supported" : "not-supported");

    if ((attr = cupsFindDestSupported(http, dest, dinfo, option)) != NULL)
    {
      count = ippGetCount(attr);

      switch (ippGetValueTag(attr))
      {
        case IPP_TAG_INTEGER :
	    for (i = 0; i < count; i ++)
              printf("  %d\n", ippGetInteger(attr, i));
	    break;

        case IPP_TAG_ENUM :
	    for (i = 0; i < count; i ++)
	    {
	      int val = ippGetInteger(attr, i);
	      char valstr[256];

              snprintf(valstr, sizeof(valstr), "%d", val);
              printf("  %s (%s)\n", ippEnumString(option, ippGetInteger(attr, i)), cupsLocalizeDestValue(http, dest, dinfo, option, valstr));
            }
	    break;

        case IPP_TAG_RANGE :
	    for (i = 0; i < count; i ++)
	    {
	      int upper, lower = ippGetRange(attr, i, &upper);

              printf("  %d-%d\n", lower, upper);
	    }
	    break;

        case IPP_TAG_RESOLUTION :
	    for (i = 0; i < count; i ++)
	    {
	      int xres, yres;
	      ipp_res_t units;
	      xres = ippGetResolution(attr, i, &yres, &units);

              if (xres == yres)
                printf("  %d%s\n", xres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
	      else
                printf("  %dx%d%s\n", xres, yres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
	    }
	    break;

	case IPP_TAG_KEYWORD :
	    for (i = 0; i < count; i ++)
              printf("  %s (%s)\n", ippGetString(attr, i, NULL), cupsLocalizeDestValue(http, dest, dinfo, option, ippGetString(attr, i, NULL)));
	    break;

	case IPP_TAG_TEXTLANG :
	case IPP_TAG_NAMELANG :
	case IPP_TAG_TEXT :
	case IPP_TAG_NAME :
	case IPP_TAG_URI :
	case IPP_TAG_URISCHEME :
	case IPP_TAG_CHARSET :
	case IPP_TAG_LANGUAGE :
	case IPP_TAG_MIMETYPE :
	    for (i = 0; i < count; i ++)
              printf("  %s\n", ippGetString(attr, i, NULL));
	    break;

        case IPP_TAG_STRING :
	    for (i = 0; i < count; i ++)
	    {
	      int j, len;
	      unsigned char *data = ippGetOctetString(attr, i, &len);

              fputs("  ", stdout);
	      for (j = 0; j < len; j ++)
	      {
	        if (data[j] < ' ' || data[j] >= 0x7f)
		  printf("<%02X>", data[j]);
		else
		  putchar(data[j]);
              }
              putchar('\n');
	    }
	    break;

        case IPP_TAG_BOOLEAN :
	    break;

        default :
	    printf("  %s\n", ippTagString(ippGetValueTag(attr)));
	    break;
      }
    }

  }
  else if (cupsCheckDestSupported(http, dest, dinfo, option, value))
    puts("YES");
  else
    puts("NO");
}


/*
 * 'usage()' - Show program usage.
 */

static void
usage(const char *arg)			/* I - Argument for usage message */
{
  if (arg)
    printf("testdest: Unknown option \"%s\".\n", arg);

  puts("Usage:");
  puts("  ./testdest [--device] name [operation ...]");
  puts("  ./testdest [--device] ipp://... [operation ...]");
  puts("  ./testdest [--device] ipps://... [operation ...]");
  puts("  ./testdest --get");
  puts("  ./testdest --enum [grayscale] [color] [duplex] [staple] [small]\n"
       "                    [medium] [large]");
  puts("");
  puts("Operations:");
  puts("  conflicts options");
  puts("  default option");
  puts("  localize option [value]");
  puts("  media [borderless] [duplex] [exact] [ready] [name or size]");
  puts("  print filename [options]");
  puts("  supported [option [value]]");

  exit(arg != NULL);
}
