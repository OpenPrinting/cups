//
// DNS-SD discovery backend for CUPS.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright © 2008-2018 by Apple Inc.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "backend-private.h"
#include <cups/array.h>
#include <cups/dnssd.h>


//
// Device structure...
//

typedef enum
{
  CUPS_DEVICE_PRINTER = 0,		// lpd://...
  CUPS_DEVICE_IPPS,			// ipps://...
  CUPS_DEVICE_IPP,			// ipp://...
  CUPS_DEVICE_FAX_IPP,			// ipp://...
  CUPS_DEVICE_PDL_DATASTREAM,		// socket://...
  CUPS_DEVICE_RIOUSBPRINT		// riousbprint://...
} cups_devtype_t;


typedef struct
{
  cups_dnssd_query_t *query;		// Query request
  char		*name,			// Service name
		*domain,		// Domain name
		*fullname,		// Full name
		*make_and_model,	// Make and model from TXT record
		*device_id,		// 1284 device ID from TXT record
		*location,		// Location from TXT record
		*uuid;			// UUID from TXT record
  cups_devtype_t type;			// Device registration type
  int		priority;		// Priority associated with type
  bool		cups_shared,		// CUPS shared printer?
		sent;			// Did we list the device?
} cups_device_t;


//
// Local globals...
//

static cups_array_t	*Devices = NULL;// Found devices
static cups_mutex_t	DevicesMutex = CUPS_MUTEX_INITIALIZER;
					// Mutex for devices array
static int		JobCanceled = 0;// Set to 1 on SIGTERM


//
// Local functions...
//

static void		browse_callback(cups_dnssd_browse_t *browser, void *data, cups_dnssd_flags_t flags, uint32_t if_index, const char *name, const char *regtype, const char *domain);
static int		compare_devices(cups_device_t *a, cups_device_t *b, void *data);
static void		error_cb(void *data, const char *message);
static void		exec_backend(char **argv) _CUPS_NORETURN;
static cups_device_t	*get_device(const char *serviceName, const char *regtype, const char *replyDomain);
static void		query_callback(cups_dnssd_query_t *query, cups_device_t *device, cups_dnssd_flags_t flags, uint32_t if_index, const char *fullname, uint16_t rrtype, const void *qdata, uint16_t qlen);
static void		sigterm_handler(int sig);
static void		unquote(char *dst, const char *src, size_t dstsize) _CUPS_NONNULL(1,2);


//
// 'main()' - Browse for printers.
//

