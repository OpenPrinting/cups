/*
 * Configuration routines for the CUPS scheduler.
 *
 * Copyright © 2020-2025 by OpenPrinting.
 * Copyright © 2007-2018 by Apple Inc.
 * Copyright © 1997-2007 by Easy Software Products, all rights reserved.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "cupsd.h"
#include <stdarg.h>
#include <grp.h>
#include <sys/utsname.h>
#ifdef HAVE_ASL_H
#  include <asl.h>
#elif defined(HAVE_SYSTEMD_SD_JOURNAL_H)
#  define SD_JOURNAL_SUPPRESS_LOCATION
#  include <systemd/sd-journal.h>
#endif /* HAVE_ASL_H */
#include <syslog.h>

#ifdef HAVE_LIBPAPER
#  include <paper.h>
#endif /* HAVE_LIBPAPER */


/*
 * Possibly missing network definitions...
 */

#ifndef INADDR_NONE
#  define INADDR_NONE	0xffffffff
#endif /* !INADDR_NONE */


/*
 * Configuration variable structure...
 */

typedef enum
{
  CUPSD_VARTYPE_INTEGER,		/* Integer option */
  CUPSD_VARTYPE_TIME,			/* Time interval option */
  CUPSD_VARTYPE_STRING,			/* String option */
  CUPSD_VARTYPE_BOOLEAN,		/* Boolean option */
  CUPSD_VARTYPE_PATHNAME,		/* File/directory name option */
  CUPSD_VARTYPE_PERM			/* File/directory permissions */
} cupsd_vartype_t;

typedef struct
{
  const char		*name;		/* Name of variable */
  void			*ptr;		/* Pointer to variable */
  cupsd_vartype_t	type;		/* Type (int, string, address) */
} cupsd_var_t;


/*
 * Local globals...
 */

static const cupsd_var_t	cupsd_vars[] =
{
  { "AutoPurgeJobs", 		&JobAutoPurge,		CUPSD_VARTYPE_BOOLEAN },
#ifdef HAVE_DNSSD
  { "BrowseDNSSDSubTypes",	&DNSSDSubTypes,		CUPSD_VARTYPE_STRING },
#endif /* HAVE_DNSSD */
  { "BrowseWebIF",		&BrowseWebIF,		CUPSD_VARTYPE_BOOLEAN },
  { "Browsing",			&Browsing,		CUPSD_VARTYPE_BOOLEAN },
  { "Classification",		&Classification,	CUPSD_VARTYPE_STRING },
  { "ClassifyOverride",		&ClassifyOverride,	CUPSD_VARTYPE_BOOLEAN },
  { "DefaultLanguage",		&DefaultLanguage,	CUPSD_VARTYPE_STRING },
  { "DefaultLeaseDuration",	&DefaultLeaseDuration,	CUPSD_VARTYPE_TIME },
  { "DefaultPaperSize",		&DefaultPaperSize,	CUPSD_VARTYPE_STRING },
  { "DefaultPolicy",		&DefaultPolicy,		CUPSD_VARTYPE_STRING },
  { "DefaultShared",		&DefaultShared,		CUPSD_VARTYPE_BOOLEAN },
  { "DirtyCleanInterval",	&DirtyCleanInterval,	CUPSD_VARTYPE_TIME },
#ifdef HAVE_DNSSD
  { "DNSSDHostName",		&DNSSDHostName,		CUPSD_VARTYPE_STRING },
#endif /* HAVE_DNSSD */
  { "ErrorPolicy",		&ErrorPolicy,		CUPSD_VARTYPE_STRING },
  { "FilterLimit",		&FilterLimit,		CUPSD_VARTYPE_INTEGER },
  { "FilterNice",		&FilterNice,		CUPSD_VARTYPE_INTEGER },
#ifdef HAVE_GSSAPI
  { "GSSServiceName",		&GSSServiceName,	CUPSD_VARTYPE_STRING },
#endif /* HAVE_GSSAPI */
#ifdef HAVE_ONDEMAND
  { "IdleExitTimeout",		&IdleExitTimeout,	CUPSD_VARTYPE_TIME },
#endif /* HAVE_ONDEMAND */
  { "JobKillDelay",		&JobKillDelay,		CUPSD_VARTYPE_TIME },
  { "JobRetryLimit",		&JobRetryLimit,		CUPSD_VARTYPE_INTEGER },
  { "JobRetryInterval",		&JobRetryInterval,	CUPSD_VARTYPE_TIME },
  { "KeepAlive",		&KeepAlive,		CUPSD_VARTYPE_BOOLEAN },
#ifdef HAVE_LAUNCHD
  { "LaunchdTimeout",		&IdleExitTimeout,	CUPSD_VARTYPE_TIME },
#endif /* HAVE_LAUNCHD */
  { "LimitRequestBody",		&MaxRequestSize,	CUPSD_VARTYPE_INTEGER },
  { "LogDebugHistory",		&LogDebugHistory,	CUPSD_VARTYPE_INTEGER },
  { "MaxActiveJobs",		&MaxActiveJobs,		CUPSD_VARTYPE_INTEGER },
  { "MaxClients",		&MaxClients,		CUPSD_VARTYPE_INTEGER },
  { "MaxClientsPerHost",	&MaxClientsPerHost,	CUPSD_VARTYPE_INTEGER },
  { "MaxCopies",		&MaxCopies,		CUPSD_VARTYPE_INTEGER },
  { "MaxEvents",		&MaxEvents,		CUPSD_VARTYPE_INTEGER },
  { "MaxHoldTime",		&MaxHoldTime,		CUPSD_VARTYPE_TIME },
  { "MaxJobs",			&MaxJobs,		CUPSD_VARTYPE_INTEGER },
  { "MaxJobsPerPrinter",	&MaxJobsPerPrinter,	CUPSD_VARTYPE_INTEGER },
  { "MaxJobsPerUser",		&MaxJobsPerUser,	CUPSD_VARTYPE_INTEGER },
  { "MaxJobTime",		&MaxJobTime,		CUPSD_VARTYPE_TIME },
  { "MaxLeaseDuration",		&MaxLeaseDuration,	CUPSD_VARTYPE_TIME },
  { "MaxLogSize",		&MaxLogSize,		CUPSD_VARTYPE_INTEGER },
  { "MaxRequestSize",		&MaxRequestSize,	CUPSD_VARTYPE_INTEGER },
  { "MaxSubscriptions",		&MaxSubscriptions,	CUPSD_VARTYPE_INTEGER },
  { "MaxSubscriptionsPerJob",	&MaxSubscriptionsPerJob,	CUPSD_VARTYPE_INTEGER },
  { "MaxSubscriptionsPerPrinter",&MaxSubscriptionsPerPrinter,	CUPSD_VARTYPE_INTEGER },
  { "MaxSubscriptionsPerUser",	&MaxSubscriptionsPerUser,	CUPSD_VARTYPE_INTEGER },
  { "MultipleOperationTimeout",	&MultipleOperationTimeout,	CUPSD_VARTYPE_TIME },
  { "PageLogFormat",		&PageLogFormat,		CUPSD_VARTYPE_STRING },
  { "PreserveJobFiles",		&JobFiles,		CUPSD_VARTYPE_TIME },
  { "PreserveJobHistory",	&JobHistory,		CUPSD_VARTYPE_TIME },
  { "ReloadTimeout",		&ReloadTimeout,		CUPSD_VARTYPE_TIME },
  { "RootCertDuration",		&RootCertDuration,	CUPSD_VARTYPE_TIME },
  { "ServerAdmin",		&ServerAdmin,		CUPSD_VARTYPE_STRING },
  { "ServerName",		&ServerName,		CUPSD_VARTYPE_STRING },
  { "StrictConformance",	&StrictConformance,	CUPSD_VARTYPE_BOOLEAN },
  { "Timeout",			&Timeout,		CUPSD_VARTYPE_TIME },
  { "WebInterface",		&WebInterface,		CUPSD_VARTYPE_BOOLEAN }
};
static const cupsd_var_t	cupsfiles_vars[] =
{
  { "AccessLog",		&AccessLog,		CUPSD_VARTYPE_STRING },
  { "CacheDir",			&CacheDir,		CUPSD_VARTYPE_STRING },
  { "ConfigFilePerm",		&ConfigFilePerm,	CUPSD_VARTYPE_PERM },
#ifdef HAVE_TLS
  { "CreateSelfSignedCerts",	&CreateSelfSignedCerts,	CUPSD_VARTYPE_BOOLEAN },
#endif /* HAVE_TLS */
  { "DataDir",			&DataDir,		CUPSD_VARTYPE_STRING },
  { "DocumentRoot",		&DocumentRoot,		CUPSD_VARTYPE_STRING },
  { "ErrorLog",			&ErrorLog,		CUPSD_VARTYPE_STRING },
  { "FileDevice",		&FileDevice,		CUPSD_VARTYPE_BOOLEAN },
  { "LogFilePerm",		&LogFilePerm,		CUPSD_VARTYPE_PERM },
  { "PageLog",			&PageLog,		CUPSD_VARTYPE_STRING },
  { "Printcap",			&Printcap,		CUPSD_VARTYPE_STRING },
  { "RemoteRoot",		&RemoteRoot,		CUPSD_VARTYPE_STRING },
  { "RequestRoot",		&RequestRoot,		CUPSD_VARTYPE_STRING },
  { "ServerBin",		&ServerBin,		CUPSD_VARTYPE_PATHNAME },
#ifdef HAVE_TLS
  { "ServerKeychain",		&ServerKeychain,	CUPSD_VARTYPE_PATHNAME },
#endif /* HAVE_TLS */
  { "ServerRoot",		&ServerRoot,		CUPSD_VARTYPE_PATHNAME },
  { "StateDir",			&StateDir,		CUPSD_VARTYPE_STRING },
  { "StripUserDomain",		&StripUserDomain,	CUPSD_VARTYPE_BOOLEAN },
  { "SyncOnClose",		&SyncOnClose,		CUPSD_VARTYPE_BOOLEAN },
#ifdef HAVE_AUTHORIZATION_H
  { "SystemGroupAuthKey",	&SystemGroupAuthKey,	CUPSD_VARTYPE_STRING },
#endif /* HAVE_AUTHORIZATION_H */
  { "TempDir",			&TempDir,		CUPSD_VARTYPE_PATHNAME }
};

static int		default_auth_type = CUPSD_AUTH_AUTO;
					/* Default AuthType, if not specified */

static const unsigned	ones[4] =
			{
			  0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff
			};
static const unsigned	zeros[4] =
			{
			  0x00000000, 0x00000000, 0x00000000, 0x00000000
			};


/*
 * Local functions...
 */

static http_addrlist_t	*get_address(const char *value, int defport);
static int		get_addr_and_mask(const char *value, unsigned *ip,
			                  unsigned *mask);
static void		mime_error_cb(void *ctx, const char *message);
static int		parse_aaa(cupsd_location_t *loc, char *line,
			          char *value, int linenum);
static int		parse_fatal_errors(const char *s);
static int		parse_groups(const char *s, int linenum);
static int		parse_protocols(const char *s);
static int		parse_variable(const char *filename, int linenum,
			               const char *line, const char *value,
			               size_t num_vars,
			               const cupsd_var_t *vars);
static int		read_cupsd_conf(cups_file_t *fp);
static int		read_cups_files_conf(cups_file_t *fp);
static int		read_location(cups_file_t *fp, char *name, int linenum);
static int		read_policy(cups_file_t *fp, char *name, int linenum);
static void		set_policy_defaults(cupsd_policy_t *pol);


/*
 * 'cupsdAddAlias()' - Add a host alias.
 */

void
cupsdAddAlias(cups_array_t *aliases,	/* I - Array of aliases */
              const char   *name)	/* I - Name to add */
{
  cupsd_alias_t	*a;			/*  New alias */
  size_t	namelen;		/* Length of name */


  namelen = strlen(name);

  if ((a = (cupsd_alias_t *)malloc(sizeof(cupsd_alias_t) + namelen)) == NULL)
    return;

  a->namelen = namelen;
  memcpy(a->name, name, namelen + 1);	/* OK since a->name is allocated */

  cupsArrayAdd(aliases, a);
}


/*
 * 'cupsdCheckPermissions()' - Fix the mode and ownership of a file or directory.
 */

int					/* O - 0 on success, -1 on error, 1 on warning */
cupsdCheckPermissions(
    const char *filename,		/* I - File/directory name */
    const char *suffix,			/* I - Additional file/directory name */
    mode_t     mode,			/* I - Permissions */
    uid_t      user,			/* I - Owner */
    gid_t      group,			/* I - Group */
    int        is_dir,			/* I - 1 = directory, 0 = file */
    int        create_dir)		/* I - 1 = create directory, -1 = create w/o logging, 0 = not */
{
  int		dir_created = 0;	/* Did we create a directory? */
  char		pathname[1024];		/* File name with prefix */
  struct stat	fileinfo;		/* Stat buffer */
  int		is_symlink;		/* Is "filename" a symlink? */


 /*
  * Prepend the given root to the filename before testing it...
  */

  if (suffix)
  {
    snprintf(pathname, sizeof(pathname), "%s/%s", filename, suffix);
    filename = pathname;
  }

 /*
  * See if we can stat the file/directory...
  */

  if (lstat(filename, &fileinfo))
  {
    if (errno == ENOENT && create_dir)
    {
      if (create_dir > 0)
	cupsdLogMessage(CUPSD_LOG_DEBUG, "Creating missing directory \"%s\"",
			filename);

      if (mkdir(filename, mode))
      {
        if (create_dir > 0)
	  cupsdLogMessage(CUPSD_LOG_ERROR,
			  "Unable to create directory \"%s\" - %s", filename,
			  strerror(errno));
        else
#ifdef HAVE_SYSTEMD_SD_JOURNAL_H
	  sd_journal_print(LOG_ERR, "Unable to create directory \"%s\" - %s", filename, strerror(errno));
#else
	  syslog(LOG_ERR, "Unable to create directory \"%s\" - %s", filename, strerror(errno));
#endif /* HAVE_SYSTEMD_SD_JOURNAL_H */

        return (-1);
      }

      dir_created      = 1;
      fileinfo.st_mode = mode | S_IFDIR;
    }
    else
      return (create_dir ? -1 : 1);
  }

  if ((is_symlink = S_ISLNK(fileinfo.st_mode)) != 0)
  {
    if (stat(filename, &fileinfo))
    {
      cupsdLogMessage(CUPSD_LOG_ERROR, "\"%s\" is a bad symlink - %s",
                      filename, strerror(errno));
      return (-1);
    }
  }

 /*
  * Make sure it's a regular file or a directory as needed...
  */

  if (!dir_created && !is_dir && !S_ISREG(fileinfo.st_mode))
  {
    cupsdLogMessage(CUPSD_LOG_ERROR, "\"%s\" is not a regular file.", filename);
    return (-1);
  }

  if (!dir_created && is_dir && !S_ISDIR(fileinfo.st_mode))
  {
    if (create_dir >= 0)
      cupsdLogMessage(CUPSD_LOG_ERROR, "\"%s\" is not a directory.", filename);
    else
#ifdef HAVE_SYSTEMD_SD_JOURNAL_H
      sd_journal_print(LOG_ERR, "\"%s\" is not a directory.", filename);
#else
      syslog(LOG_ERR, "\"%s\" is not a directory.", filename);
#endif /* HAVE_SYSTEMD_SD_JOURNAL_H */

    return (-1);
  }

 /*
  * If the filename is a symlink, do not change permissions (STR #2937)...
  */

  if (is_symlink)
    return (0);

 /*
  * Fix owner, group, and mode as needed...
  */

  if (dir_created || fileinfo.st_uid != user || fileinfo.st_gid != group)
  {
    if (create_dir >= 0)
      cupsdLogMessage(CUPSD_LOG_DEBUG, "Repairing ownership of \"%s\"",
                      filename);

    if (chown(filename, user, group) && !getuid())
    {
      if (create_dir >= 0)
	cupsdLogMessage(CUPSD_LOG_ERROR,
			"Unable to change ownership of \"%s\" - %s", filename,
			strerror(errno));
      else
#ifdef HAVE_SYSTEMD_SD_JOURNAL_H
	sd_journal_print(LOG_ERR, "Unable to change ownership of \"%s\" - %s", filename, strerror(errno));
#else
	syslog(LOG_ERR, "Unable to change ownership of \"%s\" - %s", filename, strerror(errno));
#endif /* HAVE_SYSTEMD_SD_JOURNAL_H */

      return (1);
    }
  }

  if (dir_created || (fileinfo.st_mode & 07777) != mode)
  {
    if (create_dir >= 0)
      cupsdLogMessage(CUPSD_LOG_DEBUG, "Repairing access permissions of \"%s\"",
		      filename);

    if (chmod(filename, mode))
    {
      if (create_dir >= 0)
	cupsdLogMessage(CUPSD_LOG_ERROR,
			"Unable to change permissions of \"%s\" - %s", filename,
			strerror(errno));
      else
#ifdef HAVE_SYSTEMD_SD_JOURNAL_H
	sd_journal_print(LOG_ERR, "Unable to change permissions of \"%s\" - %s", filename, strerror(errno));
#else
	syslog(LOG_ERR, "Unable to change permissions of \"%s\" - %s", filename, strerror(errno));
#endif /* HAVE_SYSTEMD_SD_JOURNAL_H */

      return (1);
    }
  }

 /*
  * Everything is OK...
  */

  return (0);
}


/*
 * 'cupsdDefaultAuthType()' - Get the default AuthType.
 *
 * When the default_auth_type is "auto", this function tries to get the GSS
 * credentials for the server.  If that succeeds we use Kerberos authentication,
 * otherwise we do a fallback to Basic authentication against the local user
 * accounts.
 */

