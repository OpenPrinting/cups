//
// Raster test program routines for CUPS.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright © 2007-2019 by Apple Inc.
// Copyright © 1997-2007 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "raster-private.h"
#include "test-internal.h"
#include <math.h>


//
// Local functions...
//

static int	do_ras_file(const char *filename);
static int	do_raster_tests(cups_mode_t mode);
static void	print_changes(cups_page_header2_t *header, cups_page_header2_t *expected);


//
// 'main()' - Test the raster functions.
//

int					// O - Exit status
main(int  argc,				// I - Number of command-line args
     char *argv[])			// I - Command-line arguments
{
  int	errors = 0;			// Number of errors


  if (argc == 1)
  {
    errors += do_raster_tests(CUPS_RASTER_WRITE);
    errors += do_raster_tests(CUPS_RASTER_WRITE_COMPRESSED);
    errors += do_raster_tests(CUPS_RASTER_WRITE_PWG);
    errors += do_raster_tests(CUPS_RASTER_WRITE_APPLE);
  }
  else
  {
    int			i;		// Looping var

    for (i = 1; i < argc; i ++)
      errors += do_ras_file(argv[i]);
  }

  return (errors);
}


//
// 'do_ras_file()' - Test reading of a raster file.
//

static int				// O - Number of errors
do_ras_file(const char *filename)	// I - Filename
{
  unsigned		y;		// Looping vars
  int			fd;		// File descriptor
  cups_raster_t		*ras;		// Raster stream
  cups_page_header2_t	header;		// Page header
  unsigned char		*data;		// Raster data
  int			errors = 0;	// Number of errors
  unsigned		pages = 0;	// Number of pages


  if ((fd = open(filename, O_RDONLY)) < 0)
  {
    printf("%s: %s\n", filename, strerror(errno));
    return (1);
  }

  if ((ras = cupsRasterOpen(fd, CUPS_RASTER_READ)) == NULL)
  {
    printf("%s: cupsRasterOpen failed.\n", filename);
    close(fd);
    return (1);
  }

  printf("%s:\n", filename);

  while (cupsRasterReadHeader2(ras, &header))
  {
    pages ++;
    data = malloc(header.cupsBytesPerLine);

    printf("    Page %u: %ux%ux%u@%ux%udpi", pages,
           header.cupsWidth, header.cupsHeight, header.cupsBitsPerPixel,
           header.HWResolution[0], header.HWResolution[1]);

    for (y = 0; y < header.cupsHeight; y ++)
      if (cupsRasterReadPixels(ras, data, header.cupsBytesPerLine) <
              header.cupsBytesPerLine)
        break;

    if (y < header.cupsHeight)
      printf(" ERROR AT LINE %d\n", y);
    else
      putchar('\n');

    free(data);
  }

  printf("EOF at %ld\n", (long)lseek(fd, SEEK_CUR, 0));

  cupsRasterClose(ras);
  close(fd);

  return (errors);
}


//
// 'do_raster_tests()' - Test reading and writing of raster data.
//