int					// O - Exit status
main(int  argc,				// I - Number of command-line args
     char *argv[])			// I - Command-line arguments
{
  const char	*name;			// Backend name
  cups_device_t	*device;		// Current device
  char		uriName[1024];		// Unquoted fullname for URI
  cups_dnssd_t	*dnssd;			// DNS-SD context
  struct sigaction action;		// Actions for POSIX signals
  time_t	start;			// Start time


  // Don't buffer stderr, and catch SIGTERM...
  setbuf(stderr, NULL);

  memset(&action, 0, sizeof(action));

  sigemptyset(&action.sa_mask);
  action.sa_handler = sigterm_handler;
  sigaction(SIGTERM, &action, NULL);

  // Check command-line...
  if (argc >= 6)
  {
    exec_backend(argv);
  }
  else if (argc != 1)
  {
    _cupsLangPrintf(stderr, _("Usage: %s job-id user title copies options [file]"), argv[0]);
    return (1);
  }

  // Only do discovery when run as "dnssd"...
  if ((name = strrchr(argv[0], '/')) != NULL)
    name ++;
  else
    name = argv[0];

  if (strcmp(name, "dnssd"))
    return (0);

  // Create an array to track devices...
  Devices = cupsArrayNew((cups_array_func_t)compare_devices, /*cb_data*/NULL);

  // Browse for different kinds of printers...
  if ((dnssd = cupsDNSSDNew(error_cb, /*cb_data*/NULL)) == NULL)
    return (1);

  cupsDNSSDBrowseNew(dnssd, CUPS_DNSSD_IF_INDEX_ANY, "_fax-ipp._tcp", /*domain*/NULL, (cups_dnssd_browse_cb_t)browse_callback, /*cb_data*/NULL);
  cupsDNSSDBrowseNew(dnssd, CUPS_DNSSD_IF_INDEX_ANY, "_ipp._tcp", /*domain*/NULL, (cups_dnssd_browse_cb_t)browse_callback, /*cb_data*/NULL);
  cupsDNSSDBrowseNew(dnssd, CUPS_DNSSD_IF_INDEX_ANY, "_ipp-tls._tcp", /*domain*/NULL, (cups_dnssd_browse_cb_t)browse_callback, /*cb_data*/NULL);
  cupsDNSSDBrowseNew(dnssd, CUPS_DNSSD_IF_INDEX_ANY, "_ipps._tcp", /*domain*/NULL, (cups_dnssd_browse_cb_t)browse_callback, /*cb_data*/NULL);
  cupsDNSSDBrowseNew(dnssd, CUPS_DNSSD_IF_INDEX_ANY, "_pdl-datastream._tcp", /*domain*/NULL, (cups_dnssd_browse_cb_t)browse_callback, /*cb_data*/NULL);
  cupsDNSSDBrowseNew(dnssd, CUPS_DNSSD_IF_INDEX_ANY, "_printer._tcp", /*domain*/NULL, (cups_dnssd_browse_cb_t)browse_callback, /*cb_data*/NULL);
  cupsDNSSDBrowseNew(dnssd, CUPS_DNSSD_IF_INDEX_ANY, "_riousbprint._tcp", /*domain*/NULL, (cups_dnssd_browse_cb_t)browse_callback, /*cb_data*/NULL);

  // Loop until we are killed...
  start = time(NULL);

  while (!JobCanceled)
  {
    sleep(1);

    cupsMutexLock(&DevicesMutex);

    if (cupsArrayGetCount(Devices) > 0)
    {
      // Announce any devices we've found...
      cups_device_t *best;		// Best matching device
      char	device_uri[1024];	// Device URI
      int	count;			// Number of queries
      int	sent;			// Number of sent

      for (device = (cups_device_t *)cupsArrayGetFirst(Devices), best = NULL, count = 0, sent = 0; device; device = (cups_device_t *)cupsArrayGetNext(Devices))
      {
        if (device->sent)
	  sent ++;

        if (device->query)
	  count ++;

        if (!device->query && !device->sent)
	{
	  // Found the device, now get the TXT record(s) for it...
          if (count < 50)
	  {
	    fprintf(stderr, "DEBUG: Querying \"%s\"...\n", device->fullname);

            if ((device->query = cupsDNSSDQueryNew(dnssd, CUPS_DNSSD_IF_INDEX_ANY, device->fullname, CUPS_DNSSD_RRTYPE_TXT, (cups_dnssd_query_cb_t)query_callback, device)) != NULL)
	      count ++;
          }
	}
	else if (!device->sent)
	{
	  // Got the TXT records, now report the device...
	  cupsDNSSDQueryDelete(device->query);
	  device->query = NULL;

          if (!best)
          {
	    best = device;
	  }
	  else if (_cups_strcasecmp(best->name, device->name) || _cups_strcasecmp(best->domain, device->domain))
          {
	    unquote(uriName, best->fullname, sizeof(uriName));

            if (best->uuid)
	      httpAssembleURIf(HTTP_URI_CODING_ALL, device_uri, sizeof(device_uri), "dnssd", /*userpass*/NULL, uriName, /*port*/0, best->cups_shared ? "/cups?uuid=%s" : "/?uuid=%s", best->uuid);
	    else
	      httpAssembleURI(HTTP_URI_CODING_ALL, device_uri, sizeof(device_uri), "dnssd", /*userpass*/NULL, uriName, /*port*/0, best->cups_shared ? "/cups" : "/");

	    cupsBackendReport("network", device_uri, best->make_and_model, best->name, best->device_id, best->location);
	    best->sent = true;
	    best       = device;

	    sent ++;
	  }
	  else if (best->priority > device->priority || (best->priority == device->priority && best->type < device->type))
          {
	    best->sent = true;
	    best       = device;

	    sent ++;
	  }
	  else
	  {
	    device->sent = true;

	    sent ++;
	  }
        }
      }

      if (best)
      {
	unquote(uriName, best->fullname, sizeof(uriName));

	if (best->uuid)
	  httpAssembleURIf(HTTP_URI_CODING_ALL, device_uri, sizeof(device_uri), "dnssd", /*userpass*/NULL, uriName, /*port*/0, best->cups_shared ? "/cups?uuid=%s" : "/?uuid=%s", best->uuid);
	else
	  httpAssembleURI(HTTP_URI_CODING_ALL, device_uri, sizeof(device_uri), "dnssd", /*userpass*/NULL, uriName, /*port*/0, best->cups_shared ? "/cups" : "/");

	cupsBackendReport("network", device_uri, best->make_and_model, best->name, best->device_id, best->location);
	best->sent = true;
	sent ++;
      }

      fprintf(stderr, "DEBUG: sent=%d, count=%d\n", sent, count);

      bool done = sent == cupsArrayGetCount(Devices) && (time(NULL) - start) > 5;

      if (done)
      {
	cupsMutexUnlock(&DevicesMutex);
	break;
      }
    }

    cupsMutexUnlock(&DevicesMutex);
  }

  return (CUPS_BACKEND_OK);
}


