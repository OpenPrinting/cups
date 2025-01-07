/*
 * Notification routines for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright 2007-2013 by Apple Inc.
 * Copyright 2005-2006 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more information.
 */

/*
 * Include necessary headers...
 */

#include "cups-private.h"
#include "debug-internal.h"


/*
 * 'cupsNotifySubject()' - Return the subject for the given notification message.
 *
 * The returned string must be freed by the caller using @code free@.
 *
 * @since CUPS 1.2/macOS 10.5@
 */

char *					/* O - Subject string or @code NULL@ */
cupsNotifySubject(cups_lang_t *lang,	/* I - Language data */
                  ipp_t       *event)	/* I - Event data */
{
  char			buffer[1024];	/* Subject buffer */
  const char		*prefix,	/* Prefix on subject */
			*state;		/* Printer/job state string */
  ipp_attribute_t	*job_id,	/* notify-job-id */
			*job_name,	/* job-name */
			*job_state,	/* job-state */
			*printer_name,	/* printer-name */
			*printer_state,	/* printer-state */
			*printer_uri,	/* notify-printer-uri */
			*subscribed;	/* notify-subscribed-event */


 /*
  * Range check input...
  */

  if (!event || !lang)
    return (NULL);

 /*
  * Get the required attributes...
  */

  job_id        = ippFindAttribute(event, "notify-job-id", IPP_TAG_INTEGER);
  job_name      = ippFindAttribute(event, "job-name", IPP_TAG_NAME);
  job_state     = ippFindAttribute(event, "job-state", IPP_TAG_ENUM);
  printer_name  = ippFindAttribute(event, "printer-name", IPP_TAG_NAME);
  printer_state = ippFindAttribute(event, "printer-state", IPP_TAG_ENUM);
  printer_uri   = ippFindAttribute(event, "notify-printer-uri", IPP_TAG_URI);
  subscribed    = ippFindAttribute(event, "notify-subscribed-event",
                                   IPP_TAG_KEYWORD);


  if (job_id && printer_name && printer_uri && job_state)
  {
   /*
    * Job event...
    */

    prefix = _cupsLangString(lang, _("Print Job:"));

    switch (job_state->values[0].integer)
    {
      case IPP_JSTATE_PENDING :
          state = _cupsLangString(lang, _("pending"));
	  break;
      case IPP_JSTATE_HELD :
          state = _cupsLangString(lang, _("held"));
	  break;
      case IPP_JSTATE_PROCESSING :
          state = _cupsLangString(lang, _("processing"));
	  break;
      case IPP_JSTATE_STOPPED :
          state = _cupsLangString(lang, _("stopped"));
	  break;
      case IPP_JSTATE_CANCELED :
          state = _cupsLangString(lang, _("canceled"));
	  break;
      case IPP_JSTATE_ABORTED :
          state = _cupsLangString(lang, _("aborted"));
	  break;
      case IPP_JSTATE_COMPLETED :
          state = _cupsLangString(lang, _("completed"));
	  break;
      default :
          state = _cupsLangString(lang, _("unknown"));
	  break;
    }

    snprintf(buffer, sizeof(buffer), "%s %s-%d (%s) %s",
             prefix,
	     printer_name->values[0].string.text,
	     job_id->values[0].integer,
	     job_name ? job_name->values[0].string.text :
	         _cupsLangString(lang, _("untitled")),
	     state);
  }
  else if (printer_uri && printer_name && printer_state)
  {
   /*
    * Printer event...
    */

    prefix = _cupsLangString(lang, _("Printer:"));

    switch (printer_state->values[0].integer)
    {
      case IPP_PSTATE_IDLE :
          state = _cupsLangString(lang, _("idle"));
	  break;
      case IPP_PSTATE_PROCESSING :
          state = _cupsLangString(lang, _("processing"));
	  break;
      case IPP_PSTATE_STOPPED :
          state = _cupsLangString(lang, _("stopped"));
	  break;
      default :
          state = _cupsLangString(lang, _("unknown"));
	  break;
    }

    snprintf(buffer, sizeof(buffer), "%s %s %s",
             prefix,
	     printer_name->values[0].string.text,
	     state);
  }
  else if (subscribed)
    strlcpy(buffer, subscribed->values[0].string.text, sizeof(buffer));
  else
    return (NULL);

 /*
  * Duplicate and return the subject string...
  */

  return (strdup(buffer));
}


/*
 * 'cupsNotifyText()' - Return the text for the given notification message.
 *
 * The returned string must be freed by the caller using @code free@.
 *
 * @since CUPS 1.2/macOS 10.5@
 */

char *					/* O - Message text or @code NULL@ */
cupsNotifyText(cups_lang_t *lang,	/* I - Language data */
               ipp_t       *event)	/* I - Event data */
{
  ipp_attribute_t	*notify_text;	/* notify-text */


 /*
  * Range check input...
  */

  if (!event || !lang)
    return (NULL);

 /*
  * Get the notify-text attribute from the server...
  */

  if ((notify_text = ippFindAttribute(event, "notify-text",
                                      IPP_TAG_TEXT)) == NULL)
    return (NULL);

 /*
  * Return a copy...
  */

  return (strdup(notify_text->values[0].string.text));
}
