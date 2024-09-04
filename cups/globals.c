/*
 * Global variable access routines for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2007-2019 by Apple Inc.
 * Copyright © 1997-2007 by Easy Software Products, all rights reserved.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "cups-private.h"
#include "debug-internal.h"
#ifndef _WIN32
#  include <pwd.h>
#endif /* !_WIN32 */


/*
 * Local globals...
 */

#ifdef DEBUG
static int		cups_global_index = 0;
					/* Next thread number */
#endif /* DEBUG */
static cups_thread_key_t cups_globals_key = CUPS_THREADKEY_INITIALIZER;
					/* Thread local storage key */
#ifdef HAVE_PTHREAD_H
static pthread_once_t	cups_globals_key_once = PTHREAD_ONCE_INIT;
					/* One-time initialization object */
#endif /* HAVE_PTHREAD_H */
#if defined(HAVE_PTHREAD_H) || defined(_WIN32)
static cups_mutex_t	cups_global_mutex = CUPS_MUTEX_INITIALIZER;
					/* Global critical section */
#endif /* HAVE_PTHREAD_H || _WIN32 */


/*
 * Local functions...
 */

#ifdef _WIN32
static void		cups_fix_path(char *path);
#endif /* _WIN32 */
static _cups_globals_t	*cups_globals_alloc(void);
#if defined(HAVE_PTHREAD_H) || defined(_WIN32)
static void		cups_globals_free(_cups_globals_t *g);
#endif /* HAVE_PTHREAD_H || _WIN32 */
#ifdef HAVE_PTHREAD_H
static void		cups_globals_init(void);
#endif /* HAVE_PTHREAD_H */


/*
 * '_cupsGlobalLock()' - Lock the global mutex.
 */

void
_cupsGlobalLock(void)
{
  cupsMutexLock(&cups_global_mutex);
}


/*
 * '_cupsGlobals()' - Return a pointer to thread local storage
 */

_cups_globals_t *			/* O - Pointer to global data */
_cupsGlobals(void)
{
  _cups_globals_t *cg;			/* Pointer to global data */


#ifdef HAVE_PTHREAD_H
 /*
  * Initialize the global data exactly once...
  */

  pthread_once(&cups_globals_key_once, cups_globals_init);
#endif /* HAVE_PTHREAD_H */

 /*
  * See if we have allocated the data yet...
  */

  if ((cg = (_cups_globals_t *)cupsThreadGetData(cups_globals_key)) == NULL)
  {
   /*
    * No, allocate memory as set the pointer for the key...
    */

    if ((cg = cups_globals_alloc()) != NULL)
      cupsThreadSetData(cups_globals_key, cg);
  }

 /*
  * Return the pointer to the data...
  */

  return (cg);
}


/*
 * '_cupsGlobalUnlock()' - Unlock the global mutex.
 */

void
_cupsGlobalUnlock(void)
{
  cupsMutexUnlock(&cups_global_mutex);
}


#ifdef _WIN32
/*
 * 'DllMain()' - Main entry for library.
 */

BOOL WINAPI				/* O - Success/failure */
DllMain(HINSTANCE hinst,		/* I - DLL module handle */
        DWORD     reason,		/* I - Reason */
        LPVOID    reserved)		/* I - Unused */
{
  _cups_globals_t *cg;			/* Global data */


  (void)hinst;
  (void)reserved;

  switch (reason)
  {
    case DLL_PROCESS_ATTACH :		/* Called on library initialization */
        cupsMutexInit(&cups_global_mutex);

        if ((cups_globals_key = TlsAlloc()) == TLS_OUT_OF_INDEXES)
          return (FALSE);
        break;

    case DLL_THREAD_DETACH :		/* Called when a thread terminates */
        if ((cg = (_cups_globals_t *)TlsGetValue(cups_globals_key)) != NULL)
          cups_globals_free(cg);
        break;

    case DLL_PROCESS_DETACH :		/* Called when library is unloaded */
        if ((cg = (_cups_globals_t *)TlsGetValue(cups_globals_key)) != NULL)
          cups_globals_free(cg);

        TlsFree(cups_globals_key);
	cupsMutexDestroy(&cups_global_mutex);
        break;

    default:
        break;
  }

  return (TRUE);
}
#endif /* _WIN32 */


/*
 * 'cups_globals_alloc()' - Allocate and initialize global data.
 */

