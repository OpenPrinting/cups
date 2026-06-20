/*
 * PackBits functions for CUPS.
 *
 * Copyright © 2026 by OpenPrinting.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#ifndef PACKBITS_H
#  define PACKBITS_H
#  include <stdlib.h>


/*
 * 'packbits_alloc()' - Allocate a PackBits compression buffer.
 *
 * Free the returned pointer with `free()`...
 */

unsigned char *				/* O - Pointer to compression buffer */
packbits_alloc(size_t len)		/* I - Size of input buffer */
{
  return (malloc(len + (len + 127) / 128));
}


/*
 * 'packbits_compress()' - PackBits compress some bytes to the destination buffer.
 *
 * The destination should be allocated wth `lprintPackBitsAlloc()`.
 * The algorithm is defined in many places, including at
 * <https://en.wikipedia.org/wiki/PackBits>.
 */

size_t					/* O - Number of compressed bytes */
packbits_compress(
    unsigned char       *dst,		/* I - Destination buffer */
    const unsigned char *src,		/* I - Source buffer */
    size_t              srclen)		/* I - Number of source bytes (at least 3) */
{
  const unsigned char	*srcptr,	/* Current byte pointer */
			*srcend,	/* End-of-line byte pointer */
			*srclptr,	/* Start of literal sequence */
			*srcrptr;	/* Start of repeated sequence */
  unsigned char		*dstptr;	/* Pointer into compression buffer */
  unsigned		count,		/* Current count */
			srclcount,	/* Count of literal bytes for output */
			srcrcount;	/* Count of repeated bytes for output */


 /*
  * Do TIFF PackBits compression over the source buffer...
  */

  srcptr = srclptr = src;
  srcend = src + srclen - 1;
  dstptr = dst;

  while (srclptr <= srcend)
  {
   /*
    * Scan for literal and repeated sequences...
    */

    srclcount = srcptr - srclptr;
    srcrcount = 0;

    while (srcptr <= srcend)
    {
     /*
      * Extend literal sequence, if any...
      */

      while (srcptr < srcend && srcptr[0] != srcptr[1])
	srcptr ++;

      srclcount = srcptr - srclptr;
      srcrcount = 0;

      if (srcptr == srcend)
      {
       /*
        * Last byte, stop here...
        */

	srcptr ++;
	srclcount ++;
	break;
      }

     /*
      * Count a run...
      */

      srcrptr = srcptr;

      while (srcptr < srcend && srcptr[0] == srcptr[1])
      {
	srcptr ++;
	srcrcount ++;
      }

      srcptr ++;
      srcrcount ++;

     /*
      * Only stop to encode if the repeated sequence is long enough to make
      * sense...
      */

      if (srcrcount > 2 || srcrptr == srclptr)
        break;
    }

   /*
    * Encode literal byte sequences...
    */

    while (srclcount > 0)
    {
      if (srclcount > 128)
	count = 128;
      else
	count = srclcount;

      *dstptr++ = (unsigned char)(count - 1);
      memcpy(dstptr, srclptr, count);
      dstptr += count;
      srclptr += count;
      srclcount -= count;
    }

   /*
    * Encode repeated byte sequences...
    */

    while (srcrcount > 1)
    {
      if (srcrcount > 128)
	count = 128;
      else
	count = srcrcount;

      *dstptr++ = (unsigned char)(257 - count);
      *dstptr++ = *srcrptr;
      srcrcount -= count;
    }

   /*
    * Reset the literal pointer and continue...
    */

    srclptr = srcptr - srcrcount;
  }

  return ((size_t)(dstptr - dst));
}
#endif // !PACKBITS_H
