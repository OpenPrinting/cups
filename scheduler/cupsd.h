/*
 * Main header file for the CUPS scheduler.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2007-2018 by Apple Inc.
 * Copyright © 1997-2007 by Easy Software Products, all rights reserved.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */


/*
 * Include necessary headers.
 */

#include <cups/cups-private.h>
#include <cups/file-private.h>
#include <cups/ppd-private.h>

#include <limits.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <math.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>

#ifdef _WIN32
#  include <direct.h>
#else
#  include <unistd.h>
#endif /* _WIN32 */

#include "mime.h"

#if defined(HAVE_CDSASSL)
#  include <CoreFoundation/CoreFoundation.h>
#endif /* HAVE_CDSASSL */


/*
 * Some OS's don't have hstrerror(), most notably Solaris...
 */

#ifndef HAVE_HSTRERROR
#  ifdef hstrerror
#    undef hstrerror
#  endif /* hstrerror */
#  define hstrerror cups_hstrerror

extern const char *cups_hstrerror(int);
#endif /* !HAVE_HSTRERROR */


/*
 * Common constants.
 */

#ifndef FALSE
#  define FALSE		0
#  define TRUE		(!FALSE)
#endif /* !FALSE */


/*
 * Implementation limits...
 */

#define MAX_ENV			100	/* Maximum number of environment strings */
#define MAX_USERPASS		33	/* Maximum size of username/password */
#define MAX_FILTERS		20	/* Maximum number of filters */
#define MAX_SYSTEM_GROUPS	32	/* Maximum number of system groups */


/*
 * Defaults...
 */

#define DEFAULT_HISTORY		INT_MAX	/* Preserve job history? */
#define DEFAULT_FILES		86400	/* Preserve job files? */
#define DEFAULT_TIMEOUT		300	/* Timeout during requests/updates */
#define DEFAULT_KEEPALIVE	30	/* Timeout between requests */


/*
 * Global variable macros...
 */

#ifdef _MAIN_C_
#  define VAR
#  define VALUE(x) =x
#  define VALUE2(x,y) ={x,y}
#else
#  define VAR      extern
#  define VALUE(x)
#  define VALUE2(x,y)
#endif /* _MAIN_C */


/*
 * Base types...
 */

typedef struct cupsd_client_s cupsd_client_t;
typedef struct cupsd_job_s cupsd_job_t;
typedef struct cupsd_printer_s cupsd_printer_t;


/*
 * Other stuff for the scheduler...
 */

#include "sysman.h"
#include "statbuf.h"
#include "cert.h"
#include "auth.h"
#include "client.h"
#include "policy.h"
#include "printers.h"
#include "classes.h"
#include "job.h"
#include "colorman.h"
#include "conf.h"
#include "banners.h"
#include "dirsvc.h"
#include "network.h"
#include "subscriptions.h"


/*
 * Reload types...
 */

#define RELOAD_NONE	0		/* No reload needed */
#define RELOAD_ALL	1		/* Reload everything */
#define RELOAD_CUPSD	2		/* Reload only cupsd.conf */


/*
 * Select callback function type...
 */

typedef void (*cupsd_selfunc_t)(void *data);


/*
 * Globals...
 */

VAR int			TestConfigFile	VALUE(0);
					/* Test the cupsd.conf file? */
VAR int			MaxFDs		VALUE(0);
					/* Maximum number of files */

VAR time_t		ReloadTime	VALUE(0);
					/* Time of reload request... */
VAR int			NeedReload	VALUE(RELOAD_ALL),
					/* Need to load configuration? */
			DoingShutdown	VALUE(0);
					/* Shutting down the scheduler? */
VAR void		*DefaultProfile	VALUE(0);
					/* Default security profile */

#ifdef HAVE_ONDEMAND
VAR int			OnDemand	VALUE(0);
					/* Launched on demand */
#endif /* HAVE_ONDEMAND */


/*
 * Prototypes...
 */

/* env.c */
extern void		cupsdInitEnv(void);
extern int		cupsdLoadEnv(char *envp[], int envmax);
extern void		cupsdSetEnv(const char *name, const char *value);
extern void		cupsdSetEnvf(const char *name, const char *value, ...) _CUPS_FORMAT(2, 3);
extern void		cupsdUpdateEnv(void);

/* file.c */
extern void		cupsdCleanFiles(const char *path, const char *pattern);
extern int		cupsdCloseCreatedConfFile(cups_file_t *fp,
			                          const char *filename);
extern void		cupsdClosePipe(int *fds);
extern cups_file_t	*cupsdCreateConfFile(const char *filename, mode_t mode);
extern cups_file_t	*cupsdOpenConfFile(const char *filename);
extern int		cupsdOpenPipe(int *fds);
extern int		cupsdRemoveFile(const char *filename);
extern int		cupsdUnlinkOrRemoveFile(const char *filename);

/* main.c */
extern int		cupsdAddString(cups_array_t **a, const char *s);
extern void		cupsdCheckProcess(void);
extern void		cupsdClearString(char **s);
extern void		cupsdFreeStrings(cups_array_t **a);
extern void		cupsdHoldSignals(void);
extern char		*cupsdMakeUUID(const char *name, int number,
				       char *buffer, size_t bufsize);
extern void		cupsdReleaseSignals(void);
extern void		cupsdSetString(char **s, const char *v);
extern void		cupsdSetStringf(char **s, const char *f, ...)
			__attribute__ ((__format__ (__printf__, 2, 3)));

/* process.c */
extern void		*cupsdCreateProfile(int job_id, int allow_networking);
extern void		cupsdDestroyProfile(void *profile);
extern int		cupsdEndProcess(int pid, int force);
extern const char	*cupsdFinishProcess(int pid, char *name, size_t namelen, int *job_id);
extern int		cupsdStartProcess(const char *command, char *argv[],
					  char *envp[], int infd, int outfd,
					  int errfd, int backfd, int sidefd,
					  int root, void *profile,
					  cupsd_job_t *job, int *pid);

/* select.c */
extern int		cupsdAddSelect(int fd, cupsd_selfunc_t read_cb,
			               cupsd_selfunc_t write_cb, void *data);
extern int		cupsdDoSelect(long timeout);
#ifdef CUPSD_IS_SELECTING
extern int		cupsdIsSelecting(int fd);
#endif /* CUPSD_IS_SELECTING */
extern void		cupsdRemoveSelect(int fd);
extern void		cupsdStartSelect(void);
extern void		cupsdStopSelect(void);

/* server.c */
extern void		cupsdStartServer(void);
extern void		cupsdStopServer(void);
