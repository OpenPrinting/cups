/*
 * Line Printer Daemon interface for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2007-2016 by Apple Inc.
 * Copyright © 1997-2006 by Easy Software Products, all rights reserved.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#define _CUPS_NO_DEPRECATED
#include <cups/cups-private.h>
#include <syslog.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#ifdef HAVE_INTTYPES_H
#  include <inttypes.h>
#endif /* HAVE_INTTYPES_H */
#ifdef __APPLE__
#  include <xpc/xpc.h>
#endif /* __APPLE__ */


/*
 * LPD "mini-daemon" for CUPS.  This program must be used in conjunction
 * with inetd or another similar program that monitors ports and starts
 * daemons for each client connection.  A typical configuration is:
 *
 *    printer stream tcp nowait lp /usr/lib/cups/daemon/cups-lpd cups-lpd
 *
 * This daemon implements most of RFC 1179 (the unofficial LPD specification)
 * except for:
 *
 *     - This daemon does not check to make sure that the source port is
 *       between 721 and 731, since it isn't necessary for proper
 *       functioning and port-based security is no security at all!
 *
 *     - The "Print any waiting jobs" command is a no-op.
 *
 * The LPD-to-IPP mapping is as defined in RFC 2569.  The report formats
 * currently match the Solaris LPD mini-daemon.
 */

/*
 * Prototypes...
 */

static int	create_job(http_t *http, const char *dest, const char *title, const char *user, int num_options, cups_option_t *options);
static int	get_printer(http_t *http, const char *name, char *dest,
		            size_t destsize, cups_option_t **options,
			    int *accepting, int *shared, ipp_pstate_t *state);
static int	print_file(http_t *http, int id, const char *filename,
		           const char *docname, const char *user,
			   const char *format, int last);
static int	recv_print_job(const char *name, int num_defaults,
		               cups_option_t *defaults);
static int	remove_jobs(const char *name, const char *agent,
		            const char *list);
static int	send_state(const char *name, const char *list,
		           int longstatus);
static char	*smart_gets(char *s, int len, FILE *fp);
static void	smart_strlcpy(char *dst, const char *src, size_t dstsize);


/*
 * 'main()' - Process an incoming LPD request...
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line arguments */
     char *argv[])			/* I - Command-line arguments */
{
  int		i;			/* Looping var */
  int		num_defaults;		/* Number of default options */
  cups_option_t	*defaults;		/* Default options */
  char		line[256],		/* Command string */
		command,		/* Command code */
		*dest,			/* Pointer to destination */
		*list,			/* Pointer to list */
		*agent,			/* Pointer to user */
		status;			/* Status for client */
  socklen_t	hostlen;		/* Size of client address */
  http_addr_t	hostaddr;		/* Address of client */
  char		hostname[256],		/* Name of client */
		hostip[256],		/* IP address */
		*hostfamily;		/* Address family */
  int		hostlookups;		/* Do hostname lookups? */


#ifdef __APPLE__
  xpc_transaction_begin();
#endif /* __APPLE__ */

 /*
  * Don't buffer the output...
  */

  setbuf(stdout, NULL);

 /*
  * Log things using the "cups-lpd" name...
  */

  openlog("cups-lpd", LOG_PID, LOG_LPR);

 /*
  * Scan the command-line for options...
  */

  num_defaults = 0;
  defaults     = NULL;
  hostlookups  = 1;

  for (i = 1; i < argc; i ++)
    if (argv[i][0] == '-')
    {
      switch (argv[i][1])
      {
        case 'h' : /* -h hostname[:port] */
            if (argv[i][2])
	      cupsSetServer(argv[i] + 2);
	    else
	    {
	      i ++;
	      if (i < argc)
	        cupsSetServer(argv[i]);
	      else
	        syslog(LOG_WARNING, "Expected hostname string after -h option!");
	    }
	    break;

	case 'o' : /* Option */
	    if (argv[i][2])
	      num_defaults = cupsParseOptions(argv[i] + 2, num_defaults,
	                                      &defaults);
	    else
	    {
	      i ++;
	      if (i < argc)
		num_defaults = cupsParseOptions(argv[i], num_defaults,
		                                &defaults);
              else
        	syslog(LOG_WARNING, "Expected option string after -o option!");
            }
	    break;

        case 'n' : /* Don't do hostname lookups */
	    hostlookups = 0;
	    break;

	default :
	    syslog(LOG_WARNING, "Unknown option \"%c\" ignored!", argv[i][1]);
	    break;
      }
    }
    else
      syslog(LOG_WARNING, "Unknown command-line option \"%s\" ignored!",
             argv[i]);

 /*
  * Get the address of the client...
  */

  hostlen = sizeof(hostaddr);

  if (getpeername(0, (struct sockaddr *)&hostaddr, &hostlen))
  {
    syslog(LOG_WARNING, "Unable to get client address - %s", strerror(errno));
    strlcpy(hostname, "unknown", sizeof(hostname));
  }
  else
  {
    httpAddrString(&hostaddr, hostip, sizeof(hostip));

    if (hostlookups)
      httpAddrLookup(&hostaddr, hostname, sizeof(hostname));
    else
      strlcpy(hostname, hostip, sizeof(hostname));

#ifdef AF_INET6
    if (hostaddr.addr.sa_family == AF_INET6)
      hostfamily = "IPv6";
    else
#endif /* AF_INET6 */
    hostfamily = "IPv4";

    syslog(LOG_INFO, "Connection from %s (%s %s)", hostname, hostfamily,
           hostip);
  }

  num_defaults = cupsAddOption("job-originating-host-name", hostname,
                               num_defaults, &defaults);

 /*
  * RFC1179 specifies that only 1 daemon command can be received for
  * every connection.
  */

  if (smart_gets(line, sizeof(line), stdin) == NULL)
  {
   /*
    * Unable to get command from client!  Send an error status and return.
    */

    syslog(LOG_ERR, "Unable to get command line from client!");
    putchar(1);

#ifdef __APPLE__
    xpc_transaction_end();
#endif /* __APPLE__ */

    return (1);
  }

 /*
  * The first byte is the command byte.  After that will be the queue name,
  * resource list, and/or user name.
  */

  if ((command = line[0]) == '\0')
    dest = line;
  else
    dest = line + 1;

  if (command == 0x02)
    list = NULL;
  else
  {
    for (list = dest; *list && !isspace(*list & 255); list ++);

    while (isspace(*list & 255))
      *list++ = '\0';
  }

 /*
  * Do the command...
  */

  switch (command)
  {
    default : /* Unknown command */
        syslog(LOG_ERR, "Unknown LPD command 0x%02X!", command);
        syslog(LOG_ERR, "Command line = %s", line + 1);
	putchar(1);

        status = 1;
	break;

    case 0x01 : /* Print any waiting jobs */
        syslog(LOG_INFO, "Print waiting jobs (no-op)");
	putchar(0);

        status = 0;
	break;

    case 0x02 : /* Receive a printer job */
        syslog(LOG_INFO, "Receive print job for %s", dest);
        /* recv_print_job() sends initial status byte */

        status = (char)recv_print_job(dest, num_defaults, defaults);
	break;

    case 0x03 : /* Send queue state (short) */
        syslog(LOG_INFO, "Send queue state (short) for %s %s", dest, list);
	/* no status byte for this command */

        status = (char)send_state(dest, list, 0);
	break;

    case 0x04 : /* Send queue state (long) */
        syslog(LOG_INFO, "Send queue state (long) for %s %s", dest, list);
	/* no status byte for this command */

        status = (char)send_state(dest, list, 1);
	break;

    case 0x05 : /* Remove jobs */
       /*
	* Grab the agent and skip to the list of users and/or jobs.
	*/

	agent = list;

	for (; *list && !isspace(*list & 255); list ++);
	while (isspace(*list & 255))
	  *list++ = '\0';

	syslog(LOG_INFO, "Remove jobs %s on %s by %s", list, dest, agent);

	status = (char)remove_jobs(dest, agent, list);

	putchar(status);
	break;
  }

  syslog(LOG_INFO, "Closing connection");
  closelog();

#ifdef __APPLE__
  xpc_transaction_end();
#endif /* __APPLE__ */

  return (status);
}


