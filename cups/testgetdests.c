//
// CUPS cupsGetDests API test program for CUPS.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright © 2017 by Apple Inc.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "cups.h"
#include "test-internal.h"


//
// 'main()' - Loop calling cupsGetDests.
//

int                                     // O - Exit status
main(int  argc,				// I - Number of command-line arguments
     char *argv[])			// I - Command-line arguments
{
  size_t	count = 1;		// Number of times
  size_t	num_dests;		// Number of destinations
  cups_dest_t	*dests;			// Destinations
  struct timeval start, end;		// Start and stop time
  double	secs;			// Total seconds to run cupsGetDests


  // Parse command-line...
  if (argc > 2 || (argc == 2 && (argv[1][0] < '1' || argv[1][0] > '9')))
  {
    fputs("Usage: ./testgetdests [COUNT]\n", stderr);
    return (1);
  }

  if (argc == 2)
    count = strtoul(argv[1], NULL, 10);
  else
    count = 5;

  // Run tests...
  while (count > 0)
  {
    testBegin("cupsGetDests");
    gettimeofday(&start, NULL);
    num_dests = cupsGetDests2(CUPS_HTTP_DEFAULT, &dests);
    gettimeofday(&end, NULL);
    secs = end.tv_sec - start.tv_sec + 0.000001 * (end.tv_usec - start.tv_usec);

    if (cupsGetError() != IPP_STATUS_OK)
      testEndMessage(false, "%s", cupsGetErrorString());
    else
      testEndMessage(secs < 2.0, "%u printers in %.3f seconds", (unsigned)num_dests, secs);

    cupsFreeDests(num_dests, dests);

    count --;

    if (count > 0)
      sleep(1);
  }

  return (testsPassed ? 0 : 1);
}
