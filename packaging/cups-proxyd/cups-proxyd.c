 /***
  This file is part of cups-filters.

  This file is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  This file is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General
  Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with avahi; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
  USA.
***/

#define _GNU_SOURCE

#include <ctype.h>
#if defined(__OpenBSD__)
#include <sys/socket.h>
#endif /* __OpenBSD__ */
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <errno.h>

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-glib/glib-watch.h>
#include <avahi-common/error.h>

#include <cups/cups.h>

#include "cups-notifier.h"

/* Attribute to mark a CUPS queue as created by us */
#define CUPS_PROXYD_MARK "cups-proxyd"

/* Update delay and interval in msec */
#define UPDATE_DELAY 500
#define UPDATE_INTERVAL 2000

#define NOTIFY_LEASE_DURATION (24 * 60 * 60)
#define CUPS_DBUS_PATH "/org/cups/cupsd/Notifier"

#define DEFAULT_LOGDIR "/var/log/cups"
#define DEBUG_LOG_FILE "/cups-proxyd_log"
#define DEBUG_LOG_FILE_2 "/cups-proxyd_previous_logs"

/* Data structure for destination list obtained with cupsEnumDests() */
typedef struct dest_list_s {
  int num_dests;
  cups_dest_t *dests;
  cups_dest_t *default_dest;
  int         temporary_dests;
  int         current_dest;
} dest_list_t;

static char *proxy_cups_server = NULL;
static char *system_cups_server = NULL;
static http_t *proxy_conn = NULL;
static http_t *system_conn = NULL;
static cups_array_t *proxy_printers = NULL;

static long update_delay = UPDATE_DELAY;
static long update_interval = UPDATE_INTERVAL;
static struct timeval last_update;
static guint update_timer_id = 0;
static guint queues_timer_id = 0;

static GMainLoop *gmainloop = NULL;

static CupsNotifier *cups_notifier = NULL;

static AvahiGLibPoll *glib_poll = NULL;
static AvahiClient *client = NULL;
static AvahiServiceBrowser *sb1 = NULL, *sb2 = NULL;
static int avahi_present = 0;

static unsigned int DebugLogFileSize = 1024;
static unsigned int HttpLocalTimeout = 2;

static int debug_stderr = 0;
static int debug_logfile = 0;
static FILE *lfp = NULL;

static char logdir[1024];
static char debug_log_file[2048];
static char debug_log_file_bckp[2048];

static int terminating = 0;


/* Open the debug log file if we use one */
void
start_debug_logging()
{
  if (debug_log_file[0] == '\0')
    return;
  if (lfp == NULL)
    lfp = fopen(debug_log_file, "a+");
  if (lfp == NULL) {
    fprintf(stderr, "cups-proxyd: ERROR: Failed creating debug log file %s\n",
      debug_log_file);
    exit(1);
  }
}


/* Close the debug log file if we use one */
void
stop_debug_logging()
{
  debug_logfile = 0;
  if (lfp)
    fclose(lfp);
  lfp = NULL;
}


/* returns the size of the debug log file */
long int
findLogFileSize() 
{ 
  FILE* fp = fopen(debug_log_file, "r"); 
  if (fp == NULL) { 
    return (-1); 
  } 
  fseek(fp, 0L, SEEK_END); 
  long int res = ftell(fp); 
  fclose(fp); 
  return (res); 
}


/* Copy a file */
void
copyToFile(FILE **fp1, FILE **fp2)
{
  int buffer_size = 2048;
  char *buf = (char*) malloc(sizeof(char)*buffer_size);
  if(!buf){
    fprintf(stderr,"Error creating buffer for debug logging\n");
    return;
  }
  fseek(*fp1, 0, SEEK_SET);
  size_t r;
  do {
    r = fread(buf, sizeof(char), buffer_size, *fp1);
    fwrite(buf, sizeof(char), r, *fp2);
  } while(r==buffer_size);
}


/* Output a log line, to stderr and/or into the log file, also check
   the log file size limit and rotate the log */
void
debug_printf(const char *format, ...)
{
  if (debug_stderr || debug_logfile) {
    time_t curtime = time(NULL);
    char buf[64];
    ctime_r(&curtime, buf);
    while(isspace(buf[strlen(buf)-1])) buf[strlen(buf)-1] = '\0';
    va_list arglist;
    if (debug_stderr) {
      va_start(arglist, format);
      fprintf(stderr, "%s ", buf);
      vfprintf(stderr, format, arglist);
      fflush(stderr);
      va_end(arglist);
    }
    if (debug_logfile && lfp) {
      va_start(arglist, format);
      fprintf(lfp, "%s ", buf);
      vfprintf(lfp, format, arglist);
      fflush(lfp);
      va_end(arglist);
    }

    long int log_file_size = findLogFileSize(); 
    if(DebugLogFileSize>0 && log_file_size>(long int)DebugLogFileSize*1024){
      fclose(lfp);
      FILE *fp1 = fopen(debug_log_file, "r");
      FILE *fp2 = fopen(debug_log_file_bckp, "w");
      copyToFile(&fp1,&fp2);
      fclose(fp1);
      fclose(fp2);
      lfp = fopen(debug_log_file, "w");
    }
  }
}


/* Output a multi-line log produced by a library function into the
   debug log, each line in the correct format */
void
debug_log_out(char *log) {
  if (debug_stderr || debug_logfile) {
    time_t curtime = time(NULL);
    char buf[64];
    char *ptr1, *ptr2;
    ctime_r(&curtime, buf);
    while(isspace(buf[strlen(buf)-1])) buf[strlen(buf)-1] = '\0';
    ptr1 = log;
    while(ptr1) {
      ptr2 = strchr(ptr1, '\n');
      if (ptr2) *ptr2 = '\0';
      if (debug_stderr)
	fprintf(stderr, "%s %s\n", buf, ptr1);
      if (debug_logfile && lfp)
	fprintf(lfp, "%s %s\n", buf, ptr1);
      if (ptr2) *ptr2 = '\n';
      ptr1 = ptr2 ? (ptr2 + 1) : NULL;
    }
  }
}


/* CUPS Password callback to avoid that the daemon gets stuck in password
   prompts of libcups functions */
static const char *
password_callback (const char *prompt,
		   http_t *http,
		   const char *method,
		   const char *resource,
		   void *user_data)
{
  return NULL;
}


/* Connect to CUPS with encryption and shortened timeout */
http_t *
httpConnectEncryptShortTimeout(const char *host, int port,
			       http_encryption_t encryption)
{
  return (httpConnect2(host, port, NULL, AF_UNSPEC, encryption, 1, 3000,
                       NULL));
}


/* Log timeout events on HTTP connections to CUPS */
int
http_timeout_cb(http_t *http, void *user_data)
{
  debug_printf("HTTP timeout!\n");
  return 0;
}


/* HTTP connection to CUPS, CUPS instanced to connec to is defined by a string
   with either the domain socket path or hostname and port */
static http_t *
http_connect(http_t **conn, const char *server)
{
  if (!conn)
    return (NULL);

  const char *server_str = strdup(server);
  int port = 631;
  char *p = strrchr(server_str, ':');

  if (p) {
    *p = '\0';
    port = atoi(p + 1);
  }

  if (!*conn) {
    if (server_str[0] == '/')
      debug_printf("cups-proxyd: Creating http connection to CUPS daemon via domain socket: %s\n",
		   server_str);
    else
      debug_printf("cups-proxyd: Creating http connection to CUPS daemon: %s:%d\n",
		   server_str, port);
    *conn = httpConnectEncryptShortTimeout(server_str, port,
					   cupsEncryption());
  }
  if (*conn)
    httpSetTimeout(*conn, HttpLocalTimeout, http_timeout_cb, NULL);
  else {
    if (server_str[0] == '/')
      debug_printf("cups-proxyd: Failed creating http connection to CUPS daemon via domain socket: %s\n",
		   server_str);
    else
      debug_printf("cups-proxyd: Failed creating http connection to CUPS daemon: %s:%d\n",
		   server_str, port);
  }

  return *conn;
}