/*
 * 'create_job()' - Create a new print job.
 */

static int				/* O - Job ID or -1 on error */
create_job(http_t        *http,		/* I - HTTP connection */
           const char    *dest,		/* I - Destination name */
	   const char    *title,	/* I - job-name */
	   const char    *user,		/* I - requesting-user-name */
	   int           num_options,	/* I - Number of options for job */
	   cups_option_t *options)	/* I - Options for job */
{
  ipp_t		*request;		/* IPP request */
  ipp_t		*response;		/* IPP response */
  ipp_attribute_t *attr;		/* IPP attribute */
  char		uri[HTTP_MAX_URI];	/* Printer URI */
  int		id;			/* Job ID */


 /*
  * Setup the Create-Job request...
  */

  request = ippNewRequest(IPP_OP_CREATE_JOB);

  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                   "localhost", 0, "/printers/%s", dest);

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
               NULL, uri);

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
               "requesting-user-name", NULL, user);

  if (title[0])
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "job-name",
                 NULL, title);

  cupsEncodeOptions(request, num_options, options);

 /*
  * Do the request...
  */

  snprintf(uri, sizeof(uri), "/printers/%s", dest);

  response = cupsDoRequest(http, request, uri);

  if (!response || cupsLastError() > IPP_STATUS_OK_CONFLICTING)
  {
    syslog(LOG_ERR, "Unable to create job - %s", cupsLastErrorString());

    ippDelete(response);

    return (-1);
  }

 /*
  * Get the job-id value from the response and return it...
  */

  if ((attr = ippFindAttribute(response, "job-id", IPP_TAG_INTEGER)) == NULL)
  {
    id = -1;

    syslog(LOG_ERR, "No job-id attribute found in response from server!");
  }
  else
  {
    id = attr->values[0].integer;

    syslog(LOG_INFO, "Print file - job ID = %d", id);
  }

  ippDelete(response);

  return (id);
}


/*
 * 'get_printer()' - Get the named printer and its options.
 */

