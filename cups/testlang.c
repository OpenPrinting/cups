/*
 * Localization test program for CUPS.
 *
 * Usage:
 *
 *   ./testlang [-l locale] [-p ppd] ["String to localize"]
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2007-2017 by Apple Inc.
 * Copyright © 1997-2006 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "cups-private.h"
#include "ppd-private.h"
#include <cups/dir.h>


/*
 * Local functions...
 */

static int	show_ppd(const char *filename);
static int	test_language(const char *locale);
static int	test_string(cups_lang_t *language, const char *msgid);
static void	usage(void);


/*
 * 'main()' - Load the specified language and show the strings for yes and no.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line arguments */
     char *argv[])			/* I - Command-line arguments */
{
  int		i;			/* Looping var */
  const char	*opt;			/* Current option */
  int		errors = 0;		/* Number of errors */
  int		dotests = 1;		/* Do standard tests? */
  const char	*lang = NULL;		/* Single language test? */
  cups_lang_t	*language = NULL;	/* Message catalog */


 /*
  * Parse command-line...
  */

  _cupsSetLocale(argv);

  for (i = 1; i < argc; i ++)
  {
    if (argv[i][0] == '-')
    {
      if (!strcmp(argv[i], "--help"))
      {
        usage();
      }
      else
      {
        for (opt = argv[i] + 1; *opt; opt ++)
        {
          switch (*opt)
          {
            case 'l' :
                i ++;
                if (i >= argc)
                {
                  usage();
                  return (1);
                }

                lang = argv[i];
		break;

	    case 'p' :
                i ++;
                if (i >= argc)
                {
                  usage();
                  return (1);
                }

		dotests = 0;
		errors += show_ppd(argv[i]);
                break;

            default :
                usage();
                return (1);
	  }
        }
      }
    }
    else
    {
      if (!language)
        language = cupsLangGet(lang);

      dotests = 0;
      errors += test_string(language, argv[i]);
    }
  }

  if (dotests)
  {
    if (lang)
    {
     /*
      * Test a single language...
      */

      errors += test_language(lang);
    }
    else
    {
     /*
      * Test all locales we find in LOCALEDIR...
      */

      cups_dir_t	*dir;		/* Locale directory */
      cups_dentry_t	*dent;		/* Directory entry */

      if ((dir = cupsDirOpen(getenv("LOCALEDIR"))) != NULL)
      {
	while ((dent = cupsDirRead(dir)) != NULL)
	  errors += test_language(dent->filename);
      }
      else
      {
        // No LOCALEDIR, just use the default language...
        errors += test_language(NULL);
      }

      cupsDirClose(dir);
    }

    if (!errors)
      puts("ALL TESTS PASSED");
  }

  return (errors > 0);
}


/*
 * 'show_ppd()' - Show localized strings in a PPD file.
 *
 * TODO: Move this to the testppd program.
 */

static int				/* O - Number of errors */
show_ppd(const char *filename)		/* I - Filename */
{
  ppd_file_t	*ppd;			/* PPD file */
  ppd_option_t	*option;		/* PageSize option */
  ppd_choice_t	*choice;		/* PageSize/Letter choice */
  char		buffer[1024];		/* String buffer */


  if ((ppd = ppdOpenFile(filename)) == NULL)
  {
    printf("Unable to open PPD file \"%s\".\n", filename);
    return (1);
  }

  ppdLocalize(ppd);

  if ((option = ppdFindOption(ppd, "PageSize")) == NULL)
  {
    puts("No PageSize option.");
    return (1);
  }
  else
  {
    printf("PageSize: %s\n", option->text);

    if ((choice = ppdFindChoice(option, "Letter")) == NULL)
    {
      puts("No Letter PageSize choice.");
      return (1);
    }
    else
    {
      printf("Letter: %s\n", choice->text);
    }
  }

  printf("media-empty: %s\n", ppdLocalizeIPPReason(ppd, "media-empty", NULL, buffer, sizeof(buffer)));

  ppdClose(ppd);

  return (0);
}


/*
 * 'test_language()' - Test a specific language...
 */

