//
// Monotonic clock test program for CUPS.
//
// Copyright © 2024 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "cups.h"
#include "test-internal.h"
#include <math.h>


//
// 'main()' - Main entry for clock tests.
//

int					// O - Exit status
main(void)
{
  double	current;		// Current time


  // Test clock values at 0, 1, 5, 10, and 30 seconds
  testBegin("cupsGetClock(initial)");
  current = cupsGetClock();
  if (current == 0.0)
    testEndMessage(true, "%g", current);
  else
    testEndMessage(false, "got %g, expected 0.0", current);

  sleep(1);

  testBegin("cupsGetClock(1 second)");
  current = cupsGetClock();
  if (fabs(current - 1.0) < 0.5)
    testEndMessage(true, "%g", current);
  else
    testEndMessage(false, "got %g, expected 1.0 +/- 0.5", current);

  sleep(4);

  testBegin("cupsGetClock(5 seconds)");
  current = cupsGetClock();
  if (fabs(current - 5.0) < 1.0)
    testEndMessage(true, "%g", current);
  else
    testEndMessage(false, "got %g, expected 5.0 +/- 1.0", current);

  sleep(5);

  testBegin("cupsGetClock(10 seconds)");
  current = cupsGetClock();
  if (fabs(current - 10.0) < 1.0)
    testEndMessage(true, "%g", current);
  else
    testEndMessage(false, "got %g, expected 10.0 +/- 1.0", current);

  sleep(20);

  testBegin("cupsGetClock(30 seconds)");
  current = cupsGetClock();
  if (fabs(current - 30.0) < 1.0)
    testEndMessage(true, "%g", current);
  else
    testEndMessage(false, "got %g, expected 30.0 +/- 1.0", current);

  sleep(30);

  testBegin("cupsGetClock(60 seconds)");
  current = cupsGetClock();
  if (fabs(current - 60.0) < 2.0)
    testEndMessage(true, "%g", current);
  else
    testEndMessage(false, "got %g, expected 60.0 +/- 2.0", current);

  sleep(60);

  testBegin("cupsGetClock(120 seconds)");
  current = cupsGetClock();
  if (fabs(current - 120.0) < 2.0)
    testEndMessage(true, "%g", current);
  else
    testEndMessage(false, "got %g, expected 120.0 +/- 2.0", current);

  return (testsPassed ? 0 : 1);
}