static int				/* O - Number of options or -1 on error */
get_printer(http_t        *http,	/* I - HTTP connection */
            const char    *name,	/* I - Printer name from request */
	    char          *dest,	/* I - Destination buffer */
            size_t        destsize,	/* I - Size of destination buffer */
	    cups_option_t **options,	/* O - Printer options */
	    int           *accepting,	/* O - printer-is-accepting-jobs value */
	    int           *shared,	/* O - printer-is-shared value */
	    ipp_pstate_t  *state)	/* O - printer-state value */
{
  int		num_options;		/* Number of options */
  cups_file_t	*fp;			/* lpoptions file */
  char		line[1024],		/* Line from lpoptions file */
		*value,			/* Pointer to value on line */
		*optptr;		/* Pointer to options on line */
  int		linenum;		/* Line number in file */
  const char	*cups_serverroot;	/* CUPS_SERVERROOT env var */
  ipp_t		*request;		/* IPP request */
  ipp_t		*response;		/* IPP response */
  ipp_attribute_t *attr;		/* IPP attribute */
  char		uri[HTTP_MAX_URI];	/* Printer URI */
  static const char * const requested[] =
		{			/* Requested attributes */
		  "printer-info",
		  "printer-is-accepting-jobs",
		  "printer-is-shared",
		  "printer-name",
		  "printer-state"
		};


 /*
  * Initialize everything...
  */

  if (accepting)
    *accepting = 0;
  if (shared)
    *shared = 0;
  if (state)
    *state = IPP_PSTATE_STOPPED;
  if (options)
    *options = NULL;

 /*
  * See if the name is a queue name optionally with an instance name.
  */

  strlcpy(dest, name, destsize);
  if ((value = strchr(dest, '/')) != NULL)
    *value = '\0';

 /*
  * Setup the Get-Printer-Attributes request...
  */

  request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);

  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
		   "localhost", 0, "/printers/%s", dest);

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
	       NULL, uri);

  ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
		"requested-attributes",
		(int)(sizeof(requested) / sizeof(requested[0])),
		NULL, requested);

 /*
  * Do the request...
  */

  response = cupsDoRequest(http, request, "/");

  if (!response || cupsLastError() > IPP_STATUS_OK_CONFLICTING)
  {
   /*
    * If we can't find the printer by name, look up the printer-name
    * using the printer-info values...
    */

    ipp_attribute_t	*accepting_attr,/* printer-is-accepting-jobs */
			*info_attr,	/* printer-info */
			*name_attr,	/* printer-name */
			*shared_attr,	/* printer-is-shared */
			*state_attr;	/* printer-state */


    ippDelete(response);

   /*
    * Setup the CUPS-Get-Printers request...
    */

    request = ippNewRequest(IPP_OP_CUPS_GET_PRINTERS);

    ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                  "requested-attributes",
		  (int)(sizeof(requested) / sizeof(requested[0])),
                  NULL, requested);

   /*
    * Do the request...
    */

    response = cupsDoRequest(http, request, "/");

    if (!response || cupsLastError() > IPP_STATUS_OK_CONFLICTING)
    {
      syslog(LOG_ERR, "Unable to get list of printers - %s",
             cupsLastErrorString());

      ippDelete(response);

      return (-1);
    }

   /*
    * Scan the response for printers...
    */

    *dest = '\0';
    attr  = response->attrs;

    while (attr)
    {
     /*
      * Skip to the next printer...
      */

      while (attr && attr->group_tag != IPP_TAG_PRINTER)
        attr = attr->next;

      if (!attr)
        break;

     /*
      * Get all of the attributes for the current printer...
      */

      accepting_attr = NULL;
      info_attr      = NULL;
      name_attr      = NULL;
      shared_attr    = NULL;
      state_attr     = NULL;

      while (attr && attr->group_tag == IPP_TAG_PRINTER)
      {
        if (!strcmp(attr->name, "printer-is-accepting-jobs") &&
	    attr->value_tag == IPP_TAG_BOOLEAN)
	  accepting_attr = attr;
	else if (!strcmp(attr->name, "printer-info") &&
	         attr->value_tag == IPP_TAG_TEXT)
	  info_attr = attr;
	else if (!strcmp(attr->name, "printer-name") &&
	         attr->value_tag == IPP_TAG_NAME)
	  name_attr = attr;
	else if (!strcmp(attr->name, "printer-is-shared") &&
	         attr->value_tag == IPP_TAG_BOOLEAN)
	  shared_attr = attr;
	else if (!strcmp(attr->name, "printer-state") &&
	         attr->value_tag == IPP_TAG_ENUM)
	  state_attr = attr;

        attr = attr->next;
      }

      if (info_attr && name_attr &&
          !_cups_strcasecmp(name, info_attr->values[0].string.text))
      {
       /*
        * Found a match, use this one!
	*/

	strlcpy(dest, name_attr->values[0].string.text, destsize);

	if (accepting && accepting_attr)
	  *accepting = accepting_attr->values[0].boolean;

	if (shared && shared_attr)
	  *shared = shared_attr->values[0].boolean;

	if (state && state_attr)
	  *state = (ipp_pstate_t)state_attr->values[0].integer;

        break;
      }
    }

    ippDelete(response);

    if (!*dest)
    {
      syslog(LOG_ERR, "Unable to find \"%s\" in list of printers!", name);

      return (-1);
    }

    name = dest;
  }
  else
  {
   /*
    * Get values from the response...
    */

    if (accepting)
    {
      if ((attr = ippFindAttribute(response, "printer-is-accepting-jobs",
				   IPP_TAG_BOOLEAN)) == NULL)
	syslog(LOG_ERR, "No printer-is-accepting-jobs attribute found in "
			"response from server!");
      else
	*accepting = attr->values[0].boolean;
    }

    if (shared)
    {
      if ((attr = ippFindAttribute(response, "printer-is-shared",
				   IPP_TAG_BOOLEAN)) == NULL)
      {
	syslog(LOG_ERR, "No printer-is-shared attribute found in "
			"response from server!");
	*shared = 1;
      }
      else
	*shared = attr->values[0].boolean;
    }

    if (state)
    {
      if ((attr = ippFindAttribute(response, "printer-state",
				   IPP_TAG_ENUM)) == NULL)
	syslog(LOG_ERR, "No printer-state attribute found in "
			"response from server!");
      else
	*state = (ipp_pstate_t)attr->values[0].integer;
    }

    ippDelete(response);
  }

 /*
  * Next look for the printer in the lpoptions file...
  */

  num_options = 0;

  if (options && shared && accepting)
  {
    if ((cups_serverroot = getenv("CUPS_SERVERROOT")) == NULL)
      cups_serverroot = CUPS_SERVERROOT;

    snprintf(line, sizeof(line), "%s/lpoptions", cups_serverroot);
    if ((fp = cupsFileOpen(line, "r")) != NULL)
    {
      linenum = 0;
      while (cupsFileGetConf(fp, line, sizeof(line), &value, &linenum))
      {
       /*
	* Make sure we have "Dest name options" or "Default name options"...
	*/

	if ((_cups_strcasecmp(line, "Dest") && _cups_strcasecmp(line, "Default")) || !value)
          continue;

       /*
	* Separate destination name from options...
	*/

	for (optptr = value; *optptr && !isspace(*optptr & 255); optptr ++);

	while (*optptr == ' ')
	  *optptr++ = '\0';

       /*
	* If this is our destination, parse the options and break out of
	* the loop - we're done!
	*/

	if (!_cups_strcasecmp(value, name))
	{
          num_options = cupsParseOptions(optptr, num_options, options);
	  break;
	}
      }

      cupsFileClose(fp);
    }
  }
  else if (options)
    *options = NULL;

 /*
  * Return the number of options for this destination...
  */

  return (num_options);
}


