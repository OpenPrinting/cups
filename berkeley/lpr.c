/*
 * "lpr" command for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2007-2019 by Apple Inc.
 * Copyright © 1997-2007 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include <cups/cups-private.h>


/*
 * Local functions...
 */

static void	usage(void) _CUPS_NORETURN;


/*
 * 'main()' - Parse options and send files for printing.
 */

int
main(int  argc,				/* I - Number of command-line arguments */
     char *argv[])			/* I - Command-line arguments */
{
  int		i, j;			/* Looping var */
  int		job_id;			/* Job ID */
  char		ch;			/* Option character */
  char		*printer,		/* Destination printer or class */
		*instance,		/* Instance */
		*opt;			/* Option pointer */
  const char	*title;			/* Job title */
  int		num_copies;		/* Number of copies per file */
  int		num_files;		/* Number of files to print */
  const char	*files[1000];		/* Files to print */
  cups_dest_t	*dest;			/* Selected destination */
  int		num_options;		/* Number of options */
  cups_option_t	*options;		/* Options */
  int		deletefile;		/* Delete file after print? */
  char		buffer[8192];		/* Copy buffer */


  _cupsSetLocale(argv);

  deletefile  = 0;
  printer     = NULL;
  dest        = NULL;
  num_options = 0;
  options     = NULL;
  num_files   = 0;
  title       = NULL;

  for (i = 1; i < argc; i ++)
  {
    if (!strcmp(argv[i], "--help"))
      usage();
    else if (argv[i][0] == '-')
    {
      for (opt = argv[i] + 1; *opt; opt ++)
      {
	switch (ch = *opt)
	{
	  case 'E' : /* Encrypt */
#ifdef HAVE_TLS
	      cupsSetEncryption(HTTP_ENCRYPT_REQUIRED);
#else
	      _cupsLangPrintf(stderr, _("%s: Sorry, no encryption support."), argv[0]);
#endif /* HAVE_TLS */
	      break;

	  case 'U' : /* Username */
	      if (opt[1] != '\0')
	      {
		cupsSetUser(opt + 1);
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;
		if (i >= argc)
		{
		  _cupsLangPrintf(stderr, _("%s: Error - expected username after \"-U\" option."), argv[0]);
		  usage();
		}

		cupsSetUser(argv[i]);
	      }
	      break;

	  case 'H' : /* Connect to host */
	      if (opt[1] != '\0')
	      {
		cupsSetServer(opt + 1);
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;

		if (i >= argc)
		{
		  _cupsLangPrintf(stderr, _("%s: Error - expected hostname after \"-H\" option."), argv[0]);
		  usage();
		}
		else
		  cupsSetServer(argv[i]);
	      }
	      break;

	  case '1' : /* TROFF font set 1 */
	  case '2' : /* TROFF font set 2 */
	  case '3' : /* TROFF font set 3 */
	  case '4' : /* TROFF font set 4 */
	  case 'i' : /* indent */
	  case 'w' : /* width */
	      if (opt[1] != '\0')
	      {
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;

		if (i >= argc)
		{
		  _cupsLangPrintf(stderr,
				  _("%s: Error - expected value after \"-%c\" "
				    "option."), argv[0], ch);
		  usage();
		}
	      }

	  case 'c' : /* CIFPLOT */
	  case 'd' : /* DVI */
	  case 'f' : /* FORTRAN */
	  case 'g' : /* plot */
	  case 'n' : /* Ditroff */
	  case 't' : /* Troff */
	  case 'v' : /* Raster image */
	      _cupsLangPrintf(stderr, _("%s: Warning - \"%c\" format modifier not supported - output may not be correct."), argv[0], ch);
	      break;

	  case 'o' : /* Option */
	      if (opt[1] != '\0')
	      {
		num_options = cupsParseOptions(opt + 1, num_options, &options);
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;
		if (i >= argc)
		{
		  _cupsLangPrintf(stderr, _("%s: Error - expected option=value after \"-o\" option."), argv[0]);
		  usage();
		}

		num_options = cupsParseOptions(argv[i], num_options, &options);
	      }
	      break;

	  case 'l' : /* Literal/raw */
	      num_options = cupsAddOption("raw", "true", num_options, &options);
	      break;

	  case 'p' : /* Prettyprint */
	      num_options = cupsAddOption("prettyprint", "true", num_options, &options);
	      break;

	  case 'h' : /* Suppress burst page */
	      num_options = cupsAddOption("job-sheets", "none", num_options, &options);
	      break;

	  case 's' : /* Don't use symlinks */
	      break;

	  case 'm' : /* Mail on completion */
	      {
		char	email[1024];	/* EMail address */

		snprintf(email, sizeof(email), "mailto:%s@%s", cupsUser(), httpGetHostname(NULL, buffer, sizeof(buffer)));
		num_options = cupsAddOption("notify-recipient-uri", email, num_options, &options);
	      }
	      break;

	  case 'q' : /* Queue file but don't print */
	      num_options = cupsAddOption("job-hold-until", "indefinite", num_options, &options);
	      break;

	  case 'r' : /* Remove file after printing */
	      deletefile = 1;
	      break;

	  case 'P' : /* Destination printer or class */
	      if (opt[1] != '\0')
	      {
		printer = opt + 1;
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;
		if (i >= argc)
		{
		  _cupsLangPrintf(stderr, _("%s: Error - expected destination after \"-P\" option."), argv[0]);
		  usage();
		}

		printer = argv[i];
	      }

	      if ((instance = strrchr(printer, '/')) != NULL)
		*instance++ = '\0';

	      if ((dest = cupsGetNamedDest(NULL, printer, instance)) != NULL)
	      {
		for (j = 0; j < dest->num_options; j ++)
		  if (cupsGetOption(dest->options[j].name, num_options,
				    options) == NULL)
		    num_options = cupsAddOption(dest->options[j].name,
						dest->options[j].value,
						num_options, &options);
	      }
	      else if (cupsLastError() == IPP_STATUS_ERROR_BAD_REQUEST ||
		       cupsLastError() == IPP_STATUS_ERROR_VERSION_NOT_SUPPORTED)
	      {
		_cupsLangPrintf(stderr, _("%s: Error - add '/version=1.1' to server name."), argv[0]);
		return (1);
	      }
	      else if (cupsLastError() == IPP_STATUS_ERROR_NOT_FOUND)
	      {
		_cupsLangPrintf(stderr,
				_("%s: Error - The printer or class does not exist."), argv[0]);
		return (1);
	      }
	      break;

	  case '#' : /* Number of copies */
	      if (opt[1] != '\0')
	      {
		num_copies = atoi(opt + 1);
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;
		if (i >= argc)
		{
		  _cupsLangPrintf(stderr, _("%s: Error - expected copies after \"-#\" option."), argv[0]);
		  usage();
		}

		num_copies = atoi(argv[i]);
	      }

	      if (num_copies < 1)
	      {
		_cupsLangPrintf(stderr, _("%s: Error - copies must be 1 or more."), argv[0]);
		return (1);
	      }

	      num_options = cupsAddIntegerOption("copies", num_copies, num_options, &options);
	      break;

	  case 'C' : /* Class */
	  case 'J' : /* Job name */
	  case 'T' : /* Title */
	      if (opt[1] != '\0')
	      {
		title = opt + 1;
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;
		if (i >= argc)
		{
		  _cupsLangPrintf(stderr, _("%s: Error - expected name after \"-%c\" option."), argv[0], ch);
		  usage();
		}

		title = argv[i];
	      }
	      break;

	  default :
	      _cupsLangPrintf(stderr, _("%s: Error - unknown option \"%c\"."), argv[0], *opt);
	      return (1);
	}
      }
    }
    else if (num_files < 1000)
    {
     /*
      * Print a file...
      */

      if (access(argv[i], R_OK) != 0)
      {
        _cupsLangPrintf(stderr,
	                _("%s: Error - unable to access \"%s\" - %s"),
		        argv[0], argv[i], strerror(errno));
        return (1);
      }

      files[num_files] = argv[i];
      num_files ++;

      if (title == NULL)
      {
        if ((title = strrchr(argv[i], '/')) != NULL)
	  title ++;
	else
          title = argv[i];
      }
    }
    else
    {
      _cupsLangPrintf(stderr, _("%s: Error - too many files - \"%s\"."), argv[0], argv[i]);
    }
  }

 /*
  * See if we have any files to print; if not, print from stdin...
  */

  if (printer == NULL)
  {
    if ((dest = cupsGetNamedDest(NULL, NULL, NULL)) != NULL)
    {
      printer = dest->name;

      for (j = 0; j < dest->num_options; j ++)
	if (cupsGetOption(dest->options[j].name, num_options, options) == NULL)
	  num_options = cupsAddOption(dest->options[j].name,
		                      dest->options[j].value,
				      num_options, &options);
    }
    else if (cupsLastError() == IPP_STATUS_ERROR_BAD_REQUEST ||
	     cupsLastError() == IPP_STATUS_ERROR_VERSION_NOT_SUPPORTED)
    {
      _cupsLangPrintf(stderr,
		      _("%s: Error - add '/version=1.1' to server "
			"name."), argv[0]);
      return (1);
    }
  }

  if (printer == NULL)
  {
    if (!cupsGetNamedDest(NULL, NULL, NULL) && cupsLastError() == IPP_STATUS_ERROR_NOT_FOUND)
      _cupsLangPrintf(stderr, _("%s: Error - %s"), argv[0], cupsLastErrorString());
    else
      _cupsLangPrintf(stderr, _("%s: Error - scheduler not responding."), argv[0]);

    return (1);
  }

  if (num_files > 0)
  {
    job_id = cupsPrintFiles(printer, num_files, files, title, num_options, options);

    if (deletefile && job_id > 0)
    {
     /*
      * Delete print files after printing...
      */

      for (i = 0; i < num_files; i ++)
        unlink(files[i]);
    }
  }
  else if ((job_id = cupsCreateJob(CUPS_HTTP_DEFAULT, printer,
                                   title ? title : "(stdin)",
                                   num_options, options)) > 0)
  {
    http_status_t	status;		/* Write status */
    const char		*format;	/* Document format */
    ssize_t		bytes;		/* Bytes read */

    if (cupsGetOption("raw", num_options, options))
      format = CUPS_FORMAT_RAW;
    else if ((format = cupsGetOption("document-format", num_options,
                                     options)) == NULL)
      format = CUPS_FORMAT_AUTO;

    status = cupsStartDocument(CUPS_HTTP_DEFAULT, printer, job_id, NULL,
                               format, 1);

    while (status == HTTP_CONTINUE &&
           (bytes = read(0, buffer, sizeof(buffer))) > 0)
      status = cupsWriteRequestData(CUPS_HTTP_DEFAULT, buffer, (size_t)bytes);

    if (status != HTTP_CONTINUE)
    {
      _cupsLangPrintf(stderr, _("%s: Error - unable to queue from stdin - %s."),
		      argv[0], httpStatus(status));
      cupsFinishDocument(CUPS_HTTP_DEFAULT, printer);
      cupsCancelJob2(CUPS_HTTP_DEFAULT, printer, job_id, 0);
      return (1);
    }

    if (cupsFinishDocument(CUPS_HTTP_DEFAULT, printer) != IPP_OK)
    {
      _cupsLangPrintf(stderr, "%s: %s", argv[0], cupsLastErrorString());
      cupsCancelJob2(CUPS_HTTP_DEFAULT, printer, job_id, 0);
      return (1);
    }
  }

  if (job_id < 1)
  {
    _cupsLangPrintf(stderr, "%s: %s", argv[0], cupsLastErrorString());
    return (1);
  }

  return (0);
}


