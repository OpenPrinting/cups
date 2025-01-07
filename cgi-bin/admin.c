/*
 * Administration CGI for CUPS.
 *
 * Copyright © 2021-2024 by OpenPrinting
 * Copyright © 2007-2021 by Apple Inc.
 * Copyright © 1997-2007 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#include "cgi-private.h"
#include <cups/http-private.h>
#include <cups/ppd-private.h>
#include <cups/adminutil.h>
#include <cups/ppd.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <limits.h>


/*
 * Local globals...
 */

static int	current_device = 0;	/* Current device shown */


/*
 * Local functions...
 */

static void	choose_device_cb(const char *device_class, const char *device_id, const char *device_info, const char *device_make_and_model, const char *device_uri, const char *device_location, const char *title);
static void	do_am_class(http_t *http, int modify);
static void	do_am_printer(http_t *http, int modify);
static void	do_config_server(http_t *http);
static void	do_delete_class(http_t *http);
static void	do_delete_printer(http_t *http);
static void	do_list_printers(http_t *http);
static void	do_menu(http_t *http);
static void	do_set_allowed_users(http_t *http);
static void	do_set_default(http_t *http);
static void	do_set_options(http_t *http, int is_class);
static char	*get_option_value(ppd_file_t *ppd, const char *name,
		                  char *buffer, size_t bufsize);
static double	get_points(double number, const char *uval);


/*
 * 'main()' - Main entry for CGI.
 */

int					/* O - Exit status */
main(void)
{
  http_t	*http;			/* Connection to the server */
  const char	*op;			/* Operation name */


 /*
  * Connect to the HTTP server...
  */

  fputs("DEBUG: admin.cgi started...\n", stderr);

  http = httpConnectEncrypt(cupsServer(), ippPort(), cupsEncryption());

  if (!http)
  {
    perror("ERROR: Unable to connect to cupsd");
    fprintf(stderr, "DEBUG: cupsServer()=\"%s\"\n",
            cupsServer() ? cupsServer() : "(null)");
    fprintf(stderr, "DEBUG: ippPort()=%d\n", ippPort());
    fprintf(stderr, "DEBUG: cupsEncryption()=%d\n", cupsEncryption());
    exit(1);
  }

  fprintf(stderr, "DEBUG: http=%p\n", http);

 /*
  * Set the web interface section...
  */

  cgiSetVariable("SECTION", "admin");
  cgiSetVariable("REFRESH_PAGE", "");

 /*
  * See if we have form data...
  */

  if (!cgiInitialize() || !cgiGetVariable("OP"))
  {
   /*
    * Nope, send the administration menu...
    */

    fputs("DEBUG: No form data, showing main menu...\n", stderr);

    do_menu(http);
  }
  else if ((op = cgiGetVariable("OP")) != NULL && cgiIsPOST())
  {
   /*
    * Do the operation...
    */

    fprintf(stderr, "DEBUG: op=\"%s\"...\n", op);

    if (!*op)
    {
      const char *printer = getenv("PRINTER_NAME"),
					/* Printer or class name */
		*server_port = getenv("SERVER_PORT");
					/* Port number string */
      int	port = server_port ? atoi(server_port) : 0;
      					/* Port number */
      char	uri[1024];		/* URL */

      if (printer)
        httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri),
	                 getenv("HTTPS") ? "https" : "http", NULL,
			 getenv("SERVER_NAME"), port, "/%s/%s",
			 cgiGetVariable("IS_CLASS") ? "classes" : "printers",
			 printer);
      else
        httpAssembleURI(HTTP_URI_CODING_ALL, uri, sizeof(uri),
	                getenv("HTTPS") ? "https" : "http", NULL,
			getenv("SERVER_NAME"), port, "/admin");

      printf("Location: %s\n\n", uri);
    }
    else if (!strcmp(op, "set-allowed-users"))
      do_set_allowed_users(http);
    else if (!strcmp(op, "set-as-default"))
      do_set_default(http);
    else if (!strcmp(op, "find-new-printers") ||
             !strcmp(op, "list-available-printers"))
      do_list_printers(http);
    else if (!strcmp(op, "add-class"))
      do_am_class(http, 0);
    else if (!strcmp(op, "add-printer"))
      do_am_printer(http, 0);
    else if (!strcmp(op, "modify-class"))
      do_am_class(http, 1);
    else if (!strcmp(op, "modify-printer"))
      do_am_printer(http, 1);
    else if (!strcmp(op, "delete-class"))
      do_delete_class(http);
    else if (!strcmp(op, "delete-printer"))
      do_delete_printer(http);
    else if (!strcmp(op, "set-class-options"))
      do_set_options(http, 1);
    else if (!strcmp(op, "set-printer-options"))
      do_set_options(http, 0);
    else if (!strcmp(op, "config-server"))
      do_config_server(http);
    else
    {
     /*
      * Bad operation code - display an error...
      */

      cgiStartHTML(cgiText(_("Administration")));
      cgiCopyTemplateLang("error-op.tmpl");
      cgiEndHTML();
    }
  }
  else if (op && !strcmp(op, "redirect"))
  {
    const char	*url;			/* Redirection URL... */
    char	prefix[1024];		/* URL prefix */


    if (getenv("HTTPS"))
      snprintf(prefix, sizeof(prefix), "https://%s:%s",
	       getenv("SERVER_NAME"), getenv("SERVER_PORT"));
    else
      snprintf(prefix, sizeof(prefix), "http://%s:%s",
	       getenv("SERVER_NAME"), getenv("SERVER_PORT"));

    fprintf(stderr, "DEBUG: redirecting with prefix %s!\n", prefix);

    if ((url = cgiGetVariable("URL")) != NULL)
    {
      char	encoded[1024],		/* Encoded URL string */
      		*ptr;			/* Pointer into encoded string */


      ptr = encoded;
      if (*url != '/')
        *ptr++ = '/';

      for (; *url && ptr < (encoded + sizeof(encoded) - 4); url ++)
      {
        if (strchr("%@&+ <>#=", *url) || *url < ' ' || *url & 128)
	{
	 /*
	  * Percent-encode this character; safe because we have at least 4
	  * bytes left in the array...
	  */

	  snprintf(ptr, sizeof(encoded) - (size_t)(ptr - encoded), "%%%02X", *url & 255);
	  ptr += 3;
	}
	else
	  *ptr++ = *url;
      }

      *ptr = '\0';

      if (*url)
      {
       /*
        * URL was too long, just redirect to the admin page...
	*/

	printf("Location: %s/admin\n\n", prefix);
      }
      else
      {
       /*
        * URL is OK, redirect there...
	*/

        printf("Location: %s%s\n\n", prefix, encoded);
      }
    }
    else
      printf("Location: %s/admin\n\n", prefix);
  }
  else
  {
   /*
    * Form data but no operation code - display an error...
    */

    cgiStartHTML(cgiText(_("Administration")));
    cgiCopyTemplateLang("error-op.tmpl");
    cgiEndHTML();
  }

 /*
  * Close the HTTP server connection...
  */

  httpClose(http);

 /*
  * Return with no errors...
  */

  return (0);
}


/*
 * 'choose_device_cb()' - Add a device to the device selection page.
 */

static void
choose_device_cb(
    const char *device_class,		/* I - Class */
    const char *device_id,		/* I - 1284 device ID */
    const char *device_info,		/* I - Description */
    const char *device_make_and_model,	/* I - Make and model */
    const char *device_uri,		/* I - Device URI */
    const char *device_location,	/* I - Location */
    const char *title)			/* I - Page title */
{
 /*
  * For modern browsers, start a multi-part page so we can show that something
  * is happening.  Non-modern browsers just get everything at the end...
  */

  if (current_device == 0 && cgiSupportsMultipart())
  {
    cgiStartMultipart();
    cgiStartHTML(title);
    cgiCopyTemplateLang("choose-device.tmpl");
    cgiEndHTML();
    fflush(stdout);
  }


 /*
  * Add the device to the array...
  */

  cgiSetArray("device_class", current_device, device_class);
  cgiSetArray("device_id", current_device, device_id);
  cgiSetArray("device_info", current_device, device_info);
  cgiSetArray("device_make_and_model", current_device, device_make_and_model);
  cgiSetArray("device_uri", current_device, device_uri);
  cgiSetArray("device_location", current_device, device_location);

  current_device ++;
}


/*
 * 'do_am_class()' - Add or modify a class.
 */

static void
do_am_class(http_t *http,		/* I - HTTP connection */
	    int    modify)		/* I - Modify the printer? */
{
  int		i, j;			/* Looping vars */
  int		element;		/* Element number */
  int		num_printers;		/* Number of printers */
  ipp_t		*request,		/* IPP request */
		*response;		/* IPP response */
  ipp_attribute_t *attr;		/* member-uris attribute */
  char		uri[HTTP_MAX_URI];	/* Device or printer URI */
  const char	*name,			/* Pointer to class name */
		*op,			/* Operation name */
		*ptr;			/* Pointer to CGI variable */
  const char	*title;			/* Title of page */
  static const char * const pattrs[] =	/* Requested printer attributes */
		{
		  "member-names",
		  "printer-info",
		  "printer-location"
		};


  title = cgiText(modify ? _("Modify Class") : _("Add Class"));
  op    = cgiGetVariable("OP");
  name  = cgiGetTextfield("PRINTER_NAME");

  if (cgiGetTextfield("PRINTER_LOCATION") == NULL)
  {
   /*
    * Build a CUPS_GET_PRINTERS request, which requires the
    * following attributes:
    *
    *    attributes-charset
    *    attributes-natural-language
    */

    request = ippNewRequest(CUPS_GET_PRINTERS);

    ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_ENUM, "printer-type",
		  CUPS_PRINTER_LOCAL);
    ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_ENUM, "printer-type-mask",
		  CUPS_PRINTER_CLASS | CUPS_PRINTER_REMOTE);

   /*
    * Do the request and get back a response...
    */

    cgiClearVariables();
    if (op)
      cgiSetVariable("OP", op);
    if (name)
      cgiSetVariable("PRINTER_NAME", name);

    if ((response = cupsDoRequest(http, request, "/")) != NULL)
    {
     /*
      * Create MEMBER_URIS and MEMBER_NAMES arrays...
      */

      for (element = 0, attr = response->attrs;
	   attr != NULL;
	   attr = attr->next)
	if (attr->name && !strcmp(attr->name, "printer-uri-supported"))
	{
	  if ((ptr = strrchr(attr->values[0].string.text, '/')) != NULL &&
	      (!name || _cups_strcasecmp(name, ptr + 1)))
	  {
	   /*
	    * Don't show the current class...
	    */

	    cgiSetArray("MEMBER_URIS", element, attr->values[0].string.text);
	    element ++;
	  }
	}

      for (element = 0, attr = response->attrs;
	   attr != NULL;
	   attr = attr->next)
	if (attr->name && !strcmp(attr->name, "printer-name"))
	{
	  if (!name || _cups_strcasecmp(name, attr->values[0].string.text))
	  {
	   /*
	    * Don't show the current class...
	    */

	    cgiSetArray("MEMBER_NAMES", element, attr->values[0].string.text);
	    element ++;
	  }
	}

      num_printers = cgiGetSize("MEMBER_URIS");

      ippDelete(response);
    }
    else
      num_printers = 0;

    if (modify)
    {
     /*
      * Build an IPP_GET_PRINTER_ATTRIBUTES request, which requires the
      * following attributes:
      *
      *    attributes-charset
      *    attributes-natural-language
      *    printer-uri
      */

      request = ippNewRequest(IPP_GET_PRINTER_ATTRIBUTES);

      httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                       "localhost", 0, "/classes/%s", name);
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
                   NULL, uri);

      ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                    "requested-attributes",
		    (int)(sizeof(pattrs) / sizeof(pattrs[0])),
		    NULL, pattrs);

     /*
      * Do the request and get back a response...
      */

      if ((response = cupsDoRequest(http, request, "/")) != NULL)
      {
	if ((attr = ippFindAttribute(response, "member-names",
	                             IPP_TAG_NAME)) != NULL)
	{
	 /*
          * Mark any current members in the class...
	  */

          for (j = 0; j < num_printers; j ++)
	    cgiSetArray("MEMBER_SELECTED", j, "");

          for (i = 0; i < attr->num_values; i ++)
	  {
	    for (j = 0; j < num_printers; j ++)
	    {
	      if (!_cups_strcasecmp(attr->values[i].string.text,
	                      cgiGetArray("MEMBER_NAMES", j)))
	      {
		cgiSetArray("MEMBER_SELECTED", j, "SELECTED");
		break;
	      }
            }
          }
	}

	if ((attr = ippFindAttribute(response, "printer-info",
	                             IPP_TAG_TEXT)) != NULL)
	  cgiSetVariable("PRINTER_INFO", attr->values[0].string.text);

	if ((attr = ippFindAttribute(response, "printer-location",
	                             IPP_TAG_TEXT)) != NULL)
	  cgiSetVariable("PRINTER_LOCATION", attr->values[0].string.text);

	ippDelete(response);
      }

     /*
      * Update the location and description of an existing printer...
      */

      cgiStartHTML(title);
      cgiCopyTemplateLang("modify-class.tmpl");
    }
    else
    {
     /*
      * Get the name, location, and description for a new printer...
      */

      cgiStartHTML(title);
      cgiCopyTemplateLang("add-class.tmpl");
    }

    cgiEndHTML();

    return;
  }

  if (!name)
  {
    cgiStartHTML(title);
    cgiSetVariable("ERROR", cgiText(_("Missing form variable")));
    cgiCopyTemplateLang("error.tmpl");
    cgiEndHTML();
    return;
  }

  for (ptr = name; *ptr; ptr ++)
    if ((*ptr >= 0 && *ptr <= ' ') || *ptr == 127 || *ptr == '/' || *ptr == '#')
      break;

  if (*ptr || ptr == name || strlen(name) > 127)
  {
    cgiSetVariable("ERROR",
                   cgiText(_("The class name may only contain up to "
			     "127 printable characters and may not "
			     "contain spaces, slashes (/), or the "
			     "pound sign (#).")));
    cgiStartHTML(title);
    cgiCopyTemplateLang("error.tmpl");
    cgiEndHTML();
    return;
  }

 /*
  * Build a CUPS_ADD_CLASS request, which requires the following
  * attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  *    printer-uri
  *    printer-location
  *    printer-info
  *    printer-is-accepting-jobs
  *    printer-state
  *    member-uris
  */

  request = ippNewRequest(CUPS_ADD_CLASS);

  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                   "localhost", 0, "/classes/%s", name);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
               NULL, uri);

  ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-location",
               NULL, cgiGetTextfield("PRINTER_LOCATION"));

  ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-info",
               NULL, cgiGetTextfield("PRINTER_INFO"));

  ippAddBoolean(request, IPP_TAG_PRINTER, "printer-is-accepting-jobs", 1);

  ippAddInteger(request, IPP_TAG_PRINTER, IPP_TAG_ENUM, "printer-state",
                IPP_PRINTER_IDLE);

  if ((num_printers = cgiGetSize("MEMBER_URIS")) > 0)
  {
    attr = ippAddStrings(request, IPP_TAG_PRINTER, IPP_TAG_URI, "member-uris",
                         num_printers, NULL, NULL);
    for (i = 0; i < num_printers; i ++)
      ippSetString(request, &attr, i, cgiGetArray("MEMBER_URIS", i));
  }

 /*
  * Do the request and get back a response...
  */

  ippDelete(cupsDoRequest(http, request, "/admin/"));

  if (cupsLastError() == IPP_NOT_AUTHORIZED)
  {
    puts("Status: 401\n");
    exit(0);
  }
  else if (cupsLastError() > IPP_OK_CONFLICT)
  {
    cgiStartHTML(title);
    cgiShowIPPError(modify ? _("Unable to modify class") :
                             _("Unable to add class"));
  }
  else
  {
   /*
    * Redirect successful updates back to the class page...
    */

    char	refresh[1024];		/* Refresh URL */

    cgiFormEncode(uri, name, sizeof(uri));
    snprintf(refresh, sizeof(refresh), "5;URL=/admin/?OP=redirect&URL=/classes/%s",
             uri);
    cgiSetVariable("refresh_page", refresh);

    cgiStartHTML(title);

    if (modify)
      cgiCopyTemplateLang("class-modified.tmpl");
    else
      cgiCopyTemplateLang("class-added.tmpl");
  }

  cgiEndHTML();
}