/*
 * 'print_file()' - Add a file to the current job.
 */

static int				/* O - 0 on success, -1 on failure */
print_file(http_t     *http,		/* I - HTTP connection */
           int        id,		/* I - Job ID */
	   const char *filename,	/* I - File to print */
           const char *docname,		/* I - document-name */
	   const char *user,		/* I - requesting-user-name */
	   const char *format,		/* I - document-format */
	   int        last)		/* I - 1 = last file in job */
{
  ipp_t		*request;		/* IPP request */
  char		uri[HTTP_MAX_URI];	/* Printer URI */


 /*
  * Setup the Send-Document request...
  */

  request = ippNewRequest(IPP_OP_SEND_DOCUMENT);

  snprintf(uri, sizeof(uri), "ipp://localhost/jobs/%d", id);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "job-uri", NULL, uri);

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
               "requesting-user-name", NULL, user);

  if (docname)
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
        	 "document-name", NULL, docname);

  if (format)
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE,
                 "document-format", NULL, format);

  ippAddBoolean(request, IPP_TAG_OPERATION, "last-document", (char)last);

 /*
  * Do the request...
  */

  snprintf(uri, sizeof(uri), "/jobs/%d", id);

  ippDelete(cupsDoFileRequest(http, request, uri, filename));

  if (cupsLastError() > IPP_STATUS_OK_CONFLICTING)
  {
    syslog(LOG_ERR, "Unable to send document - %s", cupsLastErrorString());

    return (-1);
  }

  return (0);
}


/*
 * 'recv_print_job()' - Receive a print job from the client.
 */

