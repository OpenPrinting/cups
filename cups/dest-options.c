/*
 * Destination option/media support for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2012-2019 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "cups-private.h"
#include "debug-internal.h"


/*
 * Local constants...
 */

#define _CUPS_MEDIA_READY_TTL	30	/* Life of xxx-ready values */


/*
 * Local functions...
 */

static void		cups_add_dconstres(cups_array_t *a, ipp_t *collection);
static int		cups_collection_contains(ipp_t *test, ipp_t *match);
static size_t		cups_collection_string(ipp_attribute_t *attr, char *buffer, size_t bufsize) _CUPS_NONNULL((1,2));
static int		cups_compare_dconstres(_cups_dconstres_t *a,
			                       _cups_dconstres_t *b);
static int		cups_compare_media_db(_cups_media_db_t *a,
			                      _cups_media_db_t *b);
static _cups_media_db_t	*cups_copy_media_db(_cups_media_db_t *mdb);
static void		cups_create_cached(http_t *http, cups_dinfo_t *dinfo,
			                   unsigned flags);
static void		cups_create_constraints(cups_dinfo_t *dinfo);
static void		cups_create_defaults(cups_dinfo_t *dinfo);
static void		cups_create_media_db(cups_dinfo_t *dinfo,
			                     unsigned flags);
static void		cups_free_media_db(_cups_media_db_t *mdb);
static int		cups_get_media_db(http_t *http, cups_dinfo_t *dinfo,
			                  pwg_media_t *pwg, unsigned flags,
			                  cups_size_t *size);
static int		cups_is_close_media_db(_cups_media_db_t *a,
			                       _cups_media_db_t *b);
static cups_array_t	*cups_test_constraints(cups_dinfo_t *dinfo,
					       const char *new_option,
					       const char *new_value,
					       int num_options,
					       cups_option_t *options,
					       int *num_conflicts,
					       cups_option_t **conflicts);
static void		cups_update_ready(http_t *http, cups_dinfo_t *dinfo);


/*
 * 'cupsAddDestMediaOptions()' - Add the option corresponding to the specified media size.
 *
 * @since CUPS 2.3/macOS 10.14@
 */

int					/* O  - New number of options */
cupsAddDestMediaOptions(
    http_t        *http,		/* I  - Connection to destination */
    cups_dest_t   *dest,		/* I  - Destination */
    cups_dinfo_t  *dinfo,		/* I  - Destination information */
    unsigned      flags,		/* I  - Media matching flags */
    cups_size_t   *size,		/* I  - Media size */
    int           num_options,		/* I  - Current number of options */
    cups_option_t **options)		/* IO - Options */
{
  cups_array_t		*db;		/* Media database */
  _cups_media_db_t	*mdb;		/* Media database entry */
  char			value[2048];	/* Option value */


 /*
  * Range check input...
  */

  if (!http || !dest || !dinfo || !size || !options)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (num_options);
  }

 /*
  * Find the matching media size...
  */

  if (flags & CUPS_MEDIA_FLAGS_READY)
    db = dinfo->ready_db;
  else
    db = dinfo->media_db;

  DEBUG_printf(("1cupsAddDestMediaOptions: size->media=\"%s\"", size->media));

  for (mdb = (_cups_media_db_t *)cupsArrayFirst(db); mdb; mdb = (_cups_media_db_t *)cupsArrayNext(db))
  {
    if (mdb->key && !strcmp(mdb->key, size->media))
      break;
    else if (mdb->size_name && !strcmp(mdb->size_name, size->media))
      break;
  }

  if (!mdb)
  {
    for (mdb = (_cups_media_db_t *)cupsArrayFirst(db); mdb; mdb = (_cups_media_db_t *)cupsArrayNext(db))
    {
      if (mdb->width == size->width && mdb->length == size->length && mdb->bottom == size->bottom && mdb->left == size->left && mdb->right == size->right && mdb->top == size->top)
	break;
    }
  }

  if (!mdb)
  {
    for (mdb = (_cups_media_db_t *)cupsArrayFirst(db); mdb; mdb = (_cups_media_db_t *)cupsArrayNext(db))
    {
      if (mdb->width == size->width && mdb->length == size->length)
	break;
    }
  }

  if (!mdb)
  {
    DEBUG_puts("1cupsAddDestMediaOptions: Unable to find matching size.");
    return (num_options);
  }

  DEBUG_printf(("1cupsAddDestMediaOptions: MATCH mdb%p [key=\"%s\" size_name=\"%s\" source=\"%s\" type=\"%s\" width=%d length=%d B%d L%d R%d T%d]", (void *)mdb, mdb->key, mdb->size_name, mdb->source, mdb->type, mdb->width, mdb->length, mdb->bottom, mdb->left, mdb->right, mdb->top));

  if (mdb->source)
  {
    if (mdb->type)
      snprintf(value, sizeof(value), "{media-size={x-dimension=%d y-dimension=%d} media-bottom-margin=%d media-left-margin=%d media-right-margin=%d media-top-margin=%d media-source=\"%s\" media-type=\"%s\"}", mdb->width, mdb->length, mdb->bottom, mdb->left, mdb->right, mdb->top, mdb->source, mdb->type);
    else
      snprintf(value, sizeof(value), "{media-size={x-dimension=%d y-dimension=%d} media-bottom-margin=%d media-left-margin=%d media-right-margin=%d media-top-margin=%d media-source=\"%s\"}", mdb->width, mdb->length, mdb->bottom, mdb->left, mdb->right, mdb->top, mdb->source);
  }
  else if (mdb->type)
  {
    snprintf(value, sizeof(value), "{media-size={x-dimension=%d y-dimension=%d} media-bottom-margin=%d media-left-margin=%d media-right-margin=%d media-top-margin=%d media-type=\"%s\"}", mdb->width, mdb->length, mdb->bottom, mdb->left, mdb->right, mdb->top, mdb->type);
  }
  else
  {
    snprintf(value, sizeof(value), "{media-size={x-dimension=%d y-dimension=%d} media-bottom-margin=%d media-left-margin=%d media-right-margin=%d media-top-margin=%d}", mdb->width, mdb->length, mdb->bottom, mdb->left, mdb->right, mdb->top);
  }

  num_options = cupsAddOption("media-col", value, num_options, options);

  return (num_options);
}


/*
 * 'cupsCheckDestSupported()' - Check that the option and value are supported
 *                              by the destination.
 *
 * Returns 1 if supported, 0 otherwise.
 *
 * @since CUPS 1.6/macOS 10.8@
 */

int					/* O - 1 if supported, 0 otherwise */
cupsCheckDestSupported(
    http_t       *http,			/* I - Connection to destination */
    cups_dest_t  *dest,			/* I - Destination */
    cups_dinfo_t *dinfo,		/* I - Destination information */
    const char   *option,		/* I - Option */
    const char   *value)		/* I - Value or @code NULL@ */
{
  int			i;		/* Looping var */
  char			temp[1024];	/* Temporary string */
  int			int_value;	/* Integer value */
  int			xres_value,	/* Horizontal resolution */
			yres_value;	/* Vertical resolution */
  ipp_res_t		units_value;	/* Resolution units */
  ipp_attribute_t	*attr;		/* Attribute */
  _ipp_value_t		*attrval;	/* Current attribute value */
  _ipp_option_t		*map;		/* Option mapping information */


 /*
  * Get the default connection as needed...
  */

  if (!http)
    http = _cupsConnect();

 /*
  * Range check input...
  */

  if (!http || !dest || !dinfo || !option)
    return (0);

 /*
  * Lookup the attribute...
  */

  if (strstr(option, "-supported"))
    attr = ippFindAttribute(dinfo->attrs, option, IPP_TAG_ZERO);
  else
  {
    snprintf(temp, sizeof(temp), "%s-supported", option);
    attr = ippFindAttribute(dinfo->attrs, temp, IPP_TAG_ZERO);
  }

  if (!attr)
    return (0);

  if (!value)
    return (1);

/*
  * Compare values...
  */

  if (!strcmp(option, "media") && !strncmp(value, "custom_", 7))
  {
   /*
    * Check range of custom media sizes...
    */

    pwg_media_t	*pwg;		/* Current PWG media size info */
    int		min_width,	/* Minimum width */
		min_length,	/* Minimum length */
		max_width,	/* Maximum width */
		max_length;	/* Maximum length */

   /*
    * Get the minimum and maximum size...
    */

    min_width = min_length = INT_MAX;
    max_width = max_length = 0;

    for (i = attr->num_values, attrval = attr->values;
	 i > 0;
	 i --, attrval ++)
    {
      if (!strncmp(attrval->string.text, "custom_min_", 11) &&
          (pwg = pwgMediaForPWG(attrval->string.text)) != NULL)
      {
        min_width  = pwg->width;
        min_length = pwg->length;
      }
      else if (!strncmp(attrval->string.text, "custom_max_", 11) &&
	       (pwg = pwgMediaForPWG(attrval->string.text)) != NULL)
      {
        max_width  = pwg->width;
        max_length = pwg->length;
      }
    }

   /*
    * Check the range...
    */

    if (min_width < INT_MAX && max_width > 0 &&
        (pwg = pwgMediaForPWG(value)) != NULL &&
        pwg->width >= min_width && pwg->width <= max_width &&
        pwg->length >= min_length && pwg->length <= max_length)
      return (1);
  }
  else
  {
   /*
    * Check literal values...
    */

    map = _ippFindOption(option);

    switch (attr->value_tag)
    {
      case IPP_TAG_INTEGER :
          if (map && map->value_tag == IPP_TAG_STRING)
            return (strlen(value) <= (size_t)attr->values[0].integer);

      case IPP_TAG_ENUM :
          int_value = atoi(value);

          for (i = 0; i < attr->num_values; i ++)
            if (attr->values[i].integer == int_value)
              return (1);
          break;

      case IPP_TAG_BOOLEAN :
          return (attr->values[0].boolean);

      case IPP_TAG_RANGE :
          if (map && map->value_tag == IPP_TAG_STRING)
            int_value = (int)strlen(value);
          else
            int_value = atoi(value);

          for (i = 0; i < attr->num_values; i ++)
            if (int_value >= attr->values[i].range.lower &&
                int_value <= attr->values[i].range.upper)
              return (1);
          break;

      case IPP_TAG_RESOLUTION :
          if (sscanf(value, "%dx%d%15s", &xres_value, &yres_value, temp) != 3)
          {
            if (sscanf(value, "%d%15s", &xres_value, temp) != 2)
              return (0);

            yres_value = xres_value;
          }

          if (!strcmp(temp, "dpi"))
            units_value = IPP_RES_PER_INCH;
          else if (!strcmp(temp, "dpc") || !strcmp(temp, "dpcm"))
            units_value = IPP_RES_PER_CM;
          else
            return (0);

          for (i = attr->num_values, attrval = attr->values;
               i > 0;
               i --, attrval ++)
          {
            if (attrval->resolution.xres == xres_value &&
                attrval->resolution.yres == yres_value &&
                attrval->resolution.units == units_value)
              return (1);
          }
          break;

      case IPP_TAG_TEXT :
      case IPP_TAG_NAME :
      case IPP_TAG_KEYWORD :
      case IPP_TAG_CHARSET :
      case IPP_TAG_URI :
      case IPP_TAG_URISCHEME :
      case IPP_TAG_MIMETYPE :
      case IPP_TAG_LANGUAGE :
      case IPP_TAG_TEXTLANG :
      case IPP_TAG_NAMELANG :
          for (i = 0; i < attr->num_values; i ++)
            if (!strcmp(attr->values[i].string.text, value))
              return (1);
          break;

      default :
          break;
    }
  }

 /*
  * If we get there the option+value is not supported...
  */

  return (0);
}