static int				// O - Number of errors
do_raster_tests(cups_mode_t mode)	// O - Write mode
{
  unsigned		page, x, y, count;// Looping vars
  FILE			*fp;		// Raster file
  cups_raster_t		*r;		// Raster stream
  cups_page_header2_t	header,		// Page header
			expected;	// Expected page header
  unsigned char		data[2048];	// Raster data
  int			errors = 0;	// Number of errors
  static const char * const modes[] =	// Open modes
  {
    "CUPS_RASTER_READ",
    "CUPS_RASTER_WRITE",
    "CUPS_RASTER_WRITE_COMPRESSED",
    "CUPS_RASTER_WRITE_PWG",
    "CUPS_RASTER_WRITE_APPLE"
  };


 /*
  * Test writing...
  */

  testBegin("cupsRasterOpen(%s)", modes[mode]);

  if ((fp = fopen("test.raster", "wb")) == NULL)
  {
    testEndMessage(false, "%s", strerror(errno));
    return (1);
  }

  if ((r = cupsRasterOpen(fileno(fp), mode)) == NULL)
  {
    testEndMessage(false, "%s", cupsRasterGetErrorString());
    fclose(fp);
    return (1);
  }

  testEnd(true);

  for (page = 0; page < 4; page ++)
  {
    memset(&header, 0, sizeof(header));
    header.cupsWidth        = 256;
    header.cupsHeight       = 256;
    header.cupsBytesPerLine = 256;
    header.HWResolution[0]  = 64;
    header.HWResolution[1]  = 64;
    header.PageSize[0]      = 288;
    header.PageSize[1]      = 288;
    header.cupsPageSize[0]  = 288.0f;
    header.cupsPageSize[1]  = 288.0f;

    cupsCopyString(header.MediaType, "auto", sizeof(header.MediaType));

    if (page & 1)
    {
      header.cupsBytesPerLine *= 4;
      header.cupsColorSpace = CUPS_CSPACE_CMYK;
      header.cupsColorOrder = CUPS_ORDER_CHUNKED;
      header.cupsNumColors  = 4;
    }
    else
    {
      header.cupsColorSpace = CUPS_CSPACE_W;
      header.cupsColorOrder = CUPS_ORDER_CHUNKED;
      header.cupsNumColors  = 1;
    }

    if (page & 2)
    {
      header.cupsBytesPerLine *= 2;
      header.cupsBitsPerColor = 16;
      header.cupsBitsPerPixel = (page & 1) ? 64 : 16;
    }
    else
    {
      header.cupsBitsPerColor = 8;
      header.cupsBitsPerPixel = (page & 1) ? 32 : 8;
    }

    testBegin("cupsRasterWriteHeader2(page %d)", page + 1);

    if (cupsRasterWriteHeader2(r, &header))
    {
      testEnd(true);
    }
    else
    {
      testEnd(false);
      errors ++;
    }

    testBegin("cupsRasterWritePixels");

    memset(data, 0, header.cupsBytesPerLine);
    for (y = 0; y < 64; y ++)
    {
      if (!cupsRasterWritePixels(r, data, header.cupsBytesPerLine))
        break;
    }

    if (y < 64)
    {
      testEnd(false);
      errors ++;
    }
    else
    {
      for (x = 0; x < header.cupsBytesPerLine; x ++)
	data[x] = (unsigned char)x;

      for (y = 0; y < 64; y ++)
      {
	if (!cupsRasterWritePixels(r, data, header.cupsBytesPerLine))
	  break;
      }

      if (y < 64)
      {
	testEnd(false);
	errors ++;
      }
      else
      {
	memset(data, 255, header.cupsBytesPerLine);
	for (y = 0; y < 64; y ++)
	{
	  if (!cupsRasterWritePixels(r, data, header.cupsBytesPerLine))
	    break;
        }

	if (y < 64)
	{
	  testEnd(false);
	  errors ++;
	}
	else
	{
	  for (x = 0; x < header.cupsBytesPerLine; x ++)
	    data[x] = (unsigned char)(x / 4);

	  for (y = 0; y < 64; y ++)
	  {
	    if (!cupsRasterWritePixels(r, data, header.cupsBytesPerLine))
	      break;
          }

	  if (y < 64)
	  {
	    testEnd(false);
	    errors ++;
	  }
	  else
	    testEnd(true);
        }
      }
    }
  }

  cupsRasterClose(r);
  fclose(fp);

 /*
  * Test reading...
  */

  testBegin("cupsRasterOpen(CUPS_RASTER_READ)");

  if ((fp = fopen("test.raster", "rb")) == NULL)
  {
    testEndMessage(false, "%s", strerror(errno));
    return (1);
  }

  if ((r = cupsRasterOpen(fileno(fp), CUPS_RASTER_READ)) == NULL)
  {
    testEndMessage(false, "%s", cupsRasterGetErrorString());
    fclose(fp);
    return (1);
  }

  testEnd(true);

  for (page = 0; page < 4; page ++)
  {
    memset(&expected, 0, sizeof(expected));
    expected.cupsWidth        = 256;
    expected.cupsHeight       = 256;
    expected.cupsBytesPerLine = 256;
    expected.HWResolution[0]  = 64;
    expected.HWResolution[1]  = 64;
    expected.PageSize[0]      = 288;
    expected.PageSize[1]      = 288;

    cupsCopyString(expected.MediaType, "auto", sizeof(expected.MediaType));

    if (mode != CUPS_RASTER_WRITE_PWG)
    {
      expected.cupsPageSize[0] = 288.0f;
      expected.cupsPageSize[1] = 288.0f;
    }

    if (mode >= CUPS_RASTER_WRITE_PWG)
    {
      cupsCopyString(expected.MediaClass, "PwgRaster", sizeof(expected.MediaClass));
      expected.cupsInteger[7] = 0xffffff;
    }

    if (page & 1)
    {
      expected.cupsBytesPerLine *= 4;
      expected.cupsColorSpace = CUPS_CSPACE_CMYK;
      expected.cupsColorOrder = CUPS_ORDER_CHUNKED;
      expected.cupsNumColors  = 4;
    }
    else
    {
      expected.cupsColorSpace = CUPS_CSPACE_W;
      expected.cupsColorOrder = CUPS_ORDER_CHUNKED;
      expected.cupsNumColors  = 1;
    }

    if (page & 2)
    {
      expected.cupsBytesPerLine *= 2;
      expected.cupsBitsPerColor = 16;
      expected.cupsBitsPerPixel = (page & 1) ? 64 : 16;
    }
    else
    {
      expected.cupsBitsPerColor = 8;
      expected.cupsBitsPerPixel = (page & 1) ? 32 : 8;
    }

    testBegin("cupsRasterReadHeader2(page %d)", page + 1);

    if (!cupsRasterReadHeader2(r, &header))
    {
      testEndMessage(false, "%s", cupsRasterGetErrorString());
      errors ++;
      break;
    }
    else if (memcmp(&header, &expected, sizeof(header)))
    {
      testEndMessage(false, "bad page header");
      errors ++;
      print_changes(&header, &expected);
    }
    else
    {
      testEnd(true);
    }

    testBegin("cupsRasterReadPixels");

    for (y = 0; y < 64; y ++)
    {
      if (!cupsRasterReadPixels(r, data, header.cupsBytesPerLine))
      {
	testEndMessage(false, "%s", cupsRasterGetErrorString());
	errors ++;
	break;
      }

      if (data[0] != 0 || memcmp(data, data + 1, header.cupsBytesPerLine - 1))
      {
	testEndMessage(false, "raster line %d corrupt", y);

	for (x = 0, count = 0; x < header.cupsBytesPerLine && count < 10; x ++)
        {
	  if (data[x])
	  {
	    count ++;

	    if (count == 10)
	      testMessage("   ...");
	    else
	      testMessage("  %4u %02X (expected %02X)", x, data[x], 0);
	  }
	}

	errors ++;
	break;
      }
    }

    if (y == 64)
    {
      for (y = 0; y < 64; y ++)
      {
	if (!cupsRasterReadPixels(r, data, header.cupsBytesPerLine))
	{
	  testEndMessage(false, "%s", cupsRasterGetErrorString());
	  errors ++;
	  break;
	}

	for (x = 0; x < header.cupsBytesPerLine; x ++)
	{
          if (data[x] != (x & 255))
	    break;
        }

	if (x < header.cupsBytesPerLine)
	{
          testEndMessage(false, "raster line %d corrupt", y + 64);

	  for (x = 0, count = 0; x < header.cupsBytesPerLine && count < 10; x ++)
	  {
	    if (data[x] != (x & 255))
	    {
	      count ++;

	      if (count == 10)
		testMessage("   ...");
	      else
		testMessage("  %4u %02X (expected %02X)", x, data[x], x & 255);
	    }
	  }

	  errors ++;
	  break;
	}
      }

      if (y == 64)
      {
	for (y = 0; y < 64; y ++)
	{
	  if (!cupsRasterReadPixels(r, data, header.cupsBytesPerLine))
	  {
	    testEndMessage(false, "%s", cupsRasterGetErrorString());
	    errors ++;
	    break;
	  }

	  if (data[0] != 255 || memcmp(data, data + 1, header.cupsBytesPerLine - 1))
          {
	    testEndMessage(false, "raster line %d corrupt", y + 128);

	    for (x = 0, count = 0; x < header.cupsBytesPerLine && count < 10; x ++)
	    {
	      if (data[x] != 255)
	      {
		count ++;

		if (count == 10)
		  testMessage("   ...");
		else
		  testMessage("  %4u %02X (expected %02X)", x, data[x], 255);
	      }
	    }

	    errors ++;
	    break;
	  }
	}

        if (y == 64)
	{
	  for (y = 0; y < 64; y ++)
	  {
	    if (!cupsRasterReadPixels(r, data, header.cupsBytesPerLine))
	    {
	      testEndMessage(false, "%s", cupsRasterGetErrorString());
	      errors ++;
	      break;
	    }

	    for (x = 0; x < header.cupsBytesPerLine; x ++)
	    {
              if (data[x] != ((x / 4) & 255))
		break;
            }

	    if (x < header.cupsBytesPerLine)
            {
	      testEndMessage(false, "raster line %d corrupt", y + 192);

	      for (x = 0, count = 0; x < header.cupsBytesPerLine && count < 10; x ++)
	      {
		if (data[x] != ((x / 4) & 255))
		{
		  count ++;

		  if (count == 10)
		    testMessage("   ...");
		  else
		    testMessage("  %4u %02X (expected %02X)", x, data[x], (x / 4) & 255);
		}
	      }

	      errors ++;
	      break;
	    }
	  }

	  if (y == 64)
	    testEnd(true);
	}
      }
    }
  }

  cupsRasterClose(r);
  fclose(fp);

  return (errors);
}