static int				/* O - Command status */
recv_print_job(
    const char    *queue,		/* I - Printer name */
    int           num_defaults,		/* I - Number of default options */
    cups_option_t *defaults)		/* I - Default options */
{
  http_t	*http;			/* HTTP connection */
  int		i;			/* Looping var */
  int		status;			/* Command status */
  int		fd;			/* Temporary file */
  FILE		*fp;			/* File pointer */
  char		filename[1024];		/* Temporary filename */
  ssize_t	bytes;			/* Bytes received */
  size_t	total;			/* Total bytes */
  char		line[256],		/* Line from file/stdin */
		command,		/* Command from line */
		*count,			/* Number of bytes */
		*name;			/* Name of file */
  const char	*job_sheets;		/* Job sheets */
  int		num_data;		/* Number of data files */
  char		control[1024],		/* Control filename */
		data[100][256],		/* Data files */
		temp[100][1024];	/* Temporary files */
  char		user[1024],		/* User name */
		title[1024],		/* Job title */
		docname[1024],		/* Document name */
		dest[256];		/* Printer/class queue */
  int		accepting,		/* printer-is-accepting */
		shared,			/* printer-is-shared */
		num_options;		/* Number of options */
  cups_option_t	*options;		/* Options */
  int		id;			/* Job ID */
  int		docnumber,		/* Current document number */
		doccount;		/* Count of documents */


 /*
  * Connect to the server...
  */

  http = httpConnect2(cupsServer(), ippPort(), NULL, AF_UNSPEC, cupsEncryption(), 1, 30000, NULL);
  if (!http)
  {
    syslog(LOG_ERR, "Unable to connect to server: %s", strerror(errno));

    putchar(1);

    return (1);
  }

 /*
  * See if the printer is available...
  */

  num_options = get_printer(http, queue, dest, sizeof(dest), &options,
                            &accepting, &shared, NULL);

  if (num_options < 0 || !accepting || !shared)
  {
    if (dest[0])
      syslog(LOG_INFO, "Rejecting job because \"%s\" is not %s", dest,
             !accepting ? "accepting jobs" : "shared");
    else
      syslog(LOG_ERR, "Unable to get printer information for \"%s\"", queue);

    httpClose(http);

    putchar(1);

    return (1);
  }

  putchar(0);				/* OK so far... */

 /*
  * Read the request...
  */

  status   = 0;
  num_data = 0;
  fd       = -1;

  control[0] = '\0';

  while (smart_gets(line, sizeof(line), stdin) != NULL)
  {
    if (strlen(line) < 2)
    {
      status = 1;
      break;
    }

    command = line[0];
    count   = line + 1;

    for (name = count + 1; *name && !isspace(*name & 255); name ++);
    while (isspace(*name & 255))
      *name++ = '\0';

    switch (command)
    {
      default :
      case 0x01 : /* Abort */
          status = 1;
	  break;

      case 0x02 : /* Receive control file */
          if (strlen(name) < 2)
	  {
	    syslog(LOG_ERR, "Bad control file name \"%s\"", name);
	    putchar(1);
	    status = 1;
	    break;
	  }

          if (control[0])
	  {
	   /*
	    * Append to the existing control file - the LPD spec is
	    * not entirely clear, but at least the OS/2 LPD code sends
	    * multiple control files per connection...
	    */

	    if ((fd = open(control, O_WRONLY)) < 0)
	    {
	      syslog(LOG_ERR,
	             "Unable to append to temporary control file \"%s\" - %s",
        	     control, strerror(errno));
	      putchar(1);
	      status = 1;
	      break;
	    }

	    lseek(fd, 0, SEEK_END);
          }
	  else
	  {
	    if ((fd = cupsTempFd(control, sizeof(control))) < 0)
	    {
	      syslog(LOG_ERR, "Unable to open temporary control file \"%s\" - %s",
        	     control, strerror(errno));
	      putchar(1);
	      status = 1;
	      break;
	    }

	    strlcpy(filename, control, sizeof(filename));
	  }
	  break;

      case 0x03 : /* Receive data file */
          if (strlen(name) < 2)
	  {
	    syslog(LOG_ERR, "Bad data file name \"%s\"", name);
	    putchar(1);
	    status = 1;
	    break;
	  }

          if (num_data >= (int)(sizeof(data) / sizeof(data[0])))
	  {
	   /*
	    * Too many data files...
	    */

	    syslog(LOG_ERR, "Too many data files (%d)", num_data);
	    putchar(1);
	    status = 1;
	    break;
	  }

	  strlcpy(data[num_data], name, sizeof(data[0]));

          if ((fd = cupsTempFd(temp[num_data], sizeof(temp[0]))) < 0)
	  {
	    syslog(LOG_ERR, "Unable to open temporary data file \"%s\" - %s",
        	   temp[num_data], strerror(errno));
	    putchar(1);
	    status = 1;
	    break;
	  }

	  strlcpy(filename, temp[num_data], sizeof(filename));

          num_data ++;
	  break;
    }

    putchar(status);

    if (status)
      break;

   /*
    * Copy the data or control file from the client...
    */

    for (total = (size_t)strtoll(count, NULL, 10); total > 0; total -= (size_t)bytes)
    {
      if (total > sizeof(line))
        bytes = (ssize_t)sizeof(line);
      else
        bytes = (ssize_t)total;

      if ((bytes = (ssize_t)fread(line, 1, (size_t)bytes, stdin)) > 0)
        bytes = write(fd, line, (size_t)bytes);

      if (bytes < 1)
      {
	syslog(LOG_ERR, "Error while reading file - %s",
               strerror(errno));
        status = 1;
	break;
      }
    }

   /*
    * Read trailing nul...
    */

    if (!status)
    {
      if (fread(line, 1, 1, stdin) < 1)
      {
        status = 1;
	syslog(LOG_ERR, "Error while reading trailing nul - %s",
               strerror(errno));
      }
      else if (line[0])
      {
        status = 1;
	syslog(LOG_ERR, "Trailing character after file is not nul (%02X)!",
	       line[0]);
      }
    }

   /*
    * Close the file and send an acknowledgement...
    */

    close(fd);

    putchar(status);

    if (status)
      break;
  }

  if (!status)
  {
   /*
    * Process the control file and print stuff...
    */

    if ((fp = fopen(control, "rb")) == NULL)
      status = 1;
    else
    {
     /*
      * Copy the default options...
      */

      for (i = 0; i < num_defaults; i ++)
	num_options = cupsAddOption(defaults[i].name,
		                    defaults[i].value,
		                    num_options, &options);

     /*
      * Grab the job information...
      */

      title[0]   = '\0';
      user[0]    = '\0';
      docname[0] = '\0';
      doccount   = 0;

      while (smart_gets(line, sizeof(line), fp) != NULL)
      {
       /*
        * Process control lines...
	*/

	switch (line[0])
	{
	  case 'J' : /* Job name */
	      smart_strlcpy(title, line + 1, sizeof(title));
	      break;

          case 'N' : /* Document name */
              smart_strlcpy(docname, line + 1, sizeof(docname));
              break;

	  case 'P' : /* User identification */
	      smart_strlcpy(user, line + 1, sizeof(user));
	      break;

	  case 'L' : /* Print banner page */
	     /*
	      * If a banner was requested and it's not overridden by a
	      * command line option and the destination's default is none
	      * then add the standard banner...
	      */

	      if (cupsGetOption("job-sheets", num_defaults, defaults) == NULL &&
        	  ((job_sheets = cupsGetOption("job-sheets", num_options,
					       options)) == NULL ||
        	   !strcmp(job_sheets, "none,none")))
	      {
		num_options = cupsAddOption("job-sheets", "standard",
		                	    num_options, &options);
	      }
	      break;

	  case 'c' : /* Plot CIF file */
	  case 'd' : /* Print DVI file */
	  case 'f' : /* Print formatted file */
	  case 'g' : /* Plot file */
	  case 'l' : /* Print file leaving control characters (raw) */
	  case 'n' : /* Print ditroff output file */
	  case 'o' : /* Print PostScript output file */
	  case 'p' : /* Print file with 'pr' format (prettyprint) */
	  case 'r' : /* File to print with FORTRAN carriage control */
	  case 't' : /* Print troff output file */
	  case 'v' : /* Print raster file */
	      doccount ++;

	      if (line[0] == 'l' &&
	          !cupsGetOption("document-format", num_options, options))
		num_options = cupsAddOption("raw", "", num_options, &options);

              if (line[0] == 'p')
		num_options = cupsAddOption("prettyprint", "", num_options,
		                	    &options);
              break;
	}
      }

     /*
      * Check that we have a username...
      */

      if (!user[0])
      {
	syslog(LOG_WARNING, "No username specified by client! "
		            "Using \"anonymous\"...");
	strlcpy(user, "anonymous", sizeof(user));
      }

     /*
      * Create the job...
      */

      if ((id = create_job(http, dest, title, user, num_options, options)) < 0)
        status = 1;
      else
      {
       /*
	* Then print the job files...
	*/

	rewind(fp);

	docname[0] = '\0';
	docnumber  = 0;

	while (smart_gets(line, sizeof(line), fp) != NULL)
	{
	 /*
          * Process control lines...
	  */

	  switch (line[0])
	  {
	    case 'N' : /* Document name */
		smart_strlcpy(docname, line + 1, sizeof(docname));
		break;

	    case 'c' : /* Plot CIF file */
	    case 'd' : /* Print DVI file */
	    case 'f' : /* Print formatted file */
	    case 'g' : /* Plot file */
	    case 'l' : /* Print file leaving control characters (raw) */
	    case 'n' : /* Print ditroff output file */
	    case 'o' : /* Print PostScript output file */
	    case 'p' : /* Print file with 'pr' format (prettyprint) */
	    case 'r' : /* File to print with FORTRAN carriage control */
	    case 't' : /* Print troff output file */
	    case 'v' : /* Print raster file */
               /*
		* Figure out which file we are printing...
		*/

		for (i = 0; i < num_data; i ++)
	          if (!strcmp(data[i], line + 1))
		    break;

        	if (i >= num_data)
		{
	          status = 1;
		  break;
		}

               /*
		* Send the print file...
		*/

        	docnumber ++;

        	if (print_file(http, id, temp[i], docname, user,
		               cupsGetOption("document-format", num_options,
			                     options),
	                       docnumber == doccount))
                  status = 1;
		else
	          status = 0;

		break;
	  }

	  if (status)
	    break;
	}
      }

      fclose(fp);
    }
  }

  cupsFreeOptions(num_options, options);

  httpClose(http);

 /*
  * Clean up all temporary files and return...
  */

  unlink(control);

  for (i = 0; i < num_data; i ++)
    unlink(temp[i]);

  return (status);
}