/* Connect to the proxy CUPS daemon and also tell the libcups functions to
   use the proxy CUPS */
static http_t *
http_connect_proxy(void)
{
  if (!proxy_cups_server)
    return NULL;
  cupsSetServer(proxy_cups_server);
  return http_connect(&proxy_conn, proxy_cups_server);
}


/* Connect to the system's CUPS daemon and also tell the libcups functions to
   use the system's CUPS */
static http_t *
http_connect_system(void)
{
  cupsSetServer(system_cups_server);
  return http_connect(&system_conn, system_cups_server);
}


/* Close the specified HTTP connection */
static void
http_close(http_t **conn)
{
  if (!conn)
    return;

  if (*conn) {
    httpClose (*conn);
    *conn = NULL;
  }
}


/* Close connection to proxy CUPS */
static void
http_close_proxy(void)
{
  if (!proxy_cups_server)
    return;
  debug_printf("cups-proxyd: Closing connection to proxy CUPS daemon.\n");
  http_close(&proxy_conn);
}


/* Close connection to system's CUPS */
static void
http_close_system(void)
{
  debug_printf("cups-proxyd: Closing connection to system's CUPS daemon.\n");
  http_close(&system_conn);
}


/* Callback for cupsEnumDests() to add the name of each found printer to a
   CUPS array, we use it to list the queues on the proxy CUPS daemon */
int
add_printer_name_cb(cups_array_t *user_data, unsigned flags, cups_dest_t *dest)
{
  char *p;
  
  if (flags & CUPS_DEST_FLAGS_REMOVED) {
    /* Remove printer name from array */
    if ((p = cupsArrayFind(user_data, dest->name)) != NULL) {
      cupsArrayRemove(user_data, p);
      free(p);
    }
  } else
    /* Add printer name to array... */
    cupsArrayAdd(user_data, strdup(dest->name));
  return (1);
}


/* Callback for cupsEnumDests() to add the CUPS dest record of each found
   printer to a list, we use it to get all print queue info from the
   system's CUPS */
int
add_dest_cb(dest_list_t *user_data, unsigned flags, cups_dest_t *dest)
{
  if (flags & CUPS_DEST_FLAGS_REMOVED)
    /* Remove destination from array */
    user_data->num_dests =
      cupsRemoveDest(dest->name, dest->instance, user_data->num_dests,
		     &(user_data->dests));
  else {
    /* Add destination to array... */
    user_data->num_dests =
      cupsCopyDest(dest, user_data->num_dests,
		   &(user_data->dests));
    if (dest->is_default)
      user_data->default_dest =
	cupsGetDest(dest->name, dest->instance, user_data->num_dests,
		    user_data->dests);
  }
  return (1);
}


/* Get a list of the names of all permanent queues on the proxy CUPS
   daemon.  We do not expect temporary queues for discovered printers
   on the proxy CUPS daemon, as we clone the system's queues including
   the temporary one and the proxy CUPS "sees" the same network
   printers as the system's CUPS, meaning that they cannot be any
   further temporary queues.
   We use this list when starting to find whether there leftover queue
   to be removed on the proxy */
static cups_array_t*
get_proxy_printers (void)
{
  cups_array_t *printer_list;

  debug_printf("cups-proxyd (%s): cupsEnumDests\n", proxy_cups_server);

  /* Create array for destination list */
  printer_list = cupsArrayNew((cups_array_func_t)strcasecmp, NULL);

  /* Proxy's CUPS daemon */
  cupsSetServer(proxy_cups_server);

  /* Get a list of only permanent queues. As we copy also the not
     actually existing queues of the system's CUPS which would be
     created on-demand for a discovered printer, the proxy's CUPS
     would not create temporary queues anyway (it discovers the same
     printers as the system's CUPS) */
  cupsEnumDests(CUPS_DEST_FLAGS_NONE, 1000, NULL, CUPS_PRINTER_LOCAL,
		CUPS_PRINTER_DISCOVERED, (cups_dest_cb_t)add_printer_name_cb,
		printer_list);
  return printer_list;
}


/* Get all printers of the system's CUPS, including discovered printers
   for which the system's CUPS would create a temporary queue. All these
   will be cloned to the proxy CUPS */
static dest_list_t*
get_system_printers (void)
{
  dest_list_t *dest_list;
  char *default_printer = NULL;

  debug_printf("cups-proxyd (%s): cupsEnumDests\n", system_cups_server);

  /* Memory for destination list header */
  if ((dest_list = (dest_list_t *)calloc(1, sizeof(dest_list_t))) ==
      NULL) {
    debug_printf("ERROR: Unable to allocate memory for system's print queue list.\n");
    return NULL;
  }

  /* System's CUPS daemon */
  cupsSetServer(system_cups_server);

  /* Get a complete queue list, including the discovered printers for
     which a temporary queue gets created on demand, from the system's
     CUPS daemon */
  cupsEnumDests(CUPS_DEST_FLAGS_NONE, 1000, NULL, 0, 0,
		(cups_dest_cb_t)add_dest_cb, dest_list);
  return dest_list;
}


/* Free the list of destination when completing an update run */
void
free_dest_list (dest_list_t *dest_list)
{
  if (dest_list) {
    if (dest_list->num_dests > 0 && dest_list->dests)
      cupsFreeDests (dest_list->num_dests, dest_list->dests);
    free(dest_list);
  }
}


/* This function replaces cupsGetPPD2(), but is much simplified,
   and it works with non-standard (!= 631) ports */
char*
loadPPD(http_t *http,
	const char *name)
{
  char uri[HTTP_MAX_URI];
  char *resource;
  int fd, status;
  char tempfile[1024] = "";

  /* Download URI and resource for the PPD file (this works also for
     classes, returning the first member's PPD file) */
  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "http", NULL,
		   "localhost", 0, "/printers/%s.ppd", name);
  resource = strstr(uri, "/printers/");

  /* Download the file */
  fd = cupsTempFd(tempfile, sizeof(tempfile));
  status = cupsGetFd(http, resource, fd);
  close(fd);

  /* Check for errors */
  if (status == HTTP_STATUS_OK)
  {
    if (tempfile[0])
      return(strdup(tempfile));
  }
  else if (tempfile[0])
    unlink(tempfile);
  return NULL;
}


/* Set the default on the proxy CUPS daemon, this way we can also sync
   the system's default printer with the proxy */ 
int
set_default_printer_on_proxy(const char *printer) {
  ipp_t *request;
  char uri[HTTP_MAX_URI];
  http_t *http = NULL;

  if (terminating)
    return (1);
  
  if (printer == NULL)
    return (1);

  debug_printf("Setting printer %s as default on proxy CUPS daemon.\n",
	       printer);

  if (!proxy_cups_server)
    return (1);

  /* Proxy CUPS daemon */
  cupsSetServer(proxy_cups_server);

  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                   "localhost", 0, "/printers/%s", printer);
  request = ippNewRequest(IPP_OP_CUPS_SET_DEFAULT);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
               "printer-uri", NULL, uri);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name",
               NULL, cupsUser());

  /* Set the printer as default on the proxy CUPS daemon */
  if ((http = http_connect_proxy()) == NULL) {
    debug_printf("Could not connect to proxy CUPS daemon.\n");
    return (0);
  }
  
  ippDelete(cupsDoRequest(http, request, "/admin/"));

  if (cupsLastError() > IPP_STATUS_OK_EVENTS_COMPLETE) {
    debug_printf("ERROR: Failed setting proxy CUPS default printer to %': %s\n",
		 printer, cupsLastErrorString());
    return (0);
  }
  debug_printf("Successfully set proxy CUPS default printer to %s\n",
	       printer);
  return (1);
}


