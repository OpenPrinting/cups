/*
 * Debugging functions for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2008-2018 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "cups-private.h"
#ifdef _WIN32
#  include <sys/timeb.h>
#  include <time.h>
#  include <io.h>
#  define getpid (int)GetCurrentProcessId
int					/* O  - 0 on success, -1 on failure */
_cups_gettimeofday(struct timeval *tv,	/* I  - Timeval struct */
                   void		  *tz)	/* I  - Timezone */
{
  struct _timeb timebuffer;		/* Time buffer struct */
  _ftime(&timebuffer);
  tv->tv_sec  = (long)timebuffer.time;
  tv->tv_usec = timebuffer.millitm * 1000;
  return 0;
}
#else
#  include <sys/time.h>
#  include <unistd.h>
#endif /* _WIN32 */
#include <regex.h>
#include <fcntl.h>


#ifdef DEBUG
/*
 * Globals...
 */

int			_cups_debug_fd = -1;
					/* Debug log file descriptor */
int			_cups_debug_level = 1;
					/* Log level (0 to 9) */


/*
 * Local globals...
 */

static regex_t		*debug_filter = NULL;
					/* Filter expression for messages */
static int		debug_init = 0;	/* Did we initialize debugging? */
static cups_mutex_t	debug_init_mutex = CUPS_MUTEX_INITIALIZER,
					/* Mutex to control initialization */
			debug_log_mutex = CUPS_MUTEX_INITIALIZER;
					/* Mutex to serialize log entries */


/*
 * 'debug_thread_id()' - Return an integer representing the current thread.
 */

static int				/* O - Local thread ID */
debug_thread_id(void)
{
  _cups_globals_t *cg = _cupsGlobals();	/* Global data */


  return (cg->thread_id);
}


/*
 * '_cups_debug_printf()' - Write a formatted line to the log.
 */

void
_cups_debug_printf(const char *format,	/* I - Printf-style format string */
                   ...)			/* I - Additional arguments as needed */
{
  int			result = 0;	/* Filter result */
  va_list		ap;		/* Pointer to arguments */
  struct timeval	curtime;	/* Current time */
  char			buffer[2048];	/* Output buffer */
  ssize_t		bytes;		/* Number of bytes in buffer */
  int			level;		/* Log level in message */


 /*
  * See if we need to do any logging...
  */

  if (!debug_init)
    _cups_debug_set(getenv("CUPS_DEBUG_LOG"), getenv("CUPS_DEBUG_LEVEL"), getenv("CUPS_DEBUG_FILTER"), 0);

  if (_cups_debug_fd < 0)
    return;

 /*
  * Filter as needed...
  */

  if (isdigit(format[0]))
    level = *format++ - '0';
  else
    level = 0;

  if (level > _cups_debug_level)
    return;

  cupsMutexLock(&debug_init_mutex);
  if (debug_filter)
    result = regexec(debug_filter, format, 0, NULL, 0);
  cupsMutexUnlock(&debug_init_mutex);

  if (result)
    return;

 /*
  * Format the message...
  */

  gettimeofday(&curtime, NULL);
  snprintf(buffer, sizeof(buffer), "T%03d %02d:%02d:%02d.%03d  ",
           debug_thread_id(), (int)((curtime.tv_sec / 3600) % 24),
	   (int)((curtime.tv_sec / 60) % 60),
	   (int)(curtime.tv_sec % 60), (int)(curtime.tv_usec / 1000));

  va_start(ap, format);
  bytes = cupsFormatStringv(buffer + 19, sizeof(buffer) - 20, format, ap) + 19;
  va_end(ap);

  if ((size_t)bytes >= (sizeof(buffer) - 1))
  {
    buffer[sizeof(buffer) - 2] = '\n';
    bytes = sizeof(buffer) - 1;
  }
  else if (buffer[bytes - 1] != '\n')
  {
    buffer[bytes++] = '\n';
    buffer[bytes]   = '\0';
  }

 /*
  * Write it out...
  */

  cupsMutexLock(&debug_log_mutex);
  write(_cups_debug_fd, buffer, (size_t)bytes);
  cupsMutexUnlock(&debug_log_mutex);
}


/*
 * '_cups_debug_puts()' - Write a single line to the log.
 */