/*
 * 'do_am_printer()' - Add or modify a printer.
 */

static void
do_am_printer(http_t *http,		/* I - HTTP connection */
	      int    modify)		/* I - Modify the printer? */
{
  int		i;			/* Looping var */
  ipp_attribute_t *attr;		/* Current attribute */
  ipp_t		*request,		/* IPP request */
		*response,		/* IPP response */
		*oldinfo;		/* Old printer information */
  const cgi_file_t *file;		/* Uploaded file, if any */
  const char	*var;			/* CGI variable */
  char	*ppd_name = NULL;	/* Pointer to PPD name */
  char		uri[HTTP_MAX_URI],	/* Device or printer URI */
		*uriptr,		/* Pointer into URI */
		evefile[1024] = "";	/* IPP Everywhere PPD file */
  int		maxrate;		/* Maximum baud rate */
  char		baudrate[255];		/* Baud rate string */
  const char	*name,			/* Pointer to class name */
		*ptr;			/* Pointer to CGI variable */
  const char	*title;			/* Title of page */
  static int	baudrates[] =		/* Baud rates */
		{
		  1200,
		  2400,
		  4800,
		  9600,
		  19200,
		  38400,
		  57600,
		  115200,
		  230400,
		  460800
		};


  ptr = cgiGetVariable("DEVICE_URI");
  fprintf(stderr, "DEBUG: do_am_printer: DEVICE_URI=\"%s\"\n",
          ptr ? ptr : "(null)");

  title = cgiText(modify ? _("Modify Printer") : _("Add Printer"));

  if (modify)
  {
   /*
    * Build an IPP_GET_PRINTER_ATTRIBUTES request, which requires the
    * following attributes:
    *
    *    attributes-charset
    *    attributes-natural-language
    *    printer-uri
    */

    request = ippNewRequest(IPP_GET_PRINTER_ATTRIBUTES);

    httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                     "localhost", 0, "/printers/%s",
		     cgiGetTextfield("PRINTER_NAME"));
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
                 NULL, uri);

   /*
    * Do the request and get back a response...
    */

    oldinfo = cupsDoRequest(http, request, "/");
  }
  else
    oldinfo = NULL;

  file = cgiGetFile();

  if (file)
  {
    fprintf(stderr, "DEBUG: file->tempfile=%s\n", file->tempfile);
    fprintf(stderr, "DEBUG: file->name=%s\n", file->name);
    fprintf(stderr, "DEBUG: file->filename=%s\n", file->filename);
    fprintf(stderr, "DEBUG: file->mimetype=%s\n", file->mimetype);
  }

  if ((name = cgiGetTextfield("PRINTER_NAME")) != NULL)
  {
    for (ptr = name; *ptr; ptr ++)
      if ((*ptr >= 0 && *ptr <= ' ') || *ptr == 127 || *ptr == '/' || *ptr == '\\' || *ptr == '?' || *ptr == '\'' || *ptr == '\"' || *ptr == '#')
	break;

    if (*ptr || ptr == name || strlen(name) > 127)
    {
      cgiSetVariable("ERROR",
		     cgiText(_("The printer name may only contain up to 127 printable characters and may not contain spaces, slashes (/ \\), quotes (' \"), question mark (?), or the pound sign (#).")));
      cgiStartHTML(title);
      cgiCopyTemplateLang("error.tmpl");
      cgiEndHTML();
      return;
    }
  }

  if ((var = cgiGetVariable("DEVICE_URI")) != NULL)
  {
    if ((uriptr = strrchr(var, '|')) != NULL)
    {
     /*
      * Extract make and make/model from device URI string...
      */

      char	make[1024],		/* Make string */
		*makeptr;		/* Pointer into make */


      *uriptr++ = '\0';

      strlcpy(make, uriptr, sizeof(make));

      if ((makeptr = strchr(make, ' ')) != NULL)
        *makeptr = '\0';
      else if ((makeptr = strchr(make, '-')) != NULL)
        *makeptr = '\0';
      else if (!_cups_strncasecmp(make, "laserjet", 8) ||
               !_cups_strncasecmp(make, "deskjet", 7) ||
               !_cups_strncasecmp(make, "designjet", 9))
        strlcpy(make, "HP", sizeof(make));
      else if (!_cups_strncasecmp(make, "phaser", 6))
        strlcpy(make, "Xerox", sizeof(make));
      else if (!_cups_strncasecmp(make, "stylus", 6))
        strlcpy(make, "Epson", sizeof(make));
      else
        strlcpy(make, "Generic", sizeof(make));

      if (!cgiGetVariable("CURRENT_MAKE"))
        cgiSetVariable("CURRENT_MAKE", make);

      if (!cgiGetVariable("CURRENT_MAKE_AND_MODEL"))
        cgiSetVariable("CURRENT_MAKE_AND_MODEL", uriptr);

      if (!modify)
      {
        char	template[128],		/* Template name */
		*tptr;			/* Pointer into template name */

	cgiSetVariable("PRINTER_INFO", uriptr);

	for (tptr = template;
	     tptr < (template + sizeof(template) - 1) && *uriptr;
	     uriptr ++)
	  if (isalnum(*uriptr & 255) || *uriptr == '_' || *uriptr == '-' ||
	      *uriptr == '.')
	    *tptr++ = *uriptr;
	  else if ((*uriptr == ' ' || *uriptr == '/') && tptr > template &&
	           tptr[-1] != '_')
	    *tptr++ = '_';
	  else if (*uriptr == '?' || *uriptr == '(')
	    break;

        *tptr = '\0';

        cgiSetVariable("TEMPLATE_NAME", template);
      }

     /*
      * Set DEVICE_URI to the actual device uri, without make and model from
      * html form.
      */

      cgiSetVariable("DEVICE_URI", var);
    }
  }

  if (!var)
  {
   /*
    * Look for devices so the user can pick something...
    */

    if ((attr = ippFindAttribute(oldinfo, "device-uri", IPP_TAG_URI)) != NULL)
    {
      strlcpy(uri, attr->values[0].string.text, sizeof(uri));
      if ((uriptr = strchr(uri, ':')) != NULL && strncmp(uriptr, "://", 3) == 0)
        *uriptr = '\0';

      cgiSetVariable("CURRENT_DEVICE_URI", attr->values[0].string.text);
      cgiSetVariable("CURRENT_DEVICE_SCHEME", uri);
    }

   /*
    * Scan for devices for up to 30 seconds...
    */

    fputs("DEBUG: Getting list of devices...\n", stderr);

    current_device = 0;
    if (cupsGetDevices(http, 5, CUPS_INCLUDE_ALL, CUPS_EXCLUDE_NONE,
                       (cups_device_cb_t)choose_device_cb,
		       (void *)title) == IPP_OK)
    {
      fputs("DEBUG: Got device list!\n", stderr);

      if (cgiSupportsMultipart())
        cgiStartMultipart();

      cgiSetVariable("CUPS_GET_DEVICES_DONE", "1");
      cgiStartHTML(title);
      cgiCopyTemplateLang("choose-device.tmpl");
      cgiEndHTML();

      if (cgiSupportsMultipart())
        cgiEndMultipart();
    }
    else
    {
      fprintf(stderr,
              "ERROR: CUPS-Get-Devices request failed with status %x: %s\n",
	      cupsLastError(), cupsLastErrorString());
      if (cupsLastError() == IPP_NOT_AUTHORIZED)
      {
	puts("Status: 401\n");
	exit(0);
      }
      else
      {
	cgiStartHTML(title);
	cgiShowIPPError(modify ? _("Unable to modify printer") :
				 _("Unable to add printer"));
	cgiEndHTML();
        return;
      }
    }
  }
  else if (!strchr(var, '/') ||
           (!strncmp(var, "lpd://", 6) && !strchr(var + 6, '/')))
  {
    if ((attr = ippFindAttribute(oldinfo, "device-uri", IPP_TAG_URI)) != NULL)
    {
     /*
      * Set the current device URI for the form to the old one...
      */

      if (strncmp(attr->values[0].string.text, var, strlen(var)) == 0)
	cgiSetVariable("CURRENT_DEVICE_URI", attr->values[0].string.text);
    }

   /*
    * User needs to set the full URI...
    */

    cgiStartHTML(title);
    cgiCopyTemplateLang("choose-uri.tmpl");
    cgiEndHTML();
  }
  else if (!strncmp(var, "serial:", 7) && !cgiGetVariable("BAUDRATE"))
  {
   /*
    * Need baud rate, parity, etc.
    */

    if ((var = strchr(var, '?')) != NULL &&
        strncmp(var, "?baud=", 6) == 0)
      maxrate = atoi(var + 6);
    else
      maxrate = 19200;

    for (i = 0; i < (int)(sizeof(baudrates)/sizeof(baudrates[0])); i ++)
      if (baudrates[i] > maxrate)
        break;
      else
      {
        snprintf(baudrate, sizeof(baudrate), "%d", baudrates[i]);
	cgiSetArray("BAUDRATES", i, baudrate);
      }

    cgiStartHTML(title);
    cgiCopyTemplateLang("choose-serial.tmpl");
    cgiEndHTML();
  }
  else if (!name || !cgiGetTextfield("PRINTER_LOCATION"))
  {
    cgiStartHTML(title);

    if (modify)
    {
     /*
      * Update the location and description of an existing printer...
      */

      if (oldinfo)
      {
        if ((attr = ippFindAttribute(oldinfo, "printer-info",
	                             IPP_TAG_TEXT)) != NULL)
          cgiSetVariable("PRINTER_INFO", attr->values[0].string.text);

        if ((attr = ippFindAttribute(oldinfo, "printer-location",
	                             IPP_TAG_TEXT)) != NULL)
          cgiSetVariable("PRINTER_LOCATION", attr->values[0].string.text);

	if ((attr = ippFindAttribute(oldinfo, "printer-is-shared",
				     IPP_TAG_BOOLEAN)) != NULL)
	  cgiSetVariable("PRINTER_IS_SHARED",
			 attr->values[0].boolean ? "1" : "0");
      }

      cgiCopyTemplateLang("modify-printer.tmpl");
    }
    else
    {
     /*
      * Get the name, location, and description for a new printer...
      */

#ifdef __APPLE__
      if (!strncmp(var, "usb:", 4))
        cgiSetVariable("PRINTER_IS_SHARED", "1");
      else
#endif /* __APPLE__ */
        cgiSetVariable("PRINTER_IS_SHARED", "0");

      cgiCopyTemplateLang("add-printer.tmpl");
    }

    cgiEndHTML();

    if (oldinfo)
      ippDelete(oldinfo);

    return;
  }
  else if (!file &&
           (!cgiGetVariable("PPD_NAME") || cgiGetVariable("SELECT_MAKE")))
  {
    int ipp_everywhere = !strncmp(var, "ipp://", 6) || !strncmp(var, "ipps://", 7) || (!strncmp(var, "dnssd://", 8) && (strstr(var, "_ipp._tcp") || strstr(var, "_ipps._tcp")));

    if (modify && !cgiGetVariable("SELECT_MAKE"))
    {
     /*
      * Get the PPD file...
      */

      int		fd;		/* PPD file */
      char		filename[1024];	/* PPD filename */
      ppd_file_t	*ppd;		/* PPD information */
      char		buffer[1024];	/* Buffer */
      ssize_t		bytes;		/* Number of bytes */
      http_status_t	get_status;	/* Status of GET */


      /* TODO: Use cupsGetFile() API... */
      snprintf(uri, sizeof(uri), "/printers/%s.ppd", name);

      if (httpGet(http, uri))
        httpGet(http, uri);

      while ((get_status = httpUpdate(http)) == HTTP_CONTINUE);

      if (get_status != HTTP_OK)
      {
        httpFlush(http);

        fprintf(stderr, "ERROR: Unable to get PPD file %s: %d - %s\n",
	        uri, get_status, httpStatus(get_status));
      }
      else if ((fd = cupsTempFd(filename, sizeof(filename))) >= 0)
      {
	while ((bytes = httpRead2(http, buffer, sizeof(buffer))) > 0)
          write(fd, buffer, (size_t)bytes);

	close(fd);

        if ((ppd = ppdOpenFile(filename)) != NULL)
	{
	  if (ppd->manufacturer)
	    cgiSetVariable("CURRENT_MAKE", ppd->manufacturer);

	  if (ppd->nickname)
	    cgiSetVariable("CURRENT_MAKE_AND_MODEL", ppd->nickname);

          ppdClose(ppd);
          unlink(filename);
	}
	else
	{
	  int linenum;			/* Line number */

	  fprintf(stderr, "ERROR: Unable to open PPD file %s: %s\n",
	          filename, ppdErrorString(ppdLastError(&linenum)));
	}
      }
      else
      {
        httpFlush(http);

        fprintf(stderr,
	        "ERROR: Unable to create temporary file for PPD file: %s\n",
	        strerror(errno));
      }
    }

   /*
    * Build a CUPS_GET_PPDS request, which requires the following
    * attributes:
    *
    *    attributes-charset
    *    attributes-natural-language
    *    printer-uri
    */

    request = ippNewRequest(CUPS_GET_PPDS);

    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
                 NULL, "ipp://localhost/printers/");

    if ((var = cgiGetVariable("PPD_MAKE")) == NULL)
      var = cgiGetVariable("CURRENT_MAKE");
    if (var && !cgiGetVariable("SELECT_MAKE"))
    {
      const char *make_model;		/* Make and model */


      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_TEXT,
                   "ppd-make", NULL, var);

      if ((make_model = cgiGetVariable("CURRENT_MAKE_AND_MODEL")) != NULL)
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_TEXT,
		     "ppd-make-and-model", NULL, make_model);
    }
    else
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                   "requested-attributes", NULL, "ppd-make");

   /*
    * Do the request and get back a response...
    */

    if ((response = cupsDoRequest(http, request, "/")) != NULL)
    {
     /*
      * Got the list of PPDs, see if the user has selected a make...
      */

      if (cgiSetIPPVars(response, NULL, NULL, NULL, 0) == 0 && !modify)
      {
       /*
        * No PPD files with this make, try again with all makes...
	*/

        ippDelete(response);

	request = ippNewRequest(CUPS_GET_PPDS);

	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
                     NULL, "ipp://localhost/printers/");

	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                     "requested-attributes", NULL, "ppd-make");

	if ((response = cupsDoRequest(http, request, "/")) != NULL)
          cgiSetIPPVars(response, NULL, NULL, NULL, 0);

        cgiStartHTML(title);
	cgiCopyTemplateLang("choose-make.tmpl");
        cgiEndHTML();
      }
      else if (!var || cgiGetVariable("SELECT_MAKE"))
      {
        cgiStartHTML(title);
	cgiCopyTemplateLang("choose-make.tmpl");
        cgiEndHTML();
      }
      else
      {
       /*
	* Let the user choose a model...
	*/

        cgiStartHTML(title);
	if (!cgiGetVariable("PPD_MAKE"))
	  cgiSetVariable("PPD_MAKE", cgiGetVariable("CURRENT_MAKE"));
        if (ipp_everywhere)
	  cgiSetVariable("SHOW_IPP_EVERYWHERE", "1");
	cgiCopyTemplateLang("choose-model.tmpl");
        cgiEndHTML();
      }

      ippDelete(response);
    }
    else
    {
      cgiStartHTML(title);
      cgiShowIPPError(_("Unable to get list of printer drivers"));
      cgiCopyTemplateLang("error.tmpl");
      cgiEndHTML();
    }
  }
  else
  {
   /*
    * Build a CUPS_ADD_PRINTER request, which requires the following
    * attributes:
    *
    *    attributes-charset
    *    attributes-natural-language
    *    printer-uri
    *    printer-location
    *    printer-info
    *    ppd-name
    *    device-uri
    *    printer-is-accepting-jobs
    *    printer-is-shared
    *    printer-state
    */

    request = ippNewRequest(CUPS_ADD_PRINTER);

    httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                     "localhost", 0, "/printers/%s",
		     cgiGetTextfield("PRINTER_NAME"));
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
                 NULL, uri);

    if (!file)
    {
      ppd_name = cgiGetVariable("PPD_NAME");
      if (strcmp(ppd_name, "__no_change__"))
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "ppd-name",
		     NULL, ppd_name);
    }

    ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-location",
                 NULL, cgiGetTextfield("PRINTER_LOCATION"));

    ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-info",
                 NULL, cgiGetTextfield("PRINTER_INFO"));

    strlcpy(uri, cgiGetVariable("DEVICE_URI"), sizeof(uri));

   /*
    * Strip make and model from URI...
    */

    if ((uriptr = strrchr(uri, '|')) != NULL)
      *uriptr = '\0';

    if (!strncmp(uri, "serial:", 7))
    {
     /*
      * Update serial port URI to include baud rate, etc.
      */

      if ((uriptr = strchr(uri, '?')) == NULL)
        uriptr = uri + strlen(uri);

      snprintf(uriptr, sizeof(uri) - (size_t)(uriptr - uri),
               "?baud=%s+bits=%s+parity=%s+flow=%s",
               cgiGetVariable("BAUDRATE"), cgiGetVariable("BITS"),
	       cgiGetVariable("PARITY"), cgiGetVariable("FLOW"));
    }

    ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_URI, "device-uri",
                 NULL, uri);

    ippAddBoolean(request, IPP_TAG_PRINTER, "printer-is-accepting-jobs", 1);

    ippAddBoolean(request, IPP_TAG_PRINTER, "printer-is-shared", cgiGetCheckbox("PRINTER_IS_SHARED"));

    ippAddInteger(request, IPP_TAG_PRINTER, IPP_TAG_ENUM, "printer-state",
                  IPP_PRINTER_IDLE);

   /*
    * Do the request and get back a response...
    */

    if (file)
      ippDelete(cupsDoFileRequest(http, request, "/admin/", file->tempfile));
    else if (evefile[0])
    {
      ippDelete(cupsDoFileRequest(http, request, "/admin/", evefile));
      unlink(evefile);
    }
    else
      ippDelete(cupsDoRequest(http, request, "/admin/"));

    if (cupsLastError() == IPP_NOT_AUTHORIZED)
    {
      puts("Status: 401\n");
      exit(0);
    }
    else if (cupsLastError() > IPP_OK_CONFLICT)
    {
      cgiStartHTML(title);
      cgiShowIPPError(modify ? _("Unable to modify printer") :
                               _("Unable to add printer"));
    }
    else if (modify)
    {
     /*
      * Redirect successful updates back to the printer page...
      */

      char	refresh[1024];		/* Refresh URL */


      cgiFormEncode(uri, name, sizeof(uri));

      snprintf(refresh, sizeof(refresh),
	       "5;/admin/?OP=redirect&URL=/printers/%s", uri);

      cgiSetVariable("refresh_page", refresh);

      cgiStartHTML(title);

      cgiCopyTemplateLang("printer-modified.tmpl");
    }
    else if (ppd_name && (strcmp(ppd_name, "everywhere") == 0 || strstr(ppd_name, "driverless")))
    {
     /*
      * Set the printer options...
      */

      cgiSetVariable("OP", "set-printer-options");
      do_set_options(http, 0);
      return;
    }
    else
    {
     /*
      * If we don't have an everywhere model, show printer-added
      * template with warning about drivers going away...
      */

      cgiStartHTML(title);
      cgiCopyTemplateLang("printer-added.tmpl");
    }

    cgiEndHTML();
  }

  if (oldinfo)
    ippDelete(oldinfo);
}


