//
// ipptool command for CUPS.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright © 2020 by The Printer Working Group.
// Copyright © 2007-2021 by Apple Inc.
// Copyright © 1997-2007 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

//
// Include necessary headers...
//

#include <cups/cups-private.h>
#include <cups/raster-testpage.h>
#include <regex.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef _WIN32
#  include <windows.h>
#  ifndef R_OK
#    define R_OK 0
#  endif // !R_OK
#else
#  include <signal.h>
#  include <termios.h>
#endif // _WIN32
#ifndef O_BINARY
#  define O_BINARY 0
#endif // !O_BINARY


//
// Limits...
//

#define MAX_EXPECT	1000		// Maximum number of EXPECT directives
#define MAX_DISPLAY	200		// Maximum number of DISPLAY directives
#define MAX_MONITOR	10		// Maximum number of MONITOR-PRINTER-STATE EXPECT directives


//
// Types...
//

typedef enum ipptool_content_e		// Content Validation
{
  IPPTOOL_CONTENT_NONE,			// No content validation
  IPPTOOL_CONTENT_AVAILABLE,		// Accessible resource
  IPPTOOL_CONTENT_VALID,		// Valid resource
  IPPTOOL_CONTENT_VALID_ICON		// Valid icon resource
} ipptool_content_t;

typedef enum ipptool_output_e		// Output mode
{
  IPPTOOL_OUTPUT_QUIET,			// No output
  IPPTOOL_OUTPUT_TEST,			// Traditional CUPS test output
  IPPTOOL_OUTPUT_PLIST,			// XML plist test output
  IPPTOOL_OUTPUT_IPPSERVER,		// ippserver attribute file output
  IPPTOOL_OUTPUT_LIST,			// Tabular list output
  IPPTOOL_OUTPUT_CSV,			// Comma-separated values output
  IPPTOOL_OUTPUT_JSON			// JSON output
} ipptool_output_t;

typedef enum ipptool_transfer_e		// How to send request data
{
  IPPTOOL_TRANSFER_AUTO,		// Chunk for files, length for static
  IPPTOOL_TRANSFER_CHUNKED,		// Chunk always
  IPPTOOL_TRANSFER_LENGTH		// Length always
} ipptool_transfer_t;

typedef enum ipptool_with_e		// WITH flags
{
  IPPTOOL_WITH_LITERAL = 0,		// Match string is a literal value
  IPPTOOL_WITH_ALL = 1,			// Must match all values
  IPPTOOL_WITH_REGEX = 2,		// Match string is a regular expression
  IPPTOOL_WITH_HOSTNAME = 4,		// Match string is a URI hostname
  IPPTOOL_WITH_RESOURCE = 8,		// Match string is a URI resource
  IPPTOOL_WITH_SCHEME = 16		// Match string is a URI scheme
} ipptool_with_t;

typedef struct ipptool_expect_s		// Expected attribute info
{
  bool		optional,		// Optional attribute?
		not_expect,		// Don't expect attribute?
		expect_all;		// Expect all attributes to match/not match
  char		*name,			// Attribute name
		*of_type,		// Type name
		*same_count_as,		// Parallel attribute name
		*if_defined,		// Only required if variable defined
		*if_not_defined,	// Only required if variable is not defined
		*with_value,		// Attribute must include this value
		*with_value_from,	// Attribute must have one of the values in this attribute
		*define_match,		// Variable to define on match
		*define_no_match,	// Variable to define on no-match
		*define_value,		// Variable to define with value
		*display_match;		// Message to display on a match
  ipptool_content_t with_content;	// WITH-*-CONTENT value
  cups_array_t	*with_mime_types;	// WITH-*-MIME-TYPES value(s)
  char		*save_filespec;		// SAVE-*-CONTENT filespec
  int		repeat_limit;		// Maximum number of times to repeat
  bool		repeat_match,		// Repeat test on match
		repeat_no_match,	// Repeat test on no match
		with_distinct;		// WITH-DISTINCT-VALUES?
  int		with_flags;		// WITH flags
  int		count;			// Expected count if > 0
  ipp_tag_t	in_group;		// IN-GROUP value
} ipptool_expect_t;

typedef struct ipptool_generate_s	//// GENERATE-FILE parameters
{
  char		media[128],		// Media size name
		type[128];		// Raster type/color mode
  int		xdpi,			// Horizontal resolution
		ydpi;			// Vertical resolution
  ipp_orient_t	orientation;		// Orientation
  char		sides[128];		// Duplex mode
  int		num_copies,		// Number of copies
		num_pages;		// Number of pages
  char		format[128];		// Document format
  char		sheet_back[128];	// "pwg-raster-document-sheet-back" value
} ipptool_generate_t;

typedef struct ipptool_status_s		// Status info
{
  ipp_status_t	status;			// Expected status code
  char		*if_defined,		// Only if variable is defined
		*if_not_defined,	// Only if variable is not defined
		*define_match,		// Variable to define on match
		*define_no_match,	// Variable to define on no-match
		*define_value;		// Variable to define with value
  int		repeat_limit;		// Maximum number of times to repeat
  bool		repeat_match,		// Repeat the test when it does not match
		repeat_no_match;	// Repeat the test when it matches
} ipptool_status_t;

typedef struct ipptool_test_s		// Test Data
{
  // Global Options
  ipp_file_t	*parent;		// Parent IPP data file values
  int		password_tries;		// Number of password attempts
  http_encryption_t encryption;		// Encryption for connection
  int		family;			// Address family
  ipptool_output_t output;		// Output mode
  bool		repeat_on_busy;		// Repeat tests on server-error-busy
  bool		stop_after_include_error;
					// Stop after include errors?
  double	timeout;		// Timeout for connection
  bool		validate_headers;	// Validate HTTP headers in response?
  int		verbosity;		// Show all attributes?

  // Test Defaults
  bool		def_ignore_errors;	// Default IGNORE-ERRORS value
  ipptool_transfer_t def_transfer;	// Default TRANSFER value
  int		def_version;		// Default IPP version

  // Global State
  http_t	*http;			// HTTP connection to printer/server
  cups_file_t	*outfile;		// Output file
  bool		show_header,		// Show the test header?
		xml_header,		// `true` if XML plist header was written
		pass;			// Have we passed all tests?
  int		test_count,		// Number of tests (total)
		pass_count,		// Number of tests that passed
		fail_count,		// Number of tests that failed
		skip_count;		// Number of tests that were skipped

  // Per-Test State
  ipp_op_t	op;			// Operation code
  cups_array_t	*errors;		// Errors array
  bool		prev_pass,		// Result of previous test
		skip_previous;		// Skip on previous test failure?
  char		compression[16];	// COMPRESSION value
  useconds_t	delay;                  // Initial delay
  int		num_displayed;		// Number of displayed attributes
  char		*displayed[MAX_DISPLAY];// Displayed attributes
  int		num_expects;		// Number of expected attributes
  ipptool_expect_t expects[MAX_EXPECT],	// Expected attributes
		*expect,		// Current expected attribute
		*last_expect;		// Last EXPECT (for predicates)
  char		file[1024],		// Data filename
		file_id[1024];		// File identifier
  bool		ignore_errors;		// Ignore test failures?
  char		name[1024];		// Test name
  char		pause[1024];		// PAUSE value
  useconds_t	repeat_interval;	// Repeat interval (delay)
  int		request_id;		// Current request ID
  char		resource[512];		// Resource for request
  bool		pass_test,		// Pass this test?
		skip_test;		// Skip this test?
  int		num_statuses;		// Number of valid status codes
  ipptool_status_t statuses[100],	// Valid status codes
		*last_status;		// Last STATUS (for predicates)
  char		test_id[1024];		// Test identifier
  ipptool_transfer_t transfer;		// To chunk or not to chunk
  int		version;		// IPP version number to use
  cups_thread_t	monitor_thread;		// Monitoring thread ID
  bool		monitor_done;		// Set to `true` to stop monitor thread
  char		*monitor_uri;		// MONITOR-PRINTER-STATE URI
  useconds_t	monitor_delay,		// MONITOR-PRINTER-STATE DELAY value, if any
		monitor_interval;	// MONITOR-PRINTER-STATE DELAY interval
  int		num_monitor_expects;	// Number MONITOR-PRINTER-STATE EXPECTs
  ipptool_expect_t monitor_expects[MAX_MONITOR];
					// MONITOR-PRINTER-STATE EXPECTs
  ipptool_generate_t *generate_params;	// GENERATE-FILE parameters
  char		buffer[1024*1024];	// Output buffer
} ipptool_test_t;


//
// Globals...
//

static bool	Cancel = false;		// Cancel test?


//
// Local functions...
//

static void	add_stringf(cups_array_t *a, const char *s, ...) _CUPS_FORMAT(2, 3);
static ipptool_test_t *alloc_data(void);
static void	clear_data(ipptool_test_t *data);
static int	compare_uris(const char *a, const char *b);
static http_t	*connect_printer(ipptool_test_t *data);
static void	copy_hex_string(char *buffer, unsigned char *data, int datalen, size_t bufsize);
static int	create_file(const char *filespec, const char *resource, int idx, char *filename, size_t filenamesize);
static void	*do_monitor_printer_state(ipptool_test_t *data);
static bool	do_test(ipp_file_t *file, ipptool_test_t *data);
static bool	do_tests(const char *testfile, ipptool_test_t *data);
static bool	error_cb(ipp_file_t *f, ipptool_test_t *data, const char *error);
static bool	expect_matches(ipptool_expect_t *expect, ipp_attribute_t *attr);
static void	free_data(ipptool_test_t *data);
static http_status_t generate_file(http_t *http, ipptool_generate_t *params);
static char	*get_filename(const char *testfile, char *dst, const char *src, size_t dstsize);
static const char *get_string(ipp_attribute_t *attr, int element, int flags, char *buffer, size_t bufsize);
static char	*iso_date(const ipp_uchar_t *date);
static bool	parse_generate_file(ipp_file_t *f, ipptool_test_t *data);
static bool	parse_monitor_printer_state(ipp_file_t *f, ipptool_test_t *data);
static const char *password_cb(const char *prompt, http_t *http, const char *method, const char *resource, void *user_data);
static void	pause_message(const char *message);
static void	print_attr(cups_file_t *outfile, ipptool_output_t output, ipp_attribute_t *attr, ipp_tag_t *group);
static ipp_attribute_t *print_csv(ipptool_test_t *data, ipp_t *ipp, ipp_attribute_t *attr, int num_displayed, char **displayed, int *widths);
static void	print_fatal_error(ipptool_test_t *data, const char *s, ...) _CUPS_FORMAT(2, 3);
static void	print_ippserver_attr(ipptool_test_t *data, ipp_attribute_t *attr, int indent);
static void	print_ippserver_string(ipptool_test_t *data, const char *s, size_t len);
static void	print_json_attr(ipptool_test_t *data, ipp_attribute_t *attr, int indent);
static void	print_json_string(ipptool_test_t *data, const char *s, size_t len);
static ipp_attribute_t *print_line(ipptool_test_t *data, ipp_t *ipp, ipp_attribute_t *attr, int num_displayed, char **displayed, int *widths);
static void	print_xml_header(ipptool_test_t *data);
static void	print_xml_string(cups_file_t *outfile, const char *element, const char *s);
static void	print_xml_trailer(ipptool_test_t *data, int success, const char *message);
#ifndef _WIN32
static void	sigterm_handler(int sig);
#endif // _WIN32
static int	timeout_cb(http_t *http, void *user_data);
static bool	token_cb(ipp_file_t *f, ipptool_test_t *data, const char *token);
static void	usage(void) _CUPS_NORETURN;
static bool	valid_image(const char *filename, int *width, int *height, int *depth);
static bool	with_content(cups_array_t *errors, ipp_attribute_t *attr, ipptool_content_t content, cups_array_t *mime_types, const char *filespec);
static bool	with_distinct_values(cups_array_t *errors, ipp_attribute_t *attr);
static const char *with_flags_string(int flags);
static bool	with_value(ipptool_test_t *data, cups_array_t *errors, char *value, int flags, ipp_attribute_t *attr, char *matchbuf, size_t matchlen);
static bool	with_value_from(cups_array_t *errors, ipp_attribute_t *fromattr, ipp_attribute_t *attr, char *matchbuf, size_t matchlen);


//
// 'main()' - Parse options and do tests.
//

int					// O - Exit status
main(int  argc,				// I - Number of command-line args
     char *argv[])			// I - Command-line arguments
{
  int			i;		// Looping var
  int			status;		// Status of tests...
  char			*opt,		// Current option
			name[1024],	// Name/value buffer
			*value,		// Pointer to value
			filename[1024],	// Real filename
			testname[1024];	// Real test filename
  const char		*base,		// Base filename
			*ext,		// Extension on filename
			*testfile;	// Test file to use
  int			interval,	// Test interval in microseconds
			repeat;		// Repeat count
  ipptool_test_t	*data;		// Test data
  _cups_globals_t	*cg = _cupsGlobals();
					// Global data


#ifndef _WIN32
  // Catch SIGINT and SIGTERM...
  signal(SIGINT, sigterm_handler);
  signal(SIGTERM, sigterm_handler);
#endif // !_WIN32

  // Initialize the locale and variables...
  _cupsSetLocale(argv);

  data = alloc_data();

  // We need at least:
  //
  //   ipptool URI testfile
  interval = 0;
  repeat   = 0;
  status   = 0;
  testfile = NULL;

  for (i = 1; i < argc; i ++)
  {
    if (!strcmp(argv[i], "--help"))
    {
      free_data(data);
      usage();
    }
    else if (!strcmp(argv[i], "--ippserver"))
    {
      i ++;

      if (i >= argc)
      {
	_cupsLangPuts(stderr, _("ipptool: Missing filename for \"--ippserver\"."));
	free_data(data);
	usage();
      }

      if (data->outfile != cupsFileStdout())
	usage();

      if ((data->outfile = cupsFileOpen(argv[i], "w")) == NULL)
      {
	_cupsLangPrintf(stderr, _("%s: Unable to open \"%s\": %s"), "ipptool", argv[i], strerror(errno));
	free_data(data);
	return (1);
      }

      data->output = IPPTOOL_OUTPUT_IPPSERVER;
    }
    else if (!strcmp(argv[i], "--stop-after-include-error"))
    {
      data->stop_after_include_error = 1;
    }
    else if (!strcmp(argv[i], "--version"))
    {
      puts(CUPS_SVERSION);

      free_data(data);
      return (0);
    }
    else if (argv[i][0] == '-')
    {
      for (opt = argv[i] + 1; *opt; opt ++)
      {
        switch (*opt)
        {
	  case '4' : // Connect using IPv4 only
	      data->family = AF_INET;
	      break;

#ifdef AF_INET6
	  case '6' : // Connect using IPv6 only
	      data->family = AF_INET6;
	      break;
#endif // AF_INET6

          case 'C' : // Enable HTTP chunking
              data->def_transfer = IPPTOOL_TRANSFER_CHUNKED;
              break;

	  case 'E' : // Encrypt with TLS
	      data->encryption = HTTP_ENCRYPTION_REQUIRED;
	      break;

          case 'I' : // Ignore errors
	      data->def_ignore_errors = 1;
	      break;

          case 'L' : // Disable HTTP chunking
              data->def_transfer = IPPTOOL_TRANSFER_LENGTH;
              break;

          case 'P' : // Output to plist file
	      i ++;

	      if (i >= argc)
	      {
		_cupsLangPrintf(stderr, _("%s: Missing filename for \"-P\"."), "ipptool");
		usage();
              }

              if (data->outfile != cupsFileStdout())
                usage();

              if ((data->outfile = cupsFileOpen(argv[i], "w")) == NULL)
              {
                _cupsLangPrintf(stderr, _("%s: Unable to open \"%s\": %s"), "ipptool", argv[i], strerror(errno));
                exit(1);
              }

	      data->output = IPPTOOL_OUTPUT_PLIST;

              if (interval || repeat)
	      {
	        _cupsLangPuts(stderr, _("ipptool: \"-i\" and \"-n\" are incompatible with \"-P\" and \"-X\"."));
		usage();
	      }
              break;

          case 'R' : // Repeat on server-error-busy
              data->repeat_on_busy = 1;
              break;

	  case 'S' : // Encrypt with SSL
	      data->encryption = HTTP_ENCRYPTION_ALWAYS;
	      break;

	  case 'T' : // Set timeout
	      i ++;

	      if (i >= argc)
	      {
		_cupsLangPrintf(stderr, _("%s: Missing timeout for \"-T\"."), "ipptool");
		usage();
              }

	      data->timeout = _cupsStrScand(argv[i], NULL, localeconv());
	      break;

	  case 'V' : // Set IPP version
	      i ++;

	      if (i >= argc)
	      {
		_cupsLangPrintf(stderr, _("%s: Missing version for \"-V\"."), "ipptool");
		usage();
              }

	      if (!strcmp(argv[i], "1.0"))
	      {
	        data->def_version = 10;
	      }
	      else if (!strcmp(argv[i], "1.1"))
	      {
	        data->def_version = 11;
	      }
	      else if (!strcmp(argv[i], "2.0"))
	      {
	        data->def_version = 20;
	      }
	      else if (!strcmp(argv[i], "2.1"))
	      {
	        data->def_version = 21;
	      }
	      else if (!strcmp(argv[i], "2.2"))
	      {
	        data->def_version = 22;
	      }
	      else
	      {
		_cupsLangPrintf(stderr, _("%s: Bad version %s for \"-V\"."), "ipptool", argv[i]);
		usage();
	      }
	      break;

          case 'X' : // Produce XML output
	      data->output = IPPTOOL_OUTPUT_PLIST;

              if (interval || repeat)
	      {
	        _cupsLangPuts(stderr, _("ipptool: \"-i\" and \"-n\" are incompatible with \"-P\" and \"-X\"."));
		usage();
	      }
	      break;

          case 'c' : // CSV output
              data->output = IPPTOOL_OUTPUT_CSV;
              break;

          case 'd' : // Define a variable
	      i ++;

	      if (i >= argc)
	      {
		_cupsLangPuts(stderr, _("ipptool: Missing name=value for \"-d\"."));
		usage();
              }

              cupsCopyString(name, argv[i], sizeof(name));
	      if ((value = strchr(name, '=')) != NULL)
	        *value++ = '\0';
	      else
	        value = name + strlen(name);

	      ippFileSetVar(data->parent, name, value);
	      break;

          case 'f' : // Set the default test filename
	      i ++;

	      if (i >= argc)
	      {
		_cupsLangPuts(stderr, _("ipptool: Missing filename for \"-f\"."));
		usage();
              }

              if (access(argv[i], 0))
              {
                // Try filename.gz...
		snprintf(filename, sizeof(filename), "%s.gz", argv[i]);
                if (access(filename, 0) && filename[0] != '/'
#ifdef _WIN32
                    && (!isalpha(filename[0] & 255) || filename[1] != ':')
#endif // _WIN32
                    )
		{
		  snprintf(filename, sizeof(filename), "%s/ipptool/%s", cg->cups_datadir, argv[i]);
		  if (access(filename, 0))
		  {
		    snprintf(filename, sizeof(filename), "%s/ipptool/%s.gz", cg->cups_datadir, argv[i]);
		    if (access(filename, 0))
		      cupsCopyString(filename, argv[i], sizeof(filename));
		  }
		}
	      }
              else
		cupsCopyString(filename, argv[i], sizeof(filename));

	      ippFileSetVar(data->parent, "filename", filename);

	      if ((base = strrchr(filename, '/')) != NULL)
	        ippFileSetVar(data->parent, "basename", base + 1);
	      else
	        ippFileSetVar(data->parent, "basename", filename);

              if ((ext = strrchr(filename, '.')) != NULL)
              {
                // Guess the MIME media type based on the extension...
                if (!_cups_strcasecmp(ext, ".gif"))
                  ippFileSetVar(data->parent, "filetype", "image/gif");
                else if (!_cups_strcasecmp(ext, ".htm") || !_cups_strcasecmp(ext, ".htm.gz") || !_cups_strcasecmp(ext, ".html") || !_cups_strcasecmp(ext, ".html.gz"))
                  ippFileSetVar(data->parent, "filetype", "text/html");
                else if (!_cups_strcasecmp(ext, ".jpg") || !_cups_strcasecmp(ext, ".jpeg"))
                  ippFileSetVar(data->parent, "filetype", "image/jpeg");
                else if (!_cups_strcasecmp(ext, ".pcl") || !_cups_strcasecmp(ext, ".pcl.gz"))
                  ippFileSetVar(data->parent, "filetype", "application/vnd.hp-PCL");
                else if (!_cups_strcasecmp(ext, ".pdf"))
                  ippFileSetVar(data->parent, "filetype", "application/pdf");
                else if (!_cups_strcasecmp(ext, ".png"))
                  ippFileSetVar(data->parent, "filetype", "image/png");
                else if (!_cups_strcasecmp(ext, ".ps") || !_cups_strcasecmp(ext, ".ps.gz"))
                  ippFileSetVar(data->parent, "filetype", "application/postscript");
                else if (!_cups_strcasecmp(ext, ".pwg") || !_cups_strcasecmp(ext, ".pwg.gz") || !_cups_strcasecmp(ext, ".ras") || !_cups_strcasecmp(ext, ".ras.gz"))
                  ippFileSetVar(data->parent, "filetype", "image/pwg-raster");
                else if (!_cups_strcasecmp(ext, ".pxl") || !_cups_strcasecmp(ext, ".pxl.gz"))
                  ippFileSetVar(data->parent, "filetype", "application/vnd.hp-PCLXL");
                else if (!_cups_strcasecmp(ext, ".tif") || !_cups_strcasecmp(ext, ".tiff"))
                  ippFileSetVar(data->parent, "filetype", "image/tiff");
                else if (!_cups_strcasecmp(ext, ".txt") || !_cups_strcasecmp(ext, ".txt.gz"))
                  ippFileSetVar(data->parent, "filetype", "text/plain");
                else if (!_cups_strcasecmp(ext, ".urf") || !_cups_strcasecmp(ext, ".urf.gz"))
                  ippFileSetVar(data->parent, "filetype", "image/urf");
                else if (!_cups_strcasecmp(ext, ".xps"))
                  ippFileSetVar(data->parent, "filetype", "application/openxps");
                else
		  ippFileSetVar(data->parent, "filetype", "application/octet-stream");
              }
              else
              {
                // Use the "auto-type" MIME media type...
		ippFileSetVar(data->parent, "filetype", "application/octet-stream");
              }
	      break;

          case 'h' : // Validate response headers
              data->validate_headers = 1;
              break;

          case 'i' : // Test every N seconds
	      i ++;

	      if (i >= argc)
	      {
		_cupsLangPuts(stderr, _("ipptool: Missing seconds for \"-i\"."));
		usage();
              }
	      else
	      {
		interval = (int)(_cupsStrScand(argv[i], NULL, localeconv()) * 1000000.0);
		if (interval <= 0)
		{
		  _cupsLangPuts(stderr, _("ipptool: Invalid seconds for \"-i\"."));
		  usage();
		}
              }

              if ((data->output == IPPTOOL_OUTPUT_PLIST || data->output == IPPTOOL_OUTPUT_IPPSERVER) && interval)
	      {
	        _cupsLangPuts(stderr, _("ipptool: \"-i\" and \"-n\" are incompatible with \"--ippserver\", \"-P\", and \"-X\"."));
		usage();
	      }
	      break;

          case 'j' : // JSON output
              data->output = IPPTOOL_OUTPUT_JSON;
              break;

          case 'l' : // List as a table
              data->output = IPPTOOL_OUTPUT_LIST;
              break;

          case 'n' : // Repeat count
              i ++;

	      if (i >= argc)
	      {
		_cupsLangPuts(stderr, _("ipptool: Missing count for \"-n\"."));
		usage();
              }
	      else
		repeat = atoi(argv[i]);

              if ((data->output == IPPTOOL_OUTPUT_PLIST || data->output == IPPTOOL_OUTPUT_IPPSERVER) && repeat)
	      {
	        _cupsLangPuts(stderr, _("ipptool: \"-i\" and \"-n\" are incompatible with \"--ippserver\", \"-P\", and \"-X\"."));
		usage();
	      }
	      break;

          case 'q' : // Be quiet
              data->output = IPPTOOL_OUTPUT_QUIET;
              break;

          case 't' : // CUPS test output
              data->output = IPPTOOL_OUTPUT_TEST;
              break;

          case 'v' : // Be verbose
	      data->verbosity ++;
	      break;

	  default :
	      _cupsLangPrintf(stderr, _("%s: Unknown option \"-%c\"."), "ipptool", *opt);
	      free_data(data);
	      usage();
	}
      }
    }
    else if (!strncmp(argv[i], "ipp://", 6) || !strncmp(argv[i], "http://", 7) || !strncmp(argv[i], "ipps://", 7) || !strncmp(argv[i], "https://", 8))
    {
      // Set URI...
      if (ippFileGetVar(data->parent, "uri"))
      {
        _cupsLangPuts(stderr, _("ipptool: May only specify a single URI."));
	free_data(data);
        usage();
      }

      if (!strncmp(argv[i], "ipps://", 7) || !strncmp(argv[i], "https://", 8))
        data->encryption = HTTP_ENCRYPTION_ALWAYS;

      if (!ippFileSetVar(data->parent, "uri", argv[i]))
      {
        _cupsLangPrintf(stderr, _("ipptool: Bad URI \"%s\"."), argv[i]);
	free_data(data);
        return (1);
      }

      if (ippFileGetVar(data->parent, "uriuser") && ippFileGetVar(data->parent, "uripassword"))
	cupsSetPasswordCB2(password_cb, data->parent);
    }
    else
    {
      // Run test...
      if (!ippFileGetVar(data->parent, "uri"))
      {
        _cupsLangPuts(stderr, _("ipptool: URI required before test file."));
        _cupsLangPuts(stderr, argv[i]);
	free_data(data);
	usage();
      }

      if (access(argv[i], 0) && argv[i][0] != '/'
#ifdef _WIN32
          && (!isalpha(argv[i][0] & 255) || argv[i][1] != ':')
#endif // _WIN32
          )
      {
        snprintf(testname, sizeof(testname), "%s/ipptool/%s", cg->cups_datadir, argv[i]);
        if (access(testname, 0))
          testfile = argv[i];
        else
          testfile = testname;
      }
      else
        testfile = argv[i];

      if (access(testfile, 0))
      {
        _cupsLangPrintf(stderr, _("%s: Unable to open \"%s\": %s"), "ipptool", testfile, strerror(errno));
        status = 1;
      }
      else if (!do_tests(testfile, data))
        status = 1;
    }
  }

  if (!ippFileGetVar(data->parent, "uri") || !testfile)
  {
    free_data(data);
    usage();
  }

  // Loop if the interval is set...
  if (data->output == IPPTOOL_OUTPUT_PLIST)
  {
    print_xml_trailer(data, !status, NULL);
  }
  else if (interval > 0 && repeat > 0)
  {
    while (repeat > 1)
    {
      usleep((useconds_t)interval);
      do_tests(testfile, data);
      repeat --;
    }
  }
  else if (interval > 0)
  {
    for (;;)
    {
      usleep((useconds_t)interval);
      do_tests(testfile, data);
    }
  }

  if ((data->output == IPPTOOL_OUTPUT_TEST || (data->output == IPPTOOL_OUTPUT_PLIST && data->outfile)) && data->test_count > 1)
  {
    // Show a summary report if there were multiple tests...
    cupsFilePrintf(cupsFileStdout(), "\nSummary: %d tests, %d passed, %d failed, %d skipped\nScore: %d%%\n", data->test_count, data->pass_count, data->fail_count, data->skip_count, 100 * (data->pass_count + data->skip_count) / data->test_count);
  }

  cupsFileClose(data->outfile);
  free_data(data);

  // Exit...
  return (status);
}


//
// 'add_stringf()' - Add a formatted string to an array.
//

static void
add_stringf(cups_array_t *a,		// I - Array
            const char   *s,		// I - Printf-style format string
            ...)			// I - Additional args as needed
{
  char		buffer[10240];		// Format buffer
  va_list	ap;			// Argument pointer


  // Don't bother is the array is NULL...
  if (!a)
    return;

  // Format the message...
  va_start(ap, s);
  vsnprintf(buffer, sizeof(buffer), s, ap);
  va_end(ap);

  // Add it to the array...
  cupsArrayAdd(a, buffer);
}


//
// 'alloc_data()' - Initialize and allocate test data.
//

static ipptool_test_t *		// O - Test data
alloc_data(void)
{
  ipptool_test_t *data;		// Test data


  if ((data = calloc(1, sizeof(ipptool_test_t))) == NULL)
  {
    _cupsLangPrintf(stderr, _("ipptool: Unable to allocate memory: %s"), strerror(errno));
    exit(1);
  }

  data->parent       = ippFileNew(/*parent*/NULL, /*attr_cb*/NULL, (ipp_ferror_cb_t)error_cb, data);
  data->output       = IPPTOOL_OUTPUT_LIST;
  data->outfile      = cupsFileStdout();
  data->family       = AF_UNSPEC;
  data->def_transfer = IPPTOOL_TRANSFER_AUTO;
  data->def_version  = 20;
  data->errors       = cupsArrayNew3(NULL, NULL, NULL, 0, (cups_acopy_cb_t)strdup, (cups_afree_cb_t)free);
  data->pass         = true;
  data->prev_pass    = true;
  data->request_id   = (cupsGetRand() % 1000) * 137;
  data->show_header  = true;

  ippFileSetVar(data->parent, "date-start", iso_date(ippTimeToDate(time(NULL))));

  return (data);
}


//
// 'clear_data()' - Clear per-test data...
//

static void
clear_data(ipptool_test_t *data)	// I - Test data
{
  int		i;			// Looping var
  ipptool_expect_t *expect;		// Current EXPECT


  cupsArrayClear(data->errors);

  for (i = 0; i < data->num_displayed; i ++)
    free(data->displayed[i]);
  data->num_displayed = 0;

  for (i = data->num_expects, expect = data->expects; i > 0; i --, expect ++)
  {
    free(expect->name);
    free(expect->of_type);
    free(expect->same_count_as);
    free(expect->if_defined);
    free(expect->if_not_defined);
    free(expect->with_value);
    free(expect->define_match);
    free(expect->define_no_match);
    free(expect->define_value);
    free(expect->display_match);
    cupsArrayDelete(expect->with_mime_types);
    free(expect->save_filespec);
  }
  data->num_expects = 0;

  for (i = 0; i < data->num_statuses; i ++)
  {
    free(data->statuses[i].if_defined);
    free(data->statuses[i].if_not_defined);
    free(data->statuses[i].define_match);
    free(data->statuses[i].define_no_match);
    free(data->statuses[i].define_value);
  }
  data->num_statuses = 0;

  free(data->monitor_uri);
  data->monitor_uri = NULL;

  for (i = data->num_monitor_expects, expect = data->monitor_expects; i > 0; i --, expect ++)
  {
    free(expect->name);
    free(expect->of_type);
    free(expect->same_count_as);
    free(expect->if_defined);
    free(expect->if_not_defined);
    free(expect->with_value);
    free(expect->define_match);
    free(expect->define_no_match);
    free(expect->define_value);
    free(expect->display_match);
  }
  data->num_monitor_expects = 0;

  free(data->generate_params);
  data->generate_params = NULL;
}