//
// 'browse_callback()' - Browse devices.
//

static void
browse_callback(
    cups_dnssd_browse_t *browser,	// I - Service browser
    void                *data,		// I - Callback data (unused)
    cups_dnssd_flags_t  flags,		// I - Browse flags
    uint32_t            if_index,	// I - Interface index
    const char          *name,		// I - Service name
    const char          *regtype,	// I - Service type
    const char          *domain)	// I - Domain
{
  fprintf(stderr, "DEBUG2: browse_callback(browser=%p, data=%p, flags=%x, if_index==%u, name=\"%s\", regtype=\"%s\", domain=\"%s\")\n", (void *)browser, data, flags, if_index, name, regtype, domain);

  // Only process "add" data...
  if (!(flags & CUPS_DNSSD_FLAGS_ADD))
    return;

  // Get the device...
  get_device(name, regtype, domain);
}


//
// 'compare_devices()' - Compare two devices.
//

static int				// O - Result of comparison
compare_devices(cups_device_t *a,	// I - First device
                cups_device_t *b,	// I - Second device
                void          *data)	// I - Callback data (unused)
{
  (void)data;

  return (strcmp(a->name, b->name));
}


//
// 'error_cb()' - Log an error message.
//

static void
error_cb(void       *data,		// I - Callback data (unused)
         const char *message)		// I - Error message
{
  (void)data;

  fprintf(stderr, "ERROR: %s\n", message);
}


//
// 'exec_backend()' - Execute the backend that corresponds to the
//                    resolved service name.
//

static void
exec_backend(char **argv)		// I - Command-line arguments
{
  const char	*resolved_uri,		// Resolved device URI
		*cups_serverbin;	// Location of programs
  char		scheme[1024],		// Scheme from URI
		*ptr,			// Pointer into scheme
		filename[1024];		// Backend filename


  // Resolve the device URI...
  JobCanceled = -1;

  while ((resolved_uri = cupsBackendDeviceURI(argv)) == NULL)
  {
    _cupsLangPrintFilter(stderr, "INFO", _("Unable to locate printer."));
    sleep(10);

    if (getenv("CLASS") != NULL)
      exit(CUPS_BACKEND_FAILED);
  }

 /*
  * Extract the scheme from the URI...
  */

  cupsCopyString(scheme, resolved_uri, sizeof(scheme));
  if ((ptr = strchr(scheme, ':')) != NULL)
    *ptr = '\0';

 /*
  * Get the filename of the backend...
  */

  if ((cups_serverbin = getenv("CUPS_SERVERBIN")) == NULL)
    cups_serverbin = CUPS_SERVERBIN;

  snprintf(filename, sizeof(filename), "%s/backend/%s", cups_serverbin, scheme);

 /*
  * Overwrite the device URI and run the new backend...
  */

  setenv("DEVICE_URI", resolved_uri, 1);

  argv[0] = (char *)resolved_uri;

  fprintf(stderr, "DEBUG: Executing backend \"%s\"...\n", filename);

  execv(filename, argv);

  fprintf(stderr, "ERROR: Unable to execute backend \"%s\": %s\n", filename,
          strerror(errno));
  exit(CUPS_BACKEND_STOP);
}


//
// 'device_type()' - Get DNS-SD type enumeration from string.
//

