/*
 * Device scanning mini-daemon for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2007-2018 by Apple Inc.
 * Copyright © 1997-2006 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "util.h"
#include <cups/array.h>
#include <cups/dir.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <poll.h>


/*
 * Constants...
 */

#define MAX_BACKENDS	200		/* Maximum number of backends we'll run */


/*
 * Backend information...
 */

typedef struct
{
  char		*name;			/* Name of backend */
  int		pid,			/* Process ID */
		status;			/* Exit status */
  cups_file_t	*pipe;			/* Pipe from backend stdout */
  int		count;			/* Number of devices found */
} cupsd_backend_t;


/*
 * Device information structure...
 */

typedef struct
{
  char	device_class[128],		/* Device class */
	device_info[128],		/* Device info/description */
	device_uri[1024];		/* Device URI */
} cupsd_device_t;


/*
 * Local globals...
 */

static int		num_backends = 0,
					/* Total backends */
			active_backends = 0;
					/* Active backends */
static cupsd_backend_t	backends[MAX_BACKENDS];
					/* Array of backends */
static struct pollfd	backend_fds[MAX_BACKENDS];
					/* Array for poll() */
static cups_array_t	*devices;	/* Array of devices */
static uid_t		normal_user;	/* Normal user ID */
static int		device_limit;	/* Maximum number of devices */
static int		send_class,	/* Send device-class attribute? */
			send_info,	/* Send device-info attribute? */
			send_make_and_model,
					/* Send device-make-and-model attribute? */
			send_uri,	/* Send device-uri attribute? */
			send_id,	/* Send device-id attribute? */
			send_location;	/* Send device-location attribute? */
static int		dead_children = 0;
					/* Dead children? */


/*
 * Local functions...
 */

static int		add_device(const char *device_class,
				   const char *device_make_and_model,
				   const char *device_info,
				   const char *device_uri,
				   const char *device_id,
				   const char *device_location);
static int		compare_devices(cupsd_device_t *p0,
			                cupsd_device_t *p1);
static double		get_current_time(void);
static int		get_device(cupsd_backend_t *backend);
static void		process_children(void);
static void		sigchld_handler(int sig);
static int		start_backend(const char *backend, int root);


/*
 * 'main()' - Scan for devices and return an IPP response.
 *
 * Usage:
 *
 *    cups-deviced request_id limit options
 */

