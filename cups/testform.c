//
// Form API unit test program for CUPS.
//
// Copyright © 2023-2024 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "form.h"
#include "test-internal.h"


//
// Local types...
//

typedef struct _form_data_s		// Form test data
{
  const char	*url,			// URL prefix, if any
		*encoded;		// URL-encoded data
  int		num_pairs;		// Number of name=value pairs
  const char	* const * pairs;	// name=value pairs
} _form_data_t;


//
// Local functions...
//

static void	do_test(_form_data_t *test);
static void	usage(FILE *fp);


//
// 'main()' - Main entry.
//

int					// O - Exit status
main(int  argc,				// I - Number of command-line arguments
     char *argv[])			// I - Command-line arguments
{
  int		status = 0;		// Exit status


  if (argc == 1)
  {
    // Do canned API unit tests...
    static _form_data_t test1 =
    {
      NULL,
      "",
      0,
      NULL
    };
    static const char * const pairs2[] =
    {
      "name",		"value"
    };
    static _form_data_t test2 =
    {
      NULL,
      "name=value",
      1,
      pairs2
    };
    static const char * const pairs3[] =
    {
      "name",		"value",
      "name_2",		"value 2",
      "third",		"3.1415926535"
    };
    static _form_data_t test3 =
    {
      NULL,
      "name=value&name%5F2=value+2&third=3%2E1415926535",
      3,
      pairs3
    };
    static _form_data_t test4 =
    {
      NULL,
      "bogus",
      0,
      NULL
    };
    static _form_data_t test5 =
    {
      NULL,
      "bogus=foo=bar",
      0,
      NULL
    };
    static _form_data_t test6 =
    {
      NULL,
      "nul=%00",
      0,
      NULL
    };
    static const char * const pairs7[] =
    {
      "name",		"value",
      "name_2",		"value 2",
      "third",		"3.1415926535"
    };
    static _form_data_t test7 =
    {
      "http://www.example.com:8080/userinfo",
      "http://www.example.com:8080/userinfo?name=value&name%5F2=value+2&third=3%2E1415926535",
      3,
      pairs7
    };
    static const char * const pairs8[] =
    {
      "name",		"value",
      "name_2",		"value 2",
      "third",		"3.1415926535"
    };
    static _form_data_t test8 =
    {
      "https://www.example.com",
      "https://www.example.com/?name=value&name%5F2=value+2&third=3%2E1415926535",
      3,
      pairs8
    };

    do_test(&test1);
    do_test(&test2);
    do_test(&test3);
    do_test(&test4);
    do_test(&test5);
    do_test(&test6);
    do_test(&test7);
    do_test(&test8);
  }
  else
  {
    // Parse command-line...
    int			i;		// Looping var
    const char		*opt;		// Current option
    const char		*url = NULL;	// URL, if any
    int			num_vars;	// Number of variables
    cups_option_t	*vars;		// Variables
    char		*data;		// Form data

    for (i = 1; i < argc; i ++)
    {
      if (!strcmp(argv[i], "--help"))
      {
        // --help
        usage(stdout);
      }
      else if (!strncmp(argv[i], "--", 2))
      {
        // Unknown option
        fprintf(stderr, "testform: Unknown option '%s'.\n", argv[i]);
        usage(stderr);
        status = 1;
        break;
      }
      else if (argv[i][0] == '-')
      {
        // Process options
        for (opt = argv[i] + 1; *opt && !status; opt ++)
        {
          switch (*opt)
          {
            case 'f' : // -f FORM-DATA
                i ++;
                if (i >= argc)
                {
                  fputs("testform: Missing form data after '-f'.\n", stderr);
		  usage(stderr);
		  status = 1;
		  break;
		}

		num_vars = cupsFormDecode(argv[i], &vars);
		if (num_vars == 0)
		{
		  fprintf(stderr, "testform: %s\n", cupsGetErrorString());
		  status = 1;
		}
		else
		{
		  int	j;		// Looping var

		  for (j = 0; j < num_vars; j ++)
		    printf("%s=%s\n", vars[j].name, vars[j].value);

		  cupsFreeOptions(num_vars, vars);
		}
                break;

            case 'o' : // -o 'NAME=VALUE [... NAME=VALUE]'
                i ++;
                if (i >= argc)
                {
                  fputs("testform: Missing form data after '-o'.\n", stderr);
		  usage(stderr);
		  status = 1;
		  break;
		}

                num_vars = cupsParseOptions(argv[i], 0, &vars);
                data     = cupsFormEncode(url, num_vars, vars);

                if (data)
                {
                  puts(data);
		  free(data);
		}
		else
		{
		  fprintf(stderr, "testform: %s\n", cupsGetErrorString());
		  status = 1;
		}

		cupsFreeOptions(num_vars, vars);
                break;

            case 'u' : // -u URL
                i ++;
                if (i >= argc)
                {
                  fputs("testform: Missing URL after '-u'.\n", stderr);
		  usage(stderr);
		  status = 1;
		  break;
		}

                url = argv[i];
                break;

	    default :
		fprintf(stderr, "testform: Unknown option '-%c'.\n", *opt);
		usage(stderr);
		status = 1;
		break;
          }
        }
      }
      else
      {
        // Unknown option...
        fprintf(stderr, "testform: Unknown argument '%s'.\n", argv[i]);
        usage(stderr);
        status = 1;
        break;
      }
    }
  }

  return (status);
}