/*
 * 'usage()' - Show program usage and exit.
 */

static void
usage(void)
{
  _cupsLangPuts(stdout, _("Usage: lpr [options] [file(s)]"));
  _cupsLangPuts(stdout, _("Options:"));
  _cupsLangPuts(stdout, _("-# num-copies           Specify the number of copies to print"));
  _cupsLangPuts(stdout, _("-E                      Encrypt the connection to the server"));
  _cupsLangPuts(stdout, _("-H server[:port]        Connect to the named server and port"));
  _cupsLangPuts(stdout, _("-m                      Send an email notification when the job completes"));
  _cupsLangPuts(stdout, _("-o option[=value]       Specify a printer-specific option"));
  _cupsLangPuts(stdout, _("-o job-sheets=standard  Print a banner page with the job"));
  _cupsLangPuts(stdout, _("-o media=size           Specify the media size to use"));
  _cupsLangPuts(stdout, _("-o number-up=N          Specify that input pages should be printed N-up (1, 2, 4, 6, 9, and 16 are supported)"));
  _cupsLangPuts(stdout, _("-o orientation-requested=N\n"
                          "                        Specify portrait (3) or landscape (4) orientation"));
  _cupsLangPuts(stdout, _("-o print-quality=N      Specify the print quality - draft (3), normal (4), or best (5)"));
  _cupsLangPuts(stdout, _("-o sides=one-sided      Specify 1-sided printing"));
  _cupsLangPuts(stdout, _("-o sides=two-sided-long-edge\n"
                          "                        Specify 2-sided portrait printing"));
  _cupsLangPuts(stdout, _("-o sides=two-sided-short-edge\n"
                          "                        Specify 2-sided landscape printing"));
  _cupsLangPuts(stdout, _("-P destination          Specify the destination"));
  _cupsLangPuts(stdout, _("-q                      Specify the job should be held for printing"));
  _cupsLangPuts(stdout, _("-r                      Remove the file(s) after submission"));
  _cupsLangPuts(stdout, _("-T title                Specify the job title"));
  _cupsLangPuts(stdout, _("-U username             Specify the username to use for authentication"));

  exit(1);
}