/* Remove a queue from the proxy, so we can reflect queue removal on
   the system's CUPS */
static int
remove_queue_from_proxy (const char *name)
{
  http_t        *http;
  char          uri[HTTP_MAX_URI];
  ipp_t         *request;
  char          *p;


  if (terminating)
    return (1);
  
  debug_printf("Removing proxy CUPS queue %s.\n", name);

  if (!proxy_cups_server)
    return (1);

  /* Proxy CUPS daemon */
  cupsSetServer(proxy_cups_server);

  /* Remove the CUPS queue */
  request = ippNewRequest(CUPS_DELETE_PRINTER);

  /* Printer URI: ipp://localhost/printers/<queue name> */
  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
		   "localhost", 0, "/printers/%s", name);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
	       "printer-uri", NULL, uri);

  /* Default user */
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
	       "requesting-user-name", NULL, cupsUser());

  /* Remove the queue from the proxy CUPS daemon */
  if ((http = http_connect_proxy()) == NULL) {
    debug_printf("Could not connect to proxy CUPS daemon.\n");
    return (0);
  }
  
  /* Do it */
  ippDelete(cupsDoRequest(http, request, "/admin/"));

  if (cupsLastError() > IPP_STATUS_OK_EVENTS_COMPLETE &&
      cupsLastError() != IPP_STATUS_ERROR_NOT_FOUND) {
    debug_printf("Unable to remove CUPS queue! (%s)\n",
		 cupsLastErrorString());
    return (0);
  }

  /* Remove entry from list */
  if ((p = cupsArrayFind(proxy_printers, (char *)name)) != NULL) {
    cupsArrayRemove(proxy_printers, p);
    free(p);
  }

  return (1);
}


/* Create a queue on the proxy CUPS daemon which resembles the given
   queue from the system's CUPS. Having the same PPD (but without
   filter definitions) and basic properties of the system's queue
   print dialogs offer the same options. Jobs are passed through to
   the system's CUPS daemon without filtering. */
static int
clone_system_queue_to_proxy (cups_dest_t *dest)
{
  http_t        *http;
  char          *loadedppd = NULL;
  char          uri[HTTP_MAX_URI], device_uri[HTTP_MAX_URI],
                ppdfile[1024], line[1024];
  int           num_options;
  cups_option_t *options;
  ipp_t         *request;
  int           i, is_temporary = 0, ap_remote_queue_id_line_inserted;
  cups_file_t   *in, *out;
  const char    *ptype;
  const char    *val;
  char          buf[HTTP_MAX_HOST];
  int           p;
  char          host[HTTP_MAX_URI],     /* Printer: Hostname */
                resource[HTTP_MAX_URI], /* Printer: Resource path */
                scheme[32],             /* Printer: URI's scheme */
                username[64];           /* Printer: URI's username */
  int           port = 0;               /* Printer: URI's port number */


  if (terminating)
    return (1);
  
  debug_printf("Cloning printer %s from system's CUPS to proxy CUPS.\n",
	       dest->name);

  /* First we need to copy the PPD file from the original (system's)
     queue if it has one. It will be modified to suppress filtering on
     the proxy but provides options and default seetings of the
     original PPD, so that clients show the same in their print
     dialogs as with the original PPD */

  if ((http = http_connect_system()) == NULL) {
     debug_printf("Could not connect to system's CUPS daemon.\n");
     return (0);
  }

  /* Is this a permanent queue or a temporary queue for a discovered
     IPP printer? */
  ptype = cupsGetOption("printer-type", dest->num_options,
					dest->options);
  if (ptype && (atoi(ptype) & CUPS_PRINTER_DISCOVERED))
    is_temporary = 1;
  
  /* If the original queue is only created on-demand this call makes
     the system's CUPS actually create the queue so that we can grab
     the PPD. We discard the result of the call. */
  if (is_temporary) {
    debug_printf("Establishing dummy connection to make the system's CUPS create the temporary queue.\n");
    cups_dinfo_t *dinfo = cupsCopyDestInfo(http, dest);
    if (dinfo == NULL) {
      debug_printf("Unable to connect to destination and/or to create the temporary queue, not able to clone this printer\n");
      return(0);
    } else {
      debug_printf("Temporary queue created.\n");
      cupsFreeDestInfo(dinfo);
      /* Check whether this queue, if temporary, is of CUPS echoing its own
	 shared printer as discovered printer  and skip such useless queues */
      p = 0;
      val = cupsGetOption("device-uri", dest->num_options, dest->options);
      if (val && !strncasecmp(val, "ipp", 3) &&
	  (strcasestr(val, "/printers/") || strcasestr(val, "/classes/"))) {
	if (gethostname(buf, sizeof(buf) - 1))
	  buf[0] = '\0';
	p = ippPort();
	httpSeparateURI(HTTP_URI_CODING_ALL, val,
			scheme, sizeof(scheme) - 1,
			username, sizeof(username) - 1,
			host, sizeof(host) - 1,
			&port,
			resource, sizeof(resource) - 1);
      }
    }
    if (p &&
	(!strcasecmp(scheme, "ipp") || !strcasecmp(scheme, "ipps")) &&
	(port == p || port == 0) &&
	(!strncasecmp(resource, "/printers/", 10) ||
	 !strncasecmp(resource, "/classes/", 9)) &&
	((buf[0] &&
	  strncasecmp(host, buf, strlen(buf)) == 0 &&
	  (strlen(host) == strlen(buf) ||
	   (strlen(host) > strlen(buf) &&
	    (strcasecmp(host + strlen(buf), ".local") == 0 ||
	     strcasecmp(host + strlen(buf), ".local.") == 0)))) ||
	 (strncasecmp(host, "localhost", 9) == 0 &&
	  (strlen(host) == 9 ||
	   (strlen(host) > 9 &&
	    (strcasecmp(host + 9, ".local") == 0 ||
	     strcasecmp(host + 9, ".local.") == 0)))))) {
      debug_printf("The queue %s is a shared printer of the system's CUPS echoed as a printer discovered by the system's CUPS, skipping/removing!\n",
		   dest->name);
      remove_queue_from_proxy(dest->name);
      return (1);
    }
  }

  /* Load the queue's PPD file from the system's CUPS */
  if ((loadedppd = loadPPD(http, dest->name)) == NULL) {
    debug_printf("Unable to load PPD from queue %s on the system!\n",
		 dest->name);
    if (is_temporary) {
      debug_printf("Discovered printers/Temporary queues have always a PPD, skipping.\n");
      return (0);
    }
  } else {
    debug_printf("Loaded PPD file %s from queue %s on the system.\n",
		 loadedppd, dest->name);
  }

  /* Non-raw queue with PPD, the usual thing */
  ppdfile[0] = '\0';
  if (loadedppd) {
    if ((out = cupsTempFile2(ppdfile, sizeof(ppdfile))) == NULL) {
      debug_printf("Unable to create temporary file!\n");
      unlink(loadedppd);
      free(loadedppd);
    } else if ((in = cupsFileOpen(loadedppd, "r")) == NULL) {
      debug_printf("Unable to open the downloaded PPD file!\n");
      cupsFileClose(out);
      unlink(loadedppd);
      free(loadedppd);
      unlink(ppdfile);
      ppdfile[0] = '\0';
    } else {
      debug_printf("Editing PPD file for printer %s, to mark it as remote CUPS printer and to do not do the conversion from PDF to the printer's native format, saving the resulting PPD in %s.\n",
		   dest->name, ppdfile);
      ap_remote_queue_id_line_inserted = 0;
      while (cupsFileGets(in, line, sizeof(line))) {
	if (strncmp(line, "*cupsFilter:", 12) &&
	    strncmp(line, "*cupsFilter2:", 13)) {
	  /* Write an "APRemoteQueueID" line to make this queue marked
	     as remote printer by CUPS */
	  if (strncmp(line, "*%", 2) &&
	      strncmp(line, "*PPD-Adobe:", 11) &&
	      ap_remote_queue_id_line_inserted == 0) {
	    ap_remote_queue_id_line_inserted = 1;
	    cupsFilePrintf(out, "*APRemoteQueueID: \"\"\n");
	  }
	  /* Simply write out the line as we read it */
	  cupsFilePrintf(out, "%s\n", line);
	}
      }
      cupsFilePrintf(out, "*cupsFilter2: \"application/vnd.cups-pdf application/pdf 0 -\"\n");
      cupsFileClose(in);
      cupsFileClose(out);
      unlink(loadedppd);
      free(loadedppd);
    }
  }

  if (!proxy_cups_server)
    return (1);

  /* Proxy CUPS daemon */
  cupsSetServer(proxy_cups_server);

  /* Create a new CUPS queue or modify the existing queue */
  request = ippNewRequest(CUPS_ADD_MODIFY_PRINTER);
  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
		   "localhost", 0, "/printers/%s", dest->name);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
	       "printer-uri", NULL, uri);
  /* Default user */
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
	       "requesting-user-name", NULL, cupsUser());
  /* Queue should be enabled ... */
  ippAddInteger(request, IPP_TAG_PRINTER, IPP_TAG_ENUM, "printer-state",
		IPP_PRINTER_IDLE);
  /* ... and accepting jobs */
  ippAddBoolean(request, IPP_TAG_PRINTER, "printer-is-accepting-jobs", 1);

  num_options = 0;
  options = NULL;

  /* Device URI: proxy://<system's CUPS socket>/<remote queue> */
  httpAssembleURIf(HTTP_URI_CODING_ALL, device_uri, sizeof(device_uri),
		   "proxy", NULL, system_cups_server, 0, "/%s",
		   dest->name);
  num_options = cupsAddOption("device-uri", device_uri,
			      num_options, &options);
  /* Option cups-proxyd=true, marking that we have created this queue */
  num_options = cupsAddOption(CUPS_PROXYD_MARK "-default", "true",
			      num_options, &options);
  /* Default option settings from printer entry */
  for (i = 0; i < dest->num_options; i ++) {
    debug_printf("   %s=%s\n", dest->options[i].name, dest->options[i].value);
    if (strcasecmp(dest->options[i].name, "printer-is-shared") &&
	strcasecmp(dest->options[i].name, "device-uri"))
      num_options = cupsAddOption(dest->options[i].name,
				  dest->options[i].value,
				  num_options, &options);
  }
  /* Make sure that the PPD file on the proxy CUPS gets removed if the
     queue of the system's CUPS gets turned into a raw queue */
  if (!ppdfile[0])
    num_options = cupsAddOption("ppd-name", "raw",
				num_options, &options);

  /* Encode option list into IPP attributes */
  cupsEncodeOptions2(request, num_options, options, IPP_TAG_OPERATION);
  cupsEncodeOptions2(request, num_options, options, IPP_TAG_PRINTER);

  /* Create the queue on the proxy CUPS daemon */
  if ((http = http_connect_proxy()) == NULL) {
     debug_printf("Could not connect to proxy CUPS daemon.\n");
     return (0);
  }
  
  /* Do it */
  if (ppdfile[0]) {
    debug_printf("Non-raw queue %s with PPD file: %s\n", dest->name, ppdfile);
    ippDelete(cupsDoFileRequest(http, request, "/admin/", ppdfile));
    unlink(ppdfile);
  } else {
    debug_printf("Raw queue %s\n", dest->name);
    ippDelete(cupsDoRequest(http, request, "/admin/"));
  }
  cupsFreeOptions(num_options, options);

  if (cupsLastError() > IPP_STATUS_OK_EVENTS_COMPLETE) {
    debug_printf("Unable to create/modify CUPS queue (%s)!\n",
		 cupsLastErrorString());
    return (0);
  }

  if (cupsArrayFind(proxy_printers, dest->name) == NULL)
    cupsArrayAdd(proxy_printers, strdup(dest->name));

  if (dest->is_default) {
    debug_printf("%s is the system's default printer.\n", dest->name);
    if (set_default_printer_on_proxy(dest->name))
      debug_printf("Set %s as default printer on proxy.\n", dest->name);
    else
      debug_printf("Could not set %s as default printer on proxy!\n", dest->name);
  }

  return (1);
}