int					/* O - Exit code */
main(int  argc,				/* I - Number of command-line args */
     char *argv[])			/* I - Command-line arguments */
{
  int		i;			/* Looping var */
  int		request_id;		/* Request ID */
  int		timeout;		/* Timeout in seconds */
  const char	*server_bin;		/* CUPS_SERVERBIN environment variable */
  char		filename[1024];		/* Backend directory filename */
  cups_dir_t	*dir;			/* Directory pointer */
  cups_dentry_t *dent;			/* Directory entry */
  double	current_time,		/* Current time */
		end_time;		/* Ending time */
  int		num_options;		/* Number of options */
  cups_option_t	*options;		/* Options */
  cups_array_t	*requested,		/* requested-attributes values */
		*exclude,		/* exclude-schemes values */
		*include;		/* include-schemes values */
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
  struct sigaction action;		/* Actions for POSIX signals */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */


  setbuf(stderr, NULL);

 /*
  * Check the command-line...
  */

  if (argc != 6)
  {
    fputs("Usage: cups-deviced request-id limit timeout user-id options\n", stderr);

    return (1);
  }

  request_id = atoi(argv[1]);
  if (request_id < 1)
  {
    fprintf(stderr, "ERROR: [cups-deviced] Bad request ID %d!\n", request_id);

    return (1);
  }

  device_limit = atoi(argv[2]);
  if (device_limit < 0)
  {
    fprintf(stderr, "ERROR: [cups-deviced] Bad limit %d!\n", device_limit);

    return (1);
  }

  timeout = atoi(argv[3]);
  if (timeout < 1)
  {
    fprintf(stderr, "ERROR: [cups-deviced] Bad timeout %d!\n", timeout);

    return (1);
  }

  normal_user = (uid_t)strtoul(argv[4], NULL, 10);
  if (normal_user == 0)
  {
    fprintf(stderr, "ERROR: [cups-deviced] Bad user %u!\n", (unsigned)normal_user);

    return (1);
  }

  num_options = cupsParseOptions(argv[5], 0, &options);
  requested   = cupsdCreateStringsArray(cupsGetOption("requested-attributes",
                                                      num_options, options));
  exclude     = cupsdCreateStringsArray(cupsGetOption("exclude-schemes",
                                                      num_options, options));
  include     = cupsdCreateStringsArray(cupsGetOption("include-schemes",
                                                      num_options, options));

  if (!requested || cupsArrayFind(requested, "all") != NULL)
  {
    send_class = send_info = send_make_and_model = send_uri = send_id =
        send_location = 1;
  }
  else
  {
    send_class          = cupsArrayFind(requested, "device-class") != NULL;
    send_info           = cupsArrayFind(requested, "device-info") != NULL;
    send_make_and_model = cupsArrayFind(requested, "device-make-and-model") != NULL;
    send_uri            = cupsArrayFind(requested, "device-uri") != NULL;
    send_id             = cupsArrayFind(requested, "device-id") != NULL;
    send_location       = cupsArrayFind(requested, "device-location") != NULL;
  }

 /*
  * Listen to child signals...
  */

#ifdef HAVE_SIGSET /* Use System V signals over POSIX to avoid bugs */
  sigset(SIGCHLD, sigchld_handler);
#elif defined(HAVE_SIGACTION)
  memset(&action, 0, sizeof(action));

  sigemptyset(&action.sa_mask);
  sigaddset(&action.sa_mask, SIGCHLD);
  action.sa_handler = sigchld_handler;
  sigaction(SIGCHLD, &action, NULL);
#else
  signal(SIGCLD, sigchld_handler);	/* No, SIGCLD isn't a typo... */
#endif /* HAVE_SIGSET */

 /*
  * Try opening the backend directory...
  */

  if ((server_bin = getenv("CUPS_SERVERBIN")) == NULL)
    server_bin = CUPS_SERVERBIN;

  snprintf(filename, sizeof(filename), "%s/backend", server_bin);

  if ((dir = cupsDirOpen(filename)) == NULL)
  {
    fprintf(stderr, "ERROR: [cups-deviced] Unable to open backend directory "
                    "\"%s\": %s", filename, strerror(errno));

    return (1);
  }

 /*
  * Setup the devices array...
  */

  devices = cupsArrayNew((cups_array_func_t)compare_devices, NULL);

 /*
  * Loop through all of the device backends...
  */

  while ((dent = cupsDirRead(dir)) != NULL)
  {
   /*
    * Skip entries that are not executable files...
    */

    if (!S_ISREG(dent->fileinfo.st_mode) ||
        !isalnum(dent->filename[0] & 255) ||
        (dent->fileinfo.st_mode & (S_IRUSR | S_IXUSR)) != (S_IRUSR | S_IXUSR))
      continue;

   /*
    * Skip excluded or not included backends...
    */

    if (cupsArrayFind(exclude, dent->filename) ||
        (include && !cupsArrayFind(include, dent->filename)))
      continue;

   /*
    * Backends without permissions for normal users run as root,
    * all others run as the unprivileged user...
    */

    start_backend(dent->filename, !(dent->fileinfo.st_mode & (S_IWGRP | S_IWOTH | S_IXOTH)));
  }

  cupsDirClose(dir);

 /*
  * Collect devices...
  */

  if (getenv("SOFTWARE"))
    puts("Content-Type: application/ipp\n");

  cupsdSendIPPHeader(IPP_OK, request_id);
  cupsdSendIPPGroup(IPP_TAG_OPERATION);
  cupsdSendIPPString(IPP_TAG_CHARSET, "attributes-charset", "utf-8");
  cupsdSendIPPString(IPP_TAG_LANGUAGE, "attributes-natural-language", "en-US");

  end_time = get_current_time() + timeout;

  while (active_backends > 0 && (current_time = get_current_time()) < end_time)
  {
   /*
    * Collect the output from the backends...
    */

    timeout = (int)(1000 * (end_time - current_time));

    if (poll(backend_fds, (nfds_t)num_backends, timeout) > 0)
    {
      for (i = 0; i < num_backends; i ++)
        if (backend_fds[i].revents && backends[i].pipe)
	{
	  cups_file_t *bpipe = backends[i].pipe;
					/* Copy of pipe for backend... */

	  do
	  {
	    if (get_device(backends + i))
	    {
	      backend_fds[i].fd     = 0;
	      backend_fds[i].events = 0;
	      break;
	    }
	  }
	  while (_cupsFilePeekAhead(bpipe, '\n'));
        }
    }

   /*
    * Get exit status from children...
    */

    if (dead_children)
      process_children();
  }

  cupsdSendIPPTrailer();

 /*
  * Terminate any remaining backends and exit...
  */

  if (active_backends > 0)
  {
    for (i = 0; i < num_backends; i ++)
      if (backends[i].pid)
	kill(backends[i].pid, SIGTERM);
  }

  return (0);
}


