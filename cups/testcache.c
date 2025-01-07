/*
 * PPD cache testing program for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright 2009-2018 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more information.
 */

/*
 * Include necessary headers...
 */

#include "ppd-private.h"
#include "file-private.h"


/*
 * 'main()' - Main entry.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line args */
     char *argv[])			/* I - Command-line arguments */
{
  int			i;		/* Looping var */
  const char		*ppdfile = NULL;/* PPD filename */
  ppd_file_t		*ppd;		/* PPD file */
  int			num_options = 0;/* Number of options */
  cups_option_t		*options = NULL;/* Options */
  _ppd_cache_t		*pc;		/* PPD cache and PWG mapping data */
  int			num_finishings,	/* Number of finishing options */
			finishings[20];	/* Finishing options */
  ppd_choice_t		*ppd_bin;	/* OutputBin value */
  const char		*output_bin;	/* output-bin value */

  if (argc < 2)
  {
    puts("Usage: ./testcache filename.ppd [name=value ... name=value]");
    return (1);
  }

  ppdfile = argv[1];
  if ((ppd = ppdOpenFile(ppdfile)) == NULL)
  {
    ppd_status_t err;			/* Last error in file */
    int		line;			/* Line number in file */


    err = ppdLastError(&line);

    fprintf(stderr, "Unable to open \"%s\": %s on line %d\n", ppdfile, ppdErrorString(err), line);
    return (1);
  }

  if ((pc = _ppdCacheCreateWithPPD(ppd)) == NULL)
  {
    fprintf(stderr, "Unable to create PPD cache from \"%s\".\n", ppdfile);
    return (1);
  }

  for (i = 2; i < argc; i ++)
    num_options = cupsParseOptions(argv[i], num_options, &options);

  ppdMarkDefaults(ppd);
  cupsMarkOptions(ppd, num_options, options);

  num_finishings = _ppdCacheGetFinishingValues(ppd, pc, (int)sizeof(finishings) / sizeof(finishings[0]), finishings);

  if (num_finishings > 0)
  {
    fputs("finishings=", stdout);
    for (i = 0; i < num_finishings; i ++)
      if (i)
	printf(",%d", finishings[i]);
      else
	printf("%d", finishings[i]);
    fputs("\n", stdout);
  }

  if ((ppd_bin = ppdFindMarkedChoice(ppd, "OutputBin")) != NULL &&
      (output_bin = _ppdCacheGetBin(pc, ppd_bin->choice)) != NULL)
    printf("output-bin=\"%s\"\n", output_bin);

  return (0);
}