int					/* O - Default AuthType value */
cupsdDefaultAuthType(void)
{
#ifdef HAVE_GSSAPI
  OM_uint32	major_status,		/* Major status code */
		minor_status;		/* Minor status code */
  gss_name_t	server_name;		/* Server name */
  gss_buffer_desc token = GSS_C_EMPTY_BUFFER;
					/* Service name token */
  char		buf[1024];		/* Service name buffer */
#endif /* HAVE_GSSAPI */


 /*
  * If we have already determined the correct default AuthType, use it...
  */

  if (default_auth_type != CUPSD_AUTH_AUTO)
    return (default_auth_type);

#ifdef HAVE_GSSAPI
#  ifdef __APPLE__
 /*
  * If the weak-linked GSSAPI/Kerberos library is not present, don't try
  * to use it...
  */

  if (&gss_init_sec_context == NULL)
    return (default_auth_type = CUPSD_AUTH_BASIC);
#  endif /* __APPLE__ */

 /*
  * Try to obtain the server's GSS credentials (GSSServiceName@servername).  If
  * that fails we must use Basic...
  */

  snprintf(buf, sizeof(buf), "%s@%s", GSSServiceName, ServerName);

  token.value  = buf;
  token.length = strlen(buf);
  server_name  = GSS_C_NO_NAME;
  major_status = gss_import_name(&minor_status, &token,
	 			 GSS_C_NT_HOSTBASED_SERVICE,
				 &server_name);

  memset(&token, 0, sizeof(token));

  if (GSS_ERROR(major_status))
  {
    cupsdLogGSSMessage(CUPSD_LOG_DEBUG, major_status, minor_status,
		       "cupsdDefaultAuthType: gss_import_name(%s) failed", buf);
    return (default_auth_type = CUPSD_AUTH_BASIC);
  }

  major_status = gss_display_name(&minor_status, server_name, &token, NULL);

  if (GSS_ERROR(major_status))
  {
    cupsdLogGSSMessage(CUPSD_LOG_DEBUG, major_status, minor_status,
                       "cupsdDefaultAuthType: gss_display_name(%s) failed",
                       buf);
    return (default_auth_type = CUPSD_AUTH_BASIC);
  }

  cupsdLogMessage(CUPSD_LOG_DEBUG,
                  "cupsdDefaultAuthType: Attempting to acquire Kerberos "
                  "credentials for %s...", (char *)token.value);

  ServerCreds  = GSS_C_NO_CREDENTIAL;
  major_status = gss_acquire_cred(&minor_status, server_name, GSS_C_INDEFINITE,
				  GSS_C_NO_OID_SET, GSS_C_ACCEPT,
				  &ServerCreds, NULL, NULL);
  if (GSS_ERROR(major_status))
  {
    cupsdLogGSSMessage(CUPSD_LOG_DEBUG, major_status, minor_status,
                       "cupsdDefaultAuthType: gss_acquire_cred(%s) failed",
                       (char *)token.value);
    gss_release_name(&minor_status, &server_name);
    gss_release_buffer(&minor_status, &token);
    return (default_auth_type = CUPSD_AUTH_BASIC);
  }

  cupsdLogMessage(CUPSD_LOG_DEBUG,
                  "cupsdDefaultAuthType: Kerberos credentials acquired "
                  "successfully for %s.", (char *)token.value);

  gss_release_name(&minor_status, &server_name);
  gss_release_buffer(&minor_status, &token);

  HaveServerCreds = 1;

  return (default_auth_type = CUPSD_AUTH_NEGOTIATE);

#else
 /*
  * No Kerberos support compiled in so just use Basic all the time...
  */

  return (default_auth_type = CUPSD_AUTH_BASIC);
#endif /* HAVE_GSSAPI */
}


/*
 * 'cupsdFreeAliases()' - Free all of the alias entries.
 */

void
cupsdFreeAliases(cups_array_t *aliases)	/* I - Array of aliases */
{
  cupsd_alias_t	*a;			/* Current alias */


  for (a = (cupsd_alias_t *)cupsArrayFirst(aliases);
       a;
       a = (cupsd_alias_t *)cupsArrayNext(aliases))
    free(a);

  cupsArrayDelete(aliases);
}


/*
 * 'cupsdReadConfiguration()' - Read the cupsd.conf file.
 */