/*
 * 'add_device()' - Add a new device to the list.
 */

static int				/* O - 0 on success, -1 on error */
add_device(
    const char *device_class,		/* I - Device class */
    const char *device_make_and_model,	/* I - Device make and model */
    const char *device_info,		/* I - Device information */
    const char *device_uri,		/* I - Device URI */
    const char *device_id,		/* I - 1284 device ID */
    const char *device_location)	/* I - Physical location */
{
  cupsd_device_t	*device;	/* New device */


 /*
  * Allocate memory for the device record...
  */

  if ((device = calloc(1, sizeof(cupsd_device_t))) == NULL)
  {
    fputs("ERROR: [cups-deviced] Ran out of memory allocating a device!\n",
          stderr);
    return (-1);
  }

 /*
  * Copy the strings over...
  */

  strlcpy(device->device_class, device_class, sizeof(device->device_class));
  strlcpy(device->device_info, device_info, sizeof(device->device_info));
  strlcpy(device->device_uri, device_uri, sizeof(device->device_uri));

 /*
  * Add the device to the array and return...
  */

  if (cupsArrayFind(devices, device))
  {
   /*
    * Avoid duplicates!
    */

    free(device);
  }
  else
  {
    cupsArrayAdd(devices, device);

    if (device_limit <= 0 || cupsArrayCount(devices) < device_limit)
    {
     /*
      * Send device info...
      */

      cupsdSendIPPGroup(IPP_TAG_PRINTER);
      if (send_class)
	cupsdSendIPPString(IPP_TAG_KEYWORD, "device-class",
	                   device_class);
      if (send_info)
	cupsdSendIPPString(IPP_TAG_TEXT, "device-info", device_info);
      if (send_make_and_model)
	cupsdSendIPPString(IPP_TAG_TEXT, "device-make-and-model",
			   device_make_and_model);
      if (send_uri)
	cupsdSendIPPString(IPP_TAG_URI, "device-uri", device_uri);
      if (send_id)
	cupsdSendIPPString(IPP_TAG_TEXT, "device-id",
	                   device_id ? device_id : "");
      if (send_location)
	cupsdSendIPPString(IPP_TAG_TEXT, "device-location",
	                   device_location ? device_location : "");

      fflush(stdout);
      fputs("DEBUG: Flushed attributes...\n", stderr);
    }
  }

  return (0);
}


/*
 * 'compare_devices()' - Compare device names to eliminate duplicates.
 */

static int				/* O - Result of comparison */
compare_devices(cupsd_device_t *d0,	/* I - First device */
                cupsd_device_t *d1)	/* I - Second device */
{
  int		diff;			/* Difference between strings */


 /*
  * Sort devices by device-info, device-class, and device-uri...
  */

  if ((diff = cupsdCompareNames(d0->device_info, d1->device_info)) != 0)
    return (diff);
  else if ((diff = _cups_strcasecmp(d0->device_class, d1->device_class)) != 0)
    return (diff);
  else
    return (_cups_strcasecmp(d0->device_uri, d1->device_uri));
}