/*
 * 'remove_jobs()' - Cancel one or more jobs.
 */

static int				/* O - Command status */
remove_jobs(const char *dest,		/* I - Destination */
            const char *agent,		/* I - User agent */
	    const char *list)		/* I - List of jobs or users */
{
  int		id;			/* Job ID */
  http_t	*http;			/* HTTP server connection */
  ipp_t		*request;		/* IPP Request */
  char		uri[HTTP_MAX_URI];	/* Job URI */


  (void)dest;	/* Suppress compiler warnings... */

 /*
  * Try connecting to the local server...
  */

  if ((http = httpConnect2(cupsServer(), ippPort(), NULL, AF_UNSPEC, cupsEncryption(), 1, 30000, NULL)) == NULL)
  {
    syslog(LOG_ERR, "Unable to connect to server %s: %s", cupsServer(),
           strerror(errno));
    return (1);
  }

 /*
  * Loop for each job...
  */

  while ((id = atoi(list)) > 0)
  {
   /*
    * Skip job ID in list...
    */

    while (isdigit(*list & 255))
      list ++;
    while (isspace(*list & 255))
      list ++;

   /*
    * Build an IPP_OP_CANCEL_JOB request, which requires the following
    * attributes:
    *
    *    attributes-charset
    *    attributes-natural-language
    *    job-uri
    *    requesting-user-name
    */

    request = ippNewRequest(IPP_OP_CANCEL_JOB);

    snprintf(uri, sizeof(uri), "ipp://localhost/jobs/%d", id);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "job-uri", NULL, uri);

    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
                 "requesting-user-name", NULL, agent);

   /*
    * Do the request and get back a response...
    */

    ippDelete(cupsDoRequest(http, request, "/jobs"));

    if (cupsLastError() > IPP_STATUS_OK_CONFLICTING)
    {
      syslog(LOG_WARNING, "Cancel of job ID %d failed: %s\n", id,
             cupsLastErrorString());
      httpClose(http);
      return (1);
    }
    else
      syslog(LOG_INFO, "Job ID %d canceled", id);
  }

  httpClose(http);

  return (0);
}


/*
 * 'send_state()' - Send the queue state.
 */