void
_cups_debug_puts(const char *s)		/* I - String to output */
{
  int			result = 0;	/* Filter result */
  struct timeval	curtime;	/* Current time */
  char			buffer[2048];	/* Output buffer */
  ssize_t		bytes;		/* Number of bytes in buffer */
  int			level;		/* Log level in message */


 /*
  * See if we need to do any logging...
  */

  if (!debug_init)
    _cups_debug_set(getenv("CUPS_DEBUG_LOG"), getenv("CUPS_DEBUG_LEVEL"), getenv("CUPS_DEBUG_FILTER"), 0);

  if (_cups_debug_fd < 0)
    return;

 /*
  * Filter as needed...
  */

  if (isdigit(s[0]))
    level = *s++ - '0';
  else
    level = 0;

  if (level > _cups_debug_level)
    return;

  cupsMutexLock(&debug_init_mutex);
  if (debug_filter)
    result = regexec(debug_filter, s, 0, NULL, 0);
  cupsMutexUnlock(&debug_init_mutex);

  if (result)
    return;

 /*
  * Format the message...
  */

  gettimeofday(&curtime, NULL);
  bytes = snprintf(buffer, sizeof(buffer), "T%03d %02d:%02d:%02d.%03d  %s",
                   debug_thread_id(), (int)((curtime.tv_sec / 3600) % 24),
		   (int)((curtime.tv_sec / 60) % 60),
		   (int)(curtime.tv_sec % 60), (int)(curtime.tv_usec / 1000),
		   s);

  if ((size_t)bytes >= (sizeof(buffer) - 1))
  {
    buffer[sizeof(buffer) - 2] = '\n';
    bytes = sizeof(buffer) - 1;
  }
  else if (buffer[bytes - 1] != '\n')
  {
    buffer[bytes++] = '\n';
    buffer[bytes]   = '\0';
  }

 /*
  * Write it out...
  */

  cupsMutexLock(&debug_log_mutex);
  write(_cups_debug_fd, buffer, (size_t)bytes);
  cupsMutexUnlock(&debug_log_mutex);
}


/*
 * '_cups_debug_set()' - Enable or disable debug logging.
 */

void
_cups_debug_set(const char *logfile,	/* I - Log file or NULL */
                const char *level,	/* I - Log level or NULL */
		const char *filter,	/* I - Filter string or NULL */
		int        force)	/* I - Force initialization */
{
  cupsMutexLock(&debug_init_mutex);

  if (!debug_init || force)
  {
   /*
    * Restore debug settings to defaults...
    */

    if (_cups_debug_fd != -1)
    {
      close(_cups_debug_fd);
      _cups_debug_fd = -1;
    }

    if (debug_filter)
    {
      regfree((regex_t *)debug_filter);
      debug_filter = NULL;
    }

    _cups_debug_level = 1;

   /*
    * Open logs, set log levels, etc.
    */

    if (!logfile)
      _cups_debug_fd = -1;
    else if (!strcmp(logfile, "-"))
      _cups_debug_fd = 2;
    else
    {
      char	buffer[1024];		/* Filename buffer */

      snprintf(buffer, sizeof(buffer), logfile, getpid());

      if (buffer[0] == '+')
	_cups_debug_fd = open(buffer + 1, O_WRONLY | O_APPEND | O_CREAT, 0644);
      else
	_cups_debug_fd = open(buffer, O_WRONLY | O_TRUNC | O_CREAT, 0644);
    }

    if (level)
      _cups_debug_level = atoi(level);

    if (filter)
    {
      if ((debug_filter = (regex_t *)calloc(1, sizeof(regex_t))) == NULL)
	fputs("Unable to allocate memory for CUPS_DEBUG_FILTER - results not "
	      "filtered!\n", stderr);
      else if (regcomp(debug_filter, filter, REG_EXTENDED))
      {
	fputs("Bad regular expression in CUPS_DEBUG_FILTER - results not "
	      "filtered!\n", stderr);
	free(debug_filter);
	debug_filter = NULL;
      }
    }

    debug_init = 1;
  }

  cupsMutexUnlock(&debug_init_mutex);
}


#else
/*
 * '_cups_debug_set()' - Enable or disable debug logging.
 */

void
_cups_debug_set(const char *logfile,	/* I - Log file or NULL */
		const char *level,	/* I - Log level or NULL */
		const char *filter,	/* I - Filter string or NULL */
		int        force)	/* I - Force initialization */
{
  (void)logfile;
  (void)level;
  (void)filter;
  (void)force;
}
#endif /* DEBUG */