/*
 * 'get_current_time()' - Get the current time as a double value in seconds.
 */

static double				/* O - Time in seconds */
get_current_time(void)
{
  struct timeval	curtime;	/* Current time */


  gettimeofday(&curtime, NULL);

  return (curtime.tv_sec + 0.000001 * curtime.tv_usec);
}


/*
 * 'get_device()' - Get a device from a backend.
 */

static int				/* O - 0 on success, -1 on error */
get_device(cupsd_backend_t *backend)	/* I - Backend to read from */
{
  char	line[2048],			/* Line from backend */
	temp[2048],			/* Copy of line */
	*ptr,				/* Pointer into line */
	*dclass,			/* Device class */
	*uri,				/* Device URI */
	*make_model,			/* Make and model */
	*info,				/* Device info */
	*device_id,			/* 1284 device ID */
	*location;			/* Physical location */


  if (cupsFileGets(backend->pipe, line, sizeof(line)))
  {
   /*
    * Each line is of the form:
    *
    *   class URI "make model" "name" ["1284 device ID"] ["location"]
    */

    strlcpy(temp, line, sizeof(temp));

   /*
    * device-class
    */

    dclass = temp;

    for (ptr = temp; *ptr; ptr ++)
      if (isspace(*ptr & 255))
        break;

    while (isspace(*ptr & 255))
      *ptr++ = '\0';

   /*
    * device-uri
    */

    if (!*ptr)
      goto error;

    for (uri = ptr; *ptr; ptr ++)
      if (isspace(*ptr & 255))
        break;

    while (isspace(*ptr & 255))
      *ptr++ = '\0';

   /*
    * device-make-and-model
    */

    if (*ptr != '\"')
      goto error;

    for (ptr ++, make_model = ptr; *ptr && *ptr != '\"'; ptr ++)
    {
      if (*ptr == '\\' && ptr[1])
        _cups_strcpy(ptr, ptr + 1);
    }

    if (*ptr != '\"')
      goto error;

    for (*ptr++ = '\0'; isspace(*ptr & 255); *ptr++ = '\0');

   /*
    * device-info
    */

    if (*ptr != '\"')
      goto error;

    for (ptr ++, info = ptr; *ptr && *ptr != '\"'; ptr ++)
    {
      if (*ptr == '\\' && ptr[1])
        _cups_strcpy(ptr, ptr + 1);
    }

    if (*ptr != '\"')
      goto error;

    for (*ptr++ = '\0'; isspace(*ptr & 255); *ptr++ = '\0');

   /*
    * device-id
    */

    if (*ptr == '\"')
    {
      for (ptr ++, device_id = ptr; *ptr && *ptr != '\"'; ptr ++)
      {
	if (*ptr == '\\' && ptr[1])
	  _cups_strcpy(ptr, ptr + 1);
      }

      if (*ptr != '\"')
	goto error;

      for (*ptr++ = '\0'; isspace(*ptr & 255); *ptr++ = '\0');

     /*
      * device-location
      */

      if (*ptr == '\"')
      {
	for (ptr ++, location = ptr; *ptr && *ptr != '\"'; ptr ++)
	{
	  if (*ptr == '\\' && ptr[1])
	    _cups_strcpy(ptr, ptr + 1);
	}

	if (*ptr != '\"')
	  goto error;

	*ptr = '\0';
      }
      else
        location = NULL;
    }
    else
    {
      device_id = NULL;
      location  = NULL;
    }

   /*
    * Add the device to the array of available devices...
    */

    if (!add_device(dclass, make_model, info, uri, device_id, location))
      fprintf(stderr, "DEBUG: [cups-deviced] Found device \"%s\"...\n", uri);

    return (0);
  }

 /*
  * End of file...
  */

  cupsFileClose(backend->pipe);
  backend->pipe = NULL;

  return (-1);

 /*
  * Bad format; strip trailing newline and write an error message.
  */

  error:

  if (line[strlen(line) - 1] == '\n')
    line[strlen(line) - 1] = '\0';

  fprintf(stderr, "ERROR: [cups-deviced] Bad line from \"%s\": %s\n",
	  backend->name, line);
  return (0);
}