/*
 * 'do_config_server()' - Configure server settings.
 */

static void
do_config_server(http_t *http)		/* I - HTTP connection */
{
  if (cgiGetVariable("CHANGESETTINGS"))
  {
   /*
    * Save basic setting changes...
    */

    int			num_settings;	/* Number of server settings */
    cups_option_t	*settings;	/* Server settings */
    int			advanced,	/* Advanced settings shown? */
			changed;	/* Have settings changed? */
    const char		*debug_logging,	/* DEBUG_LOGGING value */
			*preserve_jobs = NULL,
					/* PRESERVE_JOBS value */
			*remote_admin,	/* REMOTE_ADMIN value */
			*remote_any,	/* REMOTE_ANY value */
			*share_printers,/* SHARE_PRINTERS value */
			*user_cancel_any,
					/* USER_CANCEL_ANY value */
			*browse_web_if = NULL,
					/* BrowseWebIF value */
			*preserve_job_history = NULL,
					/* PreserveJobHistory value */
			*preserve_job_files = NULL,
					/* PreserveJobFiles value */
			*max_clients = NULL,
					/* MaxClients value */
			*max_jobs = NULL,
					/* MaxJobs value */
			*max_log_size = NULL;
					/* MaxLogSize value */
    const char		*current_browse_web_if,
					/* BrowseWebIF value */
			*current_preserve_job_history,
					/* PreserveJobHistory value */
			*current_preserve_job_files,
					/* PreserveJobFiles value */
			*current_max_clients,
					/* MaxClients value */
			*current_max_jobs,
					/* MaxJobs value */
			*current_max_log_size;
					/* MaxLogSize value */
#ifdef HAVE_GSSAPI
    char		default_auth_type[255];
					/* DefaultAuthType value */
    const char		*val;		/* Setting value */
#endif /* HAVE_GSSAPI */


   /*
    * Get the checkbox values from the form...
    */

    debug_logging        = cgiGetCheckbox("DEBUG_LOGGING") ? "1" : "0";
    remote_admin         = cgiGetCheckbox("REMOTE_ADMIN") ? "1" : "0";
    remote_any           = cgiGetCheckbox("REMOTE_ANY") ? "1" : "0";
    share_printers       = cgiGetCheckbox("SHARE_PRINTERS") ? "1" : "0";
    user_cancel_any      = cgiGetCheckbox("USER_CANCEL_ANY") ? "1" : "0";

    advanced = cgiGetCheckbox("ADVANCEDSETTINGS");
    if (advanced)
    {
     /*
      * Get advanced settings...
      */

      browse_web_if        = cgiGetCheckbox("BROWSE_WEB_IF") ? "Yes" : "No";
      max_clients          = cgiGetTextfield("MAX_CLIENTS");
      max_log_size         = cgiGetTextfield("MAX_LOG_SIZE");
      preserve_jobs        = cgiGetCheckbox("PRESERVE_JOBS") ? "1" : "0";

      if (atoi(preserve_jobs))
      {
        max_jobs             = cgiGetTextfield("MAX_JOBS");
	preserve_job_history = cgiGetTextfield("PRESERVE_JOB_HISTORY");
	preserve_job_files   = cgiGetTextfield("PRESERVE_JOB_FILES");

	if (!max_jobs || atoi(max_jobs) < 0)
	  max_jobs = "500";

	if (!preserve_job_history || !preserve_job_history[0] ||
	    (strcasecmp(preserve_job_history, "yes") && strcasecmp(preserve_job_history, "no") && !atoi(preserve_job_history)))
	  preserve_job_history = "Yes";

	if (!preserve_job_files || !preserve_job_files[0] ||
	    (strcasecmp(preserve_job_files, "yes") && strcasecmp(preserve_job_files, "no") && !atoi(preserve_job_files)))
	  preserve_job_files = "1d";
      }
      else
      {
        max_jobs             = "0";
        preserve_job_history = "No";
        preserve_job_files   = "No";
      }

      if (!max_clients || atoi(max_clients) <= 0)
	max_clients = "100";

      if (!max_log_size || atoi(max_log_size) <= 0.0)
	max_log_size = "1m";
    }

   /*
    * Get the current server settings...
    */

    if (!cupsAdminGetServerSettings(http, &num_settings, &settings))
    {
      cgiStartHTML(cgiText(_("Change Settings")));
      cgiSetVariable("MESSAGE",
                     cgiText(_("Unable to change server settings")));
      cgiSetVariable("ERROR", cupsLastErrorString());
      cgiCopyTemplateLang("error.tmpl");
      cgiEndHTML();
      return;
    }

#ifdef HAVE_GSSAPI
   /*
    * Get authentication settings...
    */

    if (cgiGetCheckbox("KERBEROS"))
    {
      strlcpy(default_auth_type, "Negotiate", sizeof(default_auth_type));
    }
    else
    {
      val = cupsGetOption("DefaultAuthType", num_settings, settings);

      if (!val || !_cups_strcasecmp(val, "Negotiate"))
        strlcpy(default_auth_type, "Basic", sizeof(default_auth_type));
      else
        strlcpy(default_auth_type, val, sizeof(default_auth_type));
    }

    fprintf(stderr, "DEBUG: DefaultAuthType %s\n", default_auth_type);
#endif /* HAVE_GSSAPI */

    if ((current_browse_web_if = cupsGetOption("BrowseWebIF", num_settings,
                                               settings)) == NULL)
      current_browse_web_if = "No";

    if ((current_preserve_job_history = cupsGetOption("PreserveJobHistory",
                                                      num_settings,
						      settings)) == NULL)
      current_preserve_job_history = "Yes";

    if ((current_preserve_job_files = cupsGetOption("PreserveJobFiles",
                                                    num_settings,
						    settings)) == NULL)
      current_preserve_job_files = "1d";

    if ((current_max_clients = cupsGetOption("MaxClients", num_settings,
                                             settings)) == NULL)
      current_max_clients = "100";

    if ((current_max_jobs = cupsGetOption("MaxJobs", num_settings,
                                          settings)) == NULL)
      current_max_jobs = "500";

    if ((current_max_log_size = cupsGetOption("MaxLogSize", num_settings,
                                              settings)) == NULL)
      current_max_log_size = "1m";

   /*
    * See if the settings have changed...
    */

    changed = strcmp(debug_logging, cupsGetOption(CUPS_SERVER_DEBUG_LOGGING,
                                                  num_settings, settings)) ||
	      strcmp(remote_admin, cupsGetOption(CUPS_SERVER_REMOTE_ADMIN,
						 num_settings, settings)) ||
	      strcmp(remote_any, cupsGetOption(CUPS_SERVER_REMOTE_ANY,
					       num_settings, settings)) ||
	      strcmp(share_printers, cupsGetOption(CUPS_SERVER_SHARE_PRINTERS,
						   num_settings, settings)) ||
#ifdef HAVE_GSSAPI
	      !cupsGetOption("DefaultAuthType", num_settings, settings) ||
	      strcmp(default_auth_type, cupsGetOption("DefaultAuthType",
						      num_settings, settings)) ||
#endif /* HAVE_GSSAPI */
	      strcmp(user_cancel_any, cupsGetOption(CUPS_SERVER_USER_CANCEL_ANY,
						    num_settings, settings));

    if (advanced && !changed)
      changed = _cups_strcasecmp(browse_web_if, current_browse_web_if) ||
		_cups_strcasecmp(preserve_job_history, current_preserve_job_history) ||
		_cups_strcasecmp(preserve_job_files, current_preserve_job_files) ||
		_cups_strcasecmp(max_clients, current_max_clients) ||
		_cups_strcasecmp(max_jobs, current_max_jobs) ||
		_cups_strcasecmp(max_log_size, current_max_log_size);

    if (changed)
    {
     /*
      * Settings *have* changed, so save the changes...
      */

      int		num_newsettings;/* New number of server settings */
      cups_option_t	*newsettings;	/* New server settings */

      num_newsettings = 0;
      num_newsettings = cupsAddOption(CUPS_SERVER_DEBUG_LOGGING, debug_logging, num_newsettings, &newsettings);
      num_newsettings = cupsAddOption(CUPS_SERVER_REMOTE_ADMIN, remote_admin, num_newsettings, &newsettings);
      num_newsettings = cupsAddOption(CUPS_SERVER_REMOTE_ANY, remote_any, num_newsettings, &newsettings);
      num_newsettings = cupsAddOption(CUPS_SERVER_SHARE_PRINTERS, share_printers, num_newsettings, &newsettings);
      num_newsettings = cupsAddOption(CUPS_SERVER_USER_CANCEL_ANY, user_cancel_any, num_newsettings, &newsettings);
#ifdef HAVE_GSSAPI
      num_newsettings = cupsAddOption("DefaultAuthType", default_auth_type, num_newsettings, &newsettings);
#endif /* HAVE_GSSAPI */

      if (advanced)
      {
       /*
        * Add advanced newsettings...
	*/

	if (_cups_strcasecmp(browse_web_if, current_browse_web_if))
	  num_newsettings = cupsAddOption("BrowseWebIF", browse_web_if, num_newsettings, &newsettings);
	if (_cups_strcasecmp(preserve_job_history, current_preserve_job_history))
	  num_newsettings = cupsAddOption("PreserveJobHistory", preserve_job_history, num_newsettings, &newsettings);
	if (_cups_strcasecmp(preserve_job_files, current_preserve_job_files))
	  num_newsettings = cupsAddOption("PreserveJobFiles", preserve_job_files, num_newsettings, &newsettings);
        if (_cups_strcasecmp(max_clients, current_max_clients))
	  num_newsettings = cupsAddOption("MaxClients", max_clients, num_newsettings, &newsettings);
        if (_cups_strcasecmp(max_jobs, current_max_jobs))
	  num_newsettings = cupsAddOption("MaxJobs", max_jobs, num_newsettings, &newsettings);
        if (_cups_strcasecmp(max_log_size, current_max_log_size))
	  num_newsettings = cupsAddOption("MaxLogSize", max_log_size, num_newsettings, &newsettings);
      }

      if (!cupsAdminSetServerSettings(http, num_newsettings, newsettings))
      {
        if (cupsLastError() == IPP_NOT_AUTHORIZED)
	{
	  puts("Status: 401\n");
	  exit(0);
	}

	cgiStartHTML(cgiText(_("Change Settings")));
	cgiSetVariable("MESSAGE",
                       cgiText(_("Unable to change server settings")));
	cgiSetVariable("ERROR", cupsLastErrorString());
	cgiCopyTemplateLang("error.tmpl");
      }
      else
      {
        if (advanced)
	  cgiSetVariable("refresh_page", "5;URL=/admin/?OP=redirect&"
	                                 "URL=/admin/?ADVANCEDSETTINGS=YES");
        else
	  cgiSetVariable("refresh_page", "5;URL=/admin/?OP=redirect");
	cgiStartHTML(cgiText(_("Change Settings")));
	cgiCopyTemplateLang("restart.tmpl");
      }

      cupsFreeOptions(num_newsettings, newsettings);
    }
    else
    {
     /*
      * No changes...
      */

      cgiSetVariable("refresh_page", "5;URL=/admin/?OP=redirect");
      cgiStartHTML(cgiText(_("Change Settings")));
      cgiCopyTemplateLang("norestart.tmpl");
    }

    cupsFreeOptions(num_settings, settings);

    cgiEndHTML();
  }
  else if (cgiGetVariable("SAVECHANGES") && cgiGetVariable("CUPSDCONF"))
  {
   /*
    * Save hand-edited config file...
    */

    http_status_t status;		/* PUT status */
    char	tempfile[1024];		/* Temporary new cupsd.conf */
    int		tempfd;			/* Temporary file descriptor */
    cups_file_t	*temp;			/* Temporary file */
    const char	*start,			/* Start of line */
		*end;			/* End of line */


   /*
    * Create a temporary file for the new cupsd.conf file...
    */

    if ((tempfd = cupsTempFd(tempfile, sizeof(tempfile))) < 0)
    {
      cgiStartHTML(cgiText(_("Edit Configuration File")));
      cgiSetVariable("MESSAGE", cgiText(_("Unable to create temporary file")));
      cgiSetVariable("ERROR", strerror(errno));
      cgiCopyTemplateLang("error.tmpl");
      cgiEndHTML();

      perror(tempfile);
      return;
    }

    if ((temp = cupsFileOpenFd(tempfd, "w")) == NULL)
    {
      cgiStartHTML(cgiText(_("Edit Configuration File")));
      cgiSetVariable("MESSAGE", cgiText(_("Unable to create temporary file")));
      cgiSetVariable("ERROR", strerror(errno));
      cgiCopyTemplateLang("error.tmpl");
      cgiEndHTML();

      perror(tempfile);
      close(tempfd);
      unlink(tempfile);
      return;
    }

   /*
    * Copy the cupsd.conf text from the form variable...
    */

    start = cgiGetVariable("CUPSDCONF");
    while (start)
    {
      if ((end = strstr(start, "\r\n")) == NULL)
        if ((end = strstr(start, "\n")) == NULL)
	  end = start + strlen(start);

      cupsFileWrite(temp, start, (size_t)(end - start));
      cupsFilePutChar(temp, '\n');

      if (*end == '\r')
        start = end + 2;
      else if (*end == '\n')
        start = end + 1;
      else
        start = NULL;
    }

    cupsFileClose(temp);

   /*
    * Upload the configuration file to the server...
    */

    status = cupsPutFile(http, "/admin/conf/cupsd.conf", tempfile);

    if (status == HTTP_UNAUTHORIZED)
    {
      puts("Status: 401\n");
      unlink(tempfile);
      exit(0);
    }
    else if (status != HTTP_CREATED)
    {
      cgiSetVariable("MESSAGE",
                     cgiText(_("Unable to upload cupsd.conf file")));
      cgiSetVariable("ERROR", httpStatus(status));

      cgiStartHTML(cgiText(_("Edit Configuration File")));
      cgiCopyTemplateLang("error.tmpl");
    }
    else
    {
      cgiSetVariable("refresh_page", "5;URL=/admin/");

      cgiStartHTML(cgiText(_("Edit Configuration File")));
      cgiCopyTemplateLang("restart.tmpl");
    }

    cgiEndHTML();

    unlink(tempfile);
  }
  else
  {
    struct stat	info;			/* cupsd.conf information */
    cups_file_t	*cupsd;			/* cupsd.conf file */
    char	*buffer,		/* Buffer for entire file */
		*bufptr,		/* Pointer into buffer */
		*bufend;		/* End of buffer */
    int		ch;			/* Character from file */
    char	filename[1024];		/* Filename */
    const char	*server_root;		/* Location of config files */


   /*
    * Locate the cupsd.conf file...
    */

    if ((server_root = getenv("CUPS_SERVERROOT")) == NULL)
      server_root = CUPS_SERVERROOT;

    snprintf(filename, sizeof(filename), "%s/cupsd.conf", server_root);

   /*
    * Figure out the size...
    */

    if (stat(filename, &info))
    {
      cgiStartHTML(cgiText(_("Edit Configuration File")));
      cgiSetVariable("MESSAGE",
                     cgiText(_("Unable to access cupsd.conf file")));
      cgiSetVariable("ERROR", strerror(errno));
      cgiCopyTemplateLang("error.tmpl");
      cgiEndHTML();

      perror(filename);
      return;
    }

    if (info.st_size > (1024 * 1024))
    {
      cgiStartHTML(cgiText(_("Edit Configuration File")));
      cgiSetVariable("MESSAGE",
                     cgiText(_("Unable to access cupsd.conf file")));
      cgiSetVariable("ERROR",
                     cgiText(_("Unable to edit cupsd.conf files larger than "
		               "1MB")));
      cgiCopyTemplateLang("error.tmpl");
      cgiEndHTML();

      fprintf(stderr, "ERROR: \"%s\" too large (%ld) to edit!\n", filename,
              (long)info.st_size);
      return;
    }

   /*
    * Open the cupsd.conf file...
    */

    if ((cupsd = cupsFileOpen(filename, "r")) == NULL)
    {
     /*
      * Unable to open - log an error...
      */

      cgiStartHTML(cgiText(_("Edit Configuration File")));
      cgiSetVariable("MESSAGE",
                     cgiText(_("Unable to access cupsd.conf file")));
      cgiSetVariable("ERROR", strerror(errno));
      cgiCopyTemplateLang("error.tmpl");
      cgiEndHTML();

      perror(filename);
      return;
    }

   /*
    * Allocate memory and load the file into a string buffer...
    */

    if ((buffer = calloc(1, (size_t)info.st_size + 1)) != NULL)
    {
      cupsFileRead(cupsd, buffer, (size_t)info.st_size);
      cgiSetVariable("CUPSDCONF", buffer);
      free(buffer);
    }

    cupsFileClose(cupsd);

   /*
    * Then get the default cupsd.conf file and put that into a string as
    * well...
    */

    strlcat(filename, ".default", sizeof(filename));

    if (!stat(filename, &info) && info.st_size < (1024 * 1024) &&
        (cupsd = cupsFileOpen(filename, "r")) != NULL)
    {
      if ((buffer = calloc(1, 2 * (size_t)info.st_size + 1)) != NULL)
      {
	bufend = buffer + 2 * info.st_size - 1;

	for (bufptr = buffer;
	     bufptr < bufend && (ch = cupsFileGetChar(cupsd)) != EOF;)
	{
	  if (ch == '\\' || ch == '\"')
	  {
	    *bufptr++ = '\\';
	    *bufptr++ = (char)ch;
	  }
	  else if (ch == '\n')
	  {
	    *bufptr++ = '\\';
	    *bufptr++ = 'n';
	  }
	  else if (ch == '\t')
	  {
	    *bufptr++ = '\\';
	    *bufptr++ = 't';
	  }
	  else if (ch >= ' ')
	    *bufptr++ = (char)ch;
	}

	*bufptr = '\0';

	cgiSetVariable("CUPSDCONF_DEFAULT", buffer);
	free(buffer);
      }

      cupsFileClose(cupsd);
    }

   /*
    * Show the current config file...
    */

    cgiStartHTML(cgiText(_("Edit Configuration File")));

    cgiCopyTemplateLang("edit-config.tmpl");

    cgiEndHTML();
  }
}