/* For updating each of the queues of the system's CUPS daemon on the
   proxy during an update run, we do not go through all of them in a
   loop but instead, we call this function again and again, once for
   each queue, via g_timeout_add() with a zero-second timeout. This
   way other events in the main loop can stop the process after any
   queue, especially a new update run can start when the previous one
   did not finish yet. This assures quick updating of the most
   important (the permanent queues, see below) queues which are done
   in the beginning of the update run.

   We also do not go through all printers as they appear in the list, but
   instead, run through the list twice, once doing the permanent queues
   and once the discovered printers which let CUPS create a temporary
   queue on-demand.

   We do this as the manually created queues are usually the most
   important and cloning them to the proxy is much faster than cloning
   the temporary queues as CUPS has to actually create those so that we
   can grab their PPD files. This way the the important queues get updated
   right away quickly in the beginning of an update run. */
static gboolean
update_next_proxy_printer (gpointer user_data)
{
  dest_list_t *system_printers = (dest_list_t *)user_data;
  int cloned = 0;
  const char *val;

  if (terminating)
    goto finish;
  
 next:
  val = cupsGetOption("printer-type",
    system_printers->dests[system_printers->current_dest].num_options,
    system_printers->dests[system_printers->current_dest].options);

  if ((!system_printers->temporary_dests &&
       (val && !(atoi(val) & CUPS_PRINTER_DISCOVERED))) ||
      (system_printers->temporary_dests &&
       (!val || (atoi(val) & CUPS_PRINTER_DISCOVERED)))) {
    debug_printf("Cloning %s queue %s from the system's CUPS to the proxy CUPS.\n",
		 system_printers->temporary_dests ? "temporary" : "permanent",
		 system_printers->dests[system_printers->current_dest].name);
    if (!clone_system_queue_to_proxy
	(&(system_printers->dests[system_printers->current_dest])))
      debug_printf("Unable to clone queue %s!\n",
		   system_printers->dests[system_printers->current_dest].name);
    cloned = 1;
  }

  if (system_printers->current_dest < system_printers->num_dests - 1) {
    system_printers->current_dest ++;
  } else if (!system_printers->temporary_dests) {
    system_printers->temporary_dests ++;
    system_printers->current_dest = 0;
  } else
    goto finish;

  if (!cloned)
    goto next;

  /* Repeat this function for the next printer */
  return (TRUE);

 finish:
  /* Close the connections to the CUPS daemons to not try to keep them
     open internally and CUPS closing them without notice */
  http_close_proxy();
  http_close_system();

  /* Free the memory occupied by the system's destination list */
  free_dest_list(system_printers);

  /* Mark that we are done */
  queues_timer_id = 0;

  /* We are done, do not repeat again */
  return (FALSE);
}


/* Start a new update run, killing any previous update run still running,
   marking its start time (to prevent the next update run to start earlier
   than update_interval msecs from now), removing queues from the proxy
   which have disappeared on the system, and finally kicking off the
   updating of each system's queue on the proxy, each queue as a separate
   timeout event, to allow easy stopping of the chain. */