//
// 'compare_uris()' - Compare two URIs...
//

static int                              // O - Result of comparison
compare_uris(const char *a,             // I - First URI
             const char *b)             // I - Second URI
{
  char  ascheme[32],                    // Components of first URI
        auserpass[256],
        ahost[256],
        aresource[256];
  int   aport;
  char  bscheme[32],                    // Components of second URI
        buserpass[256],
        bhost[256],
        bresource[256];
  int   bport;
  char  *ptr;                           // Pointer into string
  int   result;                         // Result of comparison


  // Separate the URIs into their components...
  if (httpSeparateURI(HTTP_URI_CODING_ALL, a, ascheme, sizeof(ascheme), auserpass, sizeof(auserpass), ahost, sizeof(ahost), &aport, aresource, sizeof(aresource)) < HTTP_URI_STATUS_OK)
    return (-1);

  if (httpSeparateURI(HTTP_URI_CODING_ALL, b, bscheme, sizeof(bscheme), buserpass, sizeof(buserpass), bhost, sizeof(bhost), &bport, bresource, sizeof(bresource)) < HTTP_URI_STATUS_OK)
    return (-1);

  // Strip trailing dots from the host components, if present...
  if ((ptr = ahost + strlen(ahost) - 1) > ahost && *ptr == '.')
    *ptr = '\0';

  if ((ptr = bhost + strlen(bhost) - 1) > bhost && *ptr == '.')
    *ptr = '\0';

  // Compare each component...
  if ((result = _cups_strcasecmp(ascheme, bscheme)) != 0)
    return (result);

  if ((result = strcmp(auserpass, buserpass)) != 0)
    return (result);

  if ((result = _cups_strcasecmp(ahost, bhost)) != 0)
    return (result);

  if (aport != bport)
    return (aport - bport);

  if (!_cups_strcasecmp(ascheme, "mailto") || !_cups_strcasecmp(ascheme, "urn"))
    return (_cups_strcasecmp(aresource, bresource));
  else
    return (strcmp(aresource, bresource));
}


//
// 'connect_printer()' - Connect to the printer.
//

static http_t *				// O - HTTP connection or `NULL` on error
connect_printer(ipptool_test_t *data)	// I - Test data
{
  const char	*scheme = ippFileGetVar(data->parent, "scheme"),
		*hostname = ippFileGetVar(data->parent, "hostname"),
		*port = ippFileGetVar(data->parent, "port");
					// URI fields
  http_encryption_t encryption;		// Encryption mode
  http_t	*http;			// HTTP connection


  if (!scheme || !hostname || !port)
  {
    // This should never happen, but just in case...
    print_fatal_error(data, "Missing printer/system URI.");
    return (NULL);
  }

  if (!_cups_strcasecmp(scheme, "https") || !_cups_strcasecmp(scheme, "ipps") || atoi(port) == 443)
    encryption = HTTP_ENCRYPTION_ALWAYS;
  else
    encryption = data->encryption;

  if ((http = httpConnect2(hostname, atoi(port), NULL, data->family, encryption, 1, 30000, NULL)) == NULL)
  {
    print_fatal_error(data, "Unable to connect to '%s' on port %s: %s", hostname, port, cupsGetErrorString());
    return (NULL);
  }

  httpSetDefaultField(data->http, HTTP_FIELD_ACCEPT_ENCODING, "deflate, gzip, identity");

  if (data->timeout > 0.0)
    httpSetTimeout(http, data->timeout, timeout_cb, NULL);

  return (http);
}


//
// 'copy_hex_string()' - Copy an octetString to a C string and encode as hex if
//                       needed.
//

static void
copy_hex_string(char          *buffer,	// I - String buffer
		unsigned char *data,	// I - octetString data
		int           datalen,	// I - octetString length
		size_t        bufsize)	// I - Size of string buffer
{
  char		*bufptr,		// Pointer into string buffer
		*bufend = buffer + bufsize - 2;
					// End of string buffer
  unsigned char	*dataptr,		// Pointer into octetString data
		*dataend = data + datalen;
					// End of octetString data
  static const char *hexdigits = "0123456789ABCDEF";
					// Hex digits


  // First see if there are any non-ASCII bytes in the octetString...
  for (dataptr = data; dataptr < dataend; dataptr ++)
  {
    if (*dataptr < 0x20 || *dataptr >= 0x7f)
      break;
  }

  if (dataptr < dataend)
  {
    // Yes, encode as hex...
    *buffer = '<';

    for (bufptr = buffer + 1, dataptr = data; bufptr < bufend && dataptr < dataend; dataptr ++)
    {
      *bufptr++ = hexdigits[*dataptr >> 4];
      *bufptr++ = hexdigits[*dataptr & 15];
    }

    if (bufptr < bufend)
      *bufptr++ = '>';

    *bufptr = '\0';
  }
  else
  {
    // No, copy as a string...
    if ((size_t)datalen > bufsize)
      datalen = (int)bufsize - 1;

    memcpy(buffer, data, (size_t)datalen);
    buffer[datalen] = '\0';
  }
}


//
// 'create_file()' - Create a file for content checks.
//

static int				// O - File descriptor or -1 on error
create_file(const char *filespec,	// I - Filespec string or NULL
            const char *resource,	// I - Resource name
            int        idx,		// I - Value index
            char       *filename,	// I - Filename buffer
            size_t     filenamesize)	// I - Filename buffer size
{
  char	*ptr,				// Pointer into filename
	*end,				// End of filename buffer
	base_resource[256],		// Base name for resource
	*base_ext;			// Extension for resource


  // If there is no filespec, just create a temporary file...
  if (!filespec)
    return (cupsCreateTempFd(NULL, NULL, filename, filenamesize));

  // Convert resource path to base name...
  if ((ptr = strrchr(resource, '/')) != NULL)
    cupsCopyString(base_resource, ptr + 1, sizeof(base_resource));
  else
    cupsCopyString(base_resource, resource, sizeof(base_resource));

  if ((base_ext = strrchr(base_resource, '.')) != NULL)
    *base_ext++ = '\0';
  else
    base_ext = base_resource + strlen(base_resource);

  // Format the filename...
  for (ptr = filename, end = filename + filenamesize - 1; *filespec && ptr < end;)
  {
    if (!strncmp(filespec, "%basename%", 10))
    {
      cupsCopyString(ptr, base_resource, (size_t)(end - ptr + 1));
      ptr += strlen(ptr);
      filespec += 10;
    }
    else if (!strncmp(filespec, "%ext%", 5))
    {
      cupsCopyString(ptr, base_ext, (size_t)(end - ptr + 1));
      ptr += strlen(ptr);
      filespec += 5;
    }
    else if (!strncmp(filespec, "%index%", 7))
    {
      snprintf(ptr, (size_t)(end - ptr + 1), "%u", (unsigned)idx);
      ptr += strlen(ptr);
      filespec += 7;
    }
    else if (*filespec == '%')
    {
      filespec ++;

      if (*filespec == '%')
        *ptr++ = '%';

      while (*filespec != '%')
        filespec ++;

      if (*filespec)
        filespec ++;
    }
    else
    {
      // Copy literal character...
      *ptr++ = *filespec++;
    }
  }

  *ptr = '\0';

  // Try creating the file...
  return (open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0666));
}


//
// 'do_monitor_printer_state()' - Do the MONITOR-PRINTER-STATE tests in the background.
//

static void *				// O - Thread exit status
do_monitor_printer_state(
    ipptool_test_t *data)		// I - Test data
{
  int		i, j;			// Looping vars
  char		scheme[32],		// URI scheme
		userpass[32],		// URI username:password
		host[256],		// URI hostname/IP address
		resource[256];		// URI resource path
  int		port;			// URI port number
  http_encryption_t encryption;		// Encryption to use
  http_t	*http;			// Connection to printer
  ipp_t		*request,		// IPP request
		*response = NULL;	// IPP response
  http_status_t	status;			// Request status
  ipp_attribute_t *found;		// Found attribute
  ipptool_expect_t *expect;		// Current EXPECT test
  char		buffer[131072];		// Copy buffer
  int		num_pattrs;		// Number of printer attributes
  const char	*pattrs[100];		// Printer attributes we care about


  if (getenv("IPPTOOL_DEBUG"))
    fprintf(stderr, "ipptool: Monitoring printer '%s' in the background.\n", data->monitor_uri);

  // Connect to the printer...
  if (httpSeparateURI(HTTP_URI_CODING_ALL, data->monitor_uri, scheme, sizeof(scheme), userpass, sizeof(userpass), host, sizeof(host), &port, resource, sizeof(resource)) < HTTP_URI_STATUS_OK)
  {
    print_fatal_error(data, "Bad printer URI \"%s\".", data->monitor_uri);
    return (NULL);
  }

  if (!_cups_strcasecmp(scheme, "https") || !_cups_strcasecmp(scheme, "ipps") || port == 443)
    encryption = HTTP_ENCRYPTION_ALWAYS;
  else
    encryption = data->encryption;

  if ((http = httpConnect2(host, port, NULL, data->family, encryption, 1, 30000, NULL)) == NULL)
  {
    print_fatal_error(data, "Unable to connect to \"%s\" on port %d - %s", host, port, cupsGetErrorString());
    return (0);
  }

  httpSetDefaultField(http, HTTP_FIELD_ACCEPT_ENCODING, "deflate, gzip, identity");

  if (data->timeout > 0.0)
    httpSetTimeout(http, data->timeout, timeout_cb, NULL);

  // Wait for the initial delay as needed...
  if (data->monitor_delay)
    usleep(data->monitor_delay);

  // Create a query request that we'll reuse...
  request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
  ippSetRequestId(request, data->request_id * 100 - 1);
  ippSetVersion(request, data->version / 10, data->version % 10);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, data->monitor_uri);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsGetUser());

  for (i = data->num_monitor_expects, expect = data->monitor_expects, num_pattrs = 0; i > 0; i --, expect ++)
  {
    // Add EXPECT attribute names...
    for (j = 0; j < num_pattrs; j ++)
    {
      if (!strcmp(expect->name, pattrs[j]))
        break;
    }

    if (j >= num_pattrs && num_pattrs < (int)(sizeof(pattrs) / sizeof(pattrs[0])))
      pattrs[num_pattrs ++] = expect->name;
  }

  if (num_pattrs > 0)
    ippAddStrings(request, IPP_TAG_OPERATION, IPP_CONST_TAG(IPP_TAG_KEYWORD), "requested-attributes", num_pattrs, NULL, pattrs);

  // Loop until we need to stop...
  while (!data->monitor_done && !Cancel)
  {
    // Poll the printer state...
    ippSetRequestId(request, ippGetRequestId(request) + 1);

    if ((status = cupsSendRequest(http, request, resource, ippGetLength(request))) != HTTP_STATUS_ERROR)
    {
      response = cupsGetResponse(http, resource);
      status   = httpGetStatus(http);
    }

    if (!data->monitor_done && !Cancel && status == HTTP_STATUS_ERROR && httpGetError(data->http) != EINVAL &&
#ifdef _WIN32
	httpGetError(data->http) != WSAETIMEDOUT)
#else
	httpGetError(data->http) != ETIMEDOUT)
#endif // _WIN32
    {
      if (!httpReconnect2(http, 30000, NULL))
	break;
    }
    else if (status == HTTP_STATUS_ERROR || status == HTTP_STATUS_CUPS_AUTHORIZATION_CANCELED)
    {
      break;
    }
    else if (status != HTTP_STATUS_OK)
    {
      httpFlush(http);

      if (status == HTTP_STATUS_UNAUTHORIZED)
	continue;

      break;
    }

    for (i = data->num_monitor_expects, expect = data->monitor_expects; i > 0; i --, expect ++)
    {
      if (expect->if_defined && !ippFileGetVar(data->parent, expect->if_defined))
	continue;

      if (expect->if_not_defined && ippFileGetVar(data->parent, expect->if_not_defined))
	continue;

      found = ippFindAttribute(response, expect->name, IPP_TAG_ZERO);

      if ((found && expect->not_expect) ||
	  (!found && !(expect->not_expect || expect->optional)) ||
	  (found && !expect_matches(expect, found)) ||
	  (expect->in_group && ippGetGroupTag(found) != expect->in_group) ||
	  (expect->with_distinct && !with_distinct_values(NULL, found)))
      {
	if (expect->define_no_match)
	{
	  ippFileSetVar(data->parent, expect->define_no_match, "1");
	  data->monitor_done = 1;
	}
	break;
      }

      if (found)
	ippAttributeString(found, buffer, sizeof(buffer));

      if (found && !with_value(data, NULL, expect->with_value, expect->with_flags, found, buffer, sizeof(buffer)))
      {
	if (expect->define_no_match)
	{
	  ippFileSetVar(data->parent, expect->define_no_match, "1");
	  data->monitor_done = 1;
	}
	break;
      }

      if (found && expect->count > 0 && ippGetCount(found) != expect->count)
      {
	if (expect->define_no_match)
	{
	  ippFileSetVar(data->parent, expect->define_no_match, "1");
	  data->monitor_done = 1;
	}
	break;
      }

      if (found && expect->display_match && (data->output == IPPTOOL_OUTPUT_TEST || (data->output == IPPTOOL_OUTPUT_PLIST && data->outfile != cupsFileStdout())))
	cupsFilePrintf(cupsFileStdout(), "CONT]\n\n%s\n\n    %-68.68s [", expect->display_match, data->name);

      if (found && expect->define_match)
      {
	ippFileSetVar(data->parent, expect->define_match, "1");
	data->monitor_done = 1;
      }

      if (found && expect->define_value)
      {
	if (!expect->with_value)
	{
	  int last = ippGetCount(found) - 1;
					// Last element in attribute

	  switch (ippGetValueTag(found))
	  {
	    case IPP_TAG_ENUM :
	    case IPP_TAG_INTEGER :
		snprintf(buffer, sizeof(buffer), "%d", ippGetInteger(found, last));
		break;

	    case IPP_TAG_BOOLEAN :
		if (ippGetBoolean(found, last))
		  cupsCopyString(buffer, "true", sizeof(buffer));
		else
		  cupsCopyString(buffer, "false", sizeof(buffer));
		break;

	    case IPP_TAG_CHARSET :
	    case IPP_TAG_KEYWORD :
	    case IPP_TAG_LANGUAGE :
	    case IPP_TAG_MIMETYPE :
	    case IPP_TAG_NAME :
	    case IPP_TAG_NAMELANG :
	    case IPP_TAG_TEXT :
	    case IPP_TAG_TEXTLANG :
	    case IPP_TAG_URI :
	    case IPP_TAG_URISCHEME :
		cupsCopyString(buffer, ippGetString(found, last, NULL), sizeof(buffer));
		break;

	    default :
		ippAttributeString(found, buffer, sizeof(buffer));
		break;
	  }
	}

	ippFileSetVar(data->parent, expect->define_value, buffer);
	data->monitor_done = 1;
      }
    }

    if (i == 0)
      data->monitor_done = 1;		// All tests passed

    ippDelete(response);
    response = NULL;

    // Sleep between requests...
    if (data->monitor_done || Cancel)
      break;

    usleep(data->monitor_interval);
  }

  // Close the connection to the printer and return...
  httpClose(http);
  ippDelete(request);
  ippDelete(response);

  return (NULL);
}


//
// 'do_test()' - Do a single test from the test file.
//

static bool				// O - `true` on success, `false` on failure
do_test(ipp_file_t     *f,		// I - IPP data file
        ipptool_test_t *data)		// I - Test data

{
  int	        i;			// Looping var
  bool		status_ok;		// Did we get a matching status?
  int		repeat_count = 0;	// Repeat count
  bool		repeat_test;		// Repeat the test?
  ipptool_expect_t *expect;		// Current expected attribute
  ipp_t		*request,		// IPP request
		*response;		// IPP response
  size_t	length;			// Length of IPP request
  http_status_t	status;			// HTTP status
  cups_array_t	*a;			// Duplicate attribute array
  ipp_tag_t	group;			// Current group
  ipp_attribute_t *attrptr,		// Attribute pointer
		*found;			// Found attribute
  char		temp[1024];		// Temporary string
  cups_file_t	*reqfile;		// File to send
  ssize_t	bytes;			// Bytes read/written
  int		widths[200];		// Width of columns
  const char	*error;			// Current error


  if (Cancel)
    return (false);

  if (getenv("IPPTOOL_DEBUG"))
    fprintf(stderr, "ipptool: Doing test '%s', num_expects=%u, num_statuses=%u.\n", data->name, (unsigned)data->num_expects, (unsigned)data->num_statuses);

  // Show any PAUSE message, as needed...
  if (data->pause[0])
  {
    if (!data->skip_test && !data->pass_test)
      pause_message(data->pause);

    data->pause[0] = '\0';
  }

  // Start the background thread as needed...
  if (data->monitor_uri)
  {
    data->monitor_done   = false;
    data->monitor_thread = cupsThreadCreate((cups_thread_func_t)do_monitor_printer_state, data);
  }

  // Take over control of the attributes in the request...
  request = ippFileGetAttributes(f);
  ippFileSetAttributes(f, NULL);

  // Submit the IPP request...
  data->test_count ++;

  ippSetOperation(request, data->op);
  ippSetVersion(request, data->version / 10, data->version % 10);
  ippSetRequestId(request, data->request_id);

  if (data->output == IPPTOOL_OUTPUT_PLIST)
  {
    cupsFilePuts(data->outfile, "<dict>\n");
    cupsFilePuts(data->outfile, "<key>Name</key>\n");
    print_xml_string(data->outfile, "string", data->name);
    if (data->file_id[0])
    {
      cupsFilePuts(data->outfile, "<key>FileId</key>\n");
      print_xml_string(data->outfile, "string", data->file_id);
    }
    if (data->test_id[0])
    {
      cupsFilePuts(data->outfile, "<key>TestId</key>\n");
      print_xml_string(data->outfile, "string", data->test_id);
    }
    cupsFilePuts(data->outfile, "<key>Version</key>\n");
    cupsFilePrintf(data->outfile, "<string>%d.%d</string>\n", data->version / 10, data->version % 10);
    cupsFilePuts(data->outfile, "<key>Operation</key>\n");
    print_xml_string(data->outfile, "string", ippOpString(ippGetOperation(request)));
    cupsFilePuts(data->outfile, "<key>RequestId</key>\n");
    cupsFilePrintf(data->outfile, "<integer>%d</integer>\n", data->request_id);
    cupsFilePuts(data->outfile, "<key>RequestAttributes</key>\n");
    cupsFilePuts(data->outfile, "<array>\n");
    if (ippGetFirstAttribute(request))
    {
      cupsFilePuts(data->outfile, "<dict>\n");
      for (attrptr = ippGetFirstAttribute(request), group = ippGetGroupTag(attrptr); attrptr; attrptr = ippGetNextAttribute(request))
	print_attr(data->outfile, data->output, attrptr, &group);
      cupsFilePuts(data->outfile, "</dict>\n");
    }
    cupsFilePuts(data->outfile, "</array>\n");
  }

  if (data->output == IPPTOOL_OUTPUT_TEST || (data->output == IPPTOOL_OUTPUT_PLIST && data->outfile != cupsFileStdout()))
  {
    if (data->verbosity)
    {
      cupsFilePrintf(cupsFileStdout(), "    %s:\n", ippOpString(ippGetOperation(request)));

      for (attrptr = ippGetFirstAttribute(request); attrptr; attrptr = ippGetNextAttribute(request))
	print_attr(cupsFileStdout(), IPPTOOL_OUTPUT_TEST, attrptr, NULL);
    }

    cupsFilePrintf(cupsFileStdout(), "    %-68.68s [", data->name);
  }

  if ((data->skip_previous && !data->prev_pass) || data->skip_test || data->pass_test)
  {
    if (!data->pass_test)
      data->skip_count ++;

    ippDelete(request);
    request  = NULL;
    response = NULL;

    if (data->output == IPPTOOL_OUTPUT_PLIST)
    {
      cupsFilePuts(data->outfile, "<key>Successful</key>\n");
      cupsFilePuts(data->outfile, "<true />\n");
      cupsFilePuts(data->outfile, "<key>Skipped</key>\n");
      if (data->pass_test)
	cupsFilePuts(data->outfile, "<false />\n");
      else
	cupsFilePuts(data->outfile, "<true />\n");
      cupsFilePuts(data->outfile, "<key>StatusCode</key>\n");
      if (data->pass_test)
	print_xml_string(data->outfile, "string", "pass");
      else
	print_xml_string(data->outfile, "string", "skip");
      cupsFilePuts(data->outfile, "<key>ResponseAttributes</key>\n");
      cupsFilePuts(data->outfile, "<dict />\n");
    }

    if (data->output == IPPTOOL_OUTPUT_TEST || (data->output == IPPTOOL_OUTPUT_PLIST && data->outfile != cupsFileStdout()))
    {
      if (data->pass_test)
	cupsFilePuts(cupsFileStdout(), "PASS]\n");
      else
	cupsFilePuts(cupsFileStdout(), "SKIP]\n");
    }

    goto skip_error;
  }

  data->password_tries = 0;

  do
  {
    if (data->delay > 0)
      usleep(data->delay);

    if (getenv("IPPTOOL_DEBUG"))
      fprintf(stderr, "ipptool: Sending %s request to '%s'.\n", ippOpString(ippGetOperation(request)), data->resource);

    data->delay = data->repeat_interval;
    repeat_count ++;

    status = HTTP_STATUS_OK;

    if (data->transfer == IPPTOOL_TRANSFER_CHUNKED || (data->transfer == IPPTOOL_TRANSFER_AUTO && (data->file[0] || data->generate_params)))
    {
      // Send request using chunking - a 0 length means "chunk".
      length = 0;
    }
    else
    {
      // Send request using content length...
      length = ippGetLength(request);

      if (data->file[0] && (reqfile = cupsFileOpen(data->file, "r")) != NULL)
      {
        // Read the file to get the uncompressed file size...
	while ((bytes = cupsFileRead(reqfile, data->buffer, sizeof(data->buffer))) > 0)
	  length += (size_t)bytes;

	cupsFileClose(reqfile);
      }
    }

    // Send the request...
    data->prev_pass = true;
    repeat_test     = false;
    response        = NULL;

    if (status != HTTP_STATUS_ERROR)
    {
      while (!response && !Cancel && data->prev_pass)
      {
        ippSetRequestId(request, ++ data->request_id);

	status = cupsSendRequest(data->http, request, data->resource, length);

	if (data->compression[0])
	  httpSetField(data->http, HTTP_FIELD_CONTENT_ENCODING, data->compression);

	if (!Cancel && status == HTTP_STATUS_CONTINUE && ippGetState(request) == IPP_STATE_DATA && data->file[0])
	{
	  // Send attached file...
	  if ((reqfile = cupsFileOpen(data->file, "r")) != NULL)
	  {
	    while (!Cancel && (bytes = cupsFileRead(reqfile, data->buffer, sizeof(data->buffer))) > 0)
	    {
	      if ((status = cupsWriteRequestData(data->http, data->buffer, (size_t)bytes)) != HTTP_STATUS_CONTINUE)
		break;
            }

	    cupsFileClose(reqfile);
	  }
	  else
	  {
	    snprintf(data->buffer, sizeof(data->buffer), "%s: %s", data->file, strerror(errno));
	    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, data->buffer, 0);

	    status = HTTP_STATUS_ERROR;
	  }
	}
	else if (!Cancel && status == HTTP_STATUS_CONTINUE && ippGetState(request) == IPP_STATE_DATA && data->generate_params)
	{
	  // Generate attached file...
	  status = generate_file(data->http, data->generate_params);
	}

        // Get the server's response...
	if (!Cancel && status != HTTP_STATUS_ERROR)
	{
	  response = cupsGetResponse(data->http, data->resource);
	  status   = httpGetStatus(data->http);
	}

	if (!Cancel && status == HTTP_STATUS_ERROR && httpGetError(data->http) != EINVAL &&
#ifdef _WIN32
	    httpGetError(data->http) != WSAETIMEDOUT)
#else
	    httpGetError(data->http) != ETIMEDOUT)
#endif // _WIN32
	{
	  if (!httpReconnect2(data->http, 30000, NULL))
	    data->prev_pass = false;
	}
	else if (status == HTTP_STATUS_ERROR || status == HTTP_STATUS_CUPS_AUTHORIZATION_CANCELED)
	{
	  data->prev_pass = false;
	  break;
	}
	else if (status != HTTP_STATUS_OK)
	{
	  httpFlush(data->http);

	  if (status == HTTP_STATUS_UNAUTHORIZED)
	    continue;

	  break;
	}
      }
    }

    if (!Cancel && status == HTTP_STATUS_ERROR && httpGetError(data->http) != EINVAL &&
#ifdef _WIN32
	httpGetError(data->http) != WSAETIMEDOUT)
#else
	httpGetError(data->http) != ETIMEDOUT)
