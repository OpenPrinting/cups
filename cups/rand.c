//
// Random number function for CUPS.
//
// Copyright © 2019-2022 by Michael R Sweet.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "cups.h"
#if !defined(_WIN32) && !defined(__APPLE__)
#  include <unistd.h>
#  include <fcntl.h>
#  include <pthread.h>
#endif // !_WIN32 && !__APPLE__


//
// 'cupsGetRand()' - Return a 32-bit pseudo-random number.
//
// This function returns a 32-bit pseudo-random number suitable for use as
// one-time identifiers or nonces.  The random numbers are generated/seeded
// using system entropy.
//

unsigned				// O - Random number
cupsGetRand(void)
{
#if _WIN32
  // rand_s uses real entropy...
  unsigned v;				// Random number


  rand_s(&v);

  return (v);

#elif defined(__APPLE__)
  // macOS/iOS arc4random() uses real entropy automatically...
  return (arc4random());

#else
  // Use a Mersenne twister random number generator seeded from /dev/urandom...
  unsigned	i,			// Looping var
		temp;			// Temporary value
  static bool	first_time = true;	// First time we ran?
  static unsigned mt_state[624],	// Mersenne twister state
		mt_index;		// Mersenne twister index
  static pthread_mutex_t mt_mutex = PTHREAD_MUTEX_INITIALIZER;
					// Mutex to control access to state


  pthread_mutex_lock(&mt_mutex);

  if (first_time)
  {
    int		fd;			// "/dev/urandom" file
    struct timeval curtime;		// Current time

    // Seed the random number state...
    if ((fd = open("/dev/urandom", O_RDONLY)) >= 0)
    {
      // Read random entropy from the system...
      if (read(fd, mt_state, sizeof(mt_state[0])) < sizeof(mt_state[0]))
        mt_state[0] = 0;		// Force fallback...

      close(fd);
    }
    else
      mt_state[0] = 0;

    if (!mt_state[0])
    {
      // Fallback to using the current time in microseconds...
      gettimeofday(&curtime, NULL);
      mt_state[0] = (unsigned)(curtime.tv_sec + curtime.tv_usec);
    }

    mt_index = 0;

    for (i = 1; i < 624; i ++)
      mt_state[i] = (unsigned)((1812433253 * (mt_state[i - 1] ^ (mt_state[i - 1] >> 30))) + i);

    first_time = false;
  }

  if (mt_index == 0)
  {
    // Generate a sequence of random numbers...
    unsigned i1 = 1, i397 = 397;	// Looping vars

    for (i = 0; i < 624; i ++)
    {
      temp        = (mt_state[i] & 0x80000000) + (mt_state[i1] & 0x7fffffff);
      mt_state[i] = mt_state[i397] ^ (temp >> 1);

      if (temp & 1)
	mt_state[i] ^= 2567483615u;

      i1 ++;
      i397 ++;

      if (i1 == 624)
	i1 = 0;

      if (i397 == 624)
	i397 = 0;
    }
  }

  // Pull 32-bits of random data...
  temp = mt_state[mt_index ++];
  temp ^= temp >> 11;
  temp ^= (temp << 7) & 2636928640u;
  temp ^= (temp << 15) & 4022730752u;
  temp ^= temp >> 18;

  if (mt_index == 624)
    mt_index = 0;

  pthread_mutex_unlock(&mt_mutex);

  return (temp);
#endif // _WIN32
}