static gboolean
update_proxy_printers (gpointer user_data)
{
  dest_list_t *system_printers;
  const char *pname;

  (void)user_data;
  
  if (terminating)
    return (FALSE);

  /* Kill previous update if it is still running */
  if (queues_timer_id) {
    debug_printf("Killing previous update.\n");
    g_source_remove (queues_timer_id);
    queues_timer_id = 0;
  }

  /* Mark the start of this update */
  gettimeofday(&last_update, NULL);
  update_timer_id = 0;
  
  /* Get list of printers on the system's CUPS */
  system_printers = get_system_printers();
  system_printers->temporary_dests = 0;
  system_printers->current_dest = 0;

  /* Check whether one of the printers on the system's CUPS has disappeared
     compared to the current state of the proxy */
  if (proxy_cups_server)
    for (pname = (const char *)cupsArrayFirst(proxy_printers);
	 pname;
	 pname = (const char *)cupsArrayNext(proxy_printers)) {
      if (terminating)
	return (FALSE);
      if (!cupsGetDest(pname, NULL,
		       system_printers->num_dests, system_printers->dests)) {
	debug_printf("Queue %s disappeared on the system, removing it from proxy.\n", pname);
	if (!remove_queue_from_proxy(pname))
	  debug_printf("Could not remove queue %s from proxy!\n", pname);
      }
    }

  /* Schedule the update for the system's queues */
  debug_printf("Cloning queues from the system to the proxy.\n");
  queues_timer_id =
    g_timeout_add(0, update_next_proxy_printer, system_printers);

  /* Do not repeat this part */
  return (FALSE);
}


/* When an event (DNS-SD or CUPS D-Bus notification) requests an
   update of the queues on the proxy CUPS, it calls this function and
   here the next update run gets scheduled. To not cause a flooding
   with update runs due to the fact that often the same change on the
   system's CUPS creates lots of events (often one DNS-SD event for
   each network interface, multiplied by 2 for IPP/IPPS and multiplied
   by 2 again for IPv4/IPv6).

   So we do not start more than one update run every 2 seconds
   (update_interval, adjustable) so that at least the most important
   (permanent) queues get updated before the update run gets killed by
   another update run.

   We also delay the start of the update run 0.5 seconds
   (update_delay, adjustable) after the first event, so that if
   several changes occur shortly after each otherb that they get
   treated by a single update run instead of causing several
   additional ones. To assure this we do not schedule any further
   update run if one is scheduled and waiting for its time to take
   off. */
static void
schedule_proxy_update (void)
{
  struct timeval now;
  long delay;
  const char *pname;

  if (terminating)
    return;
  
  /* No scheduling without main loop */
  if (!gmainloop)
    return;

  /* We already have scheduled an update, do not scedule another one */
  if (update_timer_id) {
    debug_printf("Update of queues on proxy CUPS already scheduled!\n");
    return;
  }

  /* Calculate the delay when we call update_proxy_printers(), so that the
     function does not get called more often than once in UPDATE_INTERVAL
     msecs
     Delay the cupdate at least UPDATE_DELAY msecs from now, as often one
     change on the system's CUPS causes several Avahi event, and then
     we had one update triggered immediately and another after
     UPDATE_INTERVAL msecs */
  gettimeofday(&now, NULL);
  delay = update_interval - (now.tv_sec - last_update.tv_sec) * 1000 -
    (now.tv_usec - last_update.tv_usec) / 1000;
  if (delay < update_delay)
    delay = update_delay;

  /* Schedule the update */
  debug_printf("Updating queues on proxy CUPS in %d msecs\n", delay);
  update_timer_id =
    g_timeout_add(delay, update_proxy_printers, NULL);
}


/* Create a subscription for D-Bus notifications on the system's
   CUPS. This makes the CUPS daemon fire up a D-Bus notifier
   process. */
static int
create_subscription ()
{
  ipp_t *req;
  ipp_t *resp;
  ipp_attribute_t *attr;
  int id = 0;
  http_t *http = NULL;

  debug_printf("Creating subscription to D-Bus notifications on the system's CUPS daemon.\n");
  
  http = http_connect_system();
  if (http == NULL) {
    debug_printf("Cannot connect to the system's CUPS daemon to subscribe to notifications!\n");
    return 0;
  }

  req = ippNewRequest (IPP_CREATE_PRINTER_SUBSCRIPTION);
  ippAddString(req, IPP_TAG_OPERATION, IPP_TAG_URI,
	       "printer-uri", NULL, "/");
  ippAddString(req, IPP_TAG_SUBSCRIPTION, IPP_TAG_KEYWORD,
	       "notify-events", NULL, "all");
  ippAddString(req, IPP_TAG_SUBSCRIPTION, IPP_TAG_URI,
	       "notify-recipient-uri", NULL, "dbus://");
  ippAddInteger(req, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER,
		"notify-lease-duration", NOTIFY_LEASE_DURATION);

  resp = cupsDoRequest (http, req, "/");
  if (!resp || cupsLastError() != IPP_STATUS_OK) {
    debug_printf("Error subscribing to CUPS notifications: %s\n",
		 cupsLastErrorString ());
    http_close_system();
    return 0;
  }

  attr = ippFindAttribute (resp, "notify-subscription-id", IPP_TAG_INTEGER);
  if (attr)
    id = ippGetInteger (attr, 0);
  else
    debug_printf("ipp-create-printer-subscription response doesn't contain "
		 "subscription id!\n");

  ippDelete (resp);
  http_close_system();
  return id;
}


/* Renew the D-Bus notification subscription, telling to CUPS that we
   are still there and it should not let the notifier time out. */
static gboolean
renew_subscription (int id)
{
  ipp_t *req;
  ipp_t *resp;
  http_t *http = NULL;

  http = http_connect_system();
  if (http == NULL) {
    debug_printf("Cannot connect to system's CUPS to renew subscriptions!\n");
    return FALSE;
  }

  req = ippNewRequest (IPP_RENEW_SUBSCRIPTION);
  ippAddInteger(req, IPP_TAG_OPERATION, IPP_TAG_INTEGER,
		"notify-subscription-id", id);
  ippAddString(req, IPP_TAG_OPERATION, IPP_TAG_URI,
	       "printer-uri", NULL, "/");
  ippAddString(req, IPP_TAG_SUBSCRIPTION, IPP_TAG_URI,
	       "notify-recipient-uri", NULL, "dbus://");
  ippAddInteger(req, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER,
		"notify-lease-duration", NOTIFY_LEASE_DURATION);

  resp = cupsDoRequest(http, req, "/");
  if (!resp || cupsLastError() != IPP_STATUS_OK) {
    debug_printf("Error renewing CUPS subscription %d: %s\n",
		 id, cupsLastErrorString ());
    http_close_system();
    return FALSE;
  }

  ippDelete (resp);
  http_close_system();
  return TRUE;
}


/* Function which is called as a timeout event handler to let the
   renewal of the D-Bus subscription be done to the right time. */
static gboolean
renew_subscription_timeout (gpointer userdata)
{
  int *subscription_id = userdata;

  debug_printf("renew_subscription_timeout() in THREAD %ld\n", pthread_self());

  if (*subscription_id <= 0 || !renew_subscription (*subscription_id))
    *subscription_id = create_subscription ();

  return TRUE;
}


/* Cancel the D-Bus notifier subscription, so that CUPS can terminate its
   notifier when we shut down. */