#endif // _WIN32
    {
      if (!httpReconnect2(data->http, 30000, NULL))
	data->prev_pass = false;
    }
    else if (status == HTTP_STATUS_ERROR)
    {
      if (!Cancel)
	httpReconnect2(data->http, 30000, NULL);

      data->prev_pass = false;
    }
    else if (status != HTTP_STATUS_OK)
    {
      httpFlush(data->http);
      data->prev_pass = false;
    }

    // Check results of request...
    cupsArrayClear(data->errors);

    if (httpGetVersion(data->http) != HTTP_VERSION_1_1)
    {
      int version = (int)httpGetVersion(data->http);

      add_stringf(data->errors, "Bad HTTP version (%d.%d)", version / 100, version % 100);
    }

    if (data->validate_headers)
    {
      const char *header;               // HTTP header value

      if ((header = httpGetField(data->http, HTTP_FIELD_CONTENT_TYPE)) == NULL || _cups_strcasecmp(header, "application/ipp"))
	add_stringf(data->errors, "Bad HTTP Content-Type in response (%s)", header && *header ? header : "<missing>");

      if ((header = httpGetField(data->http, HTTP_FIELD_DATE)) != NULL && *header && httpGetDateTime(header) == 0)
	add_stringf(data->errors, "Bad HTTP Date in response (%s)", header);
    }

    if (!response)
    {
      // No response, log error...
      add_stringf(data->errors, "IPP request failed with status %s (%s)", ippErrorString(cupsGetError()), cupsGetErrorString());
    }
    else
    {
      // Collect common attribute values...
      if ((attrptr = ippFindAttribute(response, "job-id", IPP_TAG_INTEGER)) != NULL)
      {
	snprintf(temp, sizeof(temp), "%d", ippGetInteger(attrptr, 0));
	ippFileSetVar(data->parent, "job-id", temp);
      }

      if ((attrptr = ippFindAttribute(response, "job-uri", IPP_TAG_URI)) != NULL)
	ippFileSetVar(data->parent, "job-uri", ippGetString(attrptr, 0, NULL));

      if ((attrptr = ippFindAttribute(response, "notify-subscription-id", IPP_TAG_INTEGER)) != NULL)
      {
	snprintf(temp, sizeof(temp), "%d", ippGetInteger(attrptr, 0));
	ippFileSetVar(data->parent, "notify-subscription-id", temp);
      }

      // Check response, validating groups and attributes and logging errors as needed...
      if (ippGetState(response) != IPP_STATE_DATA)
	add_stringf(data->errors, "Missing end-of-attributes-tag in response (RFC 2910 section 3.5.1)");

      if (data->version)
      {
        int major, minor;		// IPP version

        major = ippGetVersion(response, &minor);

        if (major != (data->version / 10) || minor != (data->version % 10))
	  add_stringf(data->errors, "Bad version %d.%d in response - expected %d.%d (RFC 8011 section 4.1.8).", major, minor, data->version / 10, data->version % 10);
      }

      if (ippGetRequestId(response) != data->request_id)
	add_stringf(data->errors, "Bad request ID %d in response - expected %d (RFC 8011 section 4.1.1)", ippGetRequestId(response), data->request_id);

      attrptr = ippGetFirstAttribute(response);
      if (!attrptr)
      {
	add_stringf(data->errors, "Missing first attribute \"attributes-charset (charset)\" in group operation-attributes-tag (RFC 8011 section 4.1.4).");
      }
      else
      {
	if (!ippGetName(attrptr) || ippGetValueTag(attrptr) != IPP_TAG_CHARSET || ippGetGroupTag(attrptr) != IPP_TAG_OPERATION || ippGetCount(attrptr) != 1 ||strcmp(ippGetName(attrptr), "attributes-charset"))
	  add_stringf(data->errors, "Bad first attribute \"%s (%s%s)\" in group %s, expected \"attributes-charset (charset)\" in group operation-attributes-tag (RFC 8011 section 4.1.4).", ippGetName(attrptr) ? ippGetName(attrptr) : "(null)", ippGetCount(attrptr) > 1 ? "1setOf " : "", ippTagString(ippGetValueTag(attrptr)), ippTagString(ippGetGroupTag(attrptr)));

	attrptr = ippGetNextAttribute(response);
	if (!attrptr)
	  add_stringf(data->errors, "Missing second attribute \"attributes-natural-language (naturalLanguage)\" in group operation-attributes-tag (RFC 8011 section 4.1.4).");
	else if (!ippGetName(attrptr) || ippGetValueTag(attrptr) != IPP_TAG_LANGUAGE || ippGetGroupTag(attrptr) != IPP_TAG_OPERATION || ippGetCount(attrptr) != 1 || strcmp(ippGetName(attrptr), "attributes-natural-language"))
	  add_stringf(data->errors, "Bad first attribute \"%s (%s%s)\" in group %s, expected \"attributes-natural-language (naturalLanguage)\" in group operation-attributes-tag (RFC 8011 section 4.1.4).", ippGetName(attrptr) ? ippGetName(attrptr) : "(null)", ippGetCount(attrptr) > 1 ? "1setOf " : "", ippTagString(ippGetValueTag(attrptr)), ippTagString(ippGetGroupTag(attrptr)));
      }

      if ((attrptr = ippFindAttribute(response, "status-message", IPP_TAG_ZERO)) != NULL)
      {
        const char *status_message = ippGetString(attrptr, 0, NULL);
						// String value

	if (ippGetValueTag(attrptr) != IPP_TAG_TEXT)
	  add_stringf(data->errors, "status-message (text(255)) has wrong value tag %s (RFC 8011 section 4.1.6.2).", ippTagString(ippGetValueTag(attrptr)));
	if (ippGetGroupTag(attrptr) != IPP_TAG_OPERATION)
	  add_stringf(data->errors, "status-message (text(255)) has wrong group tag %s (RFC 8011 section 4.1.6.2).", ippTagString(ippGetGroupTag(attrptr)));
	if (ippGetCount(attrptr) != 1)
	  add_stringf(data->errors, "status-message (text(255)) has %u values (RFC 8011 section 4.1.6.2).", (unsigned)ippGetCount(attrptr));
	if (status_message && strlen(status_message) > 255)
	  add_stringf(data->errors, "status-message (text(255)) has bad length %u (RFC 8011 section 4.1.6.2).", (unsigned)strlen(status_message));
      }

      if ((attrptr = ippFindAttribute(response, "detailed-status-message",
				       IPP_TAG_ZERO)) != NULL)
      {
        const char *detailed_status_message = ippGetString(attrptr, 0, NULL);
						// String value

	if (ippGetValueTag(attrptr) != IPP_TAG_TEXT)
	  add_stringf(data->errors, "detailed-status-message (text(MAX)) has wrong value tag %s (RFC 8011 section 4.1.6.3).", ippTagString(ippGetValueTag(attrptr)));
	if (ippGetGroupTag(attrptr) != IPP_TAG_OPERATION)
	  add_stringf(data->errors, "detailed-status-message (text(MAX)) has wrong group tag %s (RFC 8011 section 4.1.6.3).", ippTagString(ippGetGroupTag(attrptr)));
	if (ippGetCount(attrptr) != 1)
	  add_stringf(data->errors, "detailed-status-message (text(MAX)) has %u values (RFC 8011 section 4.1.6.3).", (unsigned)ippGetCount(attrptr));
	if (detailed_status_message && strlen(detailed_status_message) > 1023)
	  add_stringf(data->errors, "detailed-status-message (text(MAX)) has bad length %u (RFC 8011 section 4.1.6.3).", (unsigned)strlen(detailed_status_message));
      }

      a = cupsArrayNew3((cups_array_cb_t)_cupsArrayStrcmp, NULL, NULL, 0, NULL, NULL);

      for (attrptr = ippGetFirstAttribute(response), group = ippGetGroupTag(attrptr);
	   attrptr;
	   attrptr = ippGetNextAttribute(response))
      {
	if (ippGetGroupTag(attrptr) != group)
	{
	  bool out_of_order = false;	// Are attribute groups out-of-order?
	  cupsArrayClear(a);

	  switch (ippGetGroupTag(attrptr))
	  {
	    case IPP_TAG_ZERO :
		break;

	    case IPP_TAG_OPERATION :
		out_of_order = true;
		break;

	    case IPP_TAG_UNSUPPORTED_GROUP :
		if (group != IPP_TAG_OPERATION)
		  out_of_order = true;
		break;

	    case IPP_TAG_JOB :
	    case IPP_TAG_PRINTER :
		if (group != IPP_TAG_OPERATION && group != IPP_TAG_UNSUPPORTED_GROUP)
		  out_of_order = true;
		break;

	    case IPP_TAG_SUBSCRIPTION :
		if (group > ippGetGroupTag(attrptr) && group != IPP_TAG_DOCUMENT)
		  out_of_order = true;
		break;

	    default :
		if (group > ippGetGroupTag(attrptr))
		  out_of_order = true;
		break;
	  }

	  if (out_of_order)
	    add_stringf(data->errors, "Attribute groups out of order (%s < %s)", ippTagString(ippGetGroupTag(attrptr)), ippTagString(group));

	  if (ippGetGroupTag(attrptr) != IPP_TAG_ZERO)
	    group = ippGetGroupTag(attrptr);
	}

	if (!ippValidateAttribute(attrptr))
	  cupsArrayAdd(data->errors, (void *)cupsGetErrorString());

	if (ippGetName(attrptr))
	{
	  if (cupsArrayFind(a, (void *)ippGetName(attrptr)) && data->output < IPPTOOL_OUTPUT_LIST)
	    add_stringf(data->errors, "Duplicate \"%s\" attribute in %s group", ippGetName(attrptr), ippTagString(group));

	  cupsArrayAdd(a, (void *)ippGetName(attrptr));
	}
      }

      cupsArrayDelete(a);

      // Now check the test-defined expected status-code and attribute values...
      if (ippGetStatusCode(response) == IPP_STATUS_ERROR_BUSY && data->repeat_on_busy)
      {
        // Repeat on a server-error-busy status code...
        status_ok   = true;
        repeat_test = true;
      }
      else
      {
	for (i = 0, status_ok = false; i < data->num_statuses; i ++)
	{
	  if (data->statuses[i].if_defined && !ippFileGetVar(f, data->statuses[i].if_defined))
	    continue;

	  if (data->statuses[i].if_not_defined && ippFileGetVar(f, data->statuses[i].if_not_defined))
	    continue;

	  if (ippGetStatusCode(response) == data->statuses[i].status)
	  {
	    status_ok = true;

	    if (data->statuses[i].repeat_match && repeat_count < data->statuses[i].repeat_limit)
	      repeat_test = true;

	    if (data->statuses[i].define_match)
	      ippFileSetVar(data->parent, data->statuses[i].define_match, "1");
	  }
	  else
	  {
	    if (data->statuses[i].repeat_no_match && repeat_count < data->statuses[i].repeat_limit)
	      repeat_test = true;

	    if (data->statuses[i].define_no_match)
	    {
	      ippFileSetVar(data->parent, data->statuses[i].define_no_match, "1");
	      status_ok = true;
	    }
	  }
	}
      }

      if (!status_ok && data->num_statuses > 0)
      {
	for (i = 0; i < data->num_statuses; i ++)
	{
	  if (data->statuses[i].if_defined && !ippFileGetVar(f, data->statuses[i].if_defined))
	    continue;

	  if (data->statuses[i].if_not_defined && ippFileGetVar(f, data->statuses[i].if_not_defined))
	    continue;

	  if (!data->statuses[i].repeat_match || repeat_count >= data->statuses[i].repeat_limit)
	    add_stringf(data->errors, "EXPECTED: STATUS %s (got %s)", ippErrorString(data->statuses[i].status), ippErrorString(cupsGetError()));
	}

	if ((attrptr = ippFindAttribute(response, "status-message", IPP_TAG_TEXT)) != NULL)
	  add_stringf(data->errors, "status-message=\"%s\"", ippGetString(attrptr, 0, NULL));
      }

      for (i = data->num_expects, expect = data->expects; i > 0; i --, expect ++)
      {
        cups_array_t	*exp_errors;	// Temporary list of errors
        bool		exp_member,	// Expect for member attribute?
			exp_pass;	// Did this expect pass?
	ipp_attribute_t	*group_found;	// Found parent attribute for group tests

	if (expect->if_defined && !ippFileGetVar(f, expect->if_defined))
	  continue;

	if (expect->if_not_defined && ippFileGetVar(f, expect->if_not_defined))
	  continue;

	if ((found = ippFindAttribute(response, expect->name, IPP_TAG_ZERO)) != NULL && expect->in_group && expect->in_group != ippGetGroupTag(found))
	{
	  while ((found = ippFindNextAttribute(response, expect->name, IPP_TAG_ZERO)) != NULL)
	    if (expect->in_group == ippGetGroupTag(found))
	      break;
	}

	exp_errors = cupsArrayNew3(NULL, NULL, NULL, 0, (cups_acopy_cb_t)strdup, (cups_afree_cb_t)free);
	exp_member = strchr(expect->name, '/') != NULL;
	exp_pass   = false;

	do
	{
	  group_found = found;

	  ippSave(response);

          if (expect->in_group && strchr(expect->name, '/'))
          {
            char	group_name[256],// Parent attribute name
			*group_ptr;	// Pointer into parent attribute name

	    cupsCopyString(group_name, expect->name, sizeof(group_name));
	    if ((group_ptr = strchr(group_name, '/')) != NULL)
	      *group_ptr = '\0';

	    group_found = ippFindAttribute(response, group_name, IPP_TAG_ZERO);
	  }

	  if ((found && expect->not_expect) ||
	      (!found && !(expect->not_expect || expect->optional)) ||
	      (found && !expect_matches(expect, found)) ||
	      (group_found && expect->in_group && ippGetGroupTag(group_found) != expect->in_group) ||
	      (expect->with_distinct && !with_distinct_values(NULL, found)))
	  {
	    if (expect->define_no_match)
	    {
	      ippFileSetVar(data->parent, expect->define_no_match, "1");
	      exp_pass = true;
	    }
	    else if (!expect->define_match && !expect->define_value)
	    {
	      if (found && expect->not_expect && !expect->with_value && !expect->with_value_from)
	      {
		add_stringf(exp_errors, "NOT EXPECTED: %s", expect->name);
	      }
	      else if (!found && !(expect->not_expect || expect->optional))
	      {
		add_stringf(exp_errors, "EXPECTED: %s", expect->name);
	      }
	      else if (found)
	      {
		if (!expect_matches(expect, found))
		  add_stringf(exp_errors, "EXPECTED: %s OF-TYPE %s (got %s)",
			      expect->name, expect->of_type,
			      ippTagString(ippGetValueTag(found)));

		if (expect->in_group && ippGetGroupTag(group_found) != expect->in_group)
		  add_stringf(exp_errors, "EXPECTED: %s IN-GROUP %s (got %s).",
			      expect->name, ippTagString(expect->in_group),
			      ippTagString(ippGetGroupTag(group_found)));

                if (expect->with_distinct)
                  with_distinct_values(exp_errors, found);
	      }
	    }

	    if (expect->repeat_no_match && repeat_count < expect->repeat_limit)
	      repeat_test = true;

            ippRestore(response);
	    break;
	  }

	  if (found)
	    ippAttributeString(found, data->buffer, sizeof(data->buffer));

          if (found && (expect->with_content || expect->with_mime_types || expect->save_filespec) && !with_content(exp_errors, found, expect->with_content, expect->with_mime_types, expect->save_filespec))
          {
	    if (expect->define_no_match)
	    {
	      ippFileSetVar(data->parent, expect->define_no_match, "1");
	      exp_pass = true;
	    }
          }

	  if (found && expect->with_value_from && !with_value_from(NULL, ippFindAttribute(response, expect->with_value_from, IPP_TAG_ZERO), found, data->buffer, sizeof(data->buffer)))
	  {
	    if (expect->define_no_match)
	    {
	      ippFileSetVar(data->parent, expect->define_no_match, "1");
	      exp_pass = true;
	    }
	    else if (!expect->define_match && !expect->define_value && ((!expect->repeat_match && !expect->repeat_no_match) || repeat_count >= expect->repeat_limit))
	    {
	      add_stringf(exp_errors, "EXPECTED: %s WITH-VALUES-FROM %s", expect->name, expect->with_value_from);

	      with_value_from(exp_errors, ippFindAttribute(response, expect->with_value_from, IPP_TAG_ZERO), found, data->buffer, sizeof(data->buffer));
	    }

	    if (expect->repeat_no_match && repeat_count < expect->repeat_limit)
	      repeat_test = true;

            ippRestore(response);
	    break;
	  }
	  else if (found && !with_value(data, NULL, expect->with_value, expect->with_flags, found, data->buffer, sizeof(data->buffer)))
	  {
	    if (expect->define_no_match)
	    {
	      ippFileSetVar(data->parent, expect->define_no_match, "1");
	      exp_pass = true;
	    }
	    else if (!expect->define_match && !expect->define_value &&
		     !expect->repeat_match && (!expect->repeat_no_match || repeat_count >= expect->repeat_limit))
	    {
	      if (expect->with_flags & IPPTOOL_WITH_REGEX)
		add_stringf(exp_errors, "EXPECTED: %s %s /%s/", expect->name, with_flags_string(expect->with_flags), expect->with_value);
	      else
		add_stringf(exp_errors, "EXPECTED: %s %s \"%s\"", expect->name, with_flags_string(expect->with_flags), expect->with_value);

	      with_value(data, exp_errors, expect->with_value, expect->with_flags, found, data->buffer, sizeof(data->buffer));
	    }

	    if (expect->repeat_no_match && repeat_count < expect->repeat_limit)
	      repeat_test = true;

            ippRestore(response);
	    break;
	  }
	  else if (expect->with_value)
	  {
	    exp_pass = true;
	  }

	  if (found && expect->count > 0 && ippGetCount(found) != expect->count)
	  {
	    if (expect->define_no_match)
	    {
	      ippFileSetVar(data->parent, expect->define_no_match, "1");
	      exp_pass = true;
	    }
	    else if (!expect->define_match && !expect->define_value)
	    {
	      add_stringf(exp_errors, "EXPECTED: %s COUNT %u (got %u)", expect->name, (unsigned)expect->count, (unsigned)ippGetCount(found));
	    }

	    if (expect->repeat_no_match && repeat_count < expect->repeat_limit)
	      repeat_test = true;

            ippRestore(response);
	    break;
	  }

	  if (found && expect->same_count_as)
	  {
	    attrptr = ippFindAttribute(response, expect->same_count_as,
				       IPP_TAG_ZERO);

	    if (!attrptr || ippGetCount(attrptr) != ippGetCount(found))
	    {
	      if (expect->define_no_match)
	      {
		ippFileSetVar(data->parent, expect->define_no_match, "1");
	        exp_pass = true;
	      }
	      else if (!expect->define_match && !expect->define_value)
	      {
		if (!attrptr)
		  add_stringf(exp_errors, "EXPECTED: %s (%u values) SAME-COUNT-AS %s (not returned)", expect->name, (unsigned)ippGetCount(found), expect->same_count_as);
		else if (ippGetCount(attrptr) != ippGetCount(found))
		  add_stringf(exp_errors, "EXPECTED: %s (%u values) SAME-COUNT-AS %s (%u values)", expect->name, (unsigned)ippGetCount(found), expect->same_count_as, (unsigned)ippGetCount(attrptr));
	      }

	      if (expect->repeat_no_match && repeat_count < expect->repeat_limit)
		repeat_test = true;

	      ippRestore(response);
	      break;
	    }
	  }

	  if (found && expect->display_match && (data->output == IPPTOOL_OUTPUT_TEST || (data->output == IPPTOOL_OUTPUT_PLIST && data->outfile != cupsFileStdout())))
	    cupsFilePrintf(cupsFileStdout(), "\n%s\n\n", expect->display_match);

	  if (found && expect->define_match)
	  {
	    ippFileSetVar(data->parent, expect->define_match, "1");
	    exp_pass = true;
	  }

	  if (found && expect->define_value)
	  {
	    exp_pass = true;
	    if (!expect->with_value)
	    {
	      int last = ippGetCount(found) - 1;
					// Last element in attribute

	      switch (ippGetValueTag(found))
	      {
		case IPP_TAG_ENUM :
		case IPP_TAG_INTEGER :
		    snprintf(data->buffer, sizeof(data->buffer), "%d", ippGetInteger(found, last));
		    break;

		case IPP_TAG_BOOLEAN :
		    if (ippGetBoolean(found, last))
		      cupsCopyString(data->buffer, "true", sizeof(data->buffer));
		    else
		      cupsCopyString(data->buffer, "false", sizeof(data->buffer));
		    break;

		case IPP_TAG_RESOLUTION :
		    {
		      int	xres,	// Horizontal resolution
				yres;	// Vertical resolution
		      ipp_res_t	units;	// Resolution units

		      xres = ippGetResolution(found, last, &yres, &units);

		      if (xres == yres)
			snprintf(data->buffer, sizeof(data->buffer), "%d%s", xres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
		      else
			snprintf(data->buffer, sizeof(data->buffer), "%dx%d%s", xres, yres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
		    }
		    break;

		case IPP_TAG_CHARSET :
		case IPP_TAG_KEYWORD :
		case IPP_TAG_LANGUAGE :
		case IPP_TAG_MIMETYPE :
		case IPP_TAG_NAME :
		case IPP_TAG_NAMELANG :
		case IPP_TAG_TEXT :
		case IPP_TAG_TEXTLANG :
		case IPP_TAG_URI :
		case IPP_TAG_URISCHEME :
		    cupsCopyString(data->buffer, ippGetString(found, last, NULL), sizeof(data->buffer));
		    break;

		default :
		    ippAttributeString(found, data->buffer, sizeof(data->buffer));
		    break;
	      }
	    }

	    ippFileSetVar(data->parent, expect->define_value, data->buffer);
	  }

	  if (found && expect->repeat_match && repeat_count < expect->repeat_limit)
	    repeat_test = 1;

	  ippRestore(response);
	}
	while ((expect->expect_all || !exp_member) && (found = ippFindNextAttribute(response, expect->name, IPP_TAG_ZERO)) != NULL);

        // Handle results of the EXPECT checks...
	if (!exp_pass)
	{
	  // Copy errors...
	  char *e;			// Current error

	  for (e = (char *)cupsArrayGetFirst(exp_errors); e; e = (char *)cupsArrayGetNext(exp_errors))
	    cupsArrayAdd(data->errors, e);
	}

	cupsArrayDelete(exp_errors);
      }
    }

    // If we are going to repeat this test, display intermediate results...
    if (repeat_test)
    {
      if (data->output == IPPTOOL_OUTPUT_TEST || (data->output == IPPTOOL_OUTPUT_PLIST && data->outfile != cupsFileStdout()))
      {
	cupsFilePrintf(cupsFileStdout(), "%04d]\n", repeat_count);
\
	if (data->num_displayed > 0)
	{
	  for (attrptr = ippGetFirstAttribute(response); attrptr; attrptr = ippGetNextAttribute(response))
	  {
	    const char *attrname = ippGetName(attrptr);
	    if (attrname)
	    {
	      for (i = 0; i < data->num_displayed; i ++)
	      {
		if (!strcmp(data->displayed[i], attrname))
		{
		  print_attr(cupsFileStdout(), IPPTOOL_OUTPUT_TEST, attrptr, NULL);
		  break;
		}
	      }
	    }
	  }
	}
      }

      if (data->output == IPPTOOL_OUTPUT_TEST || (data->output == IPPTOOL_OUTPUT_PLIST && data->outfile != cupsFileStdout()))
      {
	cupsFilePrintf(cupsFileStdout(), "    %-68.68s [", data->name);
      }

      ippDelete(response);
      response = NULL;
    }
  }
  while (repeat_test);

  ippDelete(request);

  request = NULL;

  if (cupsArrayGetCount(data->errors) > 0)
    data->prev_pass = data->pass = false;

  if (data->prev_pass)
    data->pass_count ++;
  else
    data->fail_count ++;

  if (data->output == IPPTOOL_OUTPUT_PLIST)
  {
    cupsFilePuts(data->outfile, "<key>Successful</key>\n");
    cupsFilePuts(data->outfile, data->prev_pass ? "<true />\n" : "<false />\n");
    cupsFilePuts(data->outfile, "<key>StatusCode</key>\n");
    print_xml_string(data->outfile, "string", ippErrorString(cupsGetError()));
    cupsFilePuts(data->outfile, "<key>ResponseAttributes</key>\n");
    cupsFilePuts(data->outfile, "<array>\n");
    cupsFilePuts(data->outfile, "<dict>\n");
    for (attrptr = ippGetFirstAttribute(response), group = ippGetGroupTag(attrptr);
	 attrptr;
	 attrptr = ippGetNextAttribute(response))
      print_attr(data->outfile, data->output, attrptr, &group);
    cupsFilePuts(data->outfile, "</dict>\n");
    cupsFilePuts(data->outfile, "</array>\n");
  }
  else if (data->output == IPPTOOL_OUTPUT_IPPSERVER && response)
  {
    for (attrptr = ippGetFirstAttribute(response); attrptr; attrptr = ippGetNextAttribute(response))
    {
      if (!ippGetName(attrptr) || ippGetGroupTag(attrptr) != IPP_TAG_PRINTER)
	continue;

      print_ippserver_attr(data, attrptr, 0);
    }
  }
  else if (data->output == IPPTOOL_OUTPUT_JSON && response)
  {
    ipp_tag_t	cur_tag = IPP_TAG_ZERO,	// Current group tag
		group_tag;		// Attribute's group tag

    cupsFilePuts(data->outfile, "[\n");
    attrptr = ippGetFirstAttribute(response);
    while (attrptr)
    {
      group_tag = ippGetGroupTag(attrptr);

      if (group_tag && ippGetName(attrptr))
      {
	if (group_tag != cur_tag)
	{
	  if (cur_tag)
	    cupsFilePuts(data->outfile, "    },\n");

	  cupsFilePrintf(data->outfile, "    {\n        \"group-tag\": \"%s\",\n", ippTagString(group_tag));
	  cur_tag = group_tag;
	}

	print_json_attr(data, attrptr, 8);
	attrptr = ippGetNextAttribute(response);
	cupsFilePuts(data->outfile, ippGetName(attrptr) && ippGetGroupTag(attrptr) == cur_tag ? ",\n" : "\n");
      }
      else
      {
	attrptr = ippGetNextAttribute(response);
      }
    }

    if (cur_tag)
      cupsFilePuts(data->outfile, "    }\n");
    cupsFilePuts(data->outfile, "]\n");
  }

  if (data->output == IPPTOOL_OUTPUT_TEST || (data->output == IPPTOOL_OUTPUT_PLIST && data->outfile != cupsFileStdout()))
  {
    cupsFilePuts(cupsFileStdout(), data->prev_pass ? "PASS]\n" : "FAIL]\n");

    if (!data->prev_pass || (data->verbosity && response))
    {
      cupsFilePrintf(cupsFileStdout(), "        RECEIVED: %lu bytes in response\n", (unsigned long)ippGetLength(response));
      cupsFilePrintf(cupsFileStdout(), "        status-code = %s (%s)\n", ippErrorString(cupsGetError()), cupsGetErrorString());

      if (data->verbosity && response)
      {
	for (attrptr = ippGetFirstAttribute(response); attrptr; attrptr = ippGetNextAttribute(response))
	  print_attr(cupsFileStdout(), IPPTOOL_OUTPUT_TEST, attrptr, NULL);
      }
    }
  }
  else if (!data->prev_pass && data->output != IPPTOOL_OUTPUT_QUIET)
    fprintf(stderr, "%s\n", cupsGetErrorString());

  if (data->prev_pass && data->output >= IPPTOOL_OUTPUT_LIST && !data->verbosity && data->num_displayed > 0)
  {
    int	width;			// Length of value

    for (i = 0; i < data->num_displayed; i ++)
    {
      widths[i] = (int)strlen(data->displayed[i]);

      for (attrptr = ippFindAttribute(response, data->displayed[i], IPP_TAG_ZERO);
	   attrptr;
	   attrptr = ippFindNextAttribute(response, data->displayed[i], IPP_TAG_ZERO))
      {
	width = (int)ippAttributeString(attrptr, NULL, 0);
	if (width > widths[i])
	  widths[i] = width;
      }
    }

    if (data->output == IPPTOOL_OUTPUT_CSV)
      print_csv(data, NULL, NULL, data->num_displayed, data->displayed, widths);
    else
      print_line(data, NULL, NULL, data->num_displayed, data->displayed, widths);

    attrptr = ippGetFirstAttribute(response);

    while (attrptr)
    {
      while (attrptr && ippGetGroupTag(attrptr) <= IPP_TAG_OPERATION)
	attrptr = ippGetNextAttribute(response);

      if (attrptr)
      {
	if (data->output == IPPTOOL_OUTPUT_CSV)
	  attrptr = print_csv(data, response, attrptr, data->num_displayed, data->displayed, widths);
	else
	  attrptr = print_line(data, response, attrptr, data->num_displayed, data->displayed, widths);

	while (attrptr && ippGetGroupTag(attrptr) > IPP_TAG_OPERATION)
	  attrptr = ippGetNextAttribute(response);
      }
    }
  }
  else if (!data->prev_pass)
  {
    if (data->output == IPPTOOL_OUTPUT_PLIST)
    {
      cupsFilePuts(data->outfile, "<key>Errors</key>\n");
      cupsFilePuts(data->outfile, "<array>\n");

      for (error = (char *)cupsArrayGetFirst(data->errors);
	   error;
	   error = (char *)cupsArrayGetNext(data->errors))
	print_xml_string(data->outfile, "string", error);

      cupsFilePuts(data->outfile, "</array>\n");
    }

    if (data->output == IPPTOOL_OUTPUT_TEST || (data->output == IPPTOOL_OUTPUT_PLIST && data->outfile != cupsFileStdout()))
    {
      for (error = (char *)cupsArrayGetFirst(data->errors);
	   error;
	   error = (char *)cupsArrayGetNext(data->errors))
	cupsFilePrintf(cupsFileStdout(), "        %s\n", error);
    }
  }

  if (data->num_displayed > 0 && !data->verbosity && response && (data->output == IPPTOOL_OUTPUT_TEST || (data->output == IPPTOOL_OUTPUT_PLIST && data->outfile != cupsFileStdout())))
  {
    for (attrptr = ippGetFirstAttribute(response); attrptr; attrptr = ippGetNextAttribute(response))
    {
      if (ippGetName(attrptr))
      {
	for (i = 0; i < data->num_displayed; i ++)
	{
	  if (!strcmp(data->displayed[i], ippGetName(attrptr)))
	  {
	    print_attr(data->outfile, data->output, attrptr, NULL);
	    break;
	  }
	}
      }
    }
  }

  skip_error:

  if (data->monitor_thread)
  {
    data->monitor_done = 1;
    cupsThreadWait(data->monitor_thread);
  }

  if (data->output == IPPTOOL_OUTPUT_PLIST)
    cupsFilePuts(data->outfile, "</dict>\n");

  ippDelete(response);
  response = NULL;

  clear_data(data);

  return (data->ignore_errors || data->prev_pass);
}


//
// 'do_tests()' - Do tests as specified in the test file.
//

static bool				// O - `true` on success, `false` on failure
do_tests(const char     *testfile,	// I - Test file to use
         ipptool_test_t *data)		// I - Test data
{
  ipp_file_t	*file;			// IPP data file


  // Connect to the printer/server...
  data->http = connect_printer(data);

  // Run tests...
  if ((file = ippFileNew(data->parent, NULL, (ipp_ferror_cb_t)error_cb, data)) == NULL)
  {
    print_fatal_error(data, "Unable to create test file parser: %s", cupsGetErrorString());
    data->pass = false;
  }
  else if (ippFileOpen(file, testfile, "r"))
  {
    // Successfully opened file, read from it...
    ippFileRead(file, (ipp_ftoken_cb_t)token_cb, true);
  }
  else
  {
    // Report the error...
    print_fatal_error(data, "Unable to open '%s': %s", testfile, cupsGetErrorString());
    data->pass = false;
  }

  ippFileDelete(file);

  // Close connection and return...
  httpClose(data->http);
  data->http = NULL;

  return (data->pass);
}


//
// 'error_cb()' - Print/add an error message.
//

static bool				// O - `true` to continue, `false` to stop
error_cb(ipp_file_t     *f,		// I - IPP file data (not used)
         ipptool_test_t *data,		// I - Test data
         const char     *error)		// I - Error message
{
  (void)f;

  print_fatal_error(data, "%s", error);

  return (true);
}


//
// 'expect_matches()' - Return true if the tag matches the specification.
//

static bool				// O - `true` on match, `false` on non-match
expect_matches(
    ipptool_expect_t *expect,		// I - Expected attribute
    ipp_attribute_t  *attr)		// I - Attribute
{
  int		i,			// Looping var
		count;			// Number of values
  bool		match;			// Match?
  char		*of_type,		// Type name to match
		*paren,			// Pointer to opening parenthesis
		*next,			// Next name to match
		sep;			// Separator character
  ipp_tag_t	value_tag;		// Syntax/value tag
  int		lower, upper;		// Lower and upper bounds for syntax


  // If we don't expect a particular type, return immediately...
  if (!expect->of_type)
    return (true);

  // Parse the "of_type" value since the string can contain multiple attribute
  // types separated by "," or "|"...
  value_tag = ippGetValueTag(attr);
  count     = ippGetCount(attr);

  for (of_type = expect->of_type, match = false; !match && *of_type; of_type = next)
  {
    // Find the next separator, and set it (temporarily) to nul if present.
    for (next = of_type; *next && *next != '|' && *next != ','; next ++);

    if ((sep = *next) != '\0')
      *next = '\0';

    // Support some meta-types to make it easier to write the test file.
    if ((paren = strchr(of_type, '(')) != NULL)
    {
      char *ptr;			// Pointer into syntax string

      *paren = '\0';

      if (!strncmp(paren + 1, "MIN:", 4))
      {
        lower = INT_MIN;
        ptr   = paren + 5;
      }
      else if ((ptr = strchr(paren + 1, ':')) != NULL)
      {
        lower = atoi(paren + 1);
        ptr ++;
      }
      else
      {
        lower = 0;
        ptr   = paren + 1;
      }

      if (!strcmp(ptr, "MAX)"))
        upper = INT_MAX;
      else
        upper = atoi(ptr);
    }
    else
    {
      lower = INT_MIN;
      upper = INT_MAX;
    }

    if (!strcmp(of_type, "text"))
    {
      if (upper == INT_MAX)
        upper = 1023;

      if (value_tag == IPP_TAG_TEXTLANG || value_tag == IPP_TAG_TEXT)
      {
        for (i = 0; i < count; i ++)
	{
	  if (strlen(ippGetString(attr, i, NULL)) > (size_t)upper)
	    break;
	}

	match = (i == count);
      }
    }
    else if (!strcmp(of_type, "name"))
    {
      if (upper == INT_MAX)
        upper = 255;

      if (value_tag == IPP_TAG_NAMELANG || value_tag == IPP_TAG_NAME)
      {
        for (i = 0; i < count; i ++)
	{
	  if (strlen(ippGetString(attr, i, NULL)) > (size_t)upper)
	    break;
	}

	match = (i == count);
      }
    }
    else if (!strcmp(of_type, "collection"))
    {
      match = value_tag == IPP_TAG_BEGIN_COLLECTION;
    }
    else if (value_tag == ippTagValue(of_type))
    {
      switch (value_tag)
      {
        case IPP_TAG_KEYWORD :
        case IPP_TAG_URI :
            if (upper == INT_MAX)
            {
              if (value_tag == IPP_TAG_KEYWORD)
		upper = 255;
	      else
	        upper = 1023;
	    }

	    for (i = 0; i < count; i ++)
	    {
	      if (strlen(ippGetString(attr, i, NULL)) > (size_t)upper)
		break;
	    }

	    match = (i == count);
	    break;

        case IPP_TAG_STRING :
            if (upper == INT_MAX)
	      upper = 1023;

	    for (i = 0; i < count; i ++)
	    {
	      int	datalen;	// Length of octetString value

	      ippGetOctetString(attr, i, &datalen);

	      if (datalen > upper)
		break;
	    }

	    match = (i == count);
	    break;

	case IPP_TAG_INTEGER :
	    for (i = 0; i < count; i ++)
	    {
	      int value = ippGetInteger(attr, i);
					// Integer value

	      if (value < lower || value > upper)
		break;
	    }

	    match = (i == count);
	    break;

	case IPP_TAG_RANGE :
	    for (i = 0; i < count; i ++)
	    {
	      int vupper, vlower = ippGetRange(attr, i, &vupper);
					// Range value

	      if (vlower < lower || vlower > upper || vupper < lower || vupper > upper)
		break;
	    }

	    match = (i == count);
	    break;

	default :
	    // No other constraints, so this is a match
	    match = true;
	    break;
      }
    }

    // Restore the separators if we have them...
    if (paren)
      *paren = '(';

    if (sep)
      *next++ = sep;
  }

  return (match);
}


