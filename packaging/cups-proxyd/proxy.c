/*
 * proxy backend accompanying cups-proxyd, this backend passes jobs
 * which the proxy CUPS daemon receives on to the system's CUPS
 * daemon.  The system's CUPS daemon does not need to share the
 * printers for that.
 *
 * Copyright 2015-2021 by Till Kamppeter
 *
 * This is based on dnssd.c of CUPS
 * dnssd.c copyright notice is follows:
 *
 * Copyright 2008-2015 by Apple Inc.
 *
 * These coded instructions, statements, and computer programs are the
 * property of Apple Inc. and are protected by Federal copyright
 * law.  Distribution and use rights are outlined in the file "COPYING"
 * which should have been included with this file.
 */

/*
 * Include necessary headers.
 */

#include <cups/cups.h>
#include <cups/backend.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

/*
 * Local globals...
 */

static int		job_canceled = 0; /* Set to 1 on SIGTERM */

/*
 * Local functions...
 */

static void		sigterm_handler(int sig);


/*
 * 'main()' - Pass on the job to the system's CUPS daemon.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line args */
     char *argv[])			/* I - Command-line arguments */
{
  const char	*device_uri;            /* URI with which we were called */
  char scheme[64], username[32], system_cups_server[1024], resource[32];
  int port, status;
  char *system_queue;
  char *p;
  http_t *http;
  int num_options;
  cups_option_t	*options;
  int fd;
  int job_id;
  const char *format;
  ssize_t bytes;
  char buffer[1024];
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
  struct sigaction action;		/* Actions for POSIX signals */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */

 /*
  * Don't buffer stderr, and catch SIGTERM...
  */

  setbuf(stderr, NULL);

#ifdef HAVE_SIGSET /* Use System V signals over POSIX to avoid bugs */
  sigset(SIGTERM, sigterm_handler);
#elif defined(HAVE_SIGACTION)
  memset(&action, 0, sizeof(action));

  sigemptyset(&action.sa_mask);
  action.sa_handler = sigterm_handler;
  sigaction(SIGTERM, &action, NULL);
#else
  signal(SIGTERM, sigterm_handler);
#endif /* HAVE_SIGSET */

 /*
  * Check command-line...
  */

  if (argc >= 6) {
    /* Get the device URI with which we were called */
    if ((device_uri = getenv("DEVICE_URI")) == NULL) {
      if (!argv || !argv[0] || !strchr(argv[0], ':'))
	return (-1);

      device_uri = argv[0];
    }

    /* Read system CUPS server socket/host and destination queue name from
       the device URI */
    status = httpSeparateURI(HTTP_URI_CODING_ALL, device_uri,
			     scheme, sizeof(scheme),
			     username, sizeof(username),
			     system_cups_server, sizeof(system_cups_server),
			     &port,
			     resource, sizeof(resource));
    if ((status != HTTP_URI_STATUS_OK &&
	 status != HTTP_URI_STATUS_UNKNOWN_SCHEME) ||
	username[0] || resource[0] != '/' || strcmp(scheme, "proxy")) {
      fprintf(stderr, "ERROR: Incorrect device URI syntax: %s\n",
	      device_uri);
      return (CUPS_BACKEND_STOP);
    }

    /* Skip the leading '/' in the resource string */
    system_queue = resource + 1;
    
    fprintf(stderr, "DEBUG: Received job to print on the printer %s on the system's CUPS server (%s).\n",
	    system_queue, system_cups_server);

    /* Select the system's CUPS server to print on */
    cupsSetServer(system_cups_server);

    /* Connect to the system's CUPS daemon */
    port = 631;
    p = strrchr(system_cups_server, ':');
    if (p) {
      *p = '\0';
      port = atoi(p + 1);
    }
    if (system_cups_server[0] == '/')
      fprintf(stderr, "DEBUG: Creating http connection to the system's CUPS daemon via domain socket: %s\n",
	      system_cups_server);
    else
      fprintf(stderr, "DEBUG: Creating http connection to the system's CUPS daemon: %s:%d\n",
	      system_cups_server, port);
    if ((http =
	 httpConnect2(system_cups_server, port, NULL, AF_UNSPEC,
		      cupsEncryption(), 1, 30000, NULL)) == NULL) {
      fprintf(stderr, "ERROR: Unable to connect to the system's CUPS daemon!\n");
      return (CUPS_BACKEND_RETRY);
    }

    /* Job should be sent with the same user ID as the original job */
    cupsSetUser(argv[2]);

    /* Read the options to pass them on */
    num_options = cupsParseOptions(argv[5], num_options, &options);

    /* Pass on the number of copies */
    num_options = cupsAddOption("copies", argv[4], num_options, &options);

    /* Open the input file with the job data if there is one */
    if (argc == 7) {
      if ((fd = open(argv[6], O_RDONLY)) < 0) {
	fprintf(stderr, "ERROR: Unable to open input file - %s\n",
		strerror(errno));
	return (CUPS_BACKEND_FAILED);
      }
    } else
      fd = 0; /* stdin */
    
    /* Send job data off to the corresponding queue on the system's CUPS
       daemon */
    if ((job_id = cupsCreateJob(http, system_queue, argv[3],
				     num_options, options)) > 0) {
      if (cupsGetOption("raw", num_options, options))
	format = CUPS_FORMAT_RAW;
      else if ((format = cupsGetOption("document-format", num_options,
				       options)) == NULL)
	format = CUPS_FORMAT_AUTO;

      status = cupsStartDocument(http, system_queue, job_id, NULL, format, 1);

      while (!job_canceled && status == HTTP_CONTINUE &&
	     (bytes = read(fd, buffer, sizeof(buffer))) > 0)
	status = cupsWriteRequestData(http, buffer, (size_t)bytes);

      if (status != HTTP_CONTINUE) {
	fprintf(stderr, "ERROR: Unable to send job data to system's CUPS daemon - %s\n",
	  httpStatus(status));
	cupsFinishDocument(http, system_queue);
	cupsCancelJob2(http, system_queue, job_id, 0);
	close(fd);
	return (CUPS_BACKEND_RETRY);
      }

      if (cupsFinishDocument(http, system_queue) != IPP_OK) {
	fprintf(stderr, "ERROR: Could not finish job on the system's CUPS daemon - %s\n", cupsLastErrorString());
	cupsCancelJob2(http, system_queue, job_id, 0);
	close(fd);
	return (CUPS_BACKEND_RETRY);
      }
    }

    /* Close the input file */
    close(fd);

    /* Close the connection to the system's CUPS daemon */
    httpClose(http);

    if (job_id < 1) {
      fprintf(stderr, "ERROR: Could not create job on the system's CUPS daemon - %s\n", cupsLastErrorString());
      return (CUPS_BACKEND_RETRY);
    }

    /* Report success */
    fprintf(stderr, "DEBUG: Job successfully sent to the system's CUPS as request ID %s-%d\n",
	    system_queue, job_id);

    /* Done */
    return (CUPS_BACKEND_OK);

  } else if (argc != 1) {
    fprintf(stderr,
	    "Usage: %s job-id user title copies options [file]",
	    argv[0]);
    return (CUPS_BACKEND_FAILED);
  }

 /*
  * No discovery mode at all for this backend
  */

  return (CUPS_BACKEND_OK);
}


/*
 * 'sigterm_handler()' - Handle termination signals.
 */

static void
sigterm_handler(int sig)		/* I - Signal number (unused) */
{
  (void)sig;

  if (job_canceled)
    _exit(CUPS_BACKEND_OK);
  else
    job_canceled = 1;
}