void
cancel_subscription (int id)
{
  ipp_t *req;
  ipp_t *resp;
  http_t *http = NULL;

  if (id <= 0)
    return;

  http = http_connect_system();
  if (http == NULL) {
    debug_printf("Cannot connect to system's CUPS to cancel subscriptions.\n");
    return;
  }

  req = ippNewRequest (IPP_CANCEL_SUBSCRIPTION);
  ippAddString (req, IPP_TAG_OPERATION, IPP_TAG_URI,
		"printer-uri", NULL, "/");
  ippAddInteger (req, IPP_TAG_OPERATION, IPP_TAG_INTEGER,
		 "notify-subscription-id", id);

  resp = cupsDoRequest (http, req, "/");
  if (!resp || cupsLastError() != IPP_STATUS_OK) {
    debug_printf ("Error canceling subscription to CUPS notifications: %s\n",
		  cupsLastErrorString ());
    http_close_system();
    return;
  }

  ippDelete (resp);
  http_close_system();
}


/* D-Bus notification event handler for a printer status change on the
   system's CUPS, for example when a printer is enabled or disabled or
   when the default printer changes. We trigger an update run to
   forward the changes to the proxy. */
static void
on_printer_state_changed (CupsNotifier *object,
                          const gchar *text,
                          const gchar *printer_uri,
                          const gchar *printer,
                          guint printer_state,
                          const gchar *printer_state_reasons,
                          gboolean printer_is_accepting_jobs,
                          gpointer user_data)
{
  debug_printf("on_printer_state_changed() in THREAD %ld\n", pthread_self());
  debug_printf("[System CUPS Notification] Printer state change on printer %s: %s\n",
	       printer, text);
  debug_printf("[System CUPS Notification] Printer state reasons: %s\n",
	       printer_state_reasons);
  debug_printf("[System CUPS Notification] Updating printers on proxy CUPS daemon.\n");

  /* Update printers on proxy CUPS */
  schedule_proxy_update();
}


/* D-Bus notification event handler for a printer removal on the
   system's CUPS. As this does not only mean the manual removal of a
   permanent queue or the physical removal of a discoverable network
   printer, but also the simple removal of a temporary queue, we
   cannot simply remove the corresponding queue on the
   proxy. Therefore we trigger an update run to forward the actual set
   of queues to the proxy. */
static void
on_printer_deleted (CupsNotifier *object,
		    const gchar *text,
		    const gchar *printer_uri,
		    const gchar *printer,
		    guint printer_state,
		    const gchar *printer_state_reasons,
		    gboolean printer_is_accepting_jobs,
		    gpointer user_data)
{
  debug_printf("on_printer_deleted() in THREAD %ld\n", pthread_self());
  debug_printf("[System CUPS Notification] Printer deleted: %s\n", text);
  debug_printf("[System CUPS Notification] Updating printers on proxy CUPS daemon.\n");

  /* Update printers on proxy CUPS */
  schedule_proxy_update();
}


/* D-Bus notification event handler for a printer
   creation/modification event on the system's CUPS. Here we also
   trigger an update run to forward the actual set of queues to the
   proxy. */
static void
on_printer_modified (CupsNotifier *object,
		     const gchar *text,
		     const gchar *printer_uri,
		     const gchar *printer,
		     guint printer_state,
		     const gchar *printer_state_reasons,
		     gboolean printer_is_accepting_jobs,
		     gpointer user_data)
{
  debug_printf("on_printer_modified() in THREAD %ld\n", pthread_self());
  debug_printf("[System CUPS Notification] Printer modified: %s\n", text);
  debug_printf("[System CUPS Notification] Updating printers on proxy CUPS daemon.\n");

  /* Update printers on proxy CUPS */
  schedule_proxy_update();
}


/* Callback to handle the appearing and disappearing of DNS-SD
   services.  As we only browse IPP and IPPS services this means that
   this callback is only triggered on IPP printers (there are a few
   false positives as stand-alone IPP scanners). As IPP printers we
   discover here are also discovered by the system's CUPS they cause
   potential temporary queues there and therefore we need to trigger
   update runs on the proxy here, for both appearing and disappearing
   of IPP printers. We also log the events in the debug log. */
static void browse_callback(AvahiServiceBrowser *b,
			    AvahiIfIndex interface,
			    AvahiProtocol protocol,
			    AvahiBrowserEvent event,
			    const char *name,
			    const char *type,
			    const char *domain,
			    AvahiLookupResultFlags flags,
			    void* userdata) {

  AvahiClient *c = userdata;
  char ifname[IF_NAMESIZE];

  debug_printf("browse_callback() in THREAD %ld\n", pthread_self());

  if (b == NULL)
    return;

  /* Called whenever a new services becomes available on the LAN or
     is removed from the LAN */

  switch (event) {

  /* Avahi browser error */
  case AVAHI_BROWSER_FAILURE:

    debug_printf("[Avahi Browser] ERROR: %s\n",
		 avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b))));
    g_main_loop_quit(gmainloop);
    g_main_context_wakeup(NULL);
    return;

  /* New service (remote printer) */
  case AVAHI_BROWSER_NEW:

    if (c == NULL || name == NULL || type == NULL || domain == NULL)
      return;

    /* Get the interface name */
    if (!if_indextoname(interface, ifname)) {
      debug_printf("[Avahi Browser] Unable to find interface name for interface %d: %s\n",
		   interface, strerror(errno));
      strncpy(ifname, "Unknown", sizeof(ifname) - 1);
    }

    debug_printf("[Avahi Browser] NEW: service '%s' of type '%s' in domain '%s' on interface '%s' (%s)\n",
		 name, type, domain, ifname,
		 protocol != AVAHI_PROTO_UNSPEC ?
		 avahi_proto_to_string(protocol) : "Unknown");
    debug_printf("[Avahi Browser] Updating printers on proxy CUPS daemon.\n");

    /* Update printers on proxy CUPS */
    schedule_proxy_update();

    break;

  /* A service (remote printer) has disappeared */
  case AVAHI_BROWSER_REMOVE:

    if (name == NULL || type == NULL || domain == NULL)
      return;

    /* Get the interface name */
    if (!if_indextoname(interface, ifname)) {
      debug_printf("[Avahi Browser] Unable to find interface name for interface %d: %s\n",
		   interface, strerror(errno));
      strncpy(ifname, "Unknown", sizeof(ifname) - 1);
    }

    debug_printf("[Avahi Browser] REMOVE: service '%s' of type '%s' in domain '%s' on interface '%s' (%s)\n",
		 name, type, domain, ifname,
		 protocol != AVAHI_PROTO_UNSPEC ?
		 avahi_proto_to_string(protocol) : "Unknown");
    debug_printf("[Avahi Browser] Updating printers on proxy CUPS daemon.\n");

    /* Update printers on proxy CUPS */
    schedule_proxy_update();

    break;

  /* All cached Avahi events are treated now */
  case AVAHI_BROWSER_ALL_FOR_NOW:
  case AVAHI_BROWSER_CACHE_EXHAUSTED:
    debug_printf("[Avahi Browser] %s\n",
		 event == AVAHI_BROWSER_CACHE_EXHAUSTED ?
		 "CACHE_EXHAUSTED" : "ALL_FOR_NOW");
    break;
  }
}


/* Shutdown of the two (IPP and IPPS) Avahi browsers. */
void avahi_browser_shutdown() {
  avahi_present = 0;

  /* Free the data structures for DNS-SD browsing */
  if (sb1) {
    avahi_service_browser_free(sb1);
    sb1 = NULL;
  }
  if (sb2) {
    avahi_service_browser_free(sb2);
    sb2 = NULL;
  }
}


/* Shutdown of the client connection to avahi-daemon */
void avahi_shutdown() {
  avahi_browser_shutdown();
  if (client) {
    avahi_client_free(client);
    client = NULL;
  }
  if (glib_poll) {
    avahi_glib_poll_free(glib_poll);
    glib_poll = NULL;
  }
}