//
// 'free_data()' - Free test data.
//

static void
free_data(ipptool_test_t *data)		// I - Test data
{
  clear_data(data);

  ippFileDelete(data->parent);
  cupsArrayDelete(data->errors);

  free(data);
}


//
// 'generate_file()' - Generate a print file.
//

static http_status_t			// O - HTTP status
generate_file(
    http_t             *http,		// I - HTTP connection
    ipptool_generate_t *params)		// I - GENERATE-FILE parameters
{
  cups_raster_mode_t	mode;		// Raster output mode
  cups_raster_t		*ras;		// Raster stream
  cups_page_header2_t	header;		// Raster page header (front side)
  cups_page_header2_t	back_header;	// Raster page header (back side)
  pwg_media_t		*pwg;		// PWG media information
  cups_media_t		media;		// CUPS media information


  // Set the output mode...
  if (!strcmp(params->format, "image/pwg-raster"))
    mode = CUPS_RASTER_WRITE_PWG;
  else if (!strcmp(params->format, "image/urf"))
    mode = CUPS_RASTER_WRITE_APPLE;
  else
    mode = CUPS_RASTER_WRITE_COMPRESSED;

  // Create the raster header...
  if ((pwg = pwgMediaForPWG(params->media)) == NULL)
  {
    fprintf(stderr, "ipptool: Unable to parse media size '%s'.\n", params->media);
    return (HTTP_STATUS_SERVER_ERROR);
  }

  memset(&media, 0, sizeof(media));
  cupsCopyString(media.media, pwg->pwg, sizeof(media.media));
  media.width  = pwg->width;
  media.length = pwg->length;

  cupsRasterInitHeader(&header, &media, /*optimize*/NULL, IPP_QUALITY_NORMAL, /*intent*/NULL, params->orientation, params->sides, params->type, params->xdpi, params->ydpi, /*sheet_back*/NULL);
  cupsRasterInitHeader(&back_header, &media, /*optimize*/NULL, IPP_QUALITY_NORMAL, /*intent*/NULL, params->orientation, params->sides, params->type, params->xdpi, params->ydpi, params->sheet_back);

#if 0
  fprintf(stderr, "ipptool: media='%s'\n", params->media);
  fprintf(stderr, "ipptool: type='%s'\n", params->type);
  fprintf(stderr, "ipptool: resolution=%dx%d\n", params->xdpi, params->ydpi);
  fprintf(stderr, "ipptool: orientation=%d\n", params->orientation);
  fprintf(stderr, "ipptool: sides='%s'\n", params->sides);
  fprintf(stderr, "ipptool: num_copies=%d\n", params->num_copies);
  fprintf(stderr, "ipptool: num_pages=%d\n", params->num_pages);
  fprintf(stderr, "ipptool: format='%s'\n", params->format);
  fprintf(stderr, "ipptool: sheet_back='%s'\n", params->sheet_back);
#endif // 0

  // Create the raster stream...
  if ((ras = cupsRasterOpenIO((cups_raster_cb_t)httpWrite, http, mode)) == NULL)
    return (HTTP_STATUS_SERVER_ERROR);

  // Write it...
  if (!cupsRasterWriteTest(ras, &header, &back_header, params->sheet_back, params->orientation, params->num_copies, params->num_pages))
    return (HTTP_STATUS_SERVER_ERROR);

  cupsRasterClose(ras);

  return (HTTP_STATUS_CONTINUE);
}


//
// 'get_filename()' - Get a filename based on the current test file.
//

static char *				// O - Filename
get_filename(const char *testfile,	// I - Current test file
             char       *dst,		// I - Destination filename
	     const char *src,		// I - Source filename
             size_t     dstsize)	// I - Size of destination buffer
{
  char			*dstptr;	// Pointer into destination
  _cups_globals_t	*cg = _cupsGlobals();
					// Global data


  if (*src == '<' && src[strlen(src) - 1] == '>')
  {
    // Map <filename> to CUPS_DATADIR/ipptool/filename...
    snprintf(dst, dstsize, "%s/ipptool/%s", cg->cups_datadir, src + 1);
    dstptr = dst + strlen(dst) - 1;
    if (*dstptr == '>')
      *dstptr = '\0';
  }
  else if (!access(src, R_OK) || *src == '/'
#ifdef _WIN32
           || (isalpha(*src & 255) && src[1] == ':')
#endif // _WIN32
           )
  {
    // Use the path as-is...
    cupsCopyString(dst, src, dstsize);
  }
  else
  {
    // Make path relative to testfile...
    cupsCopyString(dst, testfile, dstsize);
    if ((dstptr = strrchr(dst, '/')) != NULL)
      dstptr ++;
    else
      dstptr = dst; // Should never happen

    cupsCopyString(dstptr, src, dstsize - (size_t)(dstptr - dst));

#if _WIN32
    if (_access(dst, 0))
    {
      // Not available relative to the testfile, see if it can be found on the  desktop...
      const char *userprofile = getenv("USERPROFILE");
					// User home directory

      if (userprofile)
        snprintf(dst, dstsize, "%s/Desktop/%s", userprofile, src);
    }
#endif // _WIN32
  }

  return (dst);
}


//
// 'get_string()' - Get a pointer to a string value or the portion of interest.
//

static const char *			// O - Pointer to string
get_string(ipp_attribute_t *attr,	// I - IPP attribute
           int             element,	// I - Element to fetch
           int             flags,	// I - Value ("with") flags
           char            *buffer,	// I - Temporary buffer
	   size_t          bufsize)	// I - Size of temporary buffer
{
  const char	*value;			// Value
  char		*ptr,			// Pointer into value
		scheme[256],		// URI scheme
		userpass[256],		// Username/password
		hostname[256],		// Hostname
		resource[1024];		// Resource
  int		port;			// Port number


  value = ippGetString(attr, element, NULL);

  if (flags & IPPTOOL_WITH_HOSTNAME)
  {
    if (httpSeparateURI(HTTP_URI_CODING_ALL, value, scheme, sizeof(scheme), userpass, sizeof(userpass), buffer, bufsize, &port, resource, sizeof(resource)) < HTTP_URI_STATUS_OK)
      buffer[0] = '\0';

    ptr = buffer + strlen(buffer) - 1;
    if (ptr >= buffer && *ptr == '.')
      *ptr = '\0';			// Drop trailing "."

    return (buffer);
  }
  else if (flags & IPPTOOL_WITH_RESOURCE)
  {
    if (httpSeparateURI(HTTP_URI_CODING_ALL, value, scheme, sizeof(scheme), userpass, sizeof(userpass), hostname, sizeof(hostname), &port, buffer, bufsize) < HTTP_URI_STATUS_OK)
      buffer[0] = '\0';

    return (buffer);
  }
  else if (flags & IPPTOOL_WITH_SCHEME)
  {
    if (httpSeparateURI(HTTP_URI_CODING_ALL, value, buffer, bufsize, userpass, sizeof(userpass), hostname, sizeof(hostname), &port, resource, sizeof(resource)) < HTTP_URI_STATUS_OK)
      buffer[0] = '\0';

    return (buffer);
  }
  else if (ippGetValueTag(attr) == IPP_TAG_URI && (!strncmp(value, "ipp://", 6) || !strncmp(value, "http://", 7) || !strncmp(value, "ipps://", 7) || !strncmp(value, "https://", 8)))
  {
    http_uri_status_t status = httpSeparateURI(HTTP_URI_CODING_ALL, value, scheme, sizeof(scheme), userpass, sizeof(userpass), hostname, sizeof(hostname), &port, resource, sizeof(resource));

    if (status < HTTP_URI_STATUS_OK)
    {
      // Bad URI...
      buffer[0] = '\0';
    }
    else
    {
      // Normalize URI with no trailing dot...
      if ((ptr = hostname + strlen(hostname) - 1) >= hostname && *ptr == '.')
	*ptr = '\0';

      httpAssembleURI(HTTP_URI_CODING_ALL, buffer, bufsize, scheme, userpass, hostname, port, resource);
    }

    return (buffer);
  }
  else
  {
    return (value);
  }
}


//
// 'iso_date()' - Return an ISO 8601 date/time string for the given IPP dateTime
//                value.
//

static char *				// O - ISO 8601 date/time string
iso_date(const ipp_uchar_t *date)	// I - IPP (RFC 1903) date/time value
{
  time_t	utctime;		// UTC time since 1970
  struct tm	utcdate;		// UTC date/time
  static char	buffer[255];		// String buffer


  utctime = ippDateToTime(date);
  gmtime_r(&utctime, &utcdate);

  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02dZ",
	   utcdate.tm_year + 1900, utcdate.tm_mon + 1, utcdate.tm_mday,
	   utcdate.tm_hour, utcdate.tm_min, utcdate.tm_sec);

  return (buffer);
}


//
// 'parse_generate_file()' - Parse the GENERATE-FILE directive.
//
// GENERATE-FILE {
//     MEDIA "media size name, default, ready"
//     COLORSPACE "colorspace_bits, auto, color, monochrome, bi-level"
//     RESOLUTION "resolution, min, max, default"
//     ORIENTATION "portrait, landscape, reverse-landscape, reverse-portrait"
//     SIDES "one-sided, two-sided-long-edge, two-sided-short-edge"
//     NUM-COPIES "copies"
//     NUM-PAGES "pages, min"
//     FORMAT "image/pwg-raster, image/urf"
// }
//

