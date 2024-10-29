//
// Monotonic clock test program for CUPS.
//
// Copyright © 2024 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "cups-private.h"
#include <math.h>


//
// 'main()' - Main entry for clock tests.
//

int					// O - Exit status
main(void)
{
  double	current;	// Current time
  int		status = 0;	// statusurn value


  // Test clock values at 0, 1, 5, 10, and 30 seconds
  fputs("_cupsGetClock(initial):", stdout);
  current = _cupsGetClock();
  if (current == 0.0)
    printf("PASS: %g\n", current);
  else
  {
    printf("FAIL: got %g, expected 0.0\n", current);
    status ++;
  }

  sleep(1);

  fputs("_cupsGetClock(1 second):", stdout);
  current = _cupsGetClock();
  if (fabs(current - 1.0) < 0.1)
    printf("PASS: %g\n", current);
  else
  {
    printf("FAIL: got %g, expected 1.0 +/- 0.1\n", current);
    status ++;
  }

  sleep(4);

  fputs("_cupsGetClock(5 second):", stdout);
  current = _cupsGetClock();
  if (fabs(current - 5.0) < 0.1)
    printf("PASS: %g\n", current);
  else
  {
    printf("FAIL: got %g, expected 5.0 +/- 0.1\n", current);
    status ++;
  }

  sleep(5);

  fputs("_cupsGetClock(10 second):", stdout);
  current = _cupsGetClock();
  if (fabs(current - 10.0) < 0.1)
    printf("PASS: %g\n", current);
  else
  {
    printf("FAIL: got %g, expected 10.0 +/- 0.1\n", current);
    status ++;
  }

  sleep(20);

  fputs("_cupsGetClock(30 second):", stdout);
  current = _cupsGetClock();
  if (fabs(current - 30.0) < 0.1)
    printf("PASS: %g\n", current);
  else
  {
    printf("FAIL: got %g, expected 30.0 +/- 0.1\n", current);
    status ++;
  }

  sleep(30);

  fputs("_cupsGetClock(60 second):", stdout);
  current = _cupsGetClock();
  if (fabs(current - 60.0) < 0.1)
    printf("PASS: %g\n", current);
  else
  {
    printf("FAIL: got %g, expected 60.0 +/- 0.1\n", current);
    status ++;
  }

  sleep(60);

  fputs("_cupsGetClock(120 second):", stdout);
  current = _cupsGetClock();
  if (fabs(current - 120.0) < 0.1)
    printf("PASS: %g\n", current);
  else
  {
    printf("FAIL: got %g, expected 120.0 +/- 0.1\n", current);
    status ++;
  }

  return (status ? 1 : 0);
}
