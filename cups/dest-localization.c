/*
 * Destination localization support for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2012-2017 by Apple Inc.
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
 * Local functions...
 */

static void	cups_create_localizations(http_t *http, cups_dinfo_t *dinfo);


/*
 * 'cupsLocalizeDestMedia()' - Get the localized string for a destination media
 *                             size.
 *
 * This function returns the localized string for the specified media size
 * information.
 *
 * The returned string is stored in the destination information and will become
 * invalid if the destination information is deleted.
 *
 * @since CUPS 2.0/macOS 10.10@
 */

const char *				/* O - Localized string */
cupsLocalizeDestMedia(
    http_t       *http,			/* I - Connection to destination */
    cups_dest_t  *dest,			/* I - Destination */
    cups_dinfo_t *dinfo,		/* I - Destination information */
    unsigned     flags,			/* I - Media flags */
    cups_size_t  *size)			/* I - Media size */
{
  cups_media_t	media;			/* Media information */


 /*
  * Range check input...
  */

  if (!http || !dest || !dinfo || !size)
  {
    DEBUG_puts("1cupsLocalizeDestMedia: Returning NULL.");
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (NULL);
  }

 /*
  * Copy the media size into a media information value...
  */

  memset(&media, 0, sizeof(media));
  cupsCopyString(media.media, size->media, sizeof(media.media));

  media.width  = size->width;
  media.length = size->length;
  media.bottom = size->bottom;
  media.left   = size->left;
  media.right  = size->right;
  media.top    = size->top;

 /*
  * Localize it...
  */

  return (cupsLocalizeDestMedia2(http, dest, dinfo, flags, &media));
}


/*
 * 'cupsLocalizeDestMedia2()' - Get the localized string for a destination media.
 *
 * This function returns the localized string for the specified media
 * information.
 *
 * The returned string is stored in the destination information and will become
 * invalid if the destination information is deleted.
 *
 * @since CUPS 2.5@
 */