//
// 'do_test()' - Test the form functions.
//

static void
do_test(_form_data_t *test)		// I - Test data
{
  int		i,			// Looping var
		num_vars;		// Number of variables
  cups_option_t	*vars;			// Variables
  char		*data;			// Form data


  testBegin("cupsFormDecode(\"%s\")", test->encoded);
  num_vars = cupsFormDecode(test->encoded, &vars);
  if (num_vars != test->num_pairs)
  {
    testEndMessage(false, "got %u pairs, expected %u", (unsigned)num_vars, (unsigned)test->num_pairs);
  }
  else
  {
    int		count;			// Max count
    const char	*value;			// Value

    for (i = 0, count = 2 * test->num_pairs; i < count; i += 2)
    {
      if ((value = cupsGetOption(test->pairs[i], num_vars, vars)) == NULL)
      {
        testEndMessage(false, "Missing %s", test->pairs[i]);
        break;
      }
      else if (strcmp(value, test->pairs[i + 1]))
      {
        testEndMessage(false, "Got value \"%s\" for %s, expected \"%s\"", value, test->pairs[i], test->pairs[i + 1]);
        break;
      }
    }

    if (i >= count)
      testEnd(true);
  }

  cupsFreeOptions(num_vars, vars);

  if (test->num_pairs == 0 && test->encoded[0])
    return;

  testBegin("cupsFormEncode(%u pairs)", (unsigned)test->num_pairs);
  for (i = 0, num_vars = 0, vars = NULL; i < test->num_pairs; i ++)
    num_vars = cupsAddOption(test->pairs[i * 2], test->pairs[i * 2 + 1], num_vars, &vars);

  data = cupsFormEncode(test->url, num_vars, vars);

  if (!data && test->encoded[0])
    testEndMessage(false, cupsGetErrorString());
  else if (data && strcmp(data, test->encoded))
    testEndMessage(false, "Got \"%s\", expected \"%s\"", data, test->encoded);
  else
    testEnd(true);

  free(data);
  cupsFreeOptions(num_vars, vars);
}


//
// 'usage()' - Show program usage.
//

static void
usage(FILE *fp)				// I - Output file
{
  fputs("Usage: ./testform [OPTIONS]\n", fp);
  fputs("Options:\n", fp);
  fputs("  --help                            Show program help.\n", fp);
  fputs("  -f FORM-DATA                      Decode form data.\n", fp);
  fputs("  -o 'NAME=VALUE [... NAME=VALUE]'  Encode form data.\n", fp);
}