/*
 * 'do_delete_class()' - Delete a class.
 */

static void
do_delete_class(http_t *http)		/* I - HTTP connection */
{
  ipp_t		*request;		/* IPP request */
  char		uri[HTTP_MAX_URI];	/* Job URI */
  const char	*pclass;		/* Printer class name */


 /*
  * Get form variables...
  */

  if (cgiGetVariable("CONFIRM") == NULL)
  {
    cgiStartHTML(cgiText(_("Delete Class")));
    cgiCopyTemplateLang("class-confirm.tmpl");
    cgiEndHTML();
    return;
  }

  if ((pclass = cgiGetTextfield("PRINTER_NAME")) != NULL)
    httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                     "localhost", 0, "/classes/%s", pclass);
  else
  {
    cgiStartHTML(cgiText(_("Delete Class")));
    cgiSetVariable("ERROR", cgiText(_("Missing form variable")));
    cgiCopyTemplateLang("error.tmpl");
    cgiEndHTML();
    return;
  }

 /*
  * Build a CUPS_DELETE_CLASS request, which requires the following
  * attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  *    printer-uri
  */

  request = ippNewRequest(CUPS_DELETE_CLASS);

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
               NULL, uri);

 /*
  * Do the request and get back a response...
  */

  ippDelete(cupsDoRequest(http, request, "/admin/"));

 /*
  * Show the results...
  */

  if (cupsLastError() == IPP_NOT_AUTHORIZED)
  {
    puts("Status: 401\n");
    exit(0);
  }
  else if (cupsLastError() <= IPP_OK_CONFLICT)
  {
   /*
    * Redirect successful updates back to the classes page...
    */

    cgiSetVariable("refresh_page", "5;URL=/admin/?OP=redirect&URL=/classes");
  }

  cgiStartHTML(cgiText(_("Delete Class")));

  if (cupsLastError() > IPP_OK_CONFLICT)
    cgiShowIPPError(_("Unable to delete class"));
  else
    cgiCopyTemplateLang("class-deleted.tmpl");

  cgiEndHTML();
}