static cups_devtype_t			// O - Device type
device_type(const char *regtype)	// I - Service registration type
{
#ifdef HAVE_AVAHI
  if (!strcmp(regtype, "_ipp._tcp"))
    return (CUPS_DEVICE_IPP);
  else if (!strcmp(regtype, "_ipps._tcp") ||
	   !strcmp(regtype, "_ipp-tls._tcp"))
    return (CUPS_DEVICE_IPPS);
  else if (!strcmp(regtype, "_fax-ipp._tcp"))
    return (CUPS_DEVICE_FAX_IPP);
  else if (!strcmp(regtype, "_printer._tcp"))
    return (CUPS_DEVICE_PDL_DATASTREAM);
#else
  if (!strcmp(regtype, "_ipp._tcp."))
    return (CUPS_DEVICE_IPP);
  else if (!strcmp(regtype, "_ipps._tcp.") ||
	   !strcmp(regtype, "_ipp-tls._tcp."))
    return (CUPS_DEVICE_IPPS);
  else if (!strcmp(regtype, "_fax-ipp._tcp."))
    return (CUPS_DEVICE_FAX_IPP);
  else if (!strcmp(regtype, "_printer._tcp."))
    return (CUPS_DEVICE_PRINTER);
  else if (!strcmp(regtype, "_pdl-datastream._tcp."))
    return (CUPS_DEVICE_PDL_DATASTREAM);
#endif // HAVE_AVAHI

  return (CUPS_DEVICE_RIOUSBPRINT);
}


//
// 'get_device()' - Create or update a device.
//

static cups_device_t *			// O - Device
get_device(const char   *name,		// I - Name of service/device
           const char   *regtype,	// I - Type of service
           const char   *domain)	// I - Service domain
{
  cups_device_t	key,			// Search key
		*device;		// Device
  char		fullname[1024];		// Full name for query


  // See if this is a new device...
  cupsMutexLock(&DevicesMutex);

  key.name = (char *)name;
  key.type = device_type(regtype);

  for (device = cupsArrayFind(Devices, &key); device; device = cupsArrayGetNext(Devices))
  {
    if (_cups_strcasecmp(device->name, key.name))
    {
      // Out of matches...
      break;
    }
    else if (device->type == key.type)
    {
      // Match!
      if (!_cups_strcasecmp(device->domain, "local.") && _cups_strcasecmp(device->domain, domain))
      {
        // Update the .local listing to use the "global" domain name instead.
	// The backend will try local lookups first, then the global domain name.
        free(device->domain);
	device->domain = strdup(domain);

        cupsDNSSDAssembleFullName(fullname, sizeof(fullname), name, regtype, domain);
	free(device->fullname);
	device->fullname = strdup(fullname);
      }

      cupsMutexUnlock(&DevicesMutex);

      return (device);
    }
  }

  // New device, add it...
  if ((device = calloc(1, sizeof(cups_device_t))) == NULL)
  {
    perror("DEBUG: Out of memory adding a device");
    return (NULL);
  }

  device->name     = strdup(name);
  device->domain   = strdup(domain);
  device->type     = key.type;
  device->priority = 50;

  cupsArrayAdd(Devices, device);

  // Set the "full name" of this service, which is used for queries...
  cupsDNSSDAssembleFullName(fullname, sizeof(fullname), name, regtype, domain);
  device->fullname = strdup(fullname);

  cupsMutexUnlock(&DevicesMutex);

  return (device);
}


//
// 'query_callback()' - Process query data.
//