static _cups_globals_t *		/* O - Pointer to global data */
cups_globals_alloc(void)
{
  _cups_globals_t *cg = calloc(1, sizeof(_cups_globals_t));
					/* Pointer to global data */


  if (!cg)
    return (NULL);

 /*
  * Clear the global storage and set the default encryption and password
  * callback values...
  */

  cg->encryption     = (http_encryption_t)-1;
  cg->password_cb    = (cups_password_cb2_t)_cupsGetPassword;
  cg->trust_first    = -1;
  cg->any_root       = -1;
  cg->expired_certs  = -1;
  cg->validate_certs = -1;

#ifdef DEBUG
 /*
  * Friendly thread ID for debugging...
  */

  cg->thread_id = ++ cups_global_index;
#endif /* DEBUG */

 /*
  * Then set directories as appropriate...
  */

#ifdef _WIN32
  HKEY		key;			/* Registry key */
  DWORD		size;			/* Size of string */
  static char	installdir[1024] = "",	/* Install directory */
		localedir[1024] = "",	/* Locale directory */
		sysconfig[1024] = "",	/* Server configuration directory */
		userconfig[1024] = "";	/* User configuration directory */

  if (!installdir[0])
  {
   /*
    * Open the registry...
    */

    cupsCopyString(installdir, "C:/Program Files/cups.org", sizeof(installdir));

    if (!RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\cups.org", 0, KEY_READ, &key))
    {
     /*
      * Grab the installation directory...
      */

      char  *ptr;			/* Pointer into installdir */

      size = sizeof(installdir);
      RegQueryValueExA(key, "installdir", NULL, NULL, installdir, &size);
      RegCloseKey(key);

      for (ptr = installdir; *ptr;)
      {
        if (*ptr == '\\')
        {
          if (ptr[1])
            *ptr++ = '/';
          else
            *ptr = '\0';		/* Strip trailing \ */
        }
        else if (*ptr == '/' && !ptr[1])
          *ptr = '\0';			/* Strip trailing / */
        else
          ptr ++;
      }
    }

    snprintf(sysconfig, sizeof(sysconfig), "%s/conf", installdir);
    snprintf(localedir, sizeof(localedir), "%s/locale", installdir);
  }

  if ((cg->cups_datadir = getenv("CUPS_DATADIR")) == NULL)
    cg->cups_datadir = installdir;

  if ((cg->cups_serverbin = getenv("CUPS_SERVERBIN")) == NULL)
    cg->cups_serverbin = installdir;

  if ((cg->sysconfig = getenv("CUPS_SERVERROOT")) == NULL)
    cg->sysconfig = sysconfig;

  if ((cg->cups_statedir = getenv("CUPS_STATEDIR")) == NULL)
    cg->cups_statedir = sysconfig;

  if ((cg->localedir = getenv("LOCALEDIR")) == NULL)
    cg->localedir = localedir;

  if (!userconfig[0])
  {
    const char	*userprofile = getenv("USERPROFILE");
				// User profile (home) directory
    char	*userptr;	// Pointer into userconfig

    DEBUG_printf("cups_globals_alloc: USERPROFILE=\"%s\"", userprofile);

    snprintf(userconfig, sizeof(userconfig), "%s/AppData/Local/cups", userprofile);
    for (userptr = userconfig; *userptr; userptr ++)
    {
      // Convert back slashes to forward slashes
      if (*userptr == '\\')
        *userptr = '/';
    }

    DEBUG_printf("cups_globals_alloc: userconfig=\"%s\"", userconfig);
  }

  cg->userconfig = userconfig;

#else
  const char	*home = getenv("HOME");	// HOME environment variable
  char		homedir[1024],		// Home directory from account
		temp[1024];		// Temporary directory string
#  ifndef __APPLE__
  const char	*snap_common = getenv("SNAP_COMMON"),
		*xdg_config_home = getenv("XDG_CONFIG_HOME");
					// Environment variables
#  endif // !__APPLE__
#  ifdef HAVE_GETEUID
  if ((geteuid() != getuid() && getuid()) || getegid() != getgid())
#  else
  if (!getuid())
#  endif /* HAVE_GETEUID */
  {
   /*
    * When running setuid/setgid, don't allow environment variables to override
    * the directories...
    */

    cg->cups_datadir    = CUPS_DATADIR;
    cg->cups_serverbin  = CUPS_SERVERBIN;
    cg->sysconfig       = CUPS_SERVERROOT;
    cg->cups_statedir   = CUPS_STATEDIR;
    cg->localedir       = CUPS_LOCALEDIR;
  }
  else
  {
   /*
    * Allow directories to be overridden by environment variables.
    */

    if ((cg->cups_datadir = getenv("CUPS_DATADIR")) == NULL)
      cg->cups_datadir = CUPS_DATADIR;

    if ((cg->cups_serverbin = getenv("CUPS_SERVERBIN")) == NULL)
      cg->cups_serverbin = CUPS_SERVERBIN;

    if ((cg->sysconfig = getenv("CUPS_SERVERROOT")) == NULL)
      cg->sysconfig = CUPS_SERVERROOT;

    if ((cg->cups_statedir = getenv("CUPS_STATEDIR")) == NULL)
      cg->cups_statedir = CUPS_STATEDIR;

    if ((cg->localedir = getenv("LOCALEDIR")) == NULL)
      cg->localedir = CUPS_LOCALEDIR;
  }

#  ifdef __APPLE__
  if (!home)
#else
  if (!home && !xdg_config_home)
#  endif // __APPLE__
  if (!home)
  {
    struct passwd	pw;		/* User info */
    struct passwd	*result;	/* Auxiliary pointer */

    getpwuid_r(getuid(), &pw, cg->pw_buf, PW_BUF_SIZE, &result);
    if (result)
    {
      cupsCopyString(homedir, pw.pw_dir, sizeof(homedir));
      home = homedir;
    }
  }

#  ifdef __APPLE__
  if (home)
  {
    // macOS uses ~/Library/Application Support/FOO
    snprintf(temp, sizeof(temp), "%s/Library/Application Support/cups", home);
  }
  else
  {
    // Something went wrong, use temporary directory...
    snprintf(temp, sizeof(temp), "/private/tmp/cups%u", (unsigned)getuid());
  }

#  else
  if (snap_common)
  {
    // Snaps use $SNAP_COMMON/FOO
    snprintf(temp, sizeof(temp), "%s/cups", snap_common);
  }
  else if (xdg_config_home)
  {
    // XDG uses $XDG_CONFIG_HOME/FOO
    snprintf(temp, sizeof(temp), "%s/cups", xdg_config_home);
  }
  else if (home)
  {
    // Use ~/.cups if it exists, otherwise ~/.config/cups (XDG standard)
    snprintf(temp, sizeof(temp), "%s/.cups", home);
    if (access(temp, 0))
      snprintf(temp, sizeof(temp), "%s/.config/cups", home);
  }
  else
  {
    // Something went wrong, use temporary directory...
    snprintf(temp, sizeof(temp), "/tmp/cups%u", (unsigned)getuid());
  }
#  endif // __APPLE__

  // Can't use _cupsStrAlloc since it causes a loop with debug logging enabled
  cg->userconfig = strdup(temp);
#endif /* _WIN32 */

  return (cg);
}


/*
 * 'cups_globals_free()' - Free global data.
 */

#if defined(HAVE_PTHREAD_H) || defined(_WIN32)
static void
cups_globals_free(_cups_globals_t *cg)	/* I - Pointer to global data */
{
  _cups_buffer_t	*buffer,	/* Current read/write buffer */
			*next;		/* Next buffer */


  if (cg->last_status_message)
    _cupsStrFree(cg->last_status_message);

  for (buffer = cg->cups_buffers; buffer; buffer = next)
  {
    next = buffer->next;
    free(buffer);
  }

  cupsArrayDelete(cg->leg_size_lut);
  cupsArrayDelete(cg->ppd_size_lut);
  cupsArrayDelete(cg->pwg_size_lut);

  httpClose(cg->http);

  _httpFreeCredentials(cg->tls_credentials);

  cupsFileClose(cg->stdio_files[0]);
  cupsFileClose(cg->stdio_files[1]);
  cupsFileClose(cg->stdio_files[2]);

  cupsFreeOptions(cg->cupsd_num_settings, cg->cupsd_settings);

  free(cg->userconfig);
  free(cg->raster_error.start);

  free(cg);
}
#endif /* HAVE_PTHREAD_H || _WIN32 */


#ifdef HAVE_PTHREAD_H
/*
 * 'cups_globals_init()' - Initialize environment variables.
 */

static void
cups_globals_init(void)
{
 /*
  * Register the global data for this thread...
  */

  pthread_key_create(&cups_globals_key, (void (*)(void *))cups_globals_free);
}
#endif /* HAVE_PTHREAD_H */