int					/* O - 1 on success, 0 otherwise */
cupsdReadConfiguration(void)
{
  int		i;			/* Looping var */
  cups_file_t	*fp;			/* Configuration file */
  int		status;			/* Return status */
  char		temp[1024],		/* Temporary buffer */
		mimedir[1024],		/* MIME directory */
		*slash;			/* Directory separator */
  cups_lang_t	*language;		/* Language */
  struct passwd	*user;			/* Default user */
  struct group	*group;			/* Default group */
  char		*old_serverroot,	/* Old ServerRoot */
		*old_requestroot;	/* Old RequestRoot */
  int		old_remote_port;	/* Old RemotePort */
  const char	*tmpdir;		/* TMPDIR environment variable */
  struct stat	tmpinfo;		/* Temporary directory info */
  cupsd_policy_t *p;			/* Policy */


 /*
  * Save the old root paths...
  */

  old_serverroot = NULL;
  cupsdSetString(&old_serverroot, ServerRoot);
  old_requestroot = NULL;
  cupsdSetString(&old_requestroot, RequestRoot);

 /*
  * Reset the server configuration data...
  */

  cupsdDeleteAllLocations();

  cupsdDeleteAllListeners();

 /*
  * Allocate array Listeners
  */

  Listeners = cupsArrayNew(NULL, NULL);

  if (!Listeners)
  {
    fprintf(stderr, "Unable to allocate memory for array Listeners.\n");
    return (0);
  }

  old_remote_port = RemotePort;
  RemotePort      = 0;

 /*
  * String options...
  */

  cupsdFreeAliases(ServerAlias);
  ServerAlias = NULL;

  cupsdClearString(&ServerName);
  cupsdClearString(&ServerAdmin);
  cupsdSetString(&ServerBin, CUPS_SERVERBIN);
  cupsdSetString(&RequestRoot, CUPS_REQUESTS);
  cupsdSetString(&CacheDir, CUPS_CACHEDIR);
  cupsdSetString(&DataDir, CUPS_DATADIR);
  cupsdSetString(&DocumentRoot, CUPS_DOCROOT);
  cupsdSetString(&AccessLog, CUPS_LOGDIR "/access_log");
  cupsdClearString(&ErrorLog);
  cupsdSetString(&PageLog, CUPS_LOGDIR "/page_log");
  cupsdSetString(&PageLogFormat,
                 "%p %u %j %T %P %C %{job-billing} "
		 "%{job-originating-host-name} %{job-name} %{media} %{sides}");
  cupsdSetString(&Printcap, CUPS_DEFAULT_PRINTCAP);
  cupsdSetString(&RemoteRoot, "remroot");
  cupsdSetStringf(&ServerHeader, "CUPS/%d.%d IPP/2.1", CUPS_VERSION_MAJOR,
                  CUPS_VERSION_MINOR);
  cupsdSetString(&StateDir, CUPS_STATEDIR);

  if (!strcmp(CUPS_DEFAULT_PRINTCAP, "/etc/printers.conf"))
    PrintcapFormat = PRINTCAP_SOLARIS;
  else if (!strcmp(CUPS_DEFAULT_PRINTCAP,
                   "/Library/Preferences/org.cups.printers.plist"))
    PrintcapFormat = PRINTCAP_PLIST;
  else
    PrintcapFormat = PRINTCAP_BSD;

  strlcpy(temp, ConfigurationFile, sizeof(temp));
  if ((slash = strrchr(temp, '/')) != NULL)
    *slash = '\0';

  cupsdSetString(&ServerRoot, temp);

  cupsdClearString(&Classification);
  ClassifyOverride  = 0;

#ifdef HAVE_TLS
#  if defined HAVE_GNUTLS || defined HAVE_OPENSSL
  cupsdSetString(&ServerKeychain, "ssl");
#  else
  cupsdSetString(&ServerKeychain, "/Library/Keychains/System.keychain");
#  endif /* HAVE_GNUTLS || HAVE_OPENSSL */

  _httpTLSSetOptions(_HTTP_TLS_NONE, _HTTP_TLS_1_0, _HTTP_TLS_MAX);
#endif /* HAVE_TLS */

  language = cupsLangDefault();

  if (!strcmp(language->language, "C") || !strcmp(language->language, "POSIX"))
    cupsdSetString(&DefaultLanguage, "en");
  else
    cupsdSetString(&DefaultLanguage, language->language);

  cupsdClearString(&DefaultPaperSize);
  cupsArrayDelete(ReadyPaperSizes);
  ReadyPaperSizes = NULL;

  cupsdSetString(&TempDir, NULL);

#ifdef HAVE_GSSAPI
  cupsdSetString(&GSSServiceName, CUPS_DEFAULT_GSSSERVICENAME);

  if (HaveServerCreds)
  {
    OM_uint32	minor_status;		/* Minor status code */

    gss_release_cred(&minor_status, &ServerCreds);

    HaveServerCreds = 0;
  }

  ServerCreds = GSS_C_NO_CREDENTIAL;
#endif /* HAVE_GSSAPI */

 /*
  * Find the default user...
  */

  if ((user = getpwnam(CUPS_DEFAULT_USER)) != NULL)
    User = user->pw_uid;
  else
  {
   /*
    * Use the (historical) NFS nobody user ID (-2 as a 16-bit twos-
    * complement number...)
    */

    User = 65534;
  }

  endpwent();

 /*
  * Find the default group...
  */

  group = getgrnam(CUPS_DEFAULT_GROUP);
  endgrent();

  if (group)
    Group = group->gr_gid;
  else
  {
   /*
    * Fallback to group "nobody"...
    */

    group = getgrnam("nobody");
    endgrent();

    if (group)
      Group = group->gr_gid;
    else
    {
     /*
      * Use the (historical) NFS nobody group ID (-2 as a 16-bit twos-
      * complement number...)
      */

      Group = 65534;
    }
  }

 /*
  * Numeric options...
  */

  AccessLogLevel           = CUPSD_ACCESSLOG_ACTIONS;
  ConfigFilePerm           = CUPS_DEFAULT_CONFIG_FILE_PERM;
  FatalErrors              = parse_fatal_errors(CUPS_DEFAULT_FATAL_ERRORS);
  default_auth_type        = CUPSD_AUTH_BASIC;
#ifdef HAVE_TLS
  CreateSelfSignedCerts    = TRUE;
  DefaultEncryption        = HTTP_ENCRYPT_REQUIRED;
#endif /* HAVE_TLS */
  DirtyCleanInterval       = DEFAULT_KEEPALIVE;
  JobKillDelay             = DEFAULT_TIMEOUT;
  JobRetryLimit            = 5;
  JobRetryInterval         = 300;
  FileDevice               = FALSE;
  FilterLevel              = 0;
  FilterLimit              = 0;
  FilterNice               = 0;
  HostNameLookups          = FALSE;
  KeepAlive                = TRUE;
  LogDebugHistory          = 200;
  LogFilePerm              = CUPS_DEFAULT_LOG_FILE_PERM;
  LogFileGroup             = Group;
  LogLevel                 = CUPSD_LOG_WARN;
  StripUserDomain          = FALSE;
  LogTimeFormat            = CUPSD_TIME_STANDARD;
  MaxClients               = 100;
  MaxClientsPerHost        = 0;
  MaxLogSize               = 1024 * 1024;
  MaxRequestSize           = 0;
  MultipleOperationTimeout = 900;
  NumSystemGroups          = 0;
  ReloadTimeout	           = DEFAULT_KEEPALIVE;
  RootCertDuration         = 300;
  Sandboxing               = CUPSD_SANDBOXING_STRICT;
  StrictConformance        = FALSE;
#ifdef CUPS_DEFAULT_SYNC_ON_CLOSE
  SyncOnClose              = TRUE;
#else
  SyncOnClose              = FALSE;
#endif /* CUPS_DEFAULT_SYNC_ON_CLOSE */
  Timeout                  = 900;
  WebInterface             = CUPS_DEFAULT_WEBIF;

  BrowseLocalProtocols     = parse_protocols(CUPS_DEFAULT_BROWSE_LOCAL_PROTOCOLS);
  BrowseWebIF              = FALSE;
  Browsing                 = CUPS_DEFAULT_BROWSING;
  DefaultShared            = CUPS_DEFAULT_DEFAULT_SHARED;

#ifdef HAVE_DNSSD
  cupsdSetString(&DNSSDSubTypes, "_cups,_print,_universal");
  cupsdClearString(&DNSSDHostName);
#endif /* HAVE_DNSSD */

  cupsdSetString(&ErrorPolicy, CUPS_DEFAULT_ERROR_POLICY);

  JobHistory          = DEFAULT_HISTORY;
  JobFiles            = DEFAULT_FILES;
  JobAutoPurge        = 0;
  MaxHoldTime         = 0;
  MaxJobs             = 500;
  MaxActiveJobs       = 0;
  MaxJobsPerUser      = 0;
  MaxJobsPerPrinter   = 0;
  MaxJobTime          = 3 * 60 * 60;	/* 3 hours */
  MaxCopies           = CUPS_DEFAULT_MAX_COPIES;

  cupsdDeleteAllPolicies();
  cupsdClearString(&DefaultPolicy);

#ifdef HAVE_AUTHORIZATION_H
  cupsdSetString(&SystemGroupAuthKey, CUPS_DEFAULT_SYSTEM_AUTHKEY);
#endif /* HAVE_AUTHORIZATION_H */

  MaxSubscriptions           = 100;
  MaxSubscriptionsPerJob     = 0;
  MaxSubscriptionsPerPrinter = 0;
  MaxSubscriptionsPerUser    = 0;
  DefaultLeaseDuration       = 86400;
  MaxLeaseDuration           = 0;

#ifdef HAVE_ONDEMAND
  IdleExitTimeout = 60;
#endif /* HAVE_ONDEMAND */

 /*
  * Setup environment variables...
  */

  cupsdInitEnv();

 /*
  * Read the cups-files.conf file...
  */

  if ((fp = cupsFileOpen(CupsFilesFile, "r")) != NULL)
  {
    status = read_cups_files_conf(fp);

    cupsFileClose(fp);

    if (!status)
    {
      if (TestConfigFile)
        printf("\"%s\" contains errors.\n", CupsFilesFile);
      else
#ifdef HAVE_SYSTEMD_SD_JOURNAL_H
	sd_journal_print(LOG_ERR, "Unable to read \"%s\" due to errors.", CupsFilesFile);
#else
        syslog(LOG_LPR, "Unable to read \"%s\" due to errors.", CupsFilesFile);
#endif /* HAVE_SYSTEMD_SD_JOURNAL_H */

      return (0);
    }
  }
  else if (errno == ENOENT)
    cupsdLogMessage(CUPSD_LOG_INFO, "No %s, using defaults.", CupsFilesFile);
  else
  {
    fprintf(stderr, "Unable to read \"%s\" - %s\n", CupsFilesFile, strerror(errno));

    return (0);
  }

  if (!ErrorLog)
    cupsdSetString(&ErrorLog, CUPS_LOGDIR "/error_log");

 /*
  * Read the cupsd.conf file...
  */

  if ((fp = cupsFileOpen(ConfigurationFile, "r")) == NULL)
  {
#ifdef HAVE_SYSTEMD_SD_JOURNAL_H
    sd_journal_print(LOG_ERR, "Unable to open \"%s\" - %s", ConfigurationFile, strerror(errno));
#else
    syslog(LOG_LPR, "Unable to open \"%s\" - %s", ConfigurationFile, strerror(errno));
#endif /* HAVE_SYSTEMD_SD_JOURNAL_H */

    return (0);
  }

  status = read_cupsd_conf(fp);

  cupsFileClose(fp);

  if (!status)
  {
    if (TestConfigFile)
      printf("\"%s\" contains errors.\n", ConfigurationFile);
    else
#ifdef HAVE_SYSTEMD_SD_JOURNAL_H
      sd_journal_print(LOG_ERR, "Unable to read \"%s\" due to errors.", ConfigurationFile);
#else
      syslog(LOG_LPR, "Unable to read \"%s\" due to errors.", ConfigurationFile);
#endif /* HAVE_SYSTEMD_SD_JOURNAL_H */

    return (0);
  }

  RunUser = getuid();

  cupsdLogMessage(CUPSD_LOG_INFO, "Remote access is %s.",
                  RemotePort ? "enabled" : "disabled");

  if (!RemotePort)
    BrowseLocalProtocols = 0;		/* Disable sharing - no remote access */

 /*
  * See if the ServerName is an IP address...
  */

  if (ServerName)
  {
    if (!ServerAlias)
      ServerAlias = cupsArrayNew(NULL, NULL);

    cupsdAddAlias(ServerAlias, ServerName);
    cupsdLogMessage(CUPSD_LOG_DEBUG, "Added auto ServerAlias %s", ServerName);
  }
  else
  {
    if (gethostname(temp, sizeof(temp)))
    {
      cupsdLogMessage(CUPSD_LOG_ERROR, "Unable to get hostname: %s",
                      strerror(errno));
      strlcpy(temp, "localhost", sizeof(temp));
    }

    cupsdSetString(&ServerName, temp);

    if (!ServerAlias)
      ServerAlias = cupsArrayNew(NULL, NULL);

    cupsdAddAlias(ServerAlias, temp);
    cupsdLogMessage(CUPSD_LOG_DEBUG, "Added auto ServerAlias %s", temp);

    if (HostNameLookups)
    {
      struct hostent	*host;		/* Host entry to get FQDN */

      if ((host = gethostbyname(temp)) != NULL)
      {
        if (_cups_strcasecmp(temp, host->h_name))
        {
	  cupsdSetString(&ServerName, host->h_name);
	  cupsdAddAlias(ServerAlias, host->h_name);
          cupsdLogMessage(CUPSD_LOG_DEBUG, "Added auto ServerAlias %s",
	                  host->h_name);
	}

        if (host->h_aliases)
	{
          for (i = 0; host->h_aliases[i]; i ++)
	    if (_cups_strcasecmp(temp, host->h_aliases[i]))
	    {
	      cupsdAddAlias(ServerAlias, host->h_aliases[i]);
	      cupsdLogMessage(CUPSD_LOG_DEBUG, "Added auto ServerAlias %s",
	                      host->h_aliases[i]);
	    }
	}
      }
    }

   /*
    * Make sure we have the base hostname added as an alias, too!
    */

    if ((slash = strchr(temp, '.')) != NULL)
    {
      *slash = '\0';
      cupsdAddAlias(ServerAlias, temp);
      cupsdLogMessage(CUPSD_LOG_DEBUG, "Added auto ServerAlias %s", temp);
    }
  }

  for (slash = ServerName; isdigit(*slash & 255) || *slash == '.'; slash ++);

  ServerNameIsIP = !*slash;

 /*
  * Make sure ServerAdmin is initialized...
  */

  if (!ServerAdmin)
    cupsdSetStringf(&ServerAdmin, "root@%s", ServerName);

 /*
  * Use the default system group if none was supplied in cupsd.conf...
  */

  if (NumSystemGroups == 0)
  {
    if (!parse_groups(CUPS_DEFAULT_SYSTEM_GROUPS, 0))
    {
     /*
      * Find the group associated with GID 0...
      */

      group = getgrgid(0);
      endgrent();

      if (group != NULL)
	cupsdSetString(&SystemGroups[0], group->gr_name);
      else
	cupsdSetString(&SystemGroups[0], "unknown");

      SystemGroupIDs[0] = 0;
      NumSystemGroups   = 1;
    }
  }

 /*
  * Make sure ConfigFilePerm and LogFilePerm have sane values...
  */

  ConfigFilePerm &= 0664;
  LogFilePerm    &= 0664;

 /*
  * Open the system log for cupsd if necessary...
  */

  if (!LogStderr)
  {
    if (!strcmp(AccessLog, "stderr"))
      cupsdSetString(&AccessLog, "syslog");

    if (!strcmp(ErrorLog, "stderr"))
      cupsdSetString(&ErrorLog, "syslog");

    if (!strcmp(PageLog, "stderr"))
      cupsdSetString(&PageLog, "syslog");
  }

#if defined(HAVE_VSYSLOG) && !defined(HAVE_ASL_H) && !defined(HAVE_SYSTEMD_SD_JOURNAL_H)
  if (!strcmp(AccessLog, "syslog") ||
      !strcmp(ErrorLog, "syslog") ||
      !strcmp(PageLog, "syslog"))
    openlog("cupsd", LOG_PID | LOG_NOWAIT | LOG_NDELAY, LOG_LPR);
#endif /* HAVE_VSYSLOG && !HAVE_ASL_H && !HAVE_SYSTEMD_SD_JOURNAL_H */

 /*
  * Log the configuration file that was used...
  */

  cupsdLogMessage(CUPSD_LOG_INFO, "Loaded configuration file \"%s\"",
                  ConfigurationFile);

 /*
  * Validate the Group and SystemGroup settings - they cannot be the same,
  * otherwise the CGI programs will be able to authenticate as root without
  * a password!
  */

  if (!RunUser)
  {
    for (i = 0; i < NumSystemGroups; i ++)
      if (Group == SystemGroupIDs[i])
        break;

    if (i < NumSystemGroups)
    {
     /*
      * Log the error and reset the group to a safe value...
      */

      cupsdLogMessage(CUPSD_LOG_ERROR,
                      "Group and SystemGroup cannot use the same groups.");
      if (FatalErrors & (CUPSD_FATAL_CONFIG | CUPSD_FATAL_PERMISSIONS))
        return (0);

      cupsdLogMessage(CUPSD_LOG_INFO, "Resetting Group to \"nobody\"...");

      group = getgrnam("nobody");
      endgrent();

      if (group != NULL)
	Group = group->gr_gid;
      else
      {
       /*
	* Use the (historical) NFS nobody group ID (-2 as a 16-bit twos-
	* complement number...)
	*/

	Group = 65534;
      }
    }
  }

 /*
  * Set the default locale using the language and charset...
  */

  cupsdSetStringf(&DefaultLocale, "%s.UTF-8", DefaultLanguage);

 /*
  * Update all relative filenames to include the full path from ServerRoot...
  */

  if (DocumentRoot[0] != '/')
    cupsdSetStringf(&DocumentRoot, "%s/%s", ServerRoot, DocumentRoot);

  if (RequestRoot[0] != '/')
    cupsdSetStringf(&RequestRoot, "%s/%s", ServerRoot, RequestRoot);

  if (ServerBin[0] != '/')
    cupsdSetStringf(&ServerBin, "%s/%s", ServerRoot, ServerBin);

  if (StateDir[0] != '/')
    cupsdSetStringf(&StateDir, "%s/%s", ServerRoot, StateDir);

  if (CacheDir[0] != '/')
    cupsdSetStringf(&CacheDir, "%s/%s", ServerRoot, CacheDir);

#ifdef HAVE_TLS
  if (!_cups_strcasecmp(ServerKeychain, "internal"))
    cupsdClearString(&ServerKeychain);
  else if (ServerKeychain[0] != '/')
    cupsdSetStringf(&ServerKeychain, "%s/%s", ServerRoot, ServerKeychain);

  cupsdLogMessage(CUPSD_LOG_DEBUG, "Using keychain \"%s\" for server name \"%s\".", ServerKeychain ? ServerKeychain : "internal", ServerName);
  if (!CreateSelfSignedCerts)
    cupsdLogMessage(CUPSD_LOG_DEBUG, "Self-signed TLS certificate generation is disabled.");
  cupsSetServerCredentials(ServerKeychain, ServerName, CreateSelfSignedCerts);
#endif /* HAVE_TLS */

 /*
  * Make sure that directories and config files are owned and
  * writable by the user and group in the cupsd.conf file...
  */

  snprintf(temp, sizeof(temp), "%s/rss", CacheDir);

  if ((cupsdCheckPermissions(RequestRoot, NULL, 0710, RunUser,
			     Group, 1, 1) < 0 ||
       cupsdCheckPermissions(CacheDir, NULL, 0770, RunUser,
			     Group, 1, 1) < 0 ||
       cupsdCheckPermissions(temp, NULL, 0775, RunUser,
			     Group, 1, 1) < 0 ||
       cupsdCheckPermissions(StateDir, NULL, 0755, RunUser,
			     Group, 1, 1) < 0 ||
#if CUPS_SNAP
       cupsdCheckPermissions(StateDir, "certs", 0711, RunUser, 0, 1, 1) < 0 ||
#else
       cupsdCheckPermissions(StateDir, "certs", RunUser ? 0711 : 0511, User, SystemGroupIDs[0], 1, 1) < 0 ||
#endif /* CUPS_SNAP */
       cupsdCheckPermissions(ServerRoot, NULL, 0755, RunUser,
			     Group, 1, 0) < 0 ||
       cupsdCheckPermissions(ServerRoot, "ppd", 0755, RunUser,
			     Group, 1, 1) < 0 ||
       cupsdCheckPermissions(ServerRoot, "ssl", 0700, RunUser,
			     Group, 1, 0) < 0 ||
       cupsdCheckPermissions(ConfigurationFile, NULL, ConfigFilePerm, RunUser,
			     Group, 0, 0) < 0 ||
       cupsdCheckPermissions(CupsFilesFile, NULL, ConfigFilePerm, RunUser,
			     Group, 0, 0) < 0 ||
       cupsdCheckPermissions(ServerRoot, "classes.conf", 0600, RunUser,
			     Group, 0, 0) < 0 ||
       cupsdCheckPermissions(ServerRoot, "printers.conf", 0600, RunUser,
			     Group, 0, 0) < 0 ||
       cupsdCheckPermissions(ServerRoot, "passwd.md5", 0600, User,
			     Group, 0, 0) < 0) &&
      (FatalErrors & CUPSD_FATAL_PERMISSIONS))
    return (0);

 /*
  * Update TempDir to the default if it hasn't been set already...
  */

#ifdef __APPLE__
  if (TempDir && !RunUser &&
      (!strncmp(TempDir, "/private/tmp", 12) || !strncmp(TempDir, "/tmp", 4)))
  {
    cupsdLogMessage(CUPSD_LOG_ERROR, "Cannot use %s for TempDir.", TempDir);
    cupsdClearString(&TempDir);
  }
#endif /* __APPLE__ */

  if (!TempDir)
  {
#ifdef __APPLE__
    if ((tmpdir = getenv("TMPDIR")) != NULL &&
        strncmp(tmpdir, "/private/tmp", 12) && strncmp(tmpdir, "/tmp", 4))
#else
    if ((tmpdir = getenv("TMPDIR")) != NULL)
#endif /* __APPLE__ */
    {
     /*
      * TMPDIR is defined, see if it is OK for us to use...
      */

      if (stat(tmpdir, &tmpinfo))
        cupsdLogMessage(CUPSD_LOG_ERROR, "Unable to access TMPDIR (%s): %s",
	                tmpdir, strerror(errno));
      else if (!S_ISDIR(tmpinfo.st_mode))
        cupsdLogMessage(CUPSD_LOG_ERROR, "TMPDIR (%s) is not a directory.",
	                tmpdir);
      else if ((tmpinfo.st_uid != User || !(tmpinfo.st_mode & S_IWUSR)) &&
               (tmpinfo.st_gid != Group || !(tmpinfo.st_mode & S_IWGRP)) &&
	       !(tmpinfo.st_mode & S_IWOTH))
        cupsdLogMessage(CUPSD_LOG_ERROR,
	                "TMPDIR (%s) has the wrong permissions.", tmpdir);
      else
        cupsdSetString(&TempDir, tmpdir);
    }
  }

  if (!TempDir)
  {
    cupsdLogMessage(CUPSD_LOG_INFO, "Using default TempDir of %s/tmp...",
		    RequestRoot);
    cupsdSetStringf(&TempDir, "%s/tmp", RequestRoot);
  }

  setenv("TMPDIR", TempDir, 1);

 /*
  * Make sure the temporary directory has the right permissions...
  */

  if (!strncmp(TempDir, RequestRoot, strlen(RequestRoot)) ||
      access(TempDir, 0))
  {
   /*
    * Update ownership and permissions if the CUPS temp directory
    * is under the spool directory or does not exist...
    */

    if (cupsdCheckPermissions(TempDir, NULL, 01770, RunUser, Group, 1, 1) < 0 &&
	(FatalErrors & CUPSD_FATAL_PERMISSIONS))
      return (0);
  }

 /*
  * Update environment variables...
  */

  cupsdUpdateEnv();

  /*
   * Validate the default error policy...
   */

  if (strcmp(ErrorPolicy, "retry-current-job") &&
      strcmp(ErrorPolicy, "abort-job") &&
      strcmp(ErrorPolicy, "retry-job") &&
      strcmp(ErrorPolicy, "stop-printer"))
  {
    cupsdLogMessage(CUPSD_LOG_ALERT, "Invalid ErrorPolicy \"%s\", resetting to \"stop-printer\".", ErrorPolicy);
    cupsdSetString(&ErrorPolicy, "stop-printer");
  }

 /*
  * Update default paper size setting as needed...
  */

  if (!DefaultPaperSize)
  {
#ifdef HAVE_LIBPAPER
    char	*paper_result;		/* Paper size name from libpaper */

    if ((paper_result = systempapername()) != NULL)
    {
      cupsdSetString(&DefaultPaperSize, paper_result);
      free(paper_result);
    }
    else
#endif /* HAVE_LIBPAPER */
    if (!DefaultLanguage ||
        !_cups_strcasecmp(DefaultLanguage, "C") ||
        !_cups_strcasecmp(DefaultLanguage, "POSIX") ||
	!_cups_strcasecmp(DefaultLanguage, "en") ||
	!_cups_strncasecmp(DefaultLanguage, "en.", 3) ||
	!_cups_strncasecmp(DefaultLanguage, "en_US", 5) ||
	!_cups_strncasecmp(DefaultLanguage, "en_CA", 5) ||
	!_cups_strncasecmp(DefaultLanguage, "fr_CA", 5))
    {
     /*
      * These are the only locales that will default to "letter" size...
      */

      cupsdSetString(&DefaultPaperSize, "Letter");
    }
    else
      cupsdSetString(&DefaultPaperSize, "A4");
  }

  if (!ReadyPaperSizes)
  {
    // Build default list of common sizes for North America and worldwide...
    if (!strcasecmp(DefaultPaperSize, "Letter"))
      ReadyPaperSizes = _cupsArrayNewStrings("Letter,Legal,Tabloid,4x6,Env10", ',');
    else if (!strcasecmp(DefaultPaperSize, "A4"))
      ReadyPaperSizes = _cupsArrayNewStrings("A4,A3,A5,A6,EnvDL", ',');
    else
      ReadyPaperSizes = _cupsArrayNewStrings(DefaultPaperSize, ',');
  }

 /*
  * Update classification setting as needed...
  */

  if (Classification && !_cups_strcasecmp(Classification, "none"))
    cupsdClearString(&Classification);

  if (Classification)
    cupsdLogMessage(CUPSD_LOG_INFO, "Security set to \"%s\"", Classification);

 /*
  * Check the MaxClients setting, and then allocate memory for it...
  */

  if (MaxClients > (MaxFDs / 3) || MaxClients <= 0)
  {
    if (MaxClients > 0)
      cupsdLogMessage(CUPSD_LOG_INFO,
                      "MaxClients limited to 1/3 (%d) of the file descriptor "
		      "limit (%d)...",
                      MaxFDs / 3, MaxFDs);

    MaxClients = MaxFDs / 3;
  }

  cupsdLogMessage(CUPSD_LOG_INFO, "Configured for up to %d clients.",
                  MaxClients);

 /*
  * Check the MaxActiveJobs setting; limit to 1/3 the available
  * file descriptors, since we need a pipe for each job...
  */

  if (MaxActiveJobs > (MaxFDs / 3))
    MaxActiveJobs = MaxFDs / 3;

 /*
  * Update the MaxClientsPerHost value, as needed...
  */

  if (MaxClientsPerHost <= 0)
    MaxClientsPerHost = MaxClients;

  if (MaxClientsPerHost > MaxClients)
    MaxClientsPerHost = MaxClients;

  cupsdLogMessage(CUPSD_LOG_INFO,
                  "Allowing up to %d client connections per host.",
                  MaxClientsPerHost);

 /*
  * Update the default policy, as needed...
  */

  if (DefaultPolicy)
    DefaultPolicyPtr = cupsdFindPolicy(DefaultPolicy);
  else
    DefaultPolicyPtr = NULL;

  if (!DefaultPolicyPtr)
  {
    cupsd_location_t	*po;		/* New policy operation */


    if (DefaultPolicy)
      cupsdLogMessage(CUPSD_LOG_ERROR, "Default policy \"%s\" not found.",
                      DefaultPolicy);

    cupsdSetString(&DefaultPolicy, "default");

    if ((DefaultPolicyPtr = cupsdFindPolicy("default")) != NULL)
      cupsdLogMessage(CUPSD_LOG_INFO,
                      "Using policy \"default\" as the default.");
    else
    {
      cupsdLogMessage(CUPSD_LOG_INFO,
                      "Creating CUPS default administrative policy:");

      DefaultPolicyPtr = p = cupsdAddPolicy("default");

      cupsdLogMessage(CUPSD_LOG_INFO, "<Policy default>");
	cupsdLogMessage(CUPSD_LOG_INFO, "JobPrivateAccess default");
	cupsdAddString(&(p->job_access), "@OWNER");
	cupsdAddString(&(p->job_access), "@SYSTEM");

	cupsdLogMessage(CUPSD_LOG_INFO, "JobPrivateValues default");
	cupsdAddString(&(p->job_attrs), "job-name");
	cupsdAddString(&(p->job_attrs), "job-originating-host-name");
	cupsdAddString(&(p->job_attrs), "job-originating-user-name");
	cupsdAddString(&(p->job_attrs), "phone");

	cupsdLogMessage(CUPSD_LOG_INFO, "SubscriptionPrivateAccess default");
	cupsdAddString(&(p->sub_access), "@OWNER");
	cupsdAddString(&(p->sub_access), "@SYSTEM");

	cupsdLogMessage(CUPSD_LOG_INFO, "SubscriptionPrivateValues default");
	cupsdAddString(&(p->job_attrs), "notify-events");
	cupsdAddString(&(p->job_attrs), "notify-pull-method");
	cupsdAddString(&(p->job_attrs), "notify-recipient-uri");
	cupsdAddString(&(p->job_attrs), "notify-subscriber-user-name");
	cupsdAddString(&(p->job_attrs), "notify-user-data");

	cupsdLogMessage(CUPSD_LOG_INFO, "<Limit Create-Job Print-Job Print-URI Validate-Job>");
	  po = cupsdAddPolicyOp(p, NULL, IPP_CREATE_JOB);
	  cupsdAddPolicyOp(p, po, IPP_PRINT_JOB);
	  cupsdAddPolicyOp(p, po, IPP_PRINT_URI);
	  cupsdAddPolicyOp(p, po, IPP_VALIDATE_JOB);

	  cupsdLogMessage(CUPSD_LOG_INFO, "Order Deny,Allow");
	  po->order_type = CUPSD_AUTH_ALLOW;
	cupsdLogMessage(CUPSD_LOG_INFO, "</Limit>");

	cupsdLogMessage(CUPSD_LOG_INFO, "<Limit Send-Document Send-URI Hold-Job Release-Job Restart-Job Purge-Jobs Set-Job-Attributes Create-Job-Subscription Renew-Subscription Cancel-Subscription Get-Notifications Reprocess-Job Cancel-Current-Job Suspend-Current-Job Resume-Job Cancel-My-Jobs Close-Job CUPS-Move-Job>");
	  po = cupsdAddPolicyOp(p, NULL, IPP_SEND_DOCUMENT);
	  cupsdAddPolicyOp(p, po, IPP_SEND_URI);
	  cupsdAddPolicyOp(p, po, IPP_HOLD_JOB);
	  cupsdAddPolicyOp(p, po, IPP_RELEASE_JOB);
	  cupsdAddPolicyOp(p, po, IPP_RESTART_JOB);
	  cupsdAddPolicyOp(p, po, IPP_PURGE_JOBS);
	  cupsdAddPolicyOp(p, po, IPP_SET_JOB_ATTRIBUTES);
	  cupsdAddPolicyOp(p, po, IPP_CREATE_JOB_SUBSCRIPTION);
	  cupsdAddPolicyOp(p, po, IPP_RENEW_SUBSCRIPTION);
	  cupsdAddPolicyOp(p, po, IPP_CANCEL_SUBSCRIPTION);
	  cupsdAddPolicyOp(p, po, IPP_GET_NOTIFICATIONS);
	  cupsdAddPolicyOp(p, po, IPP_REPROCESS_JOB);
	  cupsdAddPolicyOp(p, po, IPP_CANCEL_CURRENT_JOB);
	  cupsdAddPolicyOp(p, po, IPP_SUSPEND_CURRENT_JOB);
	  cupsdAddPolicyOp(p, po, IPP_RESUME_JOB);
	  cupsdAddPolicyOp(p, po, IPP_CANCEL_MY_JOBS);
	  cupsdAddPolicyOp(p, po, IPP_CLOSE_JOB);
	  cupsdAddPolicyOp(p, po, CUPS_MOVE_JOB);

	  cupsdLogMessage(CUPSD_LOG_INFO, "Order Deny,Allow");
	  po->order_type = CUPSD_AUTH_ALLOW;

	  cupsdLogMessage(CUPSD_LOG_INFO, "Require user @OWNER @SYSTEM");
	  po->level = CUPSD_AUTH_USER;
	  cupsdAddName(po, "@OWNER");
	  cupsdAddName(po, "@SYSTEM");
	cupsdLogMessage(CUPSD_LOG_INFO, "</Limit>");

	cupsdLogMessage(CUPSD_LOG_INFO, "<Limit CUPS-Authenticate-Job>");
	  po = cupsdAddPolicyOp(p, NULL, CUPS_GET_DOCUMENT);

	  cupsdLogMessage(CUPSD_LOG_INFO, "Order Deny,Allow");
	  po->order_type = CUPSD_AUTH_ALLOW;

	  cupsdLogMessage(CUPSD_LOG_INFO, "AuthType Default");
	  po->type = CUPSD_AUTH_DEFAULT;

	  cupsdLogMessage(CUPSD_LOG_INFO, "Require user @OWNER @SYSTEM");
	  po->level = CUPSD_AUTH_USER;
	  cupsdAddName(po, "@OWNER");
	  cupsdAddName(po, "@SYSTEM");
	cupsdLogMessage(CUPSD_LOG_INFO, "</Limit>");

	cupsdLogMessage(CUPSD_LOG_INFO, "<Limit Pause-Printer Resume-Printer  Set-Printer-Attributes Enable-Printer Disable-Printer Pause-Printer-After-Current-Job Hold-New-Jobs Release-Held-New-Jobs Deactivate-Printer Activate-Printer Restart-Printer Shutdown-Printer Startup-Printer Promote-Job Schedule-Job-After Cancel-Jobs CUPS-Add-Printer CUPS-Delete-Printer CUPS-Add-Class CUPS-Delete-Class CUPS-Accept-Jobs CUPS-Reject-Jobs CUPS-Set-Default>");
	  po = cupsdAddPolicyOp(p, NULL, IPP_PAUSE_PRINTER);
	  cupsdAddPolicyOp(p, po, IPP_RESUME_PRINTER);
	  cupsdAddPolicyOp(p, po, IPP_SET_PRINTER_ATTRIBUTES);
	  cupsdAddPolicyOp(p, po, IPP_ENABLE_PRINTER);
	  cupsdAddPolicyOp(p, po, IPP_DISABLE_PRINTER);
	  cupsdAddPolicyOp(p, po, IPP_PAUSE_PRINTER_AFTER_CURRENT_JOB);
	  cupsdAddPolicyOp(p, po, IPP_HOLD_NEW_JOBS);
	  cupsdAddPolicyOp(p, po, IPP_RELEASE_HELD_NEW_JOBS);
	  cupsdAddPolicyOp(p, po, IPP_DEACTIVATE_PRINTER);
	  cupsdAddPolicyOp(p, po, IPP_ACTIVATE_PRINTER);
	  cupsdAddPolicyOp(p, po, IPP_RESTART_PRINTER);
	  cupsdAddPolicyOp(p, po, IPP_SHUTDOWN_PRINTER);
	  cupsdAddPolicyOp(p, po, IPP_STARTUP_PRINTER);
	  cupsdAddPolicyOp(p, po, IPP_PROMOTE_JOB);
	  cupsdAddPolicyOp(p, po, IPP_SCHEDULE_JOB_AFTER);
	  cupsdAddPolicyOp(p, po, IPP_CANCEL_JOBS);
	  cupsdAddPolicyOp(p, po, CUPS_ADD_PRINTER);
	  cupsdAddPolicyOp(p, po, CUPS_DELETE_PRINTER);
	  cupsdAddPolicyOp(p, po, CUPS_ADD_CLASS);
	  cupsdAddPolicyOp(p, po, CUPS_DELETE_CLASS);
	  cupsdAddPolicyOp(p, po, CUPS_ACCEPT_JOBS);
	  cupsdAddPolicyOp(p, po, CUPS_REJECT_JOBS);
	  cupsdAddPolicyOp(p, po, CUPS_SET_DEFAULT);

	  cupsdLogMessage(CUPSD_LOG_INFO, "Order Deny,Allow");
	  po->order_type = CUPSD_AUTH_ALLOW;

	  cupsdLogMessage(CUPSD_LOG_INFO, "AuthType Default");
	  po->type = CUPSD_AUTH_DEFAULT;

	  cupsdLogMessage(CUPSD_LOG_INFO, "Require user @SYSTEM");
	  po->level = CUPSD_AUTH_USER;
	  cupsdAddName(po, "@SYSTEM");
	cupsdLogMessage(CUPSD_LOG_INFO, "</Limit>");

	cupsdLogMessage(CUPSD_LOG_INFO, "<Limit Cancel-Job>");
	  po = cupsdAddPolicyOp(p, NULL, IPP_CANCEL_JOB);

	  cupsdLogMessage(CUPSD_LOG_INFO, "Order Deny,Allow");
	  po->order_type = CUPSD_AUTH_ALLOW;

	  cupsdLogMessage(CUPSD_LOG_INFO, "Require user @OWNER " CUPS_DEFAULT_PRINTOPERATOR_AUTH);
	  po->level = CUPSD_AUTH_USER;
	  cupsdAddName(po, "@OWNER");
	  cupsdAddName(po, CUPS_DEFAULT_PRINTOPERATOR_AUTH);
	cupsdLogMessage(CUPSD_LOG_INFO, "</Limit>");

	cupsdLogMessage(CUPSD_LOG_INFO, "<Limit CUPS-Authenticate-Job>");
	  po = cupsdAddPolicyOp(p, NULL, CUPS_AUTHENTICATE_JOB);

	  cupsdLogMessage(CUPSD_LOG_INFO, "AuthType Default");
	  po->type = CUPSD_AUTH_DEFAULT;

	  cupsdLogMessage(CUPSD_LOG_INFO, "Order Deny,Allow");
	  po->order_type = CUPSD_AUTH_ALLOW;

	  cupsdLogMessage(CUPSD_LOG_INFO, "Require user @OWNER " CUPS_DEFAULT_PRINTOPERATOR_AUTH);
	  po->level = CUPSD_AUTH_USER;
	  cupsdAddName(po, "@OWNER");
	  cupsdAddName(po, CUPS_DEFAULT_PRINTOPERATOR_AUTH);
	cupsdLogMessage(CUPSD_LOG_INFO, "</Limit>");

	cupsdLogMessage(CUPSD_LOG_INFO, "<Limit All>");
	  po = cupsdAddPolicyOp(p, NULL, IPP_ANY_OPERATION);

	  cupsdLogMessage(CUPSD_LOG_INFO, "Order Deny,Allow");
	  po->order_type = CUPSD_AUTH_ALLOW;
	cupsdLogMessage(CUPSD_LOG_INFO, "</Limit>");
      cupsdLogMessage(CUPSD_LOG_INFO, "</Policy>");
    }
  }

  if (LogLevel >= CUPSD_LOG_DEBUG2)
  {
    cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdReadConfiguration: NumPolicies=%d",
		    cupsArrayCount(Policies));
    for (i = 0, p = (cupsd_policy_t *)cupsArrayFirst(Policies);
	 p;
	 i ++, p = (cupsd_policy_t *)cupsArrayNext(Policies))
    {
      int		j;		/* Looping var */
      cupsd_location_t	*loc;		/* Current location */

      cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdReadConfiguration: Policies[%d]=\"%s\"", i, p->name);

      for (j = 0, loc = (cupsd_location_t *)cupsArrayFirst(p->ops); loc; j ++, loc = (cupsd_location_t *)cupsArrayNext(p->ops))
      {
        cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdReadConfiguration:     ops[%d]=%s", j, ippOpString(loc->op));
      }
    }
  }

 /*
  * If we are doing a full reload or the server root has changed, flush
  * the jobs, printers, etc. and start from scratch...
  */

  if (NeedReload == RELOAD_ALL ||
      old_remote_port != RemotePort ||
      !old_serverroot || !ServerRoot || strcmp(old_serverroot, ServerRoot) ||
      !old_requestroot || !RequestRoot || strcmp(old_requestroot, RequestRoot))
  {
    mime_type_t	*type;			/* Current type */
    char	mimetype[MIME_MAX_SUPER + MIME_MAX_TYPE];
					/* MIME type name */


    cupsdLogMessage(CUPSD_LOG_INFO, "Full reload is required.");

   /*
    * Free all memory...
    */

    cupsdDeleteAllSubscriptions();
    cupsdFreeAllJobs();
    cupsdDeleteAllPrinters();

    DefaultPrinter = NULL;

    if (MimeDatabase != NULL)
      mimeDelete(MimeDatabase);

    if (NumMimeTypes)
    {
      for (i = 0; i < NumMimeTypes; i ++)
	_cupsStrFree(MimeTypes[i]);

      free(MimeTypes);
    }

   /*
    * Read the MIME type and conversion database...
    */

    snprintf(temp, sizeof(temp), "%s/filter", ServerBin);
    snprintf(mimedir, sizeof(mimedir), "%s/mime", DataDir);

    MimeDatabase = mimeNew();
    mimeSetErrorCallback(MimeDatabase, mime_error_cb, NULL);
    _cupsRWInit(&MimeDatabase->lock);

    _cupsRWLockWrite(&MimeDatabase->lock);
    MimeDatabase = mimeLoadTypes(MimeDatabase, mimedir);
    MimeDatabase = mimeLoadTypes(MimeDatabase, ServerRoot);
    MimeDatabase = mimeLoadFilters(MimeDatabase, mimedir, temp);
    MimeDatabase = mimeLoadFilters(MimeDatabase, ServerRoot, temp);
    _cupsRWUnlock(&MimeDatabase->lock);

    if (!MimeDatabase)
    {
      cupsdLogMessage(CUPSD_LOG_EMERG,
                      "Unable to load MIME database from \"%s\" or \"%s\".",
		      mimedir, ServerRoot);
      if (FatalErrors & CUPSD_FATAL_CONFIG)
        return (0);
    }

    cupsdLogMessage(CUPSD_LOG_INFO,
                    "Loaded MIME database from \"%s\" and \"%s\": %d types, "
		    "%d filters...", mimedir, ServerRoot,
		    mimeNumTypes(MimeDatabase), mimeNumFilters(MimeDatabase));

   /*
    * Create a list of MIME types for the document-format-supported
    * attribute...
    */

    NumMimeTypes = mimeNumTypes(MimeDatabase);
    if (!mimeType(MimeDatabase, "application", "octet-stream"))
      NumMimeTypes ++;

    if ((MimeTypes = calloc((size_t)NumMimeTypes, sizeof(const char *))) == NULL)
    {
      cupsdLogMessage(CUPSD_LOG_ERROR,
                      "Unable to allocate memory for %d MIME types.",
		      NumMimeTypes);
      NumMimeTypes = 0;
    }
    else
    {
      for (i = 0, type = mimeFirstType(MimeDatabase);
	   type;
	   i ++, type = mimeNextType(MimeDatabase))
      {
	snprintf(mimetype, sizeof(mimetype), "%s/%s", type->super, type->type);

	MimeTypes[i] = _cupsStrAlloc(mimetype);
      }

      if (i < NumMimeTypes)
	MimeTypes[i] = _cupsStrAlloc("application/octet-stream");
    }

    if (LogLevel == CUPSD_LOG_DEBUG2)
    {
      mime_filter_t	*filter;	/* Current filter */


      for (type = mimeFirstType(MimeDatabase);
           type;
	   type = mimeNextType(MimeDatabase))
	cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdReadConfiguration: type %s/%s",
		        type->super, type->type);

      for (filter = mimeFirstFilter(MimeDatabase);
           filter;
	   filter = mimeNextFilter(MimeDatabase))
	cupsdLogMessage(CUPSD_LOG_DEBUG2,
	                "cupsdReadConfiguration: filter %s/%s to %s/%s %d %s",
		        filter->src->super, filter->src->type,
		        filter->dst->super, filter->dst->type,
		        filter->cost, filter->filter);
    }

   /*
    * Load banners...
    */

    snprintf(temp, sizeof(temp), "%s/banners", DataDir);
    cupsdLoadBanners(temp);

   /*
    * Load printers and classes...
    */

    cupsdLoadAllPrinters();
    cupsdLoadAllClasses();

    cupsdCreateCommonData();

   /*
    * Update the printcap file as needed...
    */

    if (Printcap && *Printcap && access(Printcap, 0))
      cupsdWritePrintcap();

   /*
    * Load queued jobs...
    */

    cupsdLoadAllJobs();

   /*
    * Load subscriptions...
    */

    cupsdLoadAllSubscriptions();

    cupsdLogMessage(CUPSD_LOG_INFO, "Full reload complete.");
  }
  else
  {
   /*
    * Not a full reload, so recreate the common printer attributes...
    */

    cupsdCreateCommonData();

   /*
    * Update all jobs as needed...
    */

    cupsdUpdateJobs();

   /*
    * Update all printers as needed...
    */

    cupsdUpdatePrinters();
    cupsdMarkDirty(CUPSD_DIRTY_PRINTCAP);

    cupsdLogMessage(CUPSD_LOG_INFO, "Partial reload complete.");
  }

 /*
  * Reset the reload state...
  */

  NeedReload = RELOAD_NONE;

  cupsdClearString(&old_serverroot);
  cupsdClearString(&old_requestroot);

  return (1);
}


