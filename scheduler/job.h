/*
 * Print job definitions for the CUPS scheduler.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright 2007-2015 by Apple Inc.
 * Copyright 1997-2007 by Easy Software Products, all rights reserved.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more information.
 */

/*
 * Constants...
 */

typedef enum cupsd_jobaction_e		/**** Actions for state changes ****/
{
  CUPSD_JOB_DEFAULT,			/* Use default action */
  CUPSD_JOB_FORCE,			/* Force the change */
  CUPSD_JOB_PURGE			/* Force the change and purge */
} cupsd_jobaction_t;


/*
 * Job request structure...
 */

struct cupsd_job_s			/**** Job request ****/
{
  int			id,		/* Job ID */
			priority,	/* Job priority */
			dirty;		/* Do we need to write the "c" file? */
  ipp_jstate_t		state_value;	/* Cached job-state */
  int			pending_timeout;/* Non-zero if the job was created and
					 * waiting on files */
  char			*username;	/* Printing user */
  char			*dest;		/* Destination printer or class */
  char			*name;		/* Job name/title */
  int			koctets;	/* job-k-octets */
  cups_ptype_t		dtype;		/* Destination type */
  cupsd_printer_t	*printer;	/* Printer this job is assigned to */
  int			num_files;	/* Number of files in job */
  mime_type_t		**filetypes;	/* File types */
  int			*compressions;	/* Compression status of each file */
  ipp_attribute_t	*impressions,	/* job-impressions-completed */
			*sheets;	/* job-media-sheets-completed */
  time_t		access_time,	/* Last access time */
			cancel_time,	/* When to cancel/send SIGTERM */
			creation_time,	/* When job was created */
			completed_time,	/* When job was completed (0 if not) */
			file_time,	/* Job file retain time */
			history_time,	/* Job history retain time */
			hold_until,	/* Hold expiration date/time */
			kill_time;	/* When to send SIGKILL */
  ipp_attribute_t	*state;		/* Job state */
  ipp_attribute_t	*reasons;	/* Job state reasons */
  ipp_attribute_t	*job_sheets;	/* Job sheets (NULL if none) */
  ipp_attribute_t	*printer_message,
					/* job-printer-state-message */
			*printer_reasons;
					/* job-printer-state-reasons */
  int			current_file;	/* Current file in job */
  ipp_t			*attrs;		/* Job attributes */
  int			print_pipes[2],	/* Print data pipes */
			back_pipes[2],	/* Backchannel pipes */
			side_pipes[2],	/* Sidechannel pipes */
			status_pipes[2];/* Status pipes */
  cupsd_statbuf_t	*status_buffer;	/* Status buffer for this job */
  int			status_level;	/* Highest log level in a status
					 * message */
  int			cost;		/* Filtering cost */
  int			pending_cost;	/* Waiting for FilterLimit */
  int			filters[MAX_FILTERS + 1];
					/* Filter process IDs, 0 terminated */
  int			backend;	/* Backend process ID */
  int			status;		/* Status code from filters */
  int			tries;		/* Number of tries for this job */
  int			completed;	/* cups-waiting-for-job-completed seen */
  int			retry_as_raster;/* Need to retry the job as raster */
  char			*auth_env[3],	/* AUTH_xxx environment variables,
                                         * if any */
			*auth_uid;	/* AUTH_UID environment variable */
  void			*profile,	/* Security profile for filters */
			*bprofile;	/* Security profile for backend */
  cups_array_t		*history;	/* Debug log history */
  int			progress;	/* Printing progress */
  int			num_keywords;	/* Number of PPD keywords */
  cups_option_t		*keywords;	/* PPD keywords */
};

typedef struct cupsd_joblog_s		/**** Job log message ****/
{
  time_t		time;		/* Time of message */
  char			message[1];	/* Message string */
} cupsd_joblog_t;


/*
 * Globals...
 */

VAR int			JobHistory	VALUE(INT_MAX);
					/* Preserve job history? */
VAR int			JobFiles	VALUE(86400);
					/* Preserve job files? */
VAR time_t		JobHistoryUpdate VALUE(0);
					/* Time for next job history update */
VAR int			MaxJobs		VALUE(0),
					/* Max number of jobs */
			MaxActiveJobs	VALUE(0),
					/* Max number of active jobs */
			MaxHoldTime	VALUE(0),
					/* Max time for indefinite hold */
			MaxJobsPerUser	VALUE(0),
					/* Max jobs per user */
			MaxJobsPerPrinter VALUE(0),
					/* Max jobs per printer */
			MaxJobTime	VALUE(3 * 60 * 60);
					/* Max time for a job */
VAR int			JobAutoPurge	VALUE(0);
					/* Automatically purge jobs */
VAR cups_array_t	*Jobs		VALUE(NULL),
					/* List of current jobs */
			*ActiveJobs	VALUE(NULL),
					/* List of active jobs */
			*PrintingJobs	VALUE(NULL);
					/* List of jobs that are printing */
VAR int			NextJobId	VALUE(1);
					/* Next job ID to use */
VAR int			JobKillDelay	VALUE(DEFAULT_TIMEOUT),
					/* Delay before killing jobs */
			JobRetryLimit	VALUE(5),
					/* Max number of tries */
			JobRetryInterval VALUE(300);
					/* Seconds between retries */


/*
 * Prototypes...
 */

extern cupsd_job_t	*cupsdAddJob(int priority, const char *dest);
extern void		cupsdCancelJobs(const char *dest, const char *username,
			                int purge);
extern void		cupsdCheckJobs(void);
extern void		cupsdCleanJobs(void);
extern void		cupsdContinueJob(cupsd_job_t *job);
extern void		cupsdDeleteJob(cupsd_job_t *job,
			               cupsd_jobaction_t action);
extern cupsd_job_t	*cupsdFindJob(int id);
extern void		cupsdFreeAllJobs(void);
extern cups_array_t	*cupsdGetCompletedJobs(cupsd_printer_t *p);
extern int		cupsdGetPrinterJobCount(const char *dest);
extern int		cupsdGetUserJobCount(const char *username);
extern void		cupsdLoadAllJobs(void);
extern int		cupsdLoadJob(cupsd_job_t *job);
extern void		cupsdMoveJob(cupsd_job_t *job, cupsd_printer_t *p);
extern void		cupsdReleaseJob(cupsd_job_t *job);
extern void		cupsdRestartJob(cupsd_job_t *job);
extern void		cupsdSaveAllJobs(void);
extern void		cupsdSaveJob(cupsd_job_t *job);
extern void		cupsdSetJobHoldUntil(cupsd_job_t *job,
			                     const char *when, int update);
extern void		cupsdSetJobPriority(cupsd_job_t *job, int priority);
extern void		cupsdSetJobState(cupsd_job_t *job,
			                 ipp_jstate_t newstate,
					 cupsd_jobaction_t action,
					 const char *message, ...)
					__attribute__((__format__(__printf__,
					                          4, 5)));
extern void		cupsdStopAllJobs(cupsd_jobaction_t action,
			                 int kill_delay);
extern int		cupsdTimeoutJob(cupsd_job_t *job);
extern void		cupsdUnloadCompletedJobs(void);
extern void		cupsdUpdateJobs(void);