static int				/* O - Command status */
send_state(const char *queue,		/* I - Destination */
           const char *list,		/* I - Job or user */
	   int        longstatus)	/* I - List of jobs or users */
{
  int		id;			/* Job ID from list */
  http_t	*http;			/* HTTP server connection */
  ipp_t		*request,		/* IPP Request */
		*response;		/* IPP Response */
  ipp_attribute_t *attr;		/* Current attribute */
  ipp_pstate_t	state;			/* Printer state */
  const char	*jobdest,		/* Pointer into job-printer-uri */
		*jobuser,		/* Pointer to job-originating-user-name */
		*jobname;		/* Pointer to job-name */
  ipp_jstate_t	jobstate;		/* job-state */
  int		jobid,			/* job-id */
		jobsize,		/* job-k-octets */
		jobcount,		/* Number of jobs */
		jobcopies,		/* Number of copies */
		rank;			/* Rank of job */
  char		rankstr[255];		/* Rank string */
  char		namestr[1024];		/* Job name string */
  char		uri[HTTP_MAX_URI];	/* Printer URI */
  char		dest[256];		/* Printer/class queue */
  static const char * const ranks[10] =	/* Ranking strings */
		{
		  "th",
		  "st",
		  "nd",
		  "rd",
		  "th",
		  "th",
		  "th",
		  "th",
		  "th",
		  "th"
		};
  static const char * const requested[] =
		{			/* Requested attributes */
		  "job-id",
		  "job-k-octets",
		  "job-state",
		  "job-printer-uri",
		  "job-originating-user-name",
		  "job-name",
		  "copies"
		};


 /*
  * Try connecting to the local server...
  */

  if ((http = httpConnect2(cupsServer(), ippPort(), NULL, AF_UNSPEC, cupsEncryption(), 1, 30000, NULL)) == NULL)
  {
    syslog(LOG_ERR, "Unable to connect to server %s: %s", cupsServer(),
           strerror(errno));
    printf("Unable to connect to server %s: %s", cupsServer(), strerror(errno));
    return (1);
  }

 /*
  * Get the actual destination name and printer state...
  */

  if (get_printer(http, queue, dest, sizeof(dest), NULL, NULL, NULL, &state))
  {
    syslog(LOG_ERR, "Unable to get printer %s: %s", queue,
           cupsLastErrorString());
    printf("Unable to get printer %s: %s", queue, cupsLastErrorString());
    return (1);
  }

 /*
  * Show the queue state...
  */

  switch (state)
  {
    case IPP_PSTATE_IDLE :
        printf("%s is ready\n", dest);
	break;
    case IPP_PSTATE_PROCESSING :
        printf("%s is ready and printing\n", dest);
	break;
    case IPP_PSTATE_STOPPED :
        printf("%s is not ready\n", dest);
	break;
  }

 /*
  * Build an IPP_OP_GET_JOBS or IPP_OP_GET_JOB_ATTRIBUTES request, which requires
  * the following attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  *    job-uri or printer-uri
  */

  id = atoi(list);

  request = ippNewRequest(id ? IPP_OP_GET_JOB_ATTRIBUTES : IPP_OP_GET_JOBS);

  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                   "localhost", 0, "/printers/%s", dest);

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
               NULL, uri);

  if (id)
    ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id", id);
  else
  {
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
                 "requesting-user-name", NULL, list);
    ippAddBoolean(request, IPP_TAG_OPERATION, "my-jobs", 1);
  }

  ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                "requested-attributes",
	        sizeof(requested) / sizeof(requested[0]),
		NULL, requested);

 /*
  * Do the request and get back a response...
  */

  jobcount = 0;
  response = cupsDoRequest(http, request, "/");

  if (cupsLastError() > IPP_STATUS_OK_CONFLICTING)
  {
    printf("get-jobs failed: %s\n", cupsLastErrorString());
    ippDelete(response);
    return (1);
  }

 /*
  * Loop through the job list and display them...
  */

  for (attr = response->attrs, rank = 1; attr; attr = attr->next)
  {
   /*
    * Skip leading attributes until we hit a job...
    */

    while (attr && (attr->group_tag != IPP_TAG_JOB || !attr->name))
      attr = attr->next;

    if (!attr)
      break;

   /*
    * Pull the needed attributes from this job...
    */

    jobid     = 0;
    jobsize   = 0;
    jobstate  = IPP_JSTATE_PENDING;
    jobname   = "untitled";
    jobuser   = NULL;
    jobdest   = NULL;
    jobcopies = 1;

    while (attr && attr->group_tag == IPP_TAG_JOB)
    {
      if (!strcmp(attr->name, "job-id") &&
	  attr->value_tag == IPP_TAG_INTEGER)
	jobid = attr->values[0].integer;

      if (!strcmp(attr->name, "job-k-octets") &&
	  attr->value_tag == IPP_TAG_INTEGER)
	jobsize = attr->values[0].integer;

      if (!strcmp(attr->name, "job-state") &&
	  attr->value_tag == IPP_TAG_ENUM)
	jobstate = (ipp_jstate_t)attr->values[0].integer;

      if (!strcmp(attr->name, "job-printer-uri") &&
	  attr->value_tag == IPP_TAG_URI)
	if ((jobdest = strrchr(attr->values[0].string.text, '/')) != NULL)
	  jobdest ++;

      if (!strcmp(attr->name, "job-originating-user-name") &&
	  attr->value_tag == IPP_TAG_NAME)
	jobuser = attr->values[0].string.text;

      if (!strcmp(attr->name, "job-name") &&
	  attr->value_tag == IPP_TAG_NAME)
	jobname = attr->values[0].string.text;

      if (!strcmp(attr->name, "copies") &&
	  attr->value_tag == IPP_TAG_INTEGER)
	jobcopies = attr->values[0].integer;

      attr = attr->next;
    }

   /*
    * See if we have everything needed...
    */

    if (!jobdest || !jobid)
    {
      if (!attr)
	break;
      else
        continue;
    }

    if (!longstatus && jobcount == 0)
      puts("Rank    Owner   Job     File(s)                         Total Size");

    jobcount ++;

   /*
    * Display the job...
    */

    if (jobstate == IPP_JSTATE_PROCESSING)
      strlcpy(rankstr, "active", sizeof(rankstr));
    else
    {
      snprintf(rankstr, sizeof(rankstr), "%d%s", rank, ranks[rank % 10]);
      rank ++;
    }

    if (longstatus)
    {
      puts("");

      if (jobcopies > 1)
	snprintf(namestr, sizeof(namestr), "%d copies of %s", jobcopies,
	         jobname);
      else
	strlcpy(namestr, jobname, sizeof(namestr));

      printf("%s: %-33.33s [job %d localhost]\n", jobuser, rankstr, jobid);
      printf("        %-39.39s %.0f bytes\n", namestr, 1024.0 * jobsize);
    }
    else
      printf("%-7s %-7.7s %-7d %-31.31s %.0f bytes\n", rankstr, jobuser,
	     jobid, jobname, 1024.0 * jobsize);

    if (!attr)
      break;
  }

  ippDelete(response);

  if (jobcount == 0)
    puts("no entries");

  httpClose(http);

  return (0);
}