/*
 * 'get_address()' - Get an address + port number from a line.
 */

static http_addrlist_t *		/* O - Pointer to list if address good, NULL if bad */
get_address(const char  *value,		/* I - Value string */
	    int         defport)	/* I - Default port */
{
  char			buffer[1024],	/* Hostname + port number buffer */
			defpname[255],	/* Default port name */
			*hostname,	/* Hostname or IP */
			*portname;	/* Port number or name */
  http_addrlist_t	*addrlist;	/* Address list */


 /*
  * Check for an empty value...
  */

  if (!*value)
  {
    cupsdLogMessage(CUPSD_LOG_ERROR, "Bad (empty) address.");
    return (NULL);
  }

 /*
  * Grab a hostname and port number; if there is no colon and the port name
  * is only digits, then we have a port number by itself...
  */

  strlcpy(buffer, value, sizeof(buffer));

  if ((portname = strrchr(buffer, ':')) != NULL && !strchr(portname, ']'))
  {
    *portname++ = '\0';
    hostname = buffer;
  }
  else
  {
    for (portname = buffer; isdigit(*portname & 255); portname ++);

    if (*portname)
    {
     /*
      * Use the default port...
      */

      snprintf(defpname, sizeof(defpname), "%d", defport);
      portname = defpname;
      hostname = buffer;
    }
    else
    {
     /*
      * The buffer contains just a port number...
      */

      portname = buffer;
      hostname = NULL;
    }
  }

  if (hostname && !strcmp(hostname, "*"))
    hostname = NULL;

 /*
  * Now lookup the address using httpAddrGetList()...
  */

  if ((addrlist = httpAddrGetList(hostname, AF_UNSPEC, portname)) == NULL)
    cupsdLogMessage(CUPSD_LOG_ERROR, "Hostname lookup for \"%s\" failed.",
                    hostname ? hostname : "(nil)");

  return (addrlist);
}


/*
 * 'get_addr_and_mask()' - Get an IP address and netmask.
 */

static int				/* O - 1 on success, 0 on failure */
get_addr_and_mask(const char *value,	/* I - String from config file */
                  unsigned   *ip,	/* O - Address value */
		  unsigned   *mask)	/* O - Mask value */
{
  int		i, j,			/* Looping vars */
		family,			/* Address family */
		ipcount;		/* Count of fields in address */
  unsigned	ipval;			/* Value */
  const char	*maskval,		/* Pointer to start of mask value */
		*ptr,			/* Pointer into value */
		*ptr2;			/* ... */


 /*
  * Get the address...
  */

  ip[0]   = ip[1]   = ip[2]   = ip[3]   = 0x00000000;
  mask[0] = mask[1] = mask[2] = mask[3] = 0xffffffff;

  if ((maskval = strchr(value, '/')) != NULL)
    maskval ++;
  else
    maskval = value + strlen(value);

#ifdef AF_INET6
 /*
  * Check for an IPv6 address...
  */

  if (*value == '[')
  {
   /*
    * Parse hexadecimal IPv6/IPv4 address...
    */

    family  = AF_INET6;

    for (i = 0, ptr = value + 1; *ptr && i < 8; i ++)
    {
      if (*ptr == ']')
        break;
      else if (!strncmp(ptr, "::", 2))
      {
        for (ptr2 = strchr(ptr + 2, ':'), j = 0;
	     ptr2;
	     ptr2 = strchr(ptr2 + 1, ':'), j ++);

        i = 6 - j;
	ptr += 2;
      }
      else if (isdigit(*ptr & 255) && strchr(ptr + 1, '.') && i >= 6)
      {
       /*
        * Read IPv4 dotted quad...
        */

	unsigned val[4] = { 0, 0, 0, 0 };
					/* IPv4 address values */

	ipcount = sscanf(ptr, "%u.%u.%u.%u", val + 0, val + 1, val + 2,
	                 val + 3);

       /*
	* Range check the IP numbers...
	*/

	for (i = 0; i < ipcount; i ++)
	  if (val[i] > 255)
	    return (0);

       /*
	* Merge everything into a 32-bit IPv4 address in ip[3]...
	*/

	ip[3] = (val[0] << 24) | (val[1] << 16) | (val[2] << 8) | val[3];

	if (ipcount < 4)
	  mask[3] = (0xffffffff << (32 - 8 * ipcount)) & 0xffffffff;

       /*
        * If the leading words are all 0's then this is an IPv4 address...
        */

        if (!val[0] && !val[1] && !val[2])
	  family  = AF_INET;

        while (isdigit(*ptr & 255) || *ptr == '.')
          ptr ++;
	break;
      }
      else if (isxdigit(*ptr & 255))
      {
        ipval = strtoul(ptr, (char **)&ptr, 16);

	if (*ptr == ':' && ptr[1] != ':')
	  ptr ++;

	if (ipval > 0xffff)
	  return (0);

        if (i & 1)
          ip[i / 2] |= ipval;
	else
          ip[i / 2] |= ipval << 16;
      }
      else
        return (0);
    }

    if (*ptr != ']')
      return (0);

    ptr ++;

    if (*ptr && *ptr != '/')
      return (0);
  }
  else
#endif /* AF_INET6 */
  {
   /*
    * Parse dotted-decimal IPv4 address...
    */

    unsigned val[4] = { 0, 0, 0, 0 };	/* IPv4 address values */


    family  = AF_INET;
    ipcount = sscanf(value, "%u.%u.%u.%u", val + 0, val + 1, val + 2, val + 3);

   /*
    * Range check the IP numbers...
    */

    for (i = 0; i < ipcount; i ++)
      if (val[i] > 255)
        return (0);

   /*
    * Merge everything into a 32-bit IPv4 address in ip[3]...
    */

    ip[3] = (val[0] << 24) | (val[1] << 16) | (val[2] << 8) | val[3];

    if (ipcount < 4)
      mask[3] = (0xffffffff << (32 - 8 * ipcount)) & 0xffffffff;
  }

  if (*maskval)
  {
   /*
    * Get the netmask value(s)...
    */

    memset(mask, 0, sizeof(unsigned) * 4);

    if (strchr(maskval, '.'))
    {
     /*
      * Get dotted-decimal mask...
      */

      if (family != AF_INET)
        return (0);

      if (sscanf(maskval, "%u.%u.%u.%u", mask + 0, mask + 1, mask + 2,
                 mask + 3) != 4)
        return (0);

      mask[3] |= (mask[0] << 24) | (mask[1] << 16) | (mask[2] << 8);
      mask[0] = mask[1] = mask[2] = 0;
    }
    else
    {
     /*
      * Get address/bits format...
      */

      i = atoi(maskval);

#ifdef AF_INET6
      if (family == AF_INET6)
      {
        if (i > 128)
	  return (0);

        i = 128 - i;

	if (i <= 96)
	  mask[0] = 0xffffffff;
	else
	  mask[0] = (0xffffffff << (i - 96)) & 0xffffffff;

	if (i <= 64)
	  mask[1] = 0xffffffff;
	else if (i >= 96)
	  mask[1] = 0;
	else
	  mask[1] = (0xffffffff << (i - 64)) & 0xffffffff;

	if (i <= 32)
	  mask[2] = 0xffffffff;
	else if (i >= 64)
	  mask[2] = 0;
	else
	  mask[2] = (0xffffffff << (i - 32)) & 0xffffffff;

	if (i == 0)
	  mask[3] = 0xffffffff;
	else if (i >= 32)
	  mask[3] = 0;
	else
	  mask[3] = (0xffffffff << i) & 0xffffffff;
      }
      else
#endif /* AF_INET6 */
      {
        if (i > 32)
	  return (0);

        mask[0] = 0xffffffff;
        mask[1] = 0xffffffff;
        mask[2] = 0xffffffff;

	if (i < 32)
          mask[3] = (0xffffffff << (32 - i)) & 0xffffffff;
	else
	  mask[3] = 0xffffffff;
      }
    }
  }

  cupsdLogMessage(CUPSD_LOG_DEBUG2,
                  "get_addr_and_mask(value=\"%s\", "
                  "ip=[%08x:%08x:%08x:%08x], mask=[%08x:%08x:%08x:%08x])",
             value, ip[0], ip[1], ip[2], ip[3], mask[0], mask[1], mask[2],
	     mask[3]);

 /*
  * Check for a valid netmask; no fallback like in CUPS 1.1.x!
  */

  if ((ip[0] & ~mask[0]) != 0 ||
      (ip[1] & ~mask[1]) != 0 ||
      (ip[2] & ~mask[2]) != 0 ||
      (ip[3] & ~mask[3]) != 0)
    return (0);

  return (1);
}


