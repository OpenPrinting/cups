//
// DNS-SD API test program for CUPS.
//
// Copyright © 2022-2024 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "test-internal.h"
#include "dnssd.h"
#include "thread.h"


//
// Local structures...
//

typedef struct testdata_s		// Test data structure
{
  cups_mutex_t	mutex;			// Mutex for access
  cups_array_t	*messages;		// Messages from callbacks
  char		name[256];		// Service name
  size_t	browse_dnssd_count;	// Number of testdnssd browse callbacks
  size_t	browse_ipp_count;	// Number of IPP browse callbacks
  size_t	error_count;		// Number of error callbacks
  size_t	query_count;		// Number of query callbacks
  size_t	resolve_count;		// Number of resolve callbacks
  size_t	service_count;		// Number of service callbacks
} testdata_t;


//
// Local functions...
//

static void	browse_cb(cups_dnssd_browse_t *browse, void *cb_data, cups_dnssd_flags_t flags, uint32_t if_index, const char *name, const char *regtype, const char *domain);
static void	browse_print_cb(cups_dnssd_browse_t *browse, void *cb_data, cups_dnssd_flags_t flags, uint32_t if_index, const char *name, const char *regtype, const char *domain);
static void	error_cb(void *cb_data, const char *message);
static void	query_cb(cups_dnssd_query_t *query, void *cb_data, cups_dnssd_flags_t flags, uint32_t if_index, const char *fullname, uint16_t rrtype, const void *qdata, uint16_t qlen);
static void	resolve_cb(cups_dnssd_resolve_t *res, void *cb_data, cups_dnssd_flags_t flags, uint32_t if_index, const char *fullname, const char *host, uint16_t port, int num_txt, cups_option_t *txt);
static void	service_cb(cups_dnssd_service_t *service, void *cb_data, cups_dnssd_flags_t flags);
static void	usage(const char *arg);


//
// 'main()' - Main entry.
//