const char *				/* O - Localized string */
cupsLocalizeDestMedia2(
    http_t       *http,			/* I - Connection to destination */
    cups_dest_t  *dest,			/* I - Destination */
    cups_dinfo_t *dinfo,		/* I - Destination information */
    unsigned     flags,			/* I - Media flags */
    cups_media_t *media)		/* I - Media */
{

  cups_lang_t		*lang;		/* Standard localizations */
  _cups_message_t	key,		/* Search key */
			*match;		/* Matching entry */
  pwg_media_t		*pwg;		/* PWG media information */
  cups_array_t		*db;		/* Media database */
  _cups_media_db_t	*mdb;		/* Media database entry */
  char			lstr[1024],	/* Localized size name */
			temp[256];	/* Temporary string */
  const char		*lsize,		/* Localized media size */
			*lsource,	/* Localized media source */
			*ltype;		/* Localized media type */


  DEBUG_printf("cupsLocalizeDestMedia2(http=%p, dest=%p, dinfo=%p, flags=%x, media=%p(\"%s\"))", (void *)http, (void *)dest, (void *)dinfo, flags, (void *)media, media ? media->media : "(null)");

 /*
  * Range check input...
  */

  if (!http || !dest || !dinfo || !media)
  {
    DEBUG_puts("1cupsLocalizeDestMedia: Returning NULL.");
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (NULL);
  }

 /*
  * Find the matching media database entry...
  */

  if (flags & CUPS_MEDIA_FLAGS_READY)
    db = dinfo->ready_db;
  else
    db = dinfo->media_db;

  DEBUG_printf("1cupsLocalizeDestMedia2: media=\"%s\"", media->media);

  for (mdb = (_cups_media_db_t *)cupsArrayFirst(db); mdb; mdb = (_cups_media_db_t *)cupsArrayNext(db))
  {
    if (mdb->key && !strcmp(mdb->key, media->media))
      break;
    else if (mdb->size_name && !strcmp(mdb->size_name, media->media))
      break;
  }

  if (!mdb)
  {
    for (mdb = (_cups_media_db_t *)cupsArrayFirst(db); mdb; mdb = (_cups_media_db_t *)cupsArrayNext(db))
    {
      if (mdb->width == media->width && mdb->length == media->length && mdb->bottom == media->bottom && mdb->left == media->left && mdb->right == media->right && mdb->top == media->top)
	break;
    }
  }

 /*
  * See if the localization is cached...
  */

  lang = cupsLangDefault();

  if (!dinfo->localizations)
    cups_create_localizations(http, dinfo);

  snprintf(temp, sizeof(temp), "media.%s", media->media);
  key.msg = temp;

  if ((match = (_cups_message_t *)cupsArrayFind(dinfo->localizations, &key)) != NULL)
  {
    lsize = match->str;
  }
  else
  {
   /*
    * Not a media name, try a media-key name...
    */

    snprintf(temp, sizeof(temp), "media-key.%s", media->media);
    if ((match = (_cups_message_t *)cupsArrayFind(dinfo->localizations, &key)) != NULL)
      lsize = match->str;
    else
      lsize = NULL;
  }

  if (!lsize && (pwg = pwgMediaForSize(media->width, media->length)) != NULL && pwg->ppd)
  {
   /*
    * Get a standard localization...
    */

    snprintf(temp, sizeof(temp), "media.%s", pwg->pwg);
    if ((lsize = _cupsLangString(lang, temp)) == temp)
      lsize = NULL;
  }

  if (!lsize)
  {
   /*
    * Make a dimensional localization...
    */

    if ((media->width % 635) == 0 && (media->length % 635) == 0)
    {
     /*
      * Use inches since the size is a multiple of 1/4 inch.
      */

      snprintf(temp, sizeof(temp), _cupsLangString(lang, _("%g x %g \"")), media->width / 2540.0, media->length / 2540.0);
    }
    else
    {
     /*
      * Use millimeters since the size is not a multiple of 1/4 inch.
      */

      snprintf(temp, sizeof(temp), _cupsLangString(lang, _("%d x %d mm")), (media->width + 50) / 100, (media->length + 50) / 100);
    }

    lsize = temp;
  }

  if (media->source[0])
  {
    if ((lsource = cupsLocalizeDestValue(http, dest, dinfo, "media-source", media->source)) == media->source)
      lsource = _cupsLangString(lang, _("Other Tray"));;
  }
  else if (mdb)
  {
    DEBUG_printf("1cupsLocalizeDestMedia2: MATCH mdb%p [key=\"%s\" size_name=\"%s\" source=\"%s\" type=\"%s\" width=%d length=%d B%d L%d R%d T%d]", (void *)mdb, mdb->key, mdb->size_name, mdb->source, mdb->type, mdb->width, mdb->length, mdb->bottom, mdb->left, mdb->right, mdb->top);

    if ((lsource = cupsLocalizeDestValue(http, dest, dinfo, "media-source", mdb->source)) == mdb->source && mdb->source)
      lsource = _cupsLangString(lang, _("Other Tray"));
  }
  else
  {
    lsource = NULL;
  }

  if (media->type[0])
  {
    if ((ltype = cupsLocalizeDestValue(http, dest, dinfo, "media-type", media->type)) == media->type)
      ltype = _cupsLangString(lang, _("Other Media"));
  }
  else if (mdb)
  {
    DEBUG_printf("1cupsLocalizeDestMedia2: MATCH mdb%p [key=\"%s\" size_name=\"%s\" source=\"%s\" type=\"%s\" width=%d length=%d B%d L%d R%d T%d]", (void *)mdb, mdb->key, mdb->size_name, mdb->source, mdb->type, mdb->width, mdb->length, mdb->bottom, mdb->left, mdb->right, mdb->top);

    if ((ltype = cupsLocalizeDestValue(http, dest, dinfo, "media-type", mdb->type)) == mdb->type && mdb->type)
      ltype = _cupsLangString(lang, _("Other Media"));
  }
  else
  {
    ltype = NULL;
  }

  if (!lsource && !ltype)
  {
    if (!media->bottom && !media->left && !media->right && !media->top)
      snprintf(lstr, sizeof(lstr), _cupsLangString(lang, _("%s (Borderless)")), lsize);
    else
      cupsCopyString(lstr, lsize, sizeof(lstr));
  }
  else if (!lsource)
  {
    if (!media->bottom && !media->left && !media->right && !media->top)
      snprintf(lstr, sizeof(lstr), _cupsLangString(lang, _("%s (Borderless, %s)")), lsize, ltype);
    else
      snprintf(lstr, sizeof(lstr), _cupsLangString(lang, _("%s (%s)")), lsize, ltype);
  }
  else if (!ltype)
  {
    if (!media->bottom && !media->left && !media->right && !media->top)
      snprintf(lstr, sizeof(lstr), _cupsLangString(lang, _("%s (Borderless, %s)")), lsize, lsource);
    else
      snprintf(lstr, sizeof(lstr), _cupsLangString(lang, _("%s (%s)")), lsize, lsource);
  }
  else
  {
    if (!media->bottom && !media->left && !media->right && !media->top)
      snprintf(lstr, sizeof(lstr), _cupsLangString(lang, _("%s (Borderless, %s, %s)")), lsize, ltype, lsource);
    else
      snprintf(lstr, sizeof(lstr), _cupsLangString(lang, _("%s (%s, %s)")), lsize, ltype, lsource);
  }

  if ((match = (_cups_message_t *)calloc(1, sizeof(_cups_message_t))) == NULL)
    return (NULL);

  match->msg = strdup(media->media);
  match->str = strdup(lstr);

  cupsArrayAdd(dinfo->localizations, match);

  DEBUG_printf("1cupsLocalizeDestMedia: Returning \"%s\".", match->str);

  return (match->str);
}


