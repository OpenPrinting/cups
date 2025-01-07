/*
 * Admin function test program for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright 2007-2013 by Apple Inc.
 * Copyright 2006 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more information.
 */

/*
 * Include necessary headers...
 */

#include "adminutil.h"
#include "string-private.h"


/*
 * Local functions...
 */

static void	show_settings(int num_settings, cups_option_t *settings);


/*
 * 'main()' - Main entry.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line args */
     char *argv[])			/* I - Command-line arguments */
{
  int		i,			/* Looping var */
		num_settings;		/* Number of settings */
  cups_option_t	*settings;		/* Settings */
  http_t	*http;			/* Connection to server */


 /*
  * Connect to the server using the defaults...
  */

  http = httpConnect2(cupsServer(), ippPort(), NULL, AF_UNSPEC,
                      cupsEncryption(), 1, 30000, NULL);

 /*
  * Set the current configuration if we have anything on the command-line...
  */

  if (argc > 1)
  {
    for (i = 1, num_settings = 0, settings = NULL; i < argc; i ++)
      num_settings = cupsParseOptions(argv[i], num_settings, &settings);

    if (cupsAdminSetServerSettings(http, num_settings, settings))
    {
      puts("New server settings:");
      cupsFreeOptions(num_settings, settings);
    }
    else
    {
      printf("Server settings not changed: %s\n", cupsLastErrorString());
      return (1);
    }
  }
  else
    puts("Current server settings:");

 /*
  * Get the current configuration...
  */

  if (cupsAdminGetServerSettings(http, &num_settings, &settings))
  {
    show_settings(num_settings, settings);
    cupsFreeOptions(num_settings, settings);
    return (0);
  }
  else
  {
    printf("    %s\n", cupsLastErrorString());
    return (1);
  }
}


/*
 * 'show_settings()' - Show settings in the array...
 */

static void
show_settings(
    int           num_settings,		/* I - Number of settings */
    cups_option_t *settings)		/* I - Settings */
{
  while (num_settings > 0)
  {
    printf("    %s=%s\n", settings->name, settings->value);

    settings ++;
    num_settings --;
  }
}