/*
 * 'process_children()' - Process all dead children...
 */

static void
process_children(void)
{
  int			i;		/* Looping var */
  int			status;		/* Exit status of child */
  int			pid;		/* Process ID of child */
  cupsd_backend_t	*backend;	/* Current backend */
  const char		*name;		/* Name of process */


 /*
  * Reset the dead_children flag...
  */

  dead_children = 0;

 /*
  * Collect the exit status of some children...
  */

#ifdef HAVE_WAITPID
  while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
#elif defined(HAVE_WAIT3)
  while ((pid = wait3(&status, WNOHANG, NULL)) > 0)
#else
  if ((pid = wait(&status)) > 0)
#endif /* HAVE_WAITPID */
  {
    if (status == SIGTERM)
      status = 0;

    for (i = num_backends, backend = backends; i > 0; i --, backend ++)
      if (backend->pid == pid)
        break;

    if (i > 0)
    {
      name            = backend->name;
      backend->pid    = 0;
      backend->status = status;

      active_backends --;
    }
    else
      name = "Unknown";

    if (status)
    {
      if (WIFEXITED(status))
	fprintf(stderr,
	        "ERROR: [cups-deviced] PID %d (%s) stopped with status %d!\n",
		pid, name, WEXITSTATUS(status));
      else
	fprintf(stderr,
	        "ERROR: [cups-deviced] PID %d (%s) crashed on signal %d!\n",
		pid, name, WTERMSIG(status));
    }
    else
      fprintf(stderr,
              "DEBUG: [cups-deviced] PID %d (%s) exited with no errors.\n",
	      pid, name);
  }
}


/*
 * 'sigchld_handler()' - Handle 'child' signals from old processes.
 */

static void
sigchld_handler(int sig)		/* I - Signal number */
{
  (void)sig;

 /*
  * Flag that we have dead children...
  */

  dead_children = 1;

 /*
  * Reset the signal handler as needed...
  */

#if !defined(HAVE_SIGSET) && !defined(HAVE_SIGACTION)
  signal(SIGCLD, sigchld_handler);
#endif /* !HAVE_SIGSET && !HAVE_SIGACTION */
}


/*
 * 'start_backend()' - Run a backend to gather the available devices.
 */

static int				/* O - 0 on success, -1 on error */
start_backend(const char *name,		/* I - Backend to run */
              int        root)		/* I - Run as root? */
{
  const char		*server_bin;	/* CUPS_SERVERBIN environment variable */
  char			program[1024];	/* Full path to backend */
  cupsd_backend_t	*backend;	/* Current backend */
  char			*argv[2];	/* Command-line arguments */


  if (num_backends >= MAX_BACKENDS)
  {
    fprintf(stderr, "ERROR: Too many backends (%d)!\n", num_backends);
    return (-1);
  }

  if ((server_bin = getenv("CUPS_SERVERBIN")) == NULL)
    server_bin = CUPS_SERVERBIN;

  snprintf(program, sizeof(program), "%s/backend/%s", server_bin, name);

  if (_cupsFileCheck(program, _CUPS_FILE_CHECK_PROGRAM, !geteuid(),
                     _cupsFileCheckFilter, NULL))
    return (-1);

  backend = backends + num_backends;

  argv[0] = (char *)name;
  argv[1] = NULL;

  if ((backend->pipe = cupsdPipeCommand(&(backend->pid), program, argv,
                                        root ? 0 : normal_user)) == NULL)
  {
    fprintf(stderr, "ERROR: [cups-deviced] Unable to execute \"%s\" - %s\n",
            program, strerror(errno));
    return (-1);
  }

 /*
  * Fill in the rest of the backend information...
  */

  fprintf(stderr, "DEBUG: [cups-deviced] Started backend %s (PID %d)\n",
          program, backend->pid);

  backend_fds[num_backends].fd     = cupsFileNumber(backend->pipe);
  backend_fds[num_backends].events = POLLIN;

  backend->name   = strdup(name);
  backend->status = 0;
  backend->count  = 0;

  active_backends ++;
  num_backends ++;

  return (0);
}