/*
 * 'smart_gets()' - Get a line of text, removing the trailing CR and/or LF.
 */

static char *				/* O - Line read or NULL */
smart_gets(char *s,			/* I - Pointer to line buffer */
           int  len,			/* I - Size of line buffer */
	   FILE *fp)			/* I - File to read from */
{
  char	*ptr,				/* Pointer into line */
	*end;				/* End of line */
  int	ch;				/* Character from file */


 /*
  * Read the line; unlike fgets(), we read the entire line but dump
  * characters that go past the end of the buffer.  Also, we accept
  * CR, LF, or CR LF for the line endings to be "safe", although
  * RFC 1179 specifically says "just use LF".
  */

  ptr = s;
  end = s + len - 1;

  while ((ch = getc(fp)) != EOF)
  {
    if (ch == '\n')
      break;
    else if (ch == '\r')
    {
     /*
      * See if a LF follows...
      */

      ch = getc(fp);

      if (ch != '\n')
        ungetc(ch, fp);

      break;
    }
    else if (ptr < end)
      *ptr++ = (char)ch;
  }

  *ptr = '\0';

  if (ch == EOF && ptr == s)
    return (NULL);
  else
    return (s);
}


/*
 * 'smart_strlcpy()' - Copy a string and convert from ISO-8859-1 to UTF-8 as needed.
 */

static void
smart_strlcpy(char       *dst,		/* I - Output buffer */
              const char *src,		/* I - Input string */
              size_t     dstsize)	/* I - Size of output buffer */
{
  const unsigned char	*srcptr;	/* Pointer into input string */
  unsigned char		*dstptr,	/* Pointer into output buffer */
			*dstend;	/* End of output buffer */
  int			saw_8859 = 0;	/* Saw an extended character that was not UTF-8? */


  for (srcptr = (unsigned char *)src, dstptr = (unsigned char *)dst, dstend = dstptr + dstsize - 1; *srcptr;)
  {
    if (*srcptr < 0x80)
      *dstptr++ = *srcptr++;		/* ASCII */
    else if (saw_8859)
    {
     /*
      * Map ISO-8859-1 (most likely character set for legacy LPD clients) to
      * UTF-8...
      */

      if (dstptr > (dstend - 2))
        break;

      *dstptr++ = 0xc0 | (*srcptr >> 6);
      *dstptr++ = 0x80 | (*srcptr++ & 0x3f);
    }
    else if ((*srcptr & 0xe0) == 0xc0 && (srcptr[1] & 0xc0) == 0x80)
    {
     /*
      * 2-byte UTF-8 sequence...
      */

      if (dstptr > (dstend - 2))
        break;

      *dstptr++ = *srcptr++;
      *dstptr++ = *srcptr++;
    }
    else if ((*srcptr & 0xf0) == 0xe0 && (srcptr[1] & 0xc0) == 0x80 && (srcptr[2] & 0xc0) == 0x80)
    {
     /*
      * 3-byte UTF-8 sequence...
      */

      if (dstptr > (dstend - 3))
        break;

      *dstptr++ = *srcptr++;
      *dstptr++ = *srcptr++;
      *dstptr++ = *srcptr++;
    }
    else if ((*srcptr & 0xf8) == 0xf0 && (srcptr[1] & 0xc0) == 0x80 && (srcptr[2] & 0xc0) == 0x80 && (srcptr[3] & 0xc0) == 0x80)
    {
     /*
      * 4-byte UTF-8 sequence...
      */

      if (dstptr > (dstend - 4))
        break;

      *dstptr++ = *srcptr++;
      *dstptr++ = *srcptr++;
      *dstptr++ = *srcptr++;
      *dstptr++ = *srcptr++;
    }
    else
    {
     /*
      * Bad UTF-8 sequence, this must be an ISO-8859-1 string...
      */

      saw_8859 = 1;

      if (dstptr > (dstend - 2))
        break;

      *dstptr++ = 0xc0 | (*srcptr >> 6);
      *dstptr++ = 0x80 | (*srcptr++ & 0x3f);
    }
  }

  *dstptr = '\0';
}
