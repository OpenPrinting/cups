/*
 * Sandbox helper for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2007-2014 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 *
 * Usage:
 *
 *     cups-exec /path/to/profile [-u UID] [-g GID] [-n NICE] /path/to/program argv0 argv1 ... argvN
 */

/*
 * Include necessary headers...
 */

#include <cups/string-private.h>
#include <cups/file.h>
#include <unistd.h>
#include <fcntl.h>
#include <grp.h>
#include <sys/stat.h>
#ifdef HAVE_SANDBOX_H
#  include <sandbox.h>
#  ifndef SANDBOX_NAMED_EXTERNAL
#    define SANDBOX_NAMED_EXTERNAL  0x0003
#  endif /* !SANDBOX_NAMED_EXTERNAL */
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif /* HAVE_SANDBOX_H */


/*
 * Local functions...
 */

static void	usage(void) _CUPS_NORETURN;


/*
 * 'main()' - Apply sandbox profile and execute program.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line args */
     char *argv[])			/* I - Command-line arguments */
{
  int		i;			/* Looping var */
  const char	*opt;			/* Current option character */
  uid_t		uid = getuid();		/* UID */
  gid_t		gid = getgid();		/* GID */
  int		niceval = 0;		/* Nice value */
#ifdef HAVE_SANDBOX_H
  char		*sandbox_error = NULL;	/* Sandbox error, if any */
#endif /* HAVE_SANDBOX_H */


 /*
  * Parse command-line...
  */

  for (i = 1; i < argc; i ++)
  {
    if (argv[i][0] == '-')
    {
      for (opt = argv[i] + 1; *opt; opt ++)
      {
        switch (*opt)
        {
          case 'g' : /* -g gid */
              i ++;
              if (i >= argc)
                usage();

              gid = (gid_t)strtoul(argv[i], NULL, 10);
              break;

          case 'n' : /* -n nice-value */
              i ++;
              if (i >= argc)
                usage();

              niceval = atoi(argv[i]);
              break;

          case 'u' : /* -g gid */
              i ++;
              if (i >= argc)
                usage();

              uid = (uid_t)strtoul(argv[i], NULL, 10);
              break;

	  default :
	      fprintf(stderr, "cups-exec: Unknown option '-%c'.\n", *opt);
	      usage();
        }
      }
    }
    else
      break;
  }

 /*
  * Check that we have enough arguments...
  */

  if ((i + 3) > argc)
  {
    fputs("cups-exec: Insufficient arguments.\n", stderr);
    usage();
  }

 /*
  * Make sure side and back channel FDs are non-blocking...
  */

  fcntl(3, F_SETFL, O_NDELAY);
  fcntl(4, F_SETFL, O_NDELAY);

 /*
  * Change UID, GID, and nice value...
  */

  if (uid)
    nice(niceval);

  if (!getuid())
  {
    if (setgid(gid))
      exit(errno + 100);

#  if CUPS_SNAP
    if (setgroups(0, NULL))
#  else
    if (setgroups(1, &gid))
#  endif /* CUPS_SNAP */
      exit(errno + 100);

    if (uid && setuid(uid))
      exit(errno + 100);
  }

  umask(077);

#ifdef HAVE_SANDBOX_H
 /*
  * Run in a separate security profile...
  */

  if (strcmp(argv[i], "none") &&
      sandbox_init(argv[i], SANDBOX_NAMED_EXTERNAL, &sandbox_error))
  {
    cups_file_t	*fp;			/* File */
    char	line[1024];		/* Line from file */
    unsigned	linenum = 0;		/* Line number in file */

    fprintf(stderr, "DEBUG: sandbox_init failed: %s (%s)\n", sandbox_error,
	    strerror(errno));
    sandbox_free_error(sandbox_error);

    if ((fp = cupsFileOpen(argv[i], "r")) != NULL)
    {
      while (cupsFileGets(fp, line, sizeof(line)))
      {
        linenum ++;
        fprintf(stderr, "DEBUG: %4u  %s\n", linenum, line);
      }
      cupsFileClose(fp);
    }

    return (100 + EINVAL);
  }
#endif /* HAVE_SANDBOX_H */

 /*
  * Execute the program...
  */

  execv(argv[i + 1], argv + i + 2);

 /*
  * If we get here, execv() failed...
  */

  fprintf(stderr, "DEBUG: execv failed: %s\n", strerror(errno));
  return (errno + 100);
}


/*
 * 'usage()' - Show program usage.
 */

static void
usage(void)
{
  fputs("Usage: cups-exec [-g gid] [-n nice-value] [-u uid] /path/to/profile /path/to/program argv0 argv1 ... argvN\n", stderr);
  exit(1);
}