//
// 'print_changes()' - Print differences in the page header.
//

static void
print_changes(
    cups_page_header2_t *header,	// I - Actual page header
    cups_page_header2_t *expected)	// I - Expected page header
{
  int	i;				// Looping var


  if (strcmp(header->MediaClass, expected->MediaClass))
    testMessage("    MediaClass (%s), expected (%s)", header->MediaClass, expected->MediaClass);

  if (strcmp(header->MediaColor, expected->MediaColor))
    testMessage("    MediaColor (%s), expected (%s)", header->MediaColor, expected->MediaColor);

  if (strcmp(header->MediaType, expected->MediaType))
    testMessage("    MediaType (%s), expected (%s)", header->MediaType, expected->MediaType);

  if (strcmp(header->OutputType, expected->OutputType))
    testMessage("    OutputType (%s), expected (%s)", header->OutputType, expected->OutputType);

  if (header->AdvanceDistance != expected->AdvanceDistance)
    testMessage("    AdvanceDistance %d, expected %d", header->AdvanceDistance, expected->AdvanceDistance);

  if (header->AdvanceMedia != expected->AdvanceMedia)
    testMessage("    AdvanceMedia %d, expected %d", header->AdvanceMedia, expected->AdvanceMedia);

  if (header->Collate != expected->Collate)
    testMessage("    Collate %d, expected %d", header->Collate, expected->Collate);

  if (header->CutMedia != expected->CutMedia)
    testMessage("    CutMedia %d, expected %d", header->CutMedia, expected->CutMedia);

  if (header->Duplex != expected->Duplex)
    testMessage("    Duplex %d, expected %d", header->Duplex, expected->Duplex);

  if (header->HWResolution[0] != expected->HWResolution[0] || header->HWResolution[1] != expected->HWResolution[1])
    testMessage("    HWResolution [%d %d], expected [%d %d]", header->HWResolution[0], header->HWResolution[1], expected->HWResolution[0], expected->HWResolution[1]);

  if (memcmp(header->ImagingBoundingBox, expected->ImagingBoundingBox, sizeof(header->ImagingBoundingBox)))
    testMessage("    ImagingBoundingBox [%d %d %d %d], expected [%d %d %d %d]", header->ImagingBoundingBox[0], header->ImagingBoundingBox[1], header->ImagingBoundingBox[2], header->ImagingBoundingBox[3], expected->ImagingBoundingBox[0], expected->ImagingBoundingBox[1], expected->ImagingBoundingBox[2], expected->ImagingBoundingBox[3]);

  if (header->InsertSheet != expected->InsertSheet)
    testMessage("    InsertSheet %d, expected %d", header->InsertSheet, expected->InsertSheet);

  if (header->Jog != expected->Jog)
    testMessage("    Jog %d, expected %d", header->Jog, expected->Jog);

  if (header->LeadingEdge != expected->LeadingEdge)
    testMessage("    LeadingEdge %d, expected %d", header->LeadingEdge, expected->LeadingEdge);

  if (header->Margins[0] != expected->Margins[0] || header->Margins[1] != expected->Margins[1])
    testMessage("    Margins [%d %d], expected [%d %d]", header->Margins[0], header->Margins[1], expected->Margins[0], expected->Margins[1]);

  if (header->ManualFeed != expected->ManualFeed)
    testMessage("    ManualFeed %d, expected %d", header->ManualFeed, expected->ManualFeed);

  if (header->MediaPosition != expected->MediaPosition)
    testMessage("    MediaPosition %d, expected %d", header->MediaPosition, expected->MediaPosition);

  if (header->MediaWeight != expected->MediaWeight)
    testMessage("    MediaWeight %d, expected %d", header->MediaWeight, expected->MediaWeight);

  if (header->MirrorPrint != expected->MirrorPrint)
    testMessage("    MirrorPrint %d, expected %d", header->MirrorPrint, expected->MirrorPrint);

  if (header->NegativePrint != expected->NegativePrint)
    testMessage("    NegativePrint %d, expected %d", header->NegativePrint, expected->NegativePrint);

  if (header->NumCopies != expected->NumCopies)
    testMessage("    NumCopies %d, expected %d", header->NumCopies, expected->NumCopies);

  if (header->Orientation != expected->Orientation)
    testMessage("    Orientation %d, expected %d", header->Orientation, expected->Orientation);

  if (header->OutputFaceUp != expected->OutputFaceUp)
    testMessage("    OutputFaceUp %d, expected %d", header->OutputFaceUp, expected->OutputFaceUp);

  if (header->PageSize[0] != expected->PageSize[0] || header->PageSize[1] != expected->PageSize[1])
    testMessage("    PageSize [%d %d], expected [%d %d]", header->PageSize[0], header->PageSize[1], expected->PageSize[0], expected->PageSize[1]);

  if (header->Separations != expected->Separations)
    testMessage("    Separations %d, expected %d", header->Separations, expected->Separations);

  if (header->TraySwitch != expected->TraySwitch)
    testMessage("    TraySwitch %d, expected %d", header->TraySwitch, expected->TraySwitch);

  if (header->Tumble != expected->Tumble)
    testMessage("    Tumble %d, expected %d", header->Tumble, expected->Tumble);

  if (header->cupsWidth != expected->cupsWidth)
    testMessage("    cupsWidth %d, expected %d", header->cupsWidth, expected->cupsWidth);

  if (header->cupsHeight != expected->cupsHeight)
    testMessage("    cupsHeight %d, expected %d", header->cupsHeight, expected->cupsHeight);

  if (header->cupsMediaType != expected->cupsMediaType)
    testMessage("    cupsMediaType %d, expected %d", header->cupsMediaType, expected->cupsMediaType);

  if (header->cupsBitsPerColor != expected->cupsBitsPerColor)
    testMessage("    cupsBitsPerColor %d, expected %d", header->cupsBitsPerColor, expected->cupsBitsPerColor);

  if (header->cupsBitsPerPixel != expected->cupsBitsPerPixel)
    testMessage("    cupsBitsPerPixel %d, expected %d", header->cupsBitsPerPixel, expected->cupsBitsPerPixel);

  if (header->cupsBytesPerLine != expected->cupsBytesPerLine)
    testMessage("    cupsBytesPerLine %d, expected %d", header->cupsBytesPerLine, expected->cupsBytesPerLine);

  if (header->cupsColorOrder != expected->cupsColorOrder)
    testMessage("    cupsColorOrder %d, expected %d", header->cupsColorOrder, expected->cupsColorOrder);

  if (header->cupsColorSpace != expected->cupsColorSpace)
    testMessage("    cupsColorSpace %d, expected %d", header->cupsColorSpace, expected->cupsColorSpace);

  if (header->cupsCompression != expected->cupsCompression)
    testMessage("    cupsCompression %d, expected %d", header->cupsCompression, expected->cupsCompression);

  if (header->cupsRowCount != expected->cupsRowCount)
    testMessage("    cupsRowCount %d, expected %d", header->cupsRowCount, expected->cupsRowCount);

  if (header->cupsRowFeed != expected->cupsRowFeed)
    testMessage("    cupsRowFeed %d, expected %d", header->cupsRowFeed, expected->cupsRowFeed);

  if (header->cupsRowStep != expected->cupsRowStep)
    testMessage("    cupsRowStep %d, expected %d", header->cupsRowStep, expected->cupsRowStep);

  if (header->cupsNumColors != expected->cupsNumColors)
    testMessage("    cupsNumColors %d, expected %d", header->cupsNumColors, expected->cupsNumColors);

  if (fabs(header->cupsBorderlessScalingFactor - expected->cupsBorderlessScalingFactor) > 0.001)
    testMessage("    cupsBorderlessScalingFactor %g, expected %g", header->cupsBorderlessScalingFactor, expected->cupsBorderlessScalingFactor);

  if (fabs(header->cupsPageSize[0] - expected->cupsPageSize[0]) > 0.001 || fabs(header->cupsPageSize[1] - expected->cupsPageSize[1]) > 0.001)
    testMessage("    cupsPageSize [%g %g], expected [%g %g]", header->cupsPageSize[0], header->cupsPageSize[1], expected->cupsPageSize[0], expected->cupsPageSize[1]);

  if (fabs(header->cupsImagingBBox[0] - expected->cupsImagingBBox[0]) > 0.001 || fabs(header->cupsImagingBBox[1] - expected->cupsImagingBBox[1]) > 0.001 || fabs(header->cupsImagingBBox[2] - expected->cupsImagingBBox[2]) > 0.001 || fabs(header->cupsImagingBBox[3] - expected->cupsImagingBBox[3]) > 0.001)
    testMessage("    cupsImagingBBox [%g %g %g %g], expected [%g %g %g %g]", header->cupsImagingBBox[0], header->cupsImagingBBox[1], header->cupsImagingBBox[2], header->cupsImagingBBox[3], expected->cupsImagingBBox[0], expected->cupsImagingBBox[1], expected->cupsImagingBBox[2], expected->cupsImagingBBox[3]);

  for (i = 0; i < 16; i ++)
  {
    if (header->cupsInteger[i] != expected->cupsInteger[i])
      testMessage("    cupsInteger%d %d, expected %d", i, header->cupsInteger[i], expected->cupsInteger[i]);
  }

  for (i = 0; i < 16; i ++)
  {
    if (fabs(header->cupsReal[i] - expected->cupsReal[i]) > 0.001)
      testMessage("    cupsReal%d %g, expected %g", i, header->cupsReal[i], expected->cupsReal[i]);
  }

  for (i = 0; i < 16; i ++)
  {
    if (strcmp(header->cupsString[i], expected->cupsString[i]))
      testMessage("    cupsString%d (%s), expected (%s)", i, header->cupsString[i], expected->cupsString[i]);
  }

  if (strcmp(header->cupsMarkerType, expected->cupsMarkerType))
    testMessage("    cupsMarkerType (%s), expected (%s)", header->cupsMarkerType, expected->cupsMarkerType);

  if (strcmp(header->cupsRenderingIntent, expected->cupsRenderingIntent))
    testMessage("    cupsRenderingIntent (%s), expected (%s)", header->cupsRenderingIntent, expected->cupsRenderingIntent);

  if (strcmp(header->cupsPageSizeName, expected->cupsPageSizeName))
    testMessage("    cupsPageSizeName (%s), expected (%s)", header->cupsPageSizeName, expected->cupsPageSizeName);
}
