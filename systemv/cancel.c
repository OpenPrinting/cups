/*
 * "cancel" command for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2007-2018 by Apple Inc.
 * Copyright © 1997-2006 by Easy Software Products.
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
 * 'main()' - Parse options and cancel jobs.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line arguments */
     char *argv[])			/* I - Command-line arguments */
{
  http_t	*http;			/* HTTP connection to server */
  int		i;			/* Looping var */
  int		job_id;			/* Job ID */
  int		num_dests;		/* Number of destinations */
  cups_dest_t	*dests;			/* Destinations */
  char		*opt,			/* Option pointer */
		*dest,			/* Destination printer */
		*job,			/* Job ID pointer */
		*user;			/* Cancel jobs for a user */
  int		purge;			/* Purge or cancel jobs? */
  char		uri[1024];		/* Printer or job URI */
  ipp_t		*request;		/* IPP request */
  ipp_t		*response;		/* IPP response */
  ipp_op_t	op;			/* Operation */


  _cupsSetLocale(argv);

 /*
  * Setup to cancel individual print jobs...
  */

  op        = IPP_CANCEL_JOB;
  purge     = 0;
  dest      = NULL;
  user      = NULL;
  http      = NULL;
  num_dests = 0;
  dests     = NULL;


 /*
  * Process command-line arguments...
  */

  for (i = 1; i < argc; i ++)
  {
    if (!strcmp(argv[i], "--help"))
      usage();
    else if (argv[i][0] == '-' && argv[i][1])
    {
      for (opt = argv[i] + 1; *opt; opt ++)
      {
	switch (*opt)
	{
	  case 'E' : /* Encrypt */
#ifdef HAVE_TLS
	      cupsSetEncryption(HTTP_ENCRYPT_REQUIRED);

	      if (http)
		httpEncryption(http, HTTP_ENCRYPT_REQUIRED);
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

	  case 'a' : /* Cancel all jobs */
	      op = purge ? IPP_PURGE_JOBS : IPP_CANCEL_JOBS;
	      break;

	  case 'h' : /* Connect to host */
	      if (http != NULL)
	      {
		httpClose(http);
		http = NULL;
	      }

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
		  _cupsLangPrintf(stderr, _("%s: Error - expected hostname after \"-h\" option."), argv[0]);
		  usage();
		}
		else
		  cupsSetServer(argv[i]);
	      }
	      break;

	  case 'u' : /* Username */
	      op = IPP_CANCEL_MY_JOBS;

	      if (opt[1] != '\0')
	      {
		user = opt + 1;
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;

		if (i >= argc)
		{
		  _cupsLangPrintf(stderr, _("%s: Error - expected username after \"-u\" option."), argv[0]);
		  usage();
		}
		else
		  user = argv[i];
	      }
	      break;

	  case 'x' : /* Purge job(s) */
	      purge = 1;

	      if (op == IPP_CANCEL_JOBS)
		op = IPP_PURGE_JOBS;
	      break;

	  default :
	      _cupsLangPrintf(stderr, _("%s: Error - unknown option \"%c\"."), argv[0], *opt);
	      return (1);
	}
      }
    }
    else
    {
     /*
      * Cancel a job or printer...
      */

      if (num_dests == 0)
        num_dests = cupsGetDests(&dests);

      if (!strcmp(argv[i], "-"))
      {
       /*
        * Delete the current job...
	*/

        dest   = "";
	job_id = 0;
      }
      else if (cupsGetDest(argv[i], NULL, num_dests, dests) != NULL)
      {
       /*
        * Delete the current job on the named destination...
	*/

        dest   = argv[i];
	job_id = 0;
      }
      else if ((job = strrchr(argv[i], '-')) != NULL && isdigit(job[1] & 255))
      {
       /*
        * Delete the specified job ID.
	*/

        dest   = NULL;
	op     = IPP_CANCEL_JOB;
        job_id = atoi(job + 1);
      }
      else if (isdigit(argv[i][0] & 255))
      {
       /*
        * Delete the specified job ID.
	*/

        dest   = NULL;
	op     = IPP_CANCEL_JOB;
        job_id = atoi(argv[i]);
      }
      else
      {
       /*
        * Bad printer name!
	*/

        _cupsLangPrintf(stderr,
	                _("%s: Error - unknown destination \"%s\"."),
			argv[0], argv[i]);
	return (1);
      }

     /*
      * For Solaris LP compatibility, ignore a destination name after
      * cancelling a specific job ID...
      */

      if (job_id && (i + 1) < argc &&
          cupsGetDest(argv[i + 1], NULL, num_dests, dests) != NULL)
        i ++;

     /*
      * Open a connection to the server...
      */

      if (http == NULL)
	if ((http = httpConnectEncrypt(cupsServer(), ippPort(),
	                               cupsEncryption())) == NULL)
	{
	  _cupsLangPrintf(stderr,
	                  _("%s: Unable to connect to server."), argv[0]);
	  return (1);
	}

     /*
      * Build an IPP request, which requires the following
      * attributes:
      *
      *    attributes-charset
      *    attributes-natural-language
      *    printer-uri + job-id *or* job-uri
      *    [requesting-user-name]
      *    [purge-job] or [purge-jobs]
      */

      request = ippNewRequest(op);

      if (dest)
      {
	httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
	                 "localhost", 0, "/printers/%s", dest);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
	             "printer-uri", NULL, uri);
	ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id",
	              job_id);
      }
      else
      {
        snprintf(uri, sizeof(uri), "ipp://localhost/jobs/%d", job_id);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "job-uri", NULL,
	             uri);
      }

      if (user)
      {
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
                     "requesting-user-name", NULL, user);
	ippAddBoolean(request, IPP_TAG_OPERATION, "my-jobs", 1);

        if (op == IPP_CANCEL_JOBS)
          op = IPP_CANCEL_MY_JOBS;
      }
      else
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
                     "requesting-user-name", NULL, cupsUser());

      if (purge)
      {
	if (op == IPP_CANCEL_JOB)
	  ippAddBoolean(request, IPP_TAG_OPERATION, "purge-job", (char)purge);
	else
	  ippAddBoolean(request, IPP_TAG_OPERATION, "purge-jobs", (char)purge);
      }

     /*
      * Do the request and get back a response...
      */

      if (op == IPP_CANCEL_JOBS && (!user || _cups_strcasecmp(user, cupsUser())))
        response = cupsDoRequest(http, request, "/admin/");
      else
        response = cupsDoRequest(http, request, "/jobs/");

      if (response == NULL ||
          response->request.status.status_code > IPP_OK_CONFLICT)
      {
	_cupsLangPrintf(stderr, _("%s: %s failed: %s"), argv[0],
	        	op == IPP_PURGE_JOBS ? "purge-jobs" : "cancel-job",
        		cupsLastErrorString());

          ippDelete(response);

	return (1);
      }

      ippDelete(response);
    }
  }

  if (num_dests == 0 && op != IPP_CANCEL_JOB)
  {
   /*
    * Open a connection to the server...
    */

    if (http == NULL)
      if ((http = httpConnectEncrypt(cupsServer(), ippPort(),
	                             cupsEncryption())) == NULL)
      {
	_cupsLangPrintf(stderr, _("%s: Unable to contact server."), argv[0]);
	return (1);
      }

   /*
    * Build an IPP request, which requires the following
    * attributes:
    *
    *    attributes-charset
    *    attributes-natural-language
    *    printer-uri + job-id *or* job-uri
    *    [requesting-user-name]
    */

    request = ippNewRequest(op);

    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
	         "printer-uri", NULL, "ipp://localhost/printers/");

    if (user)
    {
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
                   "requesting-user-name", NULL, user);
      ippAddBoolean(request, IPP_TAG_OPERATION, "my-jobs", 1);
    }
    else
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
                   "requesting-user-name", NULL, cupsUser());

    ippAddBoolean(request, IPP_TAG_OPERATION, "purge-jobs", (char)purge);

   /*
    * Do the request and get back a response...
    */

    response = cupsDoRequest(http, request, "/admin/");

    if (response == NULL ||
        response->request.status.status_code > IPP_OK_CONFLICT)
    {
      _cupsLangPrintf(stderr, _("%s: %s failed: %s"), argv[0],
		      op == IPP_PURGE_JOBS ? "purge-jobs" : "cancel-job",
        	      cupsLastErrorString());

      ippDelete(response);

      return (1);
    }

    ippDelete(response);
  }

  return (0);
}


/*
 * 'usage()' - Show program usage and exit.
 */

static void
usage(void)
{
  _cupsLangPuts(stdout, _("Usage: cancel [options] [id]\n"
                          "       cancel [options] [destination]\n"
                          "       cancel [options] [destination-id]"));
  _cupsLangPuts(stdout, _("Options:"));
  _cupsLangPuts(stdout, _("-a                      Cancel all jobs"));
  _cupsLangPuts(stdout, _("-E                      Encrypt the connection to the server"));
  _cupsLangPuts(stdout, _("-h server[:port]        Connect to the named server and port"));
  _cupsLangPuts(stdout, _("-u owner                Specify the owner to use for jobs"));
  _cupsLangPuts(stdout, _("-U username             Specify the username to use for authentication"));
  _cupsLangPuts(stdout, _("-x                      Purge jobs rather than just canceling"));

  exit(1);
}