/* Callback to handle Avahi events */
static void client_callback(AvahiClient *c, AvahiClientState state, AVAHI_GCC_UNUSED void * userdata) {
  int error;

  if (c == NULL)
    return;

  /* Called whenever the client or server state changes */
  switch (state) {

  /* avahi-daemon available */
  case AVAHI_CLIENT_S_REGISTERING:
  case AVAHI_CLIENT_S_RUNNING:
  case AVAHI_CLIENT_S_COLLISION:

    debug_printf("[Avahi Browser] Avahi server connection got available, setting up service browsers.\n");

    /* Create the service browsers */
    if (!sb1)
      if (!(sb1 =
	    avahi_service_browser_new(c, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
				      "_ipp._tcp", NULL, 0, browse_callback,
				      c))) {
	debug_printf("[Avahi Browser] ERROR: Failed to create service browser for IPP: %s\n",
		     avahi_strerror(avahi_client_errno(c)));
      }
    if (!sb2)
      if (!(sb2 =
	    avahi_service_browser_new(c, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
				      "_ipps._tcp", NULL, 0, browse_callback,
				      c))) {
	debug_printf("[Avahi Browser] ERROR: Failed to create service browser for IPPS: %s\n",
		     avahi_strerror(avahi_client_errno(c)));
      }

    avahi_present = 1;
    
    break;

  /* Avahi client error */
  case AVAHI_CLIENT_FAILURE:

    if (avahi_client_errno(c) == AVAHI_ERR_DISCONNECTED) {
      debug_printf("[Avahi Browser] Avahi server disappeared, shutting down service browsers.\n");
      avahi_browser_shutdown();
      /* Renewing client */
      avahi_client_free(client);
      client = avahi_client_new(avahi_glib_poll_get(glib_poll),
				AVAHI_CLIENT_NO_FAIL,
				client_callback, NULL, &error);
      if (!client) {
	debug_printf("[Avahi Browser] ERROR: Failed to create client: %s\n",
		     avahi_strerror(error));
	avahi_shutdown();
      }
    } else {
      debug_printf("[Avahi Browser] ERROR: Avahi server connection failure: %s\n",
		   avahi_strerror(avahi_client_errno(c)));
      g_main_loop_quit(gmainloop);
      g_main_context_wakeup(NULL);
    }
    break;

  default:
    break;
  }
}


/* Initialization of the connection to the avahi-daemon */
void avahi_init() {
  int error;

  /* Allocate main loop object */
  if (!glib_poll)
    if (!(glib_poll = avahi_glib_poll_new(NULL, G_PRIORITY_DEFAULT))) {
      debug_printf("[Avahi Browser] ERROR: Failed to create glib poll object.\n");
      goto avahi_init_fail;
    }

  /* Allocate a new client */
  if (!client)
    client = avahi_client_new(avahi_glib_poll_get(glib_poll),
			      AVAHI_CLIENT_NO_FAIL,
			      client_callback, NULL, &error);

  /* Check wether creating the client object succeeded */
  if (!client) {
    debug_printf("[Avahi Browser] ERROR: Failed to create client: %s\n",
		 avahi_strerror(error));
    goto avahi_init_fail;
  }

  return;

 avahi_init_fail:
  avahi_shutdown();
}


/* Handler for SIGTERM, for a controlled shutdown of the daemon without
   long delays caused by an update run still in progress. */
static void
sigterm_handler(int sig) {
  (void)sig;    /* remove compiler warnings... */

  if (terminating) {
    debug_printf("Caught signal %d while already terminating.\n", sig);
    return;
  }
  terminating = 1; /* ignore any further callbacks and break loops */
  /* Flag that we should stop and return... */
  g_main_loop_quit(gmainloop);
  g_main_context_wakeup(NULL);
  debug_printf("Caught signal %d, shutting down ...\n", sig);
}


/* Main function, to read the command line, initiate logging,
   listening to D-Bus and DNS-SD events, and cleaning up when
   receiving a termination signal. */