int					// O - Exit status
main(int  argc,				// I - Number of command-line arguments
     char *argv[])			// I - Command-line arguments
{
  int			i,		// Looping var
			ret = 0;	// Return value
  cups_dnssd_t		*dnssd;		// DNS-SD context
  char			name[256];	// Name buffer
  cups_dnssd_browse_t	*browse;	// DNS-SD browse request
//  cups_dnssd_query_t	*query;		// DNS-SD query request
  cups_dnssd_resolve_t	*resolve;	// DNS-SD resolve request
  cups_dnssd_service_t	*service;	// DNS-SD service registration
  size_t		num_txt;	// Number of TXT record key/value pairs
  cups_option_t		*txt;		// TXT record key/value pairs
  testdata_t		testdata;	// Test data


  // Clear test data...
  memset(&testdata, 0, sizeof(testdata));
  testdata.messages = cupsArrayNew3(NULL, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free);
#if _WIN32
  snprintf(testdata.name, sizeof(testdata.name), "Test Service %d", (int)GetCurrentProcessId());
#else
  snprintf(testdata.name, sizeof(testdata.name), "Test Service %d", (int)getpid());
#endif // _WIN32

  if (argc == 1)
  {
    // Do unit tests...
    testBegin("cupsDNSSDNew");
    if ((dnssd = cupsDNSSDNew(error_cb, &testdata)) != NULL)
      testEnd(true);
    else
      return (1);

    testBegin("cupsDNSSDCopyComputerName");
    if (cupsDNSSDCopyComputerName(dnssd, name, sizeof(name)))
      testEndMessage(true, name);
    else
      testEnd(false);

    testBegin("cupsDNSSDCopyHostName");
    if (cupsDNSSDCopyHostName(dnssd, name, sizeof(name)))
      testEndMessage(true, name);
    else
      testEnd(false);

    testBegin("cupsDNSSDBrowseNew(_ipp._tcp)");
    if ((browse = cupsDNSSDBrowseNew(dnssd, CUPS_DNSSD_IF_INDEX_ANY, "_ipp._tcp", NULL, browse_cb, &testdata)) != NULL)
    {
      testEnd(true);
    }
    else
    {
      ret = 1;
      goto done;
    }

    testBegin("cupsDNSSDBrowseGetContext");
    testEnd(cupsDNSSDBrowseGetContext(browse) == dnssd);

    testBegin("cupsDNSSDBrowseNew(_testdnssd._tcp)");
    if ((browse = cupsDNSSDBrowseNew(dnssd, CUPS_DNSSD_IF_INDEX_ANY, "_testdnssd._tcp", NULL, browse_cb, &testdata)) != NULL)
    {
      testEnd(true);
    }
    else
    {
      ret = 1;
      goto done;
    }

    testBegin("cupsDNSSDBrowseGetContext");
    testEnd(cupsDNSSDBrowseGetContext(browse) == dnssd);

    testBegin("cupsDNSSDServiceNew(%s)", testdata.name);
    if ((service = cupsDNSSDServiceNew(dnssd, CUPS_DNSSD_IF_INDEX_ANY, testdata.name, service_cb, &testdata)) != NULL)
    {
      testEnd(true);
    }
    else
    {
      ret = 1;
      goto done;
    }

    num_txt = cupsAddOption("rp", "ipp/print", 0, &txt);

    testBegin("cupsDNSSDServiceAdd(_http._tcp)");
    if (cupsDNSSDServiceAdd(service, "_http._tcp,_printer", /*host*/NULL, /*domain*/NULL, 631, /*num_txt*/0, /*txt*/NULL))
    {
      testEnd(true);
    }
    else
    {
      ret = 1;
      goto done;
    }

    testBegin("cupsDNSSDServiceAdd(_ipp._tcp)");
    if (cupsDNSSDServiceAdd(service, "_ipp._tcp,_print", /*host*/NULL, /*domain*/NULL, 631, num_txt, txt))
    {
      testEnd(true);
    }
    else
    {
      ret = 1;
      goto done;
    }

    testBegin("cupsDNSSDServiceAdd(_testdnssd._tcp)");
    if (cupsDNSSDServiceAdd(service, "_testdnssd._tcp", /*host*/NULL, /*domain*/NULL, 54321, num_txt, txt))
    {
      testEnd(true);
    }
    else
    {
      ret = 1;
      goto done;
    }

    testBegin("cupsDNSSDServicePublish");
    testEnd(cupsDNSSDServicePublish(service));

    testBegin("cupsDNSSDServiceGetContext");
    testEnd(cupsDNSSDServiceGetContext(service) == dnssd);

    cupsFreeOptions(num_txt, txt);

    testBegin("cupsDNSSDResolveNew(%s._testdnssd._tcp.local.)", testdata.name);
    if ((resolve = cupsDNSSDResolveNew(dnssd, CUPS_DNSSD_IF_INDEX_ANY, testdata.name, "_testdnssd._tcp", "local.", resolve_cb, &testdata)) != NULL)
    {
      testEnd(true);
    }
    else
    {
      ret = 1;
      goto done;
    }

    testBegin("cupsDNSSDResolveGetContext");
    testEnd(cupsDNSSDResolveGetContext(resolve) == dnssd);

    testBegin("Wait for callbacks");

    for (i = 0; i < 30; i ++)
    {
      if (testdata.service_count != 0 && testdata.browse_dnssd_count != 0 && testdata.browse_ipp_count != 0 && testdata.resolve_count != 0)
        break;

      testProgress();
      sleep(1);
    }

    testEndMessage(i < 30, "Bdnssd=%u Bipp=%u Q=%u R=%u S=%u", (unsigned)testdata.browse_dnssd_count, (unsigned)testdata.browse_ipp_count, (unsigned)testdata.query_count, (unsigned)testdata.resolve_count, (unsigned)testdata.service_count);
    if (i >= 30)
      ret = 1;

    done:

    cupsDNSSDDelete(dnssd);

    if (ret || getenv("TESTDNSSD_DEBUG"))
    {
      const char *message;		// Current message

      for (message = (const char *)cupsArrayFirst(testdata.messages); message; message = (const char *)cupsArrayNext(testdata.messages))
	puts(message);
    }

    cupsArrayDelete(testdata.messages);
  }
  else if (argc == 3 && !strcmp(argv[1], "browse") && argv[2][0] == '_')
  {
    // Browse for the named service...
    size_t count = 0;			// Number of found services

    if ((dnssd = cupsDNSSDNew(error_cb, &testdata)) == NULL)
      return (1);

    puts("IfIdx Service Name");
    puts("----- ----------------------------------------------------------------");

    if ((browse = cupsDNSSDBrowseNew(dnssd, CUPS_DNSSD_IF_INDEX_ANY, argv[2], NULL, browse_print_cb, &testdata)) == NULL)
    {
      cupsDNSSDDelete(dnssd);
      return (1);
    }

    for (;;)
    {
      size_t browse_dnssd_count;	// Saved count

      sleep(1);

      cupsMutexLock(&testdata.mutex);
      browse_dnssd_count = testdata.browse_dnssd_count;
      cupsMutexUnlock(&testdata.mutex);

      if (browse_dnssd_count == count)
        break;

      count = browse_dnssd_count;
    }

    cupsDNSSDDelete(dnssd);
  }
  else
  {
    usage(argv[1]);
  }

  return (ret);
}


