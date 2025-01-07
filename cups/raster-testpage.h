//
// Raster test page generator for CUPS.
//
// Copyright © 2020-2023 by OpenPrinting
// Copyright © 2017-2019 by Apple Inc.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "raster-private.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>


//
// 'cupsRasterWriteTest()' - Write a series of raster test pages.
//
// This function writes a series of raster test pages to the specified raster
// stream.  You must call @link cupsRasterInitPWGHeader@ to create the raster
// page header and call @link cupsRasterOpen@ or @link cupsRasterOpenIO@ to create
// a stream for writing prior to calling this function.
//
// Each page consists of a black border (1/4 to 1/2" in width depending on the
// media size) with the text "TEST-PAGE ####" repeated down the page in
// different shades of gray or colors.  When generating multiple pages, the
// proper back side transforms are applied for duplex printing as needed.
//

static bool				// O - `true` on success, `false` on failure
cupsRasterWriteTest(
    cups_raster_t       *ras,		// I - Raster stream
    cups_page_header2_t *header,	// I - Raster page header (front side)
    cups_page_header2_t *back_header,	// I - Raster page header (back side)
    const char          *sheet_back,	// I - Back side transform needed
    ipp_orient_t        orientation,	// I - Output orientation
    int	                num_copies,	// I - Number of copies
    int                 num_pages)	// I - Number of pages
{
  int			copy,		// Current copy number
			page;		// Current page number
  char			pagestr[5],	// Page number string
			output[8][101];	// Output image
  unsigned char		*line,		// Line of raster data
			*bline,		// Border line in raster data
			*lineptr,	// Pointer into line
			*lineend,	// Pointer to end of line
			black,		// Black pixel
			white;		// White pixel
  unsigned		bpp,		// Bytes per pixel
			x, y,		// Current position on page
			xcount, ycount,	// Current count for X and Y
			xrep, yrep,	// Repeat count for X and Y
			xborder,	// X border
			yborder,	// Y border
			xoff, yoff,	// X and Y offsets
			xend, yend,	// End X and Y values
			yend2,		// End Y value for solid border
			rows;		// Number of rows
  int			col, row,	// Column and row in output
			color;		// Template color
  ipp_orient_t		porientation;	// Current page orientation
  bool			pflip;		// Current page (vertical) flip
  const char		*outptr;	// Pointer into output image line
  const unsigned char	*colorptr;	// Current color
  static const unsigned char colors[][3] =
  {					// Colors for test
    {   0,   0,   0 },
    {  63,  63,  63 },
    { 127, 127, 127 },
    { 191, 191, 191 },
    { 255,   0,   0 },
    { 255, 127,   0 },
    { 255, 191,   0 },
    { 255, 255,   0 },
    { 191, 255,   0 },
    {   0, 255,   0 },
    {   0, 255, 191 },
    {   0, 255, 255 },
    {   0, 191, 255 },
    {   0,   0, 255 },
    { 127,   0, 255 },
    { 255,   0, 255 }
  };
  static const char * const test_page[] =
  {                                     // "TEST-PAGE" template
    "  TTTTT  EEEEE  SSSSS  TTTTT         PPPPP  AAAAA  GGGGG  EEEEE         ",
    "    T    E      S   S    T           P   P  A   A  G      E             ",
    "    T    E      S        T           P   P  A   A  G      E             ",
    "    T    EEEE   SSSSS    T    -----  PPPPP  AAAAA  G  GG  EEEE          ",
    "    T    E          S    T           P      A   A  G   G  E             ",
    "    T    E      S   S    T           P      A   A  G   G  E             ",
    "    T    EEEEE  SSSSS    T           P      A   A  GGGGG  EEEEE         ",
    "                                                                        "
  };
  static const char * const digits[] =	// Digits template
  {
    "00000    1    22222  33333     4   55555  6666   77777  88888  99999  ",
    "0   0    1        2      3  4  4   5      6          7  8   8  9   9  ",
    "0   0    1        2      3  4  4   5      6          7  8   8  9   9  ",
    "0 0 0    1    22222   3333  44444  55555  66666      7  88888  99999  ",
    "0   0    1    2          3     4       5  6   6      7  8   8      9  ",
    "0   0    1    2          3     4       5  6   6      7  8   8      9  ",
    "00000    1    22222  33333     4   55555  66666      7  88888   9999  ",
    "                                                                      "
  };


  // Update the page header->..
  header->cupsInteger[CUPS_RASTER_PWG_TotalPageCount] = (unsigned)(num_copies * num_pages);

  // Calculate the border sizes and offsets...
  if (header->cupsWidth > (2 * header->HWResolution[0]) && header->cupsHeight > (2 * header->HWResolution[1]))
  {
    xborder = header->HWResolution[0] / 2;
    yborder = header->HWResolution[1] / 2;
  }
  else
  {
    xborder = header->HWResolution[0] / 4;
    yborder = header->HWResolution[1] / 4;
  }

  if (orientation == IPP_ORIENT_PORTRAIT || orientation == IPP_ORIENT_REVERSE_PORTRAIT)
  {
    xrep = (header->cupsWidth - 2 * xborder) / 100;
    yrep = xrep * header->HWResolution[1] / header->HWResolution[0];
    rows = (header->cupsHeight - 3 * yborder) / yrep / 8;
    xoff = (header->cupsWidth - 100 * xrep) / 2;

    if (rows)
      yoff = (header->cupsHeight - rows * 8 * yrep) / 2;
    else
      yoff = yborder + yrep / 2;
  }
  else
  {
    yrep = (header->cupsHeight - 2 * yborder) / 100;
    xrep = yrep * header->HWResolution[0] / header->HWResolution[1];
    rows = (header->cupsWidth - 3 * xborder) / xrep / 8;
    yoff = (header->cupsHeight - 100 * yrep) / 2;

    if (rows)
      xoff = (header->cupsWidth - rows * 8 * xrep) / 2;
    else
      xoff = xborder + xrep / 2;
  }

  xend  = header->cupsWidth - xoff;
  yend  = header->cupsHeight - yoff;
  yend2 = header->cupsHeight - yborder;

  // Allocate memory for the raster output...
  if ((line = malloc(header->cupsBytesPerLine)) == NULL)
  {
    _cupsRasterAddError("Unable to allocate %u bytes for line: %s", header->cupsBytesPerLine, strerror(errno));
    return (false);
  }

  if ((bline = malloc(header->cupsBytesPerLine)) == NULL)
  {
    _cupsRasterAddError("Unable to allocate %u bytes for line: %s", header->cupsBytesPerLine, strerror(errno));
    free(line);
    return (false);
  }

  switch (header->cupsColorSpace)
  {
    default :
        black = 0x00;
        white = 0xff;
        break;

    case CUPS_CSPACE_K :
    case CUPS_CSPACE_CMYK :
        black = 0xff;
        white = 0x00;
        break;
  }

  bpp     = header->cupsBitsPerPixel / 8;
  lineend = line + header->cupsBytesPerLine;

  // Loop to create all copies and pages...
  for (copy = 0; copy < num_copies; copy ++)
  {
    for (page = 0; page < num_pages; page ++)
    {
      // Format the output rows for "TEST-PAGE ####"
      memset(output, 0, sizeof(output));
      snprintf(pagestr, sizeof(pagestr), "%04d", page + 1);

      for (row = 0; row < 8; row ++)
      {
        // Base "TEST-PAGE"
        memcpy(output[row], test_page[row], 72);
        for (col = 0; col < 4; col ++)
          memcpy(output[row] + 72 + col * 7, digits[row] + (pagestr[col] - '0') * 7, 7);
      }

      // Start the page and show the borders...
      if (page & 1)
	cupsRasterWriteHeader2(ras, back_header);
      else
	cupsRasterWriteHeader2(ras, header);

      if (bpp == 4)
      {
        // 32-bit CMYK output
        for (lineptr = line; lineptr < lineend;)
        {
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0xff;
        }
      }
      else if (bpp == 8)
      {
        // 64-bit CMYK output
        for (lineptr = line; lineptr < lineend;)
        {
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0xff;
          *lineptr++ = 0xff;
        }
      }
      else
      {
        // 1/8/16/24/32-bit bitmap/grayscale/color output...
        memset(line, black, header->cupsBytesPerLine);
      }

      for (y = 0; y < yborder; y ++)
	cupsRasterWritePixels(ras, line, header->cupsBytesPerLine);

      memset(bline, white, header->cupsBytesPerLine);
      if (bpp == 4)
      {
        // 32-bit CMYK output
        for (lineptr = bline, xcount = xborder; xcount > 0; xcount --)
        {
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0xff;
        }

        for (lineptr = bline + header->cupsBytesPerLine - xborder * 4, xcount = xborder; xcount > 0; xcount --)
        {
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0xff;
        }
      }
      else if (bpp == 8)
      {
        // 64-bit CMYK output
        for (lineptr = bline, xcount = xborder; xcount > 0; xcount --)
        {
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0xff;
          *lineptr++ = 0xff;
        }

        for (lineptr = bline + header->cupsBytesPerLine - xborder * 8, xcount = xborder; xcount > 0; xcount --)
        {
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0xff;
          *lineptr++ = 0xff;
        }
      }
      else if (bpp)
      {
        // 8/16/24/32-bit grayscale/color output...
        memset(bline, black, xborder * bpp);
        memset(bline + header->cupsBytesPerLine - xborder * bpp, black, xborder * bpp);
      }
      else
      {
        // Bitmap output...
        if (xborder >= 8)
        {
          memset(bline, black, xborder / 8);
          memset(bline + header->cupsBytesPerLine - xborder / 8, black, xborder / 8);
        }
        if (xborder & 7)
        {
          // Capture partial pixels
          bline[xborder / 8] ^= (0xff << (xborder & 7)) & 0xff;
          bline[header->cupsBytesPerLine - xborder / 8 - 1] ^= 0xff >> (xborder & 7);
        }
      }

      for (; y < yoff; y ++)
	cupsRasterWritePixels(ras, bline, header->cupsBytesPerLine);

      // Generate the interior lines...
      if (header->Duplex && (page & 1) != 0)
      {
        // Update orientation for back side
	if (!strcmp(sheet_back, "normal"))
	{
	  porientation = orientation;
	  pflip        = false;
	}
	else if (!strcmp(sheet_back, "rotated"))
	{
	  if (header->Tumble)
	    porientation = orientation;
	  else
	    porientation = (ipp_orient_t)(9 - orientation);

	  pflip = false;
	}
	else if (!strcmp(sheet_back, "manual-tumble"))
	{
	  if (header->Tumble)
	    porientation = (ipp_orient_t)(9 - orientation);
	  else
	    porientation = orientation;

	  pflip = false;
	}
	else // flipped
	{
	  porientation = orientation;
	  pflip        = true;
	}
      }
      else
      {
        // Use front side orientation
        porientation = orientation;
        pflip        = false;
      }

      if (pflip)
      {
        // Draw the test image from bottom to top
	switch (porientation)
	{
	  default :
	  case IPP_ORIENT_PORTRAIT :
	      color = (int)rows - 1;
	      if (bpp <= 2)
		color &= 3;
	      else
		color &= 15;

	      for (row = 7; y < yend;)
	      {
                // Write N scan lines...
	        for (ycount = yrep; ycount > 0 && y < yend; ycount --, y ++)
	        {
		  // Format the current line in the output row...
		  memcpy(line, bline, header->cupsBytesPerLine);
		  colorptr = colors[color];

		  for (outptr = output[row], x = xoff; *outptr; outptr ++, x += xrep)
		  {
		    unsigned char	bit,	// Current bit
					pattern;// Shading pattern

		    if (*outptr == ' ')
		      continue;

                    switch (bpp)
                    {
                      case 0 : // 1-bit bitmap output
			  if (*colorptr < 63)
			  {
			    pattern = 0xff;
			  }
			  else if (*colorptr < 127)
			  {
			    pattern = (y & 1) ? 0x55 : 0xff;
			  }
			  else if (*colorptr < 191)
			  {
			    pattern = (y & 1) ? 0x55 : 0xaa;
			  }
			  else if (y & 1)
			  {
			    break;
			  }
			  else
			  {
			    pattern = 0xaa;
			  }

			  lineptr = line + x / 8;
			  bit     = 0x80 >> (x & 7);

			  for (xcount = xrep; xcount > 0; xcount --)
                          {
                            *lineptr ^= bit & pattern;
                            if (bit > 1)
                            {
                              bit /= 2;
                            }
                            else
                            {
                              bit = 0x80;
                              lineptr ++;
                            }
                          }
			  break;
		      case 1 : // 8-bit grayscale/black
		          if (black)
			    memset(line + x, 255 - *colorptr, xrep);
			  else
			    memset(line + x, *colorptr, xrep);
		          break;
		      case 2 : // 16-bit grayscale/black
		          if (black)
			    memset(line + 2 * x, 255 - *colorptr, 2 * xrep);
			  else
			    memset(line + 2 * x, *colorptr, 2 * xrep);
		          break;
		      case 3 : // 24-bit RGB
		          for (lineptr = line + 3 * x, xcount = xrep; xcount > 0; xcount --)
		          {
		            *lineptr++ = colorptr[0];
		            *lineptr++ = colorptr[1];
		            *lineptr++ = colorptr[2];
		          }
		          break;
		      case 4 : // 32-bit CMYK
		          for (lineptr = line + 4 * x, xcount = xrep; xcount > 0; xcount --)
		          {
		            if (color < 4)
		            {
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 255 - colorptr[0];
		            }
		            else
		            {
			      *lineptr++ = 255 - colorptr[0];
			      *lineptr++ = 255 - colorptr[1];
			      *lineptr++ = 255 - colorptr[2];
			      *lineptr++ = 0;
		            }
		          }
		          break;
		      case 6 : // 24-bit RGB
		          for (lineptr = line + 6 * x, xcount = xrep; xcount > 0; xcount --)
		          {
		            *lineptr++ = colorptr[0];
		            *lineptr++ = colorptr[0];
		            *lineptr++ = colorptr[1];
		            *lineptr++ = colorptr[1];
		            *lineptr++ = colorptr[2];
		            *lineptr++ = colorptr[2];
		          }
		          break;
		      case 8 : // 64-bit CMYK
		          for (lineptr = line + 8 * x, xcount = xrep; xcount > 0; xcount --)
		          {
		            if (color < 4)
		            {
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 255 - colorptr[0];
			      *lineptr++ = 255 - colorptr[0];
		            }
		            else
		            {
			      *lineptr++ = 255 - colorptr[0];
			      *lineptr++ = 255 - colorptr[0];
			      *lineptr++ = 255 - colorptr[1];
			      *lineptr++ = 255 - colorptr[1];
			      *lineptr++ = 255 - colorptr[2];
			      *lineptr++ = 255 - colorptr[2];
			      *lineptr++ = 0;
			      *lineptr++ = 0;
		            }
		          }
		          break;
                    }
		  }

	          cupsRasterWritePixels(ras, line, header->cupsBytesPerLine);
	        }

		// Next row in the output image...
		row --;
		if (row < 0)
		{
		  // New row of text with a new color/gray shade...
		  row = 7;
		  color --;
		  if (color < 0)
		  {
		    if (bpp > 2)
		      color = 15;
		    else
		      color = 3;
		  }
		}
	      }
	      break;

	  case IPP_ORIENT_LANDSCAPE :
	      for (col = 99; col >= 0; col --)
	      {
		// Write N scan lines...
		for (ycount = yrep; ycount > 0 && y < yend; ycount --, y ++)
		{
		  memcpy(line, bline, header->cupsBytesPerLine);

		  color = (int)rows - 1;
		  if (bpp <= 2)
		    color &= 3;
		  else
		    color &= 15;

		  for (row = (rows - 1) & 7, x = xoff; x < xend; x += xrep)
		  {
		    // Format the current line in the output row...
		    unsigned char bit,	// Current bit
				pattern;// Shading pattern

		    colorptr = colors[color];

                    if (output[row][col] != ' ')
                    {
		      switch (bpp)
		      {
			case 0 : // 1-bit bitmap output
			    if (*colorptr < 63)
			    {
			      pattern = 0xff;
			    }
			    else if (*colorptr < 127)
			    {
			      pattern = (y & 1) ? 0x55 : 0xff;
			    }
			    else if (*colorptr < 191)
			    {
			      pattern = (y & 1) ? 0x55 : 0xaa;
			    }
			    else if (y & 1)
			    {
			      break;
			    }
			    else
			    {
			      pattern = 0xaa;
			    }

			    lineptr = line + x / 8;
			    bit     = 0x80 >> (x & 7);

			    for (xcount = xrep; xcount > 0; xcount --)
			    {
			      *lineptr ^= bit & pattern;
			      if (bit > 1)
			      {
				bit /= 2;
			      }
			      else
			      {
				bit = 0x80;
				lineptr ++;
			      }
			    }
			    break;
			case 1 : // 8-bit grayscale/black
			    if (black)
			      memset(line + x, 255 - *colorptr, xrep);
			    else
			      memset(line + x, *colorptr, xrep);
			    break;
			case 2 : // 16-bit grayscale/black
			    if (black)
			      memset(line + 2 * x, 255 - *colorptr, 2 * xrep);
			    else
			      memset(line + 2 * x, *colorptr, 2 * xrep);
			    break;
			case 3 : // 24-bit RGB
			    for (lineptr = line + 3 * x, xcount = xrep; xcount > 0; xcount --)
			    {
			      *lineptr++ = colorptr[0];
			      *lineptr++ = colorptr[1];
			      *lineptr++ = colorptr[2];
			    }
			    break;
			case 4 : // 32-bit CMYK
			    for (lineptr = line + 4 * x, xcount = xrep; xcount > 0; xcount --)
			    {
			      if (color < 4)
			      {
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 255 - colorptr[0];
			      }
			      else
			      {
				*lineptr++ = 255 - colorptr[0];
				*lineptr++ = 255 - colorptr[1];
				*lineptr++ = 255 - colorptr[2];
				*lineptr++ = 0;
			      }
			    }
			    break;
			case 6 : // 24-bit RGB
			    for (lineptr = line + 6 * x, xcount = xrep; xcount > 0; xcount --)
			    {
			      *lineptr++ = colorptr[0];
			      *lineptr++ = colorptr[0];
			      *lineptr++ = colorptr[1];
			      *lineptr++ = colorptr[1];
			      *lineptr++ = colorptr[2];
			      *lineptr++ = colorptr[2];
			    }
			    break;
			case 8 : // 64-bit CMYK
			    for (lineptr = line + 8 * x, xcount = xrep; xcount > 0; xcount --)
			    {
			      if (color < 4)
			      {
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 255 - colorptr[0];
				*lineptr++ = 255 - colorptr[0];
			      }
			      else
			      {
				*lineptr++ = 255 - colorptr[0];
				*lineptr++ = 255 - colorptr[0];
				*lineptr++ = 255 - colorptr[1];
				*lineptr++ = 255 - colorptr[1];
				*lineptr++ = 255 - colorptr[2];
				*lineptr++ = 255 - colorptr[2];
				*lineptr++ = 0;
				*lineptr++ = 0;
			      }
			    }
			    break;
		      }
		    }

		    // Next row in the output image...
		    row --;
		    if (row < 0)
		    {
		      // New row of text with a new color/gray shade...
		      row = 7;
		      color --;
		      if (color < 0)
		      {
			if (bpp > 2)
			  color = 15;
			else
			  color = 3;
		      }
		    }
		  }

		  cupsRasterWritePixels(ras, line, header->cupsBytesPerLine);
		}
	      }
	      break;

	  case IPP_ORIENT_REVERSE_PORTRAIT :
	      for (row = 0, color = 0; y < yend;)
	      {
                // Write N scan lines...
	        for (ycount = yrep; ycount > 0 && y < yend; ycount --, y ++)
	        {
		  // Format the current line in the output row...
		  memcpy(line, bline, header->cupsBytesPerLine);
		  colorptr = colors[color];

		  for (outptr = output[row] + 99, x = xoff; outptr >= output[row]; outptr --, x += xrep)
		  {
		    unsigned char	bit,	// Current bit
					pattern;// Shading pattern

		    if (*outptr == ' ')
		      continue;

                    switch (bpp)
                    {
                      case 0 : // 1-bit bitmap output
			  if (*colorptr < 63)
			  {
			    pattern = 0xff;
			  }
			  else if (*colorptr < 127)
			  {
			    pattern = (y & 1) ? 0x55 : 0xff;
			  }
			  else if (*colorptr < 191)
			  {
			    pattern = (y & 1) ? 0x55 : 0xaa;
			  }
			  else if (y & 1)
			  {
			    break;
			  }
			  else
			  {
			    pattern = 0xaa;
			  }

			  lineptr = line + x / 8;
			  bit     = 0x80 >> (x & 7);

			  for (xcount = xrep; xcount > 0; xcount --)
                          {
                            *lineptr ^= bit & pattern;
                            if (bit > 1)
                            {
                              bit /= 2;
                            }
                            else
                            {
                              bit = 0x80;
                              lineptr ++;
                            }
                          }
			  break;
		      case 1 : // 8-bit grayscale/black
		          if (black)
			    memset(line + x, 255 - *colorptr, xrep);
			  else
			    memset(line + x, *colorptr, xrep);
		          break;
		      case 2 : // 16-bit grayscale/black
		          if (black)
			    memset(line + 2 * x, 255 - *colorptr, 2 * xrep);
			  else
			    memset(line + 2 * x, *colorptr, 2 * xrep);
		          break;
		      case 3 : // 24-bit RGB
		          for (lineptr = line + 3 * x, xcount = xrep; xcount > 0; xcount --)
		          {
		            *lineptr++ = colorptr[0];
		            *lineptr++ = colorptr[1];
		            *lineptr++ = colorptr[2];
		          }
		          break;
		      case 4 : // 32-bit CMYK
		          for (lineptr = line + 4 * x, xcount = xrep; xcount > 0; xcount --)
		          {
		            if (color < 4)
		            {
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 255 - colorptr[0];
		            }
		            else
		            {
			      *lineptr++ = 255 - colorptr[0];
			      *lineptr++ = 255 - colorptr[1];
			      *lineptr++ = 255 - colorptr[2];
			      *lineptr++ = 0;
		            }
		          }
		          break;
		      case 6 : // 24-bit RGB
		          for (lineptr = line + 6 * x, xcount = xrep; xcount > 0; xcount --)
		          {
		            *lineptr++ = colorptr[0];
		            *lineptr++ = colorptr[0];
		            *lineptr++ = colorptr[1];
		            *lineptr++ = colorptr[1];
		            *lineptr++ = colorptr[2];
		            *lineptr++ = colorptr[2];
		          }
		          break;
		      case 8 : // 64-bit CMYK
		          for (lineptr = line + 8 * x, xcount = xrep; xcount > 0; xcount --)
		          {
		            if (color < 4)
		            {
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 255 - colorptr[0];
			      *lineptr++ = 255 - colorptr[0];
		            }
		            else
		            {
			      *lineptr++ = 255 - colorptr[0];
			      *lineptr++ = 255 - colorptr[0];
			      *lineptr++ = 255 - colorptr[1];
			      *lineptr++ = 255 - colorptr[1];
			      *lineptr++ = 255 - colorptr[2];
			      *lineptr++ = 255 - colorptr[2];
			      *lineptr++ = 0;
			      *lineptr++ = 0;
		            }
		          }
		          break;
                    }
		  }

	          cupsRasterWritePixels(ras, line, header->cupsBytesPerLine);
	        }

		// Next row in the output image...
                row ++;
                if (row >= 8)
                {
                  // New row of text with a new color/gray shade...
                  row = 0;
                  color ++;
                  if ((bpp > 2 && color >= 16) || (bpp <= 2 && color >= 4))
                    color = 0;
                }
	      }
	      break;
	  case IPP_ORIENT_REVERSE_LANDSCAPE :
	      for (col = 0; col < 100; col ++)
	      {
		// Write N scan lines...
		for (ycount = yrep; ycount > 0 && y < yend; ycount --, y ++)
		{
		  memcpy(line, bline, header->cupsBytesPerLine);

		  color = 0;

		  for (row = 0, x = xoff; x < xend; x += xrep)
		  {
		    // Format the current line in the output row...
		    unsigned char bit,	// Current bit
				pattern;// Shading pattern

		    colorptr = colors[color];

                    if (output[row][col] != ' ')
                    {
		      switch (bpp)
		      {
			case 0 : // 1-bit bitmap output
			    if (*colorptr < 63)
			    {
			      pattern = 0xff;
			    }
			    else if (*colorptr < 127)
			    {
			      pattern = (y & 1) ? 0x55 : 0xff;
			    }
			    else if (*colorptr < 191)
			    {
			      pattern = (y & 1) ? 0x55 : 0xaa;
			    }
			    else if (y & 1)
			    {
			      break;
			    }
			    else
			    {
			      pattern = 0xaa;
			    }

			    lineptr = line + x / 8;
			    bit     = 0x80 >> (x & 7);

			    for (xcount = xrep; xcount > 0; xcount --)
			    {
			      *lineptr ^= bit & pattern;
			      if (bit > 1)
			      {
				bit /= 2;
			      }
			      else
			      {
				bit = 0x80;
				lineptr ++;
			      }
			    }
			    break;
			case 1 : // 8-bit grayscale/black
			    if (black)
			      memset(line + x, 255 - *colorptr, xrep);
			    else
			      memset(line + x, *colorptr, xrep);
			    break;
			case 2 : // 16-bit grayscale/black
			    if (black)
			      memset(line + 2 * x, 255 - *colorptr, 2 * xrep);
			    else
			      memset(line + 2 * x, *colorptr, 2 * xrep);
			    break;
			case 3 : // 24-bit RGB
			    for (lineptr = line + 3 * x, xcount = xrep; xcount > 0; xcount --)
			    {
			      *lineptr++ = colorptr[0];
			      *lineptr++ = colorptr[1];
			      *lineptr++ = colorptr[2];
			    }
			    break;
			case 4 : // 32-bit CMYK
			    for (lineptr = line + 4 * x, xcount = xrep; xcount > 0; xcount --)
			    {
			      if (color < 4)
			      {
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 255 - colorptr[0];
			      }
			      else
			      {
				*lineptr++ = 255 - colorptr[0];
				*lineptr++ = 255 - colorptr[1];
				*lineptr++ = 255 - colorptr[2];
				*lineptr++ = 0;
			      }
			    }
			    break;
			case 6 : // 24-bit RGB
			    for (lineptr = line + 6 * x, xcount = xrep; xcount > 0; xcount --)
			    {
			      *lineptr++ = colorptr[0];
			      *lineptr++ = colorptr[0];
			      *lineptr++ = colorptr[1];
			      *lineptr++ = colorptr[1];
			      *lineptr++ = colorptr[2];
			      *lineptr++ = colorptr[2];
			    }
			    break;
			case 8 : // 64-bit CMYK
			    for (lineptr = line + 8 * x, xcount = xrep; xcount > 0; xcount --)
			    {
			      if (color < 4)
			      {
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 255 - colorptr[0];
				*lineptr++ = 255 - colorptr[0];
			      }
			      else
			      {
				*lineptr++ = 255 - colorptr[0];
				*lineptr++ = 255 - colorptr[0];
				*lineptr++ = 255 - colorptr[1];
				*lineptr++ = 255 - colorptr[1];
				*lineptr++ = 255 - colorptr[2];
				*lineptr++ = 255 - colorptr[2];
				*lineptr++ = 0;
				*lineptr++ = 0;
			      }
			    }
			    break;
		      }
		    }

		    // Next row in the output image...
		    row ++;
		    if (row > 7)
		    {
		      // New row of text with a new color/gray shade...
		      row = 0;
		      color ++;
		      if ((bpp > 2 && color >= 16) || (bpp <= 2 && color >= 4))
			color = 0;
		    }
		  }

		  cupsRasterWritePixels(ras, line, header->cupsBytesPerLine);
		}
	      }
	      break;
	}
      }
      else
      {
        // Draw the test image from top to bottom
	switch (porientation)
	{
	  default :
	  case IPP_ORIENT_PORTRAIT :
	      for (row = 0, color = 0; y < yend;)
	      {
                // Write N scan lines...
	        for (ycount = yrep; ycount > 0 && y < yend; ycount --, y ++)
	        {
		  // Format the current line in the output row...
		  memcpy(line, bline, header->cupsBytesPerLine);
		  colorptr = colors[color];

		  for (outptr = output[row], x = xoff; *outptr; outptr ++, x += xrep)
		  {
		    unsigned char	bit,	// Current bit
					pattern;// Shading pattern

		    if (*outptr == ' ')
		      continue;

                    switch (bpp)
                    {
                      case 0 : // 1-bit bitmap output
			  if (*colorptr < 63)
			  {
			    pattern = 0xff;
			  }
			  else if (*colorptr < 127)
			  {
			    pattern = (y & 1) ? 0x55 : 0xff;
			  }
			  else if (*colorptr < 191)
			  {
			    pattern = (y & 1) ? 0x55 : 0xaa;
			  }
			  else if (y & 1)
			  {
			    break;
			  }
			  else
			  {
			    pattern = 0xaa;
			  }

			  lineptr = line + x / 8;
			  bit     = 0x80 >> (x & 7);

			  for (xcount = xrep; xcount > 0; xcount --)
                          {
                            *lineptr ^= bit & pattern;
                            if (bit > 1)
                            {
                              bit /= 2;
                            }
                            else
                            {
                              bit = 0x80;
                              lineptr ++;
                            }
                          }
			  break;
		      case 1 : // 8-bit grayscale/black
		          if (black)
			    memset(line + x, 255 - *colorptr, xrep);
			  else
			    memset(line + x, *colorptr, xrep);
		          break;
		      case 2 : // 16-bit grayscale/black
		          if (black)
			    memset(line + 2 * x, 255 - *colorptr, 2 * xrep);
			  else
			    memset(line + 2 * x, *colorptr, 2 * xrep);
		          break;
		      case 3 : // 24-bit RGB
		          for (lineptr = line + 3 * x, xcount = xrep; xcount > 0; xcount --)
		          {
		            *lineptr++ = colorptr[0];
		            *lineptr++ = colorptr[1];
		            *lineptr++ = colorptr[2];
		          }
		          break;
		      case 4 : // 32-bit CMYK
		          for (lineptr = line + 4 * x, xcount = xrep; xcount > 0; xcount --)
		          {
		            if (color < 4)
		            {
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 255 - colorptr[0];
		            }
		            else
		            {
			      *lineptr++ = 255 - colorptr[0];
			      *lineptr++ = 255 - colorptr[1];
			      *lineptr++ = 255 - colorptr[2];
			      *lineptr++ = 0;
		            }
		          }
		          break;
		      case 6 : // 24-bit RGB
		          for (lineptr = line + 6 * x, xcount = xrep; xcount > 0; xcount --)
		          {
		            *lineptr++ = colorptr[0];
		            *lineptr++ = colorptr[0];
		            *lineptr++ = colorptr[1];
		            *lineptr++ = colorptr[1];
		            *lineptr++ = colorptr[2];
		            *lineptr++ = colorptr[2];
		          }
		          break;
		      case 8 : // 64-bit CMYK
		          for (lineptr = line + 8 * x, xcount = xrep; xcount > 0; xcount --)
		          {
		            if (color < 4)
		            {
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 255 - colorptr[0];
			      *lineptr++ = 255 - colorptr[0];
		            }
		            else
		            {
			      *lineptr++ = 255 - colorptr[0];
			      *lineptr++ = 255 - colorptr[0];
			      *lineptr++ = 255 - colorptr[1];
			      *lineptr++ = 255 - colorptr[1];
			      *lineptr++ = 255 - colorptr[2];
			      *lineptr++ = 255 - colorptr[2];
			      *lineptr++ = 0;
			      *lineptr++ = 0;
		            }
		          }
		          break;
                    }
		  }

	          cupsRasterWritePixels(ras, line, header->cupsBytesPerLine);
	        }

		// Next row in the output image...
                row ++;
                if (row >= 8)
                {
                  // New row of text with a new color/gray shade...
                  row = 0;
                  color ++;
                  if ((bpp > 2 && color >= 16) || (bpp <= 2 && color >= 4))
                    color = 0;
                }
	      }
	      break;

	  case IPP_ORIENT_LANDSCAPE :
	      for (col = 0; col < 100; col ++)
	      {
		// Write N scan lines...
		for (ycount = yrep; ycount > 0 && y < yend; ycount --, y ++)
		{
		  memcpy(line, bline, header->cupsBytesPerLine);

		  color = (int)rows - 1;
		  if (bpp <= 2)
		    color &= 3;
		  else
		    color &= 15;

		  for (row = 7, x = xoff; x < xend; x += xrep)
		  {
		    // Format the current line in the output row...
		    unsigned char bit,	// Current bit
				pattern;// Shading pattern

		    colorptr = colors[color];

                    if (output[row][col] != ' ')
                    {
		      switch (bpp)
		      {
			case 0 : // 1-bit bitmap output
			    if (*colorptr < 63)
			    {
			      pattern = 0xff;
			    }
			    else if (*colorptr < 127)
			    {
			      pattern = (y & 1) ? 0x55 : 0xff;
			    }
			    else if (*colorptr < 191)
			    {
			      pattern = (y & 1) ? 0x55 : 0xaa;
			    }
			    else if (y & 1)
			    {
			      break;
			    }
			    else
			    {
			      pattern = 0xaa;
			    }

			    lineptr = line + x / 8;
			    bit     = 0x80 >> (x & 7);

			    for (xcount = xrep; xcount > 0; xcount --)
			    {
			      *lineptr ^= bit & pattern;
			      if (bit > 1)
			      {
				bit /= 2;
			      }
			      else
			      {
				bit = 0x80;
				lineptr ++;
			      }
			    }
			    break;
			case 1 : // 8-bit grayscale/black
			    if (black)
			      memset(line + x, 255 - *colorptr, xrep);
			    else
			      memset(line + x, *colorptr, xrep);
			    break;
			case 2 : // 16-bit grayscale/black
			    if (black)
			      memset(line + 2 * x, 255 - *colorptr, 2 * xrep);
			    else
			      memset(line + 2 * x, *colorptr, 2 * xrep);
			    break;
			case 3 : // 24-bit RGB
			    for (lineptr = line + 3 * x, xcount = xrep; xcount > 0; xcount --)
			    {
			      *lineptr++ = colorptr[0];
			      *lineptr++ = colorptr[1];
			      *lineptr++ = colorptr[2];
			    }
			    break;
			case 4 : // 32-bit CMYK
			    for (lineptr = line + 4 * x, xcount = xrep; xcount > 0; xcount --)
			    {
			      if (color < 4)
			      {
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 255 - colorptr[0];
			      }
			      else
			      {
				*lineptr++ = 255 - colorptr[0];
				*lineptr++ = 255 - colorptr[1];
				*lineptr++ = 255 - colorptr[2];
				*lineptr++ = 0;
			      }
			    }
			    break;
			case 6 : // 24-bit RGB
			    for (lineptr = line + 6 * x, xcount = xrep; xcount > 0; xcount --)
			    {
			      *lineptr++ = colorptr[0];
			      *lineptr++ = colorptr[0];
			      *lineptr++ = colorptr[1];
			      *lineptr++ = colorptr[1];
			      *lineptr++ = colorptr[2];
			      *lineptr++ = colorptr[2];
			    }
			    break;
			case 8 : // 64-bit CMYK
			    for (lineptr = line + 8 * x, xcount = xrep; xcount > 0; xcount --)
			    {
			      if (color < 4)
			      {
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 255 - colorptr[0];
				*lineptr++ = 255 - colorptr[0];
			      }
			      else
			      {
				*lineptr++ = 255 - colorptr[0];
				*lineptr++ = 255 - colorptr[0];
				*lineptr++ = 255 - colorptr[1];
				*lineptr++ = 255 - colorptr[1];
				*lineptr++ = 255 - colorptr[2];
				*lineptr++ = 255 - colorptr[2];
				*lineptr++ = 0;
				*lineptr++ = 0;
			      }
			    }
			    break;
		      }
		    }

		    // Next row in the output image...
		    row --;
		    if (row < 0)
		    {
		      // New row of text with a new color/gray shade...
		      row = 7;
		      color --;
		      if (color < 0)
		      {
			if (bpp > 2)
			  color = 15;
			else
			  color = 3;
		      }
		    }
		  }

		  cupsRasterWritePixels(ras, line, header->cupsBytesPerLine);
		}
	      }
	      break;

	  case IPP_ORIENT_REVERSE_PORTRAIT :
	      color = (int)rows - 1;
	      if (bpp <= 2)
	        color &= 3;
	      else
	        color &= 15;

	      for (row = 7; y < yend;)
	      {
                // Write N scan lines...
	        for (ycount = yrep; ycount > 0 && y < yend; ycount --, y ++)
	        {
		  // Format the current line in the output row...
		  memcpy(line, bline, header->cupsBytesPerLine);
		  colorptr = colors[color];

		  for (outptr = output[row] + 99, x = xoff; outptr >= output[row]; outptr --, x += xrep)
		  {
		    unsigned char	bit,	// Current bit
					pattern;// Shading pattern

		    if (*outptr == ' ')
		      continue;

                    switch (bpp)
                    {
                      case 0 : // 1-bit bitmap output
			  if (*colorptr < 63)
			  {
			    pattern = 0xff;
			  }
			  else if (*colorptr < 127)
			  {
			    pattern = (y & 1) ? 0x55 : 0xff;
			  }
			  else if (*colorptr < 191)
			  {
			    pattern = (y & 1) ? 0x55 : 0xaa;
			  }
			  else if (y & 1)
			  {
			    break;
			  }
			  else
			  {
			    pattern = 0xaa;
			  }

			  lineptr = line + x / 8;
			  bit     = 0x80 >> (x & 7);

			  for (xcount = xrep; xcount > 0; xcount --)
                          {
                            *lineptr ^= bit & pattern;
                            if (bit > 1)
                            {
                              bit /= 2;
                            }
                            else
                            {
                              bit = 0x80;
                              lineptr ++;
                            }
                          }
			  break;
		      case 1 : // 8-bit grayscale/black
		          if (black)
			    memset(line + x, 255 - *colorptr, xrep);
			  else
			    memset(line + x, *colorptr, xrep);
		          break;
		      case 2 : // 16-bit grayscale/black
		          if (black)
			    memset(line + 2 * x, 255 - *colorptr, 2 * xrep);
			  else
			    memset(line + 2 * x, *colorptr, 2 * xrep);
		          break;
		      case 3 : // 24-bit RGB
		          for (lineptr = line + 3 * x, xcount = xrep; xcount > 0; xcount --)
		          {
		            *lineptr++ = colorptr[0];
		            *lineptr++ = colorptr[1];
		            *lineptr++ = colorptr[2];
		          }
		          break;
		      case 4 : // 32-bit CMYK
		          for (lineptr = line + 4 * x, xcount = xrep; xcount > 0; xcount --)
		          {
		            if (color < 4)
		            {
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 255 - colorptr[0];
		            }
		            else
		            {
			      *lineptr++ = 255 - colorptr[0];
			      *lineptr++ = 255 - colorptr[1];
			      *lineptr++ = 255 - colorptr[2];
			      *lineptr++ = 0;
		            }
		          }
		          break;
		      case 6 : // 24-bit RGB
		          for (lineptr = line + 6 * x, xcount = xrep; xcount > 0; xcount --)
		          {
		            *lineptr++ = colorptr[0];
		            *lineptr++ = colorptr[0];
		            *lineptr++ = colorptr[1];
		            *lineptr++ = colorptr[1];
		            *lineptr++ = colorptr[2];
		            *lineptr++ = colorptr[2];
		          }
		          break;
		      case 8 : // 64-bit CMYK
		          for (lineptr = line + 8 * x, xcount = xrep; xcount > 0; xcount --)
		          {
		            if (color < 4)
		            {
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 0;
			      *lineptr++ = 255 - colorptr[0];
			      *lineptr++ = 255 - colorptr[0];
		            }
		            else
		            {
			      *lineptr++ = 255 - colorptr[0];
			      *lineptr++ = 255 - colorptr[0];
			      *lineptr++ = 255 - colorptr[1];
			      *lineptr++ = 255 - colorptr[1];
			      *lineptr++ = 255 - colorptr[2];
			      *lineptr++ = 255 - colorptr[2];
			      *lineptr++ = 0;
			      *lineptr++ = 0;
		            }
		          }
		          break;
                    }
		  }

	          cupsRasterWritePixels(ras, line, header->cupsBytesPerLine);
	        }

		// Next row in the output image...
                row --;
                if (row < 0)
                {
                  // New row of text with a new color/gray shade...
                  row = 7;
                  color --;
                  if (color < 0)
                  {
                    if (bpp > 2)
                      color = 15;
                    else
                      color = 3;
                  }
                }
	      }
	      break;
	  case IPP_ORIENT_REVERSE_LANDSCAPE :
	      for (col = 99; col >= 0; col --)
	      {
		// Write N scan lines...
		for (ycount = yrep; ycount > 0 && y < yend; ycount --, y ++)
		{
		  memcpy(line, bline, header->cupsBytesPerLine);

		  color = 0;

		  for (row = 0, x = xoff; x < xend; x += xrep)
		  {
		    // Format the current line in the output row...
		    unsigned char bit,	// Current bit
				pattern;// Shading pattern

		    colorptr = colors[color];

                    if (output[row][col] != ' ')
                    {
		      switch (bpp)
		      {
			case 0 : // 1-bit bitmap output
			    if (*colorptr < 63)
			    {
			      pattern = 0xff;
			    }
			    else if (*colorptr < 127)
			    {
			      pattern = (y & 1) ? 0x55 : 0xff;
			    }
			    else if (*colorptr < 191)
			    {
			      pattern = (y & 1) ? 0x55 : 0xaa;
			    }
			    else if (y & 1)
			    {
			      break;
			    }
			    else
			    {
			      pattern = 0xaa;
			    }

			    lineptr = line + x / 8;
			    bit     = 0x80 >> (x & 7);

			    for (xcount = xrep; xcount > 0; xcount --)
			    {
			      *lineptr ^= bit & pattern;
			      if (bit > 1)
			      {
				bit /= 2;
			      }
			      else
			      {
				bit = 0x80;
				lineptr ++;
			      }
			    }
			    break;
			case 1 : // 8-bit grayscale/black
			    if (black)
			      memset(line + x, 255 - *colorptr, xrep);
			    else
			      memset(line + x, *colorptr, xrep);
			    break;
			case 2 : // 16-bit grayscale/black
			    if (black)
			      memset(line + 2 * x, 255 - *colorptr, 2 * xrep);
			    else
			      memset(line + 2 * x, *colorptr, 2 * xrep);
			    break;
			case 3 : // 24-bit RGB
			    for (lineptr = line + 3 * x, xcount = xrep; xcount > 0; xcount --)
			    {
			      *lineptr++ = colorptr[0];
			      *lineptr++ = colorptr[1];
			      *lineptr++ = colorptr[2];
			    }
			    break;
			case 4 : // 32-bit CMYK
			    for (lineptr = line + 4 * x, xcount = xrep; xcount > 0; xcount --)
			    {
			      if (color < 4)
			      {
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 255 - colorptr[0];
			      }
			      else
			      {
				*lineptr++ = 255 - colorptr[0];
				*lineptr++ = 255 - colorptr[1];
				*lineptr++ = 255 - colorptr[2];
				*lineptr++ = 0;
			      }
			    }
			    break;
			case 6 : // 24-bit RGB
			    for (lineptr = line + 6 * x, xcount = xrep; xcount > 0; xcount --)
			    {
			      *lineptr++ = colorptr[0];
			      *lineptr++ = colorptr[0];
			      *lineptr++ = colorptr[1];
			      *lineptr++ = colorptr[1];
			      *lineptr++ = colorptr[2];
			      *lineptr++ = colorptr[2];
			    }
			    break;
			case 8 : // 64-bit CMYK
			    for (lineptr = line + 8 * x, xcount = xrep; xcount > 0; xcount --)
			    {
			      if (color < 4)
			      {
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 0;
				*lineptr++ = 255 - colorptr[0];
				*lineptr++ = 255 - colorptr[0];
			      }
			      else
			      {
				*lineptr++ = 255 - colorptr[0];
				*lineptr++ = 255 - colorptr[0];
				*lineptr++ = 255 - colorptr[1];
				*lineptr++ = 255 - colorptr[1];
				*lineptr++ = 255 - colorptr[2];
				*lineptr++ = 255 - colorptr[2];
				*lineptr++ = 0;
				*lineptr++ = 0;
			      }
			    }
			    break;
		      }
		    }

		    // Next row in the output image...
		    row ++;
		    if (row > 7)
		    {
		      // New row of text with a new color/gray shade...
		      row = 0;
		      color ++;
		      if ((bpp > 2 && color >= 16) || (bpp <= 2 && color >= 4))
			color = 0;
		    }
		  }

		  cupsRasterWritePixels(ras, line, header->cupsBytesPerLine);
		}
	      }
	      break;
	}
      }

      // Write out the last of the border lines
      for (; y < yend2; y ++)
	cupsRasterWritePixels(ras, bline, header->cupsBytesPerLine);

      if (bpp == 4)
      {
        // 32-bit CMYK output
        for (lineptr = line; lineptr < lineend;)
        {
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0xff;
        }
      }
      else if (bpp == 8)
      {
        // 64-bit CMYK output
        for (lineptr = line; lineptr < lineend;)
        {
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0x00;
          *lineptr++ = 0xff;
          *lineptr++ = 0xff;
        }
      }
      else
      {
        // 1/8/16/24/32-bit bitmap/grayscale/color output...
        memset(line, black, header->cupsBytesPerLine);
      }

      for (; y < header->cupsHeight; y ++)
	cupsRasterWritePixels(ras, line, header->cupsBytesPerLine);
    }
  }

  // Free memory and return...
  free(line);
  free(bline);

  return (true);
}