/*
 * 'cupsCopyDestConflicts()' - Get conflicts and resolutions for a new
 *                             option/value pair.
 *
 * "num_options" and "options" represent the currently selected options by the
 * user.  "new_option" and "new_value" are the setting the user has just
 * changed.
 *
 * Returns 1 if there is a conflict, 0 if there are no conflicts, and -1 if
 * there was an unrecoverable error such as a resolver loop.
 *
 * If "num_conflicts" and "conflicts" are not @code NULL@, they are set to
 * contain the list of conflicting option/value pairs.  Similarly, if
 * "num_resolved" and "resolved" are not @code NULL@ they will be set to the
 * list of changes needed to resolve the conflict.
 *
 * If cupsCopyDestConflicts returns 1 but "num_resolved" and "resolved" are set
 * to 0 and @code NULL@, respectively, then the conflict cannot be resolved.
 *
 * @since CUPS 1.6/macOS 10.8@
 */

int					/* O - 1 if there is a conflict, 0 if none, -1 on error */
cupsCopyDestConflicts(
    http_t        *http,		/* I - Connection to destination */
    cups_dest_t   *dest,		/* I - Destination */
    cups_dinfo_t  *dinfo,		/* I - Destination information */
    int           num_options,		/* I - Number of current options */
    cups_option_t *options,		/* I - Current options */
    const char    *new_option,		/* I - New option */
    const char    *new_value,		/* I - New value */
    int           *num_conflicts,	/* O - Number of conflicting options */
    cups_option_t **conflicts,		/* O - Conflicting options */
    int           *num_resolved,	/* O - Number of options to resolve */
    cups_option_t **resolved)		/* O - Resolved options */
{
  int		i,			/* Looping var */
		have_conflicts = 0,	/* Do we have conflicts? */
		changed,		/* Did we change something? */
		tries,			/* Number of tries for resolution */
		num_myconf = 0,		/* My number of conflicting options */
		num_myres = 0;		/* My number of resolved options */
  cups_option_t	*myconf = NULL,		/* My conflicting options */
		*myres = NULL,		/* My resolved options */
		*myoption,		/* My current option */
		*option;		/* Current option */
  cups_array_t	*active = NULL,		/* Active conflicts */
		*pass = NULL,		/* Resolvers for this pass */
		*resolvers = NULL,	/* Resolvers we have used */
		*test;			/* Test array for conflicts */
  _cups_dconstres_t *c,			/* Current constraint */
		*r;			/* Current resolver */
  ipp_attribute_t *attr;		/* Current attribute */
  char		value[2048];		/* Current attribute value as string */
  const char	*myvalue;		/* Current value of an option */


 /*
  * Clear returned values...
  */

  if (num_conflicts)
    *num_conflicts = 0;

  if (conflicts)
    *conflicts = NULL;

  if (num_resolved)
    *num_resolved = 0;

  if (resolved)
    *resolved = NULL;

 /*
  * Get the default connection as needed...
  */

  if (!http)
    http = _cupsConnect();

 /*
  * Range check input...
  */

  if (!http || !dest || !dinfo ||
      (num_conflicts != NULL) != (conflicts != NULL) ||
      (num_resolved != NULL) != (resolved != NULL))
    return (0);

 /*
  * Load constraints as needed...
  */

  if (!dinfo->constraints)
    cups_create_constraints(dinfo);

  if (cupsArrayCount(dinfo->constraints) == 0)
    return (0);

  if (!dinfo->num_defaults)
    cups_create_defaults(dinfo);

 /*
  * If we are resolving, create a shadow array...
  */

  if (num_resolved)
  {
    for (i = num_options, option = options; i > 0; i --, option ++)
      num_myres = cupsAddOption(option->name, option->value, num_myres, &myres);

    if (new_option && new_value)
      num_myres = cupsAddOption(new_option, new_value, num_myres, &myres);
  }
  else
  {
    num_myres = num_options;
    myres     = options;
  }

 /*
  * Check for any conflicts...
  */

  if (num_resolved)
    pass = cupsArrayNew((cups_array_func_t)cups_compare_dconstres, NULL);

  for (tries = 0; tries < 100; tries ++)
  {
   /*
    * Check for any conflicts...
    */

    if (num_conflicts || num_resolved)
    {
      cupsFreeOptions(num_myconf, myconf);

      num_myconf = 0;
      myconf     = NULL;
      active     = cups_test_constraints(dinfo, new_option, new_value,
                                         num_myres, myres, &num_myconf,
                                         &myconf);
    }
    else
      active = cups_test_constraints(dinfo, new_option, new_value, num_myres,
				     myres, NULL, NULL);

    have_conflicts = (active != NULL);

    if (!active || !num_resolved)
      break;				/* All done */

   /*
    * Scan the constraints that were triggered to apply resolvers...
    */

    if (!resolvers)
      resolvers = cupsArrayNew((cups_array_func_t)cups_compare_dconstres, NULL);

    for (c = (_cups_dconstres_t *)cupsArrayFirst(active), changed = 0;
         c;
         c = (_cups_dconstres_t *)cupsArrayNext(active))
    {
      if (cupsArrayFind(pass, c))
        continue;			/* Already applied this resolver... */

      if (cupsArrayFind(resolvers, c))
      {
        DEBUG_printf(("1cupsCopyDestConflicts: Resolver loop with %s.",
                      c->name));
        have_conflicts = -1;
        goto cleanup;
      }

      if ((r = cupsArrayFind(dinfo->resolvers, c)) == NULL)
      {
        DEBUG_printf(("1cupsCopyDestConflicts: Resolver %s not found.",
                      c->name));
        have_conflicts = -1;
        goto cleanup;
      }

     /*
      * Add the options from the resolver...
      */

      cupsArrayAdd(pass, r);
      cupsArrayAdd(resolvers, r);

      for (attr = ippFirstAttribute(r->collection);
           attr;
           attr = ippNextAttribute(r->collection))
      {
        if (new_option && !strcmp(attr->name, new_option))
          continue;			/* Ignore this if we just changed it */

        if (ippAttributeString(attr, value, sizeof(value)) >= sizeof(value))
          continue;			/* Ignore if the value is too long */

        if ((test = cups_test_constraints(dinfo, attr->name, value, num_myres,
                                          myres, NULL, NULL)) == NULL)
        {
         /*
          * That worked, flag it...
          */

          changed = 1;
        }
        else
          cupsArrayDelete(test);

       /*
	* Add the option/value from the resolver regardless of whether it
	* worked; this makes sure that we can cascade several changes to
	* make things resolve...
	*/

	num_myres = cupsAddOption(attr->name, value, num_myres, &myres);
      }
    }

    if (!changed)
    {
      DEBUG_puts("1cupsCopyDestConflicts: Unable to resolve constraints.");
      have_conflicts = -1;
      goto cleanup;
    }

    cupsArrayClear(pass);

    cupsArrayDelete(active);
    active = NULL;
  }

  if (tries >= 100)
  {
    DEBUG_puts("1cupsCopyDestConflicts: Unable to resolve after 100 tries.");
    have_conflicts = -1;
    goto cleanup;
  }

 /*
  * Copy resolved options as needed...
  */

  if (num_resolved)
  {
    for (i = num_myres, myoption = myres; i > 0; i --, myoption ++)
    {
      if ((myvalue = cupsGetOption(myoption->name, num_options,
                                   options)) == NULL ||
          strcmp(myvalue, myoption->value))
      {
        if (new_option && !strcmp(new_option, myoption->name) &&
            new_value && !strcmp(new_value, myoption->value))
          continue;

        *num_resolved = cupsAddOption(myoption->name, myoption->value,
                                      *num_resolved, resolved);
      }
    }
  }

 /*
  * Clean up...
  */

  cleanup:

  cupsArrayDelete(active);
  cupsArrayDelete(pass);
  cupsArrayDelete(resolvers);

  if (num_resolved)
  {
   /*
    * Free shadow copy of options...
    */

    cupsFreeOptions(num_myres, myres);
  }

  if (num_conflicts)
  {
   /*
    * Return conflicting options to caller...
    */

    *num_conflicts = num_myconf;
    *conflicts     = myconf;
  }
  else
  {
   /*
    * Free conflicting options...
    */

    cupsFreeOptions(num_myconf, myconf);
  }

  return (have_conflicts);
}


/*
 * 'cupsCopyDestInfo()' - Get the supported values/capabilities for the
 *                        destination.
 *
 * The caller is responsible for calling @link cupsFreeDestInfo@ on the return
 * value. @code NULL@ is returned on error.
 *
 * @since CUPS 1.6/macOS 10.8@
 */