static void
query_callback(
    cups_dnssd_query_t *query,		// I - Query request
    cups_device_t      *device,		// I - Device
    cups_dnssd_flags_t flags,		// I - Query flags
    uint32_t           if_index,	// I - Response interface index
    const char         *fullname,	// I - Fullname
    uint16_t           rrtype,		// I - RR type (TXT)
    const void         *qdata,		// I - Data
    uint16_t           qlen)		// I - Length of data
{
  char		*ptr;			// Pointer into string
  const uint8_t	*data,			// Pointer into data
		*datanext,		// Next key/value pair
		*dataend;		// End of entire TXT record
  uint8_t	datalen;		// Length of current key/value pair
  char		key[256],		// Key string
		value[256],		// Value string
		make_and_model[512],	// Manufacturer and model
		model[256],		// Model
		pdl[256],		// PDL
		device_id[2048];	// 1284 device ID


  fprintf(stderr, "DEBUG2: query_callback(query=%p, device=%p, flags=%x, if_index=%u, fullname=\"%s\", rrtype=%u, qdata=%p, qlen=%u)\n", (void *)query, (void *)device, flags, if_index, fullname, rrtype, qdata, qlen);

  // Only process "add" data...
  if (!(flags & CUPS_DNSSD_FLAGS_ADD))
    return;

  // Pull out the priority, location, make and model, and pdl list from the TXT
  // record and save it...
  device_id[0]      = '\0';
  make_and_model[0] = '\0';
  pdl[0]            = '\0';

  cupsCopyString(model, "Unknown", sizeof(model));

  for (data = qdata, dataend = data + qlen; data < dataend; data = datanext)
  {
    // Read a key/value pair starting with an 8-bit length.  Since the
    // length is 8 bits and the size of the key/value buffers is 256, we
    // don't need to check for overflow...
    datalen = *data++;

    if (!datalen || (data + datalen) > dataend)
      break;

    datanext = data + datalen;

    for (ptr = key; data < datanext && *data != '='; data ++)
      *ptr++ = (char)*data;
    *ptr = '\0';

    if (data < datanext && *data == '=')
    {
      data ++;

      if (data < datanext)
	memcpy(value, data, (size_t)(datanext - data));
      value[datanext - data] = '\0';

      fprintf(stderr, "DEBUG2: query_callback: \"%s=%s\".\n", key, value);
    }
    else
    {
      fprintf(stderr, "DEBUG2: query_callback: \"%s\" with no value.\n", key);
      continue;
    }

    if (!_cups_strncasecmp(key, "usb_", 4))
    {
      // Add USB device ID information...
      ptr = device_id + strlen(device_id);
      snprintf(ptr, sizeof(device_id) - (size_t)(ptr - device_id), "%s:%s;", key + 4, value);
    }

    if (!_cups_strcasecmp(key, "usb_MFG") || !_cups_strcasecmp(key, "usb_MANU") || !_cups_strcasecmp(key, "usb_MANUFACTURER"))
    {
      cupsCopyString(make_and_model, value, sizeof(make_and_model));
    }
    else if (!_cups_strcasecmp(key, "usb_MDL") || !_cups_strcasecmp(key, "usb_MODEL"))
    {
      cupsCopyString(model, value, sizeof(model));
    }
    else if (!_cups_strcasecmp(key, "product") && !strstr(value, "Ghostscript"))
    {
      if (value[0] == '(')
      {
        // Strip parenthesis...
	if ((ptr = value + strlen(value) - 1) > value && *ptr == ')')
	  *ptr = '\0';

	cupsCopyString(model, value + 1, sizeof(model));
      }
      else
      {
	cupsCopyString(model, value, sizeof(model));
      }
    }
    else if (!_cups_strcasecmp(key, "ty"))
    {
      cupsCopyString(model, value, sizeof(model));

      if ((ptr = strchr(model, ',')) != NULL)
	*ptr = '\0';
    }
    else if (!_cups_strcasecmp(key, "pdl"))
    {
      cupsCopyString(pdl, value, sizeof(pdl));
    }
    else if (!_cups_strcasecmp(key, "priority"))
    {
      device->priority = atoi(value);
    }
    else if ((device->type == CUPS_DEVICE_IPP || device->type == CUPS_DEVICE_IPPS || device->type == CUPS_DEVICE_PRINTER) && !_cups_strcasecmp(key, "printer-type"))
    {
      // This is a CUPS printer.
      device->cups_shared = true;

      if (device->type == CUPS_DEVICE_PRINTER)
	device->sent = true;
    }
    else if (!_cups_strcasecmp(key, "note") && value[0])
    {
      device->location = strdup(value);
    }
    else if (!_cups_strcasecmp(key, "UUID"))
    {
      device->uuid = strdup(value);
    }
  }

  if (device->device_id)
    free(device->device_id);

  if (!device_id[0] && strcmp(model, "Unknown"))
  {
    if (make_and_model[0])
    {
      snprintf(device_id, sizeof(device_id), "MFG:%s;MDL:%s;", make_and_model, model);
    }
    else if (!_cups_strncasecmp(model, "designjet ", 10))
    {
      snprintf(device_id, sizeof(device_id), "MFG:HP;MDL:%s;", model + 10);
    }
    else if (!_cups_strncasecmp(model, "stylus ", 7))
    {
      snprintf(device_id, sizeof(device_id), "MFG:EPSON;MDL:%s;", model + 7);
    }
    else if ((ptr = strchr(model, ' ')) != NULL)
    {
      // Assume the first word is the make...
      memcpy(make_and_model, model, (size_t)(ptr - model));
      make_and_model[ptr - model] = '\0';

      snprintf(device_id, sizeof(device_id), "MFG:%s;MDL:%s;", make_and_model, ptr + 1);
    }
  }

  if (device_id[0] && !strstr(device_id, "CMD:") && !strstr(device_id, "COMMAND SET:") && (strstr(pdl, "application/pdf") || strstr(pdl, "application/postscript") || strstr(pdl, "application/vnd.hp-PCL") || strstr(pdl, "image/")))
  {
    value[0] = '\0';
    if (strstr(pdl, "application/pdf"))
      cupsConcatString(value, ",PDF", sizeof(value));
    if (strstr(pdl, "application/postscript"))
      cupsConcatString(value, ",PS", sizeof(value));
    if (strstr(pdl, "application/vnd.hp-PCL"))
      cupsConcatString(value, ",PCL", sizeof(value));
    for (ptr = strstr(pdl, "image/"); ptr; ptr = strstr(ptr, "image/"))
    {
      char *valptr = value + strlen(value);
      					// Pointer into value

      if (valptr < (value + sizeof(value) - 1))
        *valptr++ = ',';

      ptr += 6;
      while (isalnum(*ptr & 255) || *ptr == '-' || *ptr == '.')
      {
        if (isalnum(*ptr & 255) && valptr < (value + sizeof(value) - 1))
          *valptr++ = (char)toupper(*ptr++ & 255);
        else
          break;
      }

      *valptr = '\0';
    }

    ptr = device_id + strlen(device_id);
    snprintf(ptr, sizeof(device_id) - (size_t)(ptr - device_id), "CMD:%s;", value + 1);
  }

  if (device_id[0])
    device->device_id = strdup(device_id);
  else
    device->device_id = NULL;

  if (device->make_and_model)
    free(device->make_and_model);

  if (make_and_model[0])
  {
    cupsConcatString(make_and_model, " ", sizeof(make_and_model));
    cupsConcatString(make_and_model, model, sizeof(make_and_model));

    if (!_cups_strncasecmp(make_and_model, "EPSON EPSON ", 12))
      _cups_strcpy(make_and_model, make_and_model + 6);
    else if (!_cups_strncasecmp(make_and_model, "HP HP ", 6))
      _cups_strcpy(make_and_model, make_and_model + 3);
    else if (!_cups_strncasecmp(make_and_model, "Lexmark International Lexmark ", 30))
      _cups_strcpy(make_and_model, make_and_model + 22);

    device->make_and_model = strdup(make_and_model);
  }
  else
  {
    device->make_and_model = strdup(model);
  }
}


//
// 'sigterm_handler()' - Handle termination signals.
//

static void
sigterm_handler(int sig)		// I - Signal number (unused)
{
  (void)sig;

  if (JobCanceled)
    _exit(CUPS_BACKEND_OK);
  else
    JobCanceled = 1;
}


//
// 'unquote()' - Unquote a name string.
//

static void
unquote(char       *dst,		// I - Destination buffer
        const char *src,		// I - Source string
	size_t     dstsize)		// I - Size of destination buffer
{
  char	*dstend = dst + dstsize - 1;	// End of destination buffer


  while (*src && dst < dstend)
  {
    if (*src == '\\')
    {
      src ++;
      if (isdigit(src[0] & 255) && isdigit(src[1] & 255) && isdigit(src[2] & 255))
      {
        *dst++ = ((((src[0] - '0') * 10) + src[1] - '0') * 10) + src[2] - '0';
	src += 3;
      }
      else
      {
        *dst++ = *src++;
      }
    }
    else
    {
      *dst++ = *src ++;
    }
  }

  *dst = '\0';
}