/*
 * 'cupsLocalizeDestOption()' - Get the localized string for a destination
 *                              option.
 *
 * The returned string is stored in the destination information and will become
 * invalid if the destination information is deleted.
 *
 * @since CUPS 1.6/macOS 10.8@
 */

const char *				/* O - Localized string */
cupsLocalizeDestOption(
    http_t       *http,			/* I - Connection to destination */
    cups_dest_t  *dest,			/* I - Destination */
    cups_dinfo_t *dinfo,		/* I - Destination information */
    const char   *option)		/* I - Option to localize */
{
  _cups_message_t	key,		/* Search key */
			*match;		/* Matching entry */
  const char            *localized;     /* Localized string */


  DEBUG_printf("cupsLocalizeDestOption(http=%p, dest=%p, dinfo=%p, option=\"%s\")", (void *)http, (void *)dest, (void *)dinfo, option);

  if (!http || !dest || !dinfo)
    return (option);

  if (!dinfo->localizations)
    cups_create_localizations(http, dinfo);

  key.msg = (char *)option;
  if ((match = (_cups_message_t *)cupsArrayFind(dinfo->localizations, &key)) != NULL)
    return (match->str);
  else if ((localized = _cupsLangString(cupsLangDefault(), option)) != NULL)
    return (localized);
  else
    return (option);
}


/*
 * 'cupsLocalizeDestValue()' - Get the localized string for a destination
 *                             option+value pair.
 *
 * The returned string is stored in the destination information and will become
 * invalid if the destination information is deleted.
 *
 * @since CUPS 1.6/macOS 10.8@
 */

const char *				/* O - Localized string */
cupsLocalizeDestValue(
    http_t       *http,			/* I - Connection to destination */
    cups_dest_t  *dest,			/* I - Destination */
    cups_dinfo_t *dinfo,		/* I - Destination information */
    const char   *option,		/* I - Option to localize */
    const char   *value)		/* I - Value to localize */
{
  _cups_message_t	key,		/* Search key */
			*match;		/* Matching entry */
  char			pair[256];	/* option.value pair */
  const char            *localized;     /* Localized string */


  DEBUG_printf("cupsLocalizeDestValue(http=%p, dest=%p, dinfo=%p, option=\"%s\", value=\"%s\")", (void *)http, (void *)dest, (void *)dinfo, option, value);

  if (!http || !dest || !dinfo)
    return (value);

  if (!strcmp(option, "media"))
  {
    pwg_media_t *media = pwgMediaForPWG(value);
    cups_size_t size;

    cupsCopyString(size.media, value, sizeof(size.media));
    size.width  = media ? media->width : 0;
    size.length = media ? media->length : 0;
    size.left   = 0;
    size.right  = 0;
    size.bottom = 0;
    size.top    = 0;

    return (cupsLocalizeDestMedia(http, dest, dinfo, CUPS_MEDIA_FLAGS_DEFAULT, &size));
  }

  if (!dinfo->localizations)
    cups_create_localizations(http, dinfo);

  snprintf(pair, sizeof(pair), "%s.%s", option, value);
  key.msg = pair;
  if ((match = (_cups_message_t *)cupsArrayFind(dinfo->localizations, &key)) != NULL)
    return (match->str);
  else if ((localized = _cupsLangString(cupsLangDefault(), pair)) != NULL && strcmp(localized, pair))
    return (localized);
  else
    return (value);
}