int main(int argc, char *argv[]) {
  int ret = 1;
  int i;
  char *val;
  char *p;
  GDBusProxy *proxy = NULL;
  GError *error = NULL;
  int subscription_id = 0;

  /* Read command line options */
  if (argc >= 2) {
    for (i = 1; i < argc; i++)
      if (!strcasecmp(argv[i], "--debug") || !strcasecmp(argv[i], "-d") ||
	  !strncasecmp(argv[i], "-v", 2)) {
	/* Turn on debug output mode if requested */
	debug_stderr = 1;
	debug_printf("Reading command line option %s, turning on debug mode (Log on standard error).\n",
		     argv[i]);
      } else if (!strcasecmp(argv[i], "--logfile") ||
		 !strcasecmp(argv[i], "-l")) {
	/* Turn on debug log file mode if requested */
	if (debug_logfile == 0) {
	  debug_logfile = 1;
	  start_debug_logging();
	  debug_printf("Reading command line option %s, turning on debug mode (Log into log file %s).\n",
		       argv[i], debug_log_file);
	}
      } else if (!strncasecmp(argv[i], "--logdir", 8)) {
	debug_printf("Reading command line: %s\n", argv[i]);
	if (argv[i][8] == '=' && argv[i][9])
	  val = argv[i] + 9;
	else if (!argv[i][8] && i < argc - 1) {
	  i++;
	  debug_printf("Reading command line: %s\n", argv[i]);
	  val = argv[i];
	} else {
	  fprintf(stderr, "Expected log directory after \"--logdir\" option.\n\n");
	  goto help;
	}
	strncpy(logdir, val, sizeof(logdir) - 1);
	debug_printf("Set log directory to %s.\n", logdir);
      } else if (!strncasecmp(argv[i], "--update-delay", 14)) {
	debug_printf("Reading command line: %s\n", argv[i]);
	if (argv[i][14] == '=' && argv[i][15])
	  val = argv[i] + 15;
	else if (!argv[i][14] && i < argc - 1) {
	  i++;
	  debug_printf("Reading command line: %s\n", argv[i]);
	  val = argv[i];
	} else {
	  fprintf(stderr, "Expected update delay setting after \"--update-delay\" option.\n\n");
	  goto help;
	}
	int t = atoi(val);
	if (t >= 0) {
	  update_delay = t;
	  debug_printf("Set update delay to %d msec.\n",
		       t);
	} else {
	  fprintf(stderr, "Invalid update delay value: %d\n\n",
		  t);
	  goto help;
	}
      } else if (!strncasecmp(argv[i], "--update-interval", 17)) {
	debug_printf("Reading command line: %s\n", argv[i]);
	if (argv[i][17] == '=' && argv[i][18])
	  val = argv[i] + 18;
	else if (!argv[i][17] && i < argc - 1) {
	  i++;
	  debug_printf("Reading command line: %s\n", argv[i]);
	  val = argv[i];
	} else {
	  fprintf(stderr, "Expected update interval setting after \"--update-interval\" option.\n\n");
	  goto help;
	}
	int t = atoi(val);
	if (t >= 0) {
	  update_interval = t;
	  debug_printf("Set update interval to %d msec.\n",
		       t);
	} else {
	  fprintf(stderr, "Invalid update interval value: %d\n\n",
		  t);
	  goto help;
	}
      } else if (!strcasecmp(argv[i], "--version") ||
		 !strcasecmp(argv[i], "--help") || !strcasecmp(argv[i], "-h")) {
	/* Help!! */
	goto help;
      } else if (argv[i][0] == '-') {
	/* Unknown option */
	fprintf(stderr,
		"Reading command line option %s, unknown command line option.\n\n",
		argv[i]);
        goto help;
      } else {
	if (proxy_cups_server == NULL) {
	  debug_printf("Reading command line: %s -> Proxy cupsd hostname:port or socket\n", argv[i]);
	  proxy_cups_server = strdup(argv[i]);
	} else if (system_cups_server == NULL) {
	  debug_printf("Reading command line: %s -> System cupsd hostname:port or socket\n", argv[i]);
	  system_cups_server = strdup(argv[i]);
	} else {
	  /* Extra argument */
	  fprintf(stderr,
		  "Reading command line option %s, too many arguments.\n\n",
		  argv[i]);
	  goto help;
	}
      }
  }
  if (system_cups_server == NULL) {
    /* Less than 2 CUPS daemons specified */
    if (proxy_cups_server == NULL) {
      debug_printf("Both a proxy cupsd and a system cupsd need to be specified (or at least a system cupsd for a dry run).\n\n");
      goto help;
    } else {
      system_cups_server = proxy_cups_server;
      proxy_cups_server = NULL;
    }
  }

  /* Set the paths of the log files */
  if (logdir[0] == '\0')
    strncpy(logdir, DEFAULT_LOGDIR, sizeof(logdir) - 1);

  strncpy(debug_log_file, logdir,
	  sizeof(debug_log_file) - 1);
  strncpy(debug_log_file + strlen(logdir),
	  DEBUG_LOG_FILE,
	  sizeof(debug_log_file) - strlen(logdir) - 1);

  strncpy(debug_log_file_bckp, logdir,
	  sizeof(debug_log_file_bckp) - 1);
  strncpy(debug_log_file_bckp + strlen(logdir),
	  DEBUG_LOG_FILE_2,
	  sizeof(debug_log_file_bckp) - strlen(logdir) - 1);
  
  if (debug_logfile == 1)
    start_debug_logging();

  debug_printf("main() in THREAD %ld\n", pthread_self());

  debug_printf("System CUPS: %s\n", system_cups_server);
  debug_printf("Proxy CUPS: %s\n", proxy_cups_server);

  debug_printf("cups-proxyd version " VERSION " starting.\n");

  /* Wait for both CUPS daemons to start */
  debug_printf("Check whether both CUPS daemons are running.\n");
  if (proxy_cups_server)
    while (http_connect_proxy() == NULL)
      sleep(1);
  while (http_connect_system() == NULL)
    sleep(1);
  if (proxy_cups_server)
    http_close_proxy();
  http_close_system();

  /* Create list of print queues on proxy CUPS daemon */
  if (proxy_cups_server)
    proxy_printers = get_proxy_printers();

  /* Redirect SIGINT and SIGTERM so that we do a proper shutdown */
#ifdef HAVE_SIGSET /* Use System V signals over POSIX to avoid bugs */
  sigset(SIGTERM, sigterm_handler);
  sigset(SIGINT, sigterm_handler);
  debug_printf("Using signal handler SIGSET\n");
#elif defined(HAVE_SIGACTION)
  struct sigaction action; /* Actions for POSIX signals */
  memset(&action, 0, sizeof(action));
  sigemptyset(&action.sa_mask);
  sigaddset(&action.sa_mask, SIGTERM);
  action.sa_handler = sigterm_handler;
  sigaction(SIGTERM, &action, NULL);
  sigemptyset(&action.sa_mask);
  sigaddset(&action.sa_mask, SIGINT);
  action.sa_handler = sigterm_handler;
  sigaction(SIGINT, &action, NULL);
  debug_printf("Using signal handler SIGACTION\n");
#else
  signal(SIGTERM, sigterm_handler);
  signal(SIGINT, sigterm_handler);
  debug_printf("Using signal handler SIGNAL\n");
#endif /* HAVE_SIGSET */

  /* Initialize last_update */ 
  memset(&last_update, 0, sizeof(last_update));

  /* Start Avahi browsers to trigger updates when network printers appear or
     disappear (which create potential temporary queues on the system's CUPS */ 
  avahi_init();

  /* Override the default password callback so we don't end up
   * prompting for it. */
  cupsSetPasswordCB2 (password_callback, NULL);

  /* Create the main loop */
  gmainloop = g_main_loop_new (NULL, FALSE);

  /* Subscribe to the system's CUPS' D-Bus notifications and create a proxy
     to receive the notifications */
  subscription_id = create_subscription ();
  g_timeout_add_seconds (NOTIFY_LEASE_DURATION - 60,
			 renew_subscription_timeout,
			 &subscription_id);
  cups_notifier = cups_notifier_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
							0,
							NULL,
							CUPS_DBUS_PATH,
							NULL,
							&error);
  if (error) {
    fprintf (stderr, "Error creating cups notify handler: %s", error->message);
    g_error_free (error);
    cups_notifier = NULL;
  }
  if (cups_notifier != NULL) {
    g_signal_connect (cups_notifier, "printer-state-changed",
		      G_CALLBACK (on_printer_state_changed), NULL);
    g_signal_connect (cups_notifier, "printer-deleted",
		      G_CALLBACK (on_printer_deleted), NULL);
    g_signal_connect (cups_notifier, "printer-modified",
		      G_CALLBACK (on_printer_modified), NULL);
  }

  /* Schedule first update to sync with current state */
  schedule_proxy_update();

  /* Run the main loop */
  g_main_loop_run (gmainloop);

  /* Main loop exited */
  debug_printf("Main loop exited\n");
  g_main_loop_unref (gmainloop);
  gmainloop = NULL;
  ret = 0;

fail:

  /* Clean up things */

  /* Remove all queue list entries */
  for (p = (char *)cupsArrayFirst(proxy_printers);
       p; p = (char *)cupsArrayNext(proxy_printers))
    free(p);
  cupsArrayDelete(proxy_printers);

  /* Cancel subscription to CUPS notifications */
  cancel_subscription (subscription_id);
  if (cups_notifier)
    g_object_unref (cups_notifier);

  /* Stop Avahi browsers */
  avahi_shutdown();

  /* Disconnect from CUPS daemons */
  if (proxy_cups_server)
    http_close_proxy();
  http_close_system();

  if (proxy_cups_server)
    free(proxy_cups_server);
  free(system_cups_server);
  
  /* Close log file if we have one */
  if (debug_logfile == 1)
    stop_debug_logging();
  
  return ret;

 help:

  fprintf(stderr,
	  "cups-proxyd version " VERSION "\n\n"
	  "Usage: cups-proxyd [<proxy_cupsd>] <system_cupsd> [options]\n"
	  "\n"
	  "<proxy_cupsd>:            The CUPS daemon being the proxy, which receives\n"
	  "                          the print jobs of the clients. If left out, we get\n"
	  "                          into dry-run mode. All appearing and disappearing\n"
	  "                          printers for the system's CUPS get logged.\n"
	  "<system_cupsd>:           The system's CUPS daemon, which is protected by\n"
	  "                          the proxy.\n"
	  "\n"
	  "Both proxy and system cupsd have to be specified either by their socket file\n"
	  "or by <hostname>:<port>\n"
	  "\n"
	  "Options:\n"
	  "  -d\n"
	  "  -v\n"
	  "  --debug                  Run in debug mode (logging to stderr).\n"
	  "  -l\n"
	  "  --logfile                Run in debug mode (logging into file).\n"
	  "  --logdir=<dir>           Directory to put the log files in. Only used\n"
	  "                           together with -l or --logfile\n"  
	  "  -h\n"
	  "  --help\n"
	  "  --version                Show this usage message.\n"
	  "  --update-delay=<time>    Update the print queues of the proxy cupsd to the\n"
	  "                           ones of the system's cupsd not before <time> msec\n"
	  "                           after the first DNS-SD or CUPS notification event,\n"
	  "                           to avoid a flooding of updates if a chenge on the\n"
	  "                           system's CUPS generates various events.\n"
	  "  --update-interval=<time> Update the print queues of the proxy cupsd to the\n"
	  "                           ones of the system's cupsd not more often than\n"
	  "                           every <time> msec.\n"
	  );

  return 1;
}
