/*
 * Test program for PPD data encoding example code.
 *
 * Compile with:
 *
 *     gcc -o testppdx -D_PPD_DEPRECATED="" -g testppdx.c ppdx.c -lcups -lz
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright 2012 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more information.
 */

/*
 * Include necessary headers...
 */

#include <stdio.h>
#include <string.h>
#include "ppdx.h"


/*
 * 'main()' - Read data from a test PPD file and write out new chunks.
 */

int					/* O - Exit status */
main(void)
{
  int		status = 0;		/* Exit status */
  FILE		*fp;			/* File to read */
  char		contents[8193],		/* Contents of file */
		*data;			/* Data from PPD */
  size_t	contsize,		/* File size */
		datasize;		/* Data size */
  ppd_file_t	*ppd;			/* Test PPD */


 /*
  * Open the PPD and get the data from it...
  */

  ppd  = ppdOpenFile("testppdx.ppd");
  data = ppdxReadData(ppd, "EXData", &datasize);

 /*
  * Open this source file and read it...
  */

  fp = fopen("testppdx.c", "r");
  if (fp)
  {
    contsize = fread(contents, 1, sizeof(contents) - 1, fp);
    fclose(fp);
    contents[contsize] = '\0';
  }
  else
  {
    contents[0] = '\0';
    contsize    = 0;
  }

 /*
  * Compare data...
  */

  if (data)
  {
    if (contsize != datasize)
    {
      fprintf(stderr, "ERROR: PPD has %ld bytes, test file is %ld bytes.\n",
              (long)datasize, (long)contsize);
      status = 1;
    }
    else if (strcmp(contents, data))
    {
      fputs("ERROR: PPD and test file are not the same.\n", stderr);
      status = 1;
    }

    if (status)
    {
      if ((fp = fopen("testppdx.dat", "wb")) != NULL)
      {
        fwrite(data, 1, datasize, fp);
        fclose(fp);
        fputs("ERROR: See testppdx.dat for data from PPD.\n", stderr);
      }
      else
        perror("Unable to open 'testppdx.dat'");
    }

    free(data);
  }

  printf("Encoding %ld bytes for PPD...\n", (long)contsize);

  ppdxWriteData("EXData", contents, contsize);

  return (1);
}