static bool				// O - `true` to continue, `false` to stop
parse_generate_file(
    ipp_file_t     *f,			// I - IPP file data
    ipptool_test_t *data)		// I - Test data
{
  int			i;		// Looping var
  ipptool_generate_t	*params = NULL;	// Generation parameters
  http_t		*http;		// Connection to printer
  ipp_t			*request,	// Get-Printer-Attributes request
			*response = NULL;// Get-Printer-Attributes response
  ipp_attribute_t	*attr;		// Current attribute
  const char		*keyword;	// Keyword value
  char			token[256],	// Token string
			temp[1024],	// Temporary string
			value[1024],	// Value string
			*ptr;		// Pointer into value
  static const char *autos[][2] =	// Automatic color/monochrome keywords
  {
    { "SRGB24",     "srgb_8" },
    { "ADOBERGB24", "adobe-rgb_8" },
    { "DEVRGB24",   "rgb_8" },
    { "DEVCMYK32",  "cmyk_8" },
    { "ADOBERGB48", "adobe-rgb_16" },
    { "DEVRGB48",   "rgb_16" },
    { "DEVCMYK64",  "cmyk_16" },
    { "W8",         "sgray_8" },
    { NULL,         "black_8" },
    { "W16",        "sgray_16" },
    { NULL,         "black_16" },
    { NULL,         "sgray_1" },
    { NULL,         "black_1" }
  };
  static const char *bi_levels[][2] =	// Bi-level keywords
  {
    { NULL,         "sgray_1" },
    { NULL,         "black_1" }
  };
  static const char *colors[][2] =	// Color keywords
  {
    { "SRGB24",     "srgb_8" },
    { "ADOBERGB24", "adobe-rgb_8" },
    { "DEVRGB24",   "rgb_8" },
    { "DEVCMYK32",  "cmyk_8" },
    { "ADOBERGB48", "adobe-rgb_16" },
    { "DEVRGB48",   "rgb_16" },
    { "DEVCMYK64",  "cmyk_16" }
  };
  static const char *monochromes[][2] =	// Monochrome keywords
  {
    { "W8",         "sgray_8" },
    { NULL,         "black_8" },
    { "W16",        "sgray_16" },
    { NULL,         "black_16" },
    { NULL,         "sgray_1" },
    { NULL,         "black_1" }
  };


  // Make sure we have an open brace after the GENERATE-FILE...
  if (!ippFileReadToken(f, token, sizeof(token)) || strcmp(token, "{"))
  {
    print_fatal_error(data, "Missing open brace on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
    return (0);
  }

  // Get printer attributes...
  if ((http = connect_printer(data)) == NULL)
  {
    print_fatal_error(data, "GENERATE-FILE connection failure on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
    return (false);
  }

  request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, ippFileGetVar(data->parent, "uri"));

  response = cupsDoRequest(http, request, ippFileGetVar(data->parent, "resource"));

  httpClose(http);

  if (cupsGetError() >= IPP_STATUS_ERROR_BAD_REQUEST)
  {
    print_fatal_error(data, "GENERATE-FILE query failure on line %d of '%s': %s", ippFileGetLineNumber(f), ippFileGetFilename(f), cupsGetErrorString());
    ippDelete(response);
    return (false);
  }

  // Allocate parameters...
  if ((params = calloc(1, sizeof(ipptool_generate_t))) == NULL)
  {
    print_fatal_error(data, "GENERATE-FILE memory allocation failure on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
    return (false);
  }

  // Loop until we get a closing brace...
  while (ippFileReadToken(f, token, sizeof(token)))
  {
    if (!strcmp(token, "}"))
    {
      // Update the raster type as needed...
      if (!params->type[0])
      {
        // Get request/printer default value for print-color-mode, default to "auto"...
        if ((attr = ippFileGetAttribute(f, "print-color-mode", IPP_TAG_KEYWORD)) == NULL)
	  attr = ippFindAttribute(response, "print-color-mode-default", IPP_TAG_KEYWORD);

        if (attr)
          cupsCopyString(params->type, ippGetString(attr, 0, NULL), sizeof(params->type));
	else
          cupsCopyString(params->type, "auto", sizeof(params->type));
      }

      if (!strcmp(params->type, "auto"))
      {
        // Find auto keyword...
        params->type[0] = '\0';

        if ((attr = ippFindAttribute(response, "pwg-raster-document-type-supported", IPP_TAG_KEYWORD)) == NULL)
          attr = ippFindAttribute(response, "urf-supported", IPP_TAG_KEYWORD);

	for (i = 0; i < (int)(sizeof(autos) / sizeof(autos[0])); i ++)
	{
	  if (ippContainsString(attr, autos[i][0]) || ippContainsString(attr, autos[i][1]))
	  {
	    cupsCopyString(params->type, autos[i][1], sizeof(params->type));
	    break;
	  }
	}

        if (!params->type[0])
        {
	  print_fatal_error(data, "Printer does not support COLORSPACE \"auto\" on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	  goto fail;
        }
      }
      else if (!strcmp(params->type, "bi-level"))
      {
        // Find bi-level keyword...
        params->type[0] = '\0';

        attr = ippFindAttribute(response, "pwg-raster-document-type-supported", IPP_TAG_KEYWORD);

	for (i = 0; i < (int)(sizeof(bi_levels) / sizeof(bi_levels[0])); i ++)
	{
	  if (ippContainsString(attr, bi_levels[i][1]))
	  {
	    cupsCopyString(params->type, bi_levels[i][1], sizeof(params->type));
	    break;
	  }
	}

	if (!params->type[0])
	{
	  print_fatal_error(data, "Printer does not support COLORSPACE \"bi-level\" on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	  goto fail;
	}
      }
      else if (!strcmp(params->type, "color"))
      {
        // Find color keyword...
        params->type[0] = '\0';

        if ((attr = ippFindAttribute(response, "pwg-raster-document-type-supported", IPP_TAG_KEYWORD)) != NULL)
          attr = ippFindAttribute(response, "urf-supported", IPP_TAG_KEYWORD);

	for (i = 0; i < (int)(sizeof(colors) / sizeof(colors[0])); i ++)
	{
	  if (ippContainsString(attr, colors[i][0]) || ippContainsString(attr, colors[i][1]))
	  {
	    cupsCopyString(params->type, colors[i][1], sizeof(params->type));
	    break;
	  }
	}

        if (!params->type[0])
        {
	  print_fatal_error(data, "Printer does not support COLORSPACE \"color\" on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	  goto fail;
        }
      }
      else if (!strcmp(params->type, "monochrome"))
      {
        // Find grayscale keyword...
        params->type[0] = '\0';

        if ((attr = ippFindAttribute(response, "pwg-raster-document-type-supported", IPP_TAG_KEYWORD)) == NULL)
          attr = ippFindAttribute(response, "urf-supported", IPP_TAG_KEYWORD);

	for (i = 0; i < (int)(sizeof(monochromes) / sizeof(monochromes[0])); i ++)
	{
	  if (ippContainsString(attr, monochromes[i][0]) || ippContainsString(attr, monochromes[i][1]))
	  {
	    cupsCopyString(params->type, monochromes[i][1], sizeof(params->type));
	    break;
	  }
	}

        if (!params->type[0])
        {
	  print_fatal_error(data, "Printer does not support COLORSPACE \"monochrome\" on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	  goto fail;
        }
      }

      // Make sure we have an output format...
      if (!params->format[0])
      {
        // Check the supported formats and choose a suitable one...
        if ((keyword = ippGetString(ippFileGetAttribute(f, "document-format", IPP_TAG_MIMETYPE), 0, NULL)) != NULL)
        {
          if (strcmp(keyword, "image/pwg-raster") && strcmp(keyword, "image/urf"))
          {
	    print_fatal_error(data, "Unsupported \"document-format\" value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	    goto fail;
          }

          cupsCopyString(params->format, keyword, sizeof(params->format));
        }
        else if ((attr = ippFindAttribute(response, "document-format-supported", IPP_TAG_MIMETYPE)) != NULL)
        {
          // Default to Apple Raster unless sending bitmaps, which are only
          // supported by PWG Raster...
          if (ippContainsString(attr, "image/urf") && strncmp(params->type, "black_", 6) && strcmp(params->type, "srgb_1"))
            cupsCopyString(params->format, "image/urf", sizeof(params->format));
	  else if (ippContainsString(attr, "image/pwg-raster"))
            cupsCopyString(params->format, "image/pwg-raster", sizeof(params->format));
        }

        if (!params->format[0])
        {
	  print_fatal_error(data, "Printer does not support a compatible FORMAT on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	  goto fail;
        }
      }

      // Get default/ready media...
      if (!params->media[0] || !strcmp(params->media, "default"))
      {
        // Use job ticket or default media...
        if (!params->media[0] && (keyword = ippGetString(ippFileGetAttribute(f, "media", IPP_TAG_ZERO), 0, NULL)) != NULL)
        {
          cupsCopyString(params->media, keyword, sizeof(params->media));
        }
        else if ((keyword = ippGetString(ippFindAttribute(response, "media-default", IPP_TAG_ZERO), 0, NULL)) != NULL)
        {
          cupsCopyString(params->media, keyword, sizeof(params->media));
        }
        else
        {
	  print_fatal_error(data, "Printer does not report a default MEDIA size name on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	  goto fail;
        }
      }
      else if (!strcmp(params->media, "ready"))
      {
        // Use ready media
        if ((keyword = ippGetString(ippFindAttribute(response, "media-ready", IPP_TAG_ZERO), 0, NULL)) != NULL)
        {
          cupsCopyString(params->media, keyword, sizeof(params->media));
        }
        else
        {
	  print_fatal_error(data, "Printer does not report a ready MEDIA size name on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	  goto fail;
        }
      }

      // Default resolution
      if (!params->xdpi || !params->ydpi)
      {
	if ((attr = ippFindAttribute(response, "pwg-raster-document-resolution-supported", IPP_TAG_RESOLUTION)) != NULL)
	{
	  ipp_res_t	units;			// Resolution units

          // Use the middle resolution in the list...
          params->xdpi = ippGetResolution(attr, ippGetCount(attr) / 2, &params->ydpi, &units);

          if (units == IPP_RES_PER_CM)
          {
            params->xdpi = (int)(params->xdpi * 2.54);
            params->ydpi = (int)(params->ydpi * 2.54);
	  }
        }
        else if ((attr = ippFindAttribute(response, "urf-supported", IPP_TAG_KEYWORD)) != NULL)
        {
          int count = ippGetCount(attr);	// Number of values

          for (i = 0; i < count; i ++)
          {
            keyword = ippGetString(attr, i, NULL);
            if (!strncmp(keyword, "RS", 2))
	    {
	      // Use the first resolution in the list...
	      params->xdpi = params->ydpi = atoi(keyword + 2);
	      break;
	    }
          }
	}

	if (!params->xdpi || !params->ydpi)
	{
	  print_fatal_error(data, "Printer does not report a supported RESOLUTION on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	  goto fail;
	}
      }

      // Default duplex/sides
      if (!params->sides[0])
      {
        if ((keyword = ippGetString(ippFileGetAttribute(f, "sides", IPP_TAG_ZERO), 0, NULL)) != NULL)
        {
          // Use the setting from the job ticket...
          cupsCopyString(params->sides, keyword, sizeof(params->sides));
	}
	else if (params->num_pages != 1 && (attr = ippFindAttribute(response, "sides-supported", IPP_TAG_KEYWORD)) != NULL && ippGetCount(attr) > 1)
	{
	  // Default to two-sided for capable printers...
	  if (params->orientation == IPP_ORIENT_LANDSCAPE || params->orientation == IPP_ORIENT_REVERSE_LANDSCAPE)
	    cupsCopyString(params->sides, "two-sided-short-edge", sizeof(params->sides));
	  else
	    cupsCopyString(params->sides, "two-sided-long-edge", sizeof(params->sides));
	}
	else
	{
	  // Fall back to 1-sided output...
	  cupsCopyString(params->sides, "one-sided", sizeof(params->sides));
	}
      }

      // Default orientation
      if (!params->orientation)
      {
        // Use the job ticket value, otherwise use landscape for short-edge duplex
        if ((attr = ippFileGetAttribute(f, "orientation-requested", IPP_TAG_ENUM)) != NULL)
          params->orientation = (ipp_orient_t)ippGetInteger(attr, 0);
	else
	  params->orientation = !strcmp(params->sides, "two-sided-short-edge") ? IPP_ORIENT_LANDSCAPE : IPP_ORIENT_PORTRAIT;
      }

      // Default number of copies and pages...
      if (!params->num_copies)
        params->num_copies = 1;

      if (!params->num_pages)
        params->num_pages = !strncmp(params->sides, "two-sided-", 10) ? 2 : 1;

      // Back side transform, if any
      if (!params->sheet_back[0])
      {
        if ((attr = ippFindAttribute(response, "pwg-raster-document-sheet-back", IPP_TAG_KEYWORD)) != NULL)
        {
          cupsCopyString(params->sheet_back, ippGetString(attr, 0, NULL), sizeof(params->sheet_back));
	}
	else if ((attr = ippFindAttribute(response, "urf-supported", IPP_TAG_KEYWORD)) != NULL)
	{
	  if (ippContainsString(attr, "DM1"))
	    cupsCopyString(params->sheet_back, "flip", sizeof(params->sheet_back));
	  else if (ippContainsString(attr, "DM2"))
	    cupsCopyString(params->sheet_back, "manual-tumble", sizeof(params->sheet_back));
	  else if (ippContainsString(attr, "DM3"))
	    cupsCopyString(params->sheet_back, "rotated", sizeof(params->sheet_back));
	  else
	    cupsCopyString(params->sheet_back, "normal", sizeof(params->sheet_back));
	}
	else
        {
          cupsCopyString(params->sheet_back, "normal", sizeof(params->sheet_back));
	}
      }

      // Everything is good, save the parameters and return...
      data->generate_params = params;
      ippDelete(response);

      return (1);
    }
    else if (!_cups_strcasecmp(token, "COLORSPACE"))
    {
      if (params->type[0])
      {
	print_fatal_error(data, "Unexpected extra COLORSPACE on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }

      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing COLORSPACE value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }

      ippFileExpandVars(f, value, temp, sizeof(value));

      if (!strcmp(value, "auto") || !strcmp(value, "bi-level") || !strcmp(value, "color") || !strcmp(value, "monochrome") || !strcmp(value, "adobe-rgb_8") || !strcmp(value, "adobe-rgb_16") || !strcmp(value, "black_1") || !strcmp(value, "black_8") || !strcmp(value, "black_16") || !strcmp(value, "cmyk_8") || !strcmp(value, "cmyk_16") || !strcmp(value, "rgb_8") || !strcmp(value, "rgb_16") || !strcmp(value, "sgray_1") || !strcmp(value, "sgray_8") || !strcmp(value, "sgray_16") || !strcmp(value, "srgb_8") || !strcmp(value, "srgb_16"))
      {
        // Use "print-color-mode" or "pwg-raster-document-type-supported" keyword...
        cupsCopyString(params->type, value, sizeof(params->type));
      }
      else
      {
	print_fatal_error(data, "Bad COLORSPACE \"%s\" on line %d of '%s'.", value, ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }
    }
    else if (!_cups_strcasecmp(token, "FORMAT"))
    {
      if (params->format[0])
      {
	print_fatal_error(data, "Unexpected extra FORMAT on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }

      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing FORMAT MIME media type on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }

      ippFileExpandVars(f, value, temp, sizeof(value));

      if (!strcmp(value, "image/pwg-raster") || !strcmp(value, "image/urf"))
      {
        cupsCopyString(params->format, value, sizeof(params->format));
      }
      else
      {
	print_fatal_error(data, "Bad FORMAT \"%s\" on line %d of '%s'.", value, ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }
    }
    else if (!_cups_strcasecmp(token, "MEDIA"))
    {
      if (params->media[0])
      {
	print_fatal_error(data, "Unexpected extra MEDIA on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }

      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing MEDIA size name on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }

      ippFileExpandVars(f, value, temp, sizeof(value));

      if (!strcmp(value, "default") || !strcmp(value, "ready") || pwgMediaForPWG(value) != NULL)
      {
        cupsCopyString(params->media, value, sizeof(params->media));
      }
      else
      {
	print_fatal_error(data, "Bad MEDIA \"%s\" on line %d of '%s'.", value, ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }
    }
    else if (!_cups_strcasecmp(token, "NUM-COPIES"))
    {
      int	intvalue;		// Number of copies value

      if (params->num_copies)
      {
	print_fatal_error(data, "Unexpected extra NUM-COPIES on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }

      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing NUM-COPIES number on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }

      ippFileExpandVars(f, value, temp, sizeof(value));

      if ((intvalue = strtol(value, NULL, 10)) > INT_MAX || intvalue < 1)
      {
	print_fatal_error(data, "Bad NUM-COPIES \"%s\" on line %d of '%s'.", value, ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }

      params->num_copies = intvalue;
    }
    else if (!_cups_strcasecmp(token, "NUM-PAGES"))
    {
      int	intvalue;		// Number of pages value

      if (params->num_pages)
      {
	print_fatal_error(data, "Unexpected extra NUM-PAGES on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }

      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing NUM-PAGES number on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }

      ippFileExpandVars(f, value, temp, sizeof(value));

      if ((intvalue = strtol(value, NULL, 10)) > INT_MAX || intvalue < 1)
      {
	print_fatal_error(data, "Bad NUM-PAGES \"%s\" on line %d of '%s'.", value, ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }

      params->num_pages = (int)intvalue;
    }
    else if (!_cups_strcasecmp(token, "ORIENTATION"))
    {
      if (params->orientation)
      {
	print_fatal_error(data, "Unexpected extra ORIENTATION on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }

      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing ORIENTATION on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }

      ippFileExpandVars(f, value, temp, sizeof(value));

      if (!strcmp(value, "portrait"))
        params->orientation = IPP_ORIENT_PORTRAIT;
      else if (!strcmp(value, "landscape"))
        params->orientation = IPP_ORIENT_LANDSCAPE;
      else if (!strcmp(value, "reverse-landscape"))
        params->orientation = IPP_ORIENT_REVERSE_LANDSCAPE;
      else if (!strcmp(value, "reverse-portrait"))
        params->orientation = IPP_ORIENT_REVERSE_PORTRAIT;
      else
      {
	print_fatal_error(data, "Bad ORIENTATION \"%s\" on line %d of '%s'.", value, ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }
    }
    else if (!_cups_strcasecmp(token, "RESOLUTION"))
    {
      if (params->xdpi || params->ydpi)
      {
	print_fatal_error(data, "Unexpected extra RESOLUTION on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }

      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing RESOLUTION on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }

      ippFileExpandVars(f, value, temp, sizeof(value));

      if (!strcmp(value, "min") || !strcmp(value, "max"))
      {
	if ((attr = ippFindAttribute(response, "pwg-raster-document-resolution-supported", IPP_TAG_RESOLUTION)) != NULL)
	{
	  ipp_res_t	units;			// Resolution units

          // Use the first or last resolution in the list...
          params->xdpi = ippGetResolution(attr, !strcmp(value, "min") ? 0 : ippGetCount(attr) - 1, &params->ydpi, &units);

          if (units == IPP_RES_PER_CM)
          {
            params->xdpi = (int)(params->xdpi * 2.54);
            params->ydpi = (int)(params->ydpi * 2.54);
	  }
        }
        else if ((attr = ippFindAttribute(response, "urf-supported", IPP_TAG_KEYWORD)) != NULL)
        {
          int count = ippGetCount(attr);	// Number of values

          for (i = 0; i < count; i ++)
          {
            keyword = ippGetString(attr, i, NULL);
            if (!strncmp(keyword, "RS", 2))
	    {
	      if (!strcmp(value, "min"))
	      {
		// Use the first resolution in the list...
		params->xdpi = params->ydpi = atoi(keyword + 2);
	      }
	      else
	      {
	        // Use the last resolution in the list...
	        params->xdpi = params->ydpi = (int)strtol(keyword + 2, &ptr, 10);
	        while (ptr && *ptr && *ptr == '-')
		  params->xdpi = params->ydpi = (int)strtol(ptr + 1, &ptr, 10);
	      }
	      break;
	    }
          }
	}
      }
      else if (strcmp(value, "default"))
      {
        char	units[8] = "";		// Resolution units (dpi or dpcm)

	if (sscanf(value, "%dx%d%7s", &params->xdpi, &params->ydpi, units) == 1)
	{
	  sscanf(value, "%d%7s", &params->xdpi, units);
	  params->ydpi = params->xdpi;
	}

	if (!strcmp(units, "dpcm"))
	{
	  params->xdpi = (int)(params->xdpi * 2.54);
	  params->ydpi = (int)(params->ydpi * 2.54);
	}
	else if (strcmp(units, "dpi"))
	  params->xdpi = params->ydpi = 0;
      }

      if (strcmp(value, "default") && (params->xdpi <= 0 || params->ydpi <= 0))
      {
	print_fatal_error(data, "Bad RESOLUTION \"%s\" on line %d of '%s'.", value, ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }
    }
    else if (!_cups_strcasecmp(token, "SIDES"))
    {
      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing SIDES on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }

      ippFileExpandVars(f, value, temp, sizeof(value));

      if (!strcmp(value, "one-sided") || !strcmp(value, "two-sided-long-edge") || !strcmp(value, "two-sided-short-edge"))
      {
        cupsCopyString(params->sides, value, sizeof(params->sides));
      }
      else
      {
	print_fatal_error(data, "Bad SIDES \"%s\" on line %d of '%s'.", value, ippFileGetLineNumber(f), ippFileGetFilename(f));
	goto fail;
      }
    }
    else
    {
      print_fatal_error(data, "Unknown %s on line %d of '%s'.", token, ippFileGetLineNumber(f), ippFileGetFilename(f));
      goto fail;
    }
  }

  print_fatal_error(data, "Missing closing brace on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));

  fail:

  free(params);
  ippDelete(response);
  return (0);
}


//
// 'parse_monitor_printer_state()' - Parse the MONITOR-PRINTER-STATE directive.
//
// MONITOR-PRINTER-STATE [printer-uri] {
//     DELAY nnn
//     EXPECT attribute-name ...
// }
//

static bool				// O - `true` to continue, `false` to stop
parse_monitor_printer_state(
    ipp_file_t     *f,			// I - IPP file data
    ipptool_test_t *data)		// I - Test data
{
  char		token[256],		// Token string
		name[1024],		// Name string
		temp[1024],		// Temporary string
		value[1024],		// Value string
		*ptr;			// Pointer into value
  const char	*uri;			// Printer URI


  if (!ippFileReadToken(f, temp, sizeof(temp)))
  {
    print_fatal_error(data, "Missing printer URI on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
    return (0);
  }

  if (strcmp(temp, "{"))
  {
    // Got a printer URI so copy it...
    ippFileExpandVars(f, value, temp, sizeof(value));
    data->monitor_uri = strdup(value);

    // Then see if we have an opening brace...
    if (!ippFileReadToken(f, temp, sizeof(temp)) || strcmp(temp, "{"))
    {
      print_fatal_error(data, "Missing opening brace on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
      return (0);
    }
  }
  else if ((uri = ippFileGetVar(data->parent, "uri")) != NULL)
  {
    // Use the default printer URI...
    data->monitor_uri = strdup(uri);
  }

  // Loop until we get a closing brace...
  while (ippFileReadToken(f, token, sizeof(token)))
  {
    if (_cups_strcasecmp(token, "COUNT") &&
	_cups_strcasecmp(token, "DEFINE-MATCH") &&
	_cups_strcasecmp(token, "DEFINE-NO-MATCH") &&
	_cups_strcasecmp(token, "DEFINE-VALUE") &&
	_cups_strcasecmp(token, "DISPLAY-MATCH") &&
	_cups_strcasecmp(token, "IF-DEFINED") &&
	_cups_strcasecmp(token, "IF-NOT-DEFINED") &&
	_cups_strcasecmp(token, "IN-GROUP") &&
	_cups_strcasecmp(token, "OF-TYPE") &&
	_cups_strcasecmp(token, "WITH-DISTINCT-VALUES") &&
	_cups_strcasecmp(token, "WITH-VALUE"))
      data->last_expect = NULL;

    if (!strcmp(token, "}"))
      return (1);
    else if (!_cups_strcasecmp(token, "EXPECT"))
    {
      // Expected attributes...
      if (data->num_monitor_expects >= (int)(sizeof(data->monitor_expects) / sizeof(data->monitor_expects[0])))
      {
	print_fatal_error(data, "Too many EXPECT's on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }

      if (!ippFileReadToken(f, name, sizeof(name)))
      {
	print_fatal_error(data, "Missing EXPECT name on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }

      data->last_expect = data->monitor_expects + data->num_monitor_expects;
      data->num_monitor_expects ++;

      memset(data->last_expect, 0, sizeof(ipptool_expect_t));
      data->last_expect->repeat_limit = 1000;

      if (name[0] == '!')
      {
	data->last_expect->not_expect = 1;
	data->last_expect->name       = strdup(name + 1);
      }
      else if (name[0] == '?')
      {
	data->last_expect->optional = 1;
	data->last_expect->name     = strdup(name + 1);
      }
      else
	data->last_expect->name = strdup(name);
    }
    else if (!_cups_strcasecmp(token, "COUNT"))
    {
      int	count;			// Count value

      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing COUNT number on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }

      if ((count = strtol(temp, NULL, 10)) > INT_MAX)
      {
	print_fatal_error(data, "Bad COUNT \"%s\" on line %d of '%s'.", temp, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }

      if (data->last_expect)
      {
	data->last_expect->count = count;
      }
      else
      {
	print_fatal_error(data, "COUNT without a preceding EXPECT on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }
    }
    else if (!_cups_strcasecmp(token, "DEFINE-MATCH"))
    {
      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing DEFINE-MATCH variable on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }

      if (data->last_expect)
      {
	data->last_expect->define_match = strdup(temp);
      }
      else
      {
	print_fatal_error(data, "DEFINE-MATCH without a preceding EXPECT on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }
    }
    else if (!_cups_strcasecmp(token, "DEFINE-NO-MATCH"))
    {
      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing DEFINE-NO-MATCH variable on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }

      if (data->last_expect)
      {
	data->last_expect->define_no_match = strdup(temp);
      }
      else
      {
	print_fatal_error(data, "DEFINE-NO-MATCH without a preceding EXPECT on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }
    }
    else if (!_cups_strcasecmp(token, "DEFINE-VALUE"))
    {
      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing DEFINE-VALUE variable on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }

      if (data->last_expect)
      {
	data->last_expect->define_value = strdup(temp);
      }
      else
      {
	print_fatal_error(data, "DEFINE-VALUE without a preceding EXPECT on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }
    }
    else if (!_cups_strcasecmp(token, "DISPLAY-MATCH"))
    {
      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing DISPLAY-MATCH message on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }

      if (data->last_expect)
      {
	data->last_expect->display_match = strdup(temp);
      }
      else
      {
	print_fatal_error(data, "DISPLAY-MATCH without a preceding EXPECT on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }
    }
    else if (!_cups_strcasecmp(token, "DELAY"))
    {
      // Delay before operation...
      double dval;                    // Delay value

      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing DELAY value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }

      ippFileExpandVars(f, value, temp, sizeof(value));

      if ((dval = _cupsStrScand(value, &ptr, localeconv())) < 0.0 || (*ptr && *ptr != ','))
      {
	print_fatal_error(data, "Bad DELAY value \"%s\" on line %d of '%s'.", value, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }

      data->monitor_delay = (useconds_t)(1000000.0 * dval);

      if (*ptr == ',')
      {
	if ((dval = _cupsStrScand(ptr + 1, &ptr, localeconv())) <= 0.0 || *ptr)
	{
	  print_fatal_error(data, "Bad DELAY value \"%s\" on line %d of '%s'.", value, ippFileGetLineNumber(f), ippFileGetFilename(f));
	  return (0);
	}

	data->monitor_interval = (useconds_t)(1000000.0 * dval);
      }
      else
	data->monitor_interval = data->monitor_delay;
    }
    else if (!_cups_strcasecmp(token, "OF-TYPE"))
    {
      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing OF-TYPE value tag(s) on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }

      if (data->last_expect)
      {
	data->last_expect->of_type = strdup(temp);
      }
      else
      {
	print_fatal_error(data, "OF-TYPE without a preceding EXPECT on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }
    }
    else if (!_cups_strcasecmp(token, "IN-GROUP"))
    {
      ipp_tag_t	in_group;		// IN-GROUP value

      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing IN-GROUP group tag on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }

      if ((in_group = ippTagValue(temp)) == IPP_TAG_ZERO || in_group >= IPP_TAG_UNSUPPORTED_VALUE)
      {
	print_fatal_error(data, "Bad IN-GROUP group tag \"%s\" on line %d of '%s'.", temp, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }
      else if (data->last_expect)
      {
	data->last_expect->in_group = in_group;
      }
      else
      {
	print_fatal_error(data, "IN-GROUP without a preceding EXPECT on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }
    }
    else if (!_cups_strcasecmp(token, "IF-DEFINED"))
    {
      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing IF-DEFINED name on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }

      if (data->last_expect)
      {
	data->last_expect->if_defined = strdup(temp);
      }
      else
      {
	print_fatal_error(data, "IF-DEFINED without a preceding EXPECT on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }
    }
    else if (!_cups_strcasecmp(token, "IF-NOT-DEFINED"))
    {
      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing IF-NOT-DEFINED name on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }

      if (data->last_expect)
      {
	data->last_expect->if_not_defined = strdup(temp);
      }
      else
      {
	print_fatal_error(data, "IF-NOT-DEFINED without a preceding EXPECT on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }
    }
    else if (!_cups_strcasecmp(token, "WITH-DISTINCT-VALUES"))
    {
      if (data->last_expect)
      {
        data->last_expect->with_distinct = 1;
      }
      else
      {
	print_fatal_error(data, "%s without a preceding EXPECT on line %d of '%s'.", token, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }
    }
    else if (!_cups_strcasecmp(token, "WITH-VALUE"))
    {
      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing %s value on line %d of '%s'.", token, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }

      // Read additional comma-delimited values - needed since legacy test files
      // will have unquoted WITH-VALUE values with commas...
      ptr = temp + strlen(temp);

      for (;;)
      {
        ippFileSavePosition(f);

        ptr += strlen(ptr);

	if (!ippFileReadToken(f, ptr, (sizeof(temp) - (size_t)(ptr - temp))))
	  break;

        if (!strcmp(ptr, ","))
        {
          // Append a value...
	  ptr += strlen(ptr);

	  if (!ippFileReadToken(f, ptr, (sizeof(temp) - (size_t)(ptr - temp))))
	    break;
        }
        else
        {
          // Not another value, stop here...
          ippFileRestorePosition(f);
          *ptr = '\0';
          break;
	}
      }

      if (data->last_expect)
      {
        // Expand any variables in the value and then save it.
	ippFileExpandVars(f, value, temp, sizeof(value));

	ptr = value + strlen(value) - 1;

	if (value[0] == '/' && ptr > value && *ptr == '/')
	{
	  // WITH-VALUE is a POSIX extended regular expression.
	  data->last_expect->with_value = calloc(1, (size_t)(ptr - value));
	  data->last_expect->with_flags |= IPPTOOL_WITH_REGEX;

	  if (data->last_expect->with_value)
	    memcpy(data->last_expect->with_value, value + 1, (size_t)(ptr - value - 1));
	}
	else
	{
	  // WITH-VALUE is a literal value...
	  for (ptr = value; *ptr; ptr ++)
	  {
	    if (*ptr == '\\' && ptr[1])
	    {
	      // Remove \ from \foo...
	      _cups_strcpy(ptr, ptr + 1);
	    }
	  }

	  data->last_expect->with_value = strdup(value);
	  data->last_expect->with_flags |= IPPTOOL_WITH_LITERAL;
	}
      }
      else
      {
	print_fatal_error(data, "%s without a preceding EXPECT on line %d of '%s'.", token, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (0);
      }
    }
  }

  print_fatal_error(data, "Missing closing brace on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));

  return (0);
}


//
// 'password_cb()' - Password callback using the IPP variables.
//

const char *				// O - Password string or @code NULL@
password_cb(
    const char *prompt,			// I - Prompt string (not used)
    http_t     *http,			// I - HTTP connection (not used)
    const char *method,			// I - HTTP method (not used)
    const char *resource,		// I - Resource path (not used)
    void       *user_data)		// I - IPP test data
{
  ipptool_test_t *test = (ipptool_test_t *)user_data;
					// IPP test data
  const char	*uriuser = ippFileGetVar(test->parent, "uriuser"),
					// Username
		*uripassword = ippFileGetVar(test->parent, "uripassword");
					// Password


  (void)prompt;
  (void)http;
  (void)method;
  (void)resource;

  if (uriuser && uripassword && test->password_tries < 3)
  {
    test->password_tries ++;

    cupsSetUser(uriuser);

    return (uripassword);
  }
  else
  {
    return (NULL);
  }
}


//
// 'pause_message()' - Display the message and pause until the user presses a key.
//

static void
pause_message(const char *message)	// I - Message
{
#ifdef _WIN32
  HANDLE	tty;			// Console handle
  DWORD		mode;			// Console mode
  char		key;			// Key press
  DWORD		bytes;			// Bytes read for key press


  // Disable input echo and set raw input...
  if ((tty = GetStdHandle(STD_INPUT_HANDLE)) == INVALID_HANDLE_VALUE)
    return;

  if (!GetConsoleMode(tty, &mode))
    return;

  if (!SetConsoleMode(tty, 0))
    return;

#else
  int			tty;		// /dev/tty - never read from stdin
  struct termios	original,	// Original input mode
			noecho;		// No echo input mode
  char			key;		// Current key press


  // Disable input echo and set raw input...
  if ((tty = open("/dev/tty", O_RDONLY)) < 0)
    return;

  if (tcgetattr(tty, &original))
  {
    close(tty);
    return;
  }

  noecho = original;
  noecho.c_lflag &= (tcflag_t)~(ICANON | ECHO | ECHOE | ISIG);

  if (tcsetattr(tty, TCSAFLUSH, &noecho))
  {
    close(tty);
    return;
  }
#endif // _WIN32

  // Display the prompt...
  cupsFilePrintf(cupsFileStdout(), "\n%s\n\n---- PRESS ANY KEY ----", message);

#ifdef _WIN32
  // Read a key...
  ReadFile(tty, &key, 1, &bytes, NULL);

  // Cleanup...
  SetConsoleMode(tty, mode);

#else
  // Read a key...
  read(tty, &key, 1);

  // Cleanup...
  tcsetattr(tty, TCSAFLUSH, &original);
  close(tty);
#endif // _WIN32

  // Erase the "press any key" prompt...
  cupsFilePuts(cupsFileStdout(), "\r                       \r");
}


//
// 'print_attr()' - Print an attribute on the screen.
//

static void
print_attr(cups_file_t      *outfile,	// I  - Output file
           ipptool_output_t output,	// I  - Output format
           ipp_attribute_t  *attr,	// I  - Attribute to print
           ipp_tag_t        *group)	// IO - Current group
{
  int			i,		// Looping var
			count;		// Number of values
  ipp_attribute_t	*colattr;	// Collection attribute


  if (output == IPPTOOL_OUTPUT_PLIST)
  {
    if (!ippGetName(attr) || (group && *group != ippGetGroupTag(attr)))
    {
      if (ippGetGroupTag(attr) != IPP_TAG_ZERO)
      {
	cupsFilePuts(outfile, "</dict>\n");
	cupsFilePuts(outfile, "<dict>\n");
      }

      if (group)
        *group = ippGetGroupTag(attr);
    }

    if (!ippGetName(attr))
      return;

    print_xml_string(outfile, "key", ippGetName(attr));
    if ((count = ippGetCount(attr)) > 1)
      cupsFilePuts(outfile, "<array>\n");

    switch (ippGetValueTag(attr))
    {
      case IPP_TAG_INTEGER :
      case IPP_TAG_ENUM :
	  for (i = 0; i < count; i ++)
	    cupsFilePrintf(outfile, "<integer>%d</integer>\n", ippGetInteger(attr, i));
	  break;

      case IPP_TAG_BOOLEAN :
	  for (i = 0; i < count; i ++)
	    cupsFilePuts(outfile, ippGetBoolean(attr, i) ? "<true />\n" : "<false />\n");
	  break;

      case IPP_TAG_RANGE :
	  for (i = 0; i < count; i ++)
	  {
	    int lower, upper;		// Lower and upper ranges

	    lower = ippGetRange(attr, i, &upper);
	    cupsFilePrintf(outfile, "<dict><key>lower</key><integer>%d</integer><key>upper</key><integer>%d</integer></dict>\n", lower, upper);
	  }
	  break;

      case IPP_TAG_RESOLUTION :
	  for (i = 0; i < count; i ++)
	  {
	    int		xres, yres;	// Resolution values
	    ipp_res_t	units;		// Resolution units

            xres = ippGetResolution(attr, i, &yres, &units);
	    cupsFilePrintf(outfile, "<dict><key>xres</key><integer>%d</integer><key>yres</key><integer>%d</integer><key>units</key><string>%s</string></dict>\n", xres, yres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
	  }
	  break;

      case IPP_TAG_DATE :
	  for (i = 0; i < count; i ++)
	    cupsFilePrintf(outfile, "<date>%s</date>\n", iso_date(ippGetDate(attr, i)));
	  break;

      case IPP_TAG_STRING :
          for (i = 0; i < count; i ++)
          {
            int		datalen;	// Length of data
            void	*data = ippGetOctetString(attr, i, &datalen);
					// Data
	    char	buffer[IPP_MAX_LENGTH * 5 / 4 + 1];
					// Base64 output buffer

	    cupsFilePrintf(outfile, "<data>%s</data>\n", httpEncode64_3(buffer, sizeof(buffer), data, (size_t)datalen, false));
          }
          break;

      case IPP_TAG_TEXT :
      case IPP_TAG_NAME :
      case IPP_TAG_KEYWORD :
      case IPP_TAG_URI :
      case IPP_TAG_URISCHEME :
      case IPP_TAG_CHARSET :
      case IPP_TAG_LANGUAGE :
      case IPP_TAG_MIMETYPE :
	  for (i = 0; i < count; i ++)
	    print_xml_string(outfile, "string", ippGetString(attr, i, NULL));
	  break;

      case IPP_TAG_TEXTLANG :
      case IPP_TAG_NAMELANG :
	  for (i = 0; i < count; i ++)
	  {
	    const char *s,		// String
			*lang;		// Language

            s = ippGetString(attr, i, &lang);
	    cupsFilePuts(outfile, "<dict><key>language</key><string>");
	    print_xml_string(outfile, NULL, lang);
	    cupsFilePuts(outfile, "</string><key>string</key><string>");
	    print_xml_string(outfile, NULL, s);
	    cupsFilePuts(outfile, "</string></dict>\n");
	  }
	  break;

      case IPP_TAG_BEGIN_COLLECTION :
	  for (i = 0; i < count; i ++)
	  {
	    ipp_t *col = ippGetCollection(attr, i);
					// Collection value

	    cupsFilePuts(outfile, "<dict>\n");
	    for (colattr = ippGetFirstAttribute(col); colattr; colattr = ippGetNextAttribute(col))
	      print_attr(outfile, output, colattr, NULL);
	    cupsFilePuts(outfile, "</dict>\n");
	  }
	  break;

      default :
	  cupsFilePrintf(outfile, "<string>&lt;&lt;%s&gt;&gt;</string>\n", ippTagString(ippGetValueTag(attr)));
	  break;
    }

    if (count > 1)
      cupsFilePuts(outfile, "</array>\n");
  }
  else
  {
    size_t		attrsize;	// Size of current attribute
    static char		*buffer = NULL;	// Value buffer
    static size_t	bufsize = 0;	// Current size of value buffer

    if (!buffer)
    {
      bufsize = 65536;
      buffer  = malloc(bufsize);
    }

    if (output == IPPTOOL_OUTPUT_TEST)
    {
      if (!ippGetName(attr))
      {
        cupsFilePuts(outfile, "        -- separator --\n");
        return;
      }

      cupsFilePrintf(outfile, "        %s (%s%s) = ", ippGetName(attr), ippGetCount(attr) > 1 ? "1setOf " : "", ippTagString(ippGetValueTag(attr)));
    }

    if ((attrsize = ippAttributeString(attr, buffer, bufsize)) >= bufsize)
    {
      // Expand attribute value buffer...
      char *temp = realloc(buffer, attrsize + 1);
					// New buffer pointer

      if (temp)
      {
        buffer  = temp;
        bufsize = attrsize + 1;

	ippAttributeString(attr, buffer, bufsize);
      }

    }
    cupsFilePrintf(outfile, "%s\n", buffer);
  }
}


//
// 'print_csv()' - Print a line of CSV text.
//

static ipp_attribute_t *		// O - Next attribute
print_csv(
    ipptool_test_t  *data,		// I - Test data
    ipp_t           *ipp,		// I - Response message
    ipp_attribute_t *attr,		// I - First attribute for line
    int             num_displayed,	// I - Number of attributes to display
    char            **displayed,	// I - Attributes to display
    int             *widths)		// I - Column widths
{
  int		i;			// Looping var
  int		maxlength;		// Max length of all columns
  ipp_attribute_t *current = attr;	// Current attribute
  char		*values[MAX_DISPLAY],	// Strings to display
		*valptr;		// Pointer into value

  // Get the maximum string length we have to show and allocate...
  for (i = 1, maxlength = widths[0]; i < num_displayed; i ++)
  {
    if (widths[i] > maxlength)
      maxlength = widths[i];
  }

  maxlength += 2;

  // Loop through the attributes to display...
  if (attr)
  {
    // Collect the values...
    memset(values, 0, sizeof(values));

    for (; current; current = ippGetNextAttribute(ipp))
    {
      if (!ippGetName(current))
	break;

      for (i = 0; i < num_displayed; i ++)
      {
        if (!strcmp(ippGetName(current), displayed[i]))
        {
          if ((values[i] = (char *)calloc(1, (size_t)maxlength)) != NULL)
	    ippAttributeString(current, values[i], (size_t)maxlength);
          break;
	}
      }
    }

    // Output the line...
    for (i = 0; i < num_displayed; i ++)
    {
      if (i)
        cupsFilePutChar(data->outfile, ',');

      if (!values[i])
        continue;

      if (strchr(values[i], ',') != NULL || strchr(values[i], '\"') != NULL || strchr(values[i], '\\') != NULL)
      {
        // Quoted value...
        cupsFilePutChar(data->outfile, '\"');
        for (valptr = values[i]; *valptr; valptr ++)
        {
          if (*valptr == '\\' || *valptr == '\"')
            cupsFilePutChar(data->outfile, '\\');
          cupsFilePutChar(data->outfile, *valptr);
        }
        cupsFilePutChar(data->outfile, '\"');
      }
      else
      {
        // Unquoted value...
        cupsFilePuts(data->outfile, values[i]);
      }

      free(values[i]);
    }
    cupsFilePutChar(data->outfile, '\n');
  }
  else
  {
    // Show column headings...
    for (i = 0; i < num_displayed; i ++)
    {
      if (i)
        cupsFilePutChar(data->outfile, ',');

      cupsFilePuts(data->outfile, displayed[i]);
    }
    cupsFilePutChar(data->outfile, '\n');
  }

  return (current);
}


//
// 'print_fatal_error()' - Print a fatal error message.
//

static void
print_fatal_error(
    ipptool_test_t *data,		// I - Test data
    const char       *s,		// I - Printf-style format string
    ...)				// I - Additional arguments as needed
{
  char		buffer[10240];		// Format buffer
  va_list	ap;			// Pointer to arguments


  // Format the error message...
  va_start(ap, s);
  vsnprintf(buffer, sizeof(buffer), s, ap);
  va_end(ap);

  // Then output it...
  if (data->output == IPPTOOL_OUTPUT_PLIST)
  {
    print_xml_header(data);
    print_xml_trailer(data, 0, buffer);
  }

  _cupsLangPrintf(stderr, "ipptool: %s", buffer);
}


//
// 'print_ippserver_attr()' - Print a attribute suitable for use by ippserver.
//

static void
print_ippserver_attr(
    ipptool_test_t *data,		// I - Test data
    ipp_attribute_t  *attr,		// I - Attribute to print
    int              indent)		// I - Indentation level
{
  int			i,		// Looping var
			count = ippGetCount(attr);
					// Number of values
  ipp_attribute_t	*colattr;	// Collection attribute


  if (indent == 0)
    cupsFilePrintf(data->outfile, "ATTR %s %s", ippTagString(ippGetValueTag(attr)), ippGetName(attr));
  else
    cupsFilePrintf(data->outfile, "%*sMEMBER %s %s", indent, "", ippTagString(ippGetValueTag(attr)), ippGetName(attr));

  switch (ippGetValueTag(attr))
  {
    case IPP_TAG_INTEGER :
    case IPP_TAG_ENUM :
	for (i = 0; i < count; i ++)
	  cupsFilePrintf(data->outfile, "%s%d", i ? "," : " ", ippGetInteger(attr, i));
	break;

    case IPP_TAG_BOOLEAN :
	cupsFilePuts(data->outfile, ippGetBoolean(attr, 0) ? " true" : " false");

	for (i = 1; i < count; i ++)
	  cupsFilePuts(data->outfile, ippGetBoolean(attr, 1) ? ",true" : ",false");
	break;

    case IPP_TAG_RANGE :
	for (i = 0; i < count; i ++)
	{
	  int upper, lower = ippGetRange(attr, i, &upper);

	  cupsFilePrintf(data->outfile, "%s%d-%d", i ? "," : " ", lower, upper);
	}
	break;

    case IPP_TAG_RESOLUTION :
	for (i = 0; i < count; i ++)
	{
	  ipp_res_t units;
	  int yres, xres = ippGetResolution(attr, i, &yres, &units);

	  cupsFilePrintf(data->outfile, "%s%dx%d%s", i ? "," : " ", xres, yres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
	}
	break;

    case IPP_TAG_DATE :
	for (i = 0; i < count; i ++)
	  cupsFilePrintf(data->outfile, "%s%s", i ? "," : " ", iso_date(ippGetDate(attr, i)));
	break;

    case IPP_TAG_STRING :
	for (i = 0; i < count; i ++)
	{
	  int len;
	  const char *s = (const char *)ippGetOctetString(attr, i, &len);

	  cupsFilePuts(data->outfile, i ? "," : " ");
	  print_ippserver_string(data, s, (size_t)len);
	}
	break;

    case IPP_TAG_TEXT :
    case IPP_TAG_TEXTLANG :
    case IPP_TAG_NAME :
    case IPP_TAG_NAMELANG :
    case IPP_TAG_KEYWORD :
    case IPP_TAG_URI :
    case IPP_TAG_URISCHEME :
    case IPP_TAG_CHARSET :
    case IPP_TAG_LANGUAGE :
    case IPP_TAG_MIMETYPE :
	for (i = 0; i < count; i ++)
	{
	  const char *s = ippGetString(attr, i, NULL);

	  cupsFilePuts(data->outfile, i ? "," : " ");
	  print_ippserver_string(data, s, strlen(s));
	}
	break;

    case IPP_TAG_BEGIN_COLLECTION :
	for (i = 0; i < count; i ++)
	{
	  ipp_t *col = ippGetCollection(attr, i);

	  cupsFilePuts(data->outfile, i ? ",{\n" : " {\n");
	  for (colattr = ippGetFirstAttribute(col); colattr; colattr = ippGetNextAttribute(col))
	    print_ippserver_attr(data, colattr, indent + 4);
	  cupsFilePrintf(data->outfile, "%*s}", indent, "");
	}
	break;

    default :
        // Out-of-band value
	break;
  }

  cupsFilePuts(data->outfile, "\n");
}


//
// 'print_ippserver_string()' - Print a string suitable for use by ippserver.
//

static void
print_ippserver_string(
    ipptool_test_t *data,		// I - Test data
    const char     *s,			// I - String to print
    size_t         len)			// I - Length of string
{
  cupsFilePutChar(data->outfile, '\"');
  while (len > 0)
  {
    if (*s == '\"' || *s == '\\')
      cupsFilePutChar(data->outfile, '\\');
    cupsFilePutChar(data->outfile, *s);

    s ++;
    len --;
  }
  cupsFilePutChar(data->outfile, '\"');
}


//
// 'print_json_attr()' - Print an attribute in JSON format.
//

static void
print_json_attr(
    ipptool_test_t  *data,		// I - Test data
    ipp_attribute_t *attr,		// I - IPP attribute
    int             indent)		// I - Indentation
{
  const char	*name = ippGetName(attr);
					// Name of attribute
  int		i,			// Looping var
		count = ippGetCount(attr);
					// Number of values
  ipp_attribute_t *colattr;		// Collection attribute


  cupsFilePrintf(data->outfile, "%*s", indent, "");
  print_json_string(data, name, strlen(name));

  switch (ippGetValueTag(attr))
  {
    case IPP_TAG_INTEGER :
    case IPP_TAG_ENUM :
        if (count == 1)
        {
	  cupsFilePrintf(data->outfile, ": %d", ippGetInteger(attr, 0));
        }
        else
        {
          cupsFilePuts(data->outfile, ": [\n");
	  for (i = 0; i < count; i ++)
	    cupsFilePrintf(data->outfile, "%*s%d%s", indent + 4, "", ippGetInteger(attr, i), (i + 1) < count ? ",\n" : "\n");
          cupsFilePrintf(data->outfile, "%*s]", indent, "");
	}
	break;

    case IPP_TAG_BOOLEAN :
        if (count == 1)
        {
	  cupsFilePrintf(data->outfile, ": %s", ippGetBoolean(attr, 0) ? "true" : "false");
        }
        else
        {
          cupsFilePuts(data->outfile, ": [\n");
	  for (i = 0; i < count; i ++)
	    cupsFilePrintf(data->outfile, "%*s%s%s", indent + 4, "", ippGetBoolean(attr, i) ? "true" : "false", (i + 1) < count ? ",\n" : "\n");
          cupsFilePrintf(data->outfile, "%*s]", indent, "");
	}
	break;

    case IPP_TAG_RANGE :
        if (count == 1)
        {
	  int upper, lower = ippGetRange(attr, 0, &upper);

	  cupsFilePrintf(data->outfile, ": {\n%*s\"lower\": %d,\n%*s\"upper\":%d\n%*s}", indent + 4, "", lower, indent + 4, "", upper, indent, "");
        }
        else
        {
          cupsFilePuts(data->outfile, ": [\n");
	  for (i = 0; i < count; i ++)
	  {
	    int upper, lower = ippGetRange(attr, i, &upper);

	    cupsFilePrintf(data->outfile, "%*s{\n%*s\"lower\": %d,\n%*s\"upper\":%d\n%*s},\n", indent + 4, "", indent + 8, "", lower, indent + 8, "", upper, indent + 4, "");
	  }
          cupsFilePrintf(data->outfile, "%*s]", indent, "");
	}
	break;

    case IPP_TAG_RESOLUTION :
        if (count == 1)
        {
	  ipp_res_t units;
	  int yres, xres = ippGetResolution(attr, 0, &yres, &units);

	  cupsFilePrintf(data->outfile, ": {\n%*s\"units\": \"%s\",\n%*s\"xres\": %d,\n%*s\"yres\":%d\n%*s}", indent + 4, "", units == IPP_RES_PER_INCH ? "dpi" : "dpcm", indent + 4, "", xres, indent + 4, "", yres, indent, "");
        }
        else
        {
          cupsFilePuts(data->outfile, ": [\n");
	  for (i = 0; i < count; i ++)
	  {
	    ipp_res_t units;
	    int yres, xres = ippGetResolution(attr, i, &yres, &units);

	    cupsFilePrintf(data->outfile, "%*s{\n%*s\"units\": \"%s\",\n%*s\"xres\": %d,\n%*s\"yres\":%d\n%*s},\n", indent + 4, "", indent + 8, "", units == IPP_RES_PER_INCH ? "dpi" : "dpcm", indent + 8, "", xres, indent + 8, "", yres, indent + 4, "");
	  }
          cupsFilePrintf(data->outfile, "%*s]", indent, "");
	}
	break;

    case IPP_TAG_DATE :
        if (count == 1)
        {
	  cupsFilePrintf(data->outfile, ": \"%s\"", iso_date(ippGetDate(attr, 0)));
        }
        else
        {
          cupsFilePuts(data->outfile, ": [\n");
	  for (i = 0; i < count; i ++)
	    cupsFilePrintf(data->outfile, "%*s\"%s\"%s", indent + 4, "", iso_date(ippGetDate(attr, i)), (i + 1) < count ? ",\n" : "\n");
          cupsFilePrintf(data->outfile, "%*s]", indent, "");
	}
	break;

    case IPP_TAG_STRING :
        if (count == 1)
        {
	  int len;
	  const char *s = (const char *)ippGetOctetString(attr, 0, &len);

	  cupsFilePuts(data->outfile, ": \"");
	  while (len > 0)
	  {
	    cupsFilePrintf(data->outfile, "%02X", *s++ & 255);
	    len --;
	  }
	  cupsFilePuts(data->outfile, "\"");
        }
        else
        {
          cupsFilePuts(data->outfile, ": [\n");
	  for (i = 0; i < count; i ++)
	  {
	    int len;
	    const char *s = (const char *)ippGetOctetString(attr, i, &len);

	    cupsFilePrintf(data->outfile, "%*s\"", indent + 4, "");
	    while (len > 0)
	    {
	      cupsFilePrintf(data->outfile, "%02X", *s++ & 255);
	      len --;
	    }
	    cupsFilePuts(data->outfile, (i + 1) < count ? "\",\n" : "\"\n");
	  }
          cupsFilePrintf(data->outfile, "%*s]", indent, "");
	}
	break;

    case IPP_TAG_TEXT :
    case IPP_TAG_TEXTLANG :
    case IPP_TAG_NAME :
    case IPP_TAG_NAMELANG :
    case IPP_TAG_KEYWORD :
    case IPP_TAG_URI :
    case IPP_TAG_URISCHEME :
    case IPP_TAG_CHARSET :
    case IPP_TAG_LANGUAGE :
    case IPP_TAG_MIMETYPE :
        if (count == 1)
        {
	  const char *s = ippGetString(attr, 0, NULL);

	  cupsFilePuts(data->outfile, ": ");
	  print_json_string(data, s, strlen(s));
        }
        else
        {
          cupsFilePuts(data->outfile, ": [\n");
	  for (i = 0; i < count; i ++)
	  {
	    const char *s = ippGetString(attr, i, NULL);

	    cupsFilePrintf(data->outfile, "%*s", indent + 4, "");
	    print_json_string(data, s, strlen(s));
	    cupsFilePuts(data->outfile, (i + 1) < count ? ",\n" : "\n");
	  }
          cupsFilePrintf(data->outfile, "%*s]", indent, "");
	}
	break;

    case IPP_TAG_BEGIN_COLLECTION :
        if (count == 1)
        {
	  ipp_t *col = ippGetCollection(attr, 0);

	  cupsFilePuts(data->outfile, ": {\n");
	  colattr = ippGetFirstAttribute(col);
	  while (colattr)
	  {
	    print_json_attr(data, colattr, indent + 4);
	    colattr = ippGetNextAttribute(col);
	    cupsFilePuts(data->outfile, colattr ? ",\n" : "\n");
	  }
	  cupsFilePrintf(data->outfile, "%*s}", indent, "");
        }
        else
        {
          cupsFilePuts(data->outfile, ": [\n");
	  for (i = 0; i < count; i ++)
	  {
	    ipp_t *col = ippGetCollection(attr, i);

	    cupsFilePrintf(data->outfile, "%*s{\n", indent + 4, "");
	    colattr = ippGetFirstAttribute(col);
	    while (colattr)
	    {
	      print_json_attr(data, colattr, indent + 8);
	      colattr = ippGetNextAttribute(col);
	      cupsFilePuts(data->outfile, colattr ? ",\n" : "\n");
	    }
	    cupsFilePrintf(data->outfile, "%*s}%s", indent + 4, "", (i + 1) < count ? ",\n" : "\n");
	  }
          cupsFilePrintf(data->outfile, "%*s]", indent, "");
	}
	break;

    default :
        // Out-of-band value
        cupsFilePrintf(data->outfile, ": null");
	break;
  }
}


//
// 'print_json_string()' - Print a string in JSON format.
//

static void
print_json_string(
    ipptool_test_t *data,		// I - Test data
    const char     *s,			// I - String to print
    size_t         len)			// I - Length of string
{
  cupsFilePutChar(data->outfile, '\"');
  while (len > 0)
  {
    switch (*s)
    {
      case '\"' :
      case '\\' :
          cupsFilePutChar(data->outfile, '\\');
	  cupsFilePutChar(data->outfile, *s);
	  break;

      case '\n' :
	  cupsFilePuts(data->outfile, "\\n");
	  break;

      case '\r' :
	  cupsFilePuts(data->outfile, "\\r");
	  break;

      case '\t' :
	  cupsFilePuts(data->outfile, "\\t");
	  break;

      default :
          if (*s < ' ' && *s >= 0)
            cupsFilePrintf(data->outfile, "\\%03o", *s);
          else
	    cupsFilePutChar(data->outfile, *s);
	  break;
    }

    s ++;
    len --;
  }
  cupsFilePutChar(data->outfile, '\"');
}


//
// 'print_line()' - Print a line of formatted or CSV text.
//

static ipp_attribute_t *		// O - Next attribute
print_line(
    ipptool_test_t *data,		// I - Test data
    ipp_t            *ipp,		// I - Response message
    ipp_attribute_t  *attr,		// I - First attribute for line
    int              num_displayed,	// I - Number of attributes to display
    char             **displayed,	// I - Attributes to display
    int              *widths)		// I - Column widths
{
  int		i;			// Looping var
  int		maxlength;		// Max length of all columns
  ipp_attribute_t *current = attr;	// Current attribute
  char		*values[MAX_DISPLAY];	// Strings to display


  // Get the maximum string length we have to show and allocate...
  for (i = 1, maxlength = widths[0]; i < num_displayed; i ++)
  {
    if (widths[i] > maxlength)
      maxlength = widths[i];
  }

  maxlength += 2;

  // Loop through the attributes to display...
  if (attr)
  {
    // Collect the values...
    memset(values, 0, sizeof(values));

    for (; current; current = ippGetNextAttribute(ipp))
    {
      if (!ippGetName(current))
	break;

      for (i = 0; i < num_displayed; i ++)
      {
        if (!strcmp(ippGetName(current), displayed[i]))
        {
          if ((values[i] = (char *)calloc(1, (size_t)maxlength)) != NULL)
	    ippAttributeString(current, values[i], (size_t)maxlength);
          break;
	}
      }
    }

    // Output the line...
    for (i = 0; i < num_displayed; i ++)
    {
      if (i)
        cupsFilePutChar(data->outfile, ' ');

      cupsFilePrintf(data->outfile, "%*s", (int)-widths[i], values[i] ? values[i] : "");
      free(values[i]);
    }
    cupsFilePutChar(data->outfile, '\n');
  }
  else
  {
    // Show column headings...
    char *buffer = (char *)malloc((size_t)maxlength);
					// Buffer for separator lines

    if (!buffer)
      return (current);

    for (i = 0; i < num_displayed; i ++)
    {
      if (i)
        cupsFilePutChar(data->outfile, ' ');

      cupsFilePrintf(data->outfile, "%*s", (int)-widths[i], displayed[i]);
    }
    cupsFilePutChar(data->outfile, '\n');

    for (i = 0; i < num_displayed; i ++)
    {
      if (i)
	cupsFilePutChar(data->outfile, ' ');

      memset(buffer, '-', widths[i]);
      buffer[widths[i]] = '\0';
      cupsFilePuts(data->outfile, buffer);
    }
    cupsFilePutChar(data->outfile, '\n');
    free(buffer);
  }

  return (current);
}


//
// 'print_xml_header()' - Print a standard XML plist header.
//

static void
print_xml_header(ipptool_test_t *data)// I - Test data
{
  if (!data->xml_header)
  {
    cupsFilePuts(data->outfile, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    cupsFilePuts(data->outfile, "<!DOCTYPE plist PUBLIC \"-//Apple Computer//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n");
    cupsFilePuts(data->outfile, "<plist version=\"1.0\">\n");
    cupsFilePuts(data->outfile, "<dict>\n");
    cupsFilePuts(data->outfile, "<key>ipptoolVersion</key>\n");
    cupsFilePuts(data->outfile, "<string>" CUPS_SVERSION "</string>\n");
    cupsFilePuts(data->outfile, "<key>Transfer</key>\n");
    cupsFilePrintf(data->outfile, "<string>%s</string>\n", data->transfer == IPPTOOL_TRANSFER_AUTO ? "auto" : data->transfer == IPPTOOL_TRANSFER_CHUNKED ? "chunked" : "length");
    cupsFilePuts(data->outfile, "<key>Tests</key>\n");
    cupsFilePuts(data->outfile, "<array>\n");

    data->xml_header = 1;
  }
}


//
// 'print_xml_string()' - Print an XML string with escaping.
//

static void
print_xml_string(cups_file_t *outfile,	// I - Test data
		 const char  *element,	// I - Element name or NULL
		 const char  *s)	// I - String to print
{
  if (element)
    cupsFilePrintf(outfile, "<%s>", element);

  while (*s)
  {
    if (*s == '&')
    {
      cupsFilePuts(outfile, "&amp;");
    }
    else if (*s == '<')
    {
      cupsFilePuts(outfile, "&lt;");
    }
    else if (*s == '>')
    {
      cupsFilePuts(outfile, "&gt;");
    }
    else if ((*s & 0xe0) == 0xc0)
    {
      // Validate UTF-8 two-byte sequence...
      if ((s[1] & 0xc0) != 0x80)
      {
        cupsFilePutChar(outfile, '?');
        s ++;
      }
      else
      {
        cupsFilePutChar(outfile, *s++);
        cupsFilePutChar(outfile, *s);
      }
    }
    else if ((*s & 0xf0) == 0xe0)
    {
      // Validate UTF-8 three-byte sequence...
      if ((s[1] & 0xc0) != 0x80 || (s[2] & 0xc0) != 0x80)
      {
        cupsFilePutChar(outfile, '?');
        s += 2;
      }
      else
      {
        cupsFilePutChar(outfile, *s++);
        cupsFilePutChar(outfile, *s++);
        cupsFilePutChar(outfile, *s);
      }
    }
    else if ((*s & 0xf8) == 0xf0)
    {
      // Validate UTF-8 four-byte sequence...
      if ((s[1] & 0xc0) != 0x80 || (s[2] & 0xc0) != 0x80 || (s[3] & 0xc0) != 0x80)
      {
        cupsFilePutChar(outfile, '?');
        s += 3;
      }
      else
      {
        cupsFilePutChar(outfile, *s++);
        cupsFilePutChar(outfile, *s++);
        cupsFilePutChar(outfile, *s++);
        cupsFilePutChar(outfile, *s);
      }
    }
    else if ((*s & 0x80) || (*s < ' ' && !isspace(*s & 255)))
    {
      // Invalid control character...
      cupsFilePutChar(outfile, '?');
    }
    else
    {
      cupsFilePutChar(outfile, *s);
    }

    s ++;
  }

  if (element)
    cupsFilePrintf(outfile, "</%s>\n", element);
}


//
// 'print_xml_trailer()' - Print the XML trailer with success/fail value.
//

static void
print_xml_trailer(
    ipptool_test_t *data,		// I - Test data
    int              success,		// I - 1 on success, 0 on failure
    const char       *message)		// I - Error message or NULL
{
  if (data->xml_header)
  {
    cupsFilePuts(data->outfile, "</array>\n");
    cupsFilePuts(data->outfile, "<key>Successful</key>\n");
    cupsFilePuts(data->outfile, success ? "<true />\n" : "<false />\n");
    if (message)
    {
      cupsFilePuts(data->outfile, "<key>ErrorMessage</key>\n");
      print_xml_string(data->outfile, "string", message);
    }
    cupsFilePuts(data->outfile, "</dict>\n");
    cupsFilePuts(data->outfile, "</plist>\n");

    data->xml_header = 0;
  }
}


#ifndef _WIN32
//
// 'sigterm_handler()' - Handle SIGINT and SIGTERM.
//

static void
sigterm_handler(int sig)		// I - Signal number (unused)
{
  (void)sig;

  Cancel = true;

  signal(SIGINT, SIG_DFL);
  signal(SIGTERM, SIG_DFL);
}
#endif // !_WIN32


//
// 'timeout_cb()' - Handle HTTP timeouts.
//

static int				// O - 1 to continue, 0 to cancel
timeout_cb(http_t *http,		// I - Connection to server
           void   *user_data)		// I - User data (unused)
{
  int		buffered = 0;		// Bytes buffered but not yet sent


  (void)user_data;

  // If the socket still have data waiting to be sent to the printer (as can
  // happen if the printer runs out of paper), continue to wait until the output
  // buffer is empty...
#ifdef SO_NWRITE			// macOS and some versions of Linux
  socklen_t len = sizeof(buffered);	// Size of return value

  if (getsockopt(httpGetFd(http), SOL_SOCKET, SO_NWRITE, &buffered, &len))
    buffered = 0;

#elif defined(SIOCOUTQ)			// Others except Windows
  if (ioctl(httpGetFd(http), SIOCOUTQ, &buffered))
    buffered = 0;

#else					// Windows (not possible)
  (void)http;
#endif // SO_NWRITE

  return (buffered > 0);
}


//
// 'token_cb()' - Parse test file-specific tokens and run tests.
//

static bool				// O - `true` to continue, `false` to stop
token_cb(ipp_file_t     *f,		// I - IPP file data
         ipptool_test_t *data,		// I - Test data
         const char     *token)		// I - Current token
{
  char	name[1024],			// Name string
	temp[1024],			// Temporary string
	value[1024],			// Value string
	*ptr;				// Pointer into value


  if (getenv("IPPTOOL_DEBUG"))
    fprintf(stderr, "ipptool: token='%s'\n", token);

  if (ippFileGetAttributes(f))
  {
    // Parse until we see a close brace...
    if (_cups_strcasecmp(token, "COUNT") &&
	_cups_strcasecmp(token, "DEFINE-MATCH") &&
	_cups_strcasecmp(token, "DEFINE-NO-MATCH") &&
	_cups_strcasecmp(token, "DEFINE-VALUE") &&
	_cups_strcasecmp(token, "DISPLAY-MATCH") &&
	_cups_strcasecmp(token, "IF-DEFINED") &&
	_cups_strcasecmp(token, "IF-NOT-DEFINED") &&
	_cups_strcasecmp(token, "IN-GROUP") &&
	_cups_strcasecmp(token, "OF-TYPE") &&
	_cups_strcasecmp(token, "REPEAT-LIMIT") &&
	_cups_strcasecmp(token, "REPEAT-MATCH") &&
	_cups_strcasecmp(token, "REPEAT-NO-MATCH") &&
	_cups_strcasecmp(token, "SAME-COUNT-AS") &&
	_cups_strcasecmp(token, "SAVE-ALL-CONTENT") &&
	_cups_strcasecmp(token, "SAVE-CONTENT") &&
	_cups_strcasecmp(token, "WITH-ALL-CONTENT") &&
	_cups_strcasecmp(token, "WITH-ALL-MIME-TYPES") &&
	_cups_strcasecmp(token, "WITH-ALL-VALUES") &&
	_cups_strcasecmp(token, "WITH-ALL-VALUES-FROM") &&
	_cups_strcasecmp(token, "WITH-ALL-HOSTNAMES") &&
	_cups_strcasecmp(token, "WITH-ALL-RESOURCES") &&
	_cups_strcasecmp(token, "WITH-ALL-SCHEMES") &&
	_cups_strcasecmp(token, "WITH-CONTENT") &&
	_cups_strcasecmp(token, "WITH-DISTINCT-VALUES") &&
	_cups_strcasecmp(token, "WITH-HOSTNAME") &&
	_cups_strcasecmp(token, "WITH-MIME-TYPES") &&
	_cups_strcasecmp(token, "WITH-RESOURCE") &&
	_cups_strcasecmp(token, "WITH-SCHEME") &&
	_cups_strcasecmp(token, "WITH-VALUE") &&
	_cups_strcasecmp(token, "WITH-VALUE-FROM"))
      data->last_expect = NULL;

    if (_cups_strcasecmp(token, "DEFINE-MATCH") &&
	_cups_strcasecmp(token, "DEFINE-NO-MATCH") &&
	_cups_strcasecmp(token, "IF-DEFINED") &&
	_cups_strcasecmp(token, "IF-NOT-DEFINED") &&
	_cups_strcasecmp(token, "REPEAT-LIMIT") &&
	_cups_strcasecmp(token, "REPEAT-MATCH") &&
	_cups_strcasecmp(token, "REPEAT-NO-MATCH"))
      data->last_status = NULL;

    if (!strcmp(token, "}"))
    {
      return (do_test(f, data));
    }
    else if (!strcmp(token, "GENERATE-FILE"))
    {
      if (data->generate_params)
      {
	print_fatal_error(data, "Extra GENERATE-FILE seen on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
      else if (data->file[0])
      {
	print_fatal_error(data, "Cannot use GENERATE-FILE on line %d of '%s' with FILE.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      return (parse_generate_file(f, data));
    }
    else if (!strcmp(token, "MONITOR-PRINTER-STATE"))
    {
      if (data->monitor_uri)
      {
	print_fatal_error(data, "Extra MONITOR-PRINTER-STATE seen on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      return (parse_monitor_printer_state(f, data));
    }
    else if (!strcmp(token, "COMPRESSION"))
    {
      // COMPRESSION none
      // COMPRESSION deflate
      // COMPRESSION gzip
      if (ippFileReadToken(f, temp, sizeof(temp)))
      {
	ippFileExpandVars(f, data->compression, temp, sizeof(data->compression));
	if (strcmp(data->compression, "none") && strcmp(data->compression, "deflate") && strcmp(data->compression, "gzip"))
	{
	  print_fatal_error(data, "Unsupported COMPRESSION value \"%s\" on line %d of '%s'.", data->compression, ippFileGetLineNumber(f), ippFileGetFilename(f));
	  return (false);
	}

	if (!strcmp(data->compression, "none"))
	  data->compression[0] = '\0';
      }
      else
      {
	print_fatal_error(data, "Missing COMPRESSION value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!strcmp(token, "DEFINE"))
    {
      // DEFINE name value
      if (ippFileReadToken(f, name, sizeof(name)) && ippFileReadToken(f, temp, sizeof(temp)))
      {
	ippFileExpandVars(f, value, temp, sizeof(value));
	ippFileSetVar(f, name, value);
      }
      else
      {
	print_fatal_error(data, "Missing DEFINE name and/or value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!strcmp(token, "IGNORE-ERRORS"))
    {
      // IGNORE-ERRORS yes
      // IGNORE-ERRORS no
      if (ippFileReadToken(f, temp, sizeof(temp)) && (!_cups_strcasecmp(temp, "yes") || !_cups_strcasecmp(temp, "no")))
      {
	data->ignore_errors = !_cups_strcasecmp(temp, "yes");
      }
      else
      {
	print_fatal_error(data, "Missing IGNORE-ERRORS value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "NAME"))
    {
      // Name of test...
      if (ippFileReadToken(f, temp, sizeof(temp)))
      {
        ippFileExpandVars(f, data->name, temp, sizeof(data->name));
      }
      else
      {
	print_fatal_error(data, "Missing NAME string on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "PAUSE"))
    {
      // Pause with a message...
      if (ippFileReadToken(f, temp, sizeof(temp)))
      {
        cupsCopyString(data->pause, temp, sizeof(data->pause));
      }
      else
      {
	print_fatal_error(data, "Missing PAUSE message on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!strcmp(token, "REQUEST-ID"))
    {
      // REQUEST-ID #
      // REQUEST-ID random
      if (ippFileReadToken(f, temp, sizeof(temp)))
      {
	if (isdigit(temp[0] & 255))
	{
	  data->request_id = atoi(temp) - 1;
	}
	else if (!_cups_strcasecmp(temp, "random"))
	{
	  data->request_id = (cupsGetRand() % 1000) * 137;
	}
	else
	{
	  print_fatal_error(data, "Bad REQUEST-ID value \"%s\" on line %d of '%s'.", temp, ippFileGetLineNumber(f), ippFileGetFilename(f));
	  return (false);
	}
      }
      else
      {
	print_fatal_error(data, "Missing REQUEST-ID value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!strcmp(token, "PASS-IF-DEFINED"))
    {
      // PASS-IF-DEFINED variable
      if (ippFileReadToken(f, name, sizeof(name)))
      {
	if (ippFileGetVar(f, name))
	  data->pass_test = 1;
      }
      else
      {
	print_fatal_error(data, "Missing PASS-IF-DEFINED value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!strcmp(token, "PASS-IF-NOT-DEFINED"))
    {
      // PASS-IF-NOT-DEFINED variable
      if (ippFileReadToken(f, name, sizeof(name)))
      {
	if (!ippFileGetVar(f, name))
	  data->pass_test = 1;
      }
      else
      {
	print_fatal_error(data, "Missing PASS-IF-NOT-DEFINED value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!strcmp(token, "SKIP-IF-DEFINED"))
    {
      // SKIP-IF-DEFINED variable
      if (ippFileReadToken(f, name, sizeof(name)))
      {
	if (ippFileGetVar(f, name) || getenv(name))
	  data->skip_test = true;
      }
      else
      {
	print_fatal_error(data, "Missing SKIP-IF-DEFINED value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!strcmp(token, "SKIP-IF-MISSING"))
    {
      // SKIP-IF-MISSING filename
      if (ippFileReadToken(f, temp, sizeof(temp)))
      {
        char filename[1024];		// Filename

	ippFileExpandVars(f, value, temp, sizeof(value));
	get_filename(ippFileGetFilename(f), filename, temp, sizeof(filename));

	if (access(filename, R_OK))
	  data->skip_test = true;
      }
      else
      {
	print_fatal_error(data, "Missing SKIP-IF-MISSING filename on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!strcmp(token, "SKIP-IF-NOT-DEFINED"))
    {
      // SKIP-IF-NOT-DEFINED variable
      if (ippFileReadToken(f, name, sizeof(name)))
      {
	if (!ippFileGetVar(f, name) && !getenv(name))
	  data->skip_test = true;
      }
      else
      {
	print_fatal_error(data, "Missing SKIP-IF-NOT-DEFINED value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!strcmp(token, "SKIP-PREVIOUS-ERROR"))
    {
      // SKIP-PREVIOUS-ERROR yes
      // SKIP-PREVIOUS-ERROR no
      if (ippFileReadToken(f, temp, sizeof(temp)) && (!_cups_strcasecmp(temp, "yes") || !_cups_strcasecmp(temp, "no")))
      {
	data->skip_previous = !_cups_strcasecmp(temp, "yes");
      }
      else
      {
	print_fatal_error(data, "Missing SKIP-PREVIOUS-ERROR value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!strcmp(token, "TEST-ID"))
    {
      // TEST-ID "string"
      if (ippFileReadToken(f, temp, sizeof(temp)))
      {
	ippFileExpandVars(f, data->test_id, temp, sizeof(data->test_id));
      }
      else
      {
	print_fatal_error(data, "Missing TEST-ID value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!strcmp(token, "TRANSFER"))
    {
      // TRANSFER auto
      // TRANSFER chunked
      // TRANSFER length
      if (ippFileReadToken(f, temp, sizeof(temp)))
      {
	if (!strcmp(temp, "auto"))
	{
	  data->transfer = IPPTOOL_TRANSFER_AUTO;
	}
	else if (!strcmp(temp, "chunked"))
	{
	  data->transfer = IPPTOOL_TRANSFER_CHUNKED;
	}
	else if (!strcmp(temp, "length"))
	{
	  data->transfer = IPPTOOL_TRANSFER_LENGTH;
	}
	else
	{
	  print_fatal_error(data, "Bad TRANSFER value \"%s\" on line %d of '%s'.", temp, ippFileGetLineNumber(f), ippFileGetFilename(f));
	  return (false);
	}
      }
      else
      {
	print_fatal_error(data, "Missing TRANSFER value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "VERSION"))
    {
      if (ippFileReadToken(f, temp, sizeof(temp)))
      {
	if (!strcmp(temp, "0.0"))
	{
	  data->version = 0;
	}
	else if (!strcmp(temp, "1.0"))
	{
	  data->version = 10;
	}
	else if (!strcmp(temp, "1.1"))
	{
	  data->version = 11;
	}
	else if (!strcmp(temp, "2.0"))
	{
	  data->version = 20;
	}
	else if (!strcmp(temp, "2.1"))
	{
	  data->version = 21;
	}
	else if (!strcmp(temp, "2.2"))
	{
	  data->version = 22;
	}
	else
	{
	  print_fatal_error(data, "Bad VERSION \"%s\" on line %d of '%s'.", temp, ippFileGetLineNumber(f), ippFileGetFilename(f));
	  return (false);
	}
      }
      else
      {
	print_fatal_error(data, "Missing VERSION number on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "RESOURCE"))
    {
      // Resource name...
      if (!ippFileReadToken(f, data->resource, sizeof(data->resource)))
      {
	print_fatal_error(data, "Missing RESOURCE path on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "OPERATION"))
    {
      // Operation...
      ipp_op_t	op;			// Operation code

      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing OPERATION code on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      ippFileExpandVars(f, value, temp, sizeof(value));

      if ((op = ippOpValue(value)) == (ipp_op_t)-1 && (op = (ipp_op_t)strtol(value, NULL, 0)) == 0)
      {
	print_fatal_error(data, "Bad OPERATION code \"%s\" on line %d of '%s'.", temp, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      data->op = op;
    }
    else if (!_cups_strcasecmp(token, "DELAY"))
    {
      // Delay before operation...
      double dval;                    // Delay value

      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing DELAY value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      ippFileExpandVars(f, value, temp, sizeof(value));

      if ((dval = _cupsStrScand(value, &ptr, localeconv())) < 0.0 || (*ptr && *ptr != ','))
      {
	print_fatal_error(data, "Bad DELAY value \"%s\" on line %d of '%s'.", value, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      data->delay = (useconds_t)(1000000.0 * dval);

      if (*ptr == ',')
      {
	if ((dval = _cupsStrScand(ptr + 1, &ptr, localeconv())) <= 0.0 || *ptr)
	{
	  print_fatal_error(data, "Bad DELAY value \"%s\" on line %d of '%s'.", value, ippFileGetLineNumber(f), ippFileGetFilename(f));
	  return (false);
	}

	data->repeat_interval = (useconds_t)(1000000.0 * dval);
      }
      else
      {
	data->repeat_interval = data->delay;
      }
    }
    else if (!_cups_strcasecmp(token, "FILE"))
    {
      // File...
      if (data->file[0])
      {
	print_fatal_error(data, "Extra FILE seen on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
      else if (data->generate_params)
      {
	print_fatal_error(data, "Cannot use FILE on line %d of '%s' with GENERATE-FILE.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing FILE filename on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      ippFileExpandVars(f, value, temp, sizeof(value));
      get_filename(ippFileGetFilename(f), data->file, value, sizeof(data->file));

      if (access(data->file, R_OK))
      {
	print_fatal_error(data, "Filename \"%s\" (mapped to \"%s\") on line %d of '%s' cannot be read.", value, data->file, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "STATUS"))
    {
      // Status...
      if (data->num_statuses >= (int)(sizeof(data->statuses) / sizeof(data->statuses[0])))
      {
	print_fatal_error(data, "Too many STATUS's on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing STATUS code on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      if ((data->statuses[data->num_statuses].status = ippErrorValue(temp)) == (ipp_status_t)-1 && (data->statuses[data->num_statuses].status = (ipp_status_t)strtol(temp, NULL, 0)) == 0)
      {
	print_fatal_error(data, "Bad STATUS code \"%s\" on line %d of '%s'.", temp, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      data->last_status = data->statuses + data->num_statuses;
      data->num_statuses ++;

      data->last_status->define_match    = NULL;
      data->last_status->define_no_match = NULL;
      data->last_status->if_defined      = NULL;
      data->last_status->if_not_defined  = NULL;
      data->last_status->repeat_limit    = 1000;
      data->last_status->repeat_match    = 0;
      data->last_status->repeat_no_match = 0;
    }
    else if (!_cups_strcasecmp(token, "EXPECT") || !_cups_strcasecmp(token, "EXPECT-ALL"))
    {
      // Expected attributes...
      int expect_all = !_cups_strcasecmp(token, "EXPECT-ALL");

      if (data->num_expects >= (int)(sizeof(data->expects) / sizeof(data->expects[0])))
      {
	print_fatal_error(data, "Too many EXPECT's on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      if (!ippFileReadToken(f, name, sizeof(name)))
      {
	print_fatal_error(data, "Missing EXPECT name on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      data->last_expect = data->expects + data->num_expects;
      data->num_expects ++;

      memset(data->last_expect, 0, sizeof(ipptool_expect_t));
      data->last_expect->repeat_limit = 1000;
      data->last_expect->expect_all   = expect_all;

      if (name[0] == '!')
      {
	data->last_expect->not_expect = 1;
	data->last_expect->name       = strdup(name + 1);
      }
      else if (name[0] == '?')
      {
	data->last_expect->optional = 1;
	data->last_expect->name     = strdup(name + 1);
      }
      else
	data->last_expect->name = strdup(name);
    }
    else if (!_cups_strcasecmp(token, "COUNT"))
    {
      int	count;			// Count value

      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing COUNT number on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      if ((count = strtol(temp, NULL, 10)) > INT_MAX)
      {
	print_fatal_error(data, "Bad COUNT \"%s\" on line %d of '%s'.", temp, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      if (data->last_expect)
      {
	data->last_expect->count = count;
      }
      else
      {
	print_fatal_error(data, "COUNT without a preceding EXPECT on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "DEFINE-MATCH"))
    {
      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing DEFINE-MATCH variable on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      if (data->last_expect)
      {
	data->last_expect->define_match = strdup(temp);
      }
      else if (data->last_status)
      {
	data->last_status->define_match = strdup(temp);
      }
      else
      {
	print_fatal_error(data, "DEFINE-MATCH without a preceding EXPECT or STATUS on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "DEFINE-NO-MATCH"))
    {
      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing DEFINE-NO-MATCH variable on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      if (data->last_expect)
      {
	data->last_expect->define_no_match = strdup(temp);
      }
      else if (data->last_status)
      {
	data->last_status->define_no_match = strdup(temp);
      }
      else
      {
	print_fatal_error(data, "DEFINE-NO-MATCH without a preceding EXPECT or STATUS on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "DEFINE-VALUE"))
    {
      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing DEFINE-VALUE variable on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      if (data->last_expect)
      {
	data->last_expect->define_value = strdup(temp);
      }
      else
      {
	print_fatal_error(data, "DEFINE-VALUE without a preceding EXPECT on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "DISPLAY-MATCH"))
    {
      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing DISPLAY-MATCH mesaage on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      if (data->last_expect)
      {
	data->last_expect->display_match = strdup(temp);
      }
      else
      {
	print_fatal_error(data, "DISPLAY-MATCH without a preceding EXPECT on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "OF-TYPE"))
    {
      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing OF-TYPE value tag(s) on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      if (data->last_expect)
      {
	data->last_expect->of_type = strdup(temp);
      }
      else
      {
	print_fatal_error(data, "OF-TYPE without a preceding EXPECT on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "IN-GROUP"))
    {
      ipp_tag_t	in_group;		// IN-GROUP value

      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing IN-GROUP group tag on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      if ((in_group = ippTagValue(temp)) == IPP_TAG_ZERO || in_group >= IPP_TAG_UNSUPPORTED_VALUE)
      {
	print_fatal_error(data, "Bad IN-GROUP group tag \"%s\" on line %d of '%s'.", temp, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
      else if (data->last_expect)
      {
	data->last_expect->in_group = in_group;
      }
      else
      {
	print_fatal_error(data, "IN-GROUP without a preceding EXPECT on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "REPEAT-LIMIT"))
    {
      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing REPEAT-LIMIT value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
      else if (atoi(temp) <= 0)
      {
	print_fatal_error(data, "Bad REPEAT-LIMIT value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      if (data->last_status)
      {
	data->last_status->repeat_limit = atoi(temp);
      }
      else if (data->last_expect)
      {
	data->last_expect->repeat_limit = atoi(temp);
      }
      else
      {
	print_fatal_error(data, "REPEAT-LIMIT without a preceding EXPECT or STATUS on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "REPEAT-MATCH"))
    {
      if (data->last_status)
      {
	data->last_status->repeat_match = 1;
      }
      else if (data->last_expect)
      {
	data->last_expect->repeat_match = 1;
      }
      else
      {
	print_fatal_error(data, "REPEAT-MATCH without a preceding EXPECT or STATUS on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "REPEAT-NO-MATCH"))
    {
      if (data->last_status)
      {
	data->last_status->repeat_no_match = 1;
      }
      else if (data->last_expect)
      {
	data->last_expect->repeat_no_match = 1;
      }
      else
      {
	print_fatal_error(data, "REPEAT-NO-MATCH without a preceding EXPECT or STATUS on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "SAME-COUNT-AS"))
    {
      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing SAME-COUNT-AS name on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      if (data->last_expect)
      {
	data->last_expect->same_count_as = strdup(temp);
      }
      else
      {
	print_fatal_error(data, "SAME-COUNT-AS without a preceding EXPECT on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "SAVE-ALL-CONTENT") || !_cups_strcasecmp(token, "SAVE-CONTENT"))
    {
      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing %s filespec on line %d of '%s'.", token, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      if (data->last_expect)
      {
	data->last_expect->save_filespec = strdup(temp);
      }
      else
      {
	print_fatal_error(data, "%s without a preceding EXPECT on line %d of '%s'.", token, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "IF-DEFINED"))
    {
      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing IF-DEFINED name on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      if (data->last_expect)
      {
	data->last_expect->if_defined = strdup(temp);
      }
      else if (data->last_status)
      {
	data->last_status->if_defined = strdup(temp);
      }
      else
      {
	print_fatal_error(data, "IF-DEFINED without a preceding EXPECT or STATUS on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "IF-NOT-DEFINED"))
    {
      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing IF-NOT-DEFINED name on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      if (data->last_expect)
      {
	data->last_expect->if_not_defined = strdup(temp);
      }
      else if (data->last_status)
      {
	data->last_status->if_not_defined = strdup(temp);
      }
      else
      {
	print_fatal_error(data, "IF-NOT-DEFINED without a preceding EXPECT or STATUS on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "WITH-ALL-CONTENT") || !_cups_strcasecmp(token, "WITH-CONTENT"))
    {
      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing %s condition on line %d of '%s'.", token, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      if (data->last_expect)
      {
        if (!_cups_strcasecmp(temp, "available"))
        {
          data->last_expect->with_content = IPPTOOL_CONTENT_AVAILABLE;
        }
        else if (!_cups_strcasecmp(temp, "valid"))
        {
          data->last_expect->with_content = IPPTOOL_CONTENT_VALID;
        }
        else if (!_cups_strcasecmp(temp, "valid-icon"))
        {
          data->last_expect->with_content = IPPTOOL_CONTENT_VALID_ICON;
        }
        else
        {
	  print_fatal_error(data, "Unsupported %s %s on line %d of '%s'.", token, temp, ippFileGetLineNumber(f), ippFileGetFilename(f));
	  return (false);
        }
      }
      else
      {
	print_fatal_error(data, "%s without a preceding EXPECT on line %d of '%s'.", token, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "WITH-ALL-MIME-TYPES") || !_cups_strcasecmp(token, "WITH-MIME-TYPES"))
    {
      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing %s MIME media type(s) on line %d of '%s'.", token, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      if (data->last_expect)
      {
	data->last_expect->with_mime_types = cupsArrayNewStrings(temp, ',');
      }
      else
      {
	print_fatal_error(data, "%s without a preceding EXPECT on line %d of '%s'.", token, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "WITH-DISTINCT-VALUES"))
    {
      if (data->last_expect)
      {
        data->last_expect->with_distinct = 1;
      }
      else
      {
	print_fatal_error(data, "%s without a preceding EXPECT on line %d of '%s'.", token, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "WITH-ALL-VALUES") ||
	     !_cups_strcasecmp(token, "WITH-ALL-HOSTNAMES") ||
	     !_cups_strcasecmp(token, "WITH-ALL-RESOURCES") ||
	     !_cups_strcasecmp(token, "WITH-ALL-SCHEMES") ||
	     !_cups_strcasecmp(token, "WITH-HOSTNAME") ||
	     !_cups_strcasecmp(token, "WITH-RESOURCE") ||
	     !_cups_strcasecmp(token, "WITH-SCHEME") ||
	     !_cups_strcasecmp(token, "WITH-VALUE"))
    {
      if (data->last_expect)
      {
	if (!_cups_strcasecmp(token, "WITH-ALL-HOSTNAMES") || !_cups_strcasecmp(token, "WITH-HOSTNAME"))
	  data->last_expect->with_flags = IPPTOOL_WITH_HOSTNAME;
	else if (!_cups_strcasecmp(token, "WITH-ALL-RESOURCES") || !_cups_strcasecmp(token, "WITH-RESOURCE"))
	  data->last_expect->with_flags = IPPTOOL_WITH_RESOURCE;
	else if (!_cups_strcasecmp(token, "WITH-ALL-SCHEMES") || !_cups_strcasecmp(token, "WITH-SCHEME"))
	  data->last_expect->with_flags = IPPTOOL_WITH_SCHEME;

	if (!_cups_strncasecmp(token, "WITH-ALL-", 9))
	  data->last_expect->with_flags |= IPPTOOL_WITH_ALL;
      }

      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing %s value on line %d of '%s'.", token, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      // Read additional comma-delimited values - needed since legacy test files
      // will have unquoted WITH-VALUE values with commas...
      ptr = temp + strlen(temp);

      for (;;)
      {
        ippFileSavePosition(f);

        ptr += strlen(ptr);

	if (!ippFileReadToken(f, ptr, (sizeof(temp) - (size_t)(ptr - temp))))
	  break;

        if (!strcmp(ptr, ","))
        {
          // Append a value...
	  ptr += strlen(ptr);

	  if (!ippFileReadToken(f, ptr, (sizeof(temp) - (size_t)(ptr - temp))))
	    break;
        }
        else
        {
          // Not another value, stop here...
          ippFileRestorePosition(f);

          *ptr = '\0';
          break;
	}
      }

      if (data->last_expect)
      {
        // Expand any variables in the value and then save it.
	ippFileExpandVars(f, value, temp, sizeof(value));

	ptr = value + strlen(value) - 1;

	if (value[0] == '/' && ptr > value && *ptr == '/')
	{
	  // WITH-VALUE is a POSIX extended regular expression.
	  data->last_expect->with_value = calloc(1, (size_t)(ptr - value));
	  data->last_expect->with_flags |= IPPTOOL_WITH_REGEX;

	  if (data->last_expect->with_value)
	    memcpy(data->last_expect->with_value, value + 1, (size_t)(ptr - value - 1));
	}
	else
	{
	  // WITH-VALUE is a literal value...
	  for (ptr = value; *ptr; ptr ++)
	  {
	    if (*ptr == '\\' && ptr[1])
	    {
	      // Remove \ from \foo...
	      _cups_strcpy(ptr, ptr + 1);
	    }
	  }

	  data->last_expect->with_value = strdup(value);
	  data->last_expect->with_flags |= IPPTOOL_WITH_LITERAL;
	}
      }
      else
      {
	print_fatal_error(data, "%s without a preceding EXPECT on line %d of '%s'.", token, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "WITH-ALL-VALUES-FROM") ||
	     !_cups_strcasecmp(token, "WITH-VALUE-FROM"))
    {
      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing %s value on line %d of '%s'.", token, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      if (data->last_expect)
      {
        // Expand any variables in the value and then save it.
	ippFileExpandVars(f, value, temp, sizeof(value));

	data->last_expect->with_value_from = strdup(value);
	data->last_expect->with_flags      = IPPTOOL_WITH_LITERAL;

	if (!_cups_strncasecmp(token, "WITH-ALL-", 9))
	  data->last_expect->with_flags |= IPPTOOL_WITH_ALL;
      }
      else
      {
	print_fatal_error(data, "%s without a preceding EXPECT on line %d of '%s'.", token, ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!_cups_strcasecmp(token, "DISPLAY"))
    {
      // Display attributes...
      if (data->num_displayed >= (int)(sizeof(data->displayed) / sizeof(data->displayed[0])))
      {
	print_fatal_error(data, "Too many DISPLAY's on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      if (!ippFileReadToken(f, temp, sizeof(temp)))
      {
	print_fatal_error(data, "Missing DISPLAY name on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }

      data->displayed[data->num_displayed] = strdup(temp);
      data->num_displayed ++;
    }
    else
    {
      print_fatal_error(data, "Unexpected token %s seen on line %d of '%s'.", token, ippFileGetLineNumber(f), ippFileGetFilename(f));
      return (false);
    }
  }
  else
  {
    // Scan for the start of a test (open brace)...
    if (!strcmp(token, "{"))
    {
      // Start new test...
      const char *resource;		// Resource path

      if (data->show_header)
      {
	if (data->output == IPPTOOL_OUTPUT_PLIST)
	  print_xml_header(data);

	if (data->output == IPPTOOL_OUTPUT_TEST || (data->output == IPPTOOL_OUTPUT_PLIST && data->outfile != cupsFileStdout()))
	  cupsFilePrintf(cupsFileStdout(), "\"%s\":\n", ippFileGetFilename(f));

	data->show_header = false;
      }

      if ((resource = ippFileGetVar(data->parent, "resource")) == NULL)
        resource = "/ipp/print";

      data->compression[0] = '\0';
      data->delay          = 0;
      data->num_expects    = 0;
      data->last_expect    = NULL;
      data->file[0]        = '\0';
      data->ignore_errors  = data->def_ignore_errors;
      cupsCopyString(data->name, ippFileGetFilename(f), sizeof(data->name));
      if ((ptr = strrchr(data->name, '.')) != NULL)
        *ptr = '\0';
      data->repeat_interval = 5000000;
      cupsCopyString(data->resource, resource, sizeof(data->resource));
      data->skip_previous = false;
      data->pass_test     = false;
      data->skip_test     = false;
      data->num_statuses  = 0;
      data->last_status   = NULL;
      data->test_id[0]    = '\0';
      data->transfer      = data->def_transfer;
      data->version       = data->def_version;

      free(data->monitor_uri);
      data->monitor_uri         = NULL;
      data->monitor_delay       = 0;
      data->monitor_interval    = 5000000;
      data->num_monitor_expects = 0;

      ippFileSetAttributes(f, ippNew());
      ippFileSetVar(f, "date-current", iso_date(ippTimeToDate(time(NULL))));
    }
    else if (!strcmp(token, "DEFINE"))
    {
      // DEFINE name value
      if (ippFileReadToken(f, name, sizeof(name)) && ippFileReadToken(f, temp, sizeof(temp)))
      {
        ippFileSetVar(f, "date-current", iso_date(ippTimeToDate(time(NULL))));
        ippFileExpandVars(f, value, temp, sizeof(value));
	ippFileSetVar(f, name, value);
      }
      else
      {
        print_fatal_error(data, "Missing DEFINE name and/or value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!strcmp(token, "DEFINE-DEFAULT"))
    {
      // DEFINE-DEFAULT name value
      if (ippFileReadToken(f, name, sizeof(name)) && ippFileReadToken(f, temp, sizeof(temp)))
      {
        if (!ippFileGetVar(f, name))
        {
          ippFileSetVar(f, "date-current", iso_date(ippTimeToDate(time(NULL))));
	  ippFileExpandVars(f, value, temp, sizeof(value));
	  ippFileSetVar(f, name, value);
	}
      }
      else
      {
        print_fatal_error(data, "Missing DEFINE-DEFAULT name and/or value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
        return (false);
      }
    }
    else if (!strcmp(token, "FILE-ID"))
    {
      // FILE-ID "string"
      if (ippFileReadToken(f, temp, sizeof(temp)))
      {
        ippFileSetVar(f, "date-current", iso_date(ippTimeToDate(time(NULL))));
        ippFileExpandVars(f, data->file_id, temp, sizeof(data->file_id));
      }
      else
      {
        print_fatal_error(data, "Missing FILE-ID value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
        return (false);
      }
    }
    else if (!strcmp(token, "IGNORE-ERRORS"))
    {
      // IGNORE-ERRORS yes
      // IGNORE-ERRORS no
      if (ippFileReadToken(f, temp, sizeof(temp)) && (!_cups_strcasecmp(temp, "yes") || !_cups_strcasecmp(temp, "no")))
      {
        data->def_ignore_errors = !_cups_strcasecmp(temp, "yes");
      }
      else
      {
        print_fatal_error(data, "Missing IGNORE-ERRORS value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
        return (false);
      }
    }
    else if (!strcmp(token, "INCLUDE"))
    {
      // INCLUDE "filename"
      // INCLUDE <filename>
      if (ippFileReadToken(f, temp, sizeof(temp)))
      {
        // Map the filename to and then run the tests...
        ipptool_test_t	inc_data;	// Data for included file
        bool		inc_pass;	// Include file passed?
        char		filename[1024];	// Mapped filename

        memcpy(&inc_data, data, sizeof(inc_data));
        inc_data.test_count  = 0;
        inc_data.pass_count  = 0;
        inc_data.fail_count  = 0;
        inc_data.skip_count  = 0;
        inc_data.http        = NULL;
	inc_data.pass        = true;
	inc_data.prev_pass   = true;
	inc_data.show_header = true;

        inc_pass = do_tests(get_filename(ippFileGetFilename(f), filename, temp, sizeof(filename)), &inc_data);

        data->test_count += inc_data.test_count;
        data->pass_count += inc_data.pass_count;
        data->fail_count += inc_data.fail_count;
        data->skip_count += inc_data.skip_count;

        if (!inc_pass && data->stop_after_include_error)
        {
          data->pass = data->prev_pass = false;
	  return (false);
	}
      }
      else
      {
        print_fatal_error(data, "Missing INCLUDE filename on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
        return (false);
      }

      data->show_header = true;
    }
    else if (!strcmp(token, "INCLUDE-IF-DEFINED"))
    {
      // INCLUDE-IF-DEFINED name "filename"
      // INCLUDE-IF-DEFINED name <filename>
      if (ippFileReadToken(f, name, sizeof(name)) && ippFileReadToken(f, temp, sizeof(temp)))
      {
        // Map the filename to and then run the tests...
        ipptool_test_t	inc_data;	// Data for included file
        bool		inc_pass;	// Include file passed?
        char		filename[1024];	// Mapped filename

        memcpy(&inc_data, data, sizeof(inc_data));
        inc_data.test_count  = 0;
        inc_data.pass_count  = 0;
        inc_data.fail_count  = 0;
        inc_data.skip_count  = 0;
        inc_data.http        = NULL;
	inc_data.pass        = true;
	inc_data.prev_pass   = true;
	inc_data.show_header = true;

        inc_pass = do_tests(get_filename(ippFileGetFilename(f), filename, temp, sizeof(filename)), &inc_data);

        data->test_count += inc_data.test_count;
        data->pass_count += inc_data.pass_count;
        data->fail_count += inc_data.fail_count;
        data->skip_count += inc_data.skip_count;

        if (!inc_pass && data->stop_after_include_error)
        {
          data->pass = data->prev_pass = false;
	  return (false);
	}
      }
      else
      {
        print_fatal_error(data, "Missing INCLUDE-IF-DEFINED name or filename on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
        return (false);
      }

      data->show_header = true;
    }
    else if (!strcmp(token, "INCLUDE-IF-NOT-DEFINED"))
    {
      // INCLUDE-IF-NOT-DEFINED name "filename"
      // INCLUDE-IF-NOT-DEFINED name <filename>
      if (ippFileReadToken(f, name, sizeof(name)) && ippFileReadToken(f, temp, sizeof(temp)))
      {
        // Map the filename to and then run the tests...
        ipptool_test_t	inc_data;	// Data for included file
        bool		inc_pass;	// Include file passed?
        char		filename[1024];	// Mapped filename

        memcpy(&inc_data, data, sizeof(inc_data));
        inc_data.test_count  = 0;
        inc_data.pass_count  = 0;
        inc_data.fail_count  = 0;
        inc_data.skip_count  = 0;
        inc_data.http        = NULL;
	inc_data.pass        = true;
	inc_data.prev_pass   = true;
	inc_data.show_header = true;

        inc_pass = do_tests(get_filename(ippFileGetFilename(f), filename, temp, sizeof(filename)), &inc_data);

        data->test_count += inc_data.test_count;
        data->pass_count += inc_data.pass_count;
        data->fail_count += inc_data.fail_count;
        data->skip_count += inc_data.skip_count;

        if (!inc_pass && data->stop_after_include_error)
        {
          data->pass = data->prev_pass = false;
	  return (false);
	}
      }
      else
      {
        print_fatal_error(data, "Missing INCLUDE-IF-NOT-DEFINED name or filename on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
        return (false);
      }

      data->show_header = true;
    }
    else if (!strcmp(token, "SKIP-IF-DEFINED"))
    {
      // SKIP-IF-DEFINED variable
      if (ippFileReadToken(f, name, sizeof(name)))
      {
        if (ippFileGetVar(f, name) || getenv(name))
          data->skip_test = true;
      }
      else
      {
        print_fatal_error(data, "Missing SKIP-IF-DEFINED variable on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
        return (false);
      }
    }
    else if (!strcmp(token, "SKIP-IF-NOT-DEFINED"))
    {
      // SKIP-IF-NOT-DEFINED variable
      if (ippFileReadToken(f, name, sizeof(name)))
      {
        if (!ippFileGetVar(f, name) && !getenv(name))
          data->skip_test = true;
      }
      else
      {
        print_fatal_error(data, "Missing SKIP-IF-NOT-DEFINED variable on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
        return (false);
      }
    }
    else if (!strcmp(token, "STOP-AFTER-INCLUDE-ERROR"))
    {
      // STOP-AFTER-INCLUDE-ERROR yes
      // STOP-AFTER-INCLUDE-ERROR no
      if (ippFileReadToken(f, temp, sizeof(temp)) && (!_cups_strcasecmp(temp, "yes") || !_cups_strcasecmp(temp, "no")))
      {
        data->stop_after_include_error = !_cups_strcasecmp(temp, "yes");
      }
      else
      {
        print_fatal_error(data, "Missing STOP-AFTER-INCLUDE-ERROR value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
        return (false);
      }
    }
    else if (!strcmp(token, "TRANSFER"))
    {
      // TRANSFER auto
      // TRANSFER chunked
      // TRANSFER length
      if (ippFileReadToken(f, temp, sizeof(temp)))
      {
        if (!strcmp(temp, "auto"))
	  data->def_transfer = IPPTOOL_TRANSFER_AUTO;
	else if (!strcmp(temp, "chunked"))
	  data->def_transfer = IPPTOOL_TRANSFER_CHUNKED;
	else if (!strcmp(temp, "length"))
	  data->def_transfer = IPPTOOL_TRANSFER_LENGTH;
	else
	{
	  print_fatal_error(data, "Bad TRANSFER value \"%s\" on line %d of '%s'.", temp, ippFileGetLineNumber(f), ippFileGetFilename(f));
	  return (false);
	}
      }
      else
      {
        print_fatal_error(data, "Missing TRANSFER value on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
	return (false);
      }
    }
    else if (!strcmp(token, "VERSION"))
    {
      if (ippFileReadToken(f, temp, sizeof(temp)))
      {
        if (!strcmp(temp, "1.0"))
	  data->def_version = 10;
	else if (!strcmp(temp, "1.1"))
	  data->def_version = 11;
	else if (!strcmp(temp, "2.0"))
	  data->def_version = 20;
	else if (!strcmp(temp, "2.1"))
	  data->def_version = 21;
	else if (!strcmp(temp, "2.2"))
	  data->def_version = 22;
	else
	{
	  print_fatal_error(data, "Bad VERSION \"%s\" on line %d of '%s'.", temp, ippFileGetLineNumber(f), ippFileGetFilename(f));
	  return (false);
	}
      }
      else
      {
        print_fatal_error(data, "Missing VERSION number on line %d of '%s'.", ippFileGetLineNumber(f), ippFileGetFilename(f));
        return (false);
      }
    }
    else
    {
      print_fatal_error(data, "Unexpected token %s seen on line %d of '%s'.", token, ippFileGetLineNumber(f), ippFileGetFilename(f));
      return (false);
    }
  }

  return (true);
}


//
// 'usage()' - Show program usage.
//

static void
usage(void)
{
  _cupsLangPuts(stderr, _("Usage: ipptool [options] URI filename [ ... filenameN ]"));
  _cupsLangPuts(stderr, _("Options:"));
  _cupsLangPuts(stderr, _("--ippserver filename    Produce ippserver attribute file"));
  _cupsLangPuts(stderr, _("--stop-after-include-error\n"
                          "                        Stop tests after a failed INCLUDE"));
  _cupsLangPuts(stderr, _("--version               Show version"));
  _cupsLangPuts(stderr, _("-4                      Connect using IPv4"));
  _cupsLangPuts(stderr, _("-6                      Connect using IPv6"));
  _cupsLangPuts(stderr, _("-C                      Send requests using chunking (default)"));
  _cupsLangPuts(stderr, _("-E                      Test with encryption using HTTP Upgrade to TLS"));
  _cupsLangPuts(stderr, _("-I                      Ignore errors"));
  _cupsLangPuts(stderr, _("-L                      Send requests using content-length"));
  _cupsLangPuts(stderr, _("-P filename.plist       Produce XML plist to a file and test report to standard output"));
  _cupsLangPuts(stderr, _("-R                      Repeat tests on server-error-busy"));
  _cupsLangPuts(stderr, _("-S                      Test with encryption using HTTPS"));
  _cupsLangPuts(stderr, _("-T seconds              Set the receive/send timeout in seconds"));
  _cupsLangPuts(stderr, _("-V version              Set default IPP version"));
  _cupsLangPuts(stderr, _("-X                      Produce XML plist instead of plain text"));
  _cupsLangPuts(stderr, _("-c                      Produce CSV output"));
  _cupsLangPuts(stderr, _("-d name=value           Set named variable to value"));
  _cupsLangPuts(stderr, _("-f filename             Set default request filename"));
  _cupsLangPuts(stderr, _("-h                      Validate HTTP response headers"));
  _cupsLangPuts(stderr, _("-i seconds              Repeat the last file with the given time interval"));
  _cupsLangPuts(stderr, _("-l                      Produce plain text output"));
  _cupsLangPuts(stderr, _("-n count                Repeat the last file the given number of times"));
  _cupsLangPuts(stderr, _("-q                      Run silently"));
  _cupsLangPuts(stderr, _("-t                      Produce a test report"));
  _cupsLangPuts(stderr, _("-v                      Be verbose"));

  exit(1);
}


//
// 'valid_image()' - Validate an image.
//
// Supports JPEG and PNG images.
//

static bool				// O - `true` if valid, `false` if not
valid_image(const char *filename,	// I - Image filename
            int        *width,		// O - Width in columns
            int        *height,		// O - Height in lines
            int        *depth)		// O - Number of color planes
{
  bool		ret = true;		// Return value
  int		fd;			// File descriptor
  unsigned char	buffer[16384],		// Read buffer
		*bufptr,		// Pointer into buffer
	        *bufend;		// End of buffer
  ssize_t	bytes;			// Number of bytes read


  // Initialize things...
  *width = *height = *depth = 0;

  // Try opening the file and reading from it...
  if ((fd = open(filename, O_RDONLY | O_BINARY)) < 0)
  {
    // Unable to open...
    ret = false;
  }
  else if ((bytes = read(fd, buffer, sizeof(buffer))) < 16)
  {
    // Unable to read...
    ret = false;
  }
  else if (!memcmp(buffer, "\211PNG\015\012\032\012\000\000\000\015IHDR", 16) && bytes > 25)
  {
    // PNG image...
    *width  = (int)((buffer[16] << 24) | (buffer[17] << 16) | (buffer[18] << 8) | buffer[19]);
    *height = (int)((buffer[20] << 24) | (buffer[21] << 16) | (buffer[22] << 8) | buffer[23]);
    *depth  = ((buffer[25] & 3) == 0 ? 1 : 3) + ((buffer[25] & 4) ? 1 : 0);
  }
  else if (!memcmp(buffer, "\377\330\377", 3))
  {
    // JPEG image...
    size_t	length;			// Length of chunk

    for (bufptr = buffer + 2, bufend = buffer + bytes; bufptr < bufend;)
    {
      if (*bufptr == 0xff)
      {
	bufptr ++;

	if (bufptr >= bufend)
	{
	  // If we are at the end of the current buffer, re-fill and continue...
	  if ((bytes = read(fd, buffer, sizeof(buffer))) <= 0)
	    break;

	  bufptr = buffer;
	  bufend = buffer + bytes;
	}

	if (*bufptr == 0xff)
	  continue;

	if ((bufptr + 16) >= bufend)
	{
	  // Read more of the marker...
	  bytes = bufend - bufptr;

	  memmove(buffer, bufptr, (size_t)bytes);
	  bufptr = buffer;
	  bufend = buffer + bytes;

	  if ((bytes = read(fd, bufend, sizeof(buffer) - (size_t)bytes)) <= 0)
	    break;

	  bufend += bytes;
	}

	length = (size_t)((bufptr[1] << 8) | bufptr[2]);

	if ((*bufptr >= 0xc0 && *bufptr <= 0xc3) || (*bufptr >= 0xc5 && *bufptr <= 0xc7) || (*bufptr >= 0xc9 && *bufptr <= 0xcb) || (*bufptr >= 0xcd && *bufptr <= 0xcf))
	{
	  // SOFn marker, look for dimensions...
	  if (bufptr[3] != 8)
	  {
	    ret = false;
	    break;
	  }

	  *width  = (int)((bufptr[6] << 8) | bufptr[7]);
	  *height = (int)((bufptr[4] << 8) | bufptr[5]);
	  *depth  = (int)bufptr[8];
	  break;
	}

	// Skip past this marker...
	bufptr ++;
	bytes = bufend - bufptr;

	while (length >= (size_t)bytes)
	{
	  length -= (size_t)bytes;

	  if ((bytes = read(fd, buffer, sizeof(buffer))) <= 0)
	    break;

	  bufptr = buffer;
	  bufend = buffer + bytes;
	}

	if (length > (size_t)bytes)
	  break;

	bufptr += length;
      }
    }

    if (*width == 0 || *height == 0 || (*depth != 1 && *depth != 3))
      ret = false;
  }
  else
  {
    // Something we don't recognize...
    ret = false;
  }

  if (fd >= 0)
    close(fd);

  return (ret);
}


//
// 'with_content()' - Verify that URIs meet content/MIME media type requirements
//                    and save as needed.
//

static bool				// O - `true` if valid, `false` otherwise
with_content(
    cups_array_t      *errors,		// I - Array of errors
    ipp_attribute_t   *attr,		// I - Attribute
    ipptool_content_t content,		// I - Content validation rule
    cups_array_t      *mime_types,	// I - Comma-delimited list of MIME media types
    const char        *filespec)	// I - Output filename specification
{
  bool		ret = true;		// Return value
  int		i,			// Looping var
		count;			// Number of values
  const char	*uri;			// URI value
  char		scheme[256],		// Scheme
		userpass[256],		// Username:password (not used)
		host[256],		// Hostname
		resource[256],		// Resource path
		*resptr;		// Pointer into resource
  int		port;			// Port number
  http_encryption_t encryption;		// Encryption  mode
  http_uri_status_t uri_status;		// URI decoding status
  http_t	*http;			// Connection to server
  http_status_t	status;			// Request status
  const char	*content_type;		// Content-Type header


  for (i = 0, count = ippGetCount(attr); i < count; i ++)
  {
    uri = ippGetString(attr, i, NULL);

    if ((uri_status = httpSeparateURI(HTTP_URI_CODING_ALL, uri, scheme, sizeof(scheme), userpass, sizeof(userpass), host, sizeof(host), &port, resource, sizeof(resource))) < HTTP_URI_STATUS_OK)
    {
      add_stringf(errors, "Bad URI value '%s': %s", uri, httpURIStatusString(uri_status));
      ret = false;
      continue;
    }

    if ((resptr = strchr(resource, '#')) != NULL)
      *resptr = '\0';			// Strip HTML target ("...#target")

    if (strcmp(scheme, "http") && strcmp(scheme, "https") && strcmp(scheme, "ipp") && strcmp(scheme, "ipps"))
    {
      add_stringf(errors, "Unsupported URI scheme for '%s'.", uri);
      ret = false;
      continue;
    }

    encryption = (!strcmp(scheme, "https") || !strcmp(scheme, "ipps") || port == 443) ? HTTP_ENCRYPTION_ALWAYS : HTTP_ENCRYPTION_IF_REQUESTED;

    if ((http = httpConnect2(host, port, NULL, AF_UNSPEC, encryption, 1, 30000, NULL)) == NULL)
    {
      add_stringf(errors, "Unable to connect to '%s' on port %d: %s", host, port, cupsGetErrorString());
      ret = false;
      continue;
    }

    if (content == IPPTOOL_CONTENT_AVAILABLE)
    {
      if (!httpWriteRequest(http, "HEAD", resource))
      {
	add_stringf(errors, "Unable to send HEAD request to '%s': %s", uri, cupsGetErrorString());
	ret = false;
	goto http_done;
      }

      while ((status = httpUpdate(http)) == HTTP_STATUS_CONTINUE)
      {
        // Do nothing
      }

      if (status != HTTP_STATUS_OK)
      {
        add_stringf(errors, "Got unexpected status %d for HEAD request to '%s'.", (int)status, uri);
	ret = false;
	goto http_done;
      }

      content_type = httpGetField(http, HTTP_FIELD_CONTENT_TYPE);
      if ((!strcmp(scheme, "ipp") || !strcmp(scheme, "ipps")) != !_cups_strcasecmp(content_type, "application/ipp") || (mime_types && !cupsArrayFind(mime_types, (void *)content_type)))
      {
        add_stringf(errors, "Got unexpected Content-Type '%s' for HEAD request to '%s'.", content_type, uri);
	ret = false;
	goto http_done;
      }
    }
    else if (!strcmp(scheme, "http") || !strcmp(scheme, "https"))
    {
      // Check HTTP resource with a GET...
      char	filename[1024];		// Temporary filename
      int	fd;			// Temporary file
      struct stat fileinfo;		// Temporary file information
      int	width,			// Image width
		height,			// Image height
		depth;			// Image color depth

      if ((fd = create_file(filespec, resource, i + 1, filename, sizeof(filename))) < 0)
      {
        add_stringf(errors, "Unable to create temporary file for WITH-CONTENT: %s", strerror(errno));
	ret = false;
	goto http_done;
      }

      status = cupsGetFd(http, resource, fd);
      if (fstat(fd, &fileinfo))
        memset(&fileinfo, 0, sizeof(fileinfo));
      close(fd);

      if (status != HTTP_STATUS_OK)
      {
        add_stringf(errors, "Got unexpected status %d for GET request to '%s'.", (int)status, uri);
        ret = false;
        goto get_done;
      }

      content_type = httpGetField(http, HTTP_FIELD_CONTENT_TYPE);

      if (mime_types && !cupsArrayFind(mime_types, (void *)content_type))
      {
        add_stringf(errors, "Got unexpected Content-Type '%s' for GET request to '%s'.", content_type, uri);
        ret = false;
        goto get_done;
      }

      if (content == IPPTOOL_CONTENT_VALID_ICON)
      {
        if (_cups_strcasecmp(content_type, "image/png"))
        {
	  add_stringf(errors, "Got unexpected Content-Type '%s' for GET request to '%s'.", content_type, uri);
	  ret = false;
	  goto get_done;
        }

        if (!valid_image(filename, &width, &height, &depth))
        {
	  add_stringf(errors, "Unable to load image '%s'.", uri);
	  ret = false;
	  goto get_done;
        }
        else if (width != height || (width != 48 && width != 128 && width != 512))
        {
          add_stringf(errors, "Image '%s' has bad dimensions %dx%d.", uri, width, height);
	  ret = false;
	  goto get_done;
        }
        else if (depth & 1)
        {
          add_stringf(errors, "Image '%s' doesn't have transparency information.", uri);
	  ret = false;
	  goto get_done;
        }
      }
      else if (!_cups_strcasecmp(content_type, "image/jpeg") || !_cups_strcasecmp(content_type, "image/png"))
      {
        // Validate image content
        if (!valid_image(filename, &width, &height, &depth))
        {
	  add_stringf(errors, "Unable to open image '%s'.", uri);
	  ret = false;
	  goto get_done;
        }
      }
      else if (!_cups_strcasecmp(content_type, "application/ipp"))
      {
        ipp_t	*ipp = ippNew();	// IPP message

        if ((fd = open(filename, O_RDONLY | O_BINARY)) < 0)
        {
          add_stringf(errors, "Unable to open '%s': %s", uri, strerror(errno));
          ippDelete(ipp);
          ret = false;
          goto get_done;
        }
        else if (ippReadFile(fd, ipp) != IPP_STATE_DATA)
        {
          add_stringf(errors, "Unable to read '%s': %s", uri, cupsGetErrorString());
          ippDelete(ipp);
          close(fd);
          ret = false;
          goto get_done;
        }

	ippDelete(ipp);
	close(fd);
      }
      else if (!_cups_strcasecmp(content_type, "application/pdf") || !_cups_strcasecmp(content_type, "application/vnd.iccprofile") || !_cups_strcasecmp(content_type, "text/css") || !_cups_strcasecmp(content_type, "text/html") || !_cups_strncasecmp(content_type, "text/html;", 10) || !_cups_strcasecmp(content_type, "text/strings"))
      {
        // Just require these files to be non-empty for now, might add more checks in the future...
        if (fileinfo.st_size == 0)
        {
	  add_stringf(errors, "Empty resource '%s'.", uri);
	  ret = false;
	  goto get_done;
        }
      }
      else
      {
	add_stringf(errors, "Got unexpected Content-Type '%s' for GET request to '%s'.", content_type, uri);
	ret = false;
	goto get_done;
      }

      get_done:

      if (!filespec)
        unlink(filename);
    }
    else
    {
      // Check IPP resource...
      ipp_t	*request;		// IPP Get-Printer-Attributes request

      request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);

      ippDelete(cupsDoRequest(http, request, resource));

      if (cupsGetError() > IPP_STATUS_OK_EVENTS_COMPLETE)
      {
        add_stringf(errors, "Got unexpected status-code '%s' (%s) for Get-Printer-Attributes request to '%s'.", ippErrorString(cupsGetError()), cupsGetErrorString(), uri);
        ret = false;
      }
    }

    http_done:

    httpClose(http);
  }

  return (ret);
}


//
// 'with_distinct_values()' - Verify that an attribute contains unique values.
//

static bool				// O - `true` if distinct, `false` if duplicate
with_distinct_values(
    cups_array_t    *errors,		// I - Array of errors
    ipp_attribute_t *attr)		// I - Attribute to test
{
  bool		ret;			// Return value
  int		i,			// Looping var
		count;			// Number of values
  ipp_tag_t	value_tag;		// Value syntax
  const char	*value;			// Current value
  char		buffer[131072];		// Temporary buffer
  cups_array_t	*values;		// Array of values as strings


  // If there is only 1 value, it must be distinct
  if ((count = ippGetCount(attr)) == 1)
    return (true);

  // Only check integers, enums, rangeOfInteger, resolution, and nul-terminated
  // strings...
  switch (value_tag = ippGetValueTag(attr))
  {
    case IPP_TAG_INTEGER :
    case IPP_TAG_ENUM :
    case IPP_TAG_RANGE :
    case IPP_TAG_RESOLUTION :
    case IPP_TAG_KEYWORD :
    case IPP_TAG_URISCHEME :
    case IPP_TAG_CHARSET :
    case IPP_TAG_LANGUAGE :
    case IPP_TAG_MIMETYPE :
    case IPP_TAG_BEGIN_COLLECTION :
        break;

    default :
        add_stringf(errors, "WITH-DISTINCT-VALUES %s not supported for 1setOf %s", ippGetName(attr), ippTagString(value_tag));
        return (false);
  }

  // Collect values and determine they are all unique...
  values = cupsArrayNew3((cups_array_cb_t)_cupsArrayStrcmp, NULL, NULL, 0, (cups_acopy_cb_t)strdup, (cups_afree_cb_t)free);

  for (i = 0; i < count; i ++)
  {
    switch (value_tag)
    {
      case IPP_TAG_INTEGER :
      case IPP_TAG_ENUM :
          snprintf(buffer, sizeof(buffer), "%d", ippGetInteger(attr, i));
          value = buffer;
          break;
      case IPP_TAG_RANGE :
          {
            int upper, lower = ippGetRange(attr, i, &upper);
					// Range values

            snprintf(buffer, sizeof(buffer), "%d-%d", lower, upper);
            value = buffer;
	  }
          break;
      case IPP_TAG_RESOLUTION :
          {
            ipp_res_t units;		// Resolution units
            int yres, xres = ippGetResolution(attr, i, &yres, &units);
					// Resolution values

            if (xres == yres)
              snprintf(buffer, sizeof(buffer), "%d%s", xres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
	    else
              snprintf(buffer, sizeof(buffer), "%dx%d%s", xres, yres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
            value = buffer;
	  }
          break;
      case IPP_TAG_KEYWORD :
      case IPP_TAG_URISCHEME :
      case IPP_TAG_CHARSET :
      case IPP_TAG_LANGUAGE :
      case IPP_TAG_MIMETYPE :
          value = ippGetString(attr, i, NULL);
          break;
      case IPP_TAG_BEGIN_COLLECTION :
          {
            ipp_t	*col = ippGetCollection(attr, i);
					// Collection value
            ipp_attribute_t *member;	// Member attribute
            char	*bufptr,	// Pointer into buffer
			*bufend,	// End of buffer
			prefix;		// Prefix character

            for (prefix = '{', bufptr = buffer, bufend = buffer + sizeof(buffer) - 2, member = ippGetFirstAttribute(col); member && bufptr < bufend; member = ippGetNextAttribute(col))
            {
              *bufptr++ = prefix;
              prefix    = ' ';

              ippAttributeString(member, bufptr, (size_t)(bufend - bufptr));
              bufptr += strlen(bufptr);
            }

            *bufptr++ = '}';
            *bufptr   = '\0';
            value     = buffer;
          }
          break;
      default : // Should never happen
          value = "unsupported";
          break;
    }

    if (cupsArrayFind(values, (void *)value))
      add_stringf(errors, "DUPLICATE: %s=%s", ippGetName(attr), value);
    else
      cupsArrayAdd(values, (void *)value);
  }

  // Cleanup...
  ret = cupsArrayGetCount(values) == count;
  cupsArrayDelete(values);

  return (ret);
}


//
// 'with_flags_string()' - Return the "WITH-xxx" predicate that corresponds to
//                         the flags.
//

static const char *                     // O - WITH-xxx string
with_flags_string(int flags)            // I - WITH flags
{
  if (flags & IPPTOOL_WITH_ALL)
  {
    if (flags & IPPTOOL_WITH_HOSTNAME)
      return ("WITH-ALL-HOSTNAMES");
    else if (flags & IPPTOOL_WITH_RESOURCE)
      return ("WITH-ALL-RESOURCES");
    else if (flags & IPPTOOL_WITH_SCHEME)
      return ("WITH-ALL-SCHEMES");
    else
      return ("WITH-ALL-VALUES");
  }
  else if (flags & IPPTOOL_WITH_HOSTNAME)
    return ("WITH-HOSTNAME");
  else if (flags & IPPTOOL_WITH_RESOURCE)
    return ("WITH-RESOURCE");
  else if (flags & IPPTOOL_WITH_SCHEME)
    return ("WITH-SCHEME");
  else
    return ("WITH-VALUE");
}


//
// 'with_value()' - Test a WITH-VALUE predicate.
//

static bool				// O - `true` on match, `false` on non-match
with_value(ipptool_test_t  *data,	// I - Test data
           cups_array_t    *errors,	// I - Errors array
           char            *value,	// I - Value string
           int             flags,	// I - Flags for match
           ipp_attribute_t *attr,	// I - Attribute to compare
	   char            *matchbuf,	// I - Buffer to hold matching value
	   size_t          matchlen)	// I - Length of match buffer
{
  int		i,			// Looping var
    		count;			// Number of values
  bool		match;			// Match?
  char		temp[1024],		// Temporary value string
		*valptr;		// Pointer into value
  const char	*name;			// Attribute name


  *matchbuf = '\0';
  match     = (flags & IPPTOOL_WITH_ALL) ? true : false;

  // NULL matches everything.
  if (!value || !*value)
    return (true);

  // Compare the value string to the attribute value.
  name  = ippGetName(attr);
  count = ippGetCount(attr);

  switch (ippGetValueTag(attr))
  {
    case IPP_TAG_INTEGER :
    case IPP_TAG_ENUM :
        for (i = 0; i < count; i ++)
        {
	  char	op,			// Comparison operator
	  	*nextptr;		// Next pointer
	  int	intvalue,		// Integer value
		attrvalue = ippGetInteger(attr, i);
					// Attribute value
	  bool	valmatch = false;	// Does the current value match?

          valptr = value;

	  while (isspace(*valptr & 255) || isdigit(*valptr & 255) ||
		 *valptr == '-' || *valptr == ',' || *valptr == '<' ||
		 *valptr == '=' || *valptr == '>')
	  {
	    op = '=';
	    while (*valptr && !isdigit(*valptr & 255) && *valptr != '-')
	    {
	      if (*valptr == '<' || *valptr == '>' || *valptr == '=')
		op = *valptr;
	      valptr ++;
	    }

            if (!*valptr)
	      break;

	    intvalue = (int)strtol(valptr, &nextptr, 0);
	    if (nextptr == valptr)
	      break;
	    valptr = nextptr;

            if ((op == '=' && attrvalue == intvalue) ||
                (op == '<' && attrvalue < intvalue) ||
                (op == '>' && attrvalue > intvalue))
	    {
	      if (!matchbuf[0])
		snprintf(matchbuf, matchlen, "%d", attrvalue);

	      valmatch = true;
	      break;
	    }
	  }

          if (flags & IPPTOOL_WITH_ALL)
          {
            if (!valmatch)
            {
              match = false;
              break;
            }
          }
          else if (valmatch)
          {
            match = true;
            break;
          }
        }

        if (!match && errors)
	{
	  for (i = 0; i < count; i ++)
	    add_stringf(data->errors, "GOT: %s=%d", name, ippGetInteger(attr, i));
	}
	break;

    case IPP_TAG_RANGE :
        for (i = 0; i < count; i ++)
        {
	  char	op,			// Comparison operator
	  	*nextptr;		// Next pointer
	  int	intvalue,		// Integer value
	        lower,			// Lower range
	        upper;			// Upper range
	  bool	valmatch = false;	// Does the current value match?

	  lower = ippGetRange(attr, i, &upper);
          valptr = value;

	  while (isspace(*valptr & 255) || isdigit(*valptr & 255) ||
		 *valptr == '-' || *valptr == ',' || *valptr == '<' ||
		 *valptr == '=' || *valptr == '>')
	  {
	    op = '=';
	    while (*valptr && !isdigit(*valptr & 255) && *valptr != '-')
	    {
	      if (*valptr == '<' || *valptr == '>' || *valptr == '=')
		op = *valptr;
	      valptr ++;
	    }

            if (!*valptr)
	      break;

	    intvalue = (int)strtol(valptr, &nextptr, 0);
	    if (nextptr == valptr)
	      break;
	    valptr = nextptr;

            if ((op == '=' && (lower == intvalue || upper == intvalue)) ||
		(op == '<' && upper < intvalue) ||
		(op == '>' && upper > intvalue))
	    {
	      if (!matchbuf[0])
		snprintf(matchbuf, matchlen, "%d-%d", lower, upper);

	      valmatch = true;
	      break;
	    }
	  }

          if (flags & IPPTOOL_WITH_ALL)
          {
            if (!valmatch)
            {
              match = false;
              break;
            }
          }
          else if (valmatch)
          {
            match = true;
            break;
          }
        }

        if (!match && errors)
	{
	  for (i = 0; i < count; i ++)
	  {
	    int lower, upper;		// Range values

	    lower = ippGetRange(attr, i, &upper);
	    add_stringf(data->errors, "GOT: %s=%d-%d", name, lower, upper);
	  }
	}
	break;

    case IPP_TAG_BOOLEAN :
	for (i = 0; i < count; i ++)
	{
          if ((!strcmp(value, "true") || !strcmp(value, "1")) == ippGetBoolean(attr, i))
          {
            if (!matchbuf[0])
	      cupsCopyString(matchbuf, value, matchlen);

	    if (!(flags & IPPTOOL_WITH_ALL))
	    {
	      match = true;
	      break;
	    }
	  }
	  else if (flags & IPPTOOL_WITH_ALL)
	  {
	    match = false;
	    break;
	  }
	}

	if (!match && errors)
	{
	  for (i = 0; i < count; i ++)
	    add_stringf(data->errors, "GOT: %s=%s", name, ippGetBoolean(attr, i) ? "true" : "false");
	}
	break;

    case IPP_TAG_RESOLUTION :
	for (i = 0; i < count; i ++)
	{
	  int		xres, yres;	// Resolution values
	  ipp_res_t	units;		// Resolution units

	  xres = ippGetResolution(attr, i, &yres, &units);
	  if (xres == yres)
	    snprintf(temp, sizeof(temp), "%d%s", xres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
	  else
	    snprintf(temp, sizeof(temp), "%dx%d%s", xres, yres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");

          if (!strcmp(value, temp))
          {
            if (!matchbuf[0])
	      cupsCopyString(matchbuf, value, matchlen);

	    if (!(flags & IPPTOOL_WITH_ALL))
	    {
	      match = true;
	      break;
	    }
	  }
	  else if (flags & IPPTOOL_WITH_ALL)
	  {
	    match = false;
	    break;
	  }
	}

	if (!match && errors)
	{
	  for (i = 0; i < count; i ++)
	  {
	    int		xres, yres;	// Resolution values
	    ipp_res_t	units;		// Resolution units

	    xres = ippGetResolution(attr, i, &yres, &units);
	    if (xres == yres)
	      snprintf(temp, sizeof(temp), "%d%s", xres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
	    else
	      snprintf(temp, sizeof(temp), "%dx%d%s", xres, yres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");

            if (strcmp(value, temp))
	      add_stringf(data->errors, "GOT: %s=%s", name, temp);
	  }
	}
	break;

    case IPP_TAG_NOVALUE :
    case IPP_TAG_UNKNOWN :
	return (true);

    case IPP_TAG_CHARSET :
    case IPP_TAG_KEYWORD :
    case IPP_TAG_LANGUAGE :
    case IPP_TAG_MIMETYPE :
    case IPP_TAG_NAME :
    case IPP_TAG_NAMELANG :
    case IPP_TAG_TEXT :
    case IPP_TAG_TEXTLANG :
    case IPP_TAG_URI :
    case IPP_TAG_URISCHEME :
        if (flags & IPPTOOL_WITH_REGEX)
	{
	  // Value is an extended, case-sensitive POSIX regular expression...
          int		r;		// Error, if any
	  regex_t	re;		// Regular expression

          if ((r = regcomp(&re, value, REG_EXTENDED | REG_NOSUB)) != 0)
	  {
            regerror(r, &re, temp, sizeof(temp));

	    print_fatal_error(data, "Unable to compile WITH-VALUE regular expression \"%s\" - %s", value, temp);
	    return (false);
	  }

          // See if ALL of the values match the given regular expression.
	  for (i = 0; i < count; i ++)
	  {
	    if (!regexec(&re, get_string(attr, i, flags, temp, sizeof(temp)),
	                 0, NULL, 0))
	    {
	      if (!matchbuf[0])
		cupsCopyString(matchbuf, get_string(attr, i, flags, temp, sizeof(temp)), matchlen);

	      if (!(flags & IPPTOOL_WITH_ALL))
	      {
	        match = true;
	        break;
	      }
	    }
	    else if (flags & IPPTOOL_WITH_ALL)
	    {
	      match = false;
	      break;
	    }
	  }

	  regfree(&re);
	}
	else if (ippGetValueTag(attr) == IPP_TAG_URI && !(flags & (IPPTOOL_WITH_SCHEME | IPPTOOL_WITH_HOSTNAME | IPPTOOL_WITH_RESOURCE)))
	{
	  // Value is a literal URI string, see if the value(s) match...
	  for (i = 0; i < count; i ++)
	  {
	    if (!compare_uris(value, get_string(attr, i, flags, temp, sizeof(temp))))
	    {
	      if (!matchbuf[0])
		cupsCopyString(matchbuf, get_string(attr, i, flags, temp, sizeof(temp)), matchlen);

	      if (!(flags & IPPTOOL_WITH_ALL))
	      {
	        match = true;
	        break;
	      }
	    }
	    else if (flags & IPPTOOL_WITH_ALL)
	    {
	      match = false;
	      break;
	    }
	  }
	}
	else
	{
	  // Value is a literal string, see if the value(s) match...
	  for (i = 0; i < count; i ++)
	  {
	    int result;

            switch (ippGetValueTag(attr))
            {
              case IPP_TAG_URI :
                  // Some URI components are case-sensitive, some not...
                  if (flags & (IPPTOOL_WITH_SCHEME | IPPTOOL_WITH_HOSTNAME))
                    result = _cups_strcasecmp(value, get_string(attr, i, flags, temp, sizeof(temp)));
                  else
                    result = strcmp(value, get_string(attr, i, flags, temp, sizeof(temp)));
                  break;

              case IPP_TAG_MIMETYPE :
              case IPP_TAG_NAME :
              case IPP_TAG_NAMELANG :
              case IPP_TAG_TEXT :
              case IPP_TAG_TEXTLANG :
                  // mimeMediaType, nameWithoutLanguage, nameWithLanguage,
                  // textWithoutLanguage, and textWithLanguage are defined to
                  // be case-insensitive strings...
                  result = _cups_strcasecmp(value, get_string(attr, i, flags, temp, sizeof(temp)));
                  break;

              default :
                  // Other string syntaxes are defined as lowercased so we use
                  // case-sensitive comparisons to catch problems...
                  result = strcmp(value, get_string(attr, i, flags, temp, sizeof(temp)));
                  break;
            }

            if (!result)
	    {
	      if (!matchbuf[0])
		cupsCopyString(matchbuf, get_string(attr, i, flags, temp, sizeof(temp)), matchlen);

	      if (!(flags & IPPTOOL_WITH_ALL))
	      {
	        match = true;
	        break;
	      }
	    }
	    else if (flags & IPPTOOL_WITH_ALL)
	    {
	      match = false;
	      break;
	    }
	  }
	}

        if (!match && errors)
        {
	  for (i = 0; i < count; i ++)
	    add_stringf(data->errors, "GOT: %s=\"%s\"", name, ippGetString(attr, i, NULL));
        }
	break;

    case IPP_TAG_STRING :
        if (flags & IPPTOOL_WITH_REGEX)
	{
	  // Value is an extended, case-sensitive POSIX regular expression...
	  void		*adata;		// Pointer to octetString data
	  int		adatalen;	// Length of octetString
	  int		r;		// Error, if any
	  regex_t	re;		// Regular expression

          if ((r = regcomp(&re, value, REG_EXTENDED | REG_NOSUB)) != 0)
	  {
            regerror(r, &re, temp, sizeof(temp));

	    print_fatal_error(data, "Unable to compile WITH-VALUE regular expression \"%s\" - %s", value, temp);
	    return (false);
	  }

          // See if ALL of the values match the given regular expression.
	  for (i = 0; i < count; i ++)
	  {
            if ((adata = ippGetOctetString(attr, i, &adatalen)) == NULL || adatalen >= (int)sizeof(temp))
            {
              match = false;
              break;
            }
            memcpy(temp, adata, (size_t)adatalen);
            temp[adatalen] = '\0';

	    if (!regexec(&re, temp, 0, NULL, 0))
	    {
	      if (!matchbuf[0])
		cupsCopyString(matchbuf, temp, matchlen);

	      if (!(flags & IPPTOOL_WITH_ALL))
	      {
	        match = true;
	        break;
	      }
	    }
	    else if (flags & IPPTOOL_WITH_ALL)
	    {
	      match = false;
	      break;
	    }
	  }

	  regfree(&re);

	  if (!match && errors)
	  {
	    for (i = 0; i < count; i ++)
	    {
	      adata = ippGetOctetString(attr, i, &adatalen);
	      copy_hex_string(temp, adata, adatalen, sizeof(temp));
	      add_stringf(data->errors, "GOT: %s=\"%s\"", name, temp);
	    }
	  }
	}
	else
        {
          // Value is a literal or hex-encoded string...
          unsigned char	withdata[1023],	// WITH-VALUE data
			*adata;		// Pointer to octetString data
	  int		withlen,	// Length of WITH-VALUE data
			adatalen;	// Length of octetString

          if (*value == '<')
          {
            // Grab hex-encoded value...
            if ((withlen = (int)strlen(value)) & 1 || withlen > (int)(2 * (sizeof(withdata) + 1)))
            {
	      print_fatal_error(data, "Bad WITH-VALUE hex value.");
              return (false);
	    }

	    withlen = withlen / 2 - 1;

            for (valptr = value + 1, adata = withdata; *valptr; valptr += 2)
            {
              int ch;			// Current character/byte

	      if (isdigit(valptr[0]))
	        ch = (valptr[0] - '0') << 4;
	      else if (isalpha(valptr[0]))
	        ch = (tolower(valptr[0]) - 'a' + 10) << 4;
	      else
	        break;

	      if (isdigit(valptr[1]))
	        ch |= valptr[1] - '0';
	      else if (isalpha(valptr[1]))
	        ch |= tolower(valptr[1]) - 'a' + 10;
	      else
	        break;

	      *adata++ = (unsigned char)ch;
	    }

	    if (*valptr)
	    {
	      print_fatal_error(data, "Bad WITH-VALUE hex value.");
              return (false);
	    }
          }
          else
          {
            // Copy literal string value...
            withlen = strlen(value);

            memcpy(withdata, value, (size_t)withlen);
	  }

	  for (i = 0; i < count; i ++)
	  {
	    adata = ippGetOctetString(attr, i, &adatalen);

	    if (withlen == adatalen && !memcmp(withdata, adata, (size_t)withlen))
	    {
	      if (!matchbuf[0])
                copy_hex_string(matchbuf, adata, adatalen, matchlen);

	      if (!(flags & IPPTOOL_WITH_ALL))
	      {
	        match = true;
	        break;
	      }
	    }
	    else if (flags & IPPTOOL_WITH_ALL)
	    {
	      match = false;
	      break;
	    }
	  }

	  if (!match && errors)
	  {
	    for (i = 0; i < count; i ++)
	    {
	      adata = ippGetOctetString(attr, i, &adatalen);
	      copy_hex_string(temp, adata, adatalen, sizeof(temp));
	      add_stringf(data->errors, "GOT: %s=\"%s\"", name, temp);
	    }
	  }
        }
        break;

    default :
        break;
  }

  return (match);
}


//
// 'with_value_from()' - Test a WITH-VALUE-FROM predicate.
//

static bool				// O - `true` on match, `false` on non-match
with_value_from(
    cups_array_t    *errors,		// I - Errors array
    ipp_attribute_t *fromattr,		// I - "From" attribute
    ipp_attribute_t *attr,		// I - Attribute to compare
    char            *matchbuf,		// I - Buffer to hold matching value
    size_t          matchlen)		// I - Length of match buffer
{
  int		i, j,			// Looping vars
		count = ippGetCount(attr);
					// Number of attribute values
  bool		match = true;		// Match?


  *matchbuf = '\0';

  // Compare the from value(s) to the attribute value(s)...
  switch (ippGetValueTag(attr))
  {
    case IPP_TAG_INTEGER :
        if (ippGetValueTag(fromattr) != IPP_TAG_INTEGER && ippGetValueTag(fromattr) != IPP_TAG_RANGE)
	  goto wrong_value_tag;

	for (i = 0; i < count; i ++)
	{
	  int value = ippGetInteger(attr, i);
					// Current integer value

	  if (ippContainsInteger(fromattr, value))
	  {
	    if (!matchbuf[0])
	      snprintf(matchbuf, matchlen, "%d", value);
	  }
	  else
	  {
	    add_stringf(errors, "GOT: %s=%d", ippGetName(attr), value);
	    match = false;
	  }
	}
	break;

    case IPP_TAG_ENUM :
        if (ippGetValueTag(fromattr) != IPP_TAG_ENUM)
	  goto wrong_value_tag;

	for (i = 0; i < count; i ++)
	{
	  int value = ippGetInteger(attr, i);
					// Current integer value

	  if (ippContainsInteger(fromattr, value))
	  {
	    if (!matchbuf[0])
	      snprintf(matchbuf, matchlen, "%d", value);
	  }
	  else
	  {
	    add_stringf(errors, "GOT: %s=%d", ippGetName(attr), value);
	    match = false;
	  }
	}
	break;

    case IPP_TAG_RESOLUTION :
        if (ippGetValueTag(fromattr) != IPP_TAG_RESOLUTION)
	  goto wrong_value_tag;

	for (i = 0; i < count; i ++)
	{
	  int		xres, yres;	// Current X,Y resolution
	  ipp_res_t	units;		// Current units
          int		fromcount = ippGetCount(fromattr);
					// From count
	  int		fromxres, fromyres;
					// From X,Y resolution
	  ipp_res_t	fromunits;	// From units

	  xres = ippGetResolution(attr, i, &yres, &units);

          for (j = 0; j < fromcount; j ++)
	  {
	    fromxres = ippGetResolution(fromattr, j, &fromyres, &fromunits);
	    if (fromxres == xres && fromyres == yres && fromunits == units)
	      break;
	  }

	  if (j < fromcount)
	  {
	    if (!matchbuf[0])
	    {
	      if (xres == yres)
	        snprintf(matchbuf, matchlen, "%d%s", xres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
	      else
	        snprintf(matchbuf, matchlen, "%dx%d%s", xres, yres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
	    }
	  }
	  else
	  {
	    if (xres == yres)
	      add_stringf(errors, "GOT: %s=%d%s", ippGetName(attr), xres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
	    else
	      add_stringf(errors, "GOT: %s=%dx%d%s", ippGetName(attr), xres, yres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");

	    match = false;
	  }
	}
	break;

    case IPP_TAG_NOVALUE :
    case IPP_TAG_UNKNOWN :
	return (true);

    case IPP_TAG_CHARSET :
    case IPP_TAG_KEYWORD :
    case IPP_TAG_LANGUAGE :
    case IPP_TAG_MIMETYPE :
    case IPP_TAG_NAME :
    case IPP_TAG_NAMELANG :
    case IPP_TAG_TEXT :
    case IPP_TAG_TEXTLANG :
    case IPP_TAG_URISCHEME :
	for (i = 0; i < count; i ++)
	{
	  const char *value = ippGetString(attr, i, NULL);
					// Current string value

	  if (ippContainsString(fromattr, value))
	  {
	    if (!matchbuf[0])
	      cupsCopyString(matchbuf, value, matchlen);
	  }
	  else
	  {
	    add_stringf(errors, "GOT: %s='%s'", ippGetName(attr), value);
	    match = false;
	  }
	}
	break;

    case IPP_TAG_URI :
	for (i = 0; i < count; i ++)
	{
	  const char	*value = ippGetString(attr, i, NULL);
					// Current string value
          int		fromcount = ippGetCount(fromattr);
					// From count

          for (j = 0; j < fromcount; j ++)
          {
            if (!compare_uris(value, ippGetString(fromattr, j, NULL)))
            {
              if (!matchbuf[0])
                cupsCopyString(matchbuf, value, matchlen);
              break;
            }
          }

	  if (j >= fromcount)
	  {
	    add_stringf(errors, "GOT: %s='%s'", ippGetName(attr), value);
	    match = false;
	  }
	}
	break;

    default :
        match = false;
        break;
  }

  return (match);

  // value tag mismatch between fromattr and attr...
  wrong_value_tag :

  add_stringf(errors, "GOT: %s OF-TYPE %s", ippGetName(attr), ippTagString(ippGetValueTag(attr)));

  return (false);
}