/*
 * 'cups_create_localizations()' - Create the localizations array for a
 *                                 destination.
 */

static void
cups_create_localizations(
    http_t       *http,			/* I - Connection to destination */
    cups_dinfo_t *dinfo)		/* I - Destination information */
{
  http_t		*http2;		/* Connection for strings file */
  http_status_t		status;		/* Request status */
  ipp_attribute_t	*attr;		/* "printer-strings-uri" attribute */
  char			scheme[32],	/* URI scheme */
  			userpass[256],	/* Username/password info */
  			hostname[256],	/* Hostname */
  			resource[1024],	/* Resource */
  			http_hostname[256],
  					/* Hostname of connection */
			tempfile[1024];	/* Temporary filename */
  int			port;		/* Port number */
  http_encryption_t	encryption;	/* Encryption to use */
  cups_file_t		*temp;		/* Temporary file */


 /*
  * See if there are any localizations...
  */

  if ((attr = ippFindAttribute(dinfo->attrs, "printer-strings-uri",
                               IPP_TAG_URI)) == NULL)
  {
   /*
    * Nope, create an empty message catalog...
    */

    dinfo->localizations = _cupsMessageNew(NULL);
    DEBUG_puts("4cups_create_localizations: No printer-strings-uri (uri) value.");
    return;
  }

 /*
  * Pull apart the URI and determine whether we need to try a different
  * server...
  */

  if (httpSeparateURI(HTTP_URI_CODING_ALL, attr->values[0].string.text,
                      scheme, sizeof(scheme), userpass, sizeof(userpass),
                      hostname, sizeof(hostname), &port, resource,
                      sizeof(resource)) < HTTP_URI_STATUS_OK)
  {
    dinfo->localizations = _cupsMessageNew(NULL);
    DEBUG_printf("4cups_create_localizations: Bad printer-strings-uri value \"%s\".", attr->values[0].string.text);
    return;
  }

  httpGetHostname(http, http_hostname, sizeof(http_hostname));

  if (!_cups_strcasecmp(http_hostname, hostname) &&
      port == httpAddrPort(http->hostaddr))
  {
   /*
    * Use the same connection...
    */

    http2 = http;
  }
  else
  {
   /*
    * Connect to the alternate host...
    */

    if (!strcmp(scheme, "https"))
      encryption = HTTP_ENCRYPTION_ALWAYS;
    else
      encryption = HTTP_ENCRYPTION_IF_REQUESTED;

    if ((http2 = httpConnect2(hostname, port, NULL, AF_UNSPEC, encryption, 1,
                              30000, NULL)) == NULL)
    {
      DEBUG_printf("4cups_create_localizations: Unable to connect to %s:%d: %s", hostname, port, cupsGetErrorString());
      return;
    }
  }

 /*
  * Get a temporary file...
  */

  if ((temp = cupsTempFile2(tempfile, sizeof(tempfile))) == NULL)
  {
    DEBUG_printf("4cups_create_localizations: Unable to create temporary file: %s", cupsGetErrorString());
    if (http2 != http)
      httpClose(http2);
    return;
  }

  status = cupsGetFd(http2, resource, cupsFileNumber(temp));
  cupsFileClose(temp);

  DEBUG_printf("4cups_create_localizations: GET %s = %s", resource, httpStatusString(status));

  if (status == HTTP_STATUS_OK)
  {
   /*
    * Got the file, read it...
    */

    dinfo->localizations = _cupsMessageLoad(NULL, tempfile, _CUPS_MESSAGE_STRINGS);
  }

  DEBUG_printf("4cups_create_localizations: %d messages loaded.", cupsArrayCount(dinfo->localizations));

 /*
  * Cleanup...
  */

  unlink(tempfile);

  if (http2 != http)
    httpClose(http2);
}