/*
 * 'do_delete_printer()' - Delete a printer.
 */

static void
do_delete_printer(http_t *http)		/* I - HTTP connection */
{
  ipp_t		*request;		/* IPP request */
  char		uri[HTTP_MAX_URI];	/* Job URI */
  const char	*printer;		/* Printer printer name */


 /*
  * Get form variables...
  */

  if (cgiGetVariable("CONFIRM") == NULL)
  {
    cgiStartHTML(cgiText(_("Delete Printer")));
    cgiCopyTemplateLang("printer-confirm.tmpl");
    cgiEndHTML();
    return;
  }

  if ((printer = cgiGetTextfield("PRINTER_NAME")) != NULL)
    httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                     "localhost", 0, "/printers/%s", printer);
  else
  {
    cgiStartHTML(cgiText(_("Delete Printer")));
    cgiSetVariable("ERROR", cgiText(_("Missing form variable")));
    cgiCopyTemplateLang("error.tmpl");
    cgiEndHTML();
    return;
  }

 /*
  * Build a CUPS_DELETE_PRINTER request, which requires the following
  * attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  *    printer-uri
  */

  request = ippNewRequest(CUPS_DELETE_PRINTER);

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
               NULL, uri);

 /*
  * Do the request and get back a response...
  */

  ippDelete(cupsDoRequest(http, request, "/admin/"));

 /*
  * Show the results...
  */

  if (cupsLastError() == IPP_NOT_AUTHORIZED)
  {
    puts("Status: 401\n");
    exit(0);
  }
  else if (cupsLastError() <= IPP_OK_CONFLICT)
  {
   /*
    * Redirect successful updates back to the printers page...
    */

    cgiSetVariable("refresh_page", "5;URL=/admin/?OP=redirect&URL=/printers");
  }

  cgiStartHTML(cgiText(_("Delete Printer")));

  if (cupsLastError() > IPP_OK_CONFLICT)
    cgiShowIPPError(_("Unable to delete printer"));
  else
    cgiCopyTemplateLang("printer-deleted.tmpl");

  cgiEndHTML();
}


/*
 * 'do_list_printers()' - List available printers.
 */

static void
do_list_printers(http_t *http)		/* I - HTTP connection */
{
  ipp_t		*request,		/* IPP request */
		*response;		/* IPP response */
  ipp_attribute_t *attr;		/* IPP attribute */


  cgiStartHTML(cgiText(_("List Available Printers")));
  fflush(stdout);

 /*
  * Get the list of printers and their devices...
  */

  request = ippNewRequest(CUPS_GET_PRINTERS);

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
               "requested-attributes", NULL, "device-uri");

  ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_ENUM, "printer-type",
                CUPS_PRINTER_LOCAL);
  ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_ENUM, "printer-type-mask",
                CUPS_PRINTER_LOCAL);

  if ((response = cupsDoRequest(http, request, "/")) != NULL)
  {
   /*
    * Got the printer list, now load the devices...
    */

    int		i;			/* Looping var */
    cups_array_t *printer_devices;	/* Printer devices for local printers */
    char	*printer_device;	/* Current printer device */


   /*
    * Allocate an array and copy the device strings...
    */

    printer_devices = cupsArrayNew((cups_array_func_t)strcmp, NULL);

    for (attr = ippFindAttribute(response, "device-uri", IPP_TAG_URI);
         attr;
	 attr = ippFindNextAttribute(response, "device-uri", IPP_TAG_URI))
    {
      cupsArrayAdd(printer_devices, strdup(attr->values[0].string.text));
    }

   /*
    * Free the printer list and get the device list...
    */

    ippDelete(response);

    request = ippNewRequest(CUPS_GET_DEVICES);

    if ((response = cupsDoRequest(http, request, "/")) != NULL)
    {
     /*
      * Got the device list, let's parse it...
      */

      const char *device_uri,		/* device-uri attribute value */
		*device_make_and_model,	/* device-make-and-model value */
		*device_info;		/* device-info value */


      for (i = 0, attr = response->attrs; attr; attr = attr->next)
      {
       /*
        * Skip leading attributes until we hit a device...
	*/

	while (attr && attr->group_tag != IPP_TAG_PRINTER)
          attr = attr->next;

	if (!attr)
          break;

       /*
	* Pull the needed attributes from this device...
	*/

	device_info           = NULL;
	device_make_and_model = NULL;
	device_uri            = NULL;

	while (attr && attr->group_tag == IPP_TAG_PRINTER)
	{
          if (!strcmp(attr->name, "device-info") &&
	      attr->value_tag == IPP_TAG_TEXT)
	    device_info = attr->values[0].string.text;

          if (!strcmp(attr->name, "device-make-and-model") &&
	      attr->value_tag == IPP_TAG_TEXT)
	    device_make_and_model = attr->values[0].string.text;

          if (!strcmp(attr->name, "device-uri") &&
	      attr->value_tag == IPP_TAG_URI)
	    device_uri = attr->values[0].string.text;

          attr = attr->next;
	}

       /*
	* See if we have everything needed...
	*/

	if (device_info && device_make_and_model && device_uri &&
	    _cups_strcasecmp(device_make_and_model, "unknown") &&
	    strchr(device_uri, ':'))
	{
	 /*
	  * Yes, now see if there is already a printer for this
	  * device...
	  */

          if (!cupsArrayFind(printer_devices, (void *)device_uri))
          {
	   /*
	    * Not found, so it must be a new printer...
	    */

            char	option[1024],	/* Form variables for this device */
			*option_ptr;	/* Pointer into string */
	    const char	*ptr;		/* Pointer into device string */


           /*
	    * Format the printer name variable for this device...
	    *
	    * We use the device-info string first, then device-uri,
	    * and finally device-make-and-model to come up with a
	    * suitable name.
	    */

            if (_cups_strncasecmp(device_info, "unknown", 7))
	      ptr = device_info;
            else if ((ptr = strstr(device_uri, "://")) != NULL)
	      ptr += 3;
	    else
	      ptr = device_make_and_model;

	    for (option_ptr = option;
	         option_ptr < (option + sizeof(option) - 1) && *ptr;
		 ptr ++)
	      if (isalnum(*ptr & 255) || *ptr == '_' || *ptr == '-' ||
	          *ptr == '.')
	        *option_ptr++ = *ptr;
	      else if ((*ptr == ' ' || *ptr == '/') && option_ptr > option &&
	               option_ptr[-1] != '_')
	        *option_ptr++ = '_';
	      else if (*ptr == '?' || *ptr == '(')
	        break;

            *option_ptr = '\0';

            cgiSetArray("TEMPLATE_NAME", i, option);

           /*
	    * Finally, set the form variables for this printer...
	    */

	    cgiSetArray("device_info", i, device_info);
	    cgiSetArray("device_make_and_model", i, device_make_and_model);
            cgiSetArray("device_uri", i, device_uri);
	    i ++;
	  }
	}

        if (!attr)
	  break;
      }

      ippDelete(response);

     /*
      * Free the device list...
      */

      for (printer_device = (char *)cupsArrayFirst(printer_devices);
           printer_device;
	   printer_device = (char *)cupsArrayNext(printer_devices))
        free(printer_device);

      cupsArrayDelete(printer_devices);
    }
  }

 /*
  * Finally, show the printer list...
  */

  cgiCopyTemplateLang("list-available-printers.tmpl");

  cgiEndHTML();
}


/*
 * 'do_menu()' - Show the main menu.
 */

static void
do_menu(http_t *http)			/* I - HTTP connection */
{
  int		num_settings;		/* Number of server settings */
  cups_option_t	*settings;		/* Server settings */
  const char	*val;			/* Setting value */


 /*
  * Get the current server settings...
  */

  if (!cupsAdminGetServerSettings(http, &num_settings, &settings))
  {
    cgiSetVariable("SETTINGS_MESSAGE",
                   cgiText(_("Unable to open cupsd.conf file:")));
    cgiSetVariable("SETTINGS_ERROR", cupsLastErrorString());
  }

  if ((val = cupsGetOption(CUPS_SERVER_DEBUG_LOGGING, num_settings,
                           settings)) != NULL && atoi(val))
    cgiSetVariable("DEBUG_LOGGING", "CHECKED");
  else
    cgiSetVariable("DEBUG_LOGGING", "");

  if ((val = cupsGetOption(CUPS_SERVER_REMOTE_ADMIN, num_settings,
                           settings)) != NULL && atoi(val))
    cgiSetVariable("REMOTE_ADMIN", "CHECKED");
  else
    cgiSetVariable("REMOTE_ADMIN", "");

  if ((val = cupsGetOption(CUPS_SERVER_REMOTE_ANY, num_settings,
                           settings)) != NULL && atoi(val))
    cgiSetVariable("REMOTE_ANY", "CHECKED");
  else
    cgiSetVariable("REMOTE_ANY", "");

  if ((val = cupsGetOption(CUPS_SERVER_SHARE_PRINTERS, num_settings,
                           settings)) != NULL && atoi(val))
    cgiSetVariable("SHARE_PRINTERS", "CHECKED");
  else
    cgiSetVariable("SHARE_PRINTERS", "");

  if ((val = cupsGetOption(CUPS_SERVER_USER_CANCEL_ANY, num_settings,
                           settings)) != NULL && atoi(val))
    cgiSetVariable("USER_CANCEL_ANY", "CHECKED");
  else
    cgiSetVariable("USER_CANCEL_ANY", "");

#ifdef HAVE_GSSAPI
  cgiSetVariable("HAVE_GSSAPI", "1");

  if ((val = cupsGetOption("DefaultAuthType", num_settings,
                           settings)) != NULL && !_cups_strcasecmp(val, "Negotiate"))
    cgiSetVariable("KERBEROS", "CHECKED");
  else
#endif /* HAVE_GSSAPI */
  cgiSetVariable("KERBEROS", "");

  if ((val = cupsGetOption("BrowseWebIF", num_settings,
                           settings)) == NULL)
    val = "No";

  if (!_cups_strcasecmp(val, "yes") || !_cups_strcasecmp(val, "on") ||
      !_cups_strcasecmp(val, "true"))
    cgiSetVariable("BROWSE_WEB_IF", "CHECKED");
  else
    cgiSetVariable("BROWSE_WEB_IF", "");

  if ((val = cupsGetOption("PreserveJobHistory", num_settings,
                           settings)) == NULL)
    val = "Yes";

  if (val &&
      (!_cups_strcasecmp(val, "0") || !_cups_strcasecmp(val, "no") ||
       !_cups_strcasecmp(val, "off") || !_cups_strcasecmp(val, "false") ||
       !_cups_strcasecmp(val, "disabled")))
  {
    cgiSetVariable("PRESERVE_JOB_HISTORY", "0");
    cgiSetVariable("PRESERVE_JOB_FILES", "0");
  }
  else
  {
    cgiSetVariable("PRESERVE_JOBS", "CHECKED");
    cgiSetVariable("PRESERVE_JOB_HISTORY", val);

    if ((val = cupsGetOption("PreserveJobFiles", num_settings,
			     settings)) == NULL)
      val = "1d";

    cgiSetVariable("PRESERVE_JOB_FILES", val);

  }

  if ((val = cupsGetOption("MaxClients", num_settings, settings)) == NULL)
    val = "100";

  cgiSetVariable("MAX_CLIENTS", val);

  if ((val = cupsGetOption("MaxJobs", num_settings, settings)) == NULL)
    val = "500";

  cgiSetVariable("MAX_JOBS", val);

  if ((val = cupsGetOption("MaxLogSize", num_settings, settings)) == NULL)
    val = "1m";

  cgiSetVariable("MAX_LOG_SIZE", val);

  cupsFreeOptions(num_settings, settings);

 /*
  * Finally, show the main menu template...
  */

  cgiStartHTML(cgiText(_("Administration")));

  cgiCopyTemplateLang("admin.tmpl");

  cgiEndHTML();
}