//
// 'browse_cb()' - Record browse request callback usage.
//

static void
browse_cb(
    cups_dnssd_browse_t *browse,	// I - Browse request
    void                *cb_data,	// I - Callback data
    cups_dnssd_flags_t  flags,		// I - Bit flags
    uint32_t            if_index,	// I - Interface index
    const char          *name,		// I - Service name
    const char          *regtype,	// I - Registration type
    const char          *domain)	// I - Domain
{
  testdata_t	*data = (testdata_t *)cb_data;
					// Test data
  char		message[1024];		// Message string
  char		fullname[1024];		// Full service name


  snprintf(message, sizeof(message), "B flags=%02X if_index=%u name=\"%s\" regtype=\"%s\" domain=\"%s\"", flags, if_index, name, regtype, domain);

  cupsDNSSDResolveNew(cupsDNSSDBrowseGetContext(browse), CUPS_DNSSD_IF_INDEX_ANY, name, regtype, domain, resolve_cb, cb_data);

  cupsDNSSDAssembleFullName(fullname, sizeof(fullname), name, regtype, domain);
  cupsDNSSDQueryNew(cupsDNSSDBrowseGetContext(browse), CUPS_DNSSD_IF_INDEX_ANY, fullname, CUPS_DNSSD_RRTYPE_TXT, query_cb, cb_data);

  cupsMutexLock(&data->mutex);
  cupsArrayAdd(data->messages, message);
  if (!strncmp(regtype, "_ipp.", 5))
    data->browse_ipp_count ++;
  else if (!strncmp(regtype, "_testdnssd.", 11))
    data->browse_dnssd_count ++;
  cupsMutexUnlock(&data->mutex);
}


//
// 'browse_print_cb()' - Print browse request callback usage.
//

static void
browse_print_cb(
    cups_dnssd_browse_t *browse,	// I - Browse request
    void                *cb_data,	// I - Callback data
    cups_dnssd_flags_t  flags,		// I - Bit flags
    uint32_t            if_index,	// I - Interface index
    const char          *name,		// I - Service name
    const char          *regtype,	// I - Registration type
    const char          *domain)	// I - Domain
{
  testdata_t	*data = (testdata_t *)cb_data;
					// Test data


  printf("%5u %s.%s.%s\n", if_index, name, regtype, domain);

  cupsMutexLock(&data->mutex);
  data->browse_dnssd_count ++;
  cupsMutexUnlock(&data->mutex);
}


//
// 'error_cb()' - Display an error.
//

static void
error_cb(void       *cb_data,		// I - Callback data
         const char *message)		// I - Error message
{
  testdata_t	*data = (testdata_t *)cb_data;
					// Test data


  testEndMessage(false, "%s", message);

  cupsMutexLock(&data->mutex);
  data->error_count ++;
  cupsMutexUnlock(&data->mutex);
}


//
// 'query_cb()' - Record query request callback usage.
//