cups_dinfo_t *				/* O - Destination information */
cupsCopyDestInfo(
    http_t      *http,			/* I - Connection to destination */
    cups_dest_t *dest)			/* I - Destination */
{
  cups_dinfo_t	*dinfo;			/* Destination information */
  unsigned	dflags;			/* Destination flags */
  ipp_t		*request,		/* Get-Printer-Attributes request */
		*response;		/* Supported attributes */
  int		tries,			/* Number of tries so far */
		delay,			/* Current retry delay */
		prev_delay;		/* Next retry delay */
  const char	*uri;			/* Printer URI */
  char		resource[1024];		/* URI resource path */
  int		version;		/* IPP version */
  ipp_status_t	status;			/* Status of request */
  _cups_globals_t *cg = _cupsGlobals();	/* Pointer to library globals */
  static const char * const requested_attrs[] =
  {					/* Requested attributes */
    "job-template",
    "media-col-database",
    "printer-description"
  };


  DEBUG_printf(("cupsCopyDestInfo(http=%p, dest=%p(%s))", (void *)http, (void *)dest, dest ? dest->name : ""));

 /*
  * Range check input...
  */

  if (!dest)
    return (NULL);

 /*
  * Get the default connection as needed...
  */

  if (!http)
  {
    DEBUG_puts("1cupsCopyDestInfo: Default server connection.");
    http   = _cupsConnect();
    dflags = CUPS_DEST_FLAGS_NONE;

    if (!http)
      return (NULL);
  }
#ifdef AF_LOCAL
  else if (httpAddrFamily(http->hostaddr) == AF_LOCAL)
  {
    DEBUG_puts("1cupsCopyDestInfo: Connection to server (domain socket).");
    dflags = CUPS_DEST_FLAGS_NONE;
  }
#endif /* AF_LOCAL */
  else
  {
    // Guess the destination flags based on the printer URI's host and port...
    char	scheme[32],		/* URI scheme */
		userpass[256],		/* URI username:password */
		host[256];		/* URI host */
    int		port;			/* URI port */

    if ((uri = cupsGetOption("printer-uri-supported", dest->num_options, dest->options)) == NULL || httpSeparateURI(HTTP_URI_CODING_ALL, uri, scheme, sizeof(scheme), userpass, sizeof(userpass), host, sizeof(host), &port, resource, sizeof(resource)) < HTTP_URI_STATUS_OK)
    {
      strlcpy(host, "localhost", sizeof(host));
      port = cg->ipp_port;
    }

    if (strcmp(http->hostname, host) || port != httpAddrPort(http->hostaddr))
    {
      DEBUG_printf(("1cupsCopyDestInfo: Connection to device (%s).", http->hostname));
      dflags = CUPS_DEST_FLAGS_DEVICE;
    }
    else
    {
      DEBUG_printf(("1cupsCopyDestInfo: Connection to server (%s).", http->hostname));
      dflags = CUPS_DEST_FLAGS_NONE;
    }
  }

 /*
  * Get the printer URI and resource path...
  */

  if ((uri = _cupsGetDestResource(dest, dflags, resource, sizeof(resource))) == NULL)
  {
    DEBUG_puts("1cupsCopyDestInfo: Unable to get resource.");
    return (NULL);
  }

 /*
  * Get the supported attributes...
  */

  delay      = 1;
  prev_delay = 1;
  tries      = 0;
  version    = 20;

  do
  {
   /*
    * Send a Get-Printer-Attributes request...
    */

    request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);

    ippSetVersion(request, version / 10, version % 10);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());
    ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes", (int)(sizeof(requested_attrs) / sizeof(requested_attrs[0])), NULL, requested_attrs);
    response = cupsDoRequest(http, request, resource);
    status   = cupsLastError();

    if (status > IPP_STATUS_OK_IGNORED_OR_SUBSTITUTED)
    {
      DEBUG_printf(("1cupsCopyDestInfo: Get-Printer-Attributes for '%s' returned %s (%s)", dest->name, ippErrorString(status), cupsLastErrorString()));

      ippDelete(response);
      response = NULL;

      if ((status == IPP_STATUS_ERROR_BAD_REQUEST || status == IPP_STATUS_ERROR_VERSION_NOT_SUPPORTED) && version > 11)
      {
        version = 11;
      }
      else if (status == IPP_STATUS_ERROR_BUSY)
      {
        sleep((unsigned)delay);

        delay = _cupsNextDelay(delay, &prev_delay);
      }
      else
        return (NULL);
    }

    tries ++;
  }
  while (!response && tries < 10);

  if (!response)
  {
    DEBUG_puts("1cupsCopyDestInfo: Unable to get printer attributes.");
    return (NULL);
  }

 /*
  * Allocate a cups_dinfo_t structure and return it...
  */

  if ((dinfo = calloc(1, sizeof(cups_dinfo_t))) == NULL)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    ippDelete(response);
    return (NULL);
  }

  DEBUG_printf(("1cupsCopyDestInfo: version=%d, uri=\"%s\", resource=\"%s\".", version, uri, resource));

  dinfo->version  = version;
  dinfo->uri      = uri;
  dinfo->resource = _cupsStrAlloc(resource);
  dinfo->attrs    = response;

  return (dinfo);
}


/*
 * 'cupsFindDestDefault()' - Find the default value(s) for the given option.
 *
 * The returned value is an IPP attribute. Use the @code ippGetBoolean@,
 * @code ippGetCollection@, @code ippGetCount@, @code ippGetDate@,
 * @code ippGetInteger@, @code ippGetOctetString@, @code ippGetRange@,
 * @code ippGetResolution@, @code ippGetString@, and @code ippGetValueTag@
 * functions to inspect the default value(s) as needed.
 *
 * @since CUPS 1.7/macOS 10.9@
 */

ipp_attribute_t	*			/* O - Default attribute or @code NULL@ for none */
cupsFindDestDefault(
    http_t       *http,			/* I - Connection to destination */
    cups_dest_t  *dest,			/* I - Destination */
    cups_dinfo_t *dinfo,		/* I - Destination information */
    const char   *option)		/* I - Option/attribute name */
{
  char	name[IPP_MAX_NAME];		/* Attribute name */


 /*
  * Get the default connection as needed...
  */

  if (!http)
    http = _cupsConnect();

 /*
  * Range check input...
  */

  if (!http || !dest || !dinfo || !option)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (NULL);
  }

 /*
  * Find and return the attribute...
  */

  snprintf(name, sizeof(name), "%s-default", option);
  return (ippFindAttribute(dinfo->attrs, name, IPP_TAG_ZERO));
}


/*
 * 'cupsFindDestReady()' - Find the default value(s) for the given option.
 *
 * The returned value is an IPP attribute. Use the @code ippGetBoolean@,
 * @code ippGetCollection@, @code ippGetCount@, @code ippGetDate@,
 * @code ippGetInteger@, @code ippGetOctetString@, @code ippGetRange@,
 * @code ippGetResolution@, @code ippGetString@, and @code ippGetValueTag@
 * functions to inspect the default value(s) as needed.
 *
 * @since CUPS 1.7/macOS 10.9@
 */

ipp_attribute_t	*			/* O - Default attribute or @code NULL@ for none */
cupsFindDestReady(
    http_t       *http,			/* I - Connection to destination */
    cups_dest_t  *dest,			/* I - Destination */
    cups_dinfo_t *dinfo,		/* I - Destination information */
    const char   *option)		/* I - Option/attribute name */
{
  char	name[IPP_MAX_NAME];		/* Attribute name */


 /*
  * Get the default connection as needed...
  */

  if (!http)
    http = _cupsConnect();

 /*
  * Range check input...
  */

  if (!http || !dest || !dinfo || !option)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (NULL);
  }

 /*
  * Find and return the attribute...
  */

  cups_update_ready(http, dinfo);

  snprintf(name, sizeof(name), "%s-ready", option);
  return (ippFindAttribute(dinfo->ready_attrs, name, IPP_TAG_ZERO));
}


/*
 * 'cupsFindDestSupported()' - Find the default value(s) for the given option.
 *
 * The returned value is an IPP attribute. Use the @code ippGetBoolean@,
 * @code ippGetCollection@, @code ippGetCount@, @code ippGetDate@,
 * @code ippGetInteger@, @code ippGetOctetString@, @code ippGetRange@,
 * @code ippGetResolution@, @code ippGetString@, and @code ippGetValueTag@
 * functions to inspect the default value(s) as needed.
 *
 * @since CUPS 1.7/macOS 10.9@
 */

ipp_attribute_t	*			/* O - Default attribute or @code NULL@ for none */
cupsFindDestSupported(
    http_t       *http,			/* I - Connection to destination */
    cups_dest_t  *dest,			/* I - Destination */
    cups_dinfo_t *dinfo,		/* I - Destination information */
    const char   *option)		/* I - Option/attribute name */
{
  char	name[IPP_MAX_NAME];		/* Attribute name */


 /*
  * Get the default connection as needed...
  */

  if (!http)
    http = _cupsConnect();

 /*
  * Range check input...
  */

  if (!http || !dest || !dinfo || !option)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (NULL);
  }

 /*
  * Find and return the attribute...
  */

  snprintf(name, sizeof(name), "%s-supported", option);
  return (ippFindAttribute(dinfo->attrs, name, IPP_TAG_ZERO));
}


/*
 * 'cupsFreeDestInfo()' - Free destination information obtained using
 *                        @link cupsCopyDestInfo@.
 *
 * @since CUPS 1.6/macOS 10.8@
 */

void
cupsFreeDestInfo(cups_dinfo_t *dinfo)	/* I - Destination information */
{
 /*
  * Range check input...
  */

  if (!dinfo)
    return;

 /*
  * Free memory and return...
  */

  _cupsStrFree(dinfo->resource);

  cupsArrayDelete(dinfo->constraints);
  cupsArrayDelete(dinfo->resolvers);

  cupsArrayDelete(dinfo->localizations);

  cupsArrayDelete(dinfo->media_db);

  cupsArrayDelete(dinfo->cached_db);

  ippDelete(dinfo->ready_attrs);
  cupsArrayDelete(dinfo->ready_db);

  ippDelete(dinfo->attrs);

  free(dinfo);
}


/*
 * 'cupsGetDestMediaByIndex()' - Get a media name, dimension, and margins for a
 *                               specific size.
 *
 * The @code flags@ parameter determines which set of media are indexed.  For
 * example, passing @code CUPS_MEDIA_FLAGS_BORDERLESS@ will get the Nth
 * borderless size supported by the printer.
 *
 * @since CUPS 1.7/macOS 10.9@
 */

int					/* O - 1 on success, 0 on failure */
cupsGetDestMediaByIndex(
    http_t       *http,			/* I - Connection to destination */
    cups_dest_t  *dest,			/* I - Destination */
    cups_dinfo_t *dinfo,		/* I - Destination information */
    int          n,			/* I - Media size number (0-based) */
    unsigned     flags,			/* I - Media flags */
    cups_size_t  *size)			/* O - Media size information */
{
  _cups_media_db_t	*nsize;		/* Size for N */
  pwg_media_t		*pwg;		/* PWG media name for size */


 /*
  * Get the default connection as needed...
  */

  if (!http)
    http = _cupsConnect();

 /*
  * Range check input...
  */

  if (size)
    memset(size, 0, sizeof(cups_size_t));

  if (!http || !dest || !dinfo || n < 0 || !size)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (0);
  }

 /*
  * Load media list as needed...
  */

  if (flags & CUPS_MEDIA_FLAGS_READY)
    cups_update_ready(http, dinfo);

  if (!dinfo->cached_db || dinfo->cached_flags != flags)
    cups_create_cached(http, dinfo, flags);

 /*
  * Copy the size over and return...
  */

  if ((nsize = (_cups_media_db_t *)cupsArrayIndex(dinfo->cached_db, n)) == NULL)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (0);
  }

  if (nsize->key)
    strlcpy(size->media, nsize->key, sizeof(size->media));
  else if (nsize->size_name)
    strlcpy(size->media, nsize->size_name, sizeof(size->media));
  else if ((pwg = pwgMediaForSize(nsize->width, nsize->length)) != NULL)
    strlcpy(size->media, pwg->pwg, sizeof(size->media));
  else
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (0);
  }

  size->width  = nsize->width;
  size->length = nsize->length;
  size->bottom = nsize->bottom;
  size->left   = nsize->left;
  size->right  = nsize->right;
  size->top    = nsize->top;

  return (1);
}