/*
 * 'do_set_allowed_users()' - Set the allowed/denied users for a queue.
 */

static void
do_set_allowed_users(http_t *http)	/* I - HTTP connection */
{
  int		i;			/* Looping var */
  ipp_t		*request,		/* IPP request */
		*response;		/* IPP response */
  char		uri[HTTP_MAX_URI];	/* Printer URI */
  const char	*printer,		/* Printer name */
		*is_class,		/* Is a class? */
		*users,			/* List of users or groups */
		*type;			/* Allow/deny type */
  int		num_users;		/* Number of users */
  char		*ptr,			/* Pointer into users string */
		*end,			/* Pointer to end of users string */
		quote;			/* Quote character */
  ipp_attribute_t *attr;		/* Attribute */
  static const char * const attrs[] =	/* Requested attributes */
		{
		  "requesting-user-name-allowed",
		  "requesting-user-name-denied"
		};


  is_class = cgiGetVariable("IS_CLASS");
  printer  = cgiGetTextfield("PRINTER_NAME");

  if (!printer)
  {
    cgiSetVariable("ERROR", cgiText(_("Missing form variable")));
    cgiStartHTML(cgiText(_("Set Allowed Users")));
    cgiCopyTemplateLang("error.tmpl");
    cgiEndHTML();
    return;
  }

  users = cgiGetTextfield("users");
  type  = cgiGetVariable("type");

  if (!users || !type ||
      (strcmp(type, "requesting-user-name-allowed") &&
       strcmp(type, "requesting-user-name-denied")))
  {
   /*
    * Build a Get-Printer-Attributes request, which requires the following
    * attributes:
    *
    *    attributes-charset
    *    attributes-natural-language
    *    printer-uri
    *    requested-attributes
    */

    request = ippNewRequest(IPP_GET_PRINTER_ATTRIBUTES);

    httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                     "localhost", 0, is_class ? "/classes/%s" : "/printers/%s",
		     printer);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
        	 NULL, uri);

    ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                  "requested-attributes",
		  (int)(sizeof(attrs) / sizeof(attrs[0])), NULL, attrs);

   /*
    * Do the request and get back a response...
    */

    if ((response = cupsDoRequest(http, request, "/")) != NULL)
    {
      cgiSetIPPVars(response, NULL, NULL, NULL, 0);

      ippDelete(response);
    }

    cgiStartHTML(cgiText(_("Set Allowed Users")));

    if (cupsLastError() == IPP_NOT_AUTHORIZED)
    {
      puts("Status: 401\n");
      exit(0);
    }
    else if (cupsLastError() > IPP_OK_CONFLICT)
      cgiShowIPPError(_("Unable to get printer attributes"));
    else
      cgiCopyTemplateLang("users.tmpl");

    cgiEndHTML();
  }
  else
  {
   /*
    * Save the changes...
    */

    for (num_users = 0, ptr = (char *)users; *ptr; num_users ++)
    {
     /*
      * Skip whitespace and commas...
      */

      while (*ptr == ',' || isspace(*ptr & 255))
	ptr ++;

      if (!*ptr)
        break;

      if (*ptr == '\'' || *ptr == '\"')
      {
       /*
	* Scan quoted name...
	*/

	quote = *ptr++;

	for (end = ptr; *end; end ++)
	  if (*end == quote)
	    break;
      }
      else
      {
       /*
	* Scan space or comma-delimited name...
	*/

        for (end = ptr; *end; end ++)
	  if (isspace(*end & 255) || *end == ',')
	    break;
      }

     /*
      * Advance to the next name...
      */

      ptr = end;
    }

   /*
    * Build a CUPS-Add-Printer/Class request, which requires the following
    * attributes:
    *
    *    attributes-charset
    *    attributes-natural-language
    *    printer-uri
    *    requesting-user-name-{allowed,denied}
    */

    request = ippNewRequest(is_class ? CUPS_ADD_CLASS : CUPS_ADD_PRINTER);

    httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                     "localhost", 0, is_class ? "/classes/%s" : "/printers/%s",
		     printer);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
        	 NULL, uri);

    if (num_users == 0)
      ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_NAME,
                   "requesting-user-name-allowed", NULL, "all");
    else
    {
      attr = ippAddStrings(request, IPP_TAG_PRINTER, IPP_TAG_NAME,
                           type, num_users, NULL, NULL);

      for (i = 0, ptr = (char *)users; *ptr; i ++)
      {
       /*
        * Skip whitespace and commas...
	*/

        while (*ptr == ',' || isspace(*ptr & 255))
	  ptr ++;

        if (!*ptr)
	  break;

        if (*ptr == '\'' || *ptr == '\"')
	{
	 /*
	  * Scan quoted name...
	  */

	  quote = *ptr++;

	  for (end = ptr; *end; end ++)
	    if (*end == quote)
	      break;
	}
	else
	{
	 /*
	  * Scan space or comma-delimited name...
	  */

          for (end = ptr; *end; end ++)
	    if (isspace(*end & 255) || *end == ',')
	      break;
        }

       /*
        * Terminate the name...
	*/

        if (*end)
          *end++ = '\0';

       /*
        * Add the name...
	*/

        ippSetString(request, &attr, i, ptr);

       /*
        * Advance to the next name...
	*/

        ptr = end;
      }
    }

   /*
    * Do the request and get back a response...
    */

    ippDelete(cupsDoRequest(http, request, "/admin/"));

    if (cupsLastError() == IPP_NOT_AUTHORIZED)
    {
      puts("Status: 401\n");
      exit(0);
    }
    else if (cupsLastError() > IPP_OK_CONFLICT)
    {
      cgiStartHTML(cgiText(_("Set Allowed Users")));
      cgiShowIPPError(_("Unable to change printer"));
    }
    else
    {
     /*
      * Redirect successful updates back to the printer page...
      */

      char	url[1024],		/* Printer/class URL */
		refresh[1024];		/* Refresh URL */


      cgiRewriteURL(uri, url, sizeof(url), NULL);
      cgiFormEncode(uri, url, sizeof(uri));
      snprintf(refresh, sizeof(refresh), "5;URL=/admin/?OP=redirect&URL=%s",
               uri);
      cgiSetVariable("refresh_page", refresh);

      cgiStartHTML(cgiText(_("Set Allowed Users")));

      cgiCopyTemplateLang(is_class ? "class-modified.tmpl" :
                                     "printer-modified.tmpl");
    }

    cgiEndHTML();
  }
}


/*
 * 'do_set_default()' - Set the server default printer/class.
 */

static void
do_set_default(http_t *http)		/* I - HTTP connection */
{
  const char	*title;			/* Page title */
  ipp_t		*request;		/* IPP request */
  char		uri[HTTP_MAX_URI];	/* Printer URI */
  const char	*printer,		/* Printer name */
		*is_class;		/* Is a class? */


  is_class = cgiGetVariable("IS_CLASS");
  printer  = cgiGetTextfield("PRINTER_NAME");
  title    = cgiText(_("Set As Server Default"));

  if (!printer)
  {
    cgiSetVariable("ERROR", cgiText(_("Missing form variable")));
    cgiStartHTML(title);
    cgiCopyTemplateLang("error.tmpl");
    cgiEndHTML();
    return;
  }

 /*
  * Build a printer request, which requires the following
  * attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  *    printer-uri
  */

  request = ippNewRequest(CUPS_SET_DEFAULT);

  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                   "localhost", 0, is_class ? "/classes/%s" : "/printers/%s",
		   printer);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
               NULL, uri);

 /*
  * Do the request and get back a response...
  */

  ippDelete(cupsDoRequest(http, request, "/admin/"));

  if (cupsLastError() == IPP_NOT_AUTHORIZED)
  {
    puts("Status: 401\n");
    exit(0);
  }
  else if (cupsLastError() > IPP_OK_CONFLICT)
  {
    cgiStartHTML(title);
    cgiShowIPPError(_("Unable to set server default"));
  }
  else
  {
   /*
    * Redirect successful updates back to the printer page...
    */

    char	url[1024],		/* Printer/class URL */
		refresh[1024];		/* Refresh URL */


    cgiRewriteURL(uri, url, sizeof(url), NULL);
    cgiFormEncode(uri, url, sizeof(uri));
    snprintf(refresh, sizeof(refresh), "5;URL=/admin/?OP=redirect&URL=%s", uri);
    cgiSetVariable("refresh_page", refresh);

    cgiStartHTML(title);
    cgiCopyTemplateLang("printer-default.tmpl");
  }

  cgiEndHTML();
}


/*
 * 'do_set_options()' - Configure the default options for a queue.
 */