/*
 * 'mime_error_cb()' - Log a MIME error.
 */

static void
mime_error_cb(void       *ctx,		/* I - Context pointer (unused) */
              const char *message)	/* I - Message */
{
  (void)ctx;

  cupsdLogMessage(CUPSD_LOG_ERROR, "%s", message);
}


/*
 * 'parse_aaa()' - Parse authentication, authorization, and access control lines.
 */

static int				/* O - 1 on success, 0 on failure */
parse_aaa(cupsd_location_t *loc,	/* I - Location */
          char             *line,	/* I - Line from file */
	  char             *value,	/* I - Start of value data */
	  int              linenum)	/* I - Current line number */
{
  char		*valptr;		/* Pointer into value */
  unsigned	ip[4],			/* IP address components */
 		mask[4];		/* IP netmask components */


  if (!_cups_strcasecmp(line, "Encryption"))
  {
   /*
    * "Encryption xxx" - set required encryption level...
    */

    if (!_cups_strcasecmp(value, "never"))
      loc->encryption = HTTP_ENCRYPT_NEVER;
    else if (!_cups_strcasecmp(value, "always"))
    {
      cupsdLogMessage(CUPSD_LOG_ERROR,
                      "Encryption value \"%s\" on line %d of %s is invalid in this "
		      "context. Using \"required\" instead.", value, linenum, ConfigurationFile);

      loc->encryption = HTTP_ENCRYPT_REQUIRED;
    }
    else if (!_cups_strcasecmp(value, "required"))
      loc->encryption = HTTP_ENCRYPT_REQUIRED;
    else if (!_cups_strcasecmp(value, "ifrequested"))
      loc->encryption = HTTP_ENCRYPT_IF_REQUESTED;
    else
    {
      cupsdLogMessage(CUPSD_LOG_ERROR,
                      "Unknown Encryption value %s on line %d of %s.", value, linenum, ConfigurationFile);
      return (0);
    }
  }
  else if (!_cups_strcasecmp(line, "Order"))
  {
   /*
    * "Order Deny,Allow" or "Order Allow,Deny"...
    */

    if (!_cups_strncasecmp(value, "deny", 4))
      loc->order_type = CUPSD_AUTH_ALLOW;
    else if (!_cups_strncasecmp(value, "allow", 5))
      loc->order_type = CUPSD_AUTH_DENY;
    else
    {
      cupsdLogMessage(CUPSD_LOG_ERROR, "Unknown Order value %s on line %d of %s.",
	              value, linenum, ConfigurationFile);
      return (0);
    }
  }
  else if (!_cups_strcasecmp(line, "Allow") || !_cups_strcasecmp(line, "Deny"))
  {
   /*
    * Allow [From] host/ip...
    * Deny [From] host/ip...
    */

    while (*value)
    {
      if (!_cups_strncasecmp(value, "from", 4))
      {
       /*
	* Strip leading "from"...
	*/

	value += 4;

	while (_cups_isspace(*value))
	  value ++;

        if (!*value)
	  break;
      }

     /*
      * Find the end of the value...
      */

      for (valptr = value; *valptr && !_cups_isspace(*valptr); valptr ++);

      while (_cups_isspace(*valptr))
        *valptr++ = '\0';

     /*
      * Figure out what form the allow/deny address takes:
      *
      *    All
      *    None
      *    *.domain.com
      *    .domain.com
      *    host.domain.com
      *    nnn.*
      *    nnn.nnn.*
      *    nnn.nnn.nnn.*
      *    nnn.nnn.nnn.nnn
      *    nnn.nnn.nnn.nnn/mm
      *    nnn.nnn.nnn.nnn/mmm.mmm.mmm.mmm
      */

      if (!_cups_strcasecmp(value, "all"))
      {
       /*
	* All hosts...
	*/

	if (!_cups_strcasecmp(line, "Allow"))
	  cupsdAddIPMask(&(loc->allow), zeros, zeros);
	else
	  cupsdAddIPMask(&(loc->deny), zeros, zeros);
      }
      else if (!_cups_strcasecmp(value, "none"))
      {
       /*
	* No hosts...
	*/

	if (!_cups_strcasecmp(line, "Allow"))
	  cupsdAddIPMask(&(loc->allow), ones, zeros);
	else
	  cupsdAddIPMask(&(loc->deny), ones, zeros);
      }
#ifdef AF_INET6
      else if (value[0] == '*' || value[0] == '.' ||
	       (!isdigit(value[0] & 255) && value[0] != '['))
#else
      else if (value[0] == '*' || value[0] == '.' || !isdigit(value[0] & 255))
#endif /* AF_INET6 */
      {
       /*
	* Host or domain name...
	*/

	if (value[0] == '*')
	  value ++;

	if (!_cups_strcasecmp(line, "Allow"))
	  cupsdAddNameMask(&(loc->allow), value);
	else
	  cupsdAddNameMask(&(loc->deny), value);
      }
      else
      {
       /*
	* One of many IP address forms...
	*/

	if (!get_addr_and_mask(value, ip, mask))
	{
	  cupsdLogMessage(CUPSD_LOG_ERROR, "Bad netmask value %s on line %d of %s.",
			  value, linenum, ConfigurationFile);
	  return (0);
	}

	if (!_cups_strcasecmp(line, "Allow"))
	  cupsdAddIPMask(&(loc->allow), ip, mask);
	else
	  cupsdAddIPMask(&(loc->deny), ip, mask);
      }

     /*
      * Advance to next value...
      */

      value = valptr;
    }
  }
  else if (!_cups_strcasecmp(line, "AuthType"))
  {
   /*
    * AuthType {none,basic,digest,basicdigest,negotiate,default}
    */

    if (!_cups_strcasecmp(value, "none"))
    {
      loc->type  = CUPSD_AUTH_NONE;
      loc->level = CUPSD_AUTH_ANON;
    }
    else if (!_cups_strcasecmp(value, "basic"))
    {
      loc->type = CUPSD_AUTH_BASIC;

      if (loc->level == CUPSD_AUTH_ANON)
	loc->level = CUPSD_AUTH_USER;
    }
    else if (!_cups_strcasecmp(value, "default"))
    {
      loc->type = CUPSD_AUTH_DEFAULT;

      if (loc->level == CUPSD_AUTH_ANON)
	loc->level = CUPSD_AUTH_USER;
    }
    else if (!_cups_strcasecmp(value, "negotiate"))
    {
      loc->type = CUPSD_AUTH_NEGOTIATE;

      if (loc->level == CUPSD_AUTH_ANON)
	loc->level = CUPSD_AUTH_USER;
    }
    else
    {
      cupsdLogMessage(CUPSD_LOG_WARN,
                      "Unknown authorization type %s on line %d of %s.",
	              value, linenum, ConfigurationFile);
      return (0);
    }
  }
  else if (!_cups_strcasecmp(line, "AuthClass"))
  {
   /*
    * AuthClass anonymous, user, system, group
    */

    if (!_cups_strcasecmp(value, "anonymous"))
    {
      loc->type  = CUPSD_AUTH_NONE;
      loc->level = CUPSD_AUTH_ANON;

      cupsdLogMessage(CUPSD_LOG_WARN,
                      "\"AuthClass %s\" is deprecated; consider removing "
		      "it from line %d.",
	              value, linenum);
    }
    else if (!_cups_strcasecmp(value, "user"))
    {
      loc->level = CUPSD_AUTH_USER;

      cupsdLogMessage(CUPSD_LOG_WARN,
                      "\"AuthClass %s\" is deprecated; consider using "
		      "\"Require valid-user\" on line %d of %s.",
	              value, linenum, ConfigurationFile);
    }
    else if (!_cups_strcasecmp(value, "group"))
    {
      loc->level = CUPSD_AUTH_GROUP;

      cupsdLogMessage(CUPSD_LOG_WARN,
                      "\"AuthClass %s\" is deprecated; consider using "
		      "\"Require user @groupname\" on line %d of %s.",
	              value, linenum, ConfigurationFile);
    }
    else if (!_cups_strcasecmp(value, "system"))
    {
      loc->level = CUPSD_AUTH_GROUP;

      cupsdAddName(loc, "@SYSTEM");

      cupsdLogMessage(CUPSD_LOG_WARN,
                      "\"AuthClass %s\" is deprecated; consider using "
		      "\"Require user @SYSTEM\" on line %d of %s.",
	              value, linenum, ConfigurationFile);
    }
    else
    {
      cupsdLogMessage(CUPSD_LOG_WARN,
                      "Unknown authorization class %s on line %d of %s.",
	              value, linenum, ConfigurationFile);
      return (0);
    }
  }
  else if (!_cups_strcasecmp(line, "AuthGroupName"))
  {
    cupsdAddName(loc, value);

    cupsdLogMessage(CUPSD_LOG_WARN,
                    "\"AuthGroupName %s\" directive is deprecated; consider "
		    "using \"Require user @%s\" on line %d of %s.",
		    value, value, linenum, ConfigurationFile);
  }
  else if (!_cups_strcasecmp(line, "Require"))
  {
   /*
    * Apache synonym for AuthClass and AuthGroupName...
    *
    * Get initial word:
    *
    *     Require valid-user
    *     Require group names
    *     Require user names
    */

    for (valptr = value; !_cups_isspace(*valptr) && *valptr; valptr ++);

    if (*valptr)
      *valptr++ = '\0';

    if (!_cups_strcasecmp(value, "valid-user") ||
        !_cups_strcasecmp(value, "user"))
      loc->level = CUPSD_AUTH_USER;
    else if (!_cups_strcasecmp(value, "group"))
      loc->level = CUPSD_AUTH_GROUP;
    else
    {
      cupsdLogMessage(CUPSD_LOG_WARN, "Unknown Require type %s on line %d of %s.",
	              value, linenum, ConfigurationFile);
      return (0);
    }

   /*
    * Get the list of names from the line...
    */

    for (value = valptr; *value;)
    {
      while (_cups_isspace(*value))
	value ++;

#ifdef HAVE_AUTHORIZATION_H
      if (!strncmp(value, "@AUTHKEY(", 9))
      {
       /*
	* Grab "@AUTHKEY(name)" value...
	*/

        for (valptr = value + 9; *valptr != ')' && *valptr; valptr ++);

	if (*valptr)
	  *valptr++ = '\0';
      }
      else
#endif /* HAVE_AUTHORIZATION_H */
      if (*value == '\"' || *value == '\'')
      {
       /*
	* Grab quoted name...
	*/

        for (valptr = value + 1; *valptr != *value && *valptr; valptr ++);

	value ++;
      }
      else
      {
       /*
	* Grab literal name.
	*/

        for (valptr = value; !_cups_isspace(*valptr) && *valptr; valptr ++);
      }

      if (*valptr)
	*valptr++ = '\0';

      cupsdAddName(loc, value);

      for (value = valptr; _cups_isspace(*value); value ++);
    }
  }
  else if (!_cups_strcasecmp(line, "Satisfy"))
  {
    if (!_cups_strcasecmp(value, "all"))
      loc->satisfy = CUPSD_AUTH_SATISFY_ALL;
    else if (!_cups_strcasecmp(value, "any"))
      loc->satisfy = CUPSD_AUTH_SATISFY_ANY;
    else
    {
      cupsdLogMessage(CUPSD_LOG_WARN, "Unknown Satisfy value %s on line %d of %s.",
                      value, linenum, ConfigurationFile);
      return (0);
    }
  }
  else
    return (0);

  return (1);
}


/*
 * 'parse_fatal_errors()' - Parse FatalErrors values in a string.
 */

static int				/* O - FatalErrors bits */
parse_fatal_errors(const char *s)	/* I - FatalErrors string */
{
  int	fatal;				/* FatalErrors bits */
  char	value[1024],			/* Value string */
	*valstart,			/* Pointer into value */
	*valend;			/* End of value */


 /*
  * Empty FatalErrors line yields NULL pointer...
  */

  if (!s)
    return (CUPSD_FATAL_NONE);

 /*
  * Loop through the value string,...
  */

  strlcpy(value, s, sizeof(value));

  fatal = CUPSD_FATAL_NONE;

  for (valstart = value; *valstart;)
  {
   /*
    * Get the current space/comma-delimited kind name...
    */

    for (valend = valstart; *valend; valend ++)
      if (_cups_isspace(*valend) || *valend == ',')
	break;

    if (*valend)
      *valend++ = '\0';

   /*
    * Add the error to the bitmask...
    */

    if (!_cups_strcasecmp(valstart, "all"))
      fatal = CUPSD_FATAL_ALL;
    else if (!_cups_strcasecmp(valstart, "browse"))
      fatal |= CUPSD_FATAL_BROWSE;
    else if (!_cups_strcasecmp(valstart, "-browse"))
      fatal &= ~CUPSD_FATAL_BROWSE;
    else if (!_cups_strcasecmp(valstart, "config"))
      fatal |= CUPSD_FATAL_CONFIG;
    else if (!_cups_strcasecmp(valstart, "-config"))
      fatal &= ~CUPSD_FATAL_CONFIG;
    else if (!_cups_strcasecmp(valstart, "listen"))
      fatal |= CUPSD_FATAL_LISTEN;
    else if (!_cups_strcasecmp(valstart, "-listen"))
      fatal &= ~CUPSD_FATAL_LISTEN;
    else if (!_cups_strcasecmp(valstart, "log"))
      fatal |= CUPSD_FATAL_LOG;
    else if (!_cups_strcasecmp(valstart, "-log"))
      fatal &= ~CUPSD_FATAL_LOG;
    else if (!_cups_strcasecmp(valstart, "permissions"))
      fatal |= CUPSD_FATAL_PERMISSIONS;
    else if (!_cups_strcasecmp(valstart, "-permissions"))
      fatal &= ~CUPSD_FATAL_PERMISSIONS;
    else if (_cups_strcasecmp(valstart, "none"))
      cupsdLogMessage(CUPSD_LOG_ERROR,
                      "Unknown FatalErrors kind \"%s\" ignored.", valstart);

    for (valstart = valend; *valstart; valstart ++)
      if (!_cups_isspace(*valstart) || *valstart != ',')
	break;
  }

  return (fatal);
}


/*
 * 'parse_groups()' - Parse system group names in a string.
 */

static int				/* O - 1 on success, 0 on failure */
parse_groups(const char *s,		/* I - Space-delimited groups */
             int        linenum)        /* I - Line number in cups-files.conf */
{
  int		status;			/* Return status */
  char		value[1024],		/* Value string */
		*valstart,		/* Pointer into value */
		*valend,		/* End of value */
		quote;			/* Quote character */
  struct group	*group;			/* Group */


 /*
  * Make a copy of the string and parse out the groups...
  */

  strlcpy(value, s, sizeof(value));

  status   = 1;
  valstart = value;

  while (*valstart && NumSystemGroups < MAX_SYSTEM_GROUPS)
  {
    if (*valstart == '\'' || *valstart == '\"')
    {
     /*
      * Scan quoted name...
      */

      quote = *valstart++;

      for (valend = valstart; *valend; valend ++)
	if (*valend == quote)
	  break;
    }
    else
    {
     /*
      * Scan space or comma-delimited name...
      */

      for (valend = valstart; *valend; valend ++)
	if (_cups_isspace(*valend) || *valend == ',')
	  break;
    }

    if (*valend)
      *valend++ = '\0';

    group = getgrnam(valstart);
    if (group)
    {
      cupsdSetString(SystemGroups + NumSystemGroups, valstart);
      SystemGroupIDs[NumSystemGroups] = group->gr_gid;

      NumSystemGroups ++;
    }
    else
    {
      if (linenum)
        cupsdLogMessage(CUPSD_LOG_ERROR, "Unknown SystemGroup \"%s\" on line %d of %s.", valstart, linenum, CupsFilesFile);
      else
        cupsdLogMessage(CUPSD_LOG_ERROR, "Unknown default SystemGroup \"%s\".", valstart);

      status = 0;
    }

    endgrent();

    valstart = valend;

    while (*valstart == ',' || _cups_isspace(*valstart))
      valstart ++;
  }

  return (status);
}


/*
 * 'parse_protocols()' - Parse browse protocols in a string.
 */

static int				/* O - Browse protocol bits */
parse_protocols(const char *s)		/* I - Space-delimited protocols */
{
  int	protocols;			/* Browse protocol bits */
  char	value[1024],			/* Value string */
	*valstart,			/* Pointer into value */
	*valend;			/* End of value */


 /*
  * Empty protocol line yields NULL pointer...
  */

  if (!s)
    return (0);

 /*
  * Loop through the value string,...
  */

  strlcpy(value, s, sizeof(value));

  protocols = 0;

  for (valstart = value; *valstart;)
  {
   /*
    * Get the current space/comma-delimited protocol name...
    */

    for (valend = valstart; *valend; valend ++)
      if (_cups_isspace(*valend) || *valend == ',')
	break;

    if (*valend)
      *valend++ = '\0';

   /*
    * Add the protocol to the bitmask...
    */

    if (!_cups_strcasecmp(valstart, "dnssd") ||
	!_cups_strcasecmp(valstart, "dns-sd") ||
	!_cups_strcasecmp(valstart, "bonjour"))
      protocols |= BROWSE_DNSSD;
    else if (!_cups_strcasecmp(valstart, "all"))
      protocols |= BROWSE_ALL;
    else if (_cups_strcasecmp(valstart, "none"))
      cupsdLogMessage(CUPSD_LOG_ERROR,
                      "Unknown browse protocol \"%s\" ignored.", valstart);

    for (valstart = valend; *valstart; valstart ++)
      if (!_cups_isspace(*valstart) || *valstart != ',')
	break;
  }

  return (protocols);
}


