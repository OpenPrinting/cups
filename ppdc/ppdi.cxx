//
// PPD file import utility for the CUPS PPD Compiler.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright 2007-2011 by Apple Inc.
// Copyright 2002-2005 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more information.
//

//
// Include necessary headers...
//

#include "ppdc-private.h"
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>


//
// Local functions...
//

static void	usage(void) _CUPS_NORETURN;


//
// 'main()' - Main entry for the PPD import utility.
//

int					// O - Exit status
main(int  argc,				// I - Number of command-line arguments
     char *argv[])			// I - Command-line arguments
{
  int		i;			// Looping var
  char		*opt;			// Current option
  const char	*srcfile;		// Output file
  ppdcSource	*src;			// PPD source file data


  _cupsSetLocale(argv);

  // Scan the command-line...
  srcfile = NULL;
  src     = NULL;

  for (i = 1; i < argc; i ++)
    if (argv[i][0] == '-')
    {
      for (opt = argv[i] + 1; *opt; opt ++)
        switch (*opt)
	{
	  case 'o' :			// Output file
              if (srcfile || src)
	        usage();

	      i ++;
	      if (i >= argc)
        	usage();

	      srcfile = argv[i];
	      break;

	  case 'I' :			// Include dir
	      i ++;
	      if (i >= argc)
        	usage();

	      ppdcSource::add_include(argv[i]);
	      break;

	  default :			// Unknown
	      usage();
        }
    }
    else
    {
      // Open and load the driver info file...
      if (!srcfile)
        srcfile = "ppdi.drv";

      if (!src)
      {
        if (access(srcfile, 0))
	  src = new ppdcSource();
	else
          src = new ppdcSource(srcfile);
      }

      // Import the PPD file...
      src->import_ppd(argv[i]);
    }

  // If no drivers have been loaded, display the program usage message.
  if (!src)
    usage();

  // Write the driver info file back to disk...
  src->write_file(srcfile);

  // Delete the printer driver information...
  src->release();

  // Return with no errors.
  return (0);
}


//
// 'usage()' - Show usage and exit.
//

static void
usage(void)
{
  _cupsLangPuts(stdout, _("Usage: ppdi [options] filename.ppd [ ... "
			  "filenameN.ppd ]"));
  _cupsLangPuts(stdout, _("Options:"));
  _cupsLangPuts(stdout, _("  -I include-dir          Add include directory to "
                          "search path."));
  _cupsLangPuts(stdout, _("  -o filename.drv         Set driver information "
                          "file (otherwise ppdi.drv)."));

  exit(1);
}