static int				/* O - Number of errors */
test_language(const char *lang)		/* I - Locale language code, NULL for default */
{
  int		i;			/* Looping var */
  int		errors = 0;		/* Number of errors */
  cups_lang_t	*language = NULL,	/* Message catalog */
		*language2 = NULL;	/* Message catalog (second time) */
  struct lconv	*loc;			/* Locale data */
  char		buffer[1024];		/* String buffer */
  double	number;			/* Number */
  static const char * const tests[] =	/* Test strings */
  {
    "1",
    "-1",
    "3",
    "5.125"
  };


  // Override the locale environment as needed...
  if (lang)
  {
    // Test the specified locale code...
    setenv("LANG", lang, 1);
    setenv("SOFTWARE", "CUPS/" CUPS_SVERSION, 1);

    printf("cupsLangGet(\"%s\"): ", lang);
    if ((language = cupsLangGet(lang)) == NULL)
    {
      puts("FAIL");
      errors ++;
    }
    else if (strcasecmp(language->language, lang))
    {
      printf("FAIL (got \"%s\")\n", language->language);
      errors ++;
    }
    else
      puts("PASS");

    printf("cupsLangGet(\"%s\") again: ", lang);
    if ((language2 = cupsLangGet(lang)) == NULL)
    {
      puts("FAIL");
      errors ++;
    }
    else if (strcasecmp(language2->language, lang))
    {
      printf("FAIL (got \"%s\")\n", language2->language);
      errors ++;
    }
    else if (language2 != language)
    {
      puts("FAIL (cache failure)");
      errors ++;
    }
    else
      puts("PASS");
  }
  else
  {
    // Test the default locale...
    fputs("cupsLangDefault: ", stdout);
    if ((language = cupsLangDefault()) == NULL)
    {
      puts("FAIL");
      errors ++;
    }
    else
      puts("PASS");

    fputs("cupsLangDefault again: ", stdout);
    if ((language2 = cupsLangDefault()) == NULL)
    {
      puts("FAIL");
      errors ++;
    }
    else if (language2 != language)
    {
      puts("FAIL (cache failure)");
      errors ++;
    }
    else
      puts("PASS");
  }

  printf("language->language: \"%s\"\n", language ? language->language : NULL);
  printf("_cupsEncodingName(language): \"%s\"\n", language ? _cupsEncodingName(language->encoding) : NULL);

  errors += test_string(language, "No");
  errors += test_string(language, "Yes");

  if (language != language2)
  {
    printf("language2->language: \"%s\"\n", language2 ? language2->language : NULL);
    printf("_cupsEncodingName(language2): \"%s\"\n", language2 ? _cupsEncodingName(language2->encoding) : NULL);
  }

  loc = localeconv();

  for (i = 0; i < (int)(sizeof(tests) / sizeof(tests[0])); i ++)
  {
    number = _cupsStrScand(tests[i], NULL, loc);

    printf("_cupsStrScand(\"%s\"): %f\n", tests[i], number);

    _cupsStrFormatd(buffer, buffer + sizeof(buffer), number, loc);

    printf("_cupsStrFormatd(%f): ", number);

    if (strcmp(buffer, tests[i]))
    {
      errors ++;
      printf("FAIL (got \"%s\")\n", buffer);
    }
    else
      puts("PASS");
  }

  return (errors);
}


/*
 * 'test_string()' - Test the localization of a string.
 */

static int				/* O - 1 on failure, 0 on success */
test_string(cups_lang_t *language,	/* I - Language */
            const char  *msgid)		/* I - Message */
{
  const char  *msgstr;			/* Localized string */


 /*
  * Get the localized string and then see if we got what we expected.
  *
  * For the POSIX locale, the string pointers should be the same.
  * For any other locale, the string pointers should be different.
  */

  if (!language)
    return (1);

  printf("_cupsLangString(\"%s\"): ", msgid);
  msgstr = _cupsLangString(language, msgid);
  if (strcmp(language->language, "C") && msgid == msgstr)
  {
    puts("FAIL (no message catalog loaded)");
    return (1);
  }
  else if (!strcmp(language->language, "C") && msgid != msgstr)
  {
    puts("FAIL (POSIX locale is localized)");
    return (1);
  }

  printf("PASS (\"%s\")\n", msgstr);

  return (0);
}


/*
 * 'usage()' - Show program usage.
 */

static void
usage(void)
{
  puts("Usage: ./testlang [-l locale] [-p ppd] [\"String to localize\"]");
  puts("");
  puts("If no arguments are specified, all locales are tested.");
}