/*
 * 'cupsGetDestMediaByName()' - Get media names, dimensions, and margins.
 *
 * The "media" string is a PWG media name.  "Flags" provides some matching
 * guidance (multiple flags can be combined):
 *
 * CUPS_MEDIA_FLAGS_DEFAULT    = find the closest size supported by the printer,
 * CUPS_MEDIA_FLAGS_BORDERLESS = find a borderless size,
 * CUPS_MEDIA_FLAGS_DUPLEX     = find a size compatible with 2-sided printing,
 * CUPS_MEDIA_FLAGS_EXACT      = find an exact match for the size, and
 * CUPS_MEDIA_FLAGS_READY      = if the printer supports media sensing, find the
 *                               size amongst the "ready" media.
 *
 * The matching result (if any) is returned in the "cups_size_t" structure.
 *
 * Returns 1 when there is a match and 0 if there is not a match.
 *
 * @since CUPS 1.6/macOS 10.8@
 */

int					/* O - 1 on match, 0 on failure */
cupsGetDestMediaByName(
    http_t       *http,			/* I - Connection to destination */
    cups_dest_t  *dest,			/* I - Destination */
    cups_dinfo_t *dinfo,		/* I - Destination information */
    const char   *media,		/* I - Media name */
    unsigned     flags,			/* I - Media matching flags */
    cups_size_t  *size)			/* O - Media size information */
{
  pwg_media_t		*pwg;		/* PWG media info */


 /*
  * Get the default connection as needed...
  */

  if (!http)
    http = _cupsConnect();

 /*
  * Range check input...
  */

  if (size)
    memset(size, 0, sizeof(cups_size_t));

  if (!http || !dest || !dinfo || !media || !size)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (0);
  }

 /*
  * Lookup the media size name...
  */

  if ((pwg = pwgMediaForPWG(media)) == NULL)
    if ((pwg = pwgMediaForLegacy(media)) == NULL)
    {
      DEBUG_printf(("1cupsGetDestMediaByName: Unknown size '%s'.", media));
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unknown media size name."), 1);
      return (0);
    }

 /*
  * Lookup the size...
  */

  return (cups_get_media_db(http, dinfo, pwg, flags, size));
}


/*
 * 'cupsGetDestMediaBySize()' - Get media names, dimensions, and margins.
 *
 * "Width" and "length" are the dimensions in hundredths of millimeters.
 * "Flags" provides some matching guidance (multiple flags can be combined):
 *
 * CUPS_MEDIA_FLAGS_DEFAULT    = find the closest size supported by the printer,
 * CUPS_MEDIA_FLAGS_BORDERLESS = find a borderless size,
 * CUPS_MEDIA_FLAGS_DUPLEX     = find a size compatible with 2-sided printing,
 * CUPS_MEDIA_FLAGS_EXACT      = find an exact match for the size, and
 * CUPS_MEDIA_FLAGS_READY      = if the printer supports media sensing, find the
 *                               size amongst the "ready" media.
 *
 * The matching result (if any) is returned in the "cups_size_t" structure.
 *
 * Returns 1 when there is a match and 0 if there is not a match.
 *
 * @since CUPS 1.6/macOS 10.8@
 */

int					/* O - 1 on match, 0 on failure */
cupsGetDestMediaBySize(
    http_t       *http,			/* I - Connection to destination */
    cups_dest_t  *dest,			/* I - Destination */
    cups_dinfo_t *dinfo,		/* I - Destination information */
    int         width,			/* I - Media width in hundredths of
					 *     of millimeters */
    int         length,			/* I - Media length in hundredths of
					 *     of millimeters */
    unsigned     flags,			/* I - Media matching flags */
    cups_size_t  *size)			/* O - Media size information */
{
  pwg_media_t		*pwg;		/* PWG media info */


 /*
  * Get the default connection as needed...
  */

  if (!http)
    http = _cupsConnect();

 /*
  * Range check input...
  */

  if (size)
    memset(size, 0, sizeof(cups_size_t));

  if (!http || !dest || !dinfo || width <= 0 || length <= 0 || !size)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (0);
  }

 /*
  * Lookup the media size name...
  */

  if ((pwg = pwgMediaForSize(width, length)) == NULL)
  {
    DEBUG_printf(("1cupsGetDestMediaBySize: Invalid size %dx%d.", width,
                  length));
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Invalid media size."), 1);
    return (0);
  }

 /*
  * Lookup the size...
  */

  return (cups_get_media_db(http, dinfo, pwg, flags, size));
}


/*
 * 'cupsGetDestMediaCount()' - Get the number of sizes supported by a
 *                             destination.
 *
 * The @code flags@ parameter determines the set of media sizes that are
 * counted.  For example, passing @code CUPS_MEDIA_FLAGS_BORDERLESS@ will return
 * the number of borderless sizes.
 *
 * @since CUPS 1.7/macOS 10.9@
 */

int					/* O - Number of sizes */
cupsGetDestMediaCount(
    http_t       *http,			/* I - Connection to destination */
    cups_dest_t  *dest,			/* I - Destination */
    cups_dinfo_t *dinfo,		/* I - Destination information */
    unsigned     flags)			/* I - Media flags */
{
 /*
  * Get the default connection as needed...
  */

  if (!http)
    http = _cupsConnect();

 /*
  * Range check input...
  */

  if (!http || !dest || !dinfo)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (0);
  }

 /*
  * Load media list as needed...
  */

  if (flags & CUPS_MEDIA_FLAGS_READY)
    cups_update_ready(http, dinfo);

  if (!dinfo->cached_db || dinfo->cached_flags != flags)
    cups_create_cached(http, dinfo, flags);

  return (cupsArrayCount(dinfo->cached_db));
}


/*
 * 'cupsGetDestMediaDefault()' - Get the default size for a destination.
 *
 * The @code flags@ parameter determines which default size is returned.  For
 * example, passing @code CUPS_MEDIA_FLAGS_BORDERLESS@ will return the default
 * borderless size, typically US Letter or A4, but sometimes 4x6 photo media.
 *
 * @since CUPS 1.7/macOS 10.9@
 */

int					/* O - 1 on success, 0 on failure */
cupsGetDestMediaDefault(
    http_t       *http,			/* I - Connection to destination */
    cups_dest_t  *dest,			/* I - Destination */
    cups_dinfo_t *dinfo,		/* I - Destination information */
    unsigned     flags,			/* I - Media flags */
    cups_size_t  *size)			/* O - Media size information */
{
  const char	*media;			/* Default media size */


 /*
  * Get the default connection as needed...
  */

  if (!http)
    http = _cupsConnect();

 /*
  * Range check input...
  */

  if (size)
    memset(size, 0, sizeof(cups_size_t));

  if (!http || !dest || !dinfo || !size)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (0);
  }

 /*
  * Get the default media size, if any...
  */

  if ((media = cupsGetOption("media", dest->num_options, dest->options)) == NULL)
    media = "na_letter_8.5x11in";

  if (cupsGetDestMediaByName(http, dest, dinfo, media, flags, size))
    return (1);

  if (strcmp(media, "na_letter_8.5x11in") && cupsGetDestMediaByName(http, dest, dinfo, "iso_a4_210x297mm", flags, size))
    return (1);

  if (strcmp(media, "iso_a4_210x297mm") && cupsGetDestMediaByName(http, dest, dinfo, "na_letter_8.5x11in", flags, size))
    return (1);

  if ((flags & CUPS_MEDIA_FLAGS_BORDERLESS) && cupsGetDestMediaByName(http, dest, dinfo, "na_index_4x6in", flags, size))
    return (1);

 /*
  * Fall back to the first matching media size...
  */

  return (cupsGetDestMediaByIndex(http, dest, dinfo, 0, flags, size));
}


/*
 * 'cups_add_dconstres()' - Add a constraint or resolver to an array.
 */

static void
cups_add_dconstres(
    cups_array_t *a,			/* I - Array */
    ipp_t        *collection)		/* I - Collection value */
{
  ipp_attribute_t	*attr;		/* Attribute */
  _cups_dconstres_t	*temp;		/* Current constraint/resolver */


  if ((attr = ippFindAttribute(collection, "resolver-name",
                               IPP_TAG_NAME)) == NULL)
    return;

  if ((temp = calloc(1, sizeof(_cups_dconstres_t))) == NULL)
    return;

  temp->name       = attr->values[0].string.text;
  temp->collection = collection;

  cupsArrayAdd(a, temp);
}


/*
 * 'cups_collection_contains()' - Check whether test collection is contained in the matching collection.
 */

static int				/* O - 1 on a match, 0 on a non-match */
cups_collection_contains(ipp_t *test,	/* I - Collection to test */
                         ipp_t *match)	/* I - Matching values */
{
  int			i, j,		/* Looping vars */
			mcount,		/* Number of match values */
			tcount;		/* Number of test values */
  ipp_attribute_t	*tattr,		/* Testing attribute */
			*mattr;		/* Matching attribute */
  const char		*tval;		/* Testing string value */


  for (mattr = ippFirstAttribute(match); mattr; mattr = ippNextAttribute(match))
  {
    if ((tattr = ippFindAttribute(test, ippGetName(mattr), IPP_TAG_ZERO)) == NULL)
      return (0);

    tcount = ippGetCount(tattr);

    switch (ippGetValueTag(mattr))
    {
      case IPP_TAG_INTEGER :
      case IPP_TAG_ENUM :
          if (ippGetValueTag(tattr) != ippGetValueTag(mattr))
            return (0);

          for (i = 0; i < tcount; i ++)
          {
            if (!ippContainsInteger(mattr, ippGetInteger(tattr, i)))
              return (0);
          }
          break;

      case IPP_TAG_RANGE :
          if (ippGetValueTag(tattr) != IPP_TAG_INTEGER)
            return (0);

          for (i = 0; i < tcount; i ++)
          {
            if (!ippContainsInteger(mattr, ippGetInteger(tattr, i)))
              return (0);
          }
          break;

      case IPP_TAG_BOOLEAN :
          if (ippGetValueTag(tattr) != IPP_TAG_BOOLEAN || ippGetBoolean(tattr, 0) != ippGetBoolean(mattr, 0))
            return (0);
          break;

      case IPP_TAG_TEXTLANG :
      case IPP_TAG_NAMELANG :
      case IPP_TAG_TEXT :
      case IPP_TAG_NAME :
      case IPP_TAG_KEYWORD :
      case IPP_TAG_URI :
      case IPP_TAG_URISCHEME :
      case IPP_TAG_CHARSET :
      case IPP_TAG_LANGUAGE :
      case IPP_TAG_MIMETYPE :
          for (i = 0; i < tcount; i ++)
          {
            if ((tval = ippGetString(tattr, i, NULL)) == NULL || !ippContainsString(mattr, tval))
              return (0);
          }
          break;

      case IPP_TAG_BEGIN_COLLECTION :
          for (i = 0; i < tcount; i ++)
          {
            ipp_t *tcol = ippGetCollection(tattr, i);
					/* Testing collection */

            for (j = 0, mcount = ippGetCount(mattr); j < mcount; j ++)
              if (!cups_collection_contains(tcol, ippGetCollection(mattr, j)))
                return (0);
          }
          break;

      default :
          return (0);
    }
  }

  return (1);
}


