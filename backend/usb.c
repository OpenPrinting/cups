/*
 * USB printer backend for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2007-2012 by Apple Inc.
 * Copyright © 1997-2007 by Easy Software Products, all rights reserved.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers.
 */

#ifdef __APPLE__
   /* A header order dependency requires this be first */
#  include <ApplicationServices/ApplicationServices.h>
#endif /* __APPLE__ */

#include "backend-private.h"

#ifdef _WIN32
#  include <io.h>
#else
#  include <unistd.h>
#  include <fcntl.h>
#  include <termios.h>
#endif /* _WIN32 */


/*
 * Local functions...
 */

void	list_devices(void);
int	print_device(const char *uri, const char *hostname,
	             const char *resource, char *options,
		     int print_fd, int copies, int argc, char *argv[]);


/*
 * Include the vendor-specific USB implementation...
 */

#ifdef HAVE_LIBUSB
#  include "usb-libusb.c"
#elif defined(__APPLE__)
#  include "usb-darwin.c"
#elif defined(__linux) || defined(__sun) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__) || defined(__FreeBSD_kernel__)
#  include "usb-unix.c"
#else
/*
 * Use dummy functions that do nothing on unsupported platforms...
 * These can be used as templates for implementing USB printing on new
 * platforms...
 */

/*
 * 'list_devices()' - List all available USB devices to stdout.
 */

void
list_devices(void)
{
 /*
  * Don't have any devices to list... Use output of the form:
  *
  *     direct usb:/make/model?serial=foo "Make Model" "USB Printer"
  *
  * Note that "Hewlett Packard" or any other variation MUST be mapped to
  * "HP" for compatibility with the PPD and ICC specs.
  */
}


/*
 * 'print_device()' - Print a file to a USB device.
 */

int					/* O - Exit status */
print_device(const char *uri,		/* I - Device URI */
             const char *hostname,	/* I - Hostname/manufacturer */
             const char *resource,	/* I - Resource/modelname */
	     char       *options,	/* I - Device options/serial number */
	     int        print_fd,	/* I - File descriptor to print */
	     int        copies,		/* I - Copies to print */
	     int	argc,		/* I - Number of command-line arguments (6 or 7) */
	     char	*argv[])	/* I - Command-line arguments */
{
 /*
  * Can't print, so just reference the arguments to eliminate compiler
  * warnings and return and exit status of 1.  Normally you would use the
  * arguments to send a file to the printer and return 0 if everything
  * worked OK and non-zero if there was an error.
  */

  (void)uri;
  (void)hostname;
  (void)resource;
  (void)options;
  (void)print_fd;
  (void)copies;
  (void)argc;
  (void)argv;

  return (CUPS_BACKEND_FAILED);
}
#endif /* HAVE_LIBUSB */


/*
 * 'main()' - Send a file to the specified USB port.
 *
 * Usage:
 *
 *    printer-uri job-id user title copies options [file]
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line arguments (6 or 7) */
     char *argv[])			/* I - Command-line arguments */
{
  int		print_fd;		/* Print file */
  int		copies;			/* Number of copies to print */
  int		status;			/* Exit status */
  int		port;			/* Port number (not used) */
  const char	*uri;			/* Device URI */
  char		method[255],		/* Method in URI */
		hostname[1024],		/* Hostname */
		username[255],		/* Username info (not used) */
		resource[1024],		/* Resource info (device and options) */
		*options;		/* Pointer to options */
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
  struct sigaction action;		/* Actions for POSIX signals */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */


 /*
  * Make sure status messages are not buffered...
  */

  setbuf(stderr, NULL);

 /*
  * Ignore SIGPIPE signals...
  */

#ifdef HAVE_SIGSET
  sigset(SIGPIPE, SIG_IGN);
#elif defined(HAVE_SIGACTION)
  memset(&action, 0, sizeof(action));
  action.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &action, NULL);
#else
  signal(SIGPIPE, SIG_IGN);
#endif /* HAVE_SIGSET */

 /*
  * Check command-line...
  */

  if (argc == 1)
  {
    list_devices();
    return (CUPS_BACKEND_OK);
  }
  else if (argc != 6 && argc != 7)
  {
    _cupsLangPrintf(stderr,
                    _("Usage: %s job-id user title copies options [file]"),
                    argv[0]);
    return (CUPS_BACKEND_FAILED);
  }

 /*
  * Extract the device name and options from the URI...
  */

  uri = cupsBackendDeviceURI(argv);

  if (httpSeparateURI(HTTP_URI_CODING_ALL, uri,
                      method, sizeof(method), username, sizeof(username),
		      hostname, sizeof(hostname), &port,
		      resource, sizeof(resource)) < HTTP_URI_OK)
  {
    _cupsLangPrintFilter(stderr, "ERROR",
			 _("No device URI found in argv[0] or in DEVICE_URI "
                           "environment variable."));
    return (1);
  }

 /*
  * See if there are any options...
  */

  if ((options = strchr(resource, '?')) != NULL)
  {
   /*
    * Yup, terminate the device name string and move to the first
    * character of the options...
    */

    *options++ = '\0';
  }

 /*
  * If we have 7 arguments, print the file named on the command-line.
  * Otherwise, send stdin instead...
  */

  if (argc == 6)
  {
    print_fd = 0;
    copies   = 1;
  }
  else
  {
   /*
    * Try to open the print file...
    */

    if ((print_fd = open(argv[6], O_RDONLY)) < 0)
    {
      _cupsLangPrintError("ERROR", _("Unable to open print file"));
      return (CUPS_BACKEND_FAILED);
    }

    copies = atoi(argv[4]);
  }

 /*
  * Finally, send the print file...
  */

  status = print_device(uri, hostname, resource, options, print_fd, copies,
                        argc, argv);

 /*
  * Close the input file and return...
  */

  if (print_fd != 0)
    close(print_fd);

  return (status);
}