/*
 * 'parse_variable()' - Parse a variable line.
 */

static int				/* O - 1 on success, 0 on failure */
parse_variable(
    const char        *filename,	/* I - Name of configuration file */
    int               linenum,		/* I - Line in configuration file */
    const char        *line,		/* I - Line from configuration file */
    const char        *value,		/* I - Value from configuration file */
    size_t            num_vars,		/* I - Number of variables */
    const cupsd_var_t *vars)		/* I - Variables */
{
  size_t		i;		/* Looping var */
  const cupsd_var_t	*var;		/* Variables */
  char			temp[1024];	/* Temporary string */


  for (i = num_vars, var = vars; i; i --, var ++)
    if (!_cups_strcasecmp(line, var->name))
      break;

  if (i == 0)
  {
   /*
    * Unknown directive!  Output an error message and continue...
    */

    if (!value)
      cupsdLogMessage(CUPSD_LOG_ERROR, "Missing value for %s on line %d of %s.",
		      line, linenum, filename);
    else
      cupsdLogMessage(CUPSD_LOG_ERROR, "Unknown directive %s on line %d of %s.",
		      line, linenum, filename);

    return (0);
  }

  switch (var->type)
  {
    case CUPSD_VARTYPE_INTEGER :
	if (!value)
	{
	  cupsdLogMessage(CUPSD_LOG_ERROR,
			  "Missing integer value for %s on line %d of %s.",
			  line, linenum, filename);
          return (0);
	}
	else if (!isdigit(*value & 255))
	{
	  cupsdLogMessage(CUPSD_LOG_ERROR,
			  "Bad integer value for %s on line %d of %s.",
			  line, linenum, filename);
          return (0);
	}
	else
	{
	  int	n;		/* Number */
	  char	*units;		/* Units */

	  n = (int)strtol(value, &units, 0);

	  if (units && *units)
	  {
	    if (tolower(units[0] & 255) == 'g')
	      n *= 1024 * 1024 * 1024;
	    else if (tolower(units[0] & 255) == 'm')
	      n *= 1024 * 1024;
	    else if (tolower(units[0] & 255) == 'k')
	      n *= 1024;
	    else if (tolower(units[0] & 255) == 't')
	      n *= 262144;
	    else
	    {
	      cupsdLogMessage(CUPSD_LOG_ERROR,
			      "Unknown integer value for %s on line %d of %s.",
			      line, linenum, filename);
	      return (0);
	    }
	  }

	  if (n < 0)
	  {
	    cupsdLogMessage(CUPSD_LOG_ERROR,
			    "Bad negative integer value for %s on line %d of "
			    "%s.", line, linenum, filename);
	    return (0);
	  }
	  else
	  {
	    *((int *)var->ptr) = n;
	  }
	}
	break;

    case CUPSD_VARTYPE_PERM :
	if (!value)
	{
	  cupsdLogMessage(CUPSD_LOG_ERROR,
			  "Missing permissions value for %s on line %d of %s.",
			  line, linenum, filename);
          return (0);
	}
	else if (!isdigit(*value & 255))
	{
	 /* TODO: Add chmod UGO syntax support */
	  cupsdLogMessage(CUPSD_LOG_ERROR,
			  "Bad permissions value for %s on line %d of %s.",
			  line, linenum, filename);
          return (0);
	}
	else
	{
	  int n = (int)strtol(value, NULL, 8);
					/* Permissions value */

	  if (n < 0)
	  {
	    cupsdLogMessage(CUPSD_LOG_ERROR,
			    "Bad negative permissions value for %s on line %d of "
			    "%s.", line, linenum, filename);
	    return (0);
	  }
	  else
	  {
	    *((mode_t *)var->ptr) = (mode_t)n;
	  }
	}
	break;

    case CUPSD_VARTYPE_TIME :
	if (!value)
	{
	  cupsdLogMessage(CUPSD_LOG_ERROR,
			  "Missing time interval value for %s on line %d of "
			  "%s.", line, linenum, filename);
	  return (0);
	}
	else if (!_cups_strncasecmp(line, "PreserveJob", 11) &&
		 (!_cups_strcasecmp(value, "true") ||
		  !_cups_strcasecmp(value, "on") ||
		  !_cups_strcasecmp(value, "enabled") ||
		  !_cups_strcasecmp(value, "yes")))
	{
	  *((int *)var->ptr) = INT_MAX;
	}
	else if (!_cups_strcasecmp(value, "false") ||
		 !_cups_strcasecmp(value, "off") ||
		 !_cups_strcasecmp(value, "disabled") ||
		 !_cups_strcasecmp(value, "no"))
	{
	  *((int *)var->ptr) = 0;
	}
	else if (!isdigit(*value & 255))
	{
	  cupsdLogMessage(CUPSD_LOG_ERROR,
			  "Unknown time interval value for %s on line %d of "
			  "%s.", line, linenum, filename);
          return (0);
	}
	else
	{
	  double	n;		/* Number */
	  char		*units;		/* Units */

	  n = strtod(value, &units);

	  if (units && *units)
	  {
	    if (tolower(units[0] & 255) == 'w')
	      n *= 7 * 24 * 60 * 60;
	    else if (tolower(units[0] & 255) == 'd')
	      n *= 24 * 60 * 60;
	    else if (tolower(units[0] & 255) == 'h')
	      n *= 60 * 60;
	    else if (tolower(units[0] & 255) == 'm')
	      n *= 60;
	    else
	    {
	      cupsdLogMessage(CUPSD_LOG_ERROR,
			      "Unknown time interval value for %s on line "
			      "%d of %s.", line, linenum, filename);
	      return (0);
	    }
	  }

	  if (n < 0.0 || n > INT_MAX)
	  {
	    cupsdLogMessage(CUPSD_LOG_ERROR,
			    "Bad time value for %s on line %d of %s.",
			    line, linenum, filename);
	    return (0);
	  }
	  else
	  {
	    *((int *)var->ptr) = (int)n;
	  }
	}
	break;

    case CUPSD_VARTYPE_BOOLEAN :
	if (!value)
	{
	  cupsdLogMessage(CUPSD_LOG_ERROR,
			  "Missing boolean value for %s on line %d of %s.",
			  line, linenum, filename);
	  return (0);
	}
	else if (!_cups_strcasecmp(value, "true") ||
		 !_cups_strcasecmp(value, "on") ||
		 !_cups_strcasecmp(value, "enabled") ||
		 !_cups_strcasecmp(value, "yes") ||
		 atoi(value) != 0)
	{
	  *((int *)var->ptr) = TRUE;
	}
	else if (!_cups_strcasecmp(value, "false") ||
		 !_cups_strcasecmp(value, "off") ||
		 !_cups_strcasecmp(value, "disabled") ||
		 !_cups_strcasecmp(value, "no") ||
		 !_cups_strcasecmp(value, "0"))
	{
	  *((int *)var->ptr) = FALSE;
	}
	else
	{
	  cupsdLogMessage(CUPSD_LOG_ERROR,
			  "Unknown boolean value %s on line %d of %s.",
			  value, linenum, filename);
	  return (0);
	}
	break;

    case CUPSD_VARTYPE_PATHNAME :
	if (!value)
	{
	  cupsdLogMessage(CUPSD_LOG_ERROR,
			  "Missing pathname value for %s on line %d of %s.",
			  line, linenum, filename);
	  return (0);
	}

	if (value[0] == '/')
	  strlcpy(temp, value, sizeof(temp));
	else
	  snprintf(temp, sizeof(temp), "%s/%s", ServerRoot, value);

	if (access(temp, 0) && _cups_strcasecmp(value, "internal") && _cups_strcasecmp(line, "ServerKeychain"))
	{
	  cupsdLogMessage(CUPSD_LOG_ERROR,
			  "File or directory for \"%s %s\" on line %d of %s "
			  "does not exist.", line, value, linenum, filename);
	  return (0);
	}

	cupsdSetString((char **)var->ptr, temp);
	break;

    case CUPSD_VARTYPE_STRING :
	cupsdSetString((char **)var->ptr, value);
	break;
  }

  return (1);
}


/*
 * 'read_cupsd_conf()' - Read the cupsd.conf configuration file.
 */

static int				/* O - 1 on success, 0 on failure */
read_cupsd_conf(cups_file_t *fp)	/* I - File to read from */
{
  int			linenum;	/* Current line number */
  char			line[HTTP_MAX_BUFFER],
					/* Line from file */
			temp[HTTP_MAX_BUFFER],
					/* Temporary buffer for value */
			*value;		/* Pointer to value */
  http_addrlist_t	*addrlist,	/* Address list */
			*addr;		/* Current address */


 /*
  * Loop through each line in the file...
  */

  linenum = 0;

  while (cupsFileGetConf(fp, line, sizeof(line), &value, &linenum))
  {
   /*
    * Decode the directive...
    */

    if (!_cups_strcasecmp(line, "<Location") && value)
    {
     /*
      * <Location path>
      */

      linenum = read_location(fp, value, linenum);
      if (linenum == 0)
	return (0);
    }
    else if (!_cups_strcasecmp(line, "<Policy") && value)
    {
     /*
      * <Policy name>
      */

      linenum = read_policy(fp, value, linenum);
      if (linenum == 0)
	return (0);
    }
    else if (!_cups_strcasecmp(line, "FaxRetryInterval") && value)
    {
      JobRetryInterval = atoi(value);
      cupsdLogMessage(CUPSD_LOG_WARN,
		      "FaxRetryInterval is deprecated; use "
		      "JobRetryInterval on line %d of %s.", linenum, ConfigurationFile);
    }
    else if (!_cups_strcasecmp(line, "FaxRetryLimit") && value)
    {
      JobRetryLimit = atoi(value);
      cupsdLogMessage(CUPSD_LOG_WARN,
		      "FaxRetryLimit is deprecated; use "
		      "JobRetryLimit on line %d of %s.", linenum, ConfigurationFile);
    }
#ifdef HAVE_TLS
    else if (!_cups_strcasecmp(line, "SSLOptions"))
    {
     /*
      * SSLOptions [AllowRC4] [AllowSSL3] [AllowDH] [DenyCBC] [DenyTLS1.0] [None]
      */

      int	options = _HTTP_TLS_NONE,/* SSL/TLS options */
		min_version = _HTTP_TLS_1_0,
		max_version = _HTTP_TLS_MAX;

      if (value)
      {
        char	*start,			/* Start of option */
		*end;			/* End of option */

	for (start = value; *start; start = end)
	{
	 /*
	  * Find end of keyword...
	  */

	  end = start;
	  while (*end && !_cups_isspace(*end))
	    end ++;

	  if (*end)
	    *end++ = '\0';

         /*
	  * Compare...
	  */

	  if (!_cups_strcasecmp(start, "AllowRC4"))
	    options |= _HTTP_TLS_ALLOW_RC4;
	  else if (!_cups_strcasecmp(start, "AllowSSL3"))
	    min_version = _HTTP_TLS_SSL3;
	  else if (!_cups_strcasecmp(start, "AllowDH"))
	    options |= _HTTP_TLS_ALLOW_DH;
	  else if (!_cups_strcasecmp(start, "DenyCBC"))
	    options |= _HTTP_TLS_DENY_CBC;
	  else if (!_cups_strcasecmp(start, "DenyTLS1.0"))
	    min_version = _HTTP_TLS_1_1;
	  else if (!_cups_strcasecmp(start, "MaxTLS1.0"))
	    max_version = _HTTP_TLS_1_0;
	  else if (!_cups_strcasecmp(start, "MaxTLS1.1"))
	    max_version = _HTTP_TLS_1_1;
	  else if (!_cups_strcasecmp(start, "MaxTLS1.2"))
	    max_version = _HTTP_TLS_1_2;
	  else if (!_cups_strcasecmp(start, "MaxTLS1.3"))
	    max_version = _HTTP_TLS_1_3;
	  else if (!_cups_strcasecmp(start, "MinTLS1.0"))
	    min_version = _HTTP_TLS_1_0;
	  else if (!_cups_strcasecmp(start, "MinTLS1.1"))
	    min_version = _HTTP_TLS_1_1;
	  else if (!_cups_strcasecmp(start, "MinTLS1.2"))
	    min_version = _HTTP_TLS_1_2;
	  else if (!_cups_strcasecmp(start, "MinTLS1.3"))
	    min_version = _HTTP_TLS_1_3;
	  else if (!_cups_strcasecmp(start, "None"))
	    options = _HTTP_TLS_NONE;
	  else if (!_cups_strcasecmp(start, "NoSystem"))
	    options |= _HTTP_TLS_NO_SYSTEM;
	  else if (_cups_strcasecmp(start, "NoEmptyFragments"))
	    cupsdLogMessage(CUPSD_LOG_WARN, "Unknown SSL option %s at line %d.", start, linenum);
        }
      }

      _httpTLSSetOptions(options, min_version, max_version);
    }
#endif /* HAVE_TLS */
    else if ((!_cups_strcasecmp(line, "Port") || !_cups_strcasecmp(line, "Listen")
#ifdef HAVE_TLS
             || !_cups_strcasecmp(line, "SSLPort") || !_cups_strcasecmp(line, "SSLListen")
#endif /* HAVE_TLS */
	     ) && value)
    {
     /*
      * Add listening address(es) to the list...
      */

      cupsd_listener_t	*lis;		/* New listeners array */


     /*
      * If we are launched on-demand, do not use domain sockets from the config
      * file.  Also check that the domain socket path is not too long...
      */

#ifdef HAVE_ONDEMAND
      if (*value == '/' && OnDemand)
      {
        if (strcmp(value, CUPS_DEFAULT_DOMAINSOCKET))
          cupsdLogMessage(CUPSD_LOG_INFO, "Ignoring %s address %s at line %d - only using domain socket from launchd/systemd.", line, value, linenum);
        continue;
      }
#endif // HAVE_ONDEMAND

      if (*value == '/' && strlen(value) > (sizeof(addr->addr.un.sun_path) - 1))
      {
        cupsdLogMessage(CUPSD_LOG_INFO, "Ignoring %s address %s at line %d - too long.", line, value, linenum);
        continue;
      }

     /*
      * Get the address list...
      */

      addrlist = get_address(value, IPP_PORT);

      if (!addrlist)
      {
        cupsdLogMessage(CUPSD_LOG_ERROR, "Bad %s address %s at line %d.", line,
	                value, linenum);
        continue;
      }

     /*
      * Add each address...
      */

      for (addr = addrlist; addr; addr = addr->next)
      {
       /*
        * See if this address is already present...
	*/

        for (lis = (cupsd_listener_t *)cupsArrayFirst(Listeners);
	     lis;
	     lis = (cupsd_listener_t *)cupsArrayNext(Listeners))
          if (httpAddrEqual(&(addr->addr), &(lis->address)) &&
	      httpAddrPort(&(addr->addr)) == httpAddrPort(&(lis->address)))
	    break;

        if (lis)
	{
#ifdef HAVE_ONDEMAND
	  if (!lis->on_demand)
#endif /* HAVE_ONDEMAND */
	  {
	    httpAddrString(&lis->address, temp, sizeof(temp));
	    cupsdLogMessage(CUPSD_LOG_WARN,
			    "Duplicate listen address \"%s\" ignored.", temp);
	  }

          continue;
	}

       /*
        * Allocate another listener...
	*/

        if ((lis = calloc(1, sizeof(cupsd_listener_t))) == NULL)
	{
          cupsdLogMessage(CUPSD_LOG_ERROR,
	                  "Unable to allocate %s at line %d - %s.",
	                  line, linenum, strerror(errno));
          break;
	}

        cupsArrayAdd(Listeners, lis);

       /*
        * Copy the current address and log it...
	*/

	memcpy(&(lis->address), &(addr->addr), sizeof(lis->address));
	lis->fd = -1;

#ifdef HAVE_TLS
        if (!_cups_strcasecmp(line, "SSLPort") || !_cups_strcasecmp(line, "SSLListen"))
          lis->encryption = HTTP_ENCRYPT_ALWAYS;
#endif /* HAVE_TLS */

	httpAddrString(&lis->address, temp, sizeof(temp));

#ifdef AF_LOCAL
        if (lis->address.addr.sa_family == AF_LOCAL)
          cupsdLogMessage(CUPSD_LOG_INFO, "Listening to %s (Domain)", temp);
	else
#endif /* AF_LOCAL */
	cupsdLogMessage(CUPSD_LOG_INFO, "Listening to %s:%d (IPv%d)", temp,
                        httpAddrPort(&(lis->address)),
			httpAddrFamily(&(lis->address)) == AF_INET ? 4 : 6);

        if (!httpAddrLocalhost(&(lis->address)))
	  RemotePort = httpAddrPort(&(lis->address));
      }

     /*
      * Free the list...
      */

      httpAddrFreeList(addrlist);
    }
    else if (!_cups_strcasecmp(line, "BrowseProtocols") ||
             !_cups_strcasecmp(line, "BrowseLocalProtocols"))
    {
     /*
      * "BrowseProtocols name [... name]"
      * "BrowseLocalProtocols name [... name]"
      */

      int protocols = parse_protocols(value);

      if (protocols < 0)
      {
	cupsdLogMessage(CUPSD_LOG_ERROR,
	                "Unknown browse protocol \"%s\" on line %d of %s.",
	                value, linenum, ConfigurationFile);
        break;
      }

      BrowseLocalProtocols = protocols;
    }
    else if (!_cups_strcasecmp(line, "DefaultAuthType") && value)
    {
     /*
      * DefaultAuthType {basic,digest,basicdigest,negotiate}
      */

      if (!_cups_strcasecmp(value, "none"))
	default_auth_type = CUPSD_AUTH_NONE;
      else if (!_cups_strcasecmp(value, "basic"))
	default_auth_type = CUPSD_AUTH_BASIC;
      else if (!_cups_strcasecmp(value, "negotiate"))
        default_auth_type = CUPSD_AUTH_NEGOTIATE;
      else if (!_cups_strcasecmp(value, "auto"))
        default_auth_type = CUPSD_AUTH_AUTO;
      else
      {
	cupsdLogMessage(CUPSD_LOG_WARN,
	                "Unknown default authorization type %s on line %d of %s.",
	                value, linenum, ConfigurationFile);
	if (FatalErrors & CUPSD_FATAL_CONFIG)
	  return (0);
      }
    }
#ifdef HAVE_TLS
    else if (!_cups_strcasecmp(line, "DefaultEncryption"))
    {
     /*
      * DefaultEncryption {Never,IfRequested,Required}
      */

      if (!value || !_cups_strcasecmp(value, "never"))
	DefaultEncryption = HTTP_ENCRYPT_NEVER;
      else if (!_cups_strcasecmp(value, "required"))
	DefaultEncryption = HTTP_ENCRYPT_REQUIRED;
      else if (!_cups_strcasecmp(value, "ifrequested"))
	DefaultEncryption = HTTP_ENCRYPT_IF_REQUESTED;
      else
      {
	cupsdLogMessage(CUPSD_LOG_WARN,
	                "Unknown default encryption %s on line %d of %s.",
	                value, linenum, ConfigurationFile);
	if (FatalErrors & CUPSD_FATAL_CONFIG)
	  return (0);
      }
    }
#endif /* HAVE_TLS */
    else if (!_cups_strcasecmp(line, "HostNameLookups") && value)
    {
     /*
      * Do hostname lookups?
      */

      if (!_cups_strcasecmp(value, "off") || !_cups_strcasecmp(value, "no") ||
          !_cups_strcasecmp(value, "false"))
        HostNameLookups = 0;
      else if (!_cups_strcasecmp(value, "on") || !_cups_strcasecmp(value, "yes") ||
          !_cups_strcasecmp(value, "true"))
        HostNameLookups = 1;
      else if (!_cups_strcasecmp(value, "double"))
        HostNameLookups = 2;
      else
	cupsdLogMessage(CUPSD_LOG_WARN, "Unknown HostNameLookups %s on line %d of %s.",
	                value, linenum, ConfigurationFile);
    }
    else if (!_cups_strcasecmp(line, "AccessLogLevel") && value)
    {
     /*
      * Amount of logging to do to access log...
      */

      if (!_cups_strcasecmp(value, "all"))
        AccessLogLevel = CUPSD_ACCESSLOG_ALL;
      else if (!_cups_strcasecmp(value, "actions"))
        AccessLogLevel = CUPSD_ACCESSLOG_ACTIONS;
      else if (!_cups_strcasecmp(value, "config"))
        AccessLogLevel = CUPSD_ACCESSLOG_CONFIG;
      else if (!_cups_strcasecmp(value, "none"))
        AccessLogLevel = CUPSD_ACCESSLOG_NONE;
      else
        cupsdLogMessage(CUPSD_LOG_WARN, "Unknown AccessLogLevel %s on line %d of %s.",
	                value, linenum, ConfigurationFile);
    }
    else if (!_cups_strcasecmp(line, "LogLevel") && value)
    {
     /*
      * Amount of logging to do to error log...
      */

      if (!_cups_strcasecmp(value, "debug2"))
        LogLevel = CUPSD_LOG_DEBUG2;
      else if (!_cups_strcasecmp(value, "debug"))
        LogLevel = CUPSD_LOG_DEBUG;
      else if (!_cups_strcasecmp(value, "info"))
        LogLevel = CUPSD_LOG_INFO;
      else if (!_cups_strcasecmp(value, "notice"))
        LogLevel = CUPSD_LOG_NOTICE;
      else if (!_cups_strcasecmp(value, "warn"))
        LogLevel = CUPSD_LOG_WARN;
      else if (!_cups_strcasecmp(value, "error"))
        LogLevel = CUPSD_LOG_ERROR;
      else if (!_cups_strcasecmp(value, "crit"))
        LogLevel = CUPSD_LOG_CRIT;
      else if (!_cups_strcasecmp(value, "alert"))
        LogLevel = CUPSD_LOG_ALERT;
      else if (!_cups_strcasecmp(value, "emerg"))
        LogLevel = CUPSD_LOG_EMERG;
      else if (!_cups_strcasecmp(value, "none"))
        LogLevel = CUPSD_LOG_NONE;
      else
        cupsdLogMessage(CUPSD_LOG_WARN, "Unknown LogLevel %s on line %d of %s.",
	                value, linenum, ConfigurationFile);
    }
    else if (!_cups_strcasecmp(line, "LogTimeFormat") && value)
    {
     /*
      * Amount of logging to do to error log...
      */

      if (!_cups_strcasecmp(value, "standard"))
        LogTimeFormat = CUPSD_TIME_STANDARD;
      else if (!_cups_strcasecmp(value, "usecs"))
        LogTimeFormat = CUPSD_TIME_USECS;
      else
        cupsdLogMessage(CUPSD_LOG_WARN, "Unknown LogTimeFormat %s on line %d of %s.",
	                value, linenum, ConfigurationFile);
    }
    else if (!_cups_strcasecmp(line, "ReadyPaperSizes") && value)
    {
     /*
      * ReadyPaperSizes sizename[,sizename,...]
      */

      if (ReadyPaperSizes)
        _cupsArrayAddStrings(ReadyPaperSizes, value, ',');
      else
        ReadyPaperSizes = _cupsArrayNewStrings(value, ',');
    }
    else if (!_cups_strcasecmp(line, "ServerTokens") && value)
    {
     /*
      * Set the string used for the Server header...
      */

      struct utsname plat;		/* Platform info */


      uname(&plat);

      if (!_cups_strcasecmp(value, "ProductOnly"))
	cupsdSetString(&ServerHeader, "CUPS IPP");
      else if (!_cups_strcasecmp(value, "Major"))
	cupsdSetStringf(&ServerHeader, "CUPS/%d IPP/2", CUPS_VERSION_MAJOR);
      else if (!_cups_strcasecmp(value, "Minor"))
	cupsdSetStringf(&ServerHeader, "CUPS/%d.%d IPP/2.1", CUPS_VERSION_MAJOR,
	                CUPS_VERSION_MINOR);
      else if (!_cups_strcasecmp(value, "Minimal"))
	cupsdSetString(&ServerHeader, CUPS_MINIMAL " IPP/2.1");
      else if (!_cups_strcasecmp(value, "OS"))
	cupsdSetStringf(&ServerHeader, CUPS_MINIMAL " (%s %s) IPP/2.1",
	                plat.sysname, plat.release);
      else if (!_cups_strcasecmp(value, "Full"))
	cupsdSetStringf(&ServerHeader, CUPS_MINIMAL " (%s %s; %s) IPP/2.1",
	                plat.sysname, plat.release, plat.machine);
      else if (!_cups_strcasecmp(value, "None"))
	cupsdSetString(&ServerHeader, "");
      else
	cupsdLogMessage(CUPSD_LOG_WARN, "Unknown ServerTokens %s on line %d of %s.",
                        value, linenum, ConfigurationFile);
    }
    else if (!_cups_strcasecmp(line, "ServerAlias") && value)
    {
     /*
      * ServerAlias name [... name]
      */

      if (!ServerAlias)
        ServerAlias = cupsArrayNew(NULL, NULL);

      while (*value)
      {
        size_t valuelen; /* Length of value */

        for (valuelen = 0; value[valuelen]; valuelen ++)
	  if (_cups_isspace(value[valuelen]) || value[valuelen] == ',')
    {
      value[valuelen ++] = '\0';
      break;
    }

	cupsdAddAlias(ServerAlias, value);

        for (value += valuelen; *value; value ++)
	  if (!_cups_isspace(*value) || *value != ',')
	    break;
      }
    }
    else if (!_cups_strcasecmp(line, "AccessLog") ||
             !_cups_strcasecmp(line, "CacheDir") ||
             !_cups_strcasecmp(line, "ConfigFilePerm") ||
             !_cups_strcasecmp(line, "DataDir") ||
             !_cups_strcasecmp(line, "DocumentRoot") ||
             !_cups_strcasecmp(line, "ErrorLog") ||
             !_cups_strcasecmp(line, "FatalErrors") ||
             !_cups_strcasecmp(line, "FileDevice") ||
             !_cups_strcasecmp(line, "Group") ||
             !_cups_strcasecmp(line, "LogFilePerm") ||
             !_cups_strcasecmp(line, "PageLog") ||
             !_cups_strcasecmp(line, "PassEnv") ||
             !_cups_strcasecmp(line, "Printcap") ||
             !_cups_strcasecmp(line, "PrintcapFormat") ||
             !_cups_strcasecmp(line, "RemoteRoot") ||
             !_cups_strcasecmp(line, "RequestRoot") ||
             !_cups_strcasecmp(line, "ServerBin") ||
             !_cups_strcasecmp(line, "ServerCertificate") ||
             !_cups_strcasecmp(line, "ServerKey") ||
             !_cups_strcasecmp(line, "ServerKeychain") ||
             !_cups_strcasecmp(line, "ServerRoot") ||
             !_cups_strcasecmp(line, "SetEnv") ||
             !_cups_strcasecmp(line, "StateDir") ||
             !_cups_strcasecmp(line, "SystemGroup") ||
             !_cups_strcasecmp(line, "SystemGroupAuthKey") ||
             !_cups_strcasecmp(line, "TempDir") ||
	     !_cups_strcasecmp(line, "User"))
    {
      cupsdLogMessage(CUPSD_LOG_INFO,
		      "Please move \"%s%s%s\" on line %d of %s to the %s file; "
		      "this will become an error in a future release.",
		      line, value ? " " : "", value ? value : "", linenum,
		      ConfigurationFile, CupsFilesFile);
    }
    else
      parse_variable(ConfigurationFile, linenum, line, value,
                     sizeof(cupsd_vars) / sizeof(cupsd_vars[0]), cupsd_vars);
  }

  return (1);
}