static void
do_set_options(http_t *http,		/* I - HTTP connection */
               int    is_class)		/* I - Set options for class? */
{
  int		i, j, k, m;		/* Looping vars */
  int		have_options;		/* Have options? */
  ipp_t		*request,		/* IPP request */
		*response;		/* IPP response */
  ipp_attribute_t *attr;		/* IPP attribute */
  char		uri[HTTP_MAX_URI];	/* Job URI */
  const char	*var;			/* Variable value */
  const char	*printer;		/* Printer printer name */
  const char	*filename;		/* PPD filename */
  char		tempfile[1024];		/* Temporary filename */
  cups_file_t	*in,			/* Input file */
		*out;			/* Output file */
  char		line[1024],		/* Line from PPD file */
		value[1024],		/* Option value */
		keyword[1024],		/* Keyword from Default line */
		*keyptr;		/* Pointer into keyword... */
  ppd_file_t	*ppd;			/* PPD file */
  ppd_group_t	*group;			/* Option group */
  ppd_option_t	*option;		/* Option */
  ppd_coption_t	*coption;		/* Custom option */
  ppd_cparam_t	*cparam;		/* Custom parameter */
  ppd_attr_t	*ppdattr;		/* PPD attribute */
  const char	*title;			/* Page title */


  title = cgiText(is_class ? _("Set Class Options") : _("Set Printer Options"));

  fprintf(stderr, "DEBUG: do_set_options(http=%p, is_class=%d)\n", http,
          is_class);

 /*
  * Get the printer name...
  */

  if ((printer = cgiGetTextfield("PRINTER_NAME")) != NULL)
    httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                     "localhost", 0, is_class ? "/classes/%s" : "/printers/%s",
		     printer);
  else
  {
    cgiSetVariable("ERROR", cgiText(_("Missing form variable")));
    cgiStartHTML(title);
    cgiCopyTemplateLang("error.tmpl");
    cgiEndHTML();
    return;
  }

  fprintf(stderr, "DEBUG: printer=\"%s\", uri=\"%s\"...\n", printer, uri);

 /*
  * If the user clicks on the Auto-Configure button, send an AutoConfigure
  * command file to the printer...
  */

  if (cgiGetVariable("AUTOCONFIGURE"))
  {
    cgiPrintCommand(http, printer, "AutoConfigure", "Set Default Options");
    return;
  }

 /*
  * Get the PPD file...
  */

  if (is_class)
    filename = NULL;
  else
    filename = cupsGetPPD2(http, printer);

  if (filename)
  {
    fprintf(stderr, "DEBUG: Got PPD file: \"%s\"\n", filename);

    if ((ppd = ppdOpenFile(filename)) == NULL)
    {
      cgiSetVariable("ERROR", ppdErrorString(ppdLastError(&i)));
      cgiSetVariable("MESSAGE", cgiText(_("Unable to open PPD file")));
      cgiStartHTML(title);
      cgiCopyTemplateLang("error.tmpl");
      cgiEndHTML();
      return;
    }
  }
  else
  {
    fputs("DEBUG: No PPD file\n", stderr);
    ppd = NULL;
  }

  if (cgiGetVariable("job_sheets_start") != NULL ||
      cgiGetVariable("job_sheets_end") != NULL)
    have_options = 1;
  else
    have_options = 0;

  if (ppd)
  {
    ppdMarkDefaults(ppd);

    for (option = ppdFirstOption(ppd);
         option;
	 option = ppdNextOption(ppd))
    {
      if ((var = cgiGetVariable(option->keyword)) != NULL)
      {
	have_options = 1;
	ppdMarkOption(ppd, option->keyword, var);
	fprintf(stderr, "DEBUG: Set %s to %s...\n", option->keyword, var);
      }
      else
        fprintf(stderr, "DEBUG: Didn't find %s...\n", option->keyword);
    }
  }

  if (!have_options || ppdConflicts(ppd))
  {
   /*
    * Show the options to the user...
    */

    fputs("DEBUG: Showing options...\n", stderr);

   /*
    * Show auto-configure button if supported...
    */

    if (ppd)
    {
      if (ppd->num_filters == 0 ||
          ((ppdattr = ppdFindAttr(ppd, "cupsCommands", NULL)) != NULL &&
           ppdattr->value && strstr(ppdattr->value, "AutoConfigure")))
        cgiSetVariable("HAVE_AUTOCONFIGURE", "YES");
      else
      {
        for (i = 0; i < ppd->num_filters; i ++)
	  if (!strncmp(ppd->filters[i], "application/vnd.cups-postscript", 31))
	  {
	    cgiSetVariable("HAVE_AUTOCONFIGURE", "YES");
	    break;
	  }
      }
    }

   /*
    * Get the printer attributes...
    */

    request = ippNewRequest(IPP_GET_PRINTER_ATTRIBUTES);

    httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                     "localhost", 0, "/printers/%s", printer);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
                 NULL, uri);

    response = cupsDoRequest(http, request, "/");

   /*
    * List the groups used as "tabs"...
    */

    i = 0;

    if (ppd)
    {
      for (group = ppd->groups;
	   i < ppd->num_groups;
	   i ++, group ++)
      {
        cgiSetArray("GROUP_ID", i, group->name);

	if (!strcmp(group->name, "InstallableOptions"))
	  cgiSetArray("GROUP", i, cgiText(_("Options Installed")));
	else
	  cgiSetArray("GROUP", i, group->text);
      }
    }

    if (ippFindAttribute(response, "job-sheets-supported", IPP_TAG_ZERO))
    {
      cgiSetArray("GROUP_ID", i, "CUPS_BANNERS");
      cgiSetArray("GROUP", i ++, cgiText(_("Banners")));
    }

    if (ippFindAttribute(response, "printer-error-policy-supported",
			 IPP_TAG_ZERO) ||
	ippFindAttribute(response, "printer-op-policy-supported",
			 IPP_TAG_ZERO))
    {
      cgiSetArray("GROUP_ID", i, "CUPS_POLICIES");
      cgiSetArray("GROUP", i ++, cgiText(_("Policies")));
    }

    if ((attr = ippFindAttribute(response, "port-monitor-supported",
                                 IPP_TAG_NAME)) != NULL && attr->num_values > 1)
    {
      cgiSetArray("GROUP_ID", i, "CUPS_PORT_MONITOR");
      cgiSetArray("GROUP", i, cgiText(_("Port Monitor")));
    }

    cgiStartHTML(cgiText(_("Set Printer Options")));
    cgiCopyTemplateLang("set-printer-options-header.tmpl");

    if (ppd)
    {
      ppdLocalize(ppd);

      if (ppdConflicts(ppd))
      {
	for (i = ppd->num_groups, k = 0, group = ppd->groups;
	     i > 0;
	     i --, group ++)
	  for (j = group->num_options, option = group->options;
	       j > 0;
	       j --, option ++)
	    if (option->conflicted)
	    {
	      cgiSetArray("ckeyword", k, option->keyword);
	      cgiSetArray("ckeytext", k, option->text);

	      for (m = 0; m < option->num_choices; m ++)
	      {
	        if (option->choices[m].marked)
	        {
	          cgiSetArray("cchoice", k, option->choices[m].text);
	          break;
	        }
              }

	      k ++;
	    }

	cgiCopyTemplateLang("option-conflict.tmpl");
      }

      for (i = ppd->num_groups, group = ppd->groups;
	   i > 0;
	   i --, group ++)
      {
	for (j = group->num_options, option = group->options;
	     j > 0;
	     j --, option ++)
	{
	  if (!strcmp(option->keyword, "PageRegion"))
	    continue;

	  if (option->num_choices > 1)
	    break;
	}

        if (j == 0)
	  continue;

        cgiSetVariable("GROUP_ID", group->name);

	if (!strcmp(group->name, "InstallableOptions"))
	  cgiSetVariable("GROUP", cgiText(_("Options Installed")));
	else
	  cgiSetVariable("GROUP", group->text);

	cgiCopyTemplateLang("option-header.tmpl");

	for (j = group->num_options, option = group->options;
	     j > 0;
	     j --, option ++)
	{
	  if (!strcmp(option->keyword, "PageRegion") || option->num_choices < 2)
	    continue;

	  cgiSetVariable("KEYWORD", option->keyword);
	  cgiSetVariable("KEYTEXT", option->text);

	  if (option->conflicted)
	    cgiSetVariable("CONFLICTED", "1");
	  else
	    cgiSetVariable("CONFLICTED", "0");

	  cgiSetSize("CHOICES", 0);
	  cgiSetSize("TEXT", 0);
	  for (k = 0, m = 0; k < option->num_choices; k ++)
	  {
	    cgiSetArray("CHOICES", m, option->choices[k].choice);
	    cgiSetArray("TEXT", m, option->choices[k].text);

	    m ++;

	    if (option->choices[k].marked)
	      cgiSetVariable("DEFCHOICE", option->choices[k].choice);
	  }

	  cgiSetSize("PARAMS", 0);
	  cgiSetSize("PARAMTEXT", 0);
	  cgiSetSize("PARAMVALUE", 0);
	  cgiSetSize("INPUTTYPE", 0);

	  if ((coption = ppdFindCustomOption(ppd, option->keyword)))
	  {
            const char *units = NULL;	/* Units value, if any */

	    cgiSetVariable("ISCUSTOM", "1");

	    for (cparam = ppdFirstCustomParam(coption), m = 0;
		 cparam;
		 cparam = ppdNextCustomParam(coption), m ++)
	    {
	      if (!_cups_strcasecmp(option->keyword, "PageSize") &&
	          _cups_strcasecmp(cparam->name, "Width") &&
		  _cups_strcasecmp(cparam->name, "Height"))
              {
	        m --;
		continue;
              }

	      cgiSetArray("PARAMS", m, cparam->name);
	      cgiSetArray("PARAMTEXT", m, cparam->text);
	      cgiSetArray("INPUTTYPE", m, "text");

	      switch (cparam->type)
	      {
	        case PPD_CUSTOM_UNKNOWN :
	            break;

		case PPD_CUSTOM_POINTS :
		    if (!_cups_strncasecmp(option->defchoice, "Custom.", 7))
		    {
		      units = option->defchoice + strlen(option->defchoice) - 2;

		      if (strcmp(units, "mm") && strcmp(units, "cm") &&
		          strcmp(units, "in") && strcmp(units, "ft"))
		      {
		        if (units[1] == 'm')
			  units ++;
			else
			  units = "pt";
		      }
		    }
		    else
		      units = "pt";

                    if (!strcmp(units, "mm"))
		      snprintf(value, sizeof(value), "%g",
		               cparam->current.custom_points / 72.0 * 25.4);
                    else if (!strcmp(units, "cm"))
		      snprintf(value, sizeof(value), "%g",
		               cparam->current.custom_points / 72.0 * 2.54);
                    else if (!strcmp(units, "in"))
		      snprintf(value, sizeof(value), "%g",
		               cparam->current.custom_points / 72.0);
                    else if (!strcmp(units, "ft"))
		      snprintf(value, sizeof(value), "%g",
		               cparam->current.custom_points / 72.0 / 12.0);
                    else if (!strcmp(units, "m"))
		      snprintf(value, sizeof(value), "%g",
		               cparam->current.custom_points / 72.0 * 0.0254);
                    else
		      snprintf(value, sizeof(value), "%g",
		               cparam->current.custom_points);
		    cgiSetArray("PARAMVALUE", m, value);
		    break;

		case PPD_CUSTOM_CURVE :
		case PPD_CUSTOM_INVCURVE :
		case PPD_CUSTOM_REAL :
		    snprintf(value, sizeof(value), "%g",
		             cparam->current.custom_real);
		    cgiSetArray("PARAMVALUE", m, value);
		    break;

		case PPD_CUSTOM_INT:
		    snprintf(value, sizeof(value), "%d",
		             cparam->current.custom_int);
		    cgiSetArray("PARAMVALUE", m, value);
		    break;

		case PPD_CUSTOM_PASSCODE:
		case PPD_CUSTOM_PASSWORD:
		    if (cparam->current.custom_password)
		      cgiSetArray("PARAMVALUE", m,
		                  cparam->current.custom_password);
		    else
		      cgiSetArray("PARAMVALUE", m, "");
		    cgiSetArray("INPUTTYPE", m, "password");
		    break;

		case PPD_CUSTOM_STRING:
		    if (cparam->current.custom_string)
		      cgiSetArray("PARAMVALUE", m,
		                  cparam->current.custom_string);
		    else
		      cgiSetArray("PARAMVALUE", m, "");
		    break;
	      }
	    }

            if (units)
	    {
	      cgiSetArray("PARAMS", m, "Units");
	      cgiSetArray("PARAMTEXT", m, cgiText(_("Units")));
	      cgiSetArray("PARAMVALUE", m, units);
	    }
	  }
	  else
	    cgiSetVariable("ISCUSTOM", "0");

	  switch (option->ui)
	  {
	    case PPD_UI_BOOLEAN :
		cgiCopyTemplateLang("option-boolean.tmpl");
		break;
	    case PPD_UI_PICKONE :
		cgiCopyTemplateLang("option-pickone.tmpl");
		break;
	    case PPD_UI_PICKMANY :
		cgiCopyTemplateLang("option-pickmany.tmpl");
		break;
	  }
	}

	cgiCopyTemplateLang("option-trailer.tmpl");
      }
    }

    if ((attr = ippFindAttribute(response, "job-sheets-supported",
				 IPP_TAG_ZERO)) != NULL)
    {
     /*
      * Add the job sheets options...
      */

      cgiSetVariable("GROUP_ID", "CUPS_BANNERS");
      cgiSetVariable("GROUP", cgiText(_("Banners")));
      cgiCopyTemplateLang("option-header.tmpl");

      cgiSetSize("CHOICES", attr->num_values);
      cgiSetSize("TEXT", attr->num_values);
      for (k = 0; k < attr->num_values; k ++)
      {
	cgiSetArray("CHOICES", k, attr->values[k].string.text);
	cgiSetArray("TEXT", k, attr->values[k].string.text);
      }

      attr = ippFindAttribute(response, "job-sheets-default", IPP_TAG_ZERO);

      cgiSetVariable("KEYWORD", "job_sheets_start");
      cgiSetVariable("KEYTEXT",
                     /* TRANSLATORS: Banner/cover sheet before the print job. */
                     cgiText(_("Starting Banner")));
      cgiSetVariable("DEFCHOICE", attr != NULL ?
				  attr->values[0].string.text : "");

      cgiCopyTemplateLang("option-pickone.tmpl");

      cgiSetVariable("KEYWORD", "job_sheets_end");
      cgiSetVariable("KEYTEXT",
                     /* TRANSLATORS: Banner/cover sheet after the print job. */
                     cgiText(_("Ending Banner")));
      cgiSetVariable("DEFCHOICE", attr != NULL && attr->num_values > 1 ?
				  attr->values[1].string.text : "");

      cgiCopyTemplateLang("option-pickone.tmpl");

      cgiCopyTemplateLang("option-trailer.tmpl");
    }

    if (ippFindAttribute(response, "printer-error-policy-supported",
			 IPP_TAG_ZERO) ||
	ippFindAttribute(response, "printer-op-policy-supported",
			 IPP_TAG_ZERO))
    {
     /*
      * Add the error and operation policy options...
      */

      cgiSetVariable("GROUP_ID", "CUPS_POLICIES");
      cgiSetVariable("GROUP", cgiText(_("Policies")));
      cgiCopyTemplateLang("option-header.tmpl");

     /*
      * Error policy...
      */

      attr = ippFindAttribute(response, "printer-error-policy-supported",
			      IPP_TAG_ZERO);

      if (attr)
      {
	cgiSetSize("CHOICES", attr->num_values);
	cgiSetSize("TEXT", attr->num_values);
	for (k = 0; k < attr->num_values; k ++)
	{
	  cgiSetArray("CHOICES", k, attr->values[k].string.text);
	  cgiSetArray("TEXT", k, attr->values[k].string.text);
	}

	attr = ippFindAttribute(response, "printer-error-policy",
				IPP_TAG_ZERO);

	cgiSetVariable("KEYWORD", "printer_error_policy");
	cgiSetVariable("KEYTEXT", cgiText(_("Error Policy")));
	cgiSetVariable("DEFCHOICE", attr == NULL ?
				    "" : attr->values[0].string.text);
      }

      cgiCopyTemplateLang("option-pickone.tmpl");

     /*
      * Operation policy...
      */

      attr = ippFindAttribute(response, "printer-op-policy-supported",
			      IPP_TAG_ZERO);

      if (attr)
      {
	cgiSetSize("CHOICES", attr->num_values);
	cgiSetSize("TEXT", attr->num_values);
	for (k = 0; k < attr->num_values; k ++)
	{
	  cgiSetArray("CHOICES", k, attr->values[k].string.text);
	  cgiSetArray("TEXT", k, attr->values[k].string.text);
	}

	attr = ippFindAttribute(response, "printer-op-policy", IPP_TAG_ZERO);

	cgiSetVariable("KEYWORD", "printer_op_policy");
	cgiSetVariable("KEYTEXT", cgiText(_("Operation Policy")));
	cgiSetVariable("DEFCHOICE", attr == NULL ?
				    "" : attr->values[0].string.text);

	cgiCopyTemplateLang("option-pickone.tmpl");
      }

      cgiCopyTemplateLang("option-trailer.tmpl");
    }

   /*
    * Binary protocol support...
    */

    if ((attr = ippFindAttribute(response, "port-monitor-supported",
                                 IPP_TAG_NAME)) != NULL && attr->num_values > 1)
    {
      cgiSetVariable("GROUP_ID", "CUPS_PORT_MONITOR");
      cgiSetVariable("GROUP", cgiText(_("Port Monitor")));

      cgiSetSize("CHOICES", attr->num_values);
      cgiSetSize("TEXT", attr->num_values);

      for (i = 0; i < attr->num_values; i ++)
      {
        cgiSetArray("CHOICES", i, attr->values[i].string.text);
        cgiSetArray("TEXT", i, attr->values[i].string.text);
      }

      attr = ippFindAttribute(response, "port-monitor", IPP_TAG_NAME);
      cgiSetVariable("KEYWORD", "port_monitor");
      cgiSetVariable("KEYTEXT", cgiText(_("Port Monitor")));
      cgiSetVariable("DEFCHOICE", attr ? attr->values[0].string.text : "none");

      cgiCopyTemplateLang("option-header.tmpl");
      cgiCopyTemplateLang("option-pickone.tmpl");
      cgiCopyTemplateLang("option-trailer.tmpl");
    }

    cgiCopyTemplateLang("set-printer-options-trailer.tmpl");
    cgiEndHTML();

    ippDelete(response);
  }
  else
  {
   /*
    * Set default options...
    */

    fputs("DEBUG: Setting options...\n", stderr);

    if (filename)
    {
      out = cupsTempFile2(tempfile, sizeof(tempfile));
      in  = cupsFileOpen(filename, "r");

      if (!in || !out)
      {
	cgiSetVariable("ERROR", strerror(errno));
	cgiStartHTML(cgiText(_("Set Printer Options")));
	cgiCopyTemplateLang("error.tmpl");
	cgiEndHTML();

	if (in)
	  cupsFileClose(in);

	if (out)
	{
	  cupsFileClose(out);
	  unlink(tempfile);
	}

	unlink(filename);
	return;
      }

      while (cupsFileGets(in, line, sizeof(line)))
      {
	if (!strncmp(line, "*cupsProtocol:", 14))
	  continue;
	else if (strncmp(line, "*Default", 8))
	  cupsFilePrintf(out, "%s\n", line);
	else
	{
	 /*
	  * Get default option name...
	  */

	  strlcpy(keyword, line + 8, sizeof(keyword));

	  for (keyptr = keyword; *keyptr; keyptr ++)
	    if (*keyptr == ':' || isspace(*keyptr & 255))
	      break;

	  *keyptr = '\0';

	  if (!strcmp(keyword, "PageRegion") ||
	      !strcmp(keyword, "PaperDimension") ||
	      !strcmp(keyword, "ImageableArea"))
	    var = get_option_value(ppd, "PageSize", value, sizeof(value));
	  else
	    var = get_option_value(ppd, keyword, value, sizeof(value));

	  if (!var)
	    cupsFilePrintf(out, "%s\n", line);
	  else
	    cupsFilePrintf(out, "*Default%s: %s\n", keyword, var);
	}
      }

      cupsFileClose(in);
      cupsFileClose(out);
    }
    else
    {
     /*
      * Make sure temporary filename is cleared when there is no PPD...
      */

      tempfile[0] = '\0';
    }

   /*
    * Build a CUPS_ADD_MODIFY_CLASS/PRINTER request, which requires the
    * following attributes:
    *
    *    attributes-charset
    *    attributes-natural-language
    *    printer-uri
    *    job-sheets-default
    *    printer-error-policy
    *    printer-op-policy
    *    [ppd file]
    */

    request = ippNewRequest(is_class ? CUPS_ADD_MODIFY_CLASS :
                                       CUPS_ADD_MODIFY_PRINTER);

    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
                 NULL, uri);

    attr = ippAddStrings(request, IPP_TAG_PRINTER, IPP_TAG_NAME,
                         "job-sheets-default", 2, NULL, NULL);
    ippSetString(request, &attr, 0, cgiGetVariable("job_sheets_start"));
    ippSetString(request, &attr, 1, cgiGetVariable("job_sheets_end"));

    if ((var = cgiGetVariable("printer_error_policy")) != NULL)
      ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_NAME,
		   "printer-error-policy", NULL, var);

    if ((var = cgiGetVariable("printer_op_policy")) != NULL)
      ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_NAME,
		   "printer-op-policy", NULL, var);

    if ((var = cgiGetVariable("port_monitor")) != NULL)
      ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_NAME,
		   "port-monitor", NULL, var);

   /*
    * Do the request and get back a response...
    */

    if (filename)
      ippDelete(cupsDoFileRequest(http, request, "/admin/", tempfile));
    else
      ippDelete(cupsDoRequest(http, request, "/admin/"));

    if (cupsLastError() == IPP_NOT_AUTHORIZED)
    {
      puts("Status: 401\n");
      exit(0);
    }
    else if (cupsLastError() > IPP_OK_CONFLICT)
    {
      cgiStartHTML(title);
      cgiShowIPPError(_("Unable to set options"));
    }
    else
    {
     /*
      * Redirect successful updates back to the printer page...
      */

      char	refresh[1024];		/* Refresh URL */


      cgiFormEncode(uri, printer, sizeof(uri));
      snprintf(refresh, sizeof(refresh), "5;URL=/admin/?OP=redirect&URL=/%s/%s",
	       is_class ? "classes" : "printers", uri);
      cgiSetVariable("refresh_page", refresh);

      cgiStartHTML(title);

      cgiCopyTemplateLang("printer-configured.tmpl");
    }

    cgiEndHTML();

    if (filename)
      unlink(tempfile);
  }

  if (filename)
    unlink(filename);
}