static void
query_cb(
    cups_dnssd_query_t *query,		// I - Query request
    void               *cb_data,	// I - Callback data
    cups_dnssd_flags_t flags,		// I - Flags
    uint32_t           if_index,	// I - Interface index
    const char         *fullname,	// I - Full service name
    uint16_t           rrtype,		// I - Record type
    const void         *qdata,		// I - Record data
    uint16_t           qlen)		// I - Length of record data
{
  testdata_t	*data = (testdata_t *)cb_data;
					// Test data
  uint16_t	i;			// Looping var
  char		message[2048],		// Message string
		*mptr;			// Pointer into message string
  const unsigned char *qptr;		// Pointer into record data


  (void)query;

  snprintf(message, sizeof(message), "Q flags=%02X if_index=%u fullname=\"%s\" rrtype=%u qlen=%u qdata=<", flags, if_index, fullname, rrtype, qlen);
  for (mptr = message + strlen(message), i = 0, qptr = (const unsigned char *)qdata; i < qlen; i ++, mptr += strlen(mptr), qptr ++)
    snprintf(mptr, sizeof(message) - (size_t)(mptr - message), "%02X", *qptr);
  if (mptr < (message + sizeof(message) - 1))
  {
    *mptr++ = '>';
    *mptr   = '\0';
  }

  cupsMutexLock(&data->mutex);
  cupsArrayAdd(data->messages, message);
  data->query_count ++;
  cupsMutexUnlock(&data->mutex);
}


//
// 'resolve_cb()' - Record resolve request callback usage.
//

static void
resolve_cb(
    cups_dnssd_resolve_t *res,		// I - Resolve request
    void                 *cb_data,	// I - Callback data
    cups_dnssd_flags_t   flags,		// I - Flags
    uint32_t             if_index,	// I - Interface index
    const char           *fullname,	// I - Full service name
    const char           *host,		// I - Hostname
    uint16_t             port,		// I - Port number
    int                  num_txt,	// I - Number of key/value pairs in TXT record
    cups_option_t        *txt)		// I - Key/value pairs
{
  testdata_t	*data = (testdata_t *)cb_data;
					// Test data
  int		i;			// Looping var
  char		message[2048],		// Message string
		*mptr;			// Pointer into message string
  const char	*prefix = " txt=";	// Prefix string
  char		name[256],		// Service name
		regtype[256],		// Registration type
		domain[256];		// Domain name


  (void)res;

  snprintf(message, sizeof(message), "R flags=%02X if_index=%u fullname=\"%s\" host=\"%s\" port=%u num_txt=%u", flags, if_index, fullname, host, port, (unsigned)num_txt);
  for (mptr = message + strlen(message), i = 0; i < num_txt; i ++, mptr += strlen(mptr))
  {
    snprintf(mptr, sizeof(message) - (size_t)(mptr - message), "%s\"%s=%s\"", prefix, txt[i].name, txt[i].value);
    prefix = ",";
  }

  cupsMutexLock(&data->mutex);

  cupsArrayAdd(data->messages, message);

  if (!cupsDNSSDSeparateFullName(fullname, name, sizeof(name), regtype, sizeof(regtype), domain, sizeof(domain)))
  {
    // Separation failed...
    snprintf(message, sizeof(message), "R ==== ERROR: Unable to separate fullname \"%s\" into components ====", fullname);
    cupsArrayAdd(data->messages, message);
  }
  else if (!strcmp(regtype, "_testdnssd._tcp"))
  {
    if (strcmp(name, data->name))
    {
      snprintf(message, sizeof(message), "R ==== ERROR: Expected name \"%s\", got \"%s\" ====", data->name, name);
      cupsArrayAdd(data->messages, message);
    }
    else if (port != 54321)
    {
      snprintf(message, sizeof(message), "R ==== ERROR: Expected port 54321, got %u ====", port);
      cupsArrayAdd(data->messages, message);
    }
    else
    {
      data->resolve_count ++;
    }
  }

  cupsMutexUnlock(&data->mutex);
}


//
// 'service_cb()' - Record service registration callback usage.
//

static void
service_cb(
    cups_dnssd_service_t *service,	// I - Service registration
    void                 *cb_data,	// I - Callback data
    cups_dnssd_flags_t   flags)		// I - Flags
{
  testdata_t	*data = (testdata_t *)cb_data;
					// Test data
  char		message[1024];		// Message string


  snprintf(message, sizeof(message), "S flags=%02X name=\"%s\"", flags, cupsDNSSDServiceGetName(service));

  cupsMutexLock(&data->mutex);
  cupsArrayAdd(data->messages, message);
  data->service_count ++;
  cupsMutexUnlock(&data->mutex);
}


//
// 'usage()' - Show program usage.
//

static void
usage(const char *arg)			// I - Argument for usage message
{
  if (arg)
    printf("testdnssd: Unknown option \"%s\".\n", arg);

  puts("Usage:");

  exit(arg != NULL);
}