/*
 * 'read_cups_files_conf()' - Read the cups-files.conf configuration file.
 */

static int				/* O - 1 on success, 0 on failure */
read_cups_files_conf(cups_file_t *fp)	/* I - File to read from */
{
  size_t		i;		/* Looping var */
  int		linenum;		/* Current line number */
  char		line[HTTP_MAX_BUFFER],	/* Line from file */
		*value;			/* Value from line */
  struct group	*group;			/* Group */
  static const char * const prohibited_env[] =
  {					/* Prohibited environment variables */
    "APPLE_LANGUAGE",
    "AUTH_DOMAIN",
    "AUTH_INFO_REQUIRED",
    "AUTH_NEGOTIATE",
    "AUTH_PASSWORD",
    "AUTH_UID",
    "AUTH_USERNAME",
    "CHARSET",
    "CLASS",
    "CLASSIFICATION",
    "CONTENT_TYPE",
    "CUPS_CACHEDIR",
    "CUPS_DATADIR",
    "CUPS_DOCROOT",
    "CUPS_FILETYPE",
    "CUPS_FONTPATH",
    "CUPS_MAX_MESSAGE",
    "CUPS_REQUESTROOT",
    "CUPS_SERVERBIN",
    "CUPS_SERVERROOT",
    "CUPS_STATEDIR",
    "DEVICE_URI",
    "FINAL_CONTENT_TYPE",
    "HOME",
    "LANG",
    "PPD",
    "PRINTER",
    "PRINTER_INFO",
    "PRINTER_LOCATION",
    "PRINTER_STATE_REASONS",
    "RIP_CACHE",
    "SERVER_ADMIN",
    "SOFTWARE",
    "TMPDIR",
    "USER"
  };


 /*
  * Loop through each line in the file...
  */

  linenum = 0;

  while (cupsFileGetConf(fp, line, sizeof(line), &value, &linenum))
  {
    if (!_cups_strcasecmp(line, "FatalErrors"))
      FatalErrors = parse_fatal_errors(value);
    else if (!_cups_strcasecmp(line, "Group") && value)
    {
     /*
      * Group ID to run as...
      */

      if (isdigit(value[0]))
        Group = (gid_t)strtoul(value, NULL, 10);
      else
      {
        endgrent();
	group = getgrnam(value);

	if (group != NULL)
	  Group = group->gr_gid;
	else
	{
	  cupsdLogMessage(CUPSD_LOG_ERROR,
	                  "Unknown Group \"%s\" on line %d of %s.", value,
	                  linenum, CupsFilesFile);
	  if (FatalErrors & CUPSD_FATAL_CONFIG)
	    return (0);
	}
      }
    }
    else if (!_cups_strcasecmp(line, "LogFileGroup") && value)
    {
     /*
      * Group ID to log as...
      */

      if (isdigit(value[0]))
        LogFileGroup = (gid_t)strtoul(value, NULL, 10);
      else
      {
        endgrent();
	group = getgrnam(value);

	if (group != NULL)
	  LogFileGroup = group->gr_gid;
	else
	{
	  cupsdLogMessage(CUPSD_LOG_ERROR,
	                  "Unknown LogFileGroup \"%s\" on line %d of %s.", value,
	                  linenum, CupsFilesFile);
	  if (FatalErrors & CUPSD_FATAL_CONFIG)
	    return (0);
	}
      }
    }
    else if (!_cups_strcasecmp(line, "PassEnv") && value)
    {
     /*
      * PassEnv variable [... variable]
      */

      size_t valuelen;			/* Length of variable name */

      while (*value)
      {
        for (valuelen = 0; value[valuelen]; valuelen ++)
	  if (_cups_isspace(value[valuelen]) || value[valuelen] == ',')
	    break;

        if (value[valuelen])
        {
	  value[valuelen ++] = '\0';
	}

        for (i = 0; i < (sizeof(prohibited_env) / sizeof(prohibited_env[0])); i ++)
        {
          if (!strcmp(value, prohibited_env[i]))
          {
	    cupsdLogMessage(CUPSD_LOG_ERROR, "Environment variable \"%s\" cannot be passed through on line %d of %s.", value, linenum, CupsFilesFile);

	    if (FatalErrors & CUPSD_FATAL_CONFIG)
	      return (0);
	    else
	      break;
          }
	}

        if (i >= sizeof(prohibited_env) / sizeof(prohibited_env[0]))
          cupsdSetEnv(value, NULL);

        for (value += valuelen; *value; value ++)
	  if (!_cups_isspace(*value) || *value != ',')
	    break;
      }
    }
    else if (!_cups_strcasecmp(line, "PrintcapFormat") && value)
    {
     /*
      * Format of printcap file?
      */

      if (!_cups_strcasecmp(value, "bsd"))
        PrintcapFormat = PRINTCAP_BSD;
      else if (!_cups_strcasecmp(value, "plist"))
        PrintcapFormat = PRINTCAP_PLIST;
      else if (!_cups_strcasecmp(value, "solaris"))
        PrintcapFormat = PRINTCAP_SOLARIS;
      else
      {
	cupsdLogMessage(CUPSD_LOG_ERROR,
	                "Unknown PrintcapFormat \"%s\" on line %d of %s.",
	                value, linenum, CupsFilesFile);
        if (FatalErrors & CUPSD_FATAL_CONFIG)
          return (0);
      }
    }
    else if (!_cups_strcasecmp(line, "Sandboxing") && value)
    {
     /*
      * Level of sandboxing?
      */

      if (!_cups_strcasecmp(value, "off") && getuid())
        Sandboxing = CUPSD_SANDBOXING_OFF;
      else if (!_cups_strcasecmp(value, "relaxed"))
        Sandboxing = CUPSD_SANDBOXING_RELAXED;
      else if (!_cups_strcasecmp(value, "strict"))
        Sandboxing = CUPSD_SANDBOXING_STRICT;
      else
      {
	cupsdLogMessage(CUPSD_LOG_ERROR,
	                "Unknown Sandboxing \"%s\" on line %d of %s.",
	                value, linenum, CupsFilesFile);
        if (FatalErrors & CUPSD_FATAL_CONFIG)
          return (0);
      }
    }
    else if (!_cups_strcasecmp(line, "SetEnv") && value)
    {
     /*
      * SetEnv variable value
      */

      char *valueptr;			/* Pointer to environment variable value */

      for (valueptr = value; *valueptr && !isspace(*valueptr & 255); valueptr ++);

      if (*valueptr)
      {
       /*
        * Found a value...
	*/

        while (isspace(*valueptr & 255))
	  *valueptr++ = '\0';

        for (i = 0; i < (sizeof(prohibited_env) / sizeof(prohibited_env[0])); i ++)
        {
          if (!strcmp(value, prohibited_env[i]))
          {
	    cupsdLogMessage(CUPSD_LOG_ERROR, "Environment variable \"%s\" cannot be set  on line %d of %s.", value, linenum, CupsFilesFile);

	    if (FatalErrors & CUPSD_FATAL_CONFIG)
	      return (0);
	    else
	      break;
          }
	}

        if (i >= (sizeof(prohibited_env) / sizeof(prohibited_env[0])))
	  cupsdSetEnv(value, valueptr);
      }
      else
        cupsdLogMessage(CUPSD_LOG_ERROR,
	                "Missing value for SetEnv directive on line %d of %s.",
	                linenum, ConfigurationFile);
    }
    else if (!_cups_strcasecmp(line, "SystemGroup") && value)
    {
     /*
      * SystemGroup (admin) group(s)...
      */

      if (!parse_groups(value, linenum))
      {
        if (FatalErrors & CUPSD_FATAL_CONFIG)
          return (0);
      }
    }
    else if (!_cups_strcasecmp(line, "User") && value)
    {
     /*
      * User ID to run as...
      */

      if (isdigit(value[0] & 255))
      {
        uid_t uid = (uid_t)strtoul(value, NULL, 10);

	if (!uid)
	{
	  cupsdLogMessage(CUPSD_LOG_ERROR,
	                  "Will not use User 0 as specified on line %d of %s "
			  "for security reasons.  You must use a non-"
			  "privileged account instead.",
	                  linenum, CupsFilesFile);
          if (FatalErrors & CUPSD_FATAL_CONFIG)
            return (0);
        }
        else
	  User = uid;
      }
      else
      {
        struct passwd *p;	/* Password information */

        endpwent();
	p = getpwnam(value);

	if (p)
	{
	  if (!p->pw_uid)
	  {
	    cupsdLogMessage(CUPSD_LOG_ERROR,
	                    "Will not use User %s (UID=0) as specified on line "
			    "%d of %s for security reasons.  You must use a "
			    "non-privileged account instead.",
	                    value, linenum, CupsFilesFile);
	    if (FatalErrors & CUPSD_FATAL_CONFIG)
	      return (0);
	  }
	  else
	    User = p->pw_uid;
	}
	else
	{
	  cupsdLogMessage(CUPSD_LOG_ERROR,
	                  "Unknown User \"%s\" on line %d of %s.",
	                  value, linenum, CupsFilesFile);
          if (FatalErrors & CUPSD_FATAL_CONFIG)
            return (0);
        }
      }
    }
    else if (!_cups_strcasecmp(line, "ServerCertificate") ||
             !_cups_strcasecmp(line, "ServerKey"))
    {
      cupsdLogMessage(CUPSD_LOG_INFO,
		      "The \"%s\" directive on line %d of %s is no longer "
		      "supported; this will become an error in a future "
		      "release.",
		      line, linenum, CupsFilesFile);
    }
    else if (!parse_variable(CupsFilesFile, linenum, line, value,
			     sizeof(cupsfiles_vars) / sizeof(cupsfiles_vars[0]),
			     cupsfiles_vars) &&
	     (FatalErrors & CUPSD_FATAL_CONFIG))
      return (0);
  }

  return (1);
}


/*
 * 'read_location()' - Read a <Location path> definition.
 */