/*
 * 'cups_collection_string()' - Convert an IPP collection to an option string.
 */

static size_t				/* O - Number of bytes needed */
cups_collection_string(
    ipp_attribute_t *attr,		/* I - Collection attribute */
    char            *buffer,		/* I - String buffer */
    size_t          bufsize)		/* I - Size of buffer */
{
  int			i, j,		/* Looping vars */
			count,		/* Number of collection values */
			mcount;		/* Number of member values */
  ipp_t			*col;		/* Collection */
  ipp_attribute_t	*first,		/* First member attribute */
			*member;	/* Member attribute */
  char			*bufptr,	/* Pointer into buffer */
			*bufend,	/* End of buffer */
			temp[100];	/* Temporary string */
  const char		*mptr;		/* Pointer into member value */
  int			mlen;		/* Length of octetString */


  bufptr = buffer;
  bufend = buffer + bufsize - 1;

  for (i = 0, count = ippGetCount(attr); i < count; i ++)
  {
    col = ippGetCollection(attr, i);

    if (i)
    {
      if (bufptr < bufend)
        *bufptr++ = ',';
      else
        bufptr ++;
    }

    if (bufptr < bufend)
      *bufptr++ = '{';
    else
      bufptr ++;

    for (member = first = ippFirstAttribute(col); member; member = ippNextAttribute(col))
    {
      const char *mname = ippGetName(member);

      if (member != first)
      {
	if (bufptr < bufend)
	  *bufptr++ = ' ';
	else
	  bufptr ++;
      }

      if (ippGetValueTag(member) == IPP_TAG_BOOLEAN)
      {
        if (!ippGetBoolean(member, 0))
        {
	  if (bufptr < bufend)
	    strlcpy(bufptr, "no", (size_t)(bufend - bufptr + 1));
	  bufptr += 2;
        }

	if (bufptr < bufend)
	  strlcpy(bufptr, mname, (size_t)(bufend - bufptr + 1));
	bufptr += strlen(mname);
        continue;
      }

      if (bufptr < bufend)
        strlcpy(bufptr, mname, (size_t)(bufend - bufptr + 1));
      bufptr += strlen(mname);

      if (bufptr < bufend)
        *bufptr++ = '=';
      else
        bufptr ++;

      if (ippGetValueTag(member) == IPP_TAG_BEGIN_COLLECTION)
      {
       /*
	* Convert sub-collection...
	*/

	bufptr += cups_collection_string(member, bufptr, bufptr < bufend ? (size_t)(bufend - bufptr + 1) : 0);
      }
      else
      {
       /*
        * Convert simple type...
        */

	for (j = 0, mcount = ippGetCount(member); j < mcount; j ++)
	{
	  if (j)
	  {
	    if (bufptr < bufend)
	      *bufptr++ = ',';
	    else
	      bufptr ++;
	  }

          switch (ippGetValueTag(member))
          {
            case IPP_TAG_INTEGER :
            case IPP_TAG_ENUM :
                bufptr += snprintf(bufptr, bufptr < bufend ? (size_t)(bufend - bufptr + 1) : 0, "%d", ippGetInteger(member, j));
                break;

	    case IPP_TAG_STRING :
		if (bufptr < bufend)
		  *bufptr++ = '\"';
		else
		  bufptr ++;

	        for (mptr = (const char *)ippGetOctetString(member, j, &mlen); mlen > 0; mlen --, mptr ++)
	        {
	          if (*mptr == '\"' || *mptr == '\\')
	          {
		    if (bufptr < bufend)
		      *bufptr++ = '\\';
		    else
		      bufptr ++;
		  }

		  if (bufptr < bufend)
		    *bufptr++ = *mptr;
		  else
		    bufptr ++;
                }

		if (bufptr < bufend)
		  *bufptr++ = '\"';
		else
		  bufptr ++;
	        break;

            case IPP_TAG_DATE :
		{
		  unsigned year;	/* Year */
		  const ipp_uchar_t *date = ippGetDate(member, j);
					/* Date value */

		  year = (date[0] << 8) | date[1];

		  if (date[9] == 0 && date[10] == 0)
		    snprintf(temp, sizeof(temp), "%04u-%02u-%02uT%02u:%02u:%02uZ", year, date[2], date[3], date[4], date[5], date[6]);
		  else
		    snprintf(temp, sizeof(temp), "%04u-%02u-%02uT%02u:%02u:%02u%c%02u%02u", year, date[2], date[3], date[4], date[5], date[6], date[8], date[9], date[10]);

		  if (bufptr < bufend)
		    strlcpy(bufptr, temp, (size_t)(bufend - bufptr + 1));

		  bufptr += strlen(temp);
		}
                break;

            case IPP_TAG_RESOLUTION :
                {
                  int		xres,	/* Horizontal resolution */
				yres;	/* Vertical resolution */
                  ipp_res_t	units;	/* Resolution units */

                  xres = ippGetResolution(member, j, &yres, &units);

                  if (xres == yres)
                    snprintf(temp, sizeof(temp), "%d%s", xres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
		  else
                    snprintf(temp, sizeof(temp), "%dx%d%s", xres, yres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");

		  if (bufptr < bufend)
		    strlcpy(bufptr, temp, (size_t)(bufend - bufptr + 1));

		  bufptr += strlen(temp);
                }
                break;

            case IPP_TAG_RANGE :
                {
                  int		lower,	/* Lower bound */
				upper;	/* Upper bound */

                  lower = ippGetRange(member, j, &upper);

		  snprintf(temp, sizeof(temp), "%d-%d", lower, upper);

		  if (bufptr < bufend)
		    strlcpy(bufptr, temp, (size_t)(bufend - bufptr + 1));

		  bufptr += strlen(temp);
                }
                break;

            case IPP_TAG_TEXTLANG :
            case IPP_TAG_NAMELANG :
            case IPP_TAG_TEXT :
            case IPP_TAG_NAME :
            case IPP_TAG_KEYWORD :
            case IPP_TAG_URI :
            case IPP_TAG_URISCHEME :
            case IPP_TAG_CHARSET :
            case IPP_TAG_LANGUAGE :
            case IPP_TAG_MIMETYPE :
		if (bufptr < bufend)
		  *bufptr++ = '\"';
		else
		  bufptr ++;

	        for (mptr = ippGetString(member, j, NULL); *mptr; mptr ++)
	        {
	          if (*mptr == '\"' || *mptr == '\\')
	          {
		    if (bufptr < bufend)
		      *bufptr++ = '\\';
		    else
		      bufptr ++;
		  }

		  if (bufptr < bufend)
		    *bufptr++ = *mptr;
		  else
		    bufptr ++;
                }

		if (bufptr < bufend)
		  *bufptr++ = '\"';
		else
		  bufptr ++;
                break;

            default :
                break;
          }
	}
      }
    }

    if (bufptr < bufend)
      *bufptr++ = '}';
    else
      bufptr ++;
  }

  *bufptr = '\0';
  return ((size_t)(bufptr - buffer + 1));
}


/*
 * 'cups_compare_dconstres()' - Compare to resolver entries.
 */

static int				/* O - Result of comparison */
cups_compare_dconstres(
    _cups_dconstres_t *a,		/* I - First resolver */
    _cups_dconstres_t *b)		/* I - Second resolver */
{
  return (strcmp(a->name, b->name));
}


/*
 * 'cups_compare_media_db()' - Compare two media entries.
 */

static int				/* O - Result of comparison */
cups_compare_media_db(
    _cups_media_db_t *a,		/* I - First media entries */
    _cups_media_db_t *b)		/* I - Second media entries */
{
  int	result;				/* Result of comparison */


  if ((result = a->width - b->width) == 0)
    result = a->length - b->length;

  return (result);
}


/*
 * 'cups_copy_media_db()' - Copy a media entry.
 */

static _cups_media_db_t *		/* O - New media entry */
cups_copy_media_db(
    _cups_media_db_t *mdb)		/* I - Media entry to copy */
{
  _cups_media_db_t *temp;		/* New media entry */


  if ((temp = calloc(1, sizeof(_cups_media_db_t))) == NULL)
    return (NULL);

  if (mdb->color)
    temp->color = _cupsStrAlloc(mdb->color);
  if (mdb->key)
    temp->key = _cupsStrAlloc(mdb->key);
  if (mdb->info)
    temp->info = _cupsStrAlloc(mdb->info);
  if (mdb->size_name)
    temp->size_name = _cupsStrAlloc(mdb->size_name);
  if (mdb->source)
    temp->source = _cupsStrAlloc(mdb->source);
  if (mdb->type)
    temp->type = _cupsStrAlloc(mdb->type);

  temp->width  = mdb->width;
  temp->length = mdb->length;
  temp->bottom = mdb->bottom;
  temp->left   = mdb->left;
  temp->right  = mdb->right;
  temp->top    = mdb->top;

  return (temp);
}


/*
 * 'cups_create_cached()' - Create the media selection cache.
 */

static void
cups_create_cached(http_t       *http,	/* I - Connection to destination */
                   cups_dinfo_t *dinfo,	/* I - Destination information */
                   unsigned     flags)	/* I - Media selection flags */
{
  cups_array_t		*db;		/* Media database array to use */
  _cups_media_db_t	*mdb,		/* Media database entry */
			*first;		/* First entry this size */


  DEBUG_printf(("3cups_create_cached(http=%p, dinfo=%p, flags=%u)", (void *)http, (void *)dinfo, flags));

  if (dinfo->cached_db)
    cupsArrayDelete(dinfo->cached_db);

  dinfo->cached_db    = cupsArrayNew(NULL, NULL);
  dinfo->cached_flags = flags;

  if (flags & CUPS_MEDIA_FLAGS_READY)
  {
    DEBUG_puts("4cups_create_cached: ready media");

    cups_update_ready(http, dinfo);
    db = dinfo->ready_db;
  }
  else
  {
    DEBUG_puts("4cups_create_cached: supported media");

    if (!dinfo->media_db)
      cups_create_media_db(dinfo, CUPS_MEDIA_FLAGS_DEFAULT);

    db = dinfo->media_db;
  }

  for (mdb = (_cups_media_db_t *)cupsArrayFirst(db), first = mdb;
       mdb;
       mdb = (_cups_media_db_t *)cupsArrayNext(db))
  {
    DEBUG_printf(("4cups_create_cached: %p key=\"%s\", type=\"%s\", %dx%d, B%d L%d R%d T%d", (void *)mdb, mdb->key, mdb->type, mdb->width, mdb->length, mdb->bottom, mdb->left, mdb->right, mdb->top));

    if (flags & CUPS_MEDIA_FLAGS_BORDERLESS)
    {
      if (!mdb->left && !mdb->right && !mdb->top && !mdb->bottom)
      {
        DEBUG_printf(("4cups_create_cached: add %p", (void *)mdb));
        cupsArrayAdd(dinfo->cached_db, mdb);
      }
    }
    else if (flags & CUPS_MEDIA_FLAGS_DUPLEX)
    {
      if (first->width != mdb->width || first->length != mdb->length)
      {
	DEBUG_printf(("4cups_create_cached: add %p", (void *)first));
        cupsArrayAdd(dinfo->cached_db, first);
        first = mdb;
      }
      else if (mdb->left >= first->left && mdb->right >= first->right && mdb->top >= first->top && mdb->bottom >= first->bottom &&
	       (mdb->left != first->left || mdb->right != first->right || mdb->top != first->top || mdb->bottom != first->bottom))
        first = mdb;
    }
    else
    {
      DEBUG_printf(("4cups_create_cached: add %p", (void *)mdb));
      cupsArrayAdd(dinfo->cached_db, mdb);
    }
  }

  if (flags & CUPS_MEDIA_FLAGS_DUPLEX)
  {
    DEBUG_printf(("4cups_create_cached: add %p", (void *)first));
    cupsArrayAdd(dinfo->cached_db, first);
  }
}


/*
 * 'cups_create_constraints()' - Create the constraints and resolvers arrays.
 */

static void
cups_create_constraints(
    cups_dinfo_t *dinfo)		/* I - Destination information */
{
  int			i;		/* Looping var */
  ipp_attribute_t	*attr;		/* Attribute */
  _ipp_value_t		*val;		/* Current value */


  dinfo->constraints = cupsArrayNew3(NULL, NULL, NULL, 0, NULL,
                                     (cups_afree_func_t)free);
  dinfo->resolvers   = cupsArrayNew3((cups_array_func_t)cups_compare_dconstres,
				     NULL, NULL, 0, NULL,
                                     (cups_afree_func_t)free);

  if ((attr = ippFindAttribute(dinfo->attrs, "job-constraints-supported",
			       IPP_TAG_BEGIN_COLLECTION)) != NULL)
  {
    for (i = attr->num_values, val = attr->values; i > 0; i --, val ++)
      cups_add_dconstres(dinfo->constraints, val->collection);
  }

  if ((attr = ippFindAttribute(dinfo->attrs, "job-resolvers-supported",
			       IPP_TAG_BEGIN_COLLECTION)) != NULL)
  {
    for (i = attr->num_values, val = attr->values; i > 0; i --, val ++)
      cups_add_dconstres(dinfo->resolvers, val->collection);
  }
}


/*
 * 'cups_create_defaults()' - Create the -default option array.
 */

static void
cups_create_defaults(
    cups_dinfo_t *dinfo)		/* I - Destination information */
{
  ipp_attribute_t	*attr;		/* Current attribute */
  char			name[IPP_MAX_NAME + 1],
					/* Current name */
			*nameptr,	/* Pointer into current name */
			value[2048];	/* Current value */


 /*
  * Iterate through the printer attributes looking for xxx-default and adding
  * xxx=value to the defaults option array.
  */

  for (attr = ippFirstAttribute(dinfo->attrs); attr; attr = ippNextAttribute(dinfo->attrs))
  {
    if (!ippGetName(attr) || ippGetGroupTag(attr) != IPP_TAG_PRINTER)
      continue;

    strlcpy(name, ippGetName(attr), sizeof(name));
    if ((nameptr = name + strlen(name) - 8) <= name || strcmp(nameptr, "-default"))
      continue;

    *nameptr = '\0';

    if (ippGetValueTag(attr) == IPP_TAG_BEGIN_COLLECTION)
    {
      if (cups_collection_string(attr, value, sizeof(value)) >= sizeof(value))
        continue;
    }
    else if (ippAttributeString(attr, value, sizeof(value)) >= sizeof(value))
      continue;

    dinfo->num_defaults = cupsAddOption(name, value, dinfo->num_defaults, &dinfo->defaults);
  }
}


/*
 * 'cups_create_media_db()' - Create the media database.
 */

static void
cups_create_media_db(
    cups_dinfo_t *dinfo,		/* I - Destination information */
    unsigned     flags)			/* I - Media flags */
{
  int			i;		/* Looping var */
  _ipp_value_t		*val;		/* Current value */
  ipp_attribute_t	*media_col_db,	/* media-col-database */
			*media_attr,	/* media-xxx */
			*x_dimension,	/* x-dimension */
			*y_dimension;	/* y-dimension */
  pwg_media_t		*pwg;		/* PWG media info */
  cups_array_t		*db;		/* New media database array */
  _cups_media_db_t	mdb;		/* Media entry */
  char			media_key[256];	/* Synthesized media-key value */


  db = cupsArrayNew3((cups_array_func_t)cups_compare_media_db,
		     NULL, NULL, 0,
		     (cups_acopy_func_t)cups_copy_media_db,
		     (cups_afree_func_t)cups_free_media_db);

  if (flags == CUPS_MEDIA_FLAGS_READY)
  {
    dinfo->ready_db = db;

    media_col_db = ippFindAttribute(dinfo->ready_attrs, "media-col-ready",
				    IPP_TAG_BEGIN_COLLECTION);
    media_attr   = ippFindAttribute(dinfo->ready_attrs, "media-ready",
				    IPP_TAG_ZERO);
  }
  else
  {
    dinfo->media_db        = db;
    dinfo->min_size.width  = INT_MAX;
    dinfo->min_size.length = INT_MAX;
    dinfo->max_size.width  = 0;
    dinfo->max_size.length = 0;

    media_col_db = ippFindAttribute(dinfo->attrs, "media-col-database",
				    IPP_TAG_BEGIN_COLLECTION);
    media_attr   = ippFindAttribute(dinfo->attrs, "media-supported",
				    IPP_TAG_ZERO);
  }

  if (media_col_db)
  {
    _ipp_value_t	*custom = NULL;	/* Custom size range value */

    for (i = media_col_db->num_values, val = media_col_db->values;
         i > 0;
         i --, val ++)
    {
      memset(&mdb, 0, sizeof(mdb));

      if ((media_attr = ippFindAttribute(val->collection, "media-size",
                                         IPP_TAG_BEGIN_COLLECTION)) != NULL)
      {
        ipp_t	*media_size = media_attr->values[0].collection;
					/* media-size collection value */

        if ((x_dimension = ippFindAttribute(media_size, "x-dimension",
                                            IPP_TAG_INTEGER)) != NULL &&
	    (y_dimension = ippFindAttribute(media_size, "y-dimension",
					    IPP_TAG_INTEGER)) != NULL)
	{
	 /*
	  * Fixed size...
	  */

	  mdb.width  = x_dimension->values[0].integer;
	  mdb.length = y_dimension->values[0].integer;
	}
	else if ((x_dimension = ippFindAttribute(media_size, "x-dimension",
						 IPP_TAG_INTEGER)) != NULL &&
		 (y_dimension = ippFindAttribute(media_size, "y-dimension",
						 IPP_TAG_RANGE)) != NULL)
	{
	 /*
	  * Roll limits...
	  */

	  mdb.width  = x_dimension->values[0].integer;
	  mdb.length = y_dimension->values[0].range.upper;
	}
        else if (flags != CUPS_MEDIA_FLAGS_READY &&
                 (x_dimension = ippFindAttribute(media_size, "x-dimension",
					         IPP_TAG_RANGE)) != NULL &&
		 (y_dimension = ippFindAttribute(media_size, "y-dimension",
						 IPP_TAG_RANGE)) != NULL)
	{
	 /*
	  * Custom size range; save this as the custom size value with default
	  * margins, then continue; we'll capture the real margins below...
	  */

	  custom = val;

	  dinfo->min_size.width  = x_dimension->values[0].range.lower;
	  dinfo->min_size.length = y_dimension->values[0].range.lower;
	  dinfo->min_size.left   =
	  dinfo->min_size.right  = 635; /* Default 1/4" side margins */
	  dinfo->min_size.top    =
	  dinfo->min_size.bottom = 1270; /* Default 1/2" top/bottom margins */

	  dinfo->max_size.width  = x_dimension->values[0].range.upper;
	  dinfo->max_size.length = y_dimension->values[0].range.upper;
	  dinfo->max_size.left   =
	  dinfo->max_size.right  = 635; /* Default 1/4" side margins */
	  dinfo->max_size.top    =
	  dinfo->max_size.bottom = 1270; /* Default 1/2" top/bottom margins */
	  continue;
	}
      }

      if ((media_attr = ippFindAttribute(val->collection, "media-color", IPP_TAG_ZERO)) != NULL && (media_attr->value_tag == IPP_TAG_NAME || media_attr->value_tag == IPP_TAG_NAMELANG || media_attr->value_tag == IPP_TAG_KEYWORD))
        mdb.color = media_attr->values[0].string.text;

      if ((media_attr = ippFindAttribute(val->collection, "media-info", IPP_TAG_TEXT)) != NULL)
        mdb.info = media_attr->values[0].string.text;

      if ((media_attr = ippFindAttribute(val->collection, "media-key", IPP_TAG_ZERO)) != NULL && (media_attr->value_tag == IPP_TAG_NAME || media_attr->value_tag == IPP_TAG_NAMELANG || media_attr->value_tag == IPP_TAG_KEYWORD))
        mdb.key = media_attr->values[0].string.text;

      if ((media_attr = ippFindAttribute(val->collection, "media-size-name", IPP_TAG_ZERO)) != NULL && (media_attr->value_tag == IPP_TAG_NAME || media_attr->value_tag == IPP_TAG_NAMELANG || media_attr->value_tag == IPP_TAG_KEYWORD))
        mdb.size_name = media_attr->values[0].string.text;

      if ((media_attr = ippFindAttribute(val->collection, "media-source", IPP_TAG_ZERO)) != NULL && (media_attr->value_tag == IPP_TAG_NAME || media_attr->value_tag == IPP_TAG_NAMELANG || media_attr->value_tag == IPP_TAG_KEYWORD))
        mdb.source = media_attr->values[0].string.text;

      if ((media_attr = ippFindAttribute(val->collection, "media-type", IPP_TAG_ZERO)) != NULL && (media_attr->value_tag == IPP_TAG_NAME || media_attr->value_tag == IPP_TAG_NAMELANG || media_attr->value_tag == IPP_TAG_KEYWORD))
        mdb.type = media_attr->values[0].string.text;

      if ((media_attr = ippFindAttribute(val->collection, "media-bottom-margin", IPP_TAG_INTEGER)) != NULL)
        mdb.bottom = media_attr->values[0].integer;

      if ((media_attr = ippFindAttribute(val->collection, "media-left-margin", IPP_TAG_INTEGER)) != NULL)
        mdb.left = media_attr->values[0].integer;

      if ((media_attr = ippFindAttribute(val->collection, "media-right-margin", IPP_TAG_INTEGER)) != NULL)
        mdb.right = media_attr->values[0].integer;

      if ((media_attr = ippFindAttribute(val->collection, "media-top-margin", IPP_TAG_INTEGER)) != NULL)
        mdb.top = media_attr->values[0].integer;

      if (!mdb.key)
      {
        if (!mdb.size_name && (pwg = pwgMediaForSize(mdb.width, mdb.length)) != NULL)
	  mdb.size_name = (char *)pwg->pwg;

        if (!mdb.size_name)
        {
         /*
          * Use a CUPS-specific identifier if we don't have a size name...
          */

	  if (flags & CUPS_MEDIA_FLAGS_READY)
	    snprintf(media_key, sizeof(media_key), "cups-media-ready-%d", i + 1);
	  else
	    snprintf(media_key, sizeof(media_key), "cups-media-%d", i + 1);
        }
        else if (mdb.source)
        {
         /*
          * Generate key using size name, source, and type (if set)...
          */

          if (mdb.type)
            snprintf(media_key, sizeof(media_key), "%s_%s_%s", mdb.size_name, mdb.source, mdb.type);
	  else
            snprintf(media_key, sizeof(media_key), "%s_%s", mdb.size_name, mdb.source);
        }
        else if (mdb.type)
        {
         /*
          * Generate key using size name and type...
          */

	  snprintf(media_key, sizeof(media_key), "%s_%s", mdb.size_name, mdb.type);
        }
        else
        {
         /*
          * Key is just the size name...
          */

          strlcpy(media_key, mdb.size_name, sizeof(media_key));
        }

       /*
        * Append "_borderless" for borderless media...
        */

        if (!mdb.bottom && !mdb.left && !mdb.right && !mdb.top)
          strlcat(media_key, "_borderless", sizeof(media_key));

        mdb.key = media_key;
      }

      DEBUG_printf(("1cups_create_media_db: Adding media: key=\"%s\", width=%d, length=%d, source=\"%s\", type=\"%s\".", mdb.key, mdb.width, mdb.length, mdb.source, mdb.type));

      cupsArrayAdd(db, &mdb);
    }

    if (custom)
    {
      if ((media_attr = ippFindAttribute(custom->collection,
                                         "media-bottom-margin",
                                         IPP_TAG_INTEGER)) != NULL)
      {
        dinfo->min_size.top =
        dinfo->max_size.top = media_attr->values[0].integer;
      }

      if ((media_attr = ippFindAttribute(custom->collection,
                                         "media-left-margin",
                                         IPP_TAG_INTEGER)) != NULL)
      {
        dinfo->min_size.left =
        dinfo->max_size.left = media_attr->values[0].integer;
      }

      if ((media_attr = ippFindAttribute(custom->collection,
                                         "media-right-margin",
                                         IPP_TAG_INTEGER)) != NULL)
      {
        dinfo->min_size.right =
        dinfo->max_size.right = media_attr->values[0].integer;
      }

      if ((media_attr = ippFindAttribute(custom->collection,
                                         "media-top-margin",
                                         IPP_TAG_INTEGER)) != NULL)
      {
        dinfo->min_size.top =
        dinfo->max_size.top = media_attr->values[0].integer;
      }
    }
  }
  else if (media_attr &&
           (media_attr->value_tag == IPP_TAG_NAME ||
            media_attr->value_tag == IPP_TAG_NAMELANG ||
            media_attr->value_tag == IPP_TAG_KEYWORD))
  {
    memset(&mdb, 0, sizeof(mdb));

    mdb.left   =
    mdb.right  = 635; /* Default 1/4" side margins */
    mdb.top    =
    mdb.bottom = 1270; /* Default 1/2" top/bottom margins */

    for (i = media_attr->num_values, val = media_attr->values;
         i > 0;
         i --, val ++)
    {
      if ((pwg = pwgMediaForPWG(val->string.text)) == NULL)
        if ((pwg = pwgMediaForLegacy(val->string.text)) == NULL)
	{
	  DEBUG_printf(("3cups_create_media_db: Ignoring unknown size '%s'.",
			val->string.text));
	  continue;
	}

      mdb.width  = pwg->width;
      mdb.length = pwg->length;

      if (flags != CUPS_MEDIA_FLAGS_READY &&
          !strncmp(val->string.text, "custom_min_", 11))
      {
        mdb.size_name   = NULL;
        dinfo->min_size = mdb;
      }
      else if (flags != CUPS_MEDIA_FLAGS_READY &&
	       !strncmp(val->string.text, "custom_max_", 11))
      {
        mdb.size_name   = NULL;
        dinfo->max_size = mdb;
      }
      else
      {
        mdb.size_name = val->string.text;

        cupsArrayAdd(db, &mdb);
      }
    }
  }
}


/*
 * 'cups_free_media_cb()' - Free a media entry.
 */

static void
cups_free_media_db(
    _cups_media_db_t *mdb)		/* I - Media entry to free */
{
  if (mdb->color)
    _cupsStrFree(mdb->color);
  if (mdb->key)
    _cupsStrFree(mdb->key);
  if (mdb->info)
    _cupsStrFree(mdb->info);
  if (mdb->size_name)
    _cupsStrFree(mdb->size_name);
  if (mdb->source)
    _cupsStrFree(mdb->source);
  if (mdb->type)
    _cupsStrFree(mdb->type);

  free(mdb);
}


/*
 * 'cups_get_media_db()' - Lookup the media entry for a given size.
 */

static int				/* O - 1 on match, 0 on failure */
cups_get_media_db(http_t       *http,	/* I - Connection to destination */
                  cups_dinfo_t *dinfo,	/* I - Destination information */
                  pwg_media_t  *pwg,	/* I - PWG media info */
                  unsigned     flags,	/* I - Media matching flags */
                  cups_size_t  *size)	/* O - Media size/margin/name info */
{
  cups_array_t		*db;		/* Which media database to query */
  _cups_media_db_t	*mdb,		/* Current media database entry */
			*best = NULL,	/* Best matching entry */
			key;		/* Search key */


 /*
  * Create the media database as needed...
  */

  if (flags & CUPS_MEDIA_FLAGS_READY)
  {
    cups_update_ready(http, dinfo);
    db = dinfo->ready_db;
  }
  else
  {
    if (!dinfo->media_db)
      cups_create_media_db(dinfo, CUPS_MEDIA_FLAGS_DEFAULT);

    db = dinfo->media_db;
  }

 /*
  * Find a match...
  */

  memset(&key, 0, sizeof(key));
  key.width  = pwg->width;
  key.length = pwg->length;

  if ((mdb = cupsArrayFind(db, &key)) != NULL)
  {
   /*
    * Found an exact match, let's figure out the best margins for the flags
    * supplied...
    */

    best = mdb;

    if (flags & CUPS_MEDIA_FLAGS_BORDERLESS)
    {
     /*
      * Look for the smallest margins...
      */

      if (best->left != 0 || best->right != 0 || best->top != 0 || best->bottom != 0)
      {
	for (mdb = (_cups_media_db_t *)cupsArrayNext(db);
	     mdb && !cups_compare_media_db(mdb, &key);
	     mdb = (_cups_media_db_t *)cupsArrayNext(db))
	{
	  if (mdb->left <= best->left && mdb->right <= best->right &&
	      mdb->top <= best->top && mdb->bottom <= best->bottom)
	  {
	    best = mdb;
	    if (mdb->left == 0 && mdb->right == 0 && mdb->bottom == 0 &&
		mdb->top == 0)
	      break;
	  }
	}
      }

     /*
      * If we need an exact match, return no-match if the size is not
      * borderless.
      */

      if ((flags & CUPS_MEDIA_FLAGS_EXACT) &&
          (best->left || best->right || best->top || best->bottom))
        return (0);
    }
    else if (flags & CUPS_MEDIA_FLAGS_DUPLEX)
    {
     /*
      * Look for the largest margins...
      */

      for (mdb = (_cups_media_db_t *)cupsArrayNext(db);
	   mdb && !cups_compare_media_db(mdb, &key);
	   mdb = (_cups_media_db_t *)cupsArrayNext(db))
      {
	if (mdb->left >= best->left && mdb->right >= best->right &&
	    mdb->top >= best->top && mdb->bottom >= best->bottom &&
	    (mdb->bottom != best->bottom || mdb->left != best->left || mdb->right != best->right || mdb->top != best->top))
	  best = mdb;
      }
    }
    else
    {
     /*
      * Look for the smallest non-zero margins...
      */

      for (mdb = (_cups_media_db_t *)cupsArrayNext(db);
	   mdb && !cups_compare_media_db(mdb, &key);
	   mdb = (_cups_media_db_t *)cupsArrayNext(db))
      {
	if (((mdb->left > 0 && mdb->left <= best->left) || best->left == 0) &&
	    ((mdb->right > 0 && mdb->right <= best->right) || best->right == 0) &&
	    ((mdb->top > 0 && mdb->top <= best->top) || best->top == 0) &&
	    ((mdb->bottom > 0 && mdb->bottom <= best->bottom) || best->bottom == 0) &&
	    (mdb->bottom != best->bottom || mdb->left != best->left || mdb->right != best->right || mdb->top != best->top))
	  best = mdb;
      }
    }
  }
  else if (flags & CUPS_MEDIA_FLAGS_EXACT)
  {
   /*
    * See if we can do this as a custom size...
    */

    if (pwg->width < dinfo->min_size.width ||
        pwg->width > dinfo->max_size.width ||
        pwg->length < dinfo->min_size.length ||
        pwg->length > dinfo->max_size.length)
      return (0);			/* Out of range */

    if ((flags & CUPS_MEDIA_FLAGS_BORDERLESS) &&
        (dinfo->min_size.left > 0 || dinfo->min_size.right > 0 ||
         dinfo->min_size.top > 0 || dinfo->min_size.bottom > 0))
      return (0);			/* Not borderless */

    key.size_name = (char *)pwg->pwg;
    key.bottom    = dinfo->min_size.bottom;
    key.left      = dinfo->min_size.left;
    key.right     = dinfo->min_size.right;
    key.top       = dinfo->min_size.top;

    best = &key;
  }
  else if (pwg->width >= dinfo->min_size.width &&
	   pwg->width <= dinfo->max_size.width &&
	   pwg->length >= dinfo->min_size.length &&
	   pwg->length <= dinfo->max_size.length)
  {
   /*
    * Map to custom size...
    */

    key.size_name = (char *)pwg->pwg;
    key.bottom    = dinfo->min_size.bottom;
    key.left      = dinfo->min_size.left;
    key.right     = dinfo->min_size.right;
    key.top       = dinfo->min_size.top;

    best = &key;
  }
  else
  {
   /*
    * Find a close size...
    */

    for (mdb = (_cups_media_db_t *)cupsArrayFirst(db);
         mdb;
         mdb = (_cups_media_db_t *)cupsArrayNext(db))
      if (cups_is_close_media_db(mdb, &key))
        break;

    if (!mdb)
      return (0);

    best = mdb;

    if (flags & CUPS_MEDIA_FLAGS_BORDERLESS)
    {
     /*
      * Look for the smallest margins...
      */

      if (best->left != 0 || best->right != 0 || best->top != 0 ||
          best->bottom != 0)
      {
	for (mdb = (_cups_media_db_t *)cupsArrayNext(db);
	     mdb && cups_is_close_media_db(mdb, &key);
	     mdb = (_cups_media_db_t *)cupsArrayNext(db))
	{
	  if (mdb->left <= best->left && mdb->right <= best->right &&
	      mdb->top <= best->top && mdb->bottom <= best->bottom &&
	      (mdb->bottom != best->bottom || mdb->left != best->left || mdb->right != best->right || mdb->top != best->top))
	  {
	    best = mdb;
	    if (mdb->left == 0 && mdb->right == 0 && mdb->bottom == 0 &&
		mdb->top == 0)
	      break;
	  }
	}
      }
    }
    else if (flags & CUPS_MEDIA_FLAGS_DUPLEX)
    {
     /*
      * Look for the largest margins...
      */

      for (mdb = (_cups_media_db_t *)cupsArrayNext(db);
	   mdb && cups_is_close_media_db(mdb, &key);
	   mdb = (_cups_media_db_t *)cupsArrayNext(db))
      {
	if (mdb->left >= best->left && mdb->right >= best->right &&
	    mdb->top >= best->top && mdb->bottom >= best->bottom &&
	    (mdb->bottom != best->bottom || mdb->left != best->left || mdb->right != best->right || mdb->top != best->top))
	  best = mdb;
      }
    }
    else
    {
     /*
      * Look for the smallest non-zero margins...
      */

      for (mdb = (_cups_media_db_t *)cupsArrayNext(db);
	   mdb && cups_is_close_media_db(mdb, &key);
	   mdb = (_cups_media_db_t *)cupsArrayNext(db))
      {
	if (((mdb->left > 0 && mdb->left <= best->left) || best->left == 0) &&
	    ((mdb->right > 0 && mdb->right <= best->right) ||
	     best->right == 0) &&
	    ((mdb->top > 0 && mdb->top <= best->top) || best->top == 0) &&
	    ((mdb->bottom > 0 && mdb->bottom <= best->bottom) ||
	     best->bottom == 0) &&
	    (mdb->bottom != best->bottom || mdb->left != best->left || mdb->right != best->right || mdb->top != best->top))
	  best = mdb;
      }
    }
  }

 /*
  * Return the matching size...
  */

  if (best->key)
    strlcpy(size->media, best->key, sizeof(size->media));
  else if (best->size_name)
    strlcpy(size->media, best->size_name, sizeof(size->media));
  else if (pwg->pwg)
    strlcpy(size->media, pwg->pwg, sizeof(size->media));
  else
    strlcpy(size->media, "unknown", sizeof(size->media));

  size->width  = best->width;
  size->length = best->length;
  size->bottom = best->bottom;
  size->left   = best->left;
  size->right  = best->right;
  size->top    = best->top;

  return (1);
}


/*
 * 'cups_is_close_media_db()' - Compare two media entries to see if they are
 *                              close to the same size.
 *
 * Currently we use 5 points (from PostScript) as the matching range...
 */

static int				/* O - 1 if the sizes are close */
cups_is_close_media_db(
    _cups_media_db_t *a,		/* I - First media entries */
    _cups_media_db_t *b)		/* I - Second media entries */
{
  int	dwidth,				/* Difference in width */
	dlength;			/* Difference in length */


  dwidth  = a->width - b->width;
  dlength = a->length - b->length;

  return (dwidth >= -176 && dwidth <= 176 &&
          dlength >= -176 && dlength <= 176);
}


/*
 * 'cups_test_constraints()' - Test constraints.
 */

static cups_array_t *			/* O - Active constraints */
cups_test_constraints(
    cups_dinfo_t  *dinfo,		/* I - Destination information */
    const char    *new_option,		/* I - Newly selected option */
    const char    *new_value,		/* I - Newly selected value */
    int           num_options,		/* I - Number of options */
    cups_option_t *options,		/* I - Options */
    int           *num_conflicts,	/* O - Number of conflicting options */
    cups_option_t **conflicts)		/* O - Conflicting options */
{
  int			i,		/* Looping var */
			count,		/* Number of values */
			match;		/* Value matches? */
  int			num_matching;	/* Number of matching options */
  cups_option_t		*matching;	/* Matching options */
  _cups_dconstres_t	*c;		/* Current constraint */
  cups_array_t		*active = NULL;	/* Active constraints */
  ipp_t			*col;		/* Collection value */
  ipp_attribute_t	*attr;		/* Current attribute */
  _ipp_value_t		*attrval;	/* Current attribute value */
  const char		*value;		/* Current value */
  char			temp[1024];	/* Temporary string */
  int			int_value;	/* Integer value */
  int			xres_value,	/* Horizontal resolution */
			yres_value;	/* Vertical resolution */
  ipp_res_t		units_value;	/* Resolution units */


  for (c = (_cups_dconstres_t *)cupsArrayFirst(dinfo->constraints);
       c;
       c = (_cups_dconstres_t *)cupsArrayNext(dinfo->constraints))
  {
    num_matching = 0;
    matching     = NULL;

    for (attr = ippFirstAttribute(c->collection);
         attr;
         attr = ippNextAttribute(c->collection))
    {
     /*
      * Get the value for the current attribute in the constraint...
      */

      if (new_option && new_value && !strcmp(attr->name, new_option))
        value = new_value;
      else if ((value = cupsGetOption(attr->name, num_options, options)) == NULL)
        value = cupsGetOption(attr->name, dinfo->num_defaults, dinfo->defaults);

      if (!value)
      {
       /*
        * Not set so this constraint does not apply...
        */

        break;
      }

      match = 0;

      switch (attr->value_tag)
      {
        case IPP_TAG_INTEGER :
        case IPP_TAG_ENUM :
	    int_value = atoi(value);

	    for (i = attr->num_values, attrval = attr->values;
	         i > 0;
	         i --, attrval ++)
	    {
	      if (attrval->integer == int_value)
	      {
		match = 1;
		break;
	      }
            }
            break;

        case IPP_TAG_BOOLEAN :
	    int_value = !strcmp(value, "true");

	    for (i = attr->num_values, attrval = attr->values;
	         i > 0;
	         i --, attrval ++)
	    {
	      if (attrval->boolean == int_value)
	      {
		match = 1;
		break;
	      }
            }
            break;

        case IPP_TAG_RANGE :
	    int_value = atoi(value);

	    for (i = attr->num_values, attrval = attr->values;
	         i > 0;
	         i --, attrval ++)
	    {
	      if (int_value >= attrval->range.lower &&
	          int_value <= attrval->range.upper)
	      {
		match = 1;
		break;
	      }
            }
            break;

        case IPP_TAG_RESOLUTION :
	    if (sscanf(value, "%dx%d%15s", &xres_value, &yres_value, temp) != 3)
	    {
	      if (sscanf(value, "%d%15s", &xres_value, temp) != 2)
		break;

	      yres_value = xres_value;
	    }

	    if (!strcmp(temp, "dpi"))
	      units_value = IPP_RES_PER_INCH;
	    else if (!strcmp(temp, "dpc") || !strcmp(temp, "dpcm"))
	      units_value = IPP_RES_PER_CM;
	    else
	      break;

	    for (i = attr->num_values, attrval = attr->values;
		 i > 0;
		 i --, attrval ++)
	    {
	      if (attrval->resolution.xres == xres_value &&
		  attrval->resolution.yres == yres_value &&
		  attrval->resolution.units == units_value)
	      {
	      	match = 1;
		break;
	      }
	    }
            break;

	case IPP_TAG_TEXT :
	case IPP_TAG_NAME :
	case IPP_TAG_KEYWORD :
	case IPP_TAG_CHARSET :
	case IPP_TAG_URI :
	case IPP_TAG_URISCHEME :
	case IPP_TAG_MIMETYPE :
	case IPP_TAG_LANGUAGE :
	case IPP_TAG_TEXTLANG :
	case IPP_TAG_NAMELANG :
	    for (i = attr->num_values, attrval = attr->values;
	         i > 0;
	         i --, attrval ++)
	    {
	      if (!strcmp(attrval->string.text, value))
	      {
		match = 1;
		break;
	      }
            }
	    break;

        case IPP_TAG_BEGIN_COLLECTION :
            col = ippNew();
            _cupsEncodeOption(col, IPP_TAG_ZERO, NULL, ippGetName(attr), value);

            for (i = 0, count = ippGetCount(attr); i < count; i ++)
            {
              if (cups_collection_contains(col, ippGetCollection(attr, i)))
              {
                match = 1;
                break;
	      }
            }

            ippDelete(col);
            break;

        default :
            break;
      }

      if (!match)
        break;

      num_matching = cupsAddOption(attr->name, value, num_matching, &matching);
    }

    if (!attr)
    {
      if (!active)
        active = cupsArrayNew(NULL, NULL);

      cupsArrayAdd(active, c);

      if (num_conflicts && conflicts)
      {
        cups_option_t	*moption;	/* Matching option */

        for (i = num_matching, moption = matching; i > 0; i --, moption ++)
          *num_conflicts = cupsAddOption(moption->name, moption->value, *num_conflicts, conflicts);
      }
    }

    cupsFreeOptions(num_matching, matching);
  }

  return (active);
}


/*
 * 'cups_update_ready()' - Update xxx-ready attributes for the printer.
 */

static void
cups_update_ready(http_t       *http,	/* I - Connection to destination */
                  cups_dinfo_t *dinfo)	/* I - Destination information */
{
  ipp_t	*request;			/* Get-Printer-Attributes request */
  static const char * const pattrs[] =	/* Printer attributes we want */
  {
    "finishings-col-ready",
    "finishings-ready",
    "job-finishings-col-ready",
    "job-finishings-ready",
    "media-col-ready",
    "media-ready"
  };


 /*
  * Don't update more than once every 30 seconds...
  */

  if ((time(NULL) - dinfo->ready_time) < _CUPS_MEDIA_READY_TTL)
    return;

 /*
  * Free any previous results...
  */

  if (dinfo->cached_flags & CUPS_MEDIA_FLAGS_READY)
  {
    cupsArrayDelete(dinfo->cached_db);
    dinfo->cached_db    = NULL;
    dinfo->cached_flags = CUPS_MEDIA_FLAGS_DEFAULT;
  }

  ippDelete(dinfo->ready_attrs);
  dinfo->ready_attrs = NULL;

  cupsArrayDelete(dinfo->ready_db);
  dinfo->ready_db = NULL;

 /*
  * Query the xxx-ready values...
  */

  request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
  ippSetVersion(request, dinfo->version / 10, dinfo->version % 10);

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL,
               dinfo->uri);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name",
               NULL, cupsUser());
  ippAddStrings(request, IPP_TAG_OPERATION, IPP_CONST_TAG(IPP_TAG_KEYWORD), "requested-attributes", (int)(sizeof(pattrs) / sizeof(pattrs[0])), NULL, pattrs);

  dinfo->ready_attrs = cupsDoRequest(http, request, dinfo->resource);

 /*
  * Update the ready media database...
  */

  cups_create_media_db(dinfo, CUPS_MEDIA_FLAGS_READY);

 /*
  * Update last lookup time and return...
  */

  dinfo->ready_time = time(NULL);
}