/*
 * 'get_option_value()' - Return the value of an option.
 *
 * This function also handles generation of custom option values.
 */

static char *				/* O - Value string or NULL on error */
get_option_value(
    ppd_file_t    *ppd,			/* I - PPD file */
    const char    *name,		/* I - Option name */
    char          *buffer,		/* I - String buffer */
    size_t        bufsize)		/* I - Size of buffer */
{
  char		*bufptr,		/* Pointer into buffer */
		*bufend;		/* End of buffer */
  ppd_coption_t *coption;		/* Custom option */
  ppd_cparam_t	*cparam;		/* Current custom parameter */
  char		keyword[256];		/* Parameter name */
  const char	*val,			/* Parameter value */
		*uval;			/* Units value */
  long		integer;		/* Integer value */
  double	number,			/* Number value */
		number_points;		/* Number in points */


 /*
  * See if we have a custom option choice...
  */

  if ((val = cgiGetVariable(name)) == NULL)
  {
   /*
    * Option not found!
    */

    return (NULL);
  }
  else if (_cups_strcasecmp(val, "Custom") ||
           (coption = ppdFindCustomOption(ppd, name)) == NULL)
  {
   /*
    * Not a custom choice...
    */

    strlcpy(buffer, val, bufsize);
    return (buffer);
  }

 /*
  * OK, we have a custom option choice, format it...
  */

  *buffer = '\0';

  if (!strcmp(coption->keyword, "PageSize"))
  {
    const char	*lval;			/* Length string value */
    double	width,			/* Width value */
		width_points,		/* Width in points */
		length,			/* Length value */
		length_points;		/* Length in points */


    val  = cgiGetVariable("PageSize.Width");
    lval = cgiGetVariable("PageSize.Height");
    uval = cgiGetVariable("PageSize.Units");

    if (!val || !lval || !uval ||
        (width = atof(val)) == 0.0 ||
        (length = atof(lval)) == 0.0 ||
        (strcmp(uval, "pt") && strcmp(uval, "in") && strcmp(uval, "ft") &&
	 strcmp(uval, "cm") && strcmp(uval, "mm") && strcmp(uval, "m")))
      return (NULL);

    width_points  = get_points(width, uval);
    length_points = get_points(length, uval);

    if (width_points < ppd->custom_min[0] ||
        width_points > ppd->custom_max[0] ||
        length_points < ppd->custom_min[1] ||
	length_points > ppd->custom_max[1])
      return (NULL);

    snprintf(buffer, bufsize, "Custom.%gx%g%s", width, length, uval);
  }
  else if (cupsArrayCount(coption->params) == 1)
  {
    cparam = ppdFirstCustomParam(coption);
    snprintf(keyword, sizeof(keyword), "%s.%s", coption->keyword, cparam->name);

    if ((val = cgiGetVariable(keyword)) == NULL)
      return (NULL);

    switch (cparam->type)
    {
      case PPD_CUSTOM_UNKNOWN :
	  break;

      case PPD_CUSTOM_CURVE :
      case PPD_CUSTOM_INVCURVE :
      case PPD_CUSTOM_REAL :
	  if ((number = atof(val)) == 0.0 ||
	      number < cparam->minimum.custom_real ||
	      number > cparam->maximum.custom_real)
	    return (NULL);

          snprintf(buffer, bufsize, "Custom.%g", number);
          break;

      case PPD_CUSTOM_INT :
          if (!*val || (integer = strtol(val, NULL, 10)) == LONG_MIN ||
	      integer == LONG_MAX ||
	      integer < cparam->minimum.custom_int ||
	      integer > cparam->maximum.custom_int)
            return (NULL);

          snprintf(buffer, bufsize, "Custom.%ld", integer);
          break;

      case PPD_CUSTOM_POINTS :
          snprintf(keyword, sizeof(keyword), "%s.Units", coption->keyword);

	  if ((number = atof(val)) == 0.0 ||
	      (uval = cgiGetVariable(keyword)) == NULL ||
	      (strcmp(uval, "pt") && strcmp(uval, "in") && strcmp(uval, "ft") &&
	       strcmp(uval, "cm") && strcmp(uval, "mm") && strcmp(uval, "m")))
	    return (NULL);

	  number_points = get_points(number, uval);
	  if (number_points < cparam->minimum.custom_points ||
	      number_points > cparam->maximum.custom_points)
	    return (NULL);

	  snprintf(buffer, bufsize, "Custom.%g%s", number, uval);
          break;

      case PPD_CUSTOM_PASSCODE :
          for (uval = val; *uval; uval ++)
	    if (!isdigit(*uval & 255))
	      return (NULL);

      case PPD_CUSTOM_PASSWORD :
      case PPD_CUSTOM_STRING :
          integer = (long)strlen(val);
	  if (integer < cparam->minimum.custom_string ||
	      integer > cparam->maximum.custom_string)
	    return (NULL);

          snprintf(buffer, bufsize, "Custom.%s", val);
	  break;
    }
  }
  else
  {
    const char *prefix = "{";		/* Prefix string */


    bufptr = buffer;
    bufend = buffer + bufsize;

    for (cparam = ppdFirstCustomParam(coption);
	 cparam;
	 cparam = ppdNextCustomParam(coption))
    {
      snprintf(keyword, sizeof(keyword), "%s.%s", coption->keyword,
               cparam->name);

      if ((val = cgiGetVariable(keyword)) == NULL)
	return (NULL);

      snprintf(bufptr, (size_t)(bufend - bufptr), "%s%s=", prefix, cparam->name);
      bufptr += strlen(bufptr);
      prefix = " ";

      switch (cparam->type)
      {
	case PPD_CUSTOM_UNKNOWN :
	    break;

	case PPD_CUSTOM_CURVE :
	case PPD_CUSTOM_INVCURVE :
	case PPD_CUSTOM_REAL :
	    if ((number = atof(val)) == 0.0 ||
		number < cparam->minimum.custom_real ||
		number > cparam->maximum.custom_real)
	      return (NULL);

	    snprintf(bufptr, (size_t)(bufend - bufptr), "%g", number);
	    break;

	case PPD_CUSTOM_INT :
	    if (!*val || (integer = strtol(val, NULL, 10)) == LONG_MIN ||
		integer == LONG_MAX ||
		integer < cparam->minimum.custom_int ||
		integer > cparam->maximum.custom_int)
	      return (NULL);

	    snprintf(bufptr, (size_t)(bufend - bufptr), "%ld", integer);
	    break;

	case PPD_CUSTOM_POINTS :
	    snprintf(keyword, sizeof(keyword), "%s.Units", coption->keyword);

	    if ((number = atof(val)) == 0.0 ||
		(uval = cgiGetVariable(keyword)) == NULL ||
		(strcmp(uval, "pt") && strcmp(uval, "in") &&
		 strcmp(uval, "ft") && strcmp(uval, "cm") &&
		 strcmp(uval, "mm") && strcmp(uval, "m")))
	      return (NULL);

	    number_points = get_points(number, uval);
	    if (number_points < cparam->minimum.custom_points ||
		number_points > cparam->maximum.custom_points)
	      return (NULL);

	    snprintf(bufptr, (size_t)(bufend - bufptr), "%g%s", number, uval);
	    break;

	case PPD_CUSTOM_PASSCODE :
	    for (uval = val; *uval; uval ++)
	      if (!isdigit(*uval & 255))
		return (NULL);

	case PPD_CUSTOM_PASSWORD :
	case PPD_CUSTOM_STRING :
	    integer = (long)strlen(val);
	    if (integer < cparam->minimum.custom_string ||
		integer > cparam->maximum.custom_string)
	      return (NULL);

	    if ((bufptr + 2) > bufend)
	      return (NULL);

	    bufend --;
	    *bufptr++ = '\"';

	    while (*val && bufptr < bufend)
	    {
	      if (*val == '\\' || *val == '\"')
	      {
		if ((bufptr + 1) >= bufend)
		  return (NULL);

		*bufptr++ = '\\';
	      }

	      *bufptr++ = *val++;
	    }

	    if (bufptr >= bufend)
	      return (NULL);

	    *bufptr++ = '\"';
	    *bufptr   = '\0';
	    bufend ++;
	    break;
      }

      bufptr += strlen(bufptr);
    }

    if (bufptr == buffer || (bufend - bufptr) < 2)
      return (NULL);

    bufptr[0] = '}';
    bufptr[1] = '\0';
  }

  return (buffer);
}


/*
 * 'get_points()' - Get a value in points.
 */

static double				/* O - Number in points */
get_points(double     number,		/* I - Original number */
           const char *uval)		/* I - Units */
{
  if (!strcmp(uval, "mm"))		/* Millimeters */
    return (number * 72.0 / 25.4);
  else if (!strcmp(uval, "cm"))		/* Centimeters */
    return (number * 72.0 / 2.54);
  else if (!strcmp(uval, "in"))		/* Inches */
    return (number * 72.0);
  else if (!strcmp(uval, "ft"))		/* Feet */
    return (number * 72.0 * 12.0);
  else if (!strcmp(uval, "m"))		/* Meters */
    return (number * 72.0 / 0.0254);
  else					/* Points */
    return (number);
}