static int				/* O - New line number or 0 on error */
read_location(cups_file_t *fp,		/* I - Configuration file */
              char        *location,	/* I - Location name/path */
	      int         linenum)	/* I - Current line number */
{
  cupsd_location_t	*loc,		/* New location */
			*parent;	/* Parent location */
  char			line[HTTP_MAX_BUFFER],
					/* Line buffer */
			*value,		/* Value for directive */
			*valptr;	/* Pointer into value */


  if ((parent = cupsdFindLocation(location)) != NULL)
    cupsdLogMessage(CUPSD_LOG_WARN, "Duplicate <Location %s> on line %d of %s.",
                    location, linenum, ConfigurationFile);
  else if ((parent = cupsdNewLocation(location)) == NULL)
    return (0);
  else
  {
    cupsdAddLocation(parent);

    parent->limit = CUPSD_AUTH_LIMIT_ALL;
  }

  loc = parent;

  while (cupsFileGetConf(fp, line, sizeof(line), &value, &linenum))
  {
   /*
    * Decode the directive...
    */

    if (!_cups_strcasecmp(line, "</Location>"))
      return (linenum);
    else if (!_cups_strcasecmp(line, "<Limit") ||
             !_cups_strcasecmp(line, "<LimitExcept"))
    {
      if (!value)
      {
        cupsdLogMessage(CUPSD_LOG_ERROR, "Syntax error on line %d of %s.", linenum, ConfigurationFile);
        if (FatalErrors & CUPSD_FATAL_CONFIG)
	  return (0);
        else
	  continue;
      }

      if ((loc = cupsdCopyLocation(parent)) == NULL)
        return (0);

      cupsdAddLocation(loc);

      loc->limit = 0;
      while (*value)
      {
        for (valptr = value; !isspace(*valptr & 255) && *valptr; valptr ++);

	if (*valptr)
	  *valptr++ = '\0';

        if (!strcmp(value, "ALL"))
	  loc->limit = CUPSD_AUTH_LIMIT_ALL;
	else if (!strcmp(value, "GET"))
	  loc->limit |= CUPSD_AUTH_LIMIT_GET;
	else if (!strcmp(value, "HEAD"))
	  loc->limit |= CUPSD_AUTH_LIMIT_HEAD;
	else if (!strcmp(value, "OPTIONS"))
	  loc->limit |= CUPSD_AUTH_LIMIT_OPTIONS;
	else if (!strcmp(value, "POST"))
	  loc->limit |= CUPSD_AUTH_LIMIT_POST;
	else if (!strcmp(value, "PUT"))
	  loc->limit |= CUPSD_AUTH_LIMIT_PUT;
	else if (!strcmp(value, "TRACE"))
	  loc->limit |= CUPSD_AUTH_LIMIT_TRACE;
	else
	  cupsdLogMessage(CUPSD_LOG_WARN, "Unknown request type %s on line %d of %s.",
	                  value, linenum, ConfigurationFile);

        for (value = valptr; isspace(*value & 255); value ++);
      }

      if (!_cups_strcasecmp(line, "<LimitExcept"))
        loc->limit = CUPSD_AUTH_LIMIT_ALL ^ loc->limit;

      parent->limit &= ~loc->limit;
    }
    else if (!_cups_strcasecmp(line, "</Limit>") ||
             !_cups_strcasecmp(line, "</LimitExcept>"))
      loc = parent;
    else if (!value)
    {
      cupsdLogMessage(CUPSD_LOG_ERROR, "Missing value on line %d of %s.", linenum, ConfigurationFile);
      if (FatalErrors & CUPSD_FATAL_CONFIG)
	return (0);
    }
    else if (!parse_aaa(loc, line, value, linenum))
    {
      cupsdLogMessage(CUPSD_LOG_ERROR,
                      "Unknown Location directive %s on line %d of %s.",
	              line, linenum, ConfigurationFile);
      if (FatalErrors & CUPSD_FATAL_CONFIG)
	return (0);
    }
  }

  cupsdLogMessage(CUPSD_LOG_ERROR,
                  "Unexpected end-of-file at line %d while reading location.",
                  linenum);

  return ((FatalErrors & CUPSD_FATAL_CONFIG) ? 0 : linenum);
}


/*
 * 'read_policy()' - Read a <Policy name> definition.
 */

static int				/* O - New line number or 0 on error */
read_policy(cups_file_t *fp,		/* I - Configuration file */
            char        *policy,	/* I - Location name/path */
	    int         linenum)	/* I - Current line number */
{
  int			i;		/* Looping var */
  cupsd_policy_t	*pol;		/* Policy */
  cupsd_location_t	*op;		/* Policy operation */
  int			num_ops;	/* Number of IPP operations */
  ipp_op_t		ops[100];	/* Operations */
  char			line[HTTP_MAX_BUFFER],
					/* Line buffer */
			*value,		/* Value for directive */
			*valptr;	/* Pointer into value */


 /*
  * Create the policy...
  */

  if ((pol = cupsdFindPolicy(policy)) != NULL)
    cupsdLogMessage(CUPSD_LOG_WARN, "Duplicate <Policy %s> on line %d of %s.",
                    policy, linenum, ConfigurationFile);
  else if ((pol = cupsdAddPolicy(policy)) == NULL)
    return (0);

 /*
  * Read from the file...
  */

  op      = NULL;
  num_ops = 0;

  while (cupsFileGetConf(fp, line, sizeof(line), &value, &linenum))
  {
   /*
    * Decode the directive...
    */

    if (!_cups_strcasecmp(line, "</Policy>"))
    {
      if (op)
        cupsdLogMessage(CUPSD_LOG_WARN,
	                "Missing </Limit> before </Policy> on line %d of %s.",
	                linenum, ConfigurationFile);

      set_policy_defaults(pol);

      return (linenum);
    }
    else if (!_cups_strcasecmp(line, "<Limit") && !op)
    {
      if (!value)
      {
        cupsdLogMessage(CUPSD_LOG_ERROR, "Syntax error on line %d of %s.", linenum, ConfigurationFile);
        if (FatalErrors & CUPSD_FATAL_CONFIG)
	  return (0);
        else
	  continue;
      }

     /*
      * Scan for IPP operation names...
      */

      num_ops = 0;

      while (*value)
      {
        for (valptr = value; !isspace(*valptr & 255) && *valptr; valptr ++);

	if (*valptr)
	  *valptr++ = '\0';

        if (num_ops < (int)(sizeof(ops) / sizeof(ops[0])))
	{
	  if (!_cups_strcasecmp(value, "All"))
	    ops[num_ops ++] = IPP_ANY_OPERATION;
	  else if ((ops[num_ops] = ippOpValue(value)) == IPP_BAD_OPERATION)
	    cupsdLogMessage(CUPSD_LOG_ERROR, "Bad IPP operation name \"%s\" on line %d of %s.", value, linenum, ConfigurationFile);
          else
	    num_ops ++;
	}
	else
	  cupsdLogMessage(CUPSD_LOG_ERROR,
	                  "Too many operations listed on line %d of %s.",
	                  linenum, ConfigurationFile);

        for (value = valptr; isspace(*value & 255); value ++);
      }

     /*
      * If none are specified, apply the policy to all operations...
      */

      if (num_ops == 0)
      {
        ops[0]  = IPP_ANY_OPERATION;
	num_ops = 1;
      }

     /*
      * Add a new policy for the first operation...
      */

      op = cupsdAddPolicyOp(pol, NULL, ops[0]);
    }
    else if (!_cups_strcasecmp(line, "</Limit>") && op)
    {
     /*
      * Finish the current operation limit...
      */

      if (num_ops > 1)
      {
       /*
        * Copy the policy to the other operations...
	*/

        for (i = 1; i < num_ops; i ++)
	  cupsdAddPolicyOp(pol, op, ops[i]);
      }

      op = NULL;
    }
    else if (!value)
    {
      cupsdLogMessage(CUPSD_LOG_ERROR, "Missing value on line %d of %s.", linenum, ConfigurationFile);
      if (FatalErrors & CUPSD_FATAL_CONFIG)
	return (0);
    }
    else if (!_cups_strcasecmp(line, "JobPrivateAccess") ||
	     !_cups_strcasecmp(line, "JobPrivateValues") ||
	     !_cups_strcasecmp(line, "SubscriptionPrivateAccess") ||
	     !_cups_strcasecmp(line, "SubscriptionPrivateValues"))
    {
      if (op)
      {
        cupsdLogMessage(CUPSD_LOG_ERROR,
	                "%s directive must appear outside <Limit>...</Limit> "
			"on line %d of %s.", line, linenum, ConfigurationFile);
	if (FatalErrors & CUPSD_FATAL_CONFIG)
	  return (0);
      }
      else
      {
       /*
        * Pull out whitespace-delimited values...
	*/

	while (*value)
	{
	 /*
	  * Find the end of the current value...
	  */

	  for (valptr = value; !isspace(*valptr & 255) && *valptr; valptr ++);

	  if (*valptr)
	    *valptr++ = '\0';

         /*
	  * Save it appropriately...
	  */

	  if (!_cups_strcasecmp(line, "JobPrivateAccess"))
	  {
	   /*
	    * JobPrivateAccess {all|default|user/group list|@@ACL}
	    */

            if (!_cups_strcasecmp(value, "default"))
	    {
	      cupsdAddString(&(pol->job_access), "@OWNER");
	      cupsdAddString(&(pol->job_access), "@SYSTEM");
	    }
	    else
	      cupsdAddString(&(pol->job_access), value);
	  }
	  else if (!_cups_strcasecmp(line, "JobPrivateValues"))
	  {
	   /*
	    * JobPrivateValues {all|none|default|attribute list}
	    */

	    if (!_cups_strcasecmp(value, "default"))
	    {
	      cupsdAddString(&(pol->job_attrs), "job-name");
	      cupsdAddString(&(pol->job_attrs), "job-originating-host-name");
	      cupsdAddString(&(pol->job_attrs), "job-originating-user-name");
	      cupsdAddString(&(pol->job_attrs), "phone");
	    }
	    else
	      cupsdAddString(&(pol->job_attrs), value);
	  }
	  else if (!_cups_strcasecmp(line, "SubscriptionPrivateAccess"))
	  {
	   /*
	    * SubscriptionPrivateAccess {all|default|user/group list|@@ACL}
	    */

            if (!_cups_strcasecmp(value, "default"))
	    {
	      cupsdAddString(&(pol->sub_access), "@OWNER");
	      cupsdAddString(&(pol->sub_access), "@SYSTEM");
	    }
	    else
	      cupsdAddString(&(pol->sub_access), value);
	  }
	  else /* if (!_cups_strcasecmp(line, "SubscriptionPrivateValues")) */
	  {
	   /*
	    * SubscriptionPrivateValues {all|none|default|attribute list}
	    */

	    if (!_cups_strcasecmp(value, "default"))
	    {
	      cupsdAddString(&(pol->sub_attrs), "notify-events");
	      cupsdAddString(&(pol->sub_attrs), "notify-pull-method");
	      cupsdAddString(&(pol->sub_attrs), "notify-recipient-uri");
	      cupsdAddString(&(pol->sub_attrs), "notify-subscriber-user-name");
	      cupsdAddString(&(pol->sub_attrs), "notify-user-data");
	    }
	    else
	      cupsdAddString(&(pol->sub_attrs), value);
	  }

	 /*
	  * Find the next string on the line...
	  */

	  for (value = valptr; isspace(*value & 255); value ++);
	}
      }
    }
    else if (!op)
    {
      cupsdLogMessage(CUPSD_LOG_ERROR,
                      "Missing <Limit ops> directive before %s on line %d of %s.",
                      line, linenum, ConfigurationFile);
      if (FatalErrors & CUPSD_FATAL_CONFIG)
	return (0);
    }
    else if (!parse_aaa(op, line, value, linenum))
    {
      cupsdLogMessage(CUPSD_LOG_ERROR,
		      "Unknown Policy Limit directive %s on line %d of %s.",
		      line, linenum, ConfigurationFile);

      if (FatalErrors & CUPSD_FATAL_CONFIG)
	return (0);
    }
  }

  cupsdLogMessage(CUPSD_LOG_ERROR,
                  "Unexpected end-of-file at line %d while reading policy "
                  "\"%s\".", linenum, policy);

  return ((FatalErrors & CUPSD_FATAL_CONFIG) ? 0 : linenum);
}


/*
 * 'set_policy_defaults()' - Set default policy values as needed.
 */

static void
set_policy_defaults(cupsd_policy_t *pol)/* I - Policy */
{
  cupsd_location_t	*op;		/* Policy operation */


 /*
  * Verify that we have an explicit policy for Validate-Job, Cancel-Jobs,
  * Cancel-My-Jobs, Close-Job, and CUPS-Get-Document, which ensures that
  * upgrades do not introduce new security issues...
  *
  * CUPS STR #4659: Allow a lone <Limit All> policy.
  */

  if (cupsArrayCount(pol->ops) > 1)
  {
    if ((op = cupsdFindPolicyOp(pol, IPP_VALIDATE_JOB)) == NULL ||
	op->op == IPP_ANY_OPERATION)
    {
      if ((op = cupsdFindPolicyOp(pol, IPP_PRINT_JOB)) != NULL &&
	  op->op != IPP_ANY_OPERATION)
      {
       /*
	* Add a new limit for Validate-Job using the Print-Job limit as a
	* template...
	*/

	cupsdLogMessage(CUPSD_LOG_WARN, "No limit for Validate-Job defined in policy %s - using Print-Job's policy.", pol->name);

	cupsdAddPolicyOp(pol, op, IPP_VALIDATE_JOB);
      }
      else
	cupsdLogMessage(CUPSD_LOG_WARN, "No limit for Validate-Job defined in policy %s and no suitable template found.", pol->name);
    }

    if ((op = cupsdFindPolicyOp(pol, IPP_CANCEL_JOBS)) == NULL ||
	op->op == IPP_ANY_OPERATION)
    {
      if ((op = cupsdFindPolicyOp(pol, IPP_PAUSE_PRINTER)) != NULL &&
	  op->op != IPP_ANY_OPERATION)
      {
       /*
	* Add a new limit for Cancel-Jobs using the Pause-Printer limit as a
	* template...
	*/

	cupsdLogMessage(CUPSD_LOG_WARN, "No limit for Cancel-Jobs defined in policy %s - using Pause-Printer's policy.", pol->name);

	cupsdAddPolicyOp(pol, op, IPP_CANCEL_JOBS);
      }
      else
	cupsdLogMessage(CUPSD_LOG_WARN, "No limit for Cancel-Jobs defined in policy %s and no suitable template found.", pol->name);
    }

    if ((op = cupsdFindPolicyOp(pol, IPP_CANCEL_MY_JOBS)) == NULL ||
	op->op == IPP_ANY_OPERATION)
    {
      if ((op = cupsdFindPolicyOp(pol, IPP_SEND_DOCUMENT)) != NULL &&
	  op->op != IPP_ANY_OPERATION)
      {
       /*
	* Add a new limit for Cancel-My-Jobs using the Send-Document limit as
	* a template...
	*/

	cupsdLogMessage(CUPSD_LOG_WARN, "No limit for Cancel-My-Jobs defined in policy %s - using Send-Document's policy.", pol->name);

	cupsdAddPolicyOp(pol, op, IPP_CANCEL_MY_JOBS);
      }
      else
	cupsdLogMessage(CUPSD_LOG_WARN, "No limit for Cancel-My-Jobs defined in policy %s and no suitable template found.", pol->name);
    }

    if ((op = cupsdFindPolicyOp(pol, IPP_CLOSE_JOB)) == NULL ||
	op->op == IPP_ANY_OPERATION)
    {
      if ((op = cupsdFindPolicyOp(pol, IPP_SEND_DOCUMENT)) != NULL &&
	  op->op != IPP_ANY_OPERATION)
      {
       /*
	* Add a new limit for Close-Job using the Send-Document limit as a
	* template...
	*/

	cupsdLogMessage(CUPSD_LOG_WARN, "No limit for Close-Job defined in policy %s - using Send-Document's policy.", pol->name);

	cupsdAddPolicyOp(pol, op, IPP_CLOSE_JOB);
      }
      else
	cupsdLogMessage(CUPSD_LOG_WARN, "No limit for Close-Job defined in policy %s and no suitable template found.", pol->name);
    }

    if ((op = cupsdFindPolicyOp(pol, CUPS_GET_DOCUMENT)) == NULL ||
	op->op == IPP_ANY_OPERATION)
    {
      if ((op = cupsdFindPolicyOp(pol, IPP_SEND_DOCUMENT)) != NULL &&
	  op->op != IPP_ANY_OPERATION)
      {
       /*
	* Add a new limit for CUPS-Get-Document using the Send-Document
	* limit as a template...
	*/

	cupsdLogMessage(CUPSD_LOG_WARN, "No limit for CUPS-Get-Document defined in policy %s - using Send-Document's policy.", pol->name);

	cupsdAddPolicyOp(pol, op, CUPS_GET_DOCUMENT);
      }
      else
	cupsdLogMessage(CUPSD_LOG_WARN, "No limit for CUPS-Get-Document defined in policy %s and no suitable template found.", pol->name);
    }
  }

 /*
  * Verify we have JobPrivateAccess, JobPrivateValues,
  * SubscriptionPrivateAccess, and SubscriptionPrivateValues in the policy.
  */

  if (!pol->job_access)
  {
    cupsdLogMessage(CUPSD_LOG_WARN, "No JobPrivateAccess defined in policy %s - using defaults.", pol->name);
    cupsdAddString(&(pol->job_access), "@OWNER");
    cupsdAddString(&(pol->job_access), "@SYSTEM");
  }

  if (!pol->job_attrs)
  {
    cupsdLogMessage(CUPSD_LOG_WARN, "No JobPrivateValues defined in policy %s - using defaults.", pol->name);
    cupsdAddString(&(pol->job_attrs), "job-name");
    cupsdAddString(&(pol->job_attrs), "job-originating-host-name");
    cupsdAddString(&(pol->job_attrs), "job-originating-user-name");
    cupsdAddString(&(pol->job_attrs), "phone");
  }

  if (!pol->sub_access)
  {
    cupsdLogMessage(CUPSD_LOG_WARN, "No SubscriptionPrivateAccess defined in policy %s - using defaults.", pol->name);
    cupsdAddString(&(pol->sub_access), "@OWNER");
    cupsdAddString(&(pol->sub_access), "@SYSTEM");
  }

  if (!pol->sub_attrs)
  {
    cupsdLogMessage(CUPSD_LOG_WARN, "No SubscriptionPrivateValues defined in policy %s - using defaults.", pol->name);
    cupsdAddString(&(pol->sub_attrs), "notify-events");
    cupsdAddString(&(pol->sub_attrs), "notify-pull-method");
    cupsdAddString(&(pol->sub_attrs), "notify-recipient-uri");
    cupsdAddString(&(pol->sub_attrs), "notify-subscriber-user-name");
    cupsdAddString(&(pol->sub_attrs), "notify-user-data");
  }
}
