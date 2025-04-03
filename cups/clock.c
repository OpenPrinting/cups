//
// Monotonic clock API for CUPS.
//
// Copyright © 2024-2025 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "cups-private.h"


//
// Local globals...
//

static bool	cups_clock_init = false;// Clock initialized?
static _cups_mutex_t cups_clock_mutex = _CUPS_MUTEX_INITIALIZER;
					// Mutex to control access
#ifdef _WIN32
static ULONGLONG cups_first_tick;	// First tick count
#else
#  if defined(CLOCK_MONOTONIC) || defined(CLOCK_MONOTONIC_RAW)
static struct timespec cups_first_clock;// First clock value
#  endif // CLOCK_MONOTONIC || CLOCK_MONOTONIC_RAW
static struct timeval cups_first_time;	// First time value
#endif // _WIN32


//
// '_cupsGetClock()' - Get a monotonic clock value in seconds.
//
// This function returns a monotonically increasing clock value in seconds.  The
// first call will always return 0.0.  Subsequent calls will return the number
// of seconds that have elapsed since the first call, regardless of system time
// changes, sleep, etc.  The sub-second accuracy varies based on the operating
// system and hardware but is typically 10ms or better.
//

double					// O - Elapsed seconds
_cupsGetClock(void)
{
  double	secs;			// Elapsed seconds
#ifdef _WIN32
  ULONGLONG	curtick;		// Current tick count
#else
#  ifdef CLOCK_MONOTONIC
  struct timespec curclock;		// Current clock value
#  endif // CLOCK_MONOTONIC
  struct timeval curtime;		// Current time value
#endif // _WIN32


  _cupsMutexLock(&cups_clock_mutex);

#ifdef _WIN32
  // Get the current tick count in milliseconds...
  curtick = GetTickCount64();

  if (!cups_clock_init)
  {
    // First time through initialize the initial tick count...
    cups_clock_init = true;
    cups_first_tick = curtick;
  }

  // Convert ticks to seconds...
  if (curtick < cups_first_tick)
    secs = 0.0;
  else
    secs = 0.001 * (curtick - cups_first_tick);

#else
#  if defined(CLOCK_MONOTONIC) || defined(CLOCK_MONOTONIC_RAW)
  // Get the current tick count in milliseconds...
#    ifdef CLOCK_MONOTONIC_RAW
  if (!clock_gettime(CLOCK_MONOTONIC_RAW, &curclock))
#    else
  if (!clock_gettime(CLOCK_MONOTONIC, &curclock))
#    endif // CLOCK_MONOTONIC_RAW
  {
    if (!cups_clock_init)
    {
      // First time through initialize the initial clock value...
      cups_clock_init  = true;
      cups_first_clock = curclock;
    }

    // Convert clock value to seconds...
    if ((secs = curclock.tv_sec - cups_first_clock.tv_sec + 0.000000001 * (curclock.tv_nsec - cups_first_clock.tv_nsec)) < 0.0)
      secs = 0.0;
  }
  else
#  endif // CLOCK_MONOTONIC || CLOCK_MONOTONIC_RAW
  {
    gettimeofday(&curtime, /*tzp*/NULL);

    if (!cups_clock_init)
    {
      // First time through initialize the initial clock value...
      cups_clock_init = true;
      cups_first_time = curtime;
    }

    // Convert time value to seconds...
    if ((secs = curtime.tv_sec - cups_first_time.tv_sec + 0.000001 * (curtime.tv_usec - cups_first_time.tv_usec)) < 0.0)
      secs = 0.0;
  }
#endif // _WIN32

  _cupsMutexUnlock(&cups_clock_mutex);

  return (secs);
}
