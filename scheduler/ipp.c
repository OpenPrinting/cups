/*
 * IPP routines for the CUPS scheduler.
 *
 * Copyright © 2020-2024 by OpenPrinting
 * Copyright © 2007-2021 by Apple Inc.
 * Copyright © 1997-2007 by Easy Software Products, all rights reserved.
 *
 * This file contains Kerberos support code, copyright 2006 by
 * Jelmer Vernooij.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "cupsd.h"
#include <cups/ppd-private.h>

#ifdef __APPLE__
#  ifdef HAVE_MEMBERSHIP_H
#    include <membership.h>
#  endif /* HAVE_MEMBERSHIP_H */
extern int mbr_user_name_to_uuid(const char* name, uuid_t uu);
extern int mbr_group_name_to_uuid(const char* name, uuid_t uu);
extern int mbr_check_membership_by_id(uuid_t user, gid_t group, int* ismember);
#endif /* __APPLE__ */


/*
 * Local functions...
 */

static void	accept_jobs(cupsd_client_t *con, ipp_attribute_t *uri);
static void	add_class(cupsd_client_t *con, ipp_attribute_t *uri);
static int	add_file(cupsd_client_t *con, cupsd_job_t *job,
		         mime_type_t *filetype, int compression);
static cupsd_job_t *add_job(cupsd_client_t *con, cupsd_printer_t *printer,
			    mime_type_t *filetype);
static void	add_job_subscriptions(cupsd_client_t *con, cupsd_job_t *job);
static void	add_job_uuid(cupsd_job_t *job);
static void	add_printer(cupsd_client_t *con, ipp_attribute_t *uri);
static void	add_printer_state_reasons(cupsd_client_t *con,
		                          cupsd_printer_t *p);
static void	add_queued_job_count(cupsd_client_t *con, cupsd_printer_t *p);
static void	apply_printer_defaults(cupsd_printer_t *printer,
				       cupsd_job_t *job);
static void	authenticate_job(cupsd_client_t *con, ipp_attribute_t *uri);
static void	cancel_all_jobs(cupsd_client_t *con, ipp_attribute_t *uri);
static void	cancel_job(cupsd_client_t *con, ipp_attribute_t *uri);
static void	cancel_subscription(cupsd_client_t *con, int id);
static int	check_rss_recipient(const char *recipient);
static int	check_quotas(cupsd_client_t *con, cupsd_printer_t *p);
static void	close_job(cupsd_client_t *con, ipp_attribute_t *uri);
static void	copy_attrs(ipp_t *to, ipp_t *from, cups_array_t *ra,
		           ipp_tag_t group, int quickcopy,
			   cups_array_t *exclude);
static int	copy_banner(cupsd_client_t *con, cupsd_job_t *job,
		            const char *name);
static int	copy_file(const char *from, const char *to, mode_t mode);
static int	copy_model(cupsd_client_t *con, const char *from,
		           const char *to);
static void	copy_job_attrs(cupsd_client_t *con,
		               cupsd_job_t *job,
			       cups_array_t *ra, cups_array_t *exclude);
static void	copy_printer_attrs(cupsd_client_t *con,
		                   cupsd_printer_t *printer,
				   cups_array_t *ra);
static void	copy_subscription_attrs(cupsd_client_t *con,
		                        cupsd_subscription_t *sub,
					cups_array_t *ra,
					cups_array_t *exclude);
static void	create_job(cupsd_client_t *con, ipp_attribute_t *uri);
static void	*create_local_bg_thread(cupsd_client_t *con);
static void	create_local_printer(cupsd_client_t *con);
static cups_array_t *create_requested_array(ipp_t *request);
static void	create_subscriptions(cupsd_client_t *con, ipp_attribute_t *uri);
static void	delete_printer(cupsd_client_t *con, ipp_attribute_t *uri);
static void	get_default(cupsd_client_t *con);
static void	get_devices(cupsd_client_t *con);
static void	get_document(cupsd_client_t *con, ipp_attribute_t *uri);
static void	get_jobs(cupsd_client_t *con, ipp_attribute_t *uri);
static void	get_job_attrs(cupsd_client_t *con, ipp_attribute_t *uri);
static void	get_notifications(cupsd_client_t *con);
static void	get_ppd(cupsd_client_t *con, ipp_attribute_t *uri);
static void	get_ppds(cupsd_client_t *con);
static void	get_printers(cupsd_client_t *con, int type);
static void	get_printer_attrs(cupsd_client_t *con, ipp_attribute_t *uri);
static void	get_printer_supported(cupsd_client_t *con, ipp_attribute_t *uri);
static void	get_subscription_attrs(cupsd_client_t *con, int sub_id);
static void	get_subscriptions(cupsd_client_t *con, ipp_attribute_t *uri);
static const char *get_username(cupsd_client_t *con);
static void	hold_job(cupsd_client_t *con, ipp_attribute_t *uri);
static void	hold_new_jobs(cupsd_client_t *con, ipp_attribute_t *uri);
static void	move_job(cupsd_client_t *con, ipp_attribute_t *uri);
static int	ppd_parse_line(const char *line, char *option, int olen,
		               char *choice, int clen);
static void	print_job(cupsd_client_t *con, ipp_attribute_t *uri);
static void	read_job_ticket(cupsd_client_t *con);
static void	reject_jobs(cupsd_client_t *con, ipp_attribute_t *uri);
static void	release_held_new_jobs(cupsd_client_t *con,
		                      ipp_attribute_t *uri);
static void	release_job(cupsd_client_t *con, ipp_attribute_t *uri);
static void	renew_subscription(cupsd_client_t *con, int sub_id);
static void	restart_job(cupsd_client_t *con, ipp_attribute_t *uri);
static void	save_auth_info(cupsd_client_t *con, cupsd_job_t *job,
		               ipp_attribute_t *auth_info);
static void	send_document(cupsd_client_t *con, ipp_attribute_t *uri);
static void	send_http_error(cupsd_client_t *con, http_status_t status,
		                cupsd_printer_t *printer);
static void	send_ipp_status(cupsd_client_t *con, ipp_status_t status, const char *message, ...) _CUPS_FORMAT(3, 4);
static int	send_response(cupsd_client_t *con);
static void	set_default(cupsd_client_t *con, ipp_attribute_t *uri);
static void	set_job_attrs(cupsd_client_t *con, ipp_attribute_t *uri);
static void	set_printer_attrs(cupsd_client_t *con, ipp_attribute_t *uri);
static int	set_printer_defaults(cupsd_client_t *con, cupsd_printer_t *printer);
static void	start_printer(cupsd_client_t *con, ipp_attribute_t *uri);
static void	stop_printer(cupsd_client_t *con, ipp_attribute_t *uri);
static void	url_encode_attr(ipp_attribute_t *attr, char *buffer, size_t bufsize);
static char	*url_encode_string(const char *s, char *buffer, size_t bufsize);
static int	user_allowed(cupsd_printer_t *p, const char *username);
static void	validate_job(cupsd_client_t *con, ipp_attribute_t *uri);
static int	validate_name(const char *name);
static int	validate_user(cupsd_job_t *job, cupsd_client_t *con, const char *owner, char *username, size_t userlen);


/*
 * 'cupsdProcessIPPRequest()' - Process an incoming IPP request.
 */

int					/* O - 1 on success, 0 on failure */
cupsdProcessIPPRequest(
    cupsd_client_t *con)		/* I - Client connection */
{
  ipp_tag_t		group;		/* Current group tag */
  ipp_attribute_t	*attr;		/* Current attribute */
  ipp_attribute_t	*charset;	/* Character set attribute */
  ipp_attribute_t	*language;	/* Language attribute */
  ipp_attribute_t	*uri = NULL;	/* Printer or job URI attribute */
  ipp_attribute_t	*username;	/* requesting-user-name attr */
  int			sub_id;		/* Subscription ID */
  int			valid = 1;	/* Valid request? */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdProcessIPPRequest(%p[%d]): operation_id=%04x(%s)", (void *)con, con->number, con->request->request.op.operation_id, ippOpString(con->request->request.op.operation_id));

  if (LogLevel >= CUPSD_LOG_DEBUG2)
  {
    for (group = IPP_TAG_ZERO, attr = ippFirstAttribute(con->request); attr; attr = ippNextAttribute(con->request))
    {
      const char  *name;                /* Attribute name */
      char        value[1024];          /* Attribute value */

      if (group != ippGetGroupTag(attr))
      {
        group = ippGetGroupTag(attr);
        if (group != IPP_TAG_ZERO)
          cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdProcessIPPRequest: %s", ippTagString(group));
      }

      if ((name = ippGetName(attr)) == NULL)
        continue;

      ippAttributeString(attr, value, sizeof(value));

      cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdProcessIPPRequest: %s %s%s '%s'", name, ippGetCount(attr) > 1 ? "1setOf " : "", ippTagString(ippGetValueTag(attr)), value);
    }
  }

 /*
  * First build an empty response message for this request...
  */

  con->response = ippNew();

  con->response->request.status.version[0] = con->request->request.op.version[0];
  con->response->request.status.version[1] = con->request->request.op.version[1];
  con->response->request.status.request_id = con->request->request.op.request_id;

 /*
  * Then validate the request header and required attributes...
  */

  if (con->request->request.any.version[0] != 1 && con->request->request.any.version[0] != 2)
  {
   /*
    * Return an error, since we only support IPP 1.x and 2.x.
    */

    cupsdAddEvent(CUPSD_EVENT_SERVER_AUDIT, NULL, NULL, "%04X %s Bad request version number %d.%d.", IPP_STATUS_ERROR_VERSION_NOT_SUPPORTED, con->http->hostname, con->request->request.any.version[0], con->request->request.any.version[1]);

    send_ipp_status(con, IPP_STATUS_ERROR_VERSION_NOT_SUPPORTED, _("Bad request version number %d.%d."), con->request->request.any.version[0], con->request->request.any.version[1]);
  }
  else if (con->request->request.any.request_id < 1)
  {
   /*
    * Return an error, since request IDs must be between 1 and 2^31-1
    */

    cupsdAddEvent(CUPSD_EVENT_SERVER_AUDIT, NULL, NULL, "%04X %s Bad request ID %d.", IPP_STATUS_ERROR_BAD_REQUEST, con->http->hostname, con->request->request.any.request_id);

    send_ipp_status(con, IPP_STATUS_ERROR_BAD_REQUEST, _("Bad request ID %d."), con->request->request.any.request_id);
  }
  else if (!con->request->attrs)
  {
    cupsdAddEvent(CUPSD_EVENT_SERVER_AUDIT, NULL, NULL, "%04X %s No attributes in request.", IPP_STATUS_ERROR_BAD_REQUEST, con->http->hostname);

    send_ipp_status(con, IPP_STATUS_ERROR_BAD_REQUEST, _("No attributes in request."));
  }
  else
  {
   /*
    * Make sure that the attributes are provided in the correct order and
    * don't repeat groups...
    */

    for (attr = con->request->attrs, group = attr->group_tag;
	 attr;
	 attr = attr->next)
      if (attr->group_tag < group && attr->group_tag != IPP_TAG_ZERO)
      {
       /*
	* Out of order; return an error...
	*/

	cupsdAddEvent(CUPSD_EVENT_SERVER_AUDIT, NULL, NULL, "%04X %s Attribute groups are out of order", IPP_STATUS_ERROR_BAD_REQUEST, con->http->hostname);

	send_ipp_status(con, IPP_STATUS_ERROR_BAD_REQUEST, _("Attribute groups are out of order (%x < %x)."), attr->group_tag, group);
	break;
      }
      else
	group = attr->group_tag;

    if (!attr)
    {
     /*
      * Then make sure that the first three attributes are:
      *
      *     attributes-charset
      *     attributes-natural-language
      *     printer-uri/job-uri
      */

      attr = con->request->attrs;
      if (attr && attr->name && !strcmp(attr->name, "attributes-charset") && (attr->value_tag & IPP_TAG_MASK) == IPP_TAG_CHARSET && attr->group_tag == IPP_TAG_OPERATION)
	charset = attr;
      else
	charset = NULL;

      if (attr)
        attr = attr->next;

      if (attr && attr->name && !strcmp(attr->name, "attributes-natural-language") && (attr->value_tag & IPP_TAG_MASK) == IPP_TAG_LANGUAGE && attr->group_tag == IPP_TAG_OPERATION)
      {
	language = attr;

       /*
        * Reset language for this request if different from Accept-Language.
        */

	if (!con->language ||
	    strcmp(attr->values[0].string.text, con->language->language))
	{
	  cupsLangFree(con->language);
	  con->language = cupsLangGet(attr->values[0].string.text);
	}
      }
      else
	language = NULL;

      if ((attr = ippFindAttribute(con->request, "printer-uri", IPP_TAG_URI)) != NULL && attr->group_tag == IPP_TAG_OPERATION)
	uri = attr;
      else if ((attr = ippFindAttribute(con->request, "job-uri", IPP_TAG_URI)) != NULL && attr->group_tag == IPP_TAG_OPERATION)
	uri = attr;
      else if (con->request->request.op.operation_id == CUPS_GET_PPD && (attr = ippFindAttribute(con->request, "ppd-name", IPP_TAG_NAME)) != NULL && attr->group_tag == IPP_TAG_OPERATION)
        uri = attr;
      else
	uri = NULL;

      if (charset)
	ippAddString(con->response, IPP_TAG_OPERATION, IPP_TAG_CHARSET, "attributes-charset", NULL, charset->values[0].string.text);
      else
	ippAddString(con->response, IPP_TAG_OPERATION, IPP_TAG_CHARSET, "attributes-charset", NULL, "utf-8");

      if (language)
	ippAddString(con->response, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE, "attributes-natural-language", NULL, language->values[0].string.text);
      else
	ippAddString(con->response, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE, "attributes-natural-language", NULL, DefaultLanguage);

      if (charset && _cups_strcasecmp(charset->values[0].string.text, "us-ascii") && _cups_strcasecmp(charset->values[0].string.text, "utf-8"))
      {
       /*
        * Bad character set...
	*/

        cupsdLogMessage(CUPSD_LOG_ERROR, "Unsupported character set \"%s\"",
	                charset->values[0].string.text);
	cupsdAddEvent(CUPSD_EVENT_SERVER_AUDIT, NULL, NULL, "%04X %s Unsupported attributes-charset value \"%s\".", IPP_STATUS_ERROR_CHARSET, con->http->hostname, charset->values[0].string.text);
	send_ipp_status(con, IPP_STATUS_ERROR_CHARSET, _("Unsupported character set \"%s\"."), charset->values[0].string.text);
      }
      else if (!charset || !language ||
	       (!uri &&
	        con->request->request.op.operation_id != CUPS_GET_DEFAULT &&
	        con->request->request.op.operation_id != CUPS_GET_PRINTERS &&
	        con->request->request.op.operation_id != CUPS_GET_CLASSES &&
	        con->request->request.op.operation_id != CUPS_GET_DEVICES &&
	        con->request->request.op.operation_id != CUPS_GET_PPDS))
      {
       /*
	* Return an error, since attributes-charset,
	* attributes-natural-language, and printer-uri/job-uri are required
	* for all operations.
	*/

        if (!charset)
	{
	  cupsdLogMessage(CUPSD_LOG_ERROR, "Missing attributes-charset attribute.");

	  cupsdAddEvent(CUPSD_EVENT_SERVER_AUDIT, NULL, NULL, "%04X %s Missing attributes-charset attribute.", IPP_STATUS_ERROR_BAD_REQUEST, con->http->hostname);
        }

        if (!language)
	{
	  cupsdLogMessage(CUPSD_LOG_ERROR,
	                  "Missing attributes-natural-language attribute.");

	  cupsdAddEvent(CUPSD_EVENT_SERVER_AUDIT, NULL, NULL, "%04X %s Missing attributes-natural-language attribute.", IPP_STATUS_ERROR_BAD_REQUEST, con->http->hostname);
        }

        if (!uri)
	{
	  cupsdLogMessage(CUPSD_LOG_ERROR, "Missing printer-uri, job-uri, or ppd-name attribute.");

	  cupsdAddEvent(CUPSD_EVENT_SERVER_AUDIT, NULL, NULL, "%04X %s Missing printer-uri, job-uri, or ppd-name attribute.", IPP_STATUS_ERROR_BAD_REQUEST, con->http->hostname);
        }

	cupsdLogMessage(CUPSD_LOG_DEBUG, "Request attributes follow...");

	for (attr = con->request->attrs; attr; attr = attr->next)
	  cupsdLogMessage(CUPSD_LOG_DEBUG,
	        	  "attr \"%s\": group_tag = %x, value_tag = %x",
	        	  attr->name ? attr->name : "(null)", attr->group_tag,
			  attr->value_tag);

	cupsdLogMessage(CUPSD_LOG_DEBUG, "End of attributes...");

	send_ipp_status(con, IPP_BAD_REQUEST,
	                _("Missing required attributes."));
      }
      else
      {
       /*
	* OK, all the checks pass so far; validate "requesting-user-name"
	* attribute value...
	*/

        if ((username = ippFindAttribute(con->request, "requesting-user-name", IPP_TAG_ZERO)) != NULL)
        {
         /*
          * Validate "requesting-user-name"...
          */

          if (username->group_tag != IPP_TAG_OPERATION && StrictConformance)
          {
	    cupsdAddEvent(CUPSD_EVENT_SERVER_AUDIT, NULL, NULL, "%04X %s \"requesting-user-name\" attribute in wrong group.", IPP_STATUS_ERROR_BAD_REQUEST, con->http->hostname);
	    send_ipp_status(con, IPP_STATUS_ERROR_BAD_REQUEST, _("\"requesting-user-name\" attribute in wrong group."));
	    valid = 0;
          }
          else if (username->value_tag != IPP_TAG_NAME && username->value_tag != IPP_TAG_NAMELANG)
          {
	    cupsdAddEvent(CUPSD_EVENT_SERVER_AUDIT, NULL, NULL, "%04X %s \"requesting-user-name\" attribute with wrong syntax.", IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES, con->http->hostname);
	    send_ipp_status(con, IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES, _("\"requesting-user-name\" attribute with wrong syntax."));
	    if ((attr = ippCopyAttribute(con->response, username, 0)) != NULL)
	      attr->group_tag = IPP_TAG_UNSUPPORTED_GROUP;
	    valid = 0;
          }
          else if (!ippValidateAttribute(username))
          {
	    cupsdAddEvent(CUPSD_EVENT_SERVER_AUDIT, NULL, NULL, "%04X %s \"requesting-user-name\" attribute with bad value.", IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES, con->http->hostname);

            if (StrictConformance)
            {
             /*
              * Throw an error...
              */

	      send_ipp_status(con, IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES, _("\"requesting-user-name\" attribute with wrong syntax."));
              if ((attr = ippCopyAttribute(con->response, username, 0)) != NULL)
                attr->group_tag = IPP_TAG_UNSUPPORTED_GROUP;
	      valid = 0;
	    }
	    else
	    {
	     /*
	      * Map bad "requesting-user-name" to 'anonymous'...
	      */

              ippSetString(con->request, &username, 0, "anonymous");
	    }
          }
          else if (!strcmp(username->values[0].string.text, "root") && _cups_strcasecmp(con->http->hostname, "localhost") && strcmp(con->username, "root"))
	  {
	   /*
	    * Remote unauthenticated user masquerading as local root...
	    */

            ippSetString(con->request, &username, 0, RemoteRoot);
	  }
	}

        if ((attr = ippFindAttribute(con->request, "notify-subscription-id", IPP_TAG_INTEGER)) != NULL)
	  sub_id = attr->values[0].integer;
	else
	  sub_id = 0;

        if (valid)
        {
	 /*
	  * Try processing the operation...
	  */

	  if (uri)
	    cupsdLogMessage(CUPSD_LOG_DEBUG, "%s %s", ippOpString(con->request->request.op.operation_id), uri->values[0].string.text);
	  else
	    cupsdLogMessage(CUPSD_LOG_DEBUG, "%s", ippOpString(con->request->request.op.operation_id));

	  switch (con->request->request.op.operation_id)
	  {
	    case IPP_OP_PRINT_JOB :
		print_job(con, uri);
		break;

	    case IPP_OP_VALIDATE_JOB :
		validate_job(con, uri);
		break;

	    case IPP_OP_CREATE_JOB :
		create_job(con, uri);
		break;

	    case IPP_OP_SEND_DOCUMENT :
		send_document(con, uri);
		break;

	    case IPP_OP_CANCEL_JOB :
		cancel_job(con, uri);
		break;

	    case IPP_OP_GET_JOB_ATTRIBUTES :
		get_job_attrs(con, uri);
		break;

	    case IPP_OP_GET_JOBS :
		get_jobs(con, uri);
		break;

	    case IPP_OP_GET_PRINTER_ATTRIBUTES :
		get_printer_attrs(con, uri);
		break;

	    case IPP_OP_GET_PRINTER_SUPPORTED_VALUES :
		get_printer_supported(con, uri);
		break;

	    case IPP_OP_HOLD_JOB :
		hold_job(con, uri);
		break;

	    case IPP_OP_RELEASE_JOB :
		release_job(con, uri);
		break;

	    case IPP_OP_RESTART_JOB :
		restart_job(con, uri);
		break;

	    case IPP_OP_PAUSE_PRINTER :
		stop_printer(con, uri);
		break;

	    case IPP_OP_RESUME_PRINTER :
		start_printer(con, uri);
		break;

	    case IPP_OP_PURGE_JOBS :
	    case IPP_OP_CANCEL_JOBS :
	    case IPP_OP_CANCEL_MY_JOBS :
		cancel_all_jobs(con, uri);
		break;

	    case IPP_OP_SET_JOB_ATTRIBUTES :
		set_job_attrs(con, uri);
		break;

	    case IPP_OP_SET_PRINTER_ATTRIBUTES :
		set_printer_attrs(con, uri);
		break;

	    case IPP_OP_HOLD_NEW_JOBS :
		hold_new_jobs(con, uri);
		break;

	    case IPP_OP_RELEASE_HELD_NEW_JOBS :
		release_held_new_jobs(con, uri);
		break;

	    case IPP_OP_CLOSE_JOB :
		close_job(con, uri);
		break;

	    case IPP_OP_CUPS_GET_DEFAULT :
		get_default(con);
		break;

	    case IPP_OP_CUPS_GET_PRINTERS :
		get_printers(con, 0);
		break;

	    case IPP_OP_CUPS_GET_CLASSES :
		get_printers(con, CUPS_PRINTER_CLASS);
		break;

	    case IPP_OP_CUPS_ADD_MODIFY_PRINTER :
		add_printer(con, uri);
		break;

	    case IPP_OP_CUPS_DELETE_PRINTER :
		delete_printer(con, uri);
		break;

	    case IPP_OP_CUPS_ADD_MODIFY_CLASS :
		add_class(con, uri);
		break;

	    case IPP_OP_CUPS_DELETE_CLASS :
		delete_printer(con, uri);
		break;

	    case IPP_OP_CUPS_ACCEPT_JOBS :
	    case IPP_OP_ENABLE_PRINTER :
		accept_jobs(con, uri);
		break;

	    case IPP_OP_CUPS_REJECT_JOBS :
	    case IPP_OP_DISABLE_PRINTER :
		reject_jobs(con, uri);
		break;

	    case IPP_OP_CUPS_SET_DEFAULT :
		set_default(con, uri);
		break;

	    case IPP_OP_CUPS_GET_DEVICES :
		get_devices(con);
		break;

	    case IPP_OP_CUPS_GET_DOCUMENT :
		get_document(con, uri);
		break;

	    case IPP_OP_CUPS_GET_PPD :
		get_ppd(con, uri);
		break;

	    case IPP_OP_CUPS_GET_PPDS :
		get_ppds(con);
		break;

	    case IPP_OP_CUPS_MOVE_JOB :
		move_job(con, uri);
		break;

	    case IPP_OP_CUPS_AUTHENTICATE_JOB :
		authenticate_job(con, uri);
		break;

	    case IPP_OP_CREATE_PRINTER_SUBSCRIPTIONS :
	    case IPP_OP_CREATE_JOB_SUBSCRIPTIONS :
		create_subscriptions(con, uri);
		break;

	    case IPP_OP_GET_SUBSCRIPTION_ATTRIBUTES :
		get_subscription_attrs(con, sub_id);
		break;

	    case IPP_OP_GET_SUBSCRIPTIONS :
		get_subscriptions(con, uri);
		break;

	    case IPP_OP_RENEW_SUBSCRIPTION :
		renew_subscription(con, sub_id);
		break;

	    case IPP_OP_CANCEL_SUBSCRIPTION :
		cancel_subscription(con, sub_id);
		break;

	    case IPP_OP_GET_NOTIFICATIONS :
		get_notifications(con);
		break;

	    case IPP_OP_CUPS_CREATE_LOCAL_PRINTER :
		create_local_printer(con);
		break;

	    default :
		cupsdAddEvent(CUPSD_EVENT_SERVER_AUDIT, NULL, NULL, "%04X %s Operation %04X (%s) not supported.", IPP_STATUS_ERROR_OPERATION_NOT_SUPPORTED, con->http->hostname, con->request->request.op.operation_id, ippOpString(con->request->request.op.operation_id));

		send_ipp_status(con, IPP_STATUS_ERROR_OPERATION_NOT_SUPPORTED, _("%s not supported."), ippOpString(con->request->request.op.operation_id));
		break;
	  }
	}
      }
    }
  }

  if (!con->bg_pending && con->response)
  {
   /*
    * Sending data from the scheduler...
    */

    return (send_response(con));
  }
  else
  {
   /*
    * Sending data from a subprocess like cups-deviced; tell the caller
    * everything is A-OK so far...
    */

    return (1);
  }
}


/*
 * 'cupsdTimeoutJob()' - Timeout a job waiting on job files.
 */

int					/* O - 0 on success, -1 on error */
cupsdTimeoutJob(cupsd_job_t *job)	/* I - Job to timeout */
{
  cupsd_printer_t	*printer;	/* Destination printer or class */
  ipp_attribute_t	*attr;		/* job-sheets attribute */
  int			kbytes;		/* Kilobytes in banner */


  job->pending_timeout = 0;

 /*
  * See if we need to add the ending sheet...
  */

  if (!cupsdLoadJob(job))
    return (-1);

  printer = cupsdFindDest(job->dest);
  attr    = ippFindAttribute(job->attrs, "job-sheets", IPP_TAG_NAME);

  if (printer && !(printer->type & CUPS_PRINTER_REMOTE) &&
      attr && attr->num_values > 1)
  {
   /*
    * Yes...
    */

    cupsdLogJob(job, CUPSD_LOG_INFO, "Adding end banner page \"%s\".",
                attr->values[1].string.text);

    if ((kbytes = copy_banner(NULL, job, attr->values[1].string.text)) < 0)
      return (-1);

    cupsdUpdateQuota(printer, job->username, 0, kbytes);
  }

  return (0);
}


/*
 * 'accept_jobs()' - Accept print jobs to a printer.
 */

static void
accept_jobs(cupsd_client_t  *con,	/* I - Client connection */
            ipp_attribute_t *uri)	/* I - Printer or class URI */
{
  http_status_t	status;			/* Policy status */
  cups_ptype_t	dtype;			/* Destination type (printer/class) */
  cupsd_printer_t *printer;		/* Printer data */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "accept_jobs(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * Is the destination valid?
  */

  if (!cupsdValidateDest(uri->values[0].string.text, &dtype, &printer))
  {
   /*
    * Bad URI...
    */

    send_ipp_status(con, IPP_NOT_FOUND,
                    _("The printer or class does not exist."));
    return;
  }

 /*
  * Check policy...
  */

  if ((status = cupsdCheckPolicy(printer->op_policy_ptr, con, NULL)) != HTTP_OK)
  {
    send_http_error(con, status, printer);
    return;
  }

 /*
  * Accept jobs sent to the printer...
  */

  printer->accepting        = 1;
  printer->state_message[0] = '\0';

  cupsdAddEvent(CUPSD_EVENT_PRINTER_STATE, printer, NULL,
                "Now accepting jobs.");

  if (dtype & CUPS_PRINTER_CLASS)
  {
    cupsdMarkDirty(CUPSD_DIRTY_CLASSES);

    cupsdLogMessage(CUPSD_LOG_INFO, "Class \"%s\" now accepting jobs (\"%s\").",
                    printer->name, get_username(con));
  }
  else
  {
    cupsdMarkDirty(CUPSD_DIRTY_PRINTERS);

    cupsdLogMessage(CUPSD_LOG_INFO,
                    "Printer \"%s\" now accepting jobs (\"%s\").",
                    printer->name, get_username(con));
  }

 /*
  * Everything was ok, so return OK status...
  */

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'add_class()' - Add a class to the system.
 */

static void
add_class(cupsd_client_t  *con,		/* I - Client connection */
          ipp_attribute_t *uri)		/* I - URI of class */
{
  http_status_t	status;			/* Policy status */
  int		i;			/* Looping var */
  char		scheme[HTTP_MAX_URI],	/* Method portion of URI */
		username[HTTP_MAX_URI],	/* Username portion of URI */
		host[HTTP_MAX_URI],	/* Host portion of URI */
		resource[HTTP_MAX_URI];	/* Resource portion of URI */
  int		port;			/* Port portion of URI */
  cupsd_printer_t *pclass,		/* Class */
		*member;		/* Member printer/class */
  cups_ptype_t	dtype;			/* Destination type */
  ipp_attribute_t *attr;		/* Printer attribute */
  int		modify;			/* Non-zero if we just modified */
  int		need_restart_job;	/* Need to restart job? */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "add_class(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * Do we have a valid URI?
  */

  httpSeparateURI(HTTP_URI_CODING_ALL, uri->values[0].string.text, scheme,
                  sizeof(scheme), username, sizeof(username), host,
		  sizeof(host), &port, resource, sizeof(resource));


  if (strncmp(resource, "/classes/", 9) || strlen(resource) == 9)
  {
   /*
    * No, return an error...
    */

    send_ipp_status(con, IPP_BAD_REQUEST,
                    _("The printer-uri must be of the form "
		      "\"ipp://HOSTNAME/classes/CLASSNAME\"."));
    return;
  }

 /*
  * Do we have a valid printer name?
  */

  if (!validate_name(resource + 9))
  {
   /*
    * No, return an error...
    */

    send_ipp_status(con, IPP_BAD_REQUEST,
                    _("The printer-uri \"%s\" contains invalid characters."),
		    uri->values[0].string.text);
    return;
  }

 /*
  * See if the class already exists; if not, create a new class...
  */

  if ((pclass = cupsdFindClass(resource + 9)) == NULL)
  {
   /*
    * Class doesn't exist; see if we have a printer of the same name...
    */

    if (cupsdFindPrinter(resource + 9))
    {
     /*
      * Yes, return an error...
      */

      send_ipp_status(con, IPP_NOT_POSSIBLE,
                      _("A printer named \"%s\" already exists."),
		      resource + 9);
      return;
    }

   /*
    * No, check the default policy and then add the class...
    */

    if ((status = cupsdCheckPolicy(DefaultPolicyPtr, con, NULL)) != HTTP_OK)
    {
      send_http_error(con, status, NULL);
      return;
    }

    pclass = cupsdAddClass(resource + 9);
    modify = 0;

    pclass->printer_id = NextPrinterId ++;
  }
  else if ((status = cupsdCheckPolicy(pclass->op_policy_ptr, con,
                                      NULL)) != HTTP_OK)
  {
    send_http_error(con, status, pclass);
    return;
  }
  else
    modify = 1;

 /*
  * Look for attributes and copy them over as needed...
  */

  need_restart_job = 0;

  if ((attr = ippFindAttribute(con->request, "printer-location", IPP_TAG_TEXT)) != NULL)
    cupsdSetString(&pclass->location, attr->values[0].string.text);

  if ((attr = ippFindAttribute(con->request, "printer-geo-location", IPP_TAG_URI)) != NULL && !strncmp(attr->values[0].string.text, "geo:", 4))
    cupsdSetString(&pclass->geo_location, attr->values[0].string.text);

  if ((attr = ippFindAttribute(con->request, "printer-organization", IPP_TAG_TEXT)) != NULL)
    cupsdSetString(&pclass->organization, attr->values[0].string.text);

  if ((attr = ippFindAttribute(con->request, "printer-organizational-unit", IPP_TAG_TEXT)) != NULL)
    cupsdSetString(&pclass->organizational_unit, attr->values[0].string.text);

  if ((attr = ippFindAttribute(con->request, "printer-info",
                               IPP_TAG_TEXT)) != NULL)
    cupsdSetString(&pclass->info, attr->values[0].string.text);

  if ((attr = ippFindAttribute(con->request, "printer-is-accepting-jobs",
                               IPP_TAG_BOOLEAN)) != NULL &&
      attr->values[0].boolean != pclass->accepting)
  {
    cupsdLogMessage(CUPSD_LOG_INFO,
                    "Setting %s printer-is-accepting-jobs to %d (was %d.)",
                    pclass->name, attr->values[0].boolean, pclass->accepting);

    pclass->accepting = attr->values[0].boolean;

    cupsdAddEvent(CUPSD_EVENT_PRINTER_STATE, pclass, NULL, "%s accepting jobs.",
		  pclass->accepting ? "Now" : "No longer");
  }

  if ((attr = ippFindAttribute(con->request, "printer-is-shared", IPP_TAG_BOOLEAN)) != NULL)
  {
    if (pclass->type & CUPS_PRINTER_REMOTE)
    {
     /*
      * Cannot re-share remote printers.
      */

      send_ipp_status(con, IPP_BAD_REQUEST, _("Cannot change printer-is-shared for remote queues."));
      if (!modify)
	cupsdDeletePrinter(pclass, 0);

      return;
    }

    if (pclass->shared && !ippGetBoolean(attr, 0))
      cupsdDeregisterPrinter(pclass, 1);

    cupsdLogMessage(CUPSD_LOG_INFO,
                    "Setting %s printer-is-shared to %d (was %d.)",
                    pclass->name, attr->values[0].boolean, pclass->shared);

    pclass->shared = ippGetBoolean(attr, 0);
  }

  if ((attr = ippFindAttribute(con->request, "printer-state",
                               IPP_TAG_ENUM)) != NULL)
  {
    if (attr->values[0].integer != IPP_PRINTER_IDLE &&
        attr->values[0].integer != IPP_PRINTER_STOPPED)
    {
      send_ipp_status(con, IPP_BAD_REQUEST,
                      _("Attempt to set %s printer-state to bad value %d."),
                      pclass->name, attr->values[0].integer);
      if (!modify)
	cupsdDeletePrinter(pclass, 0);

      return;
    }

    cupsdLogMessage(CUPSD_LOG_INFO, "Setting %s printer-state to %d (was %d.)",
                    pclass->name, attr->values[0].integer, pclass->state);

    if (attr->values[0].integer == IPP_PRINTER_STOPPED)
      cupsdStopPrinter(pclass, 0);
    else
    {
      cupsdSetPrinterState(pclass, (ipp_pstate_t)(attr->values[0].integer), 0);
      need_restart_job = 1;
    }
  }
  if ((attr = ippFindAttribute(con->request, "printer-state-message",
                               IPP_TAG_TEXT)) != NULL)
  {
    strlcpy(pclass->state_message, attr->values[0].string.text,
            sizeof(pclass->state_message));

    cupsdAddEvent(CUPSD_EVENT_PRINTER_STATE, pclass, NULL, "%s",
                  pclass->state_message);
  }
  if ((attr = ippFindAttribute(con->request, "member-uris",
                               IPP_TAG_URI)) != NULL)
  {
   /*
    * Clear the printer array as needed...
    */

    need_restart_job = 1;

    if (pclass->num_printers > 0)
    {
      free(pclass->printers);
      pclass->num_printers = 0;
    }

   /*
    * Add each printer or class that is listed...
    */

    for (i = 0; i < attr->num_values; i ++)
    {
     /*
      * Search for the printer or class URI...
      */

      if (!cupsdValidateDest(attr->values[i].string.text, &dtype, &member))
      {
       /*
	* Bad URI...
	*/

	send_ipp_status(con, IPP_NOT_FOUND,
                	_("The printer or class does not exist."));
	if (!modify)
	  cupsdDeletePrinter(pclass, 0);

	return;
      }
      else if (dtype & CUPS_PRINTER_CLASS)
      {
        send_ipp_status(con, IPP_BAD_REQUEST,
			_("Nested classes are not allowed."));
	if (!modify)
	  cupsdDeletePrinter(pclass, 0);

        return;
      }

     /*
      * Add it to the class...
      */

      cupsdAddPrinterToClass(pclass, member);
    }
  }

  if (!set_printer_defaults(con, pclass))
  {
    if (!modify)
      cupsdDeletePrinter(pclass, 0);

    return;
  }

  if ((attr = ippFindAttribute(con->request, "auth-info-required",
                               IPP_TAG_KEYWORD)) != NULL)
    cupsdSetAuthInfoRequired(pclass, NULL, attr);

  pclass->config_time = time(NULL);

 /*
  * Update the printer class attributes and return...
  */

  cupsdSetPrinterAttrs(pclass);
  cupsdMarkDirty(CUPSD_DIRTY_CLASSES);

  if (need_restart_job && pclass->job)
  {
   /*
    * Reset the current job to a "pending" status...
    */

    cupsdSetJobState(pclass->job, IPP_JOB_PENDING, CUPSD_JOB_FORCE,
                     "Job restarted because the class was modified.");
  }

  cupsdMarkDirty(CUPSD_DIRTY_PRINTCAP);

  if (modify)
  {
    cupsdAddEvent(CUPSD_EVENT_PRINTER_MODIFIED,
		  pclass, NULL, "Class \"%s\" modified by \"%s\".",
		  pclass->name, get_username(con));

    cupsdLogMessage(CUPSD_LOG_INFO, "Class \"%s\" modified by \"%s\".",
                    pclass->name, get_username(con));
  }
  else
  {
    cupsdAddEvent(CUPSD_EVENT_PRINTER_ADDED,
		  pclass, NULL, "New class \"%s\" added by \"%s\".",
		  pclass->name, get_username(con));

    cupsdLogMessage(CUPSD_LOG_INFO, "New class \"%s\" added by \"%s\".",
                    pclass->name, get_username(con));
  }

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'add_file()' - Add a file to a job.
 */

static int				/* O - 0 on success, -1 on error */
add_file(cupsd_client_t *con,		/* I - Connection to client */
         cupsd_job_t    *job,		/* I - Job to add to */
         mime_type_t    *filetype,	/* I - Type of file */
	 int            compression)	/* I - Compression */
{
  mime_type_t	**filetypes;		/* New filetypes array... */
  int		*compressions;		/* New compressions array... */


  cupsdLogMessage(CUPSD_LOG_DEBUG2,
        	  "add_file(con=%p[%d], job=%d, filetype=%s/%s, "
		  "compression=%d)", (void *)con, con ? con->number : -1, job->id,
		  filetype->super, filetype->type, compression);

 /*
  * Add the file to the job...
  */

  if (job->num_files == 0)
  {
    compressions = (int *)malloc(sizeof(int));
    filetypes    = (mime_type_t **)malloc(sizeof(mime_type_t *));
  }
  else
  {
    compressions = (int *)realloc(job->compressions,
                                  (size_t)(job->num_files + 1) * sizeof(int));
    filetypes    = (mime_type_t **)realloc(job->filetypes,
                                           (size_t)(job->num_files + 1) *
					   sizeof(mime_type_t *));
  }

  if (compressions)
    job->compressions = compressions;

  if (filetypes)
    job->filetypes = filetypes;

  if (!compressions || !filetypes)
  {
    cupsdSetJobState(job, IPP_JOB_ABORTED, CUPSD_JOB_PURGE,
                     "Job aborted because the scheduler ran out of memory.");

    if (con)
      send_ipp_status(con, IPP_INTERNAL_ERROR,
		      _("Unable to allocate memory for file types."));

    return (-1);
  }

  job->compressions[job->num_files] = compression;
  job->filetypes[job->num_files]    = filetype;

  job->num_files ++;

  job->dirty = 1;
  cupsdMarkDirty(CUPSD_DIRTY_JOBS);

  return (0);
}


/*
 * 'add_job()' - Add a job to a print queue.
 */

static cupsd_job_t *			/* O - Job object */
add_job(cupsd_client_t  *con,		/* I - Client connection */
	cupsd_printer_t *printer,	/* I - Destination printer */
	mime_type_t     *filetype)	/* I - First print file type, if any */
{
  http_status_t	status;			/* Policy status */
  ipp_attribute_t *attr,		/* Current attribute */
		*auth_info;		/* auth-info attribute */
  const char	*mandatory;		/* Current mandatory job attribute */
  const char	*val;			/* Default option value */
  int		priority;		/* Job priority */
  cupsd_job_t	*job;			/* Current job */
  char		job_uri[HTTP_MAX_URI];	/* Job URI */
  int		kbytes;			/* Size of print file */
  int		i;			/* Looping var */
  int		lowerpagerange;		/* Page range bound */
  int		exact;			/* Did we have an exact match? */
  ipp_attribute_t *media_col,		/* media-col attribute */
		*media_margin;		/* media-*-margin attribute */
  ipp_t		*unsup_col;		/* media-col in unsupported response */
  static const char * const readonly[] =/* List of read-only attributes */
  {
    "date-time-at-completed",
    "date-time-at-creation",
    "date-time-at-processing",
    "job-detailed-status-messages",
    "job-document-access-errors",
    "job-id",
    "job-impressions-completed",
    "job-k-octets-completed",
    "job-media-sheets-completed",
    "job-pages-completed",
    "job-printer-up-time",
    "job-printer-uri",
    "job-state",
    "job-state-message",
    "job-state-reasons",
    "job-uri",
    "number-of-documents",
    "number-of-intervening-jobs",
    "output-device-assigned",
    "time-at-completed",
    "time-at-creation",
    "time-at-processing"
  };


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "add_job(%p[%d], %p(%s), %p(%s/%s))",
                  (void *)con, con->number, (void *)printer, printer->name,
		  (void *)filetype, filetype ? filetype->super : "none",
		  filetype ? filetype->type : "none");

 /*
  * Check remote printing to non-shared printer...
  */

  if (!printer->shared &&
      _cups_strcasecmp(con->http->hostname, "localhost") &&
      _cups_strcasecmp(con->http->hostname, ServerName))
  {
    send_ipp_status(con, IPP_NOT_AUTHORIZED,
                    _("The printer or class is not shared."));
    return (NULL);
  }

 /*
  * Check policy...
  */

  auth_info = ippFindAttribute(con->request, "auth-info", IPP_TAG_TEXT);

  if ((status = cupsdCheckPolicy(printer->op_policy_ptr, con, NULL)) != HTTP_OK)
  {
    send_http_error(con, status, printer);
    return (NULL);
  }
  else if (printer->num_auth_info_required == 1 &&
           !strcmp(printer->auth_info_required[0], "negotiate") &&
           !con->username[0])
  {
    send_http_error(con, HTTP_UNAUTHORIZED, printer);
    return (NULL);
  }
#ifdef HAVE_TLS
  else if (auth_info && !con->http->tls &&
           !httpAddrLocalhost(con->http->hostaddr))
  {
   /*
    * Require encryption of auth-info over non-local connections...
    */

    send_http_error(con, HTTP_UPGRADE_REQUIRED, printer);
    return (NULL);
  }
#endif /* HAVE_TLS */

 /*
  * See if the printer is accepting jobs...
  */

  if (!printer->accepting)
  {
    send_ipp_status(con, IPP_NOT_ACCEPTING,
                    _("Destination \"%s\" is not accepting jobs."),
                    printer->name);
    return (NULL);
  }

 /*
  * Validate job template attributes; for now just document-format,
  * copies, job-sheets, number-up, page-ranges, mandatory attributes, and
  * media...
  */

  for (i = 0; i < (int)(sizeof(readonly) / sizeof(readonly[0])); i ++)
  {
    if ((attr = ippFindAttribute(con->request, readonly[i], IPP_TAG_ZERO)) != NULL)
    {
      ippDeleteAttribute(con->request, attr);

      if (StrictConformance)
      {
	send_ipp_status(con, IPP_BAD_REQUEST, _("The '%s' Job Status attribute cannot be supplied in a job creation request."), readonly[i]);
	return (NULL);
      }

      cupsdLogMessage(CUPSD_LOG_INFO, "Unexpected '%s' Job Status attribute in a job creation request.", readonly[i]);
    }
  }

  if (printer->pc)
  {
    for (mandatory = (char *)cupsArrayFirst(printer->pc->mandatory);
	 mandatory;
	 mandatory = (char *)cupsArrayNext(printer->pc->mandatory))
    {
      if (!ippFindAttribute(con->request, mandatory, IPP_TAG_ZERO))
      {
       /*
	* Missing a required attribute...
	*/

	send_ipp_status(con, IPP_CONFLICT,
			_("The \"%s\" attribute is required for print jobs."),
			mandatory);
	return (NULL);
      }
    }
  }

  if (filetype && printer->filetypes &&
      !cupsArrayFind(printer->filetypes, filetype))
  {
    char	mimetype[MIME_MAX_SUPER + MIME_MAX_TYPE + 2];
					/* MIME media type string */


    snprintf(mimetype, sizeof(mimetype), "%s/%s", filetype->super,
             filetype->type);

    send_ipp_status(con, IPP_DOCUMENT_FORMAT,
                    _("Unsupported format \"%s\"."), mimetype);

    ippAddString(con->response, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_MIMETYPE,
                 "document-format", NULL, mimetype);

    return (NULL);
  }

  if ((attr = ippFindAttribute(con->request, "copies",
                               IPP_TAG_INTEGER)) != NULL)
  {
    if (attr->values[0].integer < 1 || attr->values[0].integer > MaxCopies)
    {
      send_ipp_status(con, IPP_ATTRIBUTES, _("Bad copies value %d."),
                      attr->values[0].integer);
      ippAddInteger(con->response, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_INTEGER,
	            "copies", attr->values[0].integer);
      return (NULL);
    }
  }

  if ((attr = ippFindAttribute(con->request, "job-sheets",
                               IPP_TAG_ZERO)) != NULL)
  {
    if (attr->value_tag != IPP_TAG_KEYWORD &&
        attr->value_tag != IPP_TAG_NAME)
    {
      send_ipp_status(con, IPP_BAD_REQUEST, _("Bad job-sheets value type."));
      return (NULL);
    }

    if (attr->num_values > 2)
    {
      send_ipp_status(con, IPP_BAD_REQUEST,
                      _("Too many job-sheets values (%d > 2)."),
		      attr->num_values);
      return (NULL);
    }

    for (i = 0; i < attr->num_values; i ++)
      if (strcmp(attr->values[i].string.text, "none") &&
          !cupsdFindBanner(attr->values[i].string.text))
      {
	send_ipp_status(con, IPP_BAD_REQUEST, _("Bad job-sheets value \"%s\"."),
			attr->values[i].string.text);
	return (NULL);
      }
  }

  if ((attr = ippFindAttribute(con->request, "number-up",
                               IPP_TAG_INTEGER)) != NULL)
  {
    if (attr->values[0].integer != 1 &&
        attr->values[0].integer != 2 &&
        attr->values[0].integer != 4 &&
        attr->values[0].integer != 6 &&
        attr->values[0].integer != 9 &&
        attr->values[0].integer != 16)
    {
      send_ipp_status(con, IPP_ATTRIBUTES, _("Bad number-up value %d."),
                      attr->values[0].integer);
      ippAddInteger(con->response, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_INTEGER,
	            "number-up", attr->values[0].integer);
      return (NULL);
    }
  }

  if ((attr = ippFindAttribute(con->request, "page-ranges",
                               IPP_TAG_RANGE)) != NULL)
  {
    for (i = 0, lowerpagerange = 1; i < attr->num_values; i ++)
    {
      if (attr->values[i].range.lower < lowerpagerange ||
	  attr->values[i].range.lower > attr->values[i].range.upper)
      {
	send_ipp_status(con, IPP_BAD_REQUEST,
	                _("Bad page-ranges values %d-%d."),
	                attr->values[i].range.lower,
			attr->values[i].range.upper);
	return (NULL);
      }

      lowerpagerange = attr->values[i].range.upper + 1;
    }
  }

 /*
  * Do media selection as needed...
  */

  if (!ippFindAttribute(con->request, "PageRegion", IPP_TAG_ZERO) &&
      !ippFindAttribute(con->request, "PageSize", IPP_TAG_ZERO) &&
      _ppdCacheGetPageSize(printer->pc, con->request, NULL, &exact))
  {
    if (!exact &&
        (media_col = ippFindAttribute(con->request, "media-col",
	                              IPP_TAG_BEGIN_COLLECTION)) != NULL)
    {
      send_ipp_status(con, IPP_OK_SUBST, _("Unsupported margins."));

      unsup_col = ippNew();
      if ((media_margin = ippFindAttribute(media_col->values[0].collection,
                                           "media-bottom-margin",
					   IPP_TAG_INTEGER)) != NULL)
        ippAddInteger(unsup_col, IPP_TAG_ZERO, IPP_TAG_INTEGER,
	              "media-bottom-margin", media_margin->values[0].integer);

      if ((media_margin = ippFindAttribute(media_col->values[0].collection,
                                           "media-left-margin",
					   IPP_TAG_INTEGER)) != NULL)
        ippAddInteger(unsup_col, IPP_TAG_ZERO, IPP_TAG_INTEGER,
	              "media-left-margin", media_margin->values[0].integer);

      if ((media_margin = ippFindAttribute(media_col->values[0].collection,
                                           "media-right-margin",
					   IPP_TAG_INTEGER)) != NULL)
        ippAddInteger(unsup_col, IPP_TAG_ZERO, IPP_TAG_INTEGER,
	              "media-right-margin", media_margin->values[0].integer);

      if ((media_margin = ippFindAttribute(media_col->values[0].collection,
                                           "media-top-margin",
					   IPP_TAG_INTEGER)) != NULL)
        ippAddInteger(unsup_col, IPP_TAG_ZERO, IPP_TAG_INTEGER,
	              "media-top-margin", media_margin->values[0].integer);

      ippAddCollection(con->response, IPP_TAG_UNSUPPORTED_GROUP, "media-col",
                       unsup_col);
      ippDelete(unsup_col);
    }
  }

 /*
  * Make sure we aren't over our limit...
  */

  if (MaxJobs && cupsArrayCount(Jobs) >= MaxJobs)
    cupsdCleanJobs();

  if (MaxJobs && cupsArrayCount(Jobs) >= MaxJobs)
  {
    send_ipp_status(con, IPP_NOT_POSSIBLE, _("Too many active jobs."));
    return (NULL);
  }

  if ((i = check_quotas(con, printer)) < 0)
  {
    send_ipp_status(con, IPP_NOT_POSSIBLE, _("Quota limit reached."));
    return (NULL);
  }
  else if (i == 0)
  {
    send_ipp_status(con, IPP_NOT_AUTHORIZED, _("Not allowed to print."));
    return (NULL);
  }

 /*
  * Create the job and set things up...
  */

  if ((attr = ippFindAttribute(con->request, "job-priority",
                               IPP_TAG_INTEGER)) != NULL)
    priority = attr->values[0].integer;
  else
  {
    if ((val = cupsGetOption("job-priority", printer->num_options,
                             printer->options)) != NULL)
      priority = atoi(val);
    else
      priority = 50;

    ippAddInteger(con->request, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-priority",
                  priority);
  }

  if ((attr = ippFindAttribute(con->request, "job-name", IPP_TAG_ZERO)) == NULL)
    ippAddString(con->request, IPP_TAG_JOB, IPP_TAG_NAME, "job-name", NULL, "Untitled");
  else if ((attr->value_tag != IPP_TAG_NAME &&
            attr->value_tag != IPP_TAG_NAMELANG) ||
           attr->num_values != 1)
  {
    send_ipp_status(con, IPP_ATTRIBUTES,
                    _("Bad job-name value: Wrong type or count."));
    if ((attr = ippCopyAttribute(con->response, attr, 0)) != NULL)
      attr->group_tag = IPP_TAG_UNSUPPORTED_GROUP;

    if (StrictConformance)
      return (NULL);

    /* Don't use invalid attribute */
    ippDeleteAttribute(con->request, attr);

    ippAddString(con->request, IPP_TAG_JOB, IPP_TAG_NAME, "job-name", NULL, "Untitled");
  }
  else if (!ippValidateAttribute(attr))
  {
    send_ipp_status(con, IPP_ATTRIBUTES, _("Bad job-name value: %s"),
                    cupsLastErrorString());

    if ((attr = ippCopyAttribute(con->response, attr, 0)) != NULL)
      attr->group_tag = IPP_TAG_UNSUPPORTED_GROUP;

    if (StrictConformance)
      return (NULL);

    /* Don't use invalid attribute */
    ippDeleteAttribute(con->request, attr);

    ippAddString(con->request, IPP_TAG_JOB, IPP_TAG_NAME, "job-name", NULL, "Untitled");
  }

  attr = ippFindAttribute(con->request, "requesting-user-name", IPP_TAG_NAME);

  if ((job = cupsdAddJob(priority, printer->name)) == NULL)
  {
    send_ipp_status(con, IPP_INTERNAL_ERROR,
                    _("Unable to add job for destination \"%s\"."),
		    printer->name);
    return (NULL);
  }

  job->dtype   = printer->type & (CUPS_PRINTER_CLASS | CUPS_PRINTER_REMOTE);
  job->attrs   = con->request;
  job->dirty   = 1;
  con->request = ippNewRequest(job->attrs->request.op.operation_id);

  cupsdMarkDirty(CUPSD_DIRTY_JOBS);

  add_job_uuid(job);
  apply_printer_defaults(printer, job);

  if (con->username[0])
  {
    cupsdSetString(&job->username, con->username);

    if (attr)
      ippSetString(job->attrs, &attr, 0, con->username);
  }
  else if (attr)
  {
    cupsdLogMessage(CUPSD_LOG_DEBUG,
                    "add_job: requesting-user-name=\"%s\"",
                    attr->values[0].string.text);

    cupsdSetString(&job->username, attr->values[0].string.text);
  }
  else
    cupsdSetString(&job->username, "anonymous");

  if (!attr)
    ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_NAME,
                 "job-originating-user-name", NULL, job->username);
  else
  {
    ippSetGroupTag(job->attrs, &attr, IPP_TAG_JOB);
    ippSetName(job->attrs, &attr, "job-originating-user-name");
  }

  if (con->username[0] || auth_info)
  {
    save_auth_info(con, job, auth_info);

   /*
    * Remove the auth-info attribute from the attribute data...
    */

    if (auth_info)
      ippDeleteAttribute(job->attrs, auth_info);
  }

  if ((attr = ippFindAttribute(con->request, "job-name", IPP_TAG_NAME)) != NULL)
    cupsdSetString(&(job->name), attr->values[0].string.text);

  if ((attr = ippFindAttribute(job->attrs, "job-originating-host-name",
                               IPP_TAG_ZERO)) != NULL)
  {
   /*
    * Request contains a job-originating-host-name attribute; validate it...
    */

    if (attr->value_tag != IPP_TAG_NAME ||
        attr->num_values != 1 ||
        strcmp(con->http->hostname, "localhost"))
    {
     /*
      * Can't override the value if we aren't connected via localhost.
      * Also, we can only have 1 value and it must be a name value.
      */

      ippDeleteAttribute(job->attrs, attr);
      ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_NAME, "job-originating-host-name", NULL, con->http->hostname);
    }
    else
      ippSetGroupTag(job->attrs, &attr, IPP_TAG_JOB);
  }
  else
  {
   /*
    * No job-originating-host-name attribute, so use the hostname from
    * the connection...
    */

    ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_NAME,
        	 "job-originating-host-name", NULL, con->http->hostname);
  }

  ippAddOutOfBand(job->attrs, IPP_TAG_JOB, IPP_TAG_NOVALUE, "date-time-at-completed");
  ippAddDate(job->attrs, IPP_TAG_JOB, "date-time-at-creation", ippTimeToDate(time(NULL)));
  ippAddOutOfBand(job->attrs, IPP_TAG_JOB, IPP_TAG_NOVALUE, "date-time-at-processing");
  ippAddOutOfBand(job->attrs, IPP_TAG_JOB, IPP_TAG_NOVALUE, "time-at-completed");
  ippAddInteger(job->attrs, IPP_TAG_JOB, IPP_TAG_INTEGER, "time-at-creation", time(NULL));
  ippAddOutOfBand(job->attrs, IPP_TAG_JOB, IPP_TAG_NOVALUE, "time-at-processing");

 /*
  * Add remaining job attributes...
  */

  ippAddInteger(job->attrs, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-id", job->id);
  job->state = ippAddInteger(job->attrs, IPP_TAG_JOB, IPP_TAG_ENUM,
                             "job-state", IPP_JOB_STOPPED);
  job->state_value = (ipp_jstate_t)job->state->values[0].integer;
  job->reasons = ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_KEYWORD,
                              "job-state-reasons", NULL, "job-incoming");
  job->impressions = ippAddInteger(job->attrs, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-impressions-completed", 0);
  job->sheets = ippAddInteger(job->attrs, IPP_TAG_JOB, IPP_TAG_INTEGER,
                              "job-media-sheets-completed", 0);
  ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_URI, "job-printer-uri", NULL,
               printer->uri);

  if ((attr = ippFindAttribute(job->attrs, "job-k-octets", IPP_TAG_INTEGER)) != NULL)
    attr->values[0].integer = 0;
  else
    ippAddInteger(job->attrs, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-k-octets", 0);

  if ((attr = ippFindAttribute(job->attrs, "job-hold-until",
                               IPP_TAG_KEYWORD)) == NULL)
    attr = ippFindAttribute(job->attrs, "job-hold-until", IPP_TAG_NAME);
  if (!attr)
  {
    if ((val = cupsGetOption("job-hold-until", printer->num_options,
                             printer->options)) == NULL)
      val = "no-hold";

    attr = ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_KEYWORD,
                        "job-hold-until", NULL, val);
  }

  if (printer->holding_new_jobs)
  {
   /*
    * Hold all new jobs on this printer...
    */

    if (attr && strcmp(attr->values[0].string.text, "no-hold"))
      cupsdSetJobHoldUntil(job, ippGetString(attr, 0, NULL), 0);
    else
      cupsdSetJobHoldUntil(job, "indefinite", 0);

    job->state->values[0].integer = IPP_JOB_HELD;
    job->state_value              = IPP_JOB_HELD;

    ippSetString(job->attrs, &job->reasons, 0, "job-held-on-create");
  }
  else if (attr && strcmp(attr->values[0].string.text, "no-hold"))
  {
   /*
    * Hold job until specified time...
    */

    cupsdSetJobHoldUntil(job, attr->values[0].string.text, 0);

    job->state->values[0].integer = IPP_JOB_HELD;
    job->state_value              = IPP_JOB_HELD;

    ippSetString(job->attrs, &job->reasons, 0, "job-hold-until-specified");
  }
  else if (job->attrs->request.op.operation_id == IPP_CREATE_JOB)
  {
    job->hold_until               = time(NULL) + MultipleOperationTimeout;
    job->state->values[0].integer = IPP_JOB_HELD;
    job->state_value              = IPP_JOB_HELD;
  }
  else
  {
    job->state->values[0].integer = IPP_JOB_PENDING;
    job->state_value              = IPP_JOB_PENDING;

    ippSetString(job->attrs, &job->reasons, 0, "none");
  }

  if (!(printer->type & CUPS_PRINTER_REMOTE) || Classification)
  {
   /*
    * Add job sheets options...
    */

    if ((attr = ippFindAttribute(job->attrs, "job-sheets",
                                 IPP_TAG_ZERO)) == NULL)
    {
      cupsdLogMessage(CUPSD_LOG_DEBUG,
                      "Adding default job-sheets values \"%s,%s\"...",
                      printer->job_sheets[0], printer->job_sheets[1]);

      attr = ippAddStrings(job->attrs, IPP_TAG_JOB, IPP_TAG_NAME, "job-sheets",
                           2, NULL, NULL);
      ippSetString(job->attrs, &attr, 0, printer->job_sheets[0]);
      ippSetString(job->attrs, &attr, 1, printer->job_sheets[1]);
    }

    job->job_sheets = attr;

   /*
    * Enforce classification level if set...
    */

    if (Classification)
    {
      cupsdLogMessage(CUPSD_LOG_INFO,
                      "Classification=\"%s\", ClassifyOverride=%d",
                      Classification ? Classification : "(null)",
		      ClassifyOverride);

      if (ClassifyOverride)
      {
        if (!strcmp(attr->values[0].string.text, "none") &&
	    (attr->num_values == 1 ||
	     !strcmp(attr->values[1].string.text, "none")))
        {
	 /*
          * Force the leading banner to have the classification on it...
	  */

          ippSetString(job->attrs, &attr, 0, Classification);

	  cupsdLogJob(job, CUPSD_LOG_NOTICE, "CLASSIFICATION FORCED "
	                		     "job-sheets=\"%s,none\", "
					     "job-originating-user-name=\"%s\"",
	              Classification, job->username);
	}
	else if (attr->num_values == 2 &&
	         strcmp(attr->values[0].string.text,
		        attr->values[1].string.text) &&
		 strcmp(attr->values[0].string.text, "none") &&
		 strcmp(attr->values[1].string.text, "none"))
        {
	 /*
	  * Can't put two different security markings on the same document!
	  */

          ippSetString(job->attrs, &attr, 1, attr->values[0].string.text);

	  cupsdLogJob(job, CUPSD_LOG_NOTICE, "CLASSIFICATION FORCED "
	                		     "job-sheets=\"%s,%s\", "
					     "job-originating-user-name=\"%s\"",
		      attr->values[0].string.text,
		      attr->values[1].string.text, job->username);
	}
	else if (strcmp(attr->values[0].string.text, Classification) &&
	         strcmp(attr->values[0].string.text, "none") &&
		 (attr->num_values == 1 ||
	          (strcmp(attr->values[1].string.text, Classification) &&
	           strcmp(attr->values[1].string.text, "none"))))
        {
	  if (attr->num_values == 1)
            cupsdLogJob(job, CUPSD_LOG_NOTICE,
			"CLASSIFICATION OVERRIDDEN "
			"job-sheets=\"%s\", "
			"job-originating-user-name=\"%s\"",
	                attr->values[0].string.text, job->username);
          else
            cupsdLogJob(job, CUPSD_LOG_NOTICE,
			"CLASSIFICATION OVERRIDDEN "
			"job-sheets=\"%s,%s\",fffff "
			"job-originating-user-name=\"%s\"",
			attr->values[0].string.text,
			attr->values[1].string.text, job->username);
        }
      }
      else if (strcmp(attr->values[0].string.text, Classification) &&
               (attr->num_values == 1 ||
	       strcmp(attr->values[1].string.text, Classification)))
      {
       /*
        * Force the banner to have the classification on it...
	*/

        if (attr->num_values > 1 &&
	    !strcmp(attr->values[0].string.text, attr->values[1].string.text))
	{
          ippSetString(job->attrs, &attr, 0, Classification);
          ippSetString(job->attrs, &attr, 1, Classification);
	}
        else
	{
          if (attr->num_values == 1 ||
	      strcmp(attr->values[0].string.text, "none"))
            ippSetString(job->attrs, &attr, 0, Classification);

          if (attr->num_values > 1 &&
	      strcmp(attr->values[1].string.text, "none"))
	    ippSetString(job->attrs, &attr, 1, Classification);
        }

        if (attr->num_values > 1)
	  cupsdLogJob(job, CUPSD_LOG_NOTICE,
		      "CLASSIFICATION FORCED "
		      "job-sheets=\"%s,%s\", "
		      "job-originating-user-name=\"%s\"",
		      attr->values[0].string.text,
		      attr->values[1].string.text, job->username);
        else
	  cupsdLogJob(job, CUPSD_LOG_NOTICE,
		      "CLASSIFICATION FORCED "
		      "job-sheets=\"%s\", "
		      "job-originating-user-name=\"%s\"",
		      Classification, job->username);
      }
    }

   /*
    * See if we need to add the starting sheet...
    */

    if (!(printer->type & CUPS_PRINTER_REMOTE))
    {
      cupsdLogJob(job, CUPSD_LOG_INFO, "Adding start banner page \"%s\".",
		  attr->values[0].string.text);

      if ((kbytes = copy_banner(con, job, attr->values[0].string.text)) < 0)
      {
        cupsdSetJobState(job, IPP_JOB_ABORTED, CUPSD_JOB_PURGE,
	                 "Aborting job because the start banner could not be "
			 "copied.");
        return (NULL);
      }

      cupsdUpdateQuota(printer, job->username, 0, kbytes);
    }
  }
  else if ((attr = ippFindAttribute(job->attrs, "job-sheets",
                                    IPP_TAG_ZERO)) != NULL)
    job->job_sheets = attr;

 /*
  * Fill in the response info...
  */

  httpAssembleURIf(HTTP_URI_CODING_ALL, job_uri, sizeof(job_uri), "ipp", NULL, con->clientname, con->clientport, "/jobs/%d", job->id);
  ippAddString(con->response, IPP_TAG_JOB, IPP_TAG_URI, "job-uri", NULL, job_uri);

  ippAddInteger(con->response, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-id", job->id);

  ippAddInteger(con->response, IPP_TAG_JOB, IPP_TAG_ENUM, "job-state", (int)job->state_value);
  ippAddString(con->response, IPP_TAG_JOB, IPP_TAG_TEXT, "job-state-message", NULL, "");
  ippAddString(con->response, IPP_TAG_JOB, IPP_TAG_KEYWORD, "job-state-reasons", NULL, job->reasons->values[0].string.text);

  con->response->request.status.status_code = IPP_OK;

 /*
  * Add any job subscriptions...
  */

  add_job_subscriptions(con, job);

 /*
  * Set all but the first two attributes to the job attributes group...
  */

  for (attr = job->attrs->attrs->next->next; attr; attr = attr->next)
    attr->group_tag = IPP_TAG_JOB;

 /*
  * Fire the "job created" event...
  */

  cupsdAddEvent(CUPSD_EVENT_JOB_CREATED, printer, job, "Job created.");

 /*
  * Return the new job...
  */

  return (job);
}


/*
 * 'add_job_subscriptions()' - Add any subscriptions for a job.
 */

static void
add_job_subscriptions(
    cupsd_client_t *con,		/* I - Client connection */
    cupsd_job_t    *job)		/* I - Newly created job */
{
  int			i;		/* Looping var */
  ipp_attribute_t	*prev,		/* Previous attribute */
			*next,		/* Next attribute */
			*attr;		/* Current attribute */
  cupsd_subscription_t	*sub;		/* Subscription object */
  const char		*recipient,	/* notify-recipient-uri */
			*pullmethod;	/* notify-pull-method */
  ipp_attribute_t	*user_data;	/* notify-user-data */
  int			interval;	/* notify-time-interval */
  unsigned		mask;		/* notify-events */


 /*
  * Find the first subscription group attribute; return if we have
  * none...
  */

  for (attr = job->attrs->attrs; attr; attr = attr->next)
    if (attr->group_tag == IPP_TAG_SUBSCRIPTION)
      break;

  if (!attr)
    return;

 /*
  * Process the subscription attributes in the request...
  */

  while (attr)
  {
    recipient = NULL;
    pullmethod = NULL;
    user_data  = NULL;
    interval   = 0;
    mask       = CUPSD_EVENT_NONE;

    while (attr && attr->group_tag != IPP_TAG_ZERO)
    {
      if (!strcmp(attr->name, "notify-recipient-uri") &&
          attr->value_tag == IPP_TAG_URI)
      {
       /*
        * Validate the recipient scheme against the ServerBin/notifier
	* directory...
	*/

	char	notifier[1024],		/* Notifier filename */
		scheme[HTTP_MAX_URI],	/* Scheme portion of URI */
		userpass[HTTP_MAX_URI],	/* Username portion of URI */
		host[HTTP_MAX_URI],	/* Host portion of URI */
		resource[HTTP_MAX_URI];	/* Resource portion of URI */
        int	port;			/* Port portion of URI */
        struct stat info;		/* File information */

        recipient = attr->values[0].string.text;

	if (httpSeparateURI(HTTP_URI_CODING_ALL, recipient,
	                    scheme, sizeof(scheme), userpass, sizeof(userpass),
			    host, sizeof(host), &port,
			    resource, sizeof(resource)) < HTTP_URI_OK)
        {
          send_ipp_status(con, IPP_NOT_POSSIBLE,
	                  _("Bad notify-recipient-uri \"%s\"."), recipient);
	  ippAddInteger(con->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_ENUM,
	                "notify-status-code", IPP_URI_SCHEME);
	  return;
	}

        snprintf(notifier, sizeof(notifier), "%s/notifier/%s", ServerBin, scheme);
        if (access(notifier, X_OK) || stat(notifier, &info) || !S_ISREG(info.st_mode))
	{
          send_ipp_status(con, IPP_NOT_POSSIBLE,
	                  _("notify-recipient-uri URI \"%s\" uses unknown "
			    "scheme."), recipient);
	  ippAddInteger(con->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_ENUM,
	                "notify-status-code", IPP_URI_SCHEME);
	  return;
	}

        if (!strcmp(scheme, "rss") && !check_rss_recipient(recipient))
	{
          send_ipp_status(con, IPP_NOT_POSSIBLE,
	                  _("notify-recipient-uri URI \"%s\" is already used."),
			  recipient);
	  ippAddInteger(con->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_ENUM,
	                "notify-status-code", IPP_ATTRIBUTES);
	  return;
	}
      }
      else if (!strcmp(attr->name, "notify-pull-method") &&
               attr->value_tag == IPP_TAG_KEYWORD)
      {
        pullmethod = attr->values[0].string.text;

        if (strcmp(pullmethod, "ippget"))
	{
          send_ipp_status(con, IPP_NOT_POSSIBLE,
	                  _("Bad notify-pull-method \"%s\"."), pullmethod);
	  ippAddInteger(con->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_ENUM,
	                "notify-status-code", IPP_ATTRIBUTES);
	  return;
	}
      }
      else if (!strcmp(attr->name, "notify-charset") &&
               attr->value_tag == IPP_TAG_CHARSET &&
	       strcmp(attr->values[0].string.text, "us-ascii") &&
	       strcmp(attr->values[0].string.text, "utf-8"))
      {
        send_ipp_status(con, IPP_CHARSET,
	                _("Character set \"%s\" not supported."),
			attr->values[0].string.text);
	return;
      }
      else if (!strcmp(attr->name, "notify-natural-language") &&
               (attr->value_tag != IPP_TAG_LANGUAGE ||
	        strcmp(attr->values[0].string.text, DefaultLanguage)))
      {
        send_ipp_status(con, IPP_CHARSET,
	                _("Language \"%s\" not supported."),
			attr->values[0].string.text);
	return;
      }
      else if (!strcmp(attr->name, "notify-user-data") &&
               attr->value_tag == IPP_TAG_STRING)
      {
        if (attr->num_values > 1 || attr->values[0].unknown.length > 63)
	{
          send_ipp_status(con, IPP_REQUEST_VALUE,
	                  _("The notify-user-data value is too large "
			    "(%d > 63 octets)."),
			  attr->values[0].unknown.length);
	  return;
	}

        user_data = attr;
      }
      else if (!strcmp(attr->name, "notify-events") &&
               attr->value_tag == IPP_TAG_KEYWORD)
      {
        for (i = 0; i < attr->num_values; i ++)
	  mask |= cupsdEventValue(attr->values[i].string.text);
      }
      else if (!strcmp(attr->name, "notify-lease-duration"))
      {
        send_ipp_status(con, IPP_BAD_REQUEST,
	                _("The notify-lease-duration attribute cannot be "
			  "used with job subscriptions."));
	return;
      }
      else if (!strcmp(attr->name, "notify-time-interval") &&
               attr->value_tag == IPP_TAG_INTEGER)
        interval = attr->values[0].integer;

      attr = attr->next;
    }

    if (!recipient && !pullmethod)
      break;

    if (mask == CUPSD_EVENT_NONE)
      mask = CUPSD_EVENT_JOB_COMPLETED;

    if ((sub = cupsdAddSubscription(mask, cupsdFindDest(job->dest), job,
                                    recipient, 0)) != NULL)
    {
      sub->interval = interval;

      cupsdSetString(&sub->owner, job->username);

      if (user_data)
      {
	sub->user_data_len = user_data->values[0].unknown.length;
	memcpy(sub->user_data, user_data->values[0].unknown.data,
	       (size_t)sub->user_data_len);
      }

      ippAddSeparator(con->response);
      ippAddInteger(con->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER,
		    "notify-subscription-id", sub->id);

      cupsdLogMessage(CUPSD_LOG_DEBUG, "Added subscription %d for job %d",
                      sub->id, job->id);
    }

    if (attr)
      attr = attr->next;
  }

  cupsdMarkDirty(CUPSD_DIRTY_SUBSCRIPTIONS);

 /*
  * Remove all of the subscription attributes from the job request...
  *
  * TODO: Optimize this since subscription groups have to come at the
  * end of the request...
  */

  for (attr = job->attrs->attrs, prev = NULL; attr; attr = next)
  {
    next = attr->next;

    if (attr->group_tag == IPP_TAG_SUBSCRIPTION ||
        attr->group_tag == IPP_TAG_ZERO)
    {
     /*
      * Free and remove this attribute...
      */

      ippDeleteAttribute(NULL, attr);

      if (prev)
        prev->next = next;
      else
        job->attrs->attrs = next;
    }
    else
      prev = attr;
  }

  job->attrs->last    = prev;
  job->attrs->current = prev;
}


/*
 * 'add_job_uuid()' - Add job-uuid attribute to a job.
 *
 * See RFC 4122 for the definition of UUIDs and the format.
 */

static void
add_job_uuid(cupsd_job_t *job)		/* I - Job */
{
  char			uuid[64];	/* job-uuid string */


 /*
  * Add a job-uuid attribute if none exists...
  */

  if (!ippFindAttribute(job->attrs, "job-uuid", IPP_TAG_URI))
    ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_URI, "job-uuid", NULL,
		 httpAssembleUUID(ServerName, RemotePort, job->dest, job->id,
		                  uuid, sizeof(uuid)));
}


/*
 * 'add_printer()' - Add a printer to the system.
 */

static void
add_printer(cupsd_client_t  *con,	/* I - Client connection */
            ipp_attribute_t *uri)	/* I - URI of printer */
{
  http_status_t	status;			/* Policy status */
  int		i = 0;			/* Looping var */
  char		scheme[HTTP_MAX_URI],	/* Method portion of URI */
		username[HTTP_MAX_URI],	/* Username portion of URI */
		host[HTTP_MAX_URI],	/* Host portion of URI */
		resource[HTTP_MAX_URI];	/* Resource portion of URI */
  int		port;			/* Port portion of URI */
  cupsd_printer_t *printer;		/* Printer/class */
  ipp_attribute_t *attr;		/* Printer attribute */
  cups_file_t	*fp;			/* Script/PPD file */
  char		line[1024];		/* Line from file... */
  char		srcfile[1024],		/* Source Script/PPD file */
		dstfile[1024];		/* Destination Script/PPD file */
  int		modify;			/* Non-zero if we are modifying */
  int		changed_driver,		/* Changed the PPD? */
		need_restart_job,	/* Need to restart job? */
		set_device_uri,		/* Did we set the device URI? */
		set_port_monitor;	/* Did we set the port monitor? */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "add_printer(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * Do we have a valid URI?
  */

  httpSeparateURI(HTTP_URI_CODING_ALL, uri->values[0].string.text, scheme,
                  sizeof(scheme), username, sizeof(username), host,
		  sizeof(host), &port, resource, sizeof(resource));

  if (strncmp(resource, "/printers/", 10) || strlen(resource) == 10)
  {
   /*
    * No, return an error...
    */

    send_ipp_status(con, IPP_BAD_REQUEST,
                    _("The printer-uri must be of the form "
		      "\"ipp://HOSTNAME/printers/PRINTERNAME\"."));
    return;
  }

 /*
  * Do we have a valid printer name?
  */

  if (!validate_name(resource + 10))
  {
   /*
    * No, return an error...
    */

    send_ipp_status(con, IPP_BAD_REQUEST,
                    _("The printer-uri \"%s\" contains invalid characters."),
		    uri->values[0].string.text);
    return;
  }

 /*
  * See if the printer already exists; if not, create a new printer...
  */

  if ((printer = cupsdFindPrinter(resource + 10)) == NULL)
  {
   /*
    * Printer doesn't exist; see if we have a class of the same name...
    */

    if (cupsdFindClass(resource + 10))
    {
     /*
      * Yes, return an error...
      */

      send_ipp_status(con, IPP_NOT_POSSIBLE,
                      _("A class named \"%s\" already exists."),
        	      resource + 10);
      return;
    }

   /*
    * No, check the default policy then add the printer...
    */

    if ((status = cupsdCheckPolicy(DefaultPolicyPtr, con, NULL)) != HTTP_OK)
    {
      send_http_error(con, status, NULL);
      return;
    }

    printer = cupsdAddPrinter(resource + 10);
    modify  = 0;

    printer->printer_id = NextPrinterId ++;
  }
  else if ((status = cupsdCheckPolicy(printer->op_policy_ptr, con,
                                      NULL)) != HTTP_OK)
  {
    send_http_error(con, status, printer);
    return;
  }
  else
    modify = 1;

 /*
  * Look for attributes and copy them over as needed...
  */

  changed_driver   = 0;
  need_restart_job = 0;

  if ((attr = ippFindAttribute(con->request, "printer-is-temporary", IPP_TAG_BOOLEAN)) != NULL)
    printer->temporary = ippGetBoolean(attr, 0);

  if ((attr = ippFindAttribute(con->request, "printer-location",
                               IPP_TAG_TEXT)) != NULL)
    cupsdSetString(&printer->location, attr->values[0].string.text);

  if ((attr = ippFindAttribute(con->request, "printer-geo-location", IPP_TAG_URI)) != NULL && !strncmp(attr->values[0].string.text, "geo:", 4))
    cupsdSetString(&printer->geo_location, attr->values[0].string.text);

  if ((attr = ippFindAttribute(con->request, "printer-organization", IPP_TAG_TEXT)) != NULL)
    cupsdSetString(&printer->organization, attr->values[0].string.text);

  if ((attr = ippFindAttribute(con->request, "printer-organizational-unit", IPP_TAG_TEXT)) != NULL)
    cupsdSetString(&printer->organizational_unit, attr->values[0].string.text);

  if ((attr = ippFindAttribute(con->request, "printer-info",
                               IPP_TAG_TEXT)) != NULL)
    cupsdSetString(&printer->info, attr->values[0].string.text);

  set_device_uri = 0;

  if ((attr = ippFindAttribute(con->request, "ColorModel", IPP_TAG_NAME)) != NULL)
  {
    const char * keyword = NULL;

    if (!strcmp(attr->values[0].string.text, "FastGray") || !strcmp(attr->values[0].string.text, "Gray") || !strcmp(attr->values[0].string.text, "DeviceGray"))
      keyword = "monochrome";
    else
      keyword = "color";

    printer->num_options = cupsAddOption("print-color-mode", keyword, printer->num_options, &printer->options);
  }

  if ((attr = ippFindAttribute(con->request, "device-uri",
                               IPP_TAG_URI)) != NULL)
  {
   /*
    * Do we have a valid device URI?
    */

    http_uri_status_t	uri_status;	/* URI separation status */
    char		old_device_uri[1024];
					/* Old device URI */

    need_restart_job = 1;

    uri_status = httpSeparateURI(HTTP_URI_CODING_ALL,
				 attr->values[0].string.text,
				 scheme, sizeof(scheme),
				 username, sizeof(username),
				 host, sizeof(host), &port,
				 resource, sizeof(resource));

    cupsdLogMessage(CUPSD_LOG_DEBUG, "%s device-uri: %s", printer->name, httpURIStatusString(uri_status));

    if (uri_status < HTTP_URI_OK)
    {
      send_ipp_status(con, IPP_NOT_POSSIBLE, _("Bad device-uri \"%s\"."),
		      attr->values[0].string.text);
      if (!modify)
        cupsdDeletePrinter(printer, 0);

      return;
    }

    if (!strcmp(scheme, "file"))
    {
     /*
      * See if the administrator has enabled file devices...
      */

      if (!FileDevice && strcmp(resource, "/dev/null"))
      {
       /*
        * File devices are disabled and the URL is not file:/dev/null...
	*/

	send_ipp_status(con, IPP_NOT_POSSIBLE,
	                _("File device URIs have been disabled. "
	                  "To enable, see the FileDevice directive in "
			  "\"%s/cups-files.conf\"."),
			ServerRoot);
	if (!modify)
	  cupsdDeletePrinter(printer, 0);

	return;
      }
    }
    else
    {
     /*
      * See if the backend exists and is executable...
      */

      snprintf(srcfile, sizeof(srcfile), "%s/backend/%s", ServerBin, scheme);
      if (access(srcfile, X_OK))
      {
       /*
        * Could not find device in list!
	*/

	send_ipp_status(con, IPP_NOT_POSSIBLE,
                        _("Bad device-uri scheme \"%s\"."), scheme);
	if (!modify)
	  cupsdDeletePrinter(printer, 0);

	return;
      }
    }

    if (printer->sanitized_device_uri)
      strlcpy(old_device_uri, printer->sanitized_device_uri,
              sizeof(old_device_uri));
    else
      old_device_uri[0] = '\0';

    cupsdSetDeviceURI(printer, attr->values[0].string.text);

    cupsdLogMessage(CUPSD_LOG_INFO,
                    "Setting %s device-uri to \"%s\" (was \"%s\".)",
        	    printer->name, printer->sanitized_device_uri,
		    old_device_uri);

    set_device_uri = 1;
  }

  set_port_monitor = 0;

  if ((attr = ippFindAttribute(con->request, "port-monitor",
                               IPP_TAG_NAME)) != NULL)
  {
    ipp_attribute_t	*supported;	/* port-monitor-supported attribute */


    need_restart_job = 1;

    supported = ippFindAttribute(printer->ppd_attrs, "port-monitor-supported",
                                 IPP_TAG_NAME);
    if (supported)
    {
      for (i = 0; i < supported->num_values; i ++)
        if (!strcmp(supported->values[i].string.text,
                    attr->values[0].string.text))
          break;
    }

    if (!supported || i >= supported->num_values)
    {
      send_ipp_status(con, IPP_NOT_POSSIBLE, _("Bad port-monitor \"%s\"."),
        	      attr->values[0].string.text);
      if (!modify)
        cupsdDeletePrinter(printer, 0);

      return;
    }

    cupsdLogMessage(CUPSD_LOG_INFO,
                    "Setting %s port-monitor to \"%s\" (was \"%s\".)",
                    printer->name, attr->values[0].string.text,
	            printer->port_monitor ? printer->port_monitor : "none");

    if (strcmp(attr->values[0].string.text, "none"))
      cupsdSetString(&printer->port_monitor, attr->values[0].string.text);
    else
      cupsdClearString(&printer->port_monitor);

    set_port_monitor = 1;
  }

  if ((attr = ippFindAttribute(con->request, "printer-is-accepting-jobs",
                               IPP_TAG_BOOLEAN)) != NULL &&
      attr->values[0].boolean != printer->accepting)
  {
    cupsdLogMessage(CUPSD_LOG_INFO,
                    "Setting %s printer-is-accepting-jobs to %d (was %d.)",
                    printer->name, attr->values[0].boolean, printer->accepting);

    printer->accepting = attr->values[0].boolean;

    cupsdAddEvent(CUPSD_EVENT_PRINTER_STATE, printer, NULL,
                  "%s accepting jobs.",
		  printer->accepting ? "Now" : "No longer");
  }

  if ((attr = ippFindAttribute(con->request, "printer-is-shared", IPP_TAG_BOOLEAN)) != NULL)
  {
    if (ippGetBoolean(attr, 0) &&
        printer->num_auth_info_required == 1 &&
	!strcmp(printer->auth_info_required[0], "negotiate"))
    {
      send_ipp_status(con, IPP_BAD_REQUEST,
                      _("Cannot share a remote Kerberized printer."));
      if (!modify)
        cupsdDeletePrinter(printer, 0);

      return;
    }

    if (printer->type & CUPS_PRINTER_REMOTE)
    {
     /*
      * Cannot re-share remote printers.
      */

      send_ipp_status(con, IPP_BAD_REQUEST, _("Cannot change printer-is-shared for remote queues."));
      if (!modify)
        cupsdDeletePrinter(printer, 0);

      return;
    }

    if (printer->shared && !ippGetBoolean(attr, 0))
      cupsdDeregisterPrinter(printer, 1);

    cupsdLogMessage(CUPSD_LOG_INFO,
                    "Setting %s printer-is-shared to %d (was %d.)",
                    printer->name, attr->values[0].boolean, printer->shared);

    printer->shared = ippGetBoolean(attr, 0);
    if (printer->shared && printer->temporary)
      printer->temporary = 0;
  }

  if ((attr = ippFindAttribute(con->request, "printer-state",
                               IPP_TAG_ENUM)) != NULL)
  {
    if (attr->values[0].integer != IPP_PRINTER_IDLE &&
        attr->values[0].integer != IPP_PRINTER_STOPPED)
    {
      send_ipp_status(con, IPP_BAD_REQUEST, _("Bad printer-state value %d."),
                      attr->values[0].integer);
      if (!modify)
        cupsdDeletePrinter(printer, 0);

      return;
    }

    cupsdLogMessage(CUPSD_LOG_INFO, "Setting %s printer-state to %d (was %d.)",
                    printer->name, attr->values[0].integer, printer->state);

    if (attr->values[0].integer == IPP_PRINTER_STOPPED)
      cupsdStopPrinter(printer, 0);
    else
    {
      need_restart_job = 1;
      cupsdSetPrinterState(printer, (ipp_pstate_t)(attr->values[0].integer), 0);
    }
  }

  if ((attr = ippFindAttribute(con->request, "printer-state-message",
                               IPP_TAG_TEXT)) != NULL)
  {
    strlcpy(printer->state_message, attr->values[0].string.text,
            sizeof(printer->state_message));

    cupsdAddEvent(CUPSD_EVENT_PRINTER_STATE, printer, NULL, "%s",
                  printer->state_message);
  }

  if ((attr = ippFindAttribute(con->request, "printer-state-reasons",
                               IPP_TAG_KEYWORD)) != NULL)
  {
    if (attr->num_values >
            (int)(sizeof(printer->reasons) / sizeof(printer->reasons[0])))
    {
      send_ipp_status(con, IPP_NOT_POSSIBLE,
                      _("Too many printer-state-reasons values (%d > %d)."),
		      attr->num_values,
		      (int)(sizeof(printer->reasons) /
		            sizeof(printer->reasons[0])));
      if (!modify)
        cupsdDeletePrinter(printer, 0);

      return;
    }

    for (i = 0; i < printer->num_reasons; i ++)
      _cupsStrFree(printer->reasons[i]);

    printer->num_reasons = 0;
    for (i = 0; i < attr->num_values; i ++)
    {
      if (!strcmp(attr->values[i].string.text, "none"))
        continue;

      printer->reasons[printer->num_reasons] = _cupsStrAlloc(attr->values[i].string.text);
      printer->num_reasons ++;

      if (!strcmp(attr->values[i].string.text, "paused") &&
          printer->state != IPP_PRINTER_STOPPED)
      {
	cupsdLogMessage(CUPSD_LOG_INFO,
	                "Setting %s printer-state to %d (was %d.)",
			printer->name, IPP_PRINTER_STOPPED, printer->state);
	cupsdStopPrinter(printer, 0);
      }
    }

    if (PrintcapFormat == PRINTCAP_PLIST)
      cupsdMarkDirty(CUPSD_DIRTY_PRINTCAP);

    cupsdAddEvent(CUPSD_EVENT_PRINTER_STATE, printer, NULL,
                  "Printer \"%s\" state changed.", printer->name);
  }

  if (!set_printer_defaults(con, printer))
  {
    if (!modify)
      cupsdDeletePrinter(printer, 0);

    return;
  }

  if ((attr = ippFindAttribute(con->request, "auth-info-required",
                               IPP_TAG_KEYWORD)) != NULL)
    cupsdSetAuthInfoRequired(printer, NULL, attr);

 /*
  * See if we have all required attributes...
  */

  if (!printer->device_uri)
    cupsdSetString(&printer->device_uri, "file:///dev/null");

 /*
  * See if we have a PPD file attached to the request...
  */

  if (con->filename)
  {
    need_restart_job = 1;
    changed_driver   = 1;

    strlcpy(srcfile, con->filename, sizeof(srcfile));

    if ((fp = cupsFileOpen(srcfile, "rb")))
    {
     /*
      * Yes; get the first line from it...
      */

      line[0] = '\0';
      cupsFileGets(fp, line, sizeof(line));
      cupsFileClose(fp);

     /*
      * Then see what kind of file it is...
      */

      if (strncmp(line, "*PPD-Adobe", 10))
      {
	send_ipp_status(con, IPP_STATUS_ERROR_DOCUMENT_FORMAT_NOT_SUPPORTED, _("Bad PPD file."));
	if (!modify)
	  cupsdDeletePrinter(printer, 0);

	return;
      }

      snprintf(dstfile, sizeof(dstfile), "%s/ppd/%s.ppd", ServerRoot,
               printer->name);

     /*
      * The new file is a PPD file, so move the file over to the ppd
      * directory...
      */

      if (copy_file(srcfile, dstfile, ConfigFilePerm))
      {
	send_ipp_status(con, IPP_INTERNAL_ERROR, _("Unable to copy PPD file - %s"), strerror(errno));
	if (!modify)
	  cupsdDeletePrinter(printer, 0);

	return;
      }

      cupsdLogMessage(CUPSD_LOG_DEBUG, "Copied PPD file successfully");
    }
  }
  else if ((attr = ippFindAttribute(con->request, "ppd-name", IPP_TAG_NAME)) != NULL)
  {
    const char *ppd_name = ippGetString(attr, 0, NULL);
					/* ppd-name value */

    need_restart_job = 1;
    changed_driver   = 1;

    if (!strcmp(ppd_name, "everywhere"))
    {
      // Create IPP Everywhere PPD...
      if (!printer->device_uri || (strncmp(printer->device_uri, "dnssd://", 8) && strncmp(printer->device_uri, "ipp://", 6) && strncmp(printer->device_uri, "ipps://", 7) && strncmp(printer->device_uri, "ippusb://", 9)))
      {
	send_ipp_status(con, IPP_INTERNAL_ERROR, _("IPP Everywhere driver requires an IPP connection."));
	if (!modify)
	  cupsdDeletePrinter(printer, 0);

	return;
      }

      if (!printer->printer_id)
	printer->printer_id = NextPrinterId ++;

      cupsdMarkDirty(CUPSD_DIRTY_PRINTERS);

      cupsdSetPrinterAttrs(printer);

      /* Run a background thread to create the PPD... */
      cupsdLogClient(con, CUPSD_LOG_DEBUG, "Creating PPD in background thread.");

      con->bg_pending = 1;
      con->bg_printer = printer;

      _cupsThreadCreate((_cups_thread_func_t)create_local_bg_thread, con);
      return;
    }
    else if (!strcmp(ppd_name, "raw"))
    {
     /*
      * Raw driver, remove any existing PPD file.
      */

      snprintf(dstfile, sizeof(dstfile), "%s/ppd/%s.ppd", ServerRoot, printer->name);
      unlink(dstfile);
    }
    else if (strstr(ppd_name, "../"))
    {
      send_ipp_status(con, IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES, _("Invalid ppd-name value."));
      if (!modify)
	cupsdDeletePrinter(printer, 0);

      return;
    }
    else
    {
     /*
      * PPD model file...
      */

      snprintf(dstfile, sizeof(dstfile), "%s/ppd/%s.ppd", ServerRoot, printer->name);

      if (copy_model(con, ppd_name, dstfile))
      {
	if (!modify)
	  cupsdDeletePrinter(printer, 0);

	return;
      }

      cupsdLogMessage(CUPSD_LOG_DEBUG, "Copied PPD file successfully");
    }
  }

  if (changed_driver)
  {
   /*
    * If we changed the PPD, then remove the printer's cache file and clear the
    * printer-state-reasons...
    */

    char cache_name[1024];		/* Cache filename for printer attrs */

    snprintf(cache_name, sizeof(cache_name), "%s/%s.data", CacheDir, printer->name);
    unlink(cache_name);

    cupsdSetPrinterReasons(printer, "none");

   /*
    * (Re)register color profiles...
    */

    cupsdRegisterColor(printer);
  }

 /*
  * If we set the device URI but not the port monitor, check which port
  * monitor to use by default...
  */

  if (set_device_uri && !set_port_monitor)
  {
    ppd_file_t	*ppd;			/* PPD file */
    ppd_attr_t	*ppdattr;		/* cupsPortMonitor attribute */


    httpSeparateURI(HTTP_URI_CODING_ALL, printer->device_uri, scheme,
                    sizeof(scheme), username, sizeof(username), host,
		    sizeof(host), &port, resource, sizeof(resource));

    snprintf(srcfile, sizeof(srcfile), "%s/ppd/%s.ppd", ServerRoot,
	     printer->name);
    if ((ppd = _ppdOpenFile(srcfile, _PPD_LOCALIZATION_NONE)) != NULL)
    {
      for (ppdattr = ppdFindAttr(ppd, "cupsPortMonitor", NULL);
	   ppdattr;
	   ppdattr = ppdFindNextAttr(ppd, "cupsPortMonitor", NULL))
        if (!strcmp(scheme, ppdattr->spec))
	{
	  cupsdLogMessage(CUPSD_LOG_INFO,
			  "Setting %s port-monitor to \"%s\" (was \"%s\".)",
			  printer->name, ppdattr->value,
			  printer->port_monitor ? printer->port_monitor
			                        : "none");

	  if (strcmp(ppdattr->value, "none"))
	    cupsdSetString(&printer->port_monitor, ppdattr->value);
	  else
	    cupsdClearString(&printer->port_monitor);

	  break;
	}

      ppdClose(ppd);
    }
  }

  printer->config_time = time(NULL);

 /*
  * Update the printer attributes and return...
  */

  if (!printer->temporary)
  {
    if (!printer->printer_id)
      printer->printer_id = NextPrinterId ++;

    cupsdMarkDirty(CUPSD_DIRTY_PRINTERS);
  }

  cupsdSetPrinterAttrs(printer);

  if (need_restart_job && printer->job)
  {
   /*
    * Restart the current job...
    */

    cupsdSetJobState(printer->job, IPP_JOB_PENDING, CUPSD_JOB_FORCE,
                     "Job restarted because the printer was modified.");
  }

  cupsdMarkDirty(CUPSD_DIRTY_PRINTCAP);

  if (modify)
  {
    cupsdAddEvent(CUPSD_EVENT_PRINTER_MODIFIED,
                  printer, NULL, "Printer \"%s\" modified by \"%s\".",
		  printer->name, get_username(con));

    cupsdLogMessage(CUPSD_LOG_INFO, "Printer \"%s\" modified by \"%s\".",
                    printer->name, get_username(con));
  }
  else
  {
    cupsdAddEvent(CUPSD_EVENT_PRINTER_ADDED,
                  printer, NULL, "New printer \"%s\" added by \"%s\".",
		  printer->name, get_username(con));

    cupsdLogMessage(CUPSD_LOG_INFO, "New printer \"%s\" added by \"%s\".",
                    printer->name, get_username(con));
  }

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'add_printer_state_reasons()' - Add the "printer-state-reasons" attribute
 *                                 based upon the printer state...
 */

static void
add_printer_state_reasons(
    cupsd_client_t  *con,		/* I - Client connection */
    cupsd_printer_t *p)			/* I - Printer info */
{
  cupsdLogMessage(CUPSD_LOG_DEBUG2,
                  "add_printer_state_reasons(%p[%d], %p[%s])",
                  (void *)con, con->number, (void *)p, p->name);

  if (p->num_reasons == 0)
    ippAddString(con->response, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
                 "printer-state-reasons", NULL, "none");
  else
    ippAddStrings(con->response, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
                  "printer-state-reasons", p->num_reasons, NULL,
		  (const char * const *)p->reasons);
}


/*
 * 'add_queued_job_count()' - Add the "queued-job-count" attribute for
 *                            the specified printer or class.
 */

static void
add_queued_job_count(
    cupsd_client_t  *con,		/* I - Client connection */
    cupsd_printer_t *p)			/* I - Printer or class */
{
  int		count;			/* Number of jobs on destination */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "add_queued_job_count(%p[%d], %p[%s])",
                  (void *)con, con->number, (void *)p, p->name);

  count = cupsdGetPrinterJobCount(p->name);

  ippAddInteger(con->response, IPP_TAG_PRINTER, IPP_TAG_INTEGER,
                "queued-job-count", count);
}


/*
 * 'apply_printer_defaults()' - Apply printer default options to a job.
 */

static void
apply_printer_defaults(
    cupsd_printer_t *printer,		/* I - Printer */
    cupsd_job_t     *job)		/* I - Job */
{
  int		i,			/* Looping var */
		num_options;		/* Number of default options */
  cups_option_t	*options,		/* Default options */
		*option;		/* Current option */


  cupsdLogJob(job, CUPSD_LOG_DEBUG, "Applying default options...");

 /*
  * Collect all of the default options and add the missing ones to the
  * job object...
  */

  for (i = printer->num_options, num_options = 0, options = NULL,
           option = printer->options;
       i > 0;
       i --, option ++)
    if (!ippFindAttribute(job->attrs, option->name, IPP_TAG_ZERO))
    {
      if (!strcmp(option->name, "media") && ippFindAttribute(job->attrs, "PageSize", IPP_TAG_NAME))
        continue;                     /* Don't override PageSize */

      if (!strcmp(option->name, "output-bin") && ippFindAttribute(job->attrs, "OutputBin", IPP_TAG_NAME))
        continue;                     /* Don't override OutputBin */

      if (!strcmp(option->name, "print-quality") && ippFindAttribute(job->attrs, "cupsPrintQuality", IPP_TAG_NAME))
        continue;                     /* Don't override cupsPrintQuality */

      if (!strcmp(option->name, "print-color-mode") && ippFindAttribute(job->attrs, "ColorModel", IPP_TAG_NAME))
        continue;                     /* Don't override ColorModel */

      if (!strcmp(option->name, "sides") && ippFindAttribute(job->attrs, "Duplex", IPP_TAG_NAME))
        continue;                     /* Don't override Duplex */

      cupsdLogJob(job, CUPSD_LOG_DEBUG, "Adding default %s=%s", option->name, option->value);

      num_options = cupsAddOption(option->name, option->value, num_options, &options);
    }

 /*
  * Encode these options as attributes in the job object...
  */

  cupsEncodeOptions2(job->attrs, num_options, options, IPP_TAG_JOB);
  cupsFreeOptions(num_options, options);
}


/*
 * 'authenticate_job()' - Set job authentication info.
 */

static void
authenticate_job(cupsd_client_t  *con,	/* I - Client connection */
	         ipp_attribute_t *uri)	/* I - Job URI */
{
  ipp_attribute_t	*attr,		/* job-id attribute */
			*auth_info;	/* auth-info attribute */
  int			jobid;		/* Job ID */
  cupsd_job_t		*job;		/* Current job */
  char			scheme[HTTP_MAX_URI],
					/* Method portion of URI */
			username[HTTP_MAX_URI],
					/* Username portion of URI */
			host[HTTP_MAX_URI],
					/* Host portion of URI */
			resource[HTTP_MAX_URI];
					/* Resource portion of URI */
  int			port;		/* Port portion of URI */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "authenticate_job(%p[%d], %s)",
                  (void *)con, con->number, uri->values[0].string.text);

 /*
  * Start with "everything is OK" status...
  */

  con->response->request.status.status_code = IPP_OK;

 /*
  * See if we have a job URI or a printer URI...
  */

  if (!strcmp(uri->name, "printer-uri"))
  {
   /*
    * Got a printer URI; see if we also have a job-id attribute...
    */

    if ((attr = ippFindAttribute(con->request, "job-id",
                                 IPP_TAG_INTEGER)) == NULL)
    {
      send_ipp_status(con, IPP_BAD_REQUEST,
                      _("Got a printer-uri attribute but no job-id."));
      return;
    }

    jobid = attr->values[0].integer;
  }
  else
  {
   /*
    * Got a job URI; parse it to get the job ID...
    */

    httpSeparateURI(HTTP_URI_CODING_ALL, uri->values[0].string.text, scheme,
                    sizeof(scheme), username, sizeof(username), host,
		    sizeof(host), &port, resource, sizeof(resource));

    if (strncmp(resource, "/jobs/", 6))
    {
     /*
      * Not a valid URI!
      */

      send_ipp_status(con, IPP_BAD_REQUEST, _("Bad job-uri \"%s\"."),
                      uri->values[0].string.text);
      return;
    }

    jobid = atoi(resource + 6);
  }

 /*
  * See if the job exists...
  */

  if ((job = cupsdFindJob(jobid)) == NULL)
  {
   /*
    * Nope - return a "not found" error...
    */

    send_ipp_status(con, IPP_NOT_FOUND, _("Job #%d does not exist."), jobid);
    return;
  }

 /*
  * See if the job has been completed...
  */

  if (job->state_value != IPP_JOB_HELD)
  {
   /*
    * Return a "not-possible" error...
    */

    send_ipp_status(con, IPP_NOT_POSSIBLE,
                    _("Job #%d is not held for authentication."),
		    jobid);
    return;
  }

 /*
  * See if we have already authenticated...
  */

  auth_info = ippFindAttribute(con->request, "auth-info", IPP_TAG_TEXT);

  if (!con->username[0] && !auth_info)
  {
    cupsd_printer_t	*printer;	/* Job destination */

   /*
    * No auth data.  If we need to authenticate via Kerberos, send a
    * HTTP auth challenge, otherwise just return an IPP error...
    */

    printer = cupsdFindDest(job->dest);

    if (printer && printer->num_auth_info_required > 0 &&
        !strcmp(printer->auth_info_required[0], "negotiate"))
      send_http_error(con, HTTP_UNAUTHORIZED, printer);
    else
      send_ipp_status(con, IPP_NOT_AUTHORIZED,
		      _("No authentication information provided."));
    return;
  }

 /*
  * See if the job is owned by the requesting user...
  */

  if (!validate_user(job, con, job->username, username, sizeof(username)))
  {
    send_http_error(con, con->username[0] ? HTTP_FORBIDDEN : HTTP_UNAUTHORIZED,
                    cupsdFindDest(job->dest));
    return;
  }

 /*
  * Save the authentication information for this job...
  */

  save_auth_info(con, job, auth_info);

 /*
  * Reset the job-hold-until value to "no-hold"...
  */

  if ((attr = ippFindAttribute(job->attrs, "job-hold-until",
                               IPP_TAG_KEYWORD)) == NULL)
    attr = ippFindAttribute(job->attrs, "job-hold-until", IPP_TAG_NAME);

  if (attr)
  {
    ippSetValueTag(job->attrs, &attr, IPP_TAG_KEYWORD);
    ippSetString(job->attrs, &attr, 0, "no-hold");
  }

 /*
  * Release the job and return...
  */

  cupsdReleaseJob(job);

  cupsdAddEvent(CUPSD_EVENT_JOB_STATE, NULL, job, "Job authenticated by user");

  cupsdLogJob(job, CUPSD_LOG_INFO, "Authenticated by \"%s\".", con->username);

  cupsdCheckJobs();
}


/*
 * 'cancel_all_jobs()' - Cancel all or selected print jobs.
 */

static void
cancel_all_jobs(cupsd_client_t  *con,	/* I - Client connection */
	        ipp_attribute_t *uri)	/* I - Job or Printer URI */
{
  int		i;			/* Looping var */
  http_status_t	status;			/* Policy status */
  cups_ptype_t	dtype;			/* Destination type */
  char		scheme[HTTP_MAX_URI],	/* Scheme portion of URI */
		userpass[HTTP_MAX_URI],	/* Username portion of URI */
		hostname[HTTP_MAX_URI],	/* Host portion of URI */
		resource[HTTP_MAX_URI];	/* Resource portion of URI */
  int		port;			/* Port portion of URI */
  ipp_attribute_t *attr;		/* Attribute in request */
  const char	*username = NULL;	/* Username */
  cupsd_jobaction_t purge = CUPSD_JOB_DEFAULT;
					/* Purge? */
  cupsd_printer_t *printer;		/* Printer */
  ipp_attribute_t *job_ids;		/* job-ids attribute */
  cupsd_job_t	*job;			/* Job */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "cancel_all_jobs(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * Get the jobs to cancel/purge...
  */

  switch (con->request->request.op.operation_id)
  {
    case IPP_PURGE_JOBS :
       /*
	* Get the username (if any) for the jobs we want to cancel (only if
	* "my-jobs" is specified...
	*/

        if ((attr = ippFindAttribute(con->request, "my-jobs",
                                     IPP_TAG_BOOLEAN)) != NULL &&
            attr->values[0].boolean)
	{
	  if ((attr = ippFindAttribute(con->request, "requesting-user-name",
				       IPP_TAG_NAME)) != NULL)
	    username = attr->values[0].string.text;
	  else
	  {
	    send_ipp_status(con, IPP_BAD_REQUEST,
			    _("Missing requesting-user-name attribute."));
	    return;
	  }
	}

       /*
	* Look for the "purge-jobs" attribute...
	*/

	if ((attr = ippFindAttribute(con->request, "purge-jobs",
				     IPP_TAG_BOOLEAN)) != NULL)
	  purge = attr->values[0].boolean ? CUPSD_JOB_PURGE : CUPSD_JOB_DEFAULT;
	else
	  purge = CUPSD_JOB_PURGE;
	break;

    case IPP_CANCEL_MY_JOBS :
        if (con->username[0])
          username = con->username;
        else if ((attr = ippFindAttribute(con->request, "requesting-user-name",
					  IPP_TAG_NAME)) != NULL)
          username = attr->values[0].string.text;
        else
        {
	  send_ipp_status(con, IPP_BAD_REQUEST,
			  _("Missing requesting-user-name attribute."));
	  return;
        }

    default :
        break;
  }

  job_ids = ippFindAttribute(con->request, "job-ids", IPP_TAG_INTEGER);

 /*
  * See if we have a printer URI...
  */

  if (strcmp(uri->name, "printer-uri"))
  {
    send_ipp_status(con, IPP_BAD_REQUEST,
                    _("The printer-uri attribute is required."));
    return;
  }

 /*
  * And if the destination is valid...
  */

  if (!cupsdValidateDest(uri->values[0].string.text, &dtype, &printer))
  {
   /*
    * Bad URI?
    */

    httpSeparateURI(HTTP_URI_CODING_ALL, uri->values[0].string.text,
                    scheme, sizeof(scheme), userpass, sizeof(userpass),
		    hostname, sizeof(hostname), &port,
		    resource, sizeof(resource));

    if ((!strncmp(resource, "/printers/", 10) && resource[10]) ||
        (!strncmp(resource, "/classes/", 9) && resource[9]))
    {
      send_ipp_status(con, IPP_NOT_FOUND,
                      _("The printer or class does not exist."));
      return;
    }

   /*
    * Check policy...
    */

    if ((status = cupsdCheckPolicy(DefaultPolicyPtr, con, NULL)) != HTTP_OK)
    {
      send_http_error(con, status, NULL);
      return;
    }

    if (job_ids)
    {
      for (i = 0; i < job_ids->num_values; i ++)
      {
	if ((job = cupsdFindJob(job_ids->values[i].integer)) == NULL)
	  break;

        if (con->request->request.op.operation_id == IPP_CANCEL_MY_JOBS &&
            _cups_strcasecmp(job->username, username))
          break;
      }

      if (i < job_ids->num_values)
      {
	send_ipp_status(con, IPP_NOT_FOUND, _("Job #%d does not exist."),
			job_ids->values[i].integer);
	return;
      }

      for (i = 0; i < job_ids->num_values; i ++)
      {
	job = cupsdFindJob(job_ids->values[i].integer);

	cupsdSetJobState(job, IPP_JOB_CANCELED, purge,
	                 purge == CUPSD_JOB_PURGE ? "Job purged by user." :
	                                            "Job canceled by user.");
      }

      cupsdLogMessage(CUPSD_LOG_INFO, "Selected jobs were %s by \"%s\".",
		      purge == CUPSD_JOB_PURGE ? "purged" : "canceled",
		      get_username(con));
    }
    else
    {
     /*
      * Cancel all jobs on all printers...
      */

      cupsdCancelJobs(NULL, username, purge != CUPSD_JOB_DEFAULT);

      cupsdLogMessage(CUPSD_LOG_INFO, "All jobs were %s by \"%s\".",
		      purge == CUPSD_JOB_PURGE ? "purged" : "canceled",
		      get_username(con));
    }
  }
  else
  {
   /*
    * Check policy...
    */

    if ((status = cupsdCheckPolicy(printer->op_policy_ptr, con,
                                   NULL)) != HTTP_OK)
    {
      send_http_error(con, status, printer);
      return;
    }

    if (job_ids)
    {
      for (i = 0; i < job_ids->num_values; i ++)
      {
	if ((job = cupsdFindJob(job_ids->values[i].integer)) == NULL ||
	    _cups_strcasecmp(job->dest, printer->name))
	  break;

        if (con->request->request.op.operation_id == IPP_CANCEL_MY_JOBS &&
            _cups_strcasecmp(job->username, username))
          break;
      }

      if (i < job_ids->num_values)
      {
	send_ipp_status(con, IPP_NOT_FOUND, _("Job #%d does not exist."),
			job_ids->values[i].integer);
	return;
      }

      for (i = 0; i < job_ids->num_values; i ++)
      {
	job = cupsdFindJob(job_ids->values[i].integer);

	cupsdSetJobState(job, IPP_JOB_CANCELED, purge,
	                 purge == CUPSD_JOB_PURGE ? "Job purged by user." :
	                                            "Job canceled by user.");
      }

      cupsdLogMessage(CUPSD_LOG_INFO, "Selected jobs were %s by \"%s\".",
		      purge == CUPSD_JOB_PURGE ? "purged" : "canceled",
		      get_username(con));
    }
    else
    {
     /*
      * Cancel all of the jobs on the named printer...
      */

      cupsdCancelJobs(printer->name, username, purge != CUPSD_JOB_DEFAULT);

      cupsdLogMessage(CUPSD_LOG_INFO, "All jobs on \"%s\" were %s by \"%s\".",
		      printer->name,
		      purge == CUPSD_JOB_PURGE ? "purged" : "canceled",
		      get_username(con));
    }
  }

  con->response->request.status.status_code = IPP_OK;

  cupsdCheckJobs();
}


/*
 * 'cancel_job()' - Cancel a print job.
 */

static void
cancel_job(cupsd_client_t  *con,	/* I - Client connection */
	   ipp_attribute_t *uri)	/* I - Job or Printer URI */
{
  ipp_attribute_t *attr;		/* Current attribute */
  int		jobid;			/* Job ID */
  char		scheme[HTTP_MAX_URI],	/* Scheme portion of URI */
		username[HTTP_MAX_URI],	/* Username portion of URI */
		host[HTTP_MAX_URI],	/* Host portion of URI */
		resource[HTTP_MAX_URI];	/* Resource portion of URI */
  int		port;			/* Port portion of URI */
  cupsd_job_t	*job;			/* Job information */
  cups_ptype_t	dtype;			/* Destination type (printer/class) */
  cupsd_printer_t *printer;		/* Printer data */
  cupsd_jobaction_t purge;		/* Purge the job? */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "cancel_job(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * See if we have a job URI or a printer URI...
  */

  if (!strcmp(uri->name, "printer-uri"))
  {
   /*
    * Got a printer URI; see if we also have a job-id attribute...
    */

    if ((attr = ippFindAttribute(con->request, "job-id",
                                 IPP_TAG_INTEGER)) == NULL)
    {
      send_ipp_status(con, IPP_BAD_REQUEST,
                      _("Got a printer-uri attribute but no job-id."));
      return;
    }

    if ((jobid = attr->values[0].integer) == 0)
    {
     /*
      * Find the current job on the specified printer...
      */

      if (!cupsdValidateDest(uri->values[0].string.text, &dtype, &printer))
      {
       /*
	* Bad URI...
	*/

	send_ipp_status(con, IPP_NOT_FOUND,
                	_("The printer or class does not exist."));
	return;
      }

     /*
      * See if there are any pending jobs...
      */

      for (job = (cupsd_job_t *)cupsArrayFirst(ActiveJobs);
	   job;
	   job = (cupsd_job_t *)cupsArrayNext(ActiveJobs))
	if (job->state_value <= IPP_JOB_PROCESSING &&
	    !_cups_strcasecmp(job->dest, printer->name))
	  break;

      if (job)
	jobid = job->id;
      else
      {
       /*
        * No, try stopped jobs...
	*/

	for (job = (cupsd_job_t *)cupsArrayFirst(ActiveJobs);
	     job;
	     job = (cupsd_job_t *)cupsArrayNext(ActiveJobs))
	  if (job->state_value == IPP_JOB_STOPPED &&
	      !_cups_strcasecmp(job->dest, printer->name))
	    break;

	if (job)
	  jobid = job->id;
	else
	{
	  send_ipp_status(con, IPP_NOT_POSSIBLE, _("No active jobs on %s."),
			  printer->name);
	  return;
	}
      }
    }
  }
  else
  {
   /*
    * Got a job URI; parse it to get the job ID...
    */

    httpSeparateURI(HTTP_URI_CODING_ALL, uri->values[0].string.text, scheme,
                    sizeof(scheme), username, sizeof(username), host,
		    sizeof(host), &port, resource, sizeof(resource));

    if (strncmp(resource, "/jobs/", 6))
    {
     /*
      * Not a valid URI!
      */

      send_ipp_status(con, IPP_BAD_REQUEST, _("Bad job-uri \"%s\"."),
                      uri->values[0].string.text);
      return;
    }

    jobid = atoi(resource + 6);
  }

 /*
  * Look for the "purge-job" attribute...
  */

  if ((attr = ippFindAttribute(con->request, "purge-job",
                               IPP_TAG_BOOLEAN)) != NULL)
    purge = attr->values[0].boolean ? CUPSD_JOB_PURGE : CUPSD_JOB_DEFAULT;
  else
    purge = CUPSD_JOB_DEFAULT;

 /*
  * See if the job exists...
  */

  if ((job = cupsdFindJob(jobid)) == NULL)
  {
   /*
    * Nope - return a "not found" error...
    */

    send_ipp_status(con, IPP_NOT_FOUND, _("Job #%d does not exist."), jobid);
    return;
  }

 /*
  * See if the job is owned by the requesting user...
  */

  if (!validate_user(job, con, job->username, username, sizeof(username)))
  {
    send_http_error(con, con->username[0] ? HTTP_FORBIDDEN : HTTP_UNAUTHORIZED,
                    cupsdFindDest(job->dest));
    return;
  }

 /*
  * See if the job is already completed, canceled, or aborted; if so,
  * we can't cancel...
  */

  if (job->state_value >= IPP_JOB_CANCELED && purge != CUPSD_JOB_PURGE)
  {
    switch (job->state_value)
    {
      case IPP_JOB_CANCELED :
	  send_ipp_status(con, IPP_NOT_POSSIBLE,
                	  _("Job #%d is already canceled - can\'t cancel."),
			  jobid);
          break;

      case IPP_JOB_ABORTED :
	  send_ipp_status(con, IPP_NOT_POSSIBLE,
                	  _("Job #%d is already aborted - can\'t cancel."),
			  jobid);
          break;

      default :
	  send_ipp_status(con, IPP_NOT_POSSIBLE,
                	  _("Job #%d is already completed - can\'t cancel."),
			  jobid);
          break;
    }

    return;
  }

 /*
  * Cancel the job and return...
  */

  cupsdSetJobState(job, IPP_JOB_CANCELED, purge,
                   purge == CUPSD_JOB_PURGE ? "Job purged by \"%s\"" :
		                              "Job canceled by \"%s\"",
		   username);
  cupsdCheckJobs();

  if (purge == CUPSD_JOB_PURGE)
    cupsdLogMessage(CUPSD_LOG_INFO, "[Job %d] Purged by \"%s\".", jobid,
		    username);
  else
    cupsdLogMessage(CUPSD_LOG_INFO, "[Job %d] Canceled by \"%s\".", jobid,
		    username);

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'cancel_subscription()' - Cancel a subscription.
 */

static void
cancel_subscription(
    cupsd_client_t *con,		/* I - Client connection */
    int            sub_id)		/* I - Subscription ID */
{
  http_status_t		status;		/* Policy status */
  cupsd_subscription_t	*sub;		/* Subscription */


  cupsdLogMessage(CUPSD_LOG_DEBUG2,
                  "cancel_subscription(con=%p[%d], sub_id=%d)",
                  (void *)con, con->number, sub_id);

 /*
  * Is the subscription ID valid?
  */

  if ((sub = cupsdFindSubscription(sub_id)) == NULL)
  {
   /*
    * Bad subscription ID...
    */

    send_ipp_status(con, IPP_NOT_FOUND,
                    _("Subscription #%d does not exist."), sub_id);
    return;
  }

 /*
  * Check policy...
  */

  if ((status = cupsdCheckPolicy(sub->dest ? sub->dest->op_policy_ptr :
                                             DefaultPolicyPtr,
                                 con, sub->owner)) != HTTP_OK)
  {
    send_http_error(con, status, sub->dest);
    return;
  }

 /*
  * Cancel the subscription...
  */

  cupsdDeleteSubscription(sub, 1);

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'check_rss_recipient()' - Check that we do not have a duplicate RSS feed URI.
 */

static int				/* O - 1 if OK, 0 if not */
check_rss_recipient(
    const char *recipient)		/* I - Recipient URI */
{
  cupsd_subscription_t	*sub;		/* Current subscription */


  for (sub = (cupsd_subscription_t *)cupsArrayFirst(Subscriptions);
       sub;
       sub = (cupsd_subscription_t *)cupsArrayNext(Subscriptions))
    if (sub->recipient)
    {
     /*
      * Compare the URIs up to the first ?...
      */

      const char *r1, *r2;

      for (r1 = recipient, r2 = sub->recipient;
           *r1 == *r2 && *r1 && *r1 != '?' && *r2 && *r2 != '?';
	   r1 ++, r2 ++);

      if (*r1 == *r2)
        return (0);
    }

  return (1);
}


/*
 * 'check_quotas()' - Check quotas for a printer and user.
 */

static int				/* O - 1 if OK, 0 if forbidden,
					       -1 if limit reached */
check_quotas(cupsd_client_t  *con,	/* I - Client connection */
             cupsd_printer_t *p)	/* I - Printer or class */
{
  char		username[33],		/* Username */
		*name;			/* Current user name */
  cupsd_quota_t	*q;			/* Quota data */
#ifdef HAVE_MBR_UID_TO_UUID
 /*
  * Use Apple membership APIs which require that all names represent
  * valid user account or group records accessible by the server.
  */

  uuid_t	usr_uuid;		/* UUID for job requesting user  */
  uuid_t	usr2_uuid;		/* UUID for ACL user name entry  */
  uuid_t	grp_uuid;		/* UUID for ACL group name entry */
  int		mbr_err;		/* Error from membership function */
  int		is_member;		/* Is this user a member? */
#else
 /*
  * Use standard POSIX APIs for checking users and groups...
  */

  struct passwd	*pw;			/* User password data */
#endif /* HAVE_MBR_UID_TO_UUID */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "check_quotas(%p[%d], %p[%s])",
                  (void *)con, con->number, (void *)p, p->name);

 /*
  * Figure out who is printing...
  */

  strlcpy(username, get_username(con), sizeof(username));

  if ((name = strchr(username, '@')) != NULL)
    *name = '\0';			/* Strip @REALM */

 /*
  * Check global active job limits for printers and users...
  */

  if (MaxJobsPerPrinter)
  {
   /*
    * Check if there are too many pending jobs on this printer...
    */

    if (cupsdGetPrinterJobCount(p->name) >= MaxJobsPerPrinter)
    {
      cupsdLogMessage(CUPSD_LOG_INFO, "Too many jobs for printer \"%s\"...",
                      p->name);
      return (-1);
    }
  }

  if (MaxJobsPerUser)
  {
   /*
    * Check if there are too many pending jobs for this user...
    */

    if (cupsdGetUserJobCount(username) >= MaxJobsPerUser)
    {
      cupsdLogMessage(CUPSD_LOG_INFO, "Too many jobs for user \"%s\"...",
                      username);
      return (-1);
    }
  }

 /*
  * Check against users...
  */

  if (cupsArrayCount(p->users) == 0 && p->k_limit == 0 && p->page_limit == 0)
    return (1);

  if (cupsArrayCount(p->users))
  {
#ifdef HAVE_MBR_UID_TO_UUID
   /*
    * Get UUID for job requesting user...
    */

    if (mbr_user_name_to_uuid((char *)username, usr_uuid))
    {
     /*
      * Unknown user...
      */

      cupsdLogMessage(CUPSD_LOG_DEBUG,
		      "check_quotas: UUID lookup failed for user \"%s\"",
		      username);
      cupsdLogMessage(CUPSD_LOG_INFO,
		      "Denying user \"%s\" access to printer \"%s\" "
		      "(unknown user)...",
		      username, p->name);
      return (0);
    }
#else
   /*
    * Get UID and GID of requesting user...
    */

    pw = getpwnam(username);
    endpwent();
#endif /* HAVE_MBR_UID_TO_UUID */

    for (name = (char *)cupsArrayFirst(p->users);
         name;
	 name = (char *)cupsArrayNext(p->users))
      if (name[0] == '@')
      {
       /*
        * Check group membership...
	*/

#ifdef HAVE_MBR_UID_TO_UUID
        if (name[1] == '#')
	{
	  if (uuid_parse(name + 2, grp_uuid))
	    uuid_clear(grp_uuid);
	}
	else if ((mbr_err = mbr_group_name_to_uuid(name + 1, grp_uuid)) != 0)
	{
	 /*
	  * Invalid ACL entries are ignored for matching; just record a
	  * warning in the log...
	  */

	  cupsdLogMessage(CUPSD_LOG_DEBUG,
	                  "check_quotas: UUID lookup failed for ACL entry "
			  "\"%s\" (err=%d)", name, mbr_err);
	  cupsdLogMessage(CUPSD_LOG_WARN,
	                  "Access control entry \"%s\" not a valid group name; "
			  "entry ignored", name);
	}

	if ((mbr_err = mbr_check_membership(usr_uuid, grp_uuid,
					    &is_member)) != 0)
	{
	 /*
	  * At this point, there should be no errors, but check anyways...
	  */

	  cupsdLogMessage(CUPSD_LOG_DEBUG,
			  "check_quotas: group \"%s\" membership check "
			  "failed (err=%d)", name + 1, mbr_err);
	  is_member = 0;
	}

       /*
	* Stop if we found a match...
	*/

	if (is_member)
	  break;

#else
        if (cupsdCheckGroup(username, pw, name + 1))
	  break;
#endif /* HAVE_MBR_UID_TO_UUID */
      }
#ifdef HAVE_MBR_UID_TO_UUID
      else
      {
        if (name[0] == '#')
	{
	  if (uuid_parse(name + 1, usr2_uuid))
	    uuid_clear(usr2_uuid);
        }
        else if ((mbr_err = mbr_user_name_to_uuid(name, usr2_uuid)) != 0)
    	{
	 /*
	  * Invalid ACL entries are ignored for matching; just record a
	  * warning in the log...
	  */

          cupsdLogMessage(CUPSD_LOG_DEBUG,
	                  "check_quotas: UUID lookup failed for ACL entry "
			  "\"%s\" (err=%d)", name, mbr_err);
          cupsdLogMessage(CUPSD_LOG_WARN,
	                  "Access control entry \"%s\" not a valid user name; "
			  "entry ignored", name);
	}

	if (!uuid_compare(usr_uuid, usr2_uuid))
	  break;
      }
#else
      else if (!_cups_strcasecmp(username, name))
	break;
#endif /* HAVE_MBR_UID_TO_UUID */

    if ((name != NULL) == p->deny_users)
    {
      cupsdLogMessage(CUPSD_LOG_INFO,
                      "Denying user \"%s\" access to printer \"%s\"...",
        	      username, p->name);
      return (0);
    }
  }

 /*
  * Check quotas...
  */

  if (p->k_limit || p->page_limit)
  {
    if ((q = cupsdUpdateQuota(p, username, 0, 0)) == NULL)
    {
      cupsdLogMessage(CUPSD_LOG_ERROR,
                      "Unable to allocate quota data for user \"%s\"",
                      username);
      return (-1);
    }

    if ((q->k_count >= p->k_limit && p->k_limit) ||
        (q->page_count >= p->page_limit && p->page_limit))
    {
      cupsdLogMessage(CUPSD_LOG_INFO, "User \"%s\" is over the quota limit...",
                      username);
      return (-1);
    }
  }

 /*
  * If we have gotten this far, we're done!
  */

  return (1);
}


/*
 * 'close_job()' - Close a multi-file job.
 */

static void
close_job(cupsd_client_t  *con,		/* I - Client connection */
          ipp_attribute_t *uri)		/* I - Printer URI */
{
  cupsd_job_t		*job;		/* Job */
  ipp_attribute_t	*attr;		/* Attribute */
  char			job_uri[HTTP_MAX_URI],
					/* Job URI */
			username[256];	/* User name */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "close_job(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * See if we have a job URI or a printer URI...
  */

  if (strcmp(uri->name, "printer-uri"))
  {
   /*
    * job-uri is not supported by Close-Job!
    */

    send_ipp_status(con, IPP_BAD_REQUEST,
		    _("Close-Job doesn't support the job-uri attribute."));
    return;
  }

 /*
  * Got a printer URI; see if we also have a job-id attribute...
  */

  if ((attr = ippFindAttribute(con->request, "job-id",
			       IPP_TAG_INTEGER)) == NULL)
  {
    send_ipp_status(con, IPP_BAD_REQUEST,
		    _("Got a printer-uri attribute but no job-id."));
    return;
  }

  if ((job = cupsdFindJob(attr->values[0].integer)) == NULL)
  {
   /*
    * Nope - return a "not found" error...
    */

    send_ipp_status(con, IPP_NOT_FOUND, _("Job #%d does not exist."),
                    attr->values[0].integer);
    return;
  }

 /*
  * See if the job is owned by the requesting user...
  */

  if (!validate_user(job, con, job->username, username, sizeof(username)))
  {
    send_http_error(con, con->username[0] ? HTTP_FORBIDDEN : HTTP_UNAUTHORIZED,
                    cupsdFindDest(job->dest));
    return;
  }

 /*
  * Add any ending sheet...
  */

  if (cupsdTimeoutJob(job))
    return;

  if (job->state_value == IPP_JOB_STOPPED)
  {
    job->state->values[0].integer = IPP_JOB_PENDING;
    job->state_value              = IPP_JOB_PENDING;
  }
  else if (job->state_value == IPP_JOB_HELD)
  {
    if ((attr = ippFindAttribute(job->attrs, "job-hold-until",
				 IPP_TAG_KEYWORD)) == NULL)
      attr = ippFindAttribute(job->attrs, "job-hold-until", IPP_TAG_NAME);

    if (!attr || !strcmp(attr->values[0].string.text, "no-hold"))
    {
      job->state->values[0].integer = IPP_JOB_PENDING;
      job->state_value              = IPP_JOB_PENDING;
    }
  }

  job->dirty = 1;
  cupsdMarkDirty(CUPSD_DIRTY_JOBS);

 /*
  * Fill in the response info...
  */

  httpAssembleURIf(HTTP_URI_CODING_ALL, job_uri, sizeof(job_uri), "ipp", NULL,
                   con->clientname, con->clientport, "/jobs/%d", job->id);
  ippAddString(con->response, IPP_TAG_JOB, IPP_TAG_URI, "job-uri", NULL,
               job_uri);

  ippAddInteger(con->response, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-id", job->id);

  ippAddInteger(con->response, IPP_TAG_JOB, IPP_TAG_ENUM, "job-state", (int)job->state_value);

  con->response->request.status.status_code = IPP_OK;

 /*
  * Start the job if necessary...
  */

  cupsdCheckJobs();
}


/*
 * 'copy_attrs()' - Copy attributes from one request to another.
 */

static void
copy_attrs(ipp_t        *to,		/* I - Destination request */
           ipp_t        *from,		/* I - Source request */
           cups_array_t *ra,		/* I - Requested attributes */
	   ipp_tag_t    group,		/* I - Group to copy */
	   int          quickcopy,	/* I - Do a quick copy? */
	   cups_array_t *exclude)	/* I - Attributes to exclude? */
{
  ipp_attribute_t	*fromattr;	/* Source attribute */


  cupsdLogMessage(CUPSD_LOG_DEBUG2,
                  "copy_attrs(to=%p, from=%p, ra=%p, group=%x, quickcopy=%d)",
		  (void *)to, (void *)from, (void *)ra, group, quickcopy);

  if (!to || !from)
    return;

  for (fromattr = from->attrs; fromattr; fromattr = fromattr->next)
  {
   /*
    * Filter attributes as needed...
    */

    if ((group != IPP_TAG_ZERO && fromattr->group_tag != group &&
         fromattr->group_tag != IPP_TAG_ZERO) || !fromattr->name)
      continue;

    if (!strcmp(fromattr->name, "document-password") ||
        !strcmp(fromattr->name, "job-authorization-uri") ||
        !strcmp(fromattr->name, "job-password") ||
        !strcmp(fromattr->name, "job-password-encryption") ||
        !strcmp(fromattr->name, "job-printer-uri"))
      continue;

    if (exclude &&
        (cupsArrayFind(exclude, fromattr->name) ||
	 cupsArrayFind(exclude, "all")))
    {
     /*
      * We need to exclude this attribute for security reasons; we require the
      * job-id attribute regardless of the security settings for IPP
      * conformance.
      *
      * The job-printer-uri attribute is handled by copy_job_attrs().
      *
      * Subscription attribute security is handled by copy_subscription_attrs().
      */

      if (strcmp(fromattr->name, "job-id"))
        continue;
    }

    if (!ra || cupsArrayFind(ra, fromattr->name))
    {
     /*
      * Don't send collection attributes by default to IPP/1.x clients
      * since many do not support collections.  Also don't send
      * media-col-database unless specifically requested by the client.
      */

      if (fromattr->value_tag == IPP_TAG_BEGIN_COLLECTION &&
          !ra &&
	  (to->request.status.version[0] == 1 ||
	   !strcmp(fromattr->name, "media-col-database")))
	continue;

      ippCopyAttribute(to, fromattr, quickcopy);
    }
  }
}


/*
 * 'copy_banner()' - Copy a banner file to the requests directory for the
 *                   specified job.
 */

static int				/* O - Size of banner file in kbytes */
copy_banner(cupsd_client_t *con,	/* I - Client connection */
            cupsd_job_t    *job,	/* I - Job information */
            const char     *name)	/* I - Name of banner */
{
  int		i;			/* Looping var */
  int		kbytes;			/* Size of banner file in kbytes */
  char		filename[1024];		/* Job filename */
  cupsd_banner_t *banner;		/* Pointer to banner */
  cups_file_t	*in;			/* Input file */
  cups_file_t	*out;			/* Output file */
  int		ch;			/* Character from file */
  char		attrname[255],		/* Name of attribute */
		*s;			/* Pointer into name */
  ipp_attribute_t *attr;		/* Attribute */


  cupsdLogMessage(CUPSD_LOG_DEBUG2,
                  "copy_banner(con=%p[%d], job=%p[%d], name=\"%s\")",
                  (void *)con, con ? con->number : -1, (void *)job, job->id,
		  name ? name : "(null)");

 /*
  * Find the banner; return if not found or "none"...
  */

  if (!name || !strcmp(name, "none") ||
      (banner = cupsdFindBanner(name)) == NULL)
    return (0);

 /*
  * Open the banner and job files...
  */

  if (add_file(con, job, banner->filetype, 0))
    return (-1);

  snprintf(filename, sizeof(filename), "%s/d%05d-%03d", RequestRoot, job->id,
           job->num_files);
  if ((out = cupsFileOpen(filename, "w")) == NULL)
  {
    cupsdLogMessage(CUPSD_LOG_ERROR,
                    "Unable to create banner job file %s - %s",
                    filename, strerror(errno));
    job->num_files --;
    return (0);
  }

  fchmod(cupsFileNumber(out), 0640);
  fchown(cupsFileNumber(out), RunUser, Group);

 /*
  * Try the localized banner file under the subdirectory...
  */

  strlcpy(attrname, job->attrs->attrs->next->values[0].string.text,
          sizeof(attrname));
  if (strlen(attrname) > 2 && attrname[2] == '-')
  {
   /*
    * Convert ll-cc to ll_CC...
    */

    attrname[2] = '_';
    attrname[3] = (char)toupper(attrname[3] & 255);
    attrname[4] = (char)toupper(attrname[4] & 255);
  }

  snprintf(filename, sizeof(filename), "%s/banners/%s/%s", DataDir,
           attrname, name);

  if (access(filename, 0) && strlen(attrname) > 2)
  {
   /*
    * Wasn't able to find "ll_CC" locale file; try the non-national
    * localization banner directory.
    */

    attrname[2] = '\0';

    snprintf(filename, sizeof(filename), "%s/banners/%s/%s", DataDir,
             attrname, name);
  }

  if (access(filename, 0))
  {
   /*
    * Use the non-localized banner file.
    */

    snprintf(filename, sizeof(filename), "%s/banners/%s", DataDir, name);
  }

  if ((in = cupsFileOpen(filename, "r")) == NULL)
  {
    cupsFileClose(out);
    unlink(filename);
    cupsdLogMessage(CUPSD_LOG_ERROR,
                    "Unable to open banner template file %s - %s",
                    filename, strerror(errno));
    job->num_files --;
    return (0);
  }

 /*
  * Parse the file to the end...
  */

  while ((ch = cupsFileGetChar(in)) != EOF)
    if (ch == '{')
    {
     /*
      * Get an attribute name...
      */

      for (s = attrname; (ch = cupsFileGetChar(in)) != EOF;)
        if (!isalpha(ch & 255) && ch != '-' && ch != '?')
          break;
	else if (s < (attrname + sizeof(attrname) - 1))
          *s++ = (char)ch;
	else
	  break;

      *s = '\0';

      if (ch != '}')
      {
       /*
        * Ignore { followed by stuff that is not an attribute name...
	*/

        cupsFilePrintf(out, "{%s%c", attrname, ch);
	continue;
      }

     /*
      * See if it is defined...
      */

      if (attrname[0] == '?')
        s = attrname + 1;
      else
        s = attrname;

      if (!strcmp(s, "printer-name"))
      {
        cupsFilePuts(out, job->dest);
	continue;
      }
      else if ((attr = ippFindAttribute(job->attrs, s, IPP_TAG_ZERO)) == NULL)
      {
       /*
        * See if we have a leading question mark...
	*/

	if (attrname[0] != '?')
	{
	 /*
          * Nope, write to file as-is; probably a PostScript procedure...
	  */

	  cupsFilePrintf(out, "{%s}", attrname);
        }

        continue;
      }

     /*
      * Output value(s)...
      */

      for (i = 0; i < attr->num_values; i ++)
      {
	if (i)
	  cupsFilePutChar(out, ',');

	switch (attr->value_tag)
	{
	  case IPP_TAG_INTEGER :
	  case IPP_TAG_ENUM :
	      if (!strncmp(s, "time-at-", 8))
	      {
	        struct timeval tv;	/* Time value */

		tv.tv_sec  = attr->values[i].integer;
		tv.tv_usec = 0;

	        cupsFilePuts(out, cupsdGetDateTime(&tv, CUPSD_TIME_STANDARD));
	      }
	      else
	        cupsFilePrintf(out, "%d", attr->values[i].integer);
	      break;

	  case IPP_TAG_BOOLEAN :
	      cupsFilePrintf(out, "%d", attr->values[i].boolean);
	      break;

	  case IPP_TAG_NOVALUE :
	      cupsFilePuts(out, "novalue");
	      break;

	  case IPP_TAG_RANGE :
	      cupsFilePrintf(out, "%d-%d", attr->values[i].range.lower,
		      attr->values[i].range.upper);
	      break;

	  case IPP_TAG_RESOLUTION :
	      cupsFilePrintf(out, "%dx%d%s", attr->values[i].resolution.xres,
		      attr->values[i].resolution.yres,
		      attr->values[i].resolution.units == IPP_RES_PER_INCH ?
			  "dpi" : "dpcm");
	      break;

	  case IPP_TAG_URI :
          case IPP_TAG_STRING :
	  case IPP_TAG_TEXT :
	  case IPP_TAG_NAME :
	  case IPP_TAG_KEYWORD :
	  case IPP_TAG_CHARSET :
	  case IPP_TAG_LANGUAGE :
	      if (!_cups_strcasecmp(banner->filetype->type, "postscript"))
	      {
	       /*
	        * Need to quote strings for PS banners...
		*/

	        const char *p;

		for (p = attr->values[i].string.text; *p; p ++)
		{
		  if (*p == '(' || *p == ')' || *p == '\\')
		  {
		    cupsFilePutChar(out, '\\');
		    cupsFilePutChar(out, *p);
		  }
		  else if (*p < 32 || *p > 126)
		    cupsFilePrintf(out, "\\%03o", *p & 255);
		  else
		    cupsFilePutChar(out, *p);
		}
	      }
	      else
		cupsFilePuts(out, attr->values[i].string.text);
	      break;

          default :
	      break; /* anti-compiler-warning-code */
	}
      }
    }
    else if (ch == '\\')	/* Quoted char */
    {
      ch = cupsFileGetChar(in);

      if (ch != '{')		/* Only do special handling for \{ */
        cupsFilePutChar(out, '\\');

      cupsFilePutChar(out, ch);
    }
    else
      cupsFilePutChar(out, ch);

  cupsFileClose(in);

  kbytes = (cupsFileTell(out) + 1023) / 1024;

  job->koctets += kbytes;

  if ((attr = ippFindAttribute(job->attrs, "job-k-octets", IPP_TAG_INTEGER)) != NULL)
    attr->values[0].integer += kbytes;

  cupsFileClose(out);

  return (kbytes);
}


/*
 * 'copy_file()' - Copy a PPD file...
 */

static int				/* O - 0 = success, -1 = error */
copy_file(const char *from,		/* I - Source file */
          const char *to,		/* I - Destination file */
	  mode_t     mode)		/* I - Permissions */
{
  cups_file_t	*src,			/* Source file */
		*dst;			/* Destination file */
  int		bytes;			/* Bytes to read/write */
  char		buffer[2048];		/* Copy buffer */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "copy_file(\"%s\", \"%s\")", from, to);

 /*
  * Open the source and destination file for a copy...
  */

  if ((src = cupsFileOpen(from, "rb")) == NULL)
    return (-1);

  if ((dst = cupsdCreateConfFile(to, mode)) == NULL)
  {
    cupsFileClose(src);
    return (-1);
  }

 /*
  * Copy the source file to the destination...
  */

  while ((bytes = cupsFileRead(src, buffer, sizeof(buffer))) > 0)
    if (cupsFileWrite(dst, buffer, (size_t)bytes) < bytes)
    {
      cupsFileClose(src);
      cupsFileClose(dst);
      return (-1);
    }

 /*
  * Close both files and return...
  */

  cupsFileClose(src);

  return (cupsdCloseCreatedConfFile(dst, to));
}


/*
 * 'copy_model()' - Copy a PPD model file, substituting default values
 *                  as needed...
 */

static int				/* O - 0 = success, -1 = error */
copy_model(cupsd_client_t *con,		/* I - Client connection */
           const char     *from,	/* I - Source file */
           const char     *to)		/* I - Destination file */
{
  fd_set	input;			/* select() input set */
  struct timeval timeout;		/* select() timeout */
  int		maxfd;			/* Max file descriptor for select() */
  char		tempfile[1024];		/* Temporary PPD file */
  int		tempfd;			/* Temporary PPD file descriptor */
  int		temppid;		/* Process ID of cups-driverd */
  int		temppipe[2];		/* Temporary pipes */
  char		*argv[4],		/* Command-line arguments */
		*envp[MAX_ENV];		/* Environment */
  cups_file_t	*src,			/* Source file */
		*dst;			/* Destination file */
  ppd_file_t	*ppd;			/* PPD file */
  int		bytes,			/* Bytes from pipe */
		total;			/* Total bytes from pipe */
  char		buffer[2048];		/* Copy buffer */
  int		i;			/* Looping var */
  char		option[PPD_MAX_NAME],	/* Option name */
		choice[PPD_MAX_NAME];	/* Choice name */
  ppd_size_t	*size;			/* Default size */
  int		num_defaults;		/* Number of default options */
  cups_option_t	*defaults;		/* Default options */
  char		cups_protocol[PPD_MAX_LINE];
					/* cupsProtocol attribute */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "copy_model(con=%p, from=\"%s\", to=\"%s\")", (void *)con, from, to);

 /*
  * Run cups-driverd to get the PPD file...
  */

  argv[0] = "cups-driverd";
  argv[1] = "cat";
  argv[2] = (char *)from;
  argv[3] = NULL;

  cupsdLoadEnv(envp, (int)(sizeof(envp) / sizeof(envp[0])));

  snprintf(buffer, sizeof(buffer), "%s/daemon/cups-driverd", ServerBin);
  snprintf(tempfile, sizeof(tempfile), "%s/%d.ppd", TempDir, con->number);
  if ((tempfd = open(tempfile, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0)
    return (-1);
  if (cupsdOpenPipe(temppipe))
  {
    close(tempfd);
    unlink(tempfile);

    return (-1);
  }

  cupsdLogMessage(CUPSD_LOG_DEBUG,
                  "copy_model: Running \"cups-driverd cat %s\"...", from);

  if (!cupsdStartProcess(buffer, argv, envp, -1, temppipe[1], CGIPipes[1],
                         -1, -1, 0, DefaultProfile, NULL, &temppid))
  {
    send_ipp_status(con, IPP_INTERNAL_ERROR, _("Unable to run cups-driverd: %s"), strerror(errno));
    close(tempfd);
    unlink(tempfile);

    return (-1);
  }

  close(temppipe[1]);

 /*
  * Wait up to 30 seconds for the PPD file to be copied...
  */

  total = 0;

  if (temppipe[0] > CGIPipes[0])
    maxfd = temppipe[0] + 1;
  else
    maxfd = CGIPipes[0] + 1;

  for (;;)
  {
   /*
    * See if we have data ready...
    */

    FD_ZERO(&input);
    FD_SET(temppipe[0], &input);
    FD_SET(CGIPipes[0], &input);

    timeout.tv_sec  = 30;
    timeout.tv_usec = 0;

    if ((i = select(maxfd, &input, NULL, NULL, &timeout)) < 0)
    {
      if (errno == EINTR)
        continue;
      else
        break;
    }
    else if (i == 0)
    {
     /*
      * We have timed out...
      */

      break;
    }

    if (FD_ISSET(temppipe[0], &input))
    {
     /*
      * Read the PPD file from the pipe, and write it to the PPD file.
      */

      if ((bytes = read(temppipe[0], buffer, sizeof(buffer))) > 0)
      {
	if (write(tempfd, buffer, (size_t)bytes) < bytes)
          break;

	total += bytes;
      }
      else
	break;
    }

    if (FD_ISSET(CGIPipes[0], &input))
      cupsdUpdateCGI();
  }

  close(temppipe[0]);
  close(tempfd);

  if (!total)
  {
   /*
    * No data from cups-deviced...
    */

    cupsdLogMessage(CUPSD_LOG_ERROR, "copy_model: empty PPD file");
    send_ipp_status(con, IPP_INTERNAL_ERROR, _("cups-driverd failed to get PPD file - see error_log for details."));
    unlink(tempfile);
    return (-1);
  }

 /*
  * Open the source file for a copy...
  */

  if ((src = cupsFileOpen(tempfile, "rb")) == NULL)
  {
    unlink(tempfile);
    return (-1);
  }

 /*
  * Read the source file and see what page sizes are supported...
  */

  if ((ppd = _ppdOpen(src, _PPD_LOCALIZATION_NONE)) == NULL)
  {
    cupsFileClose(src);
    unlink(tempfile);
    return (-1);
  }

 /*
  * Open the destination (if possible) and set the default options...
  */

  num_defaults     = 0;
  defaults         = NULL;
  cups_protocol[0] = '\0';

  if ((dst = cupsFileOpen(to, "rb")) != NULL)
  {
   /*
    * Read all of the default lines from the old PPD...
    */

    while (cupsFileGets(dst, buffer, sizeof(buffer)))
      if (!strncmp(buffer, "*Default", 8))
      {
       /*
	* Add the default option...
	*/

        if (!ppd_parse_line(buffer, option, sizeof(option),
	                    choice, sizeof(choice)))
        {
	  ppd_option_t	*ppdo;		/* PPD option */


         /*
	  * Only add the default if the default hasn't already been
	  * set and the choice exists in the new PPD...
	  */

	  if (!cupsGetOption(option, num_defaults, defaults) &&
	      (ppdo = ppdFindOption(ppd, option)) != NULL &&
	      ppdFindChoice(ppdo, choice))
            num_defaults = cupsAddOption(option, choice, num_defaults,
	                                 &defaults);
        }
      }
      else if (!strncmp(buffer, "*cupsProtocol:", 14))
        strlcpy(cups_protocol, buffer, sizeof(cups_protocol));

    cupsFileClose(dst);
  }
  else if ((size = ppdPageSize(ppd, DefaultPaperSize)) != NULL)
  {
   /*
    * Add the default media sizes...
    */

    num_defaults = cupsAddOption("PageSize", size->name,
                                 num_defaults, &defaults);
    num_defaults = cupsAddOption("PageRegion", size->name,
                                 num_defaults, &defaults);
    num_defaults = cupsAddOption("PaperDimension", size->name,
                                 num_defaults, &defaults);
    num_defaults = cupsAddOption("ImageableArea", size->name,
                                 num_defaults, &defaults);
  }

  ppdClose(ppd);

 /*
  * Open the destination file for a copy...
  */

  if ((dst = cupsdCreateConfFile(to, ConfigFilePerm)) == NULL)
  {
    send_ipp_status(con, IPP_INTERNAL_ERROR, _("Unable to save PPD file: %s"), strerror(errno));
    cupsFreeOptions(num_defaults, defaults);
    cupsFileClose(src);
    unlink(tempfile);
    return (-1);
  }

 /*
  * Copy the source file to the destination...
  */

  cupsFileRewind(src);

  while (cupsFileGets(src, buffer, sizeof(buffer)))
  {
    if (!strncmp(buffer, "*Default", 8))
    {
     /*
      * Check for an previous default option choice...
      */

      if (!ppd_parse_line(buffer, option, sizeof(option),
	                  choice, sizeof(choice)))
      {
        const char	*val;		/* Default option value */


        if ((val = cupsGetOption(option, num_defaults, defaults)) != NULL)
	{
	 /*
	  * Substitute the previous choice...
	  */

	  snprintf(buffer, sizeof(buffer), "*Default%s: %s", option, val);
	}
      }
    }

    cupsFilePrintf(dst, "%s\n", buffer);
  }

  if (cups_protocol[0])
    cupsFilePrintf(dst, "%s\n", cups_protocol);

  cupsFreeOptions(num_defaults, defaults);

 /*
  * Close both files and return...
  */

  cupsFileClose(src);

  unlink(tempfile);

  if (cupsdCloseCreatedConfFile(dst, to))
  {
    send_ipp_status(con, IPP_INTERNAL_ERROR, _("Unable to commit PPD file: %s"), strerror(errno));
    return (-1);
  }
  else
  {
    return (0);
  }
}


/*
 * 'copy_job_attrs()' - Copy job attributes.
 */

static void
copy_job_attrs(cupsd_client_t *con,	/* I - Client connection */
	       cupsd_job_t    *job,	/* I - Job */
	       cups_array_t   *ra,	/* I - Requested attributes array */
	       cups_array_t   *exclude)	/* I - Private attributes array */
{
  char	job_uri[HTTP_MAX_URI];		/* Job URI */


 /*
  * Send the requested attributes for each job...
  */

  if (!cupsArrayFind(exclude, "all"))
  {
    if ((!exclude || !cupsArrayFind(exclude, "number-of-documents")) &&
        (!ra || cupsArrayFind(ra, "number-of-documents")))
      ippAddInteger(con->response, IPP_TAG_JOB, IPP_TAG_INTEGER,
		    "number-of-documents", job->num_files);

    if ((!exclude || !cupsArrayFind(exclude, "job-media-progress")) &&
        (!ra || cupsArrayFind(ra, "job-media-progress")))
      ippAddInteger(con->response, IPP_TAG_JOB, IPP_TAG_INTEGER,
		    "job-media-progress", job->progress);

    if ((!exclude || !cupsArrayFind(exclude, "job-more-info")) &&
        (!ra || cupsArrayFind(ra, "job-more-info")))
    {
      httpAssembleURIf(HTTP_URI_CODING_ALL, job_uri, sizeof(job_uri), "http",
                       NULL, con->clientname, con->clientport, "/jobs/%d",
		       job->id);
      ippAddString(con->response, IPP_TAG_JOB, IPP_TAG_URI,
		   "job-more-info", NULL, job_uri);
    }

    if (job->state_value > IPP_JOB_PROCESSING &&
	(!exclude || !cupsArrayFind(exclude, "job-preserved")) &&
        (!ra || cupsArrayFind(ra, "job-preserved")))
      ippAddBoolean(con->response, IPP_TAG_JOB, "job-preserved",
		    job->num_files > 0);

    if ((!exclude || !cupsArrayFind(exclude, "job-printer-up-time")) &&
        (!ra || cupsArrayFind(ra, "job-printer-up-time")))
      ippAddInteger(con->response, IPP_TAG_JOB, IPP_TAG_INTEGER,
		    "job-printer-up-time", time(NULL));
  }

  if (!ra || cupsArrayFind(ra, "job-printer-uri"))
  {
    httpAssembleURIf(HTTP_URI_CODING_ALL, job_uri, sizeof(job_uri), "ipp", NULL,
		     con->clientname, con->clientport,
		     (job->dtype & CUPS_PRINTER_CLASS) ? "/classes/%s" :
		                                         "/printers/%s",
		     job->dest);
    ippAddString(con->response, IPP_TAG_JOB, IPP_TAG_URI,
        	 "job-printer-uri", NULL, job_uri);
  }

  if (!ra || cupsArrayFind(ra, "job-uri"))
  {
    httpAssembleURIf(HTTP_URI_CODING_ALL, job_uri, sizeof(job_uri), "ipp", NULL,
		     con->clientname, con->clientport, "/jobs/%d",
		     job->id);
    ippAddString(con->response, IPP_TAG_JOB, IPP_TAG_URI,
        	 "job-uri", NULL, job_uri);
  }

  if (job->attrs)
  {
    copy_attrs(con->response, job->attrs, ra, IPP_TAG_JOB, 0, exclude);
  }
  else
  {
   /*
    * Generate attributes from the job structure...
    */

    if (job->completed_time && (!ra || cupsArrayFind(ra, "date-time-at-completed")))
      ippAddDate(con->response, IPP_TAG_JOB, "date-time-at-completed", ippTimeToDate(job->completed_time));

    if (job->creation_time && (!ra || cupsArrayFind(ra, "date-time-at-creation")))
      ippAddDate(con->response, IPP_TAG_JOB, "date-time-at-creation", ippTimeToDate(job->creation_time));

    if (!ra || cupsArrayFind(ra, "job-id"))
      ippAddInteger(con->response, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-id", job->id);

    if (!ra || cupsArrayFind(ra, "job-k-octets"))
      ippAddInteger(con->response, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-k-octets", job->koctets);

    if (job->name && (!ra || cupsArrayFind(ra, "job-name")))
      ippAddString(con->response, IPP_TAG_JOB, IPP_TAG_NAME, "job-name", NULL, job->name);

    if (job->username && (!ra || cupsArrayFind(ra, "job-originating-user-name")))
      ippAddString(con->response, IPP_TAG_JOB, IPP_TAG_NAME, "job-originating-user-name", NULL, job->username);

    if (!ra || cupsArrayFind(ra, "job-state"))
      ippAddInteger(con->response, IPP_TAG_JOB, IPP_TAG_ENUM, "job-state", (int)job->state_value);

    if (!ra || cupsArrayFind(ra, "job-state-reasons"))
    {
      switch (job->state_value)
      {
        default : /* Should never get here for processing, pending, held, or stopped jobs since they don't get unloaded... */
	    break;
        case IPP_JSTATE_ABORTED :
	    ippAddString(con->response, IPP_TAG_JOB, IPP_TAG_KEYWORD, "job-state-reasons", NULL, "job-aborted-by-system");
	    break;
        case IPP_JSTATE_CANCELED :
	    ippAddString(con->response, IPP_TAG_JOB, IPP_TAG_KEYWORD, "job-state-reasons", NULL, "job-canceled-by-user");
	    break;
        case IPP_JSTATE_COMPLETED :
	    ippAddString(con->response, IPP_TAG_JOB, IPP_TAG_KEYWORD, "job-state-reasons", NULL, "job-completed-successfully");
	    break;
      }
    }

    if (job->completed_time && (!ra || cupsArrayFind(ra, "time-at-completed")))
      ippAddInteger(con->response, IPP_TAG_JOB, IPP_TAG_INTEGER, "time-at-completed", (int)job->completed_time);

    if (job->creation_time && (!ra || cupsArrayFind(ra, "time-at-creation")))
      ippAddInteger(con->response, IPP_TAG_JOB, IPP_TAG_INTEGER, "time-at-creation", (int)job->creation_time);
  }
}


/*
 * 'copy_printer_attrs()' - Copy printer attributes.
 */

static void
copy_printer_attrs(
    cupsd_client_t  *con,		/* I - Client connection */
    cupsd_printer_t *printer,		/* I - Printer */
    cups_array_t    *ra)		/* I - Requested attributes array */
{
  char		uri[HTTP_MAX_URI];	/* URI value */
  time_t	curtime;		/* Current time */
  int		i;			/* Looping var */
  int		is_encrypted = httpIsEncrypted(con->http);
					/* Is the connection encrypted? */


 /*
  * Copy the printer attributes to the response using requested-attributes
  * and document-format attributes that may be provided by the client.
  */

  _cupsRWLockRead(&printer->lock);

  curtime = time(NULL);

  if (!ra || cupsArrayFind(ra, "marker-change-time"))
    ippAddInteger(con->response, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "marker-change-time", printer->marker_time);

  if (printer->num_printers > 0 && (!ra || cupsArrayFind(ra, "member-uris")))
  {
    ipp_attribute_t	*member_uris;	/* member-uris attribute */
    cupsd_printer_t	*p2;		/* Printer in class */
    ipp_attribute_t	*p2_uri;	/* printer-uri-supported for class printer */


    if ((member_uris = ippAddStrings(con->response, IPP_TAG_PRINTER, IPP_TAG_URI, "member-uris", printer->num_printers, NULL, NULL)) != NULL)
    {
      for (i = 0; i < printer->num_printers; i ++)
      {
        p2 = printer->printers[i];

        if ((p2_uri = ippFindAttribute(p2->attrs, "printer-uri-supported", IPP_TAG_URI)) != NULL)
        {
          member_uris->values[i].string.text = _cupsStrAlloc(p2_uri->values[0].string.text);
        }
        else
	{
	  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), is_encrypted ? "ipps" : "ipp", NULL, con->clientname, con->clientport, (p2->type & CUPS_PRINTER_CLASS) ? "/classes/%s" : "/printers/%s", p2->name);
	  member_uris->values[i].string.text = _cupsStrAlloc(uri);
        }
      }
    }
  }

  if (printer->alert && (!ra || cupsArrayFind(ra, "printer-alert")))
    ippAddOctetString(con->response, IPP_TAG_PRINTER, "printer-alert", printer->alert, (int)strlen(printer->alert));

  if (printer->alert_description && (!ra || cupsArrayFind(ra, "printer-alert-description")))
    ippAddString(con->response, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-alert-description", NULL, printer->alert_description);

  if (!ra || cupsArrayFind(ra, "printer-config-change-date-time"))
    ippAddDate(con->response, IPP_TAG_PRINTER, "printer-config-change-date-time", ippTimeToDate(printer->config_time));

  if (!ra || cupsArrayFind(ra, "printer-config-change-time"))
    ippAddInteger(con->response, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "printer-config-change-time", printer->config_time);

  if (!ra || cupsArrayFind(ra, "printer-current-time"))
    ippAddDate(con->response, IPP_TAG_PRINTER, "printer-current-time", ippTimeToDate(curtime));

#ifdef HAVE_DNSSD
  if (!ra || cupsArrayFind(ra, "printer-dns-sd-name"))
  {
    if (printer->reg_name)
      ippAddString(con->response, IPP_TAG_PRINTER, IPP_TAG_NAME, "printer-dns-sd-name", NULL, printer->reg_name);
    else
      ippAddInteger(con->response, IPP_TAG_PRINTER, IPP_TAG_NOVALUE, "printer-dns-sd-name", 0);
  }
#endif /* HAVE_DNSSD */

  if (!ra || cupsArrayFind(ra, "printer-error-policy"))
    ippAddString(con->response, IPP_TAG_PRINTER, IPP_TAG_NAME, "printer-error-policy", NULL, printer->error_policy);

  if (!ra || cupsArrayFind(ra, "printer-error-policy-supported"))
  {
    static const char * const errors[] =/* printer-error-policy-supported values */
    {
      "abort-job",
      "retry-current-job",
      "retry-job",
      "stop-printer"
    };

    if (printer->type & CUPS_PRINTER_CLASS)
      ippAddString(con->response, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_NAME), "printer-error-policy-supported", NULL, "retry-current-job");
    else
      ippAddStrings(con->response, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_NAME), "printer-error-policy-supported", sizeof(errors) / sizeof(errors[0]), NULL, errors);
  }

  if (!ra || cupsArrayFind(ra, "printer-icons"))
  {
    httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), is_encrypted ? "https" : "http", NULL, con->clientname, con->clientport, "/icons/%s.png", printer->name);
    ippAddString(con->response, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-icons", NULL, uri);
    cupsdLogMessage(CUPSD_LOG_DEBUG2, "printer-icons=\"%s\"", uri);
  }

  if (!ra || cupsArrayFind(ra, "printer-is-accepting-jobs"))
    ippAddBoolean(con->response, IPP_TAG_PRINTER, "printer-is-accepting-jobs", (char)printer->accepting);

  if (!ra || cupsArrayFind(ra, "printer-is-shared"))
    ippAddBoolean(con->response, IPP_TAG_PRINTER, "printer-is-shared", (char)printer->shared);

  if (!ra || cupsArrayFind(ra, "printer-is-temporary"))
    ippAddBoolean(con->response, IPP_TAG_PRINTER, "printer-is-temporary", (char)printer->temporary);

  if (!ra || cupsArrayFind(ra, "printer-more-info"))
  {
    httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), is_encrypted ? "https" : "http", NULL, con->clientname, con->clientport, (printer->type & CUPS_PRINTER_CLASS) ? "/classes/%s" : "/printers/%s", printer->name);
    ippAddString(con->response, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-more-info", NULL, uri);
  }

  if (!ra || cupsArrayFind(ra, "printer-op-policy"))
    ippAddString(con->response, IPP_TAG_PRINTER, IPP_TAG_NAME, "printer-op-policy", NULL, printer->op_policy);

  if (!ra || cupsArrayFind(ra, "printer-state"))
    ippAddInteger(con->response, IPP_TAG_PRINTER, IPP_TAG_ENUM, "printer-state", (int)printer->state);

  if (!ra || cupsArrayFind(ra, "printer-state-change-date-time"))
    ippAddDate(con->response, IPP_TAG_PRINTER, "printer-state-change-date-time", ippTimeToDate(printer->state_time));

  if (!ra || cupsArrayFind(ra, "printer-state-change-time"))
    ippAddInteger(con->response, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "printer-state-change-time", printer->state_time);

  if (!ra || cupsArrayFind(ra, "printer-state-message"))
    ippAddString(con->response, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-state-message", NULL, printer->state_message);

  if (!ra || cupsArrayFind(ra, "printer-state-reasons"))
    add_printer_state_reasons(con, printer);

  if (!ra || cupsArrayFind(ra, "printer-strings-uri"))
  {
    httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), is_encrypted ? "https" : "http", NULL, con->clientname, con->clientport, "/strings/%s.strings", printer->name);
    ippAddString(con->response, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-strings-uri", NULL, uri);
    cupsdLogMessage(CUPSD_LOG_DEBUG2, "printer-strings-uri=\"%s\"", uri);
  }

  if (!ra || cupsArrayFind(ra, "printer-type"))
  {
    cups_ptype_t type;			/* printer-type value */

   /*
    * Add the CUPS-specific printer-type attribute...
    */

    type = printer->type;

    if (printer == DefaultPrinter)
      type |= CUPS_PRINTER_DEFAULT;

    if (!printer->accepting)
      type |= CUPS_PRINTER_REJECTING;

    if (!printer->shared)
      type |= CUPS_PRINTER_NOT_SHARED;

    ippAddInteger(con->response, IPP_TAG_PRINTER, IPP_TAG_ENUM, "printer-type", (int)type);
  }

  if (!ra || cupsArrayFind(ra, "printer-up-time"))
    ippAddInteger(con->response, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "printer-up-time", curtime);

  if (!ra || cupsArrayFind(ra, "printer-uri-supported"))
  {
    httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), is_encrypted ? "ipps" : "ipp", NULL, con->clientname, con->clientport, (printer->type & CUPS_PRINTER_CLASS) ? "/classes/%s" : "/printers/%s", printer->name);
    ippAddString(con->response, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-uri-supported", NULL, uri);
    cupsdLogMessage(CUPSD_LOG_DEBUG2, "printer-uri-supported=\"%s\"", uri);
  }

  if (!ra || cupsArrayFind(ra, "queued-job-count"))
    add_queued_job_count(con, printer);

  if (!ra || cupsArrayFind(ra, "uri-security-supported"))
    ippAddString(con->response, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "uri-security-supported", NULL, is_encrypted ? "tls" : "none");

  copy_attrs(con->response, printer->attrs, ra, IPP_TAG_ZERO, 0, NULL);
  if (printer->ppd_attrs)
    copy_attrs(con->response, printer->ppd_attrs, ra, IPP_TAG_ZERO, 0, NULL);
  copy_attrs(con->response, CommonData, ra, IPP_TAG_ZERO, IPP_TAG_COPY, NULL);

  _cupsRWUnlock(&printer->lock);
}


/*
 * 'copy_subscription_attrs()' - Copy subscription attributes.
 */

static void
copy_subscription_attrs(
    cupsd_client_t       *con,		/* I - Client connection */
    cupsd_subscription_t *sub,		/* I - Subscription */
    cups_array_t         *ra,		/* I - Requested attributes array */
    cups_array_t         *exclude)	/* I - Private attributes array */
{
  ipp_attribute_t	*attr;		/* Current attribute */
  char			printer_uri[HTTP_MAX_URI];
					/* Printer URI */
  int			count;		/* Number of events */
  unsigned		mask;		/* Current event mask */
  const char		*name;		/* Current event name */


  cupsdLogMessage(CUPSD_LOG_DEBUG2,
                  "copy_subscription_attrs(con=%p, sub=%p, ra=%p, exclude=%p)",
		  (void *)con, (void *)sub, (void *)ra, (void *)exclude);

 /*
  * Copy the subscription attributes to the response using the
  * requested-attributes attribute that may be provided by the client.
  */

  if (!exclude || !cupsArrayFind(exclude, "all"))
  {
    if ((!exclude || !cupsArrayFind(exclude, "notify-events")) &&
        (!ra || cupsArrayFind(ra, "notify-events")))
    {
      cupsdLogMessage(CUPSD_LOG_DEBUG2, "copy_subscription_attrs: notify-events");

      if ((name = cupsdEventName((cupsd_eventmask_t)sub->mask)) != NULL)
      {
       /*
	* Simple event list...
	*/

	ippAddString(con->response, IPP_TAG_SUBSCRIPTION, IPP_CONST_TAG(IPP_TAG_KEYWORD), "notify-events", NULL, name);
      }
      else
      {
       /*
	* Complex event list...
	*/

	for (mask = 1, count = 0; mask < CUPSD_EVENT_ALL; mask <<= 1)
	  if (sub->mask & mask)
	    count ++;

	attr = ippAddStrings(con->response, IPP_TAG_SUBSCRIPTION, IPP_CONST_TAG(IPP_TAG_KEYWORD), "notify-events", count, NULL, NULL);

	for (mask = 1, count = 0; mask < CUPSD_EVENT_ALL; mask <<= 1)
	  if (sub->mask & mask)
	  {
	    attr->values[count].string.text = (char *)cupsdEventName((cupsd_eventmask_t)mask);

	    count ++;
	  }
      }
    }

    if ((!exclude || !cupsArrayFind(exclude, "notify-lease-duration")) &&
        (!sub->job && (!ra || cupsArrayFind(ra, "notify-lease-duration"))))
      ippAddInteger(con->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER,
		    "notify-lease-duration", sub->lease);

    if ((!exclude || !cupsArrayFind(exclude, "notify-recipient-uri")) &&
        (sub->recipient && (!ra || cupsArrayFind(ra, "notify-recipient-uri"))))
      ippAddString(con->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_URI,
		   "notify-recipient-uri", NULL, sub->recipient);
    else if ((!exclude || !cupsArrayFind(exclude, "notify-pull-method")) &&
             (!ra || cupsArrayFind(ra, "notify-pull-method")))
      ippAddString(con->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_KEYWORD,
		   "notify-pull-method", NULL, "ippget");

    if ((!exclude || !cupsArrayFind(exclude, "notify-subscriber-user-name")) &&
        (!ra || cupsArrayFind(ra, "notify-subscriber-user-name")))
      ippAddString(con->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_NAME,
		   "notify-subscriber-user-name", NULL, sub->owner);

    if ((!exclude || !cupsArrayFind(exclude, "notify-time-interval")) &&
        (!ra || cupsArrayFind(ra, "notify-time-interval")))
      ippAddInteger(con->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER,
		    "notify-time-interval", sub->interval);

    if (sub->user_data_len > 0 &&
	(!exclude || !cupsArrayFind(exclude, "notify-user-data")) &&
        (!ra || cupsArrayFind(ra, "notify-user-data")))
      ippAddOctetString(con->response, IPP_TAG_SUBSCRIPTION, "notify-user-data",
			sub->user_data, sub->user_data_len);
  }

  if (sub->job && (!ra || cupsArrayFind(ra, "notify-job-id")))
    ippAddInteger(con->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER,
                  "notify-job-id", sub->job->id);

  if (sub->dest && (!ra || cupsArrayFind(ra, "notify-printer-uri")))
  {
    httpAssembleURIf(HTTP_URI_CODING_ALL, printer_uri, sizeof(printer_uri),
                     "ipp", NULL, con->clientname, con->clientport,
		     "/printers/%s", sub->dest->name);
    ippAddString(con->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_URI,
        	 "notify-printer-uri", NULL, printer_uri);
  }

  if (!ra || cupsArrayFind(ra, "notify-subscription-id"))
    ippAddInteger(con->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER,
                  "notify-subscription-id", sub->id);
}


/*
 * 'create_job()' - Print a file to a printer or class.
 */

static void
create_job(cupsd_client_t  *con,	/* I - Client connection */
	   ipp_attribute_t *uri)	/* I - Printer URI */
{
  int			i;		/* Looping var */
  cupsd_printer_t	*printer;	/* Printer */
  cupsd_job_t		*job;		/* New job */
  static const char * const forbidden_attrs[] =
  {					/* List of forbidden attributes */
    "compression",
    "document-format",
    "document-name",
    "document-natural-language"
  };


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "create_job(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * Is the destination valid?
  */

  if (!cupsdValidateDest(uri->values[0].string.text, NULL, &printer))
  {
   /*
    * Bad URI...
    */

    send_ipp_status(con, IPP_NOT_FOUND,
                    _("The printer or class does not exist."));
    return;
  }

 /*
  * Check for invalid Create-Job attributes and log a warning or error depending
  * on whether cupsd is running in "strict conformance" mode...
  */

  for (i = 0;
       i < (int)(sizeof(forbidden_attrs) / sizeof(forbidden_attrs[0]));
       i ++)
    if (ippFindAttribute(con->request, forbidden_attrs[i], IPP_TAG_ZERO))
    {
      if (StrictConformance)
      {
	send_ipp_status(con, IPP_BAD_REQUEST,
			_("The '%s' operation attribute cannot be supplied in a "
			  "Create-Job request."), forbidden_attrs[i]);
	return;
      }

      cupsdLogMessage(CUPSD_LOG_WARN,
                      "Unexpected '%s' operation attribute in a Create-Job "
                      "request.", forbidden_attrs[i]);
    }

 /*
  * Create the job object...
  */

  if ((job = add_job(con, printer, NULL)) == NULL)
    return;

  job->pending_timeout = 1;

 /*
  * Save and log the job...
  */

  cupsdLogJob(job, CUPSD_LOG_INFO, "Queued on \"%s\" by \"%s\".",
	      job->dest, job->username);
}


/*
 * 'create_local_bg_thread()' - Background thread for creating a local print queue.
 */

static void *				/* O - Exit status */
create_local_bg_thread(
    cupsd_client_t *con)		/* I - Client */
{
  cupsd_printer_t *printer = con->bg_printer;
					/* Printer */
  cups_file_t	*from,			/* Source file */
		*to;			/* Destination file */
  char		device_uri[1024],	/* Device URI */
		fromppd[1024],		/* Source PPD */
		toppd[1024],		/* Destination PPD */
		scheme[32],		/* URI scheme */
		userpass[256],		/* User:pass */
		host[256],		/* Hostname */
		resource[1024],		/* Resource path */
		uri[1024],		/* Resolved URI, if needed */
		line[1024];		/* Line from PPD */
  int		port;			/* Port number */
  http_encryption_t encryption;		/* Type of encryption to use */
  http_t	*http;			/* Connection to printer */
  ipp_t		*request,		/* Request to printer */
		*response = NULL;	/* Response from printer */
  ipp_attribute_t *attr;		/* Attribute in response */
  ipp_status_t	status;			/* Status code */
  static const char * const pattrs[] =	/* Printer attributes we need */
  {
    "all",
    "media-col-database"
  };


 /*
  * Try connecting to the printer...
  */

  _cupsRWLockRead(&printer->lock);
  strlcpy(device_uri, printer->device_uri, sizeof(device_uri));
  _cupsRWUnlock(&printer->lock);

  cupsdLogMessage(CUPSD_LOG_DEBUG, "%s: Generating PPD file from \"%s\"...", printer->name, device_uri);

  if (strstr(device_uri, "._tcp"))
  {
    cupsdLogMessage(CUPSD_LOG_DEBUG2, "%s: Resolving mDNS URI \"%s\".", printer->name, device_uri);

    if (!_httpResolveURI(device_uri, uri, sizeof(uri), _HTTP_RESOLVE_DEFAULT, NULL, NULL))
    {
      cupsdLogMessage(CUPSD_LOG_ERROR, "%s: Couldn't resolve mDNS URI \"%s\".", printer->name, device_uri);

      /* Force printer to timeout and be deleted */
      _cupsRWLockWrite(&printer->lock);
      printer->state_time = 0;
      printer->temporary = 1;
      _cupsRWUnlock(&printer->lock);

      send_ipp_status(con, IPP_STATUS_ERROR_DEVICE, _("Couldn't resolve mDNS URI \"%s\"."), printer->device_uri);
      goto finish_response;
    }

    _cupsRWLockWrite(&printer->lock);
    cupsdSetString(&printer->device_uri, uri);
    _cupsRWUnlock(&printer->lock);

    strlcpy(device_uri, uri, sizeof(device_uri));
  }

  if (httpSeparateURI(HTTP_URI_CODING_ALL, device_uri, scheme, sizeof(scheme), userpass, sizeof(userpass), host, sizeof(host), &port, resource, sizeof(resource)) < HTTP_URI_STATUS_OK)
  {
    cupsdLogMessage(CUPSD_LOG_ERROR, "%s: Bad device URI \"%s\".", printer->name, device_uri);

    /* Force printer to timeout and be deleted */
    _cupsRWLockWrite(&printer->lock);
    printer->state_time = 0;
    printer->temporary = 1;
    _cupsRWUnlock(&printer->lock);

    send_ipp_status(con, IPP_STATUS_ERROR_DEVICE, _("Bad device URI \"%s\"."), device_uri);
    goto finish_response;
  }

  if (!strcmp(scheme, "ipps") || port == 443)
    encryption = HTTP_ENCRYPTION_ALWAYS;
  else
    encryption = HTTP_ENCRYPTION_IF_REQUESTED;

  if ((http = httpConnect2(host, port, NULL, AF_UNSPEC, encryption, 1, 30000, NULL)) == NULL)
  {
    cupsdLogMessage(CUPSD_LOG_ERROR, "%s: Unable to connect to %s:%d: %s", printer->name, host, port, cupsLastErrorString());

    /* Force printer to timeout and be deleted */
    _cupsRWLockWrite(&printer->lock);
    printer->state_time = 0;
    printer->temporary = 1;
    _cupsRWUnlock(&printer->lock);

    send_ipp_status(con, IPP_STATUS_ERROR_DEVICE, _("Unable to connect to %s:%d: %s"), host, port, cupsLastErrorString());
    goto finish_response;
  }

 /*
  * Query the printer for its capabilities...
  */

  cupsdLogMessage(CUPSD_LOG_DEBUG, "%s: Connected to %s:%d, sending Get-Printer-Attributes request...", printer->name, host, port);

  request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
  ippSetVersion(request, 2, 0);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, device_uri);
  ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes", (int)(sizeof(pattrs) / sizeof(pattrs[0])), NULL, pattrs);

  response = cupsDoRequest(http, request, resource);
  status   = cupsLastError();

  cupsdLogMessage(CUPSD_LOG_DEBUG, "%s: Get-Printer-Attributes returned %s (%s)", printer->name, ippErrorString(cupsLastError()), cupsLastErrorString());

  if (status == IPP_STATUS_ERROR_BAD_REQUEST || status == IPP_STATUS_ERROR_VERSION_NOT_SUPPORTED)
  {
   /*
    * Try request using IPP/1.1, in case we are talking to an old CUPS server or
    * printer...
    */

    ippDelete(response);

    cupsdLogMessage(CUPSD_LOG_DEBUG, "%s: Re-sending Get-Printer-Attributes request using IPP/1.1...", printer->name);

    request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
    ippSetVersion(request, 1, 1);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, device_uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes", NULL, "all");

    response = cupsDoRequest(http, request, resource);

    cupsdLogMessage(CUPSD_LOG_DEBUG, "%s: IPP/1.1 Get-Printer-Attributes returned %s (%s)", printer->name, ippErrorString(cupsLastError()), cupsLastErrorString());
  }

 /*
  * If we did not succeed to obtain the "media-col-database" attribute
  * try to get it separately
  */

  if (ippFindAttribute(response, "media-col-database", IPP_TAG_ZERO) ==
      NULL)
  {
    ipp_t *response2;

    cupsdLogMessage(CUPSD_LOG_DEBUG,
		    "Polling \"media-col-database\" attribute separately.");
    request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
    ippSetVersion(request, 2, 0);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
		 "printer-uri", NULL, device_uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
		 "requested-attributes", NULL, "media-col-database");
    response2 = cupsDoRequest(http, request, resource);
    //ipp_status = cupsLastError();
    if (response2)
    {
      if ((attr = ippFindAttribute(response2, "media-col-database",
				   IPP_TAG_ZERO)) != NULL)
      {
	cupsdLogMessage(CUPSD_LOG_WARN, "The printer %s does not support requests"
			" with attribute set \"all,media-col-database\", which breaks IPP"
			" conformance (RFC 8011, 4.2.5.1 \"requested-attributes\")"
			" - report the issue to your printer manufacturer", printer->name);
       /*
	* Copy "media-col-database" attribute into the original
	* IPP response
	*/

	cupsdLogMessage(CUPSD_LOG_DEBUG,
			"\"media-col-database\" attribute found.");
	ippCopyAttribute(response, attr, 0);
      }
      ippDelete(response2);
    }
  }

  // Validate response from printer...
  if (!ippValidateAttributes(response))
  {
    /* Force printer to timeout and be deleted */
    _cupsRWLockWrite(&printer->lock);
    printer->state_time = 0;
    printer->temporary = 1;
    _cupsRWUnlock(&printer->lock);

    send_ipp_status(con, IPP_STATUS_ERROR_DEVICE, _("Printer returned invalid data: %s"), cupsLastErrorString());
    goto finish_response;
  }

  // TODO: Grab printer icon file...
  httpClose(http);

 /*
  * Write the PPD for the queue...
  */

  if (_ppdCreateFromIPP(fromppd, sizeof(fromppd), response))
  {
    _cupsRWLockWrite(&printer->lock);

    if ((!printer->info || !*(printer->info)) && (attr = ippFindAttribute(response, "printer-info", IPP_TAG_TEXT)) != NULL)
      cupsdSetString(&printer->info, ippGetString(attr, 0, NULL));

    if ((!printer->location || !*(printer->location)) && (attr = ippFindAttribute(response, "printer-location", IPP_TAG_TEXT)) != NULL)
      cupsdSetString(&printer->location, ippGetString(attr, 0, NULL));

    if ((!printer->geo_location || !*(printer->geo_location)) && (attr = ippFindAttribute(response, "printer-geo-location", IPP_TAG_URI)) != NULL)
      cupsdSetString(&printer->geo_location, ippGetString(attr, 0, NULL));

    _cupsRWUnlock(&printer->lock);

    if ((from = cupsFileOpen(fromppd, "r")) == NULL)
    {
      cupsdLogMessage(CUPSD_LOG_ERROR, "%s: Unable to read generated PPD: %s", printer->name, strerror(errno));

      /* Force printer to timeout and be deleted */
      _cupsRWLockWrite(&printer->lock);
      printer->state_time = 0;
      printer->temporary = 1;
      _cupsRWUnlock(&printer->lock);

      send_ipp_status(con, IPP_STATUS_ERROR_DEVICE, _("Unable to read generated PPD: %s"), strerror(errno));
      goto finish_response;
    }

    snprintf(toppd, sizeof(toppd), "%s/ppd/%s.ppd", ServerRoot, printer->name);
    if ((to = cupsdCreateConfFile(toppd, ConfigFilePerm)) == NULL)
    {
      cupsdLogMessage(CUPSD_LOG_ERROR, "%s: Unable to create PPD for printer: %s", printer->name, strerror(errno));
      cupsFileClose(from);

      /* Force printer to timeout and be deleted */
      _cupsRWLockWrite(&printer->lock);
      printer->state_time = 0;
      printer->temporary = 1;
      _cupsRWUnlock(&printer->lock);

      send_ipp_status(con, IPP_STATUS_ERROR_DEVICE, _("Unable to create PPD for printer: %s"), strerror(errno));
      goto finish_response;
    }

    while (cupsFileGets(from, line, sizeof(line)))
      cupsFilePrintf(to, "%s\n", line);

    cupsFileClose(from);
    if (!cupsdCloseCreatedConfFile(to, toppd))
    {
      _cupsRWLockWrite(&printer->lock);

      printer->config_time = time(NULL);
      printer->state       = IPP_PSTATE_IDLE;
      printer->accepting   = 1;

      _cupsRWUnlock(&printer->lock);

      cupsdSetPrinterAttrs(printer);

      cupsdAddEvent(CUPSD_EVENT_PRINTER_CONFIG, printer, NULL, "Printer \"%s\" is now available.", printer->name);
      cupsdLogMessage(CUPSD_LOG_INFO, "Printer \"%s\" is now available.", printer->name);
    }
  }
  else
  {
    cupsdLogMessage(CUPSD_LOG_ERROR, "%s: PPD creation failed: %s", printer->name, cupsLastErrorString());

    /* Force printer to timeout and be deleted */
    _cupsRWLockWrite(&printer->lock);
    printer->state_time = 0;
    printer->temporary = 1;
    _cupsRWUnlock(&printer->lock);

    send_ipp_status(con, IPP_STATUS_ERROR_DEVICE, _("Unable to create PPD: %s"), cupsLastErrorString());
    goto finish_response;
  }

 /*
  * Respond to the client...
  */

  send_ipp_status(con, IPP_STATUS_OK, _("Local printer created."));

  ippAddBoolean(con->response, IPP_TAG_PRINTER, "printer-is-accepting-jobs", (char)printer->accepting);
  ippAddInteger(con->response, IPP_TAG_PRINTER, IPP_TAG_ENUM, "printer-state", (int)printer->state);
  add_printer_state_reasons(con, printer);

  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), httpIsEncrypted(con->http) ? "ipps" : "ipp", NULL, con->clientname, con->clientport, "/printers/%s", printer->name);
  ippAddString(con->response, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-uri-supported", NULL, uri);

  finish_response:

  ippDelete(response);

  send_response(con);

  con->bg_pending = 0;

  return (NULL);
}


/*
 * 'create_local_printer()' - Create a local (temporary) print queue.
 */

static void
create_local_printer(
    cupsd_client_t *con)		/* I - Client connection */
{
  ipp_attribute_t *device_uri,		/* device-uri attribute */
		*printer_geo_location,	/* printer-geo-location attribute */
		*printer_info,		/* printer-info attribute */
		*printer_location,	/* printer-location attribute */
		*printer_name;		/* printer-name attribute */
  cupsd_printer_t *printer;		/* New printer */
  http_status_t	status;			/* Policy status */
  char		name[128],		/* Sanitized printer name */
		*nameptr,		/* Pointer into name */
		uri[1024];		/* printer-uri-supported value */
  const char	*ptr;			/* Pointer into attribute value */
  char		scheme[HTTP_MAX_URI],	/* Scheme portion of URI */
		userpass[HTTP_MAX_URI],	/* Username portion of URI */
		host[HTTP_MAX_URI],	/* Host portion of URI */
		resource[HTTP_MAX_URI];	/* Resource portion of URI */
  int		port;			/* Port portion of URI */


 /*
  * Require local access to create a local printer...
  */

  if (!httpAddrLocalhost(httpGetAddress(con->http)))
  {
    send_ipp_status(con, IPP_STATUS_ERROR_FORBIDDEN, _("Only local users can create a local printer."));
    return;
  }

 /*
  * Check any other policy limits...
  */

  if ((status = cupsdCheckPolicy(DefaultPolicyPtr, con, NULL)) != HTTP_OK)
  {
    send_http_error(con, status, NULL);
    return;
  }

 /*
  * Grab needed attributes...
  */

  if ((printer_name = ippFindAttribute(con->request, "printer-name", IPP_TAG_ZERO)) == NULL || ippGetGroupTag(printer_name) != IPP_TAG_PRINTER || ippGetValueTag(printer_name) != IPP_TAG_NAME)
  {
    if (!printer_name)
      send_ipp_status(con, IPP_STATUS_ERROR_BAD_REQUEST, _("Missing required attribute \"%s\"."), "printer-name");
    else if (ippGetGroupTag(printer_name) != IPP_TAG_PRINTER)
      send_ipp_status(con, IPP_STATUS_ERROR_BAD_REQUEST, _("Attribute \"%s\" is in the wrong group."), "printer-name");
    else
      send_ipp_status(con, IPP_STATUS_ERROR_BAD_REQUEST, _("Attribute \"%s\" is the wrong value type."), "printer-name");

    return;
  }

  for (nameptr = name, ptr = ippGetString(printer_name, 0, NULL); *ptr && nameptr < (name + sizeof(name) - 1); ptr ++)
  {
   /*
    * Sanitize the printer name...
    */

    if (_cups_isalnum(*ptr))
      *nameptr++ = *ptr;
    else if (nameptr == name || nameptr[-1] != '_')
      *nameptr++ = '_';
  }

  *nameptr = '\0';

  if ((device_uri = ippFindAttribute(con->request, "device-uri", IPP_TAG_ZERO)) == NULL || ippGetGroupTag(device_uri) != IPP_TAG_PRINTER || ippGetValueTag(device_uri) != IPP_TAG_URI)
  {
    if (!device_uri)
      send_ipp_status(con, IPP_STATUS_ERROR_BAD_REQUEST, _("Missing required attribute \"%s\"."), "device-uri");
    else if (ippGetGroupTag(device_uri) != IPP_TAG_PRINTER)
      send_ipp_status(con, IPP_STATUS_ERROR_BAD_REQUEST, _("Attribute \"%s\" is in the wrong group."), "device-uri");
    else
      send_ipp_status(con, IPP_STATUS_ERROR_BAD_REQUEST, _("Attribute \"%s\" is the wrong value type."), "device-uri");

    return;
  }

  ptr = ippGetString(device_uri, 0, NULL);

  if (!ptr || !ptr[0])
  {
    send_ipp_status(con, IPP_STATUS_ERROR_BAD_REQUEST, _("Attribute \"%s\" has empty value."), "device-uri");

    return;
  }

  printer_geo_location = ippFindAttribute(con->request, "printer-geo-location", IPP_TAG_URI);
  printer_info         = ippFindAttribute(con->request, "printer-info", IPP_TAG_TEXT);
  printer_location     = ippFindAttribute(con->request, "printer-location", IPP_TAG_TEXT);

 /*
  * See if the printer already exists...
  */

  if ((printer = cupsdFindDest(name)) != NULL)
  {
    printer->state_time = time(NULL);
    send_ipp_status(con, IPP_STATUS_OK, _("Printer \"%s\" already exists."), name);
    goto add_printer_attributes;
  }

  for (printer = (cupsd_printer_t *)cupsArrayFirst(Printers); printer; printer = (cupsd_printer_t *)cupsArrayNext(Printers))
  {
    if (printer->device_uri && !strcmp(ptr, printer->device_uri))
    {
      printer->state_time = time(NULL);
      send_ipp_status(con, IPP_STATUS_OK, _("Printer \"%s\" already exists."), printer->name);
      goto add_printer_attributes;
    }
  }

 /*
  * Create the printer...
  */

  if ((printer = cupsdAddPrinter(name)) == NULL)
  {
    send_ipp_status(con, IPP_STATUS_ERROR_INTERNAL, _("Unable to create printer."));
    return;
  }

  printer->shared    = 0;
  printer->temporary = 1;

 /*
  * Check device URI if it has the same hostname as we have, if so, replace
  * the hostname by localhost. This way we assure that local-only services
  * like ipp-usb or Printer Applications always work.
  *
  * When comparing our hostname with the one in the device URI,
  * consider names with or without trailing dot ('.') the same. Also
  * compare case-insensitively.
  */

#ifdef HAVE_DNSSD
  if (DNSSDHostName)
    nameptr = DNSSDHostName;
  else
#endif
  if (ServerName)
    nameptr = ServerName;
  else
    nameptr = NULL;

  if (nameptr)
  {
    size_t host_len,
        server_name_len;

    /* Get host name of device URI */
    httpSeparateURI(HTTP_URI_CODING_ALL, ptr,
		    scheme, sizeof(scheme), userpass, sizeof(userpass), host,
		    sizeof(host), &port, resource, sizeof(resource));

    /* Take trailing dot out of comparison */
    host_len = strlen(host);
    if (host_len > 1 && host[host_len - 1] == '.')
      host_len --;

    server_name_len = strlen(nameptr);
    if (server_name_len > 1 && nameptr[server_name_len - 1] == '.')
      server_name_len --;

   /*
    * If we have no DNSSDHostName but only a ServerName (if we are not
    * sharing printers, Browsing = Off) the ServerName has no ".local"
    * but the requested device URI has. Take this into account.
    */

    if (nameptr == ServerName && host_len >= 6 && (server_name_len < 6 || strcmp(nameptr + server_name_len - 6, ".local") != 0) && strcmp(host + host_len - 6, ".local") == 0)
      host_len -= 6;

    if (host_len == server_name_len && strncasecmp(host, nameptr, host_len) == 0)
      ptr = "localhost";
    else
      ptr = host;

    httpAssembleURI(HTTP_URI_CODING_ALL, uri, sizeof(uri), scheme, userpass,
		    ptr, port, resource);
    cupsdSetDeviceURI(printer, uri);
  }
  else
    cupsdSetDeviceURI(printer, ptr);

  if (printer_geo_location)
    cupsdSetString(&printer->geo_location, ippGetString(printer_geo_location, 0, NULL));
  if (printer_info)
    cupsdSetString(&printer->info, ippGetString(printer_info, 0, NULL));
  if (printer_location)
    cupsdSetString(&printer->location, ippGetString(printer_location, 0, NULL));

  cupsdSetPrinterAttrs(printer);

 /*
  * Run a background thread to create the PPD...
  */

  con->bg_pending = 1;
  con->bg_printer = printer;

  _cupsThreadCreate((_cups_thread_func_t)create_local_bg_thread, con);

  return;

 /*
  * Return printer attributes...
  */

  add_printer_attributes:

  ippAddBoolean(con->response, IPP_TAG_PRINTER, "printer-is-accepting-jobs", (char)printer->accepting);
  ippAddInteger(con->response, IPP_TAG_PRINTER, IPP_TAG_ENUM, "printer-state", (int)printer->state);
  add_printer_state_reasons(con, printer);

  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), httpIsEncrypted(con->http) ? "ipps" : "ipp", NULL, con->clientname, con->clientport, "/printers/%s", printer->name);
  ippAddString(con->response, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-uri-supported", NULL, uri);
}


/*
 * 'create_requested_array()' - Create an array for the requested-attributes.
 */

static cups_array_t *			/* O - Array of attributes or NULL */
create_requested_array(ipp_t *request)	/* I - IPP request */
{
  cups_array_t		*ra;		/* Requested attributes array */


 /*
  * Create the array for standard attributes...
  */

  ra = ippCreateRequestedArray(request);

 /*
  * Add CUPS defaults as needed...
  */

  if (cupsArrayFind(ra, "printer-defaults"))
  {
   /*
    * Include user-set defaults...
    */

    char	*name;			/* Option name */

    cupsArrayRemove(ra, "printer-defaults");

    for (name = (char *)cupsArrayFirst(CommonDefaults);
	 name;
	 name = (char *)cupsArrayNext(CommonDefaults))
      if (!cupsArrayFind(ra, name))
        cupsArrayAdd(ra, name);
  }

  return (ra);
}


/*
 * 'create_subscriptions()' - Create one or more notification subscriptions.
 */

static void
create_subscriptions(
    cupsd_client_t  *con,		/* I - Client connection */
    ipp_attribute_t *uri)		/* I - Printer URI */
{
  http_status_t	status;			/* Policy status */
  int			i;		/* Looping var */
  ipp_attribute_t	*attr;		/* Current attribute */
  cups_ptype_t		dtype;		/* Destination type (printer/class) */
  char			scheme[HTTP_MAX_URI],
					/* Scheme portion of URI */
			userpass[HTTP_MAX_URI],
					/* Username portion of URI */
			host[HTTP_MAX_URI],
					/* Host portion of URI */
			resource[HTTP_MAX_URI];
					/* Resource portion of URI */
  int			port;		/* Port portion of URI */
  cupsd_printer_t	*printer;	/* Printer/class */
  cupsd_job_t		*job;		/* Job */
  int			jobid;		/* Job ID */
  cupsd_subscription_t	*sub;		/* Subscription object */
  const char		*username,	/* requesting-user-name or
					   authenticated username */
			*recipient,	/* notify-recipient-uri */
			*pullmethod;	/* notify-pull-method */
  ipp_attribute_t	*user_data;	/* notify-user-data */
  int			interval,	/* notify-time-interval */
			lease;		/* notify-lease-duration */
  unsigned		mask;		/* notify-events */
  ipp_attribute_t	*notify_events,/* notify-events(-default) */
			*notify_lease;	/* notify-lease-duration(-default) */


#ifdef DEBUG
  for (attr = con->request->attrs; attr; attr = attr->next)
  {
    if (attr->group_tag != IPP_TAG_ZERO)
      cupsdLogMessage(CUPSD_LOG_DEBUG2, "g%04x v%04x %s", attr->group_tag,
                      attr->value_tag, attr->name);
    else
      cupsdLogMessage(CUPSD_LOG_DEBUG2, "----SEP----");
  }
#endif /* DEBUG */

 /*
  * Is the destination valid?
  */

  cupsdLogMessage(CUPSD_LOG_DEBUG, "create_subscriptions(con=%p(%d), uri=\"%s\")", (void *)con, con->number, uri->values[0].string.text);

  httpSeparateURI(HTTP_URI_CODING_ALL, uri->values[0].string.text, scheme,
                  sizeof(scheme), userpass, sizeof(userpass), host,
		  sizeof(host), &port, resource, sizeof(resource));

  if (!strcmp(resource, "/"))
  {
    dtype   = (cups_ptype_t)0;
    printer = NULL;
  }
  else if (!strncmp(resource, "/printers", 9) && strlen(resource) <= 10)
  {
    dtype   = (cups_ptype_t)0;
    printer = NULL;
  }
  else if (!strncmp(resource, "/classes", 8) && strlen(resource) <= 9)
  {
    dtype   = CUPS_PRINTER_CLASS;
    printer = NULL;
  }
  else if (!cupsdValidateDest(uri->values[0].string.text, &dtype, &printer))
  {
   /*
    * Bad URI...
    */

    send_ipp_status(con, IPP_NOT_FOUND,
                    _("The printer or class does not exist."));
    return;
  }

 /*
  * Check policy...
  */

  if (printer)
  {
    if ((status = cupsdCheckPolicy(printer->op_policy_ptr, con,
                                   NULL)) != HTTP_OK)
    {
      send_http_error(con, status, printer);
      return;
    }
  }
  else if ((status = cupsdCheckPolicy(DefaultPolicyPtr, con, NULL)) != HTTP_OK)
  {
    send_http_error(con, status, NULL);
    return;
  }

 /*
  * Get the user that is requesting the subscription...
  */

  username = get_username(con);

 /*
  * Find the first subscription group attribute; return if we have
  * none...
  */

  for (attr = con->request->attrs; attr; attr = attr->next)
    if (attr->group_tag == IPP_TAG_SUBSCRIPTION)
      break;

  if (!attr)
  {
    send_ipp_status(con, IPP_BAD_REQUEST,
                    _("No subscription attributes in request."));
    return;
  }

 /*
  * Process the subscription attributes in the request...
  */

  con->response->request.status.status_code = IPP_BAD_REQUEST;

  while (attr)
  {
    recipient = NULL;
    pullmethod = NULL;
    user_data  = NULL;
    interval   = 0;
    lease      = DefaultLeaseDuration;
    jobid      = 0;
    mask       = CUPSD_EVENT_NONE;

    if (printer)
    {
      notify_events = ippFindAttribute(printer->attrs, "notify-events-default",
                                       IPP_TAG_KEYWORD);
      notify_lease  = ippFindAttribute(printer->attrs,
                                       "notify-lease-duration-default",
                                       IPP_TAG_INTEGER);

      if (notify_lease)
        lease = notify_lease->values[0].integer;
    }
    else
    {
      notify_events = NULL;
      notify_lease  = NULL;
    }

    while (attr && attr->group_tag != IPP_TAG_ZERO)
    {
      if (!strcmp(attr->name, "notify-recipient-uri") &&
          attr->value_tag == IPP_TAG_URI)
      {
       /*
        * Validate the recipient scheme against the ServerBin/notifier
	* directory...
	*/

	char	notifier[1024];		/* Notifier filename */


        recipient = attr->values[0].string.text;

	if (httpSeparateURI(HTTP_URI_CODING_ALL, recipient,
	                    scheme, sizeof(scheme), userpass, sizeof(userpass),
			    host, sizeof(host), &port,
			    resource, sizeof(resource)) < HTTP_URI_OK)
        {
          send_ipp_status(con, IPP_NOT_POSSIBLE,
	                  _("Bad notify-recipient-uri \"%s\"."), recipient);
	  ippAddInteger(con->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_ENUM,
	                "notify-status-code", IPP_URI_SCHEME);
	  return;
	}

        snprintf(notifier, sizeof(notifier), "%s/notifier/%s", ServerBin,
	         scheme);
        if (access(notifier, X_OK) || !strcmp(scheme, ".") || !strcmp(scheme, ".."))
	{
          send_ipp_status(con, IPP_NOT_POSSIBLE,
	                  _("notify-recipient-uri URI \"%s\" uses unknown "
			    "scheme."), recipient);
	  ippAddInteger(con->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_ENUM,
	                "notify-status-code", IPP_URI_SCHEME);
	  return;
	}

        if (!strcmp(scheme, "rss") && !check_rss_recipient(recipient))
	{
          send_ipp_status(con, IPP_NOT_POSSIBLE,
	                  _("notify-recipient-uri URI \"%s\" is already used."),
			  recipient);
	  ippAddInteger(con->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_ENUM,
	                "notify-status-code", IPP_ATTRIBUTES);
	  return;
	}
      }
      else if (!strcmp(attr->name, "notify-pull-method") &&
               attr->value_tag == IPP_TAG_KEYWORD)
      {
        pullmethod = attr->values[0].string.text;

        if (strcmp(pullmethod, "ippget"))
	{
          send_ipp_status(con, IPP_NOT_POSSIBLE,
	                  _("Bad notify-pull-method \"%s\"."), pullmethod);
	  ippAddInteger(con->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_ENUM,
	                "notify-status-code", IPP_ATTRIBUTES);
	  return;
	}
      }
      else if (!strcmp(attr->name, "notify-charset") &&
               attr->value_tag == IPP_TAG_CHARSET &&
	       strcmp(attr->values[0].string.text, "us-ascii") &&
	       strcmp(attr->values[0].string.text, "utf-8"))
      {
        send_ipp_status(con, IPP_CHARSET,
	                _("Character set \"%s\" not supported."),
			attr->values[0].string.text);
	return;
      }
      else if (!strcmp(attr->name, "notify-natural-language") &&
               (attr->value_tag != IPP_TAG_LANGUAGE ||
	        strcmp(attr->values[0].string.text, DefaultLanguage)))
      {
        send_ipp_status(con, IPP_CHARSET,
	                _("Language \"%s\" not supported."),
			attr->values[0].string.text);
	return;
      }
      else if (!strcmp(attr->name, "notify-user-data") &&
               attr->value_tag == IPP_TAG_STRING)
      {
        if (attr->num_values > 1 || attr->values[0].unknown.length > 63)
	{
          send_ipp_status(con, IPP_REQUEST_VALUE,
	                  _("The notify-user-data value is too large "
			    "(%d > 63 octets)."),
			  attr->values[0].unknown.length);
	  return;
	}

        user_data = attr;
      }
      else if (!strcmp(attr->name, "notify-events") &&
               attr->value_tag == IPP_TAG_KEYWORD)
        notify_events = attr;
      else if (!strcmp(attr->name, "notify-lease-duration") &&
               attr->value_tag == IPP_TAG_INTEGER)
        lease = attr->values[0].integer;
      else if (!strcmp(attr->name, "notify-time-interval") &&
               attr->value_tag == IPP_TAG_INTEGER)
        interval = attr->values[0].integer;
      else if (!strcmp(attr->name, "notify-job-id") &&
               attr->value_tag == IPP_TAG_INTEGER)
        jobid = attr->values[0].integer;

      attr = attr->next;
    }

    if (notify_events)
    {
      for (i = 0; i < notify_events->num_values; i ++)
	mask |= cupsdEventValue(notify_events->values[i].string.text);
    }

    if (recipient)
    {
      cupsdLogMessage(CUPSD_LOG_DEBUG, "recipient=\"%s\"", recipient);


      if (!strncmp(recipient, "mailto:", 7) && user_data)
      {
        char	temp[64];		/* Temporary string */

	memcpy(temp, user_data->values[0].unknown.data, (size_t)user_data->values[0].unknown.length);
	temp[user_data->values[0].unknown.length] = '\0';

	if (httpSeparateURI(HTTP_URI_CODING_ALL, temp, scheme, sizeof(scheme), userpass, sizeof(userpass), host, sizeof(host), &port, resource, sizeof(resource)) < HTTP_URI_OK)
	{
	  send_ipp_status(con, IPP_NOT_POSSIBLE, _("Bad notify-user-data \"%s\"."), temp);
	  ippAddInteger(con->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_ENUM, "notify-status-code", IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES);
	  return;
	}
      }
    }

    if (pullmethod)
      cupsdLogMessage(CUPSD_LOG_DEBUG, "pullmethod=\"%s\"", pullmethod);
    cupsdLogMessage(CUPSD_LOG_DEBUG, "notify-lease-duration=%d", lease);
    cupsdLogMessage(CUPSD_LOG_DEBUG, "notify-time-interval=%d", interval);

    if (!recipient && !pullmethod)
      break;

    if (mask == CUPSD_EVENT_NONE)
    {
      if (jobid)
        mask = CUPSD_EVENT_JOB_COMPLETED;
      else if (printer)
        mask = CUPSD_EVENT_PRINTER_STATE_CHANGED;
      else
      {
        send_ipp_status(con, IPP_BAD_REQUEST,
	                _("notify-events not specified."));
	return;
      }
    }

    if (MaxLeaseDuration && (lease == 0 || lease > MaxLeaseDuration))
    {
      cupsdLogMessage(CUPSD_LOG_INFO,
                      "create_subscriptions: Limiting notify-lease-duration to "
		      "%d seconds.",
		      MaxLeaseDuration);
      lease = MaxLeaseDuration;
    }

    if (jobid)
    {
      if ((job = cupsdFindJob(jobid)) == NULL)
      {
	send_ipp_status(con, IPP_NOT_FOUND, _("Job #%d does not exist."),
	                jobid);
	return;
      }
    }
    else
      job = NULL;

    if ((sub = cupsdAddSubscription(mask, printer, job, recipient, 0)) == NULL)
    {
      send_ipp_status(con, IPP_TOO_MANY_SUBSCRIPTIONS,
		      _("There are too many subscriptions."));
      return;
    }

    if (job)
      cupsdLogMessage(CUPSD_LOG_DEBUG, "Added subscription #%d for job %d.",
		      sub->id, job->id);
    else if (printer)
      cupsdLogMessage(CUPSD_LOG_DEBUG,
                      "Added subscription #%d for printer \"%s\".",
		      sub->id, printer->name);
    else
      cupsdLogMessage(CUPSD_LOG_DEBUG, "Added subscription #%d for server.",
		      sub->id);

    sub->interval = interval;
    sub->lease    = lease;
    sub->expire   = lease ? time(NULL) + lease : 0;

    cupsdSetString(&sub->owner, username);

    if (user_data)
    {
      sub->user_data_len = user_data->values[0].unknown.length;
      memcpy(sub->user_data, user_data->values[0].unknown.data,
             (size_t)sub->user_data_len);
    }

    ippAddSeparator(con->response);
    ippAddInteger(con->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER,
                  "notify-subscription-id", sub->id);

    con->response->request.status.status_code = IPP_OK;

    if (attr)
      attr = attr->next;
  }

  cupsdMarkDirty(CUPSD_DIRTY_SUBSCRIPTIONS);
}


/*
 * 'delete_printer()' - Remove a printer or class from the system.
 */

static void
delete_printer(cupsd_client_t  *con,	/* I - Client connection */
               ipp_attribute_t *uri)	/* I - URI of printer or class */
{
  http_status_t	status;			/* Policy status */
  cups_ptype_t	dtype;			/* Destination type (printer/class) */
  cupsd_printer_t *printer;		/* Printer/class */
  char		filename[1024];		/* Script/PPD filename */
  int		temporary;		/* Temporary queue? */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "delete_printer(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * Do we have a valid URI?
  */

  if (!cupsdValidateDest(uri->values[0].string.text, &dtype, &printer))
  {
   /*
    * Bad URI...
    */

    send_ipp_status(con, IPP_NOT_FOUND,
                    _("The printer or class does not exist."));
    return;
  }

 /*
  * Check policy...
  */

  if ((status = cupsdCheckPolicy(DefaultPolicyPtr, con, NULL)) != HTTP_OK)
  {
    send_http_error(con, status, NULL);
    return;
  }

 /*
  * Remove old jobs...
  */

  cupsdCancelJobs(printer->name, NULL, 1);

 /*
  * Remove old subscriptions and send a "deleted printer" event...
  */

  cupsdAddEvent(CUPSD_EVENT_PRINTER_DELETED, printer, NULL,
                "%s \"%s\" deleted by \"%s\".",
		(dtype & CUPS_PRINTER_CLASS) ? "Class" : "Printer",
		printer->name, get_username(con));

  cupsdExpireSubscriptions(printer, NULL);

 /*
  * Remove any old PPD or script files...
  */

  snprintf(filename, sizeof(filename), "%s/ppd/%s.ppd", ServerRoot,
           printer->name);
  unlink(filename);
  snprintf(filename, sizeof(filename), "%s/ppd/%s.ppd.O", ServerRoot,
           printer->name);
  unlink(filename);

  snprintf(filename, sizeof(filename), "%s/%s.png", CacheDir, printer->name);
  unlink(filename);

  snprintf(filename, sizeof(filename), "%s/%s.data", CacheDir, printer->name);
  unlink(filename);

 /*
  * Unregister color profiles...
  */

  cupsdUnregisterColor(printer);

  temporary = printer->temporary;

  if (dtype & CUPS_PRINTER_CLASS)
  {
    cupsdLogMessage(CUPSD_LOG_INFO, "Class \"%s\" deleted by \"%s\".",
                    printer->name, get_username(con));

    cupsdDeletePrinter(printer, 0);
    if (!temporary)
      cupsdMarkDirty(CUPSD_DIRTY_CLASSES);
  }
  else
  {
    cupsdLogMessage(CUPSD_LOG_INFO, "Printer \"%s\" deleted by \"%s\".",
                    printer->name, get_username(con));

    if (cupsdDeletePrinter(printer, 0) && !temporary)
      cupsdMarkDirty(CUPSD_DIRTY_CLASSES);

    if (!temporary)
      cupsdMarkDirty(CUPSD_DIRTY_PRINTERS);
  }

  if (!temporary)
    cupsdMarkDirty(CUPSD_DIRTY_PRINTCAP);

 /*
  * Return with no errors...
  */

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'get_default()' - Get the default destination.
 */

static void
get_default(cupsd_client_t *con)	/* I - Client connection */
{
  http_status_t	status;			/* Policy status */
  cups_array_t	*ra;			/* Requested attributes array */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "get_default(%p[%d])", (void *)con, con->number);

 /*
  * Check policy...
  */

  if ((status = cupsdCheckPolicy(DefaultPolicyPtr, con, NULL)) != HTTP_OK)
  {
    send_http_error(con, status, NULL);
    return;
  }

  if (DefaultPrinter)
  {
    ra = create_requested_array(con->request);

    copy_printer_attrs(con, DefaultPrinter, ra);

    cupsArrayDelete(ra);

    con->response->request.status.status_code = IPP_OK;
  }
  else
    send_ipp_status(con, IPP_NOT_FOUND, _("No default printer."));
}


/*
 * 'get_devices()' - Get the list of available devices on the local system.
 */

static void
get_devices(cupsd_client_t *con)	/* I - Client connection */
{
  http_status_t		status;		/* Policy status */
  ipp_attribute_t	*limit,		/* limit attribute */
			*timeout,	/* timeout attribute */
			*requested,	/* requested-attributes attribute */
			*exclude,	/* exclude-schemes attribute */
			*include;	/* include-schemes attribute */
  char			command[1024],	/* cups-deviced command */
			options[2048],	/* Options to pass to command */
			requested_str[256],
					/* String for requested attributes */
			exclude_str[512],
					/* String for excluded schemes */
			include_str[512];
					/* String for included schemes */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "get_devices(%p[%d])", (void *)con, con->number);

 /*
  * Check policy...
  */

  if ((status = cupsdCheckPolicy(DefaultPolicyPtr, con, NULL)) != HTTP_OK)
  {
    send_http_error(con, status, NULL);
    return;
  }

 /*
  * Run cups-deviced command with the given options...
  */

  limit     = ippFindAttribute(con->request, "limit", IPP_TAG_INTEGER);
  timeout   = ippFindAttribute(con->request, "timeout", IPP_TAG_INTEGER);
  requested = ippFindAttribute(con->request, "requested-attributes",
                               IPP_TAG_KEYWORD);
  exclude   = ippFindAttribute(con->request, "exclude-schemes", IPP_TAG_NAME);
  include   = ippFindAttribute(con->request, "include-schemes", IPP_TAG_NAME);

  if (requested)
    url_encode_attr(requested, requested_str, sizeof(requested_str));
  else
    strlcpy(requested_str, "requested-attributes=all", sizeof(requested_str));

  if (exclude)
    url_encode_attr(exclude, exclude_str, sizeof(exclude_str));
  else
    exclude_str[0] = '\0';

  if (include)
    url_encode_attr(include, include_str, sizeof(include_str));
  else
    include_str[0] = '\0';

  snprintf(command, sizeof(command), "%s/daemon/cups-deviced", ServerBin);
  snprintf(options, sizeof(options),
           "%d+%d+%d+%d+%s%s%s%s%s",
           con->request->request.op.request_id,
           limit ? limit->values[0].integer : 0,
	   timeout ? timeout->values[0].integer : 15,
	   (int)User,
	   requested_str,
	   exclude_str[0] ? "%20" : "", exclude_str,
	   include_str[0] ? "%20" : "", include_str);

  if (cupsdSendCommand(con, command, options, 1))
  {
   /*
    * Command started successfully, don't send an IPP response here...
    */

    ippDelete(con->response);
    con->response = NULL;
  }
  else
  {
   /*
    * Command failed, return "internal error" so the user knows something
    * went wrong...
    */

    send_ipp_status(con, IPP_INTERNAL_ERROR,
                    _("cups-deviced failed to execute."));
  }
}


/*
 * 'get_document()' - Get a copy of a job file.
 */

static void
get_document(cupsd_client_t  *con,	/* I - Client connection */
             ipp_attribute_t *uri)	/* I - Job URI */
{
  http_status_t	status;			/* Policy status */
  ipp_attribute_t *attr;		/* Current attribute */
  int		jobid;			/* Job ID */
  int		docnum;			/* Document number */
  cupsd_job_t	*job;			/* Current job */
  char		scheme[HTTP_MAX_URI],	/* Method portion of URI */
		username[HTTP_MAX_URI],	/* Username portion of URI */
		host[HTTP_MAX_URI],	/* Host portion of URI */
		resource[HTTP_MAX_URI];	/* Resource portion of URI */
  int		port;			/* Port portion of URI */
  char		filename[1024],		/* Filename for document */
		format[1024];		/* Format for document */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "get_document(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * See if we have a job URI or a printer URI...
  */

  if (!strcmp(uri->name, "printer-uri"))
  {
   /*
    * Got a printer URI; see if we also have a job-id attribute...
    */

    if ((attr = ippFindAttribute(con->request, "job-id",
                                 IPP_TAG_INTEGER)) == NULL)
    {
      send_ipp_status(con, IPP_BAD_REQUEST,
                      _("Got a printer-uri attribute but no job-id."));
      return;
    }

    jobid = attr->values[0].integer;
  }
  else
  {
   /*
    * Got a job URI; parse it to get the job ID...
    */

    httpSeparateURI(HTTP_URI_CODING_ALL, uri->values[0].string.text, scheme,
                    sizeof(scheme), username, sizeof(username), host,
		    sizeof(host), &port, resource, sizeof(resource));

    if (strncmp(resource, "/jobs/", 6))
    {
     /*
      * Not a valid URI!
      */

      send_ipp_status(con, IPP_BAD_REQUEST, _("Bad job-uri \"%s\"."),
                      uri->values[0].string.text);
      return;
    }

    jobid = atoi(resource + 6);
  }

 /*
  * See if the job exists...
  */

  if ((job = cupsdFindJob(jobid)) == NULL)
  {
   /*
    * Nope - return a "not found" error...
    */

    send_ipp_status(con, IPP_NOT_FOUND, _("Job #%d does not exist."), jobid);
    return;
  }

 /*
  * Check policy...
  */

  if ((status = cupsdCheckPolicy(DefaultPolicyPtr, con,
                                 job->username)) != HTTP_OK)
  {
    send_http_error(con, status, NULL);
    return;
  }

 /*
  * Get the document number...
  */

  if ((attr = ippFindAttribute(con->request, "document-number",
                               IPP_TAG_INTEGER)) == NULL)
  {
    send_ipp_status(con, IPP_BAD_REQUEST,
                    _("Missing document-number attribute."));
    return;
  }

  if ((docnum = attr->values[0].integer) < 1 || docnum > job->num_files ||
      attr->num_values > 1)
  {
    send_ipp_status(con, IPP_NOT_FOUND,
                    _("Document #%d does not exist in job #%d."), docnum,
		    jobid);
    return;
  }

  snprintf(filename, sizeof(filename), "%s/d%05d-%03d", RequestRoot, jobid,
           docnum);
  if ((con->file = open(filename, O_RDONLY)) == -1)
  {
    cupsdLogMessage(CUPSD_LOG_ERROR,
                    "Unable to open document %d in job %d - %s", docnum, jobid,
		    strerror(errno));
    send_ipp_status(con, IPP_NOT_FOUND,
                    _("Unable to open document #%d in job #%d."), docnum,
		    jobid);
    return;
  }

  fcntl(con->file, F_SETFD, fcntl(con->file, F_GETFD) | FD_CLOEXEC);

  cupsdLoadJob(job);

  snprintf(format, sizeof(format), "%s/%s", job->filetypes[docnum - 1]->super,
           job->filetypes[docnum - 1]->type);

  ippAddString(con->response, IPP_TAG_JOB, IPP_TAG_MIMETYPE, "document-format",
               NULL, format);
  ippAddInteger(con->response, IPP_TAG_JOB, IPP_TAG_INTEGER, "document-number",
                docnum);
  if ((attr = ippFindAttribute(job->attrs, "document-name",
                               IPP_TAG_NAME)) != NULL)
    ippAddString(con->response, IPP_TAG_JOB, IPP_TAG_NAME, "document-name",
                 NULL, attr->values[0].string.text);
}


/*
 * 'get_job_attrs()' - Get job attributes.
 */

static void
get_job_attrs(cupsd_client_t  *con,	/* I - Client connection */
	      ipp_attribute_t *uri)	/* I - Job URI */
{
  http_status_t	status;			/* Policy status */
  ipp_attribute_t *attr;		/* Current attribute */
  int		jobid;			/* Job ID */
  cupsd_job_t	*job;			/* Current job */
  cupsd_printer_t *printer;		/* Current printer */
  cupsd_policy_t *policy;		/* Current security policy */
  char		scheme[HTTP_MAX_URI],	/* Scheme portion of URI */
		username[HTTP_MAX_URI],	/* Username portion of URI */
		host[HTTP_MAX_URI],	/* Host portion of URI */
		resource[HTTP_MAX_URI];	/* Resource portion of URI */
  int		port;			/* Port portion of URI */
  cups_array_t	*ra,			/* Requested attributes array */
		*exclude;		/* Private attributes array */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "get_job_attrs(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * See if we have a job URI or a printer URI...
  */

  if (!strcmp(uri->name, "printer-uri"))
  {
   /*
    * Got a printer URI; see if we also have a job-id attribute...
    */

    if ((attr = ippFindAttribute(con->request, "job-id",
                                 IPP_TAG_INTEGER)) == NULL)
    {
      send_ipp_status(con, IPP_BAD_REQUEST,
                      _("Got a printer-uri attribute but no job-id."));
      return;
    }

    jobid = attr->values[0].integer;
  }
  else
  {
   /*
    * Got a job URI; parse it to get the job ID...
    */

    httpSeparateURI(HTTP_URI_CODING_ALL, uri->values[0].string.text, scheme,
                    sizeof(scheme), username, sizeof(username), host,
		    sizeof(host), &port, resource, sizeof(resource));

    if (strncmp(resource, "/jobs/", 6))
    {
     /*
      * Not a valid URI!
      */

      send_ipp_status(con, IPP_BAD_REQUEST, _("Bad job-uri \"%s\"."),
                      uri->values[0].string.text);
      return;
    }

    jobid = atoi(resource + 6);
  }

 /*
  * See if the job exists...
  */

  if ((job = cupsdFindJob(jobid)) == NULL)
  {
   /*
    * Nope - return a "not found" error...
    */

    send_ipp_status(con, IPP_NOT_FOUND, _("Job #%d does not exist."), jobid);
    return;
  }

 /*
  * Check policy...
  */

  if ((printer = job->printer) == NULL)
    printer = cupsdFindDest(job->dest);

  if (printer)
    policy = printer->op_policy_ptr;
  else
    policy = DefaultPolicyPtr;

  if ((status = cupsdCheckPolicy(policy, con, job->username)) != HTTP_OK)
  {
    send_http_error(con, status, NULL);
    return;
  }

  exclude = cupsdGetPrivateAttrs(policy, con, printer, job->username);

 /*
  * Copy attributes...
  */

  cupsdLoadJob(job);

  ra = create_requested_array(con->request);
  copy_job_attrs(con, job, ra, exclude);
  cupsArrayDelete(ra);

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'get_jobs()' - Get a list of jobs for the specified printer.
 */

static void
get_jobs(cupsd_client_t  *con,		/* I - Client connection */
	 ipp_attribute_t *uri)		/* I - Printer URI */
{
  http_status_t	status;			/* Policy status */
  ipp_attribute_t *attr;		/* Current attribute */
  const char	*dest;			/* Destination */
  cups_ptype_t	dtype;			/* Destination type (printer/class) */
  cups_ptype_t	dmask;			/* Destination type mask */
  char		scheme[HTTP_MAX_URI],	/* Scheme portion of URI */
		username[HTTP_MAX_URI],	/* Username portion of URI */
		host[HTTP_MAX_URI],	/* Host portion of URI */
		resource[HTTP_MAX_URI];	/* Resource portion of URI */
  int		port;			/* Port portion of URI */
  int		job_comparison;		/* Job comparison */
  ipp_jstate_t	job_state;		/* job-state value */
  int		first_job_id = 1,	/* First job ID */
		first_index = 1,	/* First index */
		limit = 0,		/* Maximum number of jobs to return */
		count,			/* Number of jobs that match */
		need_load_job = 0;	/* Do we need to load the job? */
  const char	*job_attr;		/* Job attribute requested */
  ipp_attribute_t *job_ids;		/* job-ids attribute */
  cupsd_job_t	*job;			/* Current job pointer */
  cupsd_printer_t *printer;		/* Printer */
  cups_array_t	*list;			/* Which job list... */
  int		delete_list = 0;	/* Delete the list afterwards? */
  cups_array_t	*ra,			/* Requested attributes array */
		*exclude;		/* Private attributes array */
  cupsd_policy_t *policy;		/* Current policy */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "get_jobs(%p[%d], %s)", (void *)con, con->number,
                  uri->values[0].string.text);

 /*
  * Is the destination valid?
  */

  if (strcmp(uri->name, "printer-uri"))
  {
    send_ipp_status(con, IPP_BAD_REQUEST, _("No printer-uri in request."));
    return;
  }

  httpSeparateURI(HTTP_URI_CODING_ALL, uri->values[0].string.text, scheme,
                  sizeof(scheme), username, sizeof(username), host,
		  sizeof(host), &port, resource, sizeof(resource));

  if (!strcmp(resource, "/") || !strcmp(resource, "/jobs"))
  {
    dest    = NULL;
    dtype   = (cups_ptype_t)0;
    dmask   = (cups_ptype_t)0;
    printer = NULL;
  }
  else if (!strncmp(resource, "/printers", 9) && strlen(resource) <= 10)
  {
    dest    = NULL;
    dtype   = (cups_ptype_t)0;
    dmask   = CUPS_PRINTER_CLASS;
    printer = NULL;
  }
  else if (!strncmp(resource, "/classes", 8) && strlen(resource) <= 9)
  {
    dest    = NULL;
    dtype   = CUPS_PRINTER_CLASS;
    dmask   = CUPS_PRINTER_CLASS;
    printer = NULL;
  }
  else if ((dest = cupsdValidateDest(uri->values[0].string.text, &dtype,
                                     &printer)) == NULL)
  {
   /*
    * Bad URI...
    */

    send_ipp_status(con, IPP_NOT_FOUND,
                    _("The printer or class does not exist."));
    return;
  }
  else
  {
    dtype &= CUPS_PRINTER_CLASS;
    dmask = CUPS_PRINTER_CLASS;
  }

 /*
  * Check policy...
  */

  if (printer)
    policy = printer->op_policy_ptr;
  else
    policy = DefaultPolicyPtr;

  if ((status = cupsdCheckPolicy(policy, con, NULL)) != HTTP_OK)
  {
    send_http_error(con, status, NULL);
    return;
  }

  job_ids = ippFindAttribute(con->request, "job-ids", IPP_TAG_INTEGER);

 /*
  * See if the "which-jobs" attribute have been specified...
  */

  if ((attr = ippFindAttribute(con->request, "which-jobs",
                               IPP_TAG_KEYWORD)) != NULL && job_ids)
  {
    send_ipp_status(con, IPP_CONFLICT,
                    _("The %s attribute cannot be provided with job-ids."),
                    "which-jobs");
    return;
  }
  else if (!attr || !strcmp(attr->values[0].string.text, "not-completed"))
  {
    job_comparison = -1;
    job_state      = IPP_JOB_STOPPED;
    list           = ActiveJobs;
  }
  else if (!strcmp(attr->values[0].string.text, "completed"))
  {
    job_comparison = 1;
    job_state      = IPP_JOB_CANCELED;
    list           = cupsdGetCompletedJobs(printer);
    delete_list    = 1;
  }
  else if (!strcmp(attr->values[0].string.text, "aborted"))
  {
    job_comparison = 0;
    job_state      = IPP_JOB_ABORTED;
    list           = cupsdGetCompletedJobs(printer);
    delete_list    = 1;
  }
  else if (!strcmp(attr->values[0].string.text, "all"))
  {
    job_comparison = 1;
    job_state      = IPP_JOB_PENDING;
    list           = Jobs;
  }
  else if (!strcmp(attr->values[0].string.text, "canceled"))
  {
    job_comparison = 0;
    job_state      = IPP_JOB_CANCELED;
    list           = cupsdGetCompletedJobs(printer);
    delete_list    = 1;
  }
  else if (!strcmp(attr->values[0].string.text, "pending"))
  {
    job_comparison = 0;
    job_state      = IPP_JOB_PENDING;
    list           = ActiveJobs;
  }
  else if (!strcmp(attr->values[0].string.text, "pending-held"))
  {
    job_comparison = 0;
    job_state      = IPP_JOB_HELD;
    list           = ActiveJobs;
  }
  else if (!strcmp(attr->values[0].string.text, "processing"))
  {
    job_comparison = 0;
    job_state      = IPP_JOB_PROCESSING;
    list           = PrintingJobs;
  }
  else if (!strcmp(attr->values[0].string.text, "processing-stopped"))
  {
    job_comparison = 0;
    job_state      = IPP_JOB_STOPPED;
    list           = ActiveJobs;
  }
  else
  {
    send_ipp_status(con, IPP_ATTRIBUTES,
                    _("The which-jobs value \"%s\" is not supported."),
		    attr->values[0].string.text);
    ippAddString(con->response, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_KEYWORD,
                 "which-jobs", NULL, attr->values[0].string.text);
    return;
  }

 /*
  * See if they want to limit the number of jobs reported...
  */

  if ((attr = ippFindAttribute(con->request, "limit", IPP_TAG_INTEGER)) != NULL)
  {
    if (job_ids)
    {
      send_ipp_status(con, IPP_CONFLICT,
		      _("The %s attribute cannot be provided with job-ids."),
		      "limit");
      return;
    }

    limit = attr->values[0].integer;
  }

  if ((attr = ippFindAttribute(con->request, "first-index", IPP_TAG_INTEGER)) != NULL)
  {
    if (job_ids)
    {
      send_ipp_status(con, IPP_CONFLICT,
		      _("The %s attribute cannot be provided with job-ids."),
		      "first-index");
      return;
    }

    first_index = attr->values[0].integer;
  }
  else if ((attr = ippFindAttribute(con->request, "first-job-id", IPP_TAG_INTEGER)) != NULL)
  {
    if (job_ids)
    {
      send_ipp_status(con, IPP_CONFLICT,
		      _("The %s attribute cannot be provided with job-ids."),
		      "first-job-id");
      return;
    }

    first_job_id = attr->values[0].integer;
  }

 /*
  * See if we only want to see jobs for a specific user...
  */

  if ((attr = ippFindAttribute(con->request, "my-jobs", IPP_TAG_BOOLEAN)) != NULL && job_ids)
  {
    send_ipp_status(con, IPP_CONFLICT,
                    _("The %s attribute cannot be provided with job-ids."),
                    "my-jobs");
    return;
  }
  else if (attr && attr->values[0].boolean)
    strlcpy(username, get_username(con), sizeof(username));
  else
    username[0] = '\0';

  ra = create_requested_array(con->request);
  for (job_attr = (char *)cupsArrayFirst(ra); job_attr; job_attr = (char *)cupsArrayNext(ra))
    if (strcmp(job_attr, "job-id") &&
	strcmp(job_attr, "job-k-octets") &&
	strcmp(job_attr, "job-media-progress") &&
	strcmp(job_attr, "job-more-info") &&
	strcmp(job_attr, "job-name") &&
	strcmp(job_attr, "job-originating-user-name") &&
	strcmp(job_attr, "job-preserved") &&
	strcmp(job_attr, "job-printer-up-time") &&
        strcmp(job_attr, "job-printer-uri") &&
	strcmp(job_attr, "job-state") &&
	strcmp(job_attr, "job-state-reasons") &&
	strcmp(job_attr, "job-uri") &&
	strcmp(job_attr, "time-at-completed") &&
	strcmp(job_attr, "time-at-creation") &&
	strcmp(job_attr, "number-of-documents"))
    {
      need_load_job = 1;
      break;
    }

  if (need_load_job && (limit == 0 || limit > 500) && (list == Jobs || delete_list))
  {
   /*
    * Limit expensive Get-Jobs for job history to 500 jobs...
    */

    ippAddInteger(con->response, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "limit", 500);

    if (limit)
      ippAddInteger(con->response, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_INTEGER, "limit", limit);

    limit = 500;

    cupsdLogClient(con, CUPSD_LOG_INFO, "Limiting Get-Jobs response to %d jobs.", limit);
  }

 /*
  * OK, build a list of jobs for this printer...
  */

  if (job_ids)
  {
    int	i;				/* Looping var */

    for (i = 0; i < job_ids->num_values; i ++)
    {
      if (!cupsdFindJob(job_ids->values[i].integer))
        break;
    }

    if (i < job_ids->num_values)
    {
      send_ipp_status(con, IPP_NOT_FOUND, _("Job #%d does not exist."),
                      job_ids->values[i].integer);
      cupsArrayDelete(ra);
      return;
    }

    for (i = 0; i < job_ids->num_values; i ++)
    {
      job = cupsdFindJob(job_ids->values[i].integer);

      if (need_load_job && !job->attrs)
      {
        cupsdLoadJob(job);

	if (!job->attrs)
	{
	  cupsdLogMessage(CUPSD_LOG_DEBUG2, "get_jobs: No attributes for job %d", job->id);
	  continue;
	}
      }

      if (i > 0)
	ippAddSeparator(con->response);

      exclude = cupsdGetPrivateAttrs(job->printer ?
                                         job->printer->op_policy_ptr :
					 policy, con, job->printer,
					 job->username);

      copy_job_attrs(con, job, ra, exclude);
    }
  }
  else
  {
    if (first_index > 1)
      job = (cupsd_job_t *)cupsArrayIndex(list, first_index - 1);
    else
      job = (cupsd_job_t *)cupsArrayFirst(list);

    for (count = 0; (limit <= 0 || count < limit) && job; job = (cupsd_job_t *)cupsArrayNext(list))
    {
     /*
      * Filter out jobs that don't match...
      */

      cupsdLogMessage(CUPSD_LOG_DEBUG2,
		      "get_jobs: job->id=%d, dest=\"%s\", username=\"%s\", "
		      "state_value=%d, attrs=%p", job->id, job->dest,
		      job->username, job->state_value, (void *)job->attrs);

      if (!job->dest || !job->username)
	cupsdLoadJob(job);

      if (!job->dest || !job->username)
	continue;

      if ((dest && strcmp(job->dest, dest)) &&
	  (!job->printer || !dest || strcmp(job->printer->name, dest)))
	continue;
      if ((job->dtype & dmask) != dtype &&
	  (!job->printer || (job->printer->type & dmask) != dtype))
	continue;

      if ((job_comparison < 0 && job->state_value > job_state) ||
          (job_comparison == 0 && job->state_value != job_state) ||
          (job_comparison > 0 && job->state_value < job_state))
	continue;

      if (job->id < first_job_id)
	continue;

      if (need_load_job && !job->attrs)
      {
        cupsdLoadJob(job);

	if (!job->attrs)
	{
	  cupsdLogMessage(CUPSD_LOG_DEBUG2, "get_jobs: No attributes for job %d", job->id);
	  continue;
	}
      }

      if (username[0] && _cups_strcasecmp(username, job->username))
	continue;

      if (count > 0)
	ippAddSeparator(con->response);

      count ++;

      exclude = cupsdGetPrivateAttrs(job->printer ?
                                         job->printer->op_policy_ptr :
					 policy, con, job->printer,
					 job->username);

      copy_job_attrs(con, job, ra, exclude);
    }

    cupsdLogMessage(CUPSD_LOG_DEBUG2, "get_jobs: count=%d", count);
  }

  cupsArrayDelete(ra);

  if (delete_list)
    cupsArrayDelete(list);

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'get_notifications()' - Get events for a subscription.
 */

static void
get_notifications(cupsd_client_t *con)	/* I - Client connection */
{
  int			i, j;		/* Looping vars */
  http_status_t		status;		/* Policy status */
  cupsd_subscription_t	*sub;		/* Subscription */
  ipp_attribute_t	*ids,		/* notify-subscription-ids */
			*sequences;	/* notify-sequence-numbers */
  int			min_seq;	/* Minimum sequence number */
  int			interval;	/* Poll interval */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "get_notifications(con=%p[%d])",
                  (void *)con, con->number);

 /*
  * Get subscription attributes...
  */

  ids       = ippFindAttribute(con->request, "notify-subscription-ids",
                               IPP_TAG_INTEGER);
  sequences = ippFindAttribute(con->request, "notify-sequence-numbers",
                               IPP_TAG_INTEGER);

  if (!ids)
  {
    send_ipp_status(con, IPP_BAD_REQUEST,
                    _("Missing notify-subscription-ids attribute."));
    return;
  }

 /*
  * Are the subscription IDs valid?
  */

  for (i = 0, interval = 60; i < ids->num_values; i ++)
  {
    if ((sub = cupsdFindSubscription(ids->values[i].integer)) == NULL)
    {
     /*
      * Bad subscription ID...
      */

      send_ipp_status(con, IPP_NOT_FOUND, _("Subscription #%d does not exist."),
		      ids->values[i].integer);
      return;
    }

   /*
    * Check policy...
    */

    if ((status = cupsdCheckPolicy(sub->dest ? sub->dest->op_policy_ptr :
                                               DefaultPolicyPtr,
                                   con, sub->owner)) != HTTP_OK)
    {
      send_http_error(con, status, sub->dest);
      return;
    }

   /*
    * Check the subscription type and update the interval accordingly.
    */

    if (sub->job && sub->job->state_value == IPP_JOB_PROCESSING &&
        interval > 10)
      interval = 10;
    else if (sub->job && sub->job->state_value >= IPP_JOB_STOPPED)
      interval = 0;
    else if (sub->dest && sub->dest->state == IPP_PRINTER_PROCESSING &&
             interval > 30)
      interval = 30;
  }

 /*
  * Tell the client to poll again in N seconds...
  */

  if (interval > 0)
    ippAddInteger(con->response, IPP_TAG_OPERATION, IPP_TAG_INTEGER,
                  "notify-get-interval", interval);

  ippAddInteger(con->response, IPP_TAG_OPERATION, IPP_TAG_INTEGER,
                "printer-up-time", time(NULL));

 /*
  * Copy the subscription event attributes to the response.
  */

  con->response->request.status.status_code =
      interval ? IPP_OK : IPP_OK_EVENTS_COMPLETE;

  for (i = 0; i < ids->num_values; i ++)
  {
   /*
    * Get the subscription and sequence number...
    */

    sub = cupsdFindSubscription(ids->values[i].integer);

    if (sequences && i < sequences->num_values)
      min_seq = sequences->values[i].integer;
    else
      min_seq = 1;

   /*
    * If we don't have any new events, nothing to do here...
    */

    if (min_seq > (sub->first_event_id + cupsArrayCount(sub->events)))
      continue;

   /*
    * Otherwise copy all of the new events...
    */

    if (sub->first_event_id > min_seq)
      j = 0;
    else
      j = min_seq - sub->first_event_id;

    for (; j < cupsArrayCount(sub->events); j ++)
    {
      ippAddSeparator(con->response);

      copy_attrs(con->response,
                 ((cupsd_event_t *)cupsArrayIndex(sub->events, j))->attrs, NULL,
        	 IPP_TAG_EVENT_NOTIFICATION, 0, NULL);
    }
  }
}


/*
 * 'get_ppd()' - Get a named PPD from the local system.
 */

static void
get_ppd(cupsd_client_t  *con,		/* I - Client connection */
        ipp_attribute_t *uri)		/* I - Printer URI or PPD name */
{
  http_status_t		status;		/* Policy status */
  cupsd_printer_t	*dest;		/* Destination */
  cups_ptype_t		dtype;		/* Destination type */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "get_ppd(%p[%d], %p[%s=%s])", (void *)con,
                  con->number, (void *)uri, uri->name, uri->values[0].string.text);

  if (!strcmp(ippGetName(uri), "ppd-name"))
  {
   /*
    * Return a PPD file from cups-driverd...
    */

    const char *ppd_name = ippGetString(uri, 0, NULL);
					/* ppd-name value */
    char	command[1024],		/* cups-driverd command */
		options[1024],		/* Options to pass to command */
		oppd_name[1024];	/* Escaped ppd-name */

   /*
    * Check policy...
    */

    if ((status = cupsdCheckPolicy(DefaultPolicyPtr, con, NULL)) != HTTP_OK)
    {
      send_http_error(con, status, NULL);
      return;
    }

   /*
    * Check ppd-name value...
    */

    if (strstr(ppd_name, "../"))
    {
      send_ipp_status(con, IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES, _("Invalid ppd-name value."));
      return;
    }

   /*
    * Run cups-driverd command with the given options...
    */

    snprintf(command, sizeof(command), "%s/daemon/cups-driverd", ServerBin);
    url_encode_string(ppd_name, oppd_name, sizeof(oppd_name));
    snprintf(options, sizeof(options), "get+%d+%s", ippGetRequestId(con->request), oppd_name);

    if (cupsdSendCommand(con, command, options, 0))
    {
     /*
      * Command started successfully, don't send an IPP response here...
      */

      ippDelete(con->response);
      con->response = NULL;
    }
    else
    {
     /*
      * Command failed, return "internal error" so the user knows something
      * went wrong...
      */

      send_ipp_status(con, IPP_INTERNAL_ERROR, _("cups-driverd failed to execute."));
    }
  }
  else if (!strcmp(ippGetName(uri), "printer-uri") && cupsdValidateDest(ippGetString(uri, 0, NULL), &dtype, &dest))
  {
    int 	i;			/* Looping var */
    char	filename[1024];		/* PPD filename */

   /*
    * Check policy...
    */

    if ((status = cupsdCheckPolicy(dest->op_policy_ptr, con, NULL)) != HTTP_OK)
    {
      send_http_error(con, status, dest);
      return;
    }

   /*
    * See if we need the PPD for a class or remote printer...
    */

    snprintf(filename, sizeof(filename), "%s/ppd/%s.ppd", ServerRoot, dest->name);

    if ((dtype & CUPS_PRINTER_REMOTE) && access(filename, 0))
    {
      send_ipp_status(con, IPP_STATUS_CUPS_SEE_OTHER, _("See remote printer."));
      ippAddString(con->response, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, dest->uri);
      return;
    }
    else if (dtype & CUPS_PRINTER_CLASS)
    {
      for (i = 0; i < dest->num_printers; i ++)
        if (!(dest->printers[i]->type & CUPS_PRINTER_CLASS))
	{
	  snprintf(filename, sizeof(filename), "%s/ppd/%s.ppd", ServerRoot, dest->printers[i]->name);

          if (!access(filename, 0))
	    break;
        }

      if (i < dest->num_printers)
        dest = dest->printers[i];
      else
      {
	send_ipp_status(con, IPP_STATUS_CUPS_SEE_OTHER, _("See remote printer."));
        ippAddString(con->response, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, dest->printers[0]->uri);
        return;
      }
    }

   /*
    * Found the printer with the PPD file, now see if there is one...
    */

    if ((con->file = open(filename, O_RDONLY)) < 0)
    {
      send_ipp_status(con, IPP_STATUS_ERROR_NOT_FOUND, _("The PPD file \"%s\" could not be opened: %s"), ippGetString(uri, 0, NULL), strerror(errno));
      return;
    }

    fcntl(con->file, F_SETFD, fcntl(con->file, F_GETFD) | FD_CLOEXEC);

    con->pipe_pid = 0;

    ippSetStatusCode(con->response, IPP_STATUS_OK);
  }
  else
    send_ipp_status(con, IPP_STATUS_ERROR_NOT_FOUND, _("The PPD file \"%s\" could not be found."), ippGetString(uri, 0, NULL));
}


/*
 * 'get_ppds()' - Get the list of PPD files on the local system.
 */

static void
get_ppds(cupsd_client_t *con)		/* I - Client connection */
{
  http_status_t		status;		/* Policy status */
  ipp_attribute_t	*limit,		/* Limit attribute */
			*device,	/* ppd-device-id attribute */
			*language,	/* ppd-natural-language attribute */
			*make,		/* ppd-make attribute */
			*model,		/* ppd-make-and-model attribute */
			*model_number,	/* ppd-model-number attribute */
			*product,	/* ppd-product attribute */
			*psversion,	/* ppd-psverion attribute */
			*type,		/* ppd-type attribute */
			*requested,	/* requested-attributes attribute */
			*exclude,	/* exclude-schemes attribute */
			*include;	/* include-schemes attribute */
  char			command[1024],	/* cups-driverd command */
			options[4096],	/* Options to pass to command */
			device_str[256],/* Escaped ppd-device-id string */
			language_str[256],
					/* Escaped ppd-natural-language */
			make_str[256],	/* Escaped ppd-make string */
			model_str[256],	/* Escaped ppd-make-and-model string */
			model_number_str[256],
					/* ppd-model-number string */
			product_str[256],
					/* Escaped ppd-product string */
			psversion_str[256],
					/* Escaped ppd-psversion string */
			type_str[256],	/* Escaped ppd-type string */
			requested_str[256],
					/* String for requested attributes */
			exclude_str[512],
					/* String for excluded schemes */
			include_str[512];
					/* String for included schemes */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "get_ppds(%p[%d])", (void *)con, con->number);

 /*
  * Check policy...
  */

  if ((status = cupsdCheckPolicy(DefaultPolicyPtr, con, NULL)) != HTTP_OK)
  {
    send_http_error(con, status, NULL);
    return;
  }

 /*
  * Run cups-driverd command with the given options...
  */

  limit        = ippFindAttribute(con->request, "limit", IPP_TAG_INTEGER);
  device       = ippFindAttribute(con->request, "ppd-device-id", IPP_TAG_TEXT);
  language     = ippFindAttribute(con->request, "ppd-natural-language",
                                  IPP_TAG_LANGUAGE);
  make         = ippFindAttribute(con->request, "ppd-make", IPP_TAG_TEXT);
  model        = ippFindAttribute(con->request, "ppd-make-and-model",
                                  IPP_TAG_TEXT);
  model_number = ippFindAttribute(con->request, "ppd-model-number",
                                  IPP_TAG_INTEGER);
  product      = ippFindAttribute(con->request, "ppd-product", IPP_TAG_TEXT);
  psversion    = ippFindAttribute(con->request, "ppd-psversion", IPP_TAG_TEXT);
  type         = ippFindAttribute(con->request, "ppd-type", IPP_TAG_KEYWORD);
  requested    = ippFindAttribute(con->request, "requested-attributes",
                                  IPP_TAG_KEYWORD);
  exclude      = ippFindAttribute(con->request, "exclude-schemes",
                                  IPP_TAG_NAME);
  include      = ippFindAttribute(con->request, "include-schemes",
                                  IPP_TAG_NAME);

  if (requested)
    url_encode_attr(requested, requested_str, sizeof(requested_str));
  else
    strlcpy(requested_str, "requested-attributes=all", sizeof(requested_str));

  if (device)
    url_encode_attr(device, device_str, sizeof(device_str));
  else
    device_str[0] = '\0';

  if (language)
    url_encode_attr(language, language_str, sizeof(language_str));
  else
    language_str[0] = '\0';

  if (make)
    url_encode_attr(make, make_str, sizeof(make_str));
  else
    make_str[0] = '\0';

  if (model)
    url_encode_attr(model, model_str, sizeof(model_str));
  else
    model_str[0] = '\0';

  if (model_number)
    snprintf(model_number_str, sizeof(model_number_str), "ppd-model-number=%d",
             model_number->values[0].integer);
  else
    model_number_str[0] = '\0';

  if (product)
    url_encode_attr(product, product_str, sizeof(product_str));
  else
    product_str[0] = '\0';

  if (psversion)
    url_encode_attr(psversion, psversion_str, sizeof(psversion_str));
  else
    psversion_str[0] = '\0';

  if (type)
    url_encode_attr(type, type_str, sizeof(type_str));
  else
    type_str[0] = '\0';

  if (exclude)
    url_encode_attr(exclude, exclude_str, sizeof(exclude_str));
  else
    exclude_str[0] = '\0';

  if (include)
    url_encode_attr(include, include_str, sizeof(include_str));
  else
    include_str[0] = '\0';

  snprintf(command, sizeof(command), "%s/daemon/cups-driverd", ServerBin);
  snprintf(options, sizeof(options),
           "list+%d+%d+%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s",
           con->request->request.op.request_id,
           limit ? limit->values[0].integer : 0,
	   requested_str,
	   device ? "%20" : "", device_str,
	   language ? "%20" : "", language_str,
	   make ? "%20" : "", make_str,
	   model ? "%20" : "", model_str,
	   model_number ? "%20" : "", model_number_str,
	   product ? "%20" : "", product_str,
	   psversion ? "%20" : "", psversion_str,
	   type ? "%20" : "", type_str,
	   exclude_str[0] ? "%20" : "", exclude_str,
	   include_str[0] ? "%20" : "", include_str);

  if (cupsdSendCommand(con, command, options, 0))
  {
   /*
    * Command started successfully, don't send an IPP response here...
    */

    ippDelete(con->response);
    con->response = NULL;
  }
  else
  {
   /*
    * Command failed, return "internal error" so the user knows something
    * went wrong...
    */

    send_ipp_status(con, IPP_INTERNAL_ERROR,
                    _("cups-driverd failed to execute."));
  }
}


/*
 * 'get_printer_attrs()' - Get printer attributes.
 */

static void
get_printer_attrs(cupsd_client_t  *con,	/* I - Client connection */
		  ipp_attribute_t *uri)	/* I - Printer URI */
{
  http_status_t		status;		/* Policy status */
  cups_ptype_t		dtype;		/* Destination type (printer/class) */
  cupsd_printer_t	*printer;	/* Printer/class */
  cups_array_t		*ra;		/* Requested attributes array */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "get_printer_attrs(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * Is the destination valid?
  */

  if (!cupsdValidateDest(uri->values[0].string.text, &dtype, &printer))
  {
   /*
    * Bad URI...
    */

    send_ipp_status(con, IPP_NOT_FOUND,
                    _("The printer or class does not exist."));
    return;
  }

 /*
  * Check policy...
  */

  if ((status = cupsdCheckPolicy(printer->op_policy_ptr, con, NULL)) != HTTP_OK)
  {
    send_http_error(con, status, printer);
    return;
  }

 /*
  * Send the attributes...
  */

  ra = create_requested_array(con->request);

  copy_printer_attrs(con, printer, ra);

  cupsArrayDelete(ra);

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'get_printer_supported()' - Get printer supported values.
 */

static void
get_printer_supported(
    cupsd_client_t  *con,		/* I - Client connection */
    ipp_attribute_t *uri)		/* I - Printer URI */
{
  http_status_t		status;		/* Policy status */
  cups_ptype_t		dtype;		/* Destination type (printer/class) */
  cupsd_printer_t	*printer;	/* Printer/class */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "get_printer_supported(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * Is the destination valid?
  */

  if (!cupsdValidateDest(uri->values[0].string.text, &dtype, &printer))
  {
   /*
    * Bad URI...
    */

    send_ipp_status(con, IPP_NOT_FOUND,
                    _("The printer or class does not exist."));
    return;
  }

 /*
  * Check policy...
  */

  if ((status = cupsdCheckPolicy(printer->op_policy_ptr, con, NULL)) != HTTP_OK)
  {
    send_http_error(con, status, printer);
    return;
  }

 /*
  * Return a list of attributes that can be set via Set-Printer-Attributes.
  */

  ippAddInteger(con->response, IPP_TAG_PRINTER, IPP_TAG_ADMINDEFINE,
                "printer-geo-location", 0);
  ippAddInteger(con->response, IPP_TAG_PRINTER, IPP_TAG_ADMINDEFINE,
                "printer-info", 0);
  ippAddInteger(con->response, IPP_TAG_PRINTER, IPP_TAG_ADMINDEFINE,
                "printer-location", 0);
  ippAddInteger(con->response, IPP_TAG_PRINTER, IPP_TAG_ADMINDEFINE,
                "printer-organization", 0);
  ippAddInteger(con->response, IPP_TAG_PRINTER, IPP_TAG_ADMINDEFINE,
                "printer-organizational-unit", 0);

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'get_printers()' - Get a list of printers or classes.
 */

static void
get_printers(cupsd_client_t *con,	/* I - Client connection */
             int            type)	/* I - 0 or CUPS_PRINTER_CLASS */
{
  http_status_t	status;			/* Policy status */
  ipp_attribute_t *attr;		/* Current attribute */
  int		limit;			/* Max number of printers to return */
  int		count;			/* Number of printers that match */
  int		printer_id;		/* Printer we are interested in */
  cupsd_printer_t *printer;		/* Current printer pointer */
  cups_ptype_t	printer_type,		/* printer-type attribute */
		printer_mask;		/* printer-type-mask attribute */
  char		*location;		/* Location string */
  const char	*username;		/* Current user */
  char		*first_printer_name;	/* first-printer-name attribute */
  cups_array_t	*ra;			/* Requested attributes array */
  int		local;			/* Local connection? */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "get_printers(%p[%d], %x)", (void *)con,
                  con->number, type);

 /*
  * Check policy...
  */

  if ((status = cupsdCheckPolicy(DefaultPolicyPtr, con, NULL)) != HTTP_OK)
  {
    send_http_error(con, status, NULL);
    return;
  }

 /*
  * Check for printers...
  */

  if (!Printers || !cupsArrayCount(Printers))
  {
    send_ipp_status(con, IPP_NOT_FOUND, _("No destinations added."));
    return;
  }

 /*
  * See if they want to limit the number of printers reported...
  */

  if ((attr = ippFindAttribute(con->request, "limit",
                               IPP_TAG_INTEGER)) != NULL)
    limit = attr->values[0].integer;
  else
    limit = 10000000;

  if ((attr = ippFindAttribute(con->request, "first-printer-name",
                               IPP_TAG_NAME)) != NULL)
    first_printer_name = attr->values[0].string.text;
  else
    first_printer_name = NULL;

 /*
  * Support filtering...
  */

  if ((attr = ippFindAttribute(con->request, "printer-id", IPP_TAG_INTEGER)) != NULL)
  {
    if ((printer_id = ippGetInteger(attr, 0)) <= 0)
    {
      send_ipp_status(con, IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES, _("Bad \"printer-id\" value %d."), printer_id);
      return;
    }
  }
  else
    printer_id = 0;

  if ((attr = ippFindAttribute(con->request, "printer-type",
                               IPP_TAG_ENUM)) != NULL)
    printer_type = (cups_ptype_t)attr->values[0].integer;
  else
    printer_type = (cups_ptype_t)0;

  if ((attr = ippFindAttribute(con->request, "printer-type-mask",
                               IPP_TAG_ENUM)) != NULL)
    printer_mask = (cups_ptype_t)attr->values[0].integer;
  else
    printer_mask = (cups_ptype_t)0;

  local = httpAddrLocalhost(&(con->clientaddr));

  if ((attr = ippFindAttribute(con->request, "printer-location",
                               IPP_TAG_TEXT)) != NULL)
    location = attr->values[0].string.text;
  else
    location = NULL;

  if (con->username[0])
    username = con->username;
  else if ((attr = ippFindAttribute(con->request, "requesting-user-name",
                                    IPP_TAG_NAME)) != NULL)
    username = attr->values[0].string.text;
  else
    username = NULL;

  ra = create_requested_array(con->request);

 /*
  * OK, build a list of printers for this printer...
  */

  if (first_printer_name)
  {
    if ((printer = cupsdFindDest(first_printer_name)) == NULL)
      printer = (cupsd_printer_t *)cupsArrayFirst(Printers);
  }
  else
    printer = (cupsd_printer_t *)cupsArrayFirst(Printers);

  for (count = 0;
       count < limit && printer;
       printer = (cupsd_printer_t *)cupsArrayNext(Printers))
  {
    if (!local && !printer->shared)
      continue;

    if (printer_id && printer->printer_id != printer_id)
      continue;

    if ((!type || (printer->type & CUPS_PRINTER_CLASS) == type) &&
        (printer->type & printer_mask) == printer_type &&
	(!location ||
	 (printer->location && !_cups_strcasecmp(printer->location, location))))
    {
     /*
      * If a username is specified, see if it is allowed or denied
      * access...
      */

      if (cupsArrayCount(printer->users) && username &&
	  !user_allowed(printer, username))
        continue;

     /*
      * Add the group separator as needed...
      */

      if (count > 0)
        ippAddSeparator(con->response);

      count ++;

     /*
      * Send the attributes...
      */

      copy_printer_attrs(con, printer, ra);
    }
  }

  cupsArrayDelete(ra);

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'get_subscription_attrs()' - Get subscription attributes.
 */

static void
get_subscription_attrs(
    cupsd_client_t *con,		/* I - Client connection */
    int            sub_id)		/* I - Subscription ID */
{
  http_status_t		status;		/* Policy status */
  cupsd_subscription_t	*sub;		/* Subscription */
  cupsd_policy_t	*policy;	/* Current security policy */
  cups_array_t		*ra,		/* Requested attributes array */
			*exclude;	/* Private attributes array */


  cupsdLogMessage(CUPSD_LOG_DEBUG2,
                  "get_subscription_attrs(con=%p[%d], sub_id=%d)",
                  (void *)con, con->number, sub_id);

 /*
  * Expire subscriptions as needed...
  */

  cupsdExpireSubscriptions(NULL, NULL);

 /*
  * Is the subscription ID valid?
  */

  if ((sub = cupsdFindSubscription(sub_id)) == NULL)
  {
   /*
    * Bad subscription ID...
    */

    send_ipp_status(con, IPP_NOT_FOUND, _("Subscription #%d does not exist."),
                    sub_id);
    return;
  }

 /*
  * Check policy...
  */

  if (sub->dest)
    policy = sub->dest->op_policy_ptr;
  else
    policy = DefaultPolicyPtr;

  if ((status = cupsdCheckPolicy(policy, con, sub->owner)) != HTTP_OK)
  {
    send_http_error(con, status, sub->dest);
    return;
  }

  exclude = cupsdGetPrivateAttrs(policy, con, sub->dest, sub->owner);

 /*
  * Copy the subscription attributes to the response using the
  * requested-attributes attribute that may be provided by the client.
  */

  ra = create_requested_array(con->request);

  copy_subscription_attrs(con, sub, ra, exclude);

  cupsArrayDelete(ra);

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'get_subscriptions()' - Get subscriptions.
 */

static void
get_subscriptions(cupsd_client_t  *con,	/* I - Client connection */
                  ipp_attribute_t *uri)	/* I - Printer/job URI */
{
  http_status_t		status;		/* Policy status */
  int			count;		/* Number of subscriptions */
  int			limit;		/* Limit */
  cupsd_subscription_t	*sub;		/* Subscription */
  cups_array_t		*ra;		/* Requested attributes array */
  ipp_attribute_t	*attr;		/* Attribute */
  cups_ptype_t		dtype;		/* Destination type (printer/class) */
  char			scheme[HTTP_MAX_URI],
					/* Scheme portion of URI */
			username[HTTP_MAX_URI],
					/* Username portion of URI */
			host[HTTP_MAX_URI],
					/* Host portion of URI */
			resource[HTTP_MAX_URI];
					/* Resource portion of URI */
  int			port;		/* Port portion of URI */
  cupsd_job_t		*job;		/* Job pointer */
  cupsd_printer_t	*printer;	/* Printer */
  cupsd_policy_t	*policy;	/* Policy */
  cups_array_t		*exclude;	/* Private attributes array */


  cupsdLogMessage(CUPSD_LOG_DEBUG2,
                  "get_subscriptions(con=%p[%d], uri=%s)",
                  (void *)con, con->number, uri->values[0].string.text);

 /*
  * Is the destination valid?
  */

  httpSeparateURI(HTTP_URI_CODING_ALL, uri->values[0].string.text, scheme,
                  sizeof(scheme), username, sizeof(username), host,
		  sizeof(host), &port, resource, sizeof(resource));

  if (!strcmp(resource, "/") ||
      (!strncmp(resource, "/jobs", 5) && strlen(resource) <= 6) ||
      (!strncmp(resource, "/printers", 9) && strlen(resource) <= 10) ||
      (!strncmp(resource, "/classes", 8) && strlen(resource) <= 9))
  {
    printer = NULL;
    job     = NULL;
  }
  else if (!strncmp(resource, "/jobs/", 6) && resource[6])
  {
    int job_id = atoi(resource + 6);
    printer = NULL;
    job     = cupsdFindJob(job_id);

    if (!job)
    {
      send_ipp_status(con, IPP_NOT_FOUND, _("Job #%d does not exist."),
                      job_id);
      return;
    }
  }
  else if (!cupsdValidateDest(uri->values[0].string.text, &dtype, &printer))
  {
   /*
    * Bad URI...
    */

    send_ipp_status(con, IPP_NOT_FOUND,
                    _("The printer or class does not exist."));
    return;
  }
  else if ((attr = ippFindAttribute(con->request, "notify-job-id",
                                    IPP_TAG_INTEGER)) != NULL)
  {
    job = cupsdFindJob(attr->values[0].integer);

    if (!job)
    {
      send_ipp_status(con, IPP_NOT_FOUND, _("Job #%d does not exist."),
                      attr->values[0].integer);
      return;
    }
  }
  else
    job = NULL;

 /*
  * Check policy...
  */

  if (printer)
    policy = printer->op_policy_ptr;
  else
    policy = DefaultPolicyPtr;

  if ((status = cupsdCheckPolicy(policy, con, NULL)) != HTTP_OK)
  {
    send_http_error(con, status, printer);
    return;
  }

 /*
  * Expire subscriptions as needed...
  */

  cupsdExpireSubscriptions(NULL, NULL);

 /*
  * Copy the subscription attributes to the response using the
  * requested-attributes attribute that may be provided by the client.
  */

  ra = create_requested_array(con->request);

  if ((attr = ippFindAttribute(con->request, "limit",
                               IPP_TAG_INTEGER)) != NULL)
    limit = attr->values[0].integer;
  else
    limit = 0;

 /*
  * See if we only want to see subscriptions for a specific user...
  */

  if ((attr = ippFindAttribute(con->request, "my-subscriptions",
                               IPP_TAG_BOOLEAN)) != NULL &&
      attr->values[0].boolean)
    strlcpy(username, get_username(con), sizeof(username));
  else
    username[0] = '\0';

  for (sub = (cupsd_subscription_t *)cupsArrayFirst(Subscriptions), count = 0;
       sub;
       sub = (cupsd_subscription_t *)cupsArrayNext(Subscriptions))
    if ((!printer || sub->dest == printer) && (!job || sub->job == job) &&
        (!username[0] || !_cups_strcasecmp(username, sub->owner)))
    {
      ippAddSeparator(con->response);

      exclude = cupsdGetPrivateAttrs(sub->dest ? sub->dest->op_policy_ptr :
						 policy, con, sub->dest,
						 sub->owner);

      copy_subscription_attrs(con, sub, ra, exclude);

      count ++;
      if (limit && count >= limit)
        break;
    }

  cupsArrayDelete(ra);

  if (count)
    con->response->request.status.status_code = IPP_OK;
  else
    send_ipp_status(con, IPP_NOT_FOUND, _("No subscriptions found."));
}


/*
 * 'get_username()' - Get the username associated with a request.
 */

static const char *			/* O - Username */
get_username(cupsd_client_t *con)	/* I - Connection */
{
  ipp_attribute_t	*attr;		/* Attribute */


  if (con->username[0])
    return (con->username);
  else if ((attr = ippFindAttribute(con->request, "requesting-user-name",
                                    IPP_TAG_NAME)) != NULL)
    return (attr->values[0].string.text);
  else
    return ("anonymous");
}


/*
 * 'hold_job()' - Hold a print job.
 */

static void
hold_job(cupsd_client_t  *con,		/* I - Client connection */
         ipp_attribute_t *uri)		/* I - Job or Printer URI */
{
  ipp_attribute_t *attr;		/* Current job-hold-until */
  const char	*when;			/* New value */
  int		jobid;			/* Job ID */
  char		scheme[HTTP_MAX_URI],	/* Method portion of URI */
		username[HTTP_MAX_URI],	/* Username portion of URI */
		host[HTTP_MAX_URI],	/* Host portion of URI */
		resource[HTTP_MAX_URI];	/* Resource portion of URI */
  int		port;			/* Port portion of URI */
  cupsd_job_t	*job;			/* Job information */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "hold_job(%p[%d], %s)", (void *)con, con->number,
                  uri->values[0].string.text);

 /*
  * See if we have a job URI or a printer URI...
  */

  if (!strcmp(uri->name, "printer-uri"))
  {
   /*
    * Got a printer URI; see if we also have a job-id attribute...
    */

    if ((attr = ippFindAttribute(con->request, "job-id",
                                 IPP_TAG_INTEGER)) == NULL)
    {
      send_ipp_status(con, IPP_BAD_REQUEST,
                      _("Got a printer-uri attribute but no job-id."));
      return;
    }

    jobid = attr->values[0].integer;
  }
  else
  {
   /*
    * Got a job URI; parse it to get the job ID...
    */

    httpSeparateURI(HTTP_URI_CODING_ALL, uri->values[0].string.text, scheme,
                    sizeof(scheme), username, sizeof(username), host,
		    sizeof(host), &port, resource, sizeof(resource));

    if (strncmp(resource, "/jobs/", 6))
    {
     /*
      * Not a valid URI!
      */

      send_ipp_status(con, IPP_BAD_REQUEST,
                      _("Bad job-uri \"%s\"."),
                      uri->values[0].string.text);
      return;
    }

    jobid = atoi(resource + 6);
  }

 /*
  * See if the job exists...
  */

  if ((job = cupsdFindJob(jobid)) == NULL)
  {
   /*
    * Nope - return a "not found" error...
    */

    send_ipp_status(con, IPP_NOT_FOUND, _("Job #%d does not exist."), jobid);
    return;
  }

 /*
  * See if the job is owned by the requesting user...
  */

  if (!validate_user(job, con, job->username, username, sizeof(username)))
  {
    send_http_error(con, con->username[0] ? HTTP_FORBIDDEN : HTTP_UNAUTHORIZED,
		    cupsdFindDest(job->dest));
    return;
  }

 /*
  * See if the job is in a state that allows holding...
  */

  if (job->state_value > IPP_JOB_STOPPED)
  {
   /*
    * Return a "not-possible" error...
    */

    send_ipp_status(con, IPP_NOT_POSSIBLE,
		    _("Job #%d is finished and cannot be altered."),
		    job->id);
    return;
  }

 /*
  * Hold the job and return...
  */

  if ((attr = ippFindAttribute(con->request, "job-hold-until", IPP_TAG_ZERO)) != NULL)
  {
    if ((ippGetValueTag(attr) != IPP_TAG_KEYWORD && ippGetValueTag(attr) != IPP_TAG_NAME && ippGetValueTag(attr) != IPP_TAG_NAMELANG) || ippGetCount(attr) != 1 || !ippValidateAttribute(attr))
    {
      send_ipp_status(con, IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES, _("Unsupported 'job-hold-until' value."));
      ippCopyAttribute(con->response, attr, 0);
      return;
    }

    when = ippGetString(attr, 0, NULL);

    cupsdAddEvent(CUPSD_EVENT_JOB_CONFIG_CHANGED, cupsdFindDest(job->dest), job,
		  "Job job-hold-until value changed by user.");
  }
  else
    when = "indefinite";

  cupsdSetJobHoldUntil(job, when, 1);
  cupsdSetJobState(job, IPP_JOB_HELD, CUPSD_JOB_DEFAULT, "Job held by \"%s\".",
                   username);

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'hold_new_jobs()' - Hold pending/new jobs on a printer or class.
 */

static void
hold_new_jobs(cupsd_client_t  *con,	/* I - Connection */
              ipp_attribute_t *uri)	/* I - Printer URI */
{
  http_status_t		status;		/* Policy status */
  cups_ptype_t		dtype;		/* Destination type (printer/class) */
  cupsd_printer_t	*printer;	/* Printer data */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "hold_new_jobs(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * Is the destination valid?
  */

  if (!cupsdValidateDest(uri->values[0].string.text, &dtype, &printer))
  {
   /*
    * Bad URI...
    */

    send_ipp_status(con, IPP_NOT_FOUND,
                    _("The printer or class does not exist."));
    return;
  }

 /*
  * Check policy...
  */

  if ((status = cupsdCheckPolicy(printer->op_policy_ptr, con, NULL)) != HTTP_OK)
  {
    send_http_error(con, status, printer);
    return;
  }

 /*
  * Hold pending/new jobs sent to the printer...
  */

  printer->holding_new_jobs = 1;

  cupsdSetPrinterReasons(printer, "+hold-new-jobs");

  if (dtype & CUPS_PRINTER_CLASS)
    cupsdLogMessage(CUPSD_LOG_INFO,
                    "Class \"%s\" now holding pending/new jobs (\"%s\").",
                    printer->name, get_username(con));
  else
    cupsdLogMessage(CUPSD_LOG_INFO,
                    "Printer \"%s\" now holding pending/new jobs (\"%s\").",
                    printer->name, get_username(con));

 /*
  * Everything was ok, so return OK status...
  */

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'move_job()' - Move a job to a new destination.
 */

static void
move_job(cupsd_client_t  *con,		/* I - Client connection */
	 ipp_attribute_t *uri)		/* I - Job URI */
{
  http_status_t	status;			/* Policy status */
  ipp_attribute_t *attr;		/* Current attribute */
  int		jobid;			/* Job ID */
  cupsd_job_t	*job;			/* Current job */
  const char	*src;			/* Source printer/class */
  cups_ptype_t	stype,			/* Source type (printer or class) */
		dtype;			/* Destination type (printer/class) */
  char		scheme[HTTP_MAX_URI],	/* Scheme portion of URI */
		username[HTTP_MAX_URI],	/* Username portion of URI */
		host[HTTP_MAX_URI],	/* Host portion of URI */
		resource[HTTP_MAX_URI];	/* Resource portion of URI */
  int		port;			/* Port portion of URI */
  cupsd_printer_t *sprinter,		/* Source printer */
		*dprinter;		/* Destination printer */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "move_job(%p[%d], %s)", (void *)con, con->number,
                  uri->values[0].string.text);

 /*
  * Get the new printer or class...
  */

  if ((attr = ippFindAttribute(con->request, "job-printer-uri",
                               IPP_TAG_URI)) == NULL)
  {
   /*
    * Need job-printer-uri...
    */

    send_ipp_status(con, IPP_BAD_REQUEST,
                    _("job-printer-uri attribute missing."));
    return;
  }

  if (!cupsdValidateDest(attr->values[0].string.text, &dtype, &dprinter))
  {
   /*
    * Bad URI...
    */

    send_ipp_status(con, IPP_NOT_FOUND,
                    _("The printer or class does not exist."));
    return;
  }

 /*
  * See if we have a job URI or a printer URI...
  */

  httpSeparateURI(HTTP_URI_CODING_ALL, uri->values[0].string.text, scheme,
                  sizeof(scheme), username, sizeof(username), host,
		  sizeof(host), &port, resource, sizeof(resource));

  if (!strcmp(uri->name, "printer-uri"))
  {
   /*
    * Got a printer URI; see if we also have a job-id attribute...
    */

    if ((attr = ippFindAttribute(con->request, "job-id",
                                 IPP_TAG_INTEGER)) == NULL)
    {
     /*
      * Move all jobs...
      */

      if ((src = cupsdValidateDest(uri->values[0].string.text, &stype,
                                   &sprinter)) == NULL)
      {
       /*
	* Bad URI...
	*/

	send_ipp_status(con, IPP_NOT_FOUND,
                	_("The printer or class does not exist."));
	return;
      }

      job = NULL;
    }
    else
    {
     /*
      * Otherwise, just move a single job...
      */

      if ((job = cupsdFindJob(attr->values[0].integer)) == NULL)
      {
       /*
	* Nope - return a "not found" error...
	*/

	send_ipp_status(con, IPP_NOT_FOUND,
                	_("Job #%d does not exist."), attr->values[0].integer);
	return;
      }
      else
      {
       /*
        * Job found, initialize source pointers...
	*/

	src      = NULL;
	sprinter = NULL;
      }
    }
  }
  else
  {
   /*
    * Got a job URI; parse it to get the job ID...
    */

    if (strncmp(resource, "/jobs/", 6))
    {
     /*
      * Not a valid URI!
      */

      send_ipp_status(con, IPP_BAD_REQUEST, _("Bad job-uri \"%s\"."),
                      uri->values[0].string.text);
      return;
    }

   /*
    * See if the job exists...
    */

    jobid = atoi(resource + 6);

    if ((job = cupsdFindJob(jobid)) == NULL)
    {
     /*
      * Nope - return a "not found" error...
      */

      send_ipp_status(con, IPP_NOT_FOUND, _("Job #%d does not exist."), jobid);
      return;
    }
    else
    {
     /*
      * Job found, initialize source pointers...
      */

      src      = NULL;
      sprinter = NULL;
    }
  }

 /*
  * Check the policy of the destination printer...
  */

  if ((status = cupsdCheckPolicy(dprinter->op_policy_ptr, con,
                                 job ? job->username : NULL)) != HTTP_OK)
  {
    send_http_error(con, status, dprinter);
    return;
  }

 /*
  * Now move the job or jobs...
  */

  if (job)
  {
   /*
    * See if the job has been completed...
    */

    if (job->state_value > IPP_JOB_STOPPED)
    {
     /*
      * Return a "not-possible" error...
      */

      send_ipp_status(con, IPP_NOT_POSSIBLE,
                      _("Job #%d is finished and cannot be altered."),
		      job->id);
      return;
    }

   /*
    * See if the job is owned by the requesting user...
    */

    if (!validate_user(job, con, job->username, username, sizeof(username)))
    {
      send_http_error(con, con->username[0] ? HTTP_FORBIDDEN : HTTP_UNAUTHORIZED,
                      cupsdFindDest(job->dest));
      return;
    }

   /*
    * Move the job to a different printer or class...
    */

    cupsdMoveJob(job, dprinter);
  }
  else
  {
   /*
    * Got the source printer, now look through the jobs...
    */

    for (job = (cupsd_job_t *)cupsArrayFirst(Jobs);
         job;
	 job = (cupsd_job_t *)cupsArrayNext(Jobs))
    {
     /*
      * See if the job is pointing at the source printer or has not been
      * completed...
      */

      if (_cups_strcasecmp(job->dest, src) ||
          job->state_value > IPP_JOB_STOPPED)
	continue;

     /*
      * See if the job can be moved by the requesting user...
      */

      if (!validate_user(job, con, job->username, username, sizeof(username)))
        continue;

     /*
      * Move the job to a different printer or class...
      */

      cupsdMoveJob(job, dprinter);
    }
  }

 /*
  * Start jobs if possible...
  */

  cupsdCheckJobs();

 /*
  * Return with "everything is OK" status...
  */

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'ppd_parse_line()' - Parse a PPD default line.
 */

static int				/* O - 0 on success, -1 on failure */
ppd_parse_line(const char *line,	/* I - Line */
               char       *option,	/* O - Option name */
	       int        olen,		/* I - Size of option name */
               char       *choice,	/* O - Choice name */
	       int        clen)		/* I - Size of choice name */
{
 /*
  * Verify this is a default option line...
  */

  if (strncmp(line, "*Default", 8))
    return (-1);

 /*
  * Read the option name...
  */

  for (line += 8, olen --;
       *line > ' ' && *line < 0x7f && *line != ':' && *line != '/';
       line ++)
    if (olen > 0)
    {
      *option++ = *line;
      olen --;
    }

  *option = '\0';

 /*
  * Skip everything else up to the colon (:)...
  */

  while (*line && *line != ':')
    line ++;

  if (!*line)
    return (-1);

  line ++;

 /*
  * Now grab the option choice, skipping leading whitespace...
  */

  while (isspace(*line & 255))
    line ++;

  for (clen --;
       *line > ' ' && *line < 0x7f && *line != ':' && *line != '/';
       line ++)
    if (clen > 0)
    {
      *choice++ = *line;
      clen --;
    }

  *choice = '\0';

 /*
  * Return with no errors...
  */

  return (0);
}


/*
 * 'print_job()' - Print a file to a printer or class.
 */

static void
print_job(cupsd_client_t  *con,		/* I - Client connection */
	  ipp_attribute_t *uri)		/* I - Printer URI */
{
  ipp_attribute_t *attr;		/* Current attribute */
  ipp_attribute_t *doc_name;		/* document-name attribute */
  ipp_attribute_t *format;		/* Document-format attribute */
  const char	*default_format;	/* document-format-default value */
  cupsd_job_t	*job;			/* New job */
  char		filename[1024];		/* Job filename */
  mime_type_t	*filetype;		/* Type of file */
  char		super[MIME_MAX_SUPER],	/* Supertype of file */
		type[MIME_MAX_TYPE],	/* Subtype of file */
		mimetype[MIME_MAX_SUPER + MIME_MAX_TYPE + 2];
					/* Textual name of mime type */
  cupsd_printer_t *printer;		/* Printer data */
  struct stat	fileinfo;		/* File information */
  int		kbytes;			/* Size of file */
  int		compression;		/* Document compression */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "print_job(%p[%d], %s)", (void *)con, con->number,
                  uri->values[0].string.text);

 /*
  * Validate print file attributes, for now just document-format and
  * compression (CUPS only supports "none" and "gzip")...
  */

  compression = CUPS_FILE_NONE;

  if ((attr = ippFindAttribute(con->request, "compression",
                               IPP_TAG_KEYWORD)) != NULL)
  {
    if (strcmp(attr->values[0].string.text, "none")
#ifdef HAVE_LIBZ
        && strcmp(attr->values[0].string.text, "gzip")
#endif /* HAVE_LIBZ */
      )
    {
      send_ipp_status(con, IPP_ATTRIBUTES,
                      _("Unsupported compression \"%s\"."),
        	      attr->values[0].string.text);
      ippAddString(con->response, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_KEYWORD,
	           "compression", NULL, attr->values[0].string.text);
      return;
    }

#ifdef HAVE_LIBZ
    if (!strcmp(attr->values[0].string.text, "gzip"))
      compression = CUPS_FILE_GZIP;
#endif /* HAVE_LIBZ */
  }

 /*
  * Do we have a file to print?
  */

  if (!con->filename)
  {
    send_ipp_status(con, IPP_BAD_REQUEST, _("No file in print request."));
    return;
  }

 /*
  * Is the destination valid?
  */

  if (!cupsdValidateDest(uri->values[0].string.text, NULL, &printer))
  {
   /*
    * Bad URI...
    */

    send_ipp_status(con, IPP_NOT_FOUND,
                    _("The printer or class does not exist."));
    return;
  }

 /*
  * Is it a format we support?
  */

  doc_name = ippFindAttribute(con->request, "document-name", IPP_TAG_NAME);
  if (doc_name)
    ippSetName(con->request, &doc_name, "document-name-supplied");

  if ((format = ippFindAttribute(con->request, "document-format",
                                 IPP_TAG_MIMETYPE)) != NULL)
  {
   /*
    * Grab format from client...
    */

    if (sscanf(format->values[0].string.text, "%15[^/]/%255[^;]", super,
               type) != 2)
    {
      send_ipp_status(con, IPP_BAD_REQUEST,
                      _("Bad document-format \"%s\"."),
		      format->values[0].string.text);
      return;
    }

    ippAddString(con->request, IPP_TAG_JOB, IPP_TAG_MIMETYPE, "document-format-supplied", NULL, ippGetString(format, 0, NULL));
  }
  else if ((default_format = cupsGetOption("document-format",
                                           printer->num_options,
					   printer->options)) != NULL)
  {
   /*
    * Use default document format...
    */

    if (sscanf(default_format, "%15[^/]/%255[^;]", super, type) != 2)
    {
      send_ipp_status(con, IPP_BAD_REQUEST,
                      _("Bad document-format \"%s\"."),
		      default_format);
      return;
    }
  }
  else
  {
   /*
    * Auto-type it!
    */

    strlcpy(super, "application", sizeof(super));
    strlcpy(type, "octet-stream", sizeof(type));
  }

  _cupsRWLockRead(&MimeDatabase->lock);

  if (!strcmp(super, "application") && !strcmp(type, "octet-stream"))
  {
   /*
    * Auto-type the file...
    */

    cupsdLogMessage(CUPSD_LOG_DEBUG, "[Job ???] Auto-typing file...");


    filetype = mimeFileType(MimeDatabase, con->filename,
                            doc_name ? doc_name->values[0].string.text : NULL,
			    &compression);

    if (!filetype)
      filetype = mimeType(MimeDatabase, super, type);

    cupsdLogMessage(CUPSD_LOG_INFO, "[Job ???] Request file type is %s/%s.",
		    filetype->super, filetype->type);

    snprintf(mimetype, sizeof(mimetype), "%s/%s", filetype->super, filetype->type);
    ippAddString(con->request, IPP_TAG_JOB, IPP_TAG_MIMETYPE, "document-format-detected", NULL, mimetype);
  }
  else
    filetype = mimeType(MimeDatabase, super, type);

  _cupsRWUnlock(&MimeDatabase->lock);

  if (filetype &&
      (!format ||
       (!strcmp(super, "application") && !strcmp(type, "octet-stream"))))
  {
   /*
    * Replace the document-format attribute value with the auto-typed or
    * default one.
    */

    snprintf(mimetype, sizeof(mimetype), "%s/%s", filetype->super,
             filetype->type);

    if (format)
      ippSetString(con->request, &format, 0, mimetype);
    else
      ippAddString(con->request, IPP_TAG_JOB, IPP_TAG_MIMETYPE,
	           "document-format", NULL, mimetype);
  }
  else if (!filetype)
  {
    send_ipp_status(con, IPP_DOCUMENT_FORMAT,
                    _("Unsupported document-format \"%s\"."),
		    format ? format->values[0].string.text :
			     "application/octet-stream");
    cupsdLogMessage(CUPSD_LOG_INFO,
                    "Hint: Do you have the raw file printing rules enabled?");

    if (format)
      ippAddString(con->response, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_MIMETYPE,
                   "document-format", NULL, format->values[0].string.text);

    return;
  }

 /*
  * Read any embedded job ticket info from PS files...
  */

  if (!_cups_strcasecmp(filetype->super, "application") &&
      (!_cups_strcasecmp(filetype->type, "postscript") ||
       !_cups_strcasecmp(filetype->type, "pdf")))
    read_job_ticket(con);

 /*
  * Create the job object...
  */

  if ((job = add_job(con, printer, filetype)) == NULL)
    return;

 /*
  * Update quota data...
  */

  if (stat(con->filename, &fileinfo))
    kbytes = 0;
  else
    kbytes = (fileinfo.st_size + 1023) / 1024;

  cupsdUpdateQuota(printer, job->username, 0, kbytes);

  job->koctets += kbytes;

  if ((attr = ippFindAttribute(job->attrs, "job-k-octets", IPP_TAG_INTEGER)) != NULL)
    attr->values[0].integer += kbytes;

 /*
  * Add the job file...
  */

  if (add_file(con, job, filetype, compression))
    return;

  snprintf(filename, sizeof(filename), "%s/d%05d-%03d", RequestRoot, job->id, job->num_files);
  if (rename(con->filename, filename))
  {
    cupsdLogJob(job, CUPSD_LOG_ERROR, "Unable to rename job document file \"%s\": %s", filename, strerror(errno));

    send_ipp_status(con, IPP_INTERNAL_ERROR, _("Unable to rename job document file."));
    return;
  }

  cupsdClearString(&con->filename);

 /*
  * See if we need to add the ending sheet...
  */

  if (cupsdTimeoutJob(job))
    return;

 /*
  * Log and save the job...
  */

  cupsdLogJob(job, CUPSD_LOG_INFO,
	      "File of type %s/%s queued by \"%s\".",
	      filetype->super, filetype->type, job->username);
  cupsdLogJob(job, CUPSD_LOG_DEBUG, "hold_until=%d", (int)job->hold_until);
  cupsdLogJob(job, CUPSD_LOG_INFO, "Queued on \"%s\" by \"%s\".",
	      job->dest, job->username);

 /*
  * Start the job if possible...
  */

  cupsdCheckJobs();
}


/*
 * 'read_job_ticket()' - Read a job ticket embedded in a print file.
 *
 * This function only gets called when printing a single PDF or PostScript
 * file using the Print-Job operation.  It doesn't work for Create-Job +
 * Send-File, since the job attributes need to be set at job creation
 * time for banners to work.  The embedded job ticket stuff is here
 * primarily to allow the Windows printer driver for CUPS to pass in JCL
 * options and IPP attributes which otherwise would be lost.
 *
 * The format of a job ticket is simple:
 *
 *     %cupsJobTicket: attr1=value1 attr2=value2 ... attrN=valueN
 *
 *     %cupsJobTicket: attr1=value1
 *     %cupsJobTicket: attr2=value2
 *     ...
 *     %cupsJobTicket: attrN=valueN
 *
 * Job ticket lines must appear immediately after the first line that
 * specifies PostScript (%!PS-Adobe-3.0) or PDF (%PDF) format, and CUPS
 * stops looking for job ticket info when it finds a line that does not begin
 * with "%cupsJobTicket:".
 *
 * The maximum length of a job ticket line, including the prefix, is
 * 255 characters to conform with the Adobe DSC.
 *
 * Read-only attributes are rejected with a notice to the error log in
 * case a malicious user tries anything.  Since the job ticket is read
 * prior to attribute validation in print_job(), job ticket attributes
 * will go through the same validation as IPP attributes...
 */

static void
read_job_ticket(cupsd_client_t *con)	/* I - Client connection */
{
  cups_file_t		*fp;		/* File to read from */
  char			line[256];	/* Line data */
  int			num_options;	/* Number of options */
  cups_option_t		*options;	/* Options */
  ipp_t			*ticket;	/* New attributes */
  ipp_attribute_t	*attr,		/* Current attribute */
			*attr2,		/* Job attribute */
			*prev2;		/* Previous job attribute */


 /*
  * First open the print file...
  */

  if ((fp = cupsFileOpen(con->filename, "rb")) == NULL)
  {
    cupsdLogMessage(CUPSD_LOG_ERROR,
                    "Unable to open print file for job ticket - %s",
                    strerror(errno));
    return;
  }

 /*
  * Skip the first line...
  */

  if (cupsFileGets(fp, line, sizeof(line)) == NULL)
  {
    cupsdLogMessage(CUPSD_LOG_ERROR,
                    "Unable to read from print file for job ticket - %s",
                    strerror(errno));
    cupsFileClose(fp);
    return;
  }

  if (strncmp(line, "%!PS-Adobe-", 11) && strncmp(line, "%PDF-", 5))
  {
   /*
    * Not a DSC-compliant file, so no job ticket info will be available...
    */

    cupsFileClose(fp);
    return;
  }

 /*
  * Read job ticket info from the file...
  */

  num_options = 0;
  options     = NULL;

  while (cupsFileGets(fp, line, sizeof(line)))
  {
   /*
    * Stop at the first non-ticket line...
    */

    if (strncmp(line, "%cupsJobTicket:", 15))
      break;

   /*
    * Add the options to the option array...
    */

    num_options = cupsParseOptions(line + 15, num_options, &options);
  }

 /*
  * Done with the file; see if we have any options...
  */

  cupsFileClose(fp);

  if (num_options == 0)
    return;

 /*
  * OK, convert the options to an attribute list, and apply them to
  * the request...
  */

  ticket = ippNew();
  cupsEncodeOptions(ticket, num_options, options);

 /*
  * See what the user wants to change.
  */

  for (attr = ticket->attrs; attr; attr = attr->next)
  {
    if (attr->group_tag != IPP_TAG_JOB || !attr->name)
      continue;

    if (!strncmp(attr->name, "date-time-at-", 13) ||
        !strcmp(attr->name, "job-impressions-completed") ||
	!strcmp(attr->name, "job-media-sheets-completed") ||
	!strncmp(attr->name, "job-k-octets", 12) ||
	!strcmp(attr->name, "job-id") ||
	!strcmp(attr->name, "job-originating-host-name") ||
        !strcmp(attr->name, "job-originating-user-name") ||
	!strcmp(attr->name, "job-pages-completed") ||
	!strcmp(attr->name, "job-printer-uri") ||
	!strncmp(attr->name, "job-state", 9) ||
	!strcmp(attr->name, "job-uri") ||
	!strncmp(attr->name, "time-at-", 8))
      continue; /* Read-only attrs */

    if ((attr2 = ippFindAttribute(con->request, attr->name,
                                  IPP_TAG_ZERO)) != NULL)
    {
     /*
      * Some other value; first free the old value...
      */

      if (con->request->attrs == attr2)
      {
	con->request->attrs = attr2->next;
	prev2               = NULL;
      }
      else
      {
	for (prev2 = con->request->attrs; prev2; prev2 = prev2->next)
	  if (prev2->next == attr2)
	  {
	    prev2->next = attr2->next;
	    break;
	  }
      }

      if (con->request->last == attr2)
        con->request->last = prev2;

      ippDeleteAttribute(NULL, attr2);
    }

   /*
    * Add new option by copying it...
    */

    ippCopyAttribute(con->request, attr, 0);
  }

 /*
  * Then free the attribute list and option array...
  */

  ippDelete(ticket);
  cupsFreeOptions(num_options, options);
}


/*
 * 'reject_jobs()' - Reject print jobs to a printer.
 */

static void
reject_jobs(cupsd_client_t  *con,	/* I - Client connection */
            ipp_attribute_t *uri)	/* I - Printer or class URI */
{
  http_status_t	status;			/* Policy status */
  cups_ptype_t	dtype;			/* Destination type (printer/class) */
  cupsd_printer_t *printer;		/* Printer data */
  ipp_attribute_t *attr;		/* printer-state-message text */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "reject_jobs(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * Is the destination valid?
  */

  if (!cupsdValidateDest(uri->values[0].string.text, &dtype, &printer))
  {
   /*
    * Bad URI...
    */

    send_ipp_status(con, IPP_NOT_FOUND,
                    _("The printer or class does not exist."));
    return;
  }

 /*
  * Check policy...
  */

  if ((status = cupsdCheckPolicy(printer->op_policy_ptr, con, NULL)) != HTTP_OK)
  {
    send_http_error(con, status, printer);
    return;
  }

 /*
  * Reject jobs sent to the printer...
  */

  printer->accepting = 0;

  if ((attr = ippFindAttribute(con->request, "printer-state-message",
                               IPP_TAG_TEXT)) == NULL)
    strlcpy(printer->state_message, "Rejecting Jobs",
            sizeof(printer->state_message));
  else
    strlcpy(printer->state_message, attr->values[0].string.text,
            sizeof(printer->state_message));

  cupsdAddEvent(CUPSD_EVENT_PRINTER_STATE, printer, NULL,
                "No longer accepting jobs.");

  if (dtype & CUPS_PRINTER_CLASS)
  {
    cupsdMarkDirty(CUPSD_DIRTY_CLASSES);

    cupsdLogMessage(CUPSD_LOG_INFO, "Class \"%s\" rejecting jobs (\"%s\").",
                    printer->name, get_username(con));
  }
  else
  {
    cupsdMarkDirty(CUPSD_DIRTY_PRINTERS);

    cupsdLogMessage(CUPSD_LOG_INFO, "Printer \"%s\" rejecting jobs (\"%s\").",
                    printer->name, get_username(con));
  }

 /*
  * Everything was ok, so return OK status...
  */

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'release_held_new_jobs()' - Release pending/new jobs on a printer or class.
 */

static void
release_held_new_jobs(
    cupsd_client_t  *con,		/* I - Connection */
    ipp_attribute_t *uri)		/* I - Printer URI */
{
  http_status_t		status;		/* Policy status */
  cups_ptype_t		dtype;		/* Destination type (printer/class) */
  cupsd_printer_t	*printer;	/* Printer data */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "release_held_new_jobs(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * Is the destination valid?
  */

  if (!cupsdValidateDest(uri->values[0].string.text, &dtype, &printer))
  {
   /*
    * Bad URI...
    */

    send_ipp_status(con, IPP_NOT_FOUND,
                    _("The printer or class does not exist."));
    return;
  }

 /*
  * Check policy...
  */

  if ((status = cupsdCheckPolicy(printer->op_policy_ptr, con, NULL)) != HTTP_OK)
  {
    send_http_error(con, status, printer);
    return;
  }

 /*
  * Hold pending/new jobs sent to the printer...
  */

  printer->holding_new_jobs = 0;

  cupsdSetPrinterReasons(printer, "-hold-new-jobs");

  if (dtype & CUPS_PRINTER_CLASS)
    cupsdLogMessage(CUPSD_LOG_INFO,
                    "Class \"%s\" now printing pending/new jobs (\"%s\").",
                    printer->name, get_username(con));
  else
    cupsdLogMessage(CUPSD_LOG_INFO,
                    "Printer \"%s\" now printing pending/new jobs (\"%s\").",
                    printer->name, get_username(con));

  cupsdCheckJobs();

 /*
  * Everything was ok, so return OK status...
  */

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'release_job()' - Release a held print job.
 */

static void
release_job(cupsd_client_t  *con,	/* I - Client connection */
            ipp_attribute_t *uri)	/* I - Job or Printer URI */
{
  ipp_attribute_t *attr;		/* Current attribute */
  int		jobid;			/* Job ID */
  char		scheme[HTTP_MAX_URI],	/* Method portion of URI */
		username[HTTP_MAX_URI],	/* Username portion of URI */
		host[HTTP_MAX_URI],	/* Host portion of URI */
		resource[HTTP_MAX_URI];	/* Resource portion of URI */
  int		port;			/* Port portion of URI */
  cupsd_job_t	*job;			/* Job information */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "release_job(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * See if we have a job URI or a printer URI...
  */

  if (!strcmp(uri->name, "printer-uri"))
  {
   /*
    * Got a printer URI; see if we also have a job-id attribute...
    */

    if ((attr = ippFindAttribute(con->request, "job-id",
                                 IPP_TAG_INTEGER)) == NULL)
    {
      send_ipp_status(con, IPP_BAD_REQUEST,
                      _("Got a printer-uri attribute but no job-id."));
      return;
    }

    jobid = attr->values[0].integer;
  }
  else
  {
   /*
    * Got a job URI; parse it to get the job ID...
    */

    httpSeparateURI(HTTP_URI_CODING_ALL, uri->values[0].string.text, scheme,
                    sizeof(scheme), username, sizeof(username), host,
		    sizeof(host), &port, resource, sizeof(resource));

    if (strncmp(resource, "/jobs/", 6))
    {
     /*
      * Not a valid URI!
      */

      send_ipp_status(con, IPP_BAD_REQUEST, _("Bad job-uri \"%s\"."),
                      uri->values[0].string.text);
      return;
    }

    jobid = atoi(resource + 6);
  }

 /*
  * See if the job exists...
  */

  if ((job = cupsdFindJob(jobid)) == NULL)
  {
   /*
    * Nope - return a "not found" error...
    */

    send_ipp_status(con, IPP_NOT_FOUND, _("Job #%d does not exist."), jobid);
    return;
  }

 /*
  * See if job is "held"...
  */

  if (job->state_value != IPP_JOB_HELD)
  {
   /*
    * Nope - return a "not possible" error...
    */

    send_ipp_status(con, IPP_NOT_POSSIBLE, _("Job #%d is not held."), jobid);
    return;
  }

 /*
  * See if the job is owned by the requesting user...
  */

  if (!validate_user(job, con, job->username, username, sizeof(username)))
  {
    send_http_error(con, con->username[0] ? HTTP_FORBIDDEN : HTTP_UNAUTHORIZED,
                    cupsdFindDest(job->dest));
    return;
  }

 /*
  * Reset the job-hold-until value to "no-hold"...
  */

  if ((attr = ippFindAttribute(job->attrs, "job-hold-until",
                               IPP_TAG_KEYWORD)) == NULL)
    attr = ippFindAttribute(job->attrs, "job-hold-until", IPP_TAG_NAME);

  if (attr)
  {
    ippSetValueTag(job->attrs, &attr, IPP_TAG_KEYWORD);
    ippSetString(job->attrs, &attr, 0, "no-hold");

    cupsdAddEvent(CUPSD_EVENT_JOB_CONFIG_CHANGED, cupsdFindDest(job->dest), job,
                  "Job job-hold-until value changed by user.");
    ippSetString(job->attrs, &job->reasons, 0, "none");
  }

 /*
  * Release the job and return...
  */

  cupsdReleaseJob(job);

  cupsdAddEvent(CUPSD_EVENT_JOB_STATE, cupsdFindDest(job->dest), job,
                "Job released by user.");

  cupsdLogJob(job, CUPSD_LOG_INFO, "Released by \"%s\".", username);

  con->response->request.status.status_code = IPP_OK;

  cupsdCheckJobs();
}


/*
 * 'renew_subscription()' - Renew an existing subscription...
 */

static void
renew_subscription(
    cupsd_client_t *con,		/* I - Client connection */
    int            sub_id)		/* I - Subscription ID */
{
  http_status_t		status;		/* Policy status */
  cupsd_subscription_t	*sub;		/* Subscription */
  ipp_attribute_t	*lease;		/* notify-lease-duration */


  cupsdLogMessage(CUPSD_LOG_DEBUG2,
                  "renew_subscription(con=%p[%d], sub_id=%d)",
                  (void *)con, con->number, sub_id);

 /*
  * Is the subscription ID valid?
  */

  if ((sub = cupsdFindSubscription(sub_id)) == NULL)
  {
   /*
    * Bad subscription ID...
    */

    send_ipp_status(con, IPP_NOT_FOUND, _("Subscription #%d does not exist."),
                    sub_id);
    return;
  }

  if (sub->job)
  {
   /*
    * Job subscriptions cannot be renewed...
    */

    send_ipp_status(con, IPP_NOT_POSSIBLE,
                    _("Job subscriptions cannot be renewed."));
    return;
  }

 /*
  * Check policy...
  */

  if ((status = cupsdCheckPolicy(sub->dest ? sub->dest->op_policy_ptr :
                                             DefaultPolicyPtr,
                                 con, sub->owner)) != HTTP_OK)
  {
    send_http_error(con, status, sub->dest);
    return;
  }

 /*
  * Renew the subscription...
  */

  lease = ippFindAttribute(con->request, "notify-lease-duration",
                           IPP_TAG_INTEGER);

  sub->lease = lease ? lease->values[0].integer : DefaultLeaseDuration;

  if (MaxLeaseDuration && (sub->lease == 0 || sub->lease > MaxLeaseDuration))
  {
    cupsdLogMessage(CUPSD_LOG_INFO,
                    "renew_subscription: Limiting notify-lease-duration to "
		    "%d seconds.",
		    MaxLeaseDuration);
    sub->lease = MaxLeaseDuration;
  }

  sub->expire = sub->lease ? time(NULL) + sub->lease : 0;

  cupsdMarkDirty(CUPSD_DIRTY_SUBSCRIPTIONS);

  con->response->request.status.status_code = IPP_OK;

  ippAddInteger(con->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER,
                "notify-lease-duration", sub->lease);
}


/*
 * 'restart_job()' - Restart an old print job.
 */

static void
restart_job(cupsd_client_t  *con,	/* I - Client connection */
            ipp_attribute_t *uri)	/* I - Job or Printer URI */
{
  ipp_attribute_t *attr;		/* Current attribute */
  int		jobid;			/* Job ID */
  cupsd_job_t	*job;			/* Job information */
  char		scheme[HTTP_MAX_URI],	/* Method portion of URI */
		username[HTTP_MAX_URI],	/* Username portion of URI */
		host[HTTP_MAX_URI],	/* Host portion of URI */
		resource[HTTP_MAX_URI];	/* Resource portion of URI */
  int		port;			/* Port portion of URI */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "restart_job(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * See if we have a job URI or a printer URI...
  */

  if (!strcmp(uri->name, "printer-uri"))
  {
   /*
    * Got a printer URI; see if we also have a job-id attribute...
    */

    if ((attr = ippFindAttribute(con->request, "job-id",
                                 IPP_TAG_INTEGER)) == NULL)
    {
      send_ipp_status(con, IPP_BAD_REQUEST,
                      _("Got a printer-uri attribute but no job-id."));
      return;
    }

    jobid = attr->values[0].integer;
  }
  else
  {
   /*
    * Got a job URI; parse it to get the job ID...
    */

    httpSeparateURI(HTTP_URI_CODING_ALL, uri->values[0].string.text, scheme,
                    sizeof(scheme), username, sizeof(username), host,
		    sizeof(host), &port, resource, sizeof(resource));

    if (strncmp(resource, "/jobs/", 6))
    {
     /*
      * Not a valid URI!
      */

      send_ipp_status(con, IPP_BAD_REQUEST, _("Bad job-uri \"%s\"."),
                      uri->values[0].string.text);
      return;
    }

    jobid = atoi(resource + 6);
  }

 /*
  * See if the job exists...
  */

  if ((job = cupsdFindJob(jobid)) == NULL)
  {
   /*
    * Nope - return a "not found" error...
    */

    send_ipp_status(con, IPP_NOT_FOUND, _("Job #%d does not exist."), jobid);
    return;
  }

 /*
  * See if job is in any of the "completed" states...
  */

  if (job->state_value <= IPP_JOB_PROCESSING)
  {
   /*
    * Nope - return a "not possible" error...
    */

    send_ipp_status(con, IPP_NOT_POSSIBLE, _("Job #%d is not complete."),
                    jobid);
    return;
  }

 /*
  * See if we have retained the job files...
  */

  cupsdLoadJob(job);

  if (!job->attrs || job->num_files == 0)
  {
   /*
    * Nope - return a "not possible" error...
    */

    send_ipp_status(con, IPP_NOT_POSSIBLE,
                    _("Job #%d cannot be restarted - no files."), jobid);
    return;
  }

 /*
  * See if the job is owned by the requesting user...
  */

  if (!validate_user(job, con, job->username, username, sizeof(username)))
  {
    send_http_error(con, con->username[0] ? HTTP_FORBIDDEN : HTTP_UNAUTHORIZED,
                    cupsdFindDest(job->dest));
    return;
  }

 /*
  * See if the job-hold-until attribute is specified...
  */

  if ((attr = ippFindAttribute(con->request, "job-hold-until",
                               IPP_TAG_KEYWORD)) == NULL)
    attr = ippFindAttribute(con->request, "job-hold-until", IPP_TAG_NAME);

  if (attr && strcmp(attr->values[0].string.text, "no-hold"))
  {
   /*
    * Return the job to a held state...
    */

    cupsdLogJob(job, CUPSD_LOG_DEBUG,
		"Restarted by \"%s\" with job-hold-until=%s.",
                username, attr->values[0].string.text);
    cupsdSetJobHoldUntil(job, attr->values[0].string.text, 1);
    cupsdSetJobState(job, IPP_JOB_HELD, CUPSD_JOB_DEFAULT,
		     "Job restarted by user with job-hold-until=%s",
		     attr->values[0].string.text);
  }
  else
  {
   /*
    * Restart the job...
    */

    cupsdRestartJob(job);
    cupsdCheckJobs();
  }

  cupsdLogJob(job, CUPSD_LOG_INFO, "Restarted by \"%s\".", username);

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'save_auth_info()' - Save authentication information for a job.
 */

static void
save_auth_info(
    cupsd_client_t  *con,		/* I - Client connection */
    cupsd_job_t     *job,		/* I - Job */
    ipp_attribute_t *auth_info)		/* I - auth-info attribute, if any */
{
  int			i;		/* Looping var */
  char			filename[1024];	/* Job authentication filename */
  cups_file_t		*fp;		/* Job authentication file */
  char			line[65536];	/* Line for file */
  cupsd_printer_t	*dest;		/* Destination printer/class */


 /*
  * This function saves the in-memory authentication information for
  * a job so that it can be used to authenticate with a remote host.
  * The information is stored in a file that is readable only by the
  * root user.  The fields are Base-64 encoded, each on a separate line,
  * followed by random number (up to 1024) of newlines to limit the
  * amount of information that is exposed.
  *
  * Because of the potential for exposing of authentication information,
  * this functionality is only enabled when running cupsd as root.
  *
  * This caching only works for the Basic and BasicDigest authentication
  * types.  Digest authentication cannot be cached this way, and in
  * the future Kerberos authentication may make all of this obsolete.
  *
  * Authentication information is saved whenever an authenticated
  * Print-Job, Create-Job, or CUPS-Authenticate-Job operation is
  * performed.
  *
  * This information is deleted after a job is completed or canceled,
  * so reprints may require subsequent re-authentication.
  */

  if (RunUser)
    return;

  if ((dest = cupsdFindDest(job->dest)) == NULL)
    return;

 /*
  * Create the authentication file and change permissions...
  */

  snprintf(filename, sizeof(filename), "%s/a%05d", RequestRoot, job->id);
  if ((fp = cupsFileOpen(filename, "w")) == NULL)
  {
    cupsdLogMessage(CUPSD_LOG_ERROR,
                    "Unable to save authentication info to \"%s\" - %s",
                    filename, strerror(errno));
    return;
  }

  fchown(cupsFileNumber(fp), 0, 0);
  fchmod(cupsFileNumber(fp), 0400);

  cupsFilePuts(fp, "CUPSD-AUTH-V3\n");

  for (i = 0;
       i < (int)(sizeof(job->auth_env) / sizeof(job->auth_env[0]));
       i ++)
    cupsdClearString(job->auth_env + i);

  if (auth_info && auth_info->num_values == dest->num_auth_info_required)
  {
   /*
    * Write 1 to 3 auth values...
    */

    for (i = 0;
         i < auth_info->num_values &&
	     i < (int)(sizeof(job->auth_env) / sizeof(job->auth_env[0]));
	 i ++)
    {
      if (strcmp(dest->auth_info_required[i], "negotiate"))
      {
	httpEncode64_2(line, sizeof(line), auth_info->values[i].string.text, (int)strlen(auth_info->values[i].string.text));
	cupsFilePutConf(fp, dest->auth_info_required[i], line);
      }
      else
	cupsFilePutConf(fp, dest->auth_info_required[i],
	                auth_info->values[i].string.text);

      if (!strcmp(dest->auth_info_required[i], "username"))
        cupsdSetStringf(job->auth_env + i, "AUTH_USERNAME=%s",
	                auth_info->values[i].string.text);
      else if (!strcmp(dest->auth_info_required[i], "domain"))
        cupsdSetStringf(job->auth_env + i, "AUTH_DOMAIN=%s",
	                auth_info->values[i].string.text);
      else if (!strcmp(dest->auth_info_required[i], "password"))
        cupsdSetStringf(job->auth_env + i, "AUTH_PASSWORD=%s",
	                auth_info->values[i].string.text);
      else if (!strcmp(dest->auth_info_required[i], "negotiate"))
        cupsdSetStringf(job->auth_env + i, "AUTH_NEGOTIATE=%s",
	                auth_info->values[i].string.text);
      else
        i --;
    }
  }
  else if (auth_info && auth_info->num_values == 2 &&
           dest->num_auth_info_required == 1 &&
           !strcmp(dest->auth_info_required[0], "negotiate"))
  {
   /*
    * Allow fallback to username+password for Kerberized queues...
    */

    httpEncode64_2(line, sizeof(line), auth_info->values[0].string.text, (int)strlen(auth_info->values[0].string.text));
    cupsFilePutConf(fp, "username", line);

    cupsdSetStringf(job->auth_env + 0, "AUTH_USERNAME=%s",
                    auth_info->values[0].string.text);

    httpEncode64_2(line, sizeof(line), auth_info->values[1].string.text, (int)strlen(auth_info->values[1].string.text));
    cupsFilePutConf(fp, "password", line);

    cupsdSetStringf(job->auth_env + 1, "AUTH_PASSWORD=%s",
                    auth_info->values[1].string.text);
  }
  else if (con->username[0])
  {
   /*
    * Write the authenticated username...
    */

    httpEncode64_2(line, sizeof(line), con->username, (int)strlen(con->username));
    cupsFilePutConf(fp, "username", line);

    cupsdSetStringf(job->auth_env + 0, "AUTH_USERNAME=%s", con->username);

   /*
    * Write the authenticated password...
    */

    httpEncode64_2(line, sizeof(line), con->password, (int)strlen(con->password));
    cupsFilePutConf(fp, "password", line);

    cupsdSetStringf(job->auth_env + 1, "AUTH_PASSWORD=%s", con->password);
  }

#ifdef HAVE_GSSAPI
  if (con->gss_uid > 0)
  {
    cupsFilePrintf(fp, "uid %d\n", (int)con->gss_uid);
    cupsdSetStringf(&job->auth_uid, "AUTH_UID=%d", (int)con->gss_uid);
  }
#endif /* HAVE_GSSAPI */

 /*
  * Write a random number of newlines to the end of the file...
  */

  for (i = (CUPS_RAND() % 1024); i >= 0; i --)
    cupsFilePutChar(fp, '\n');

 /*
  * Close the file and return...
  */

  cupsFileClose(fp);
}


/*
 * 'send_document()' - Send a file to a printer or class.
 */

static void
send_document(cupsd_client_t  *con,	/* I - Client connection */
	      ipp_attribute_t *uri)	/* I - Printer URI */
{
  ipp_attribute_t	*attr;		/* Current attribute */
  ipp_attribute_t	*format;	/* Request's document-format attribute */
  ipp_attribute_t	*jformat;	/* Job's document-format attribute */
  const char		*default_format;/* document-format-default value */
  int			jobid;		/* Job ID number */
  cupsd_job_t		*job;		/* Current job */
  char			job_uri[HTTP_MAX_URI],
					/* Job URI */
			scheme[HTTP_MAX_URI],
					/* Method portion of URI */
			username[HTTP_MAX_URI],
					/* Username portion of URI */
			host[HTTP_MAX_URI],
					/* Host portion of URI */
			resource[HTTP_MAX_URI];
					/* Resource portion of URI */
  int			port;		/* Port portion of URI */
  mime_type_t		*filetype;	/* Type of file */
  char			super[MIME_MAX_SUPER],
					/* Supertype of file */
			type[MIME_MAX_TYPE],
					/* Subtype of file */
			mimetype[MIME_MAX_SUPER + MIME_MAX_TYPE + 2];
					/* Textual name of mime type */
  char			filename[1024];	/* Job filename */
  cupsd_printer_t	*printer;	/* Current printer */
  struct stat		fileinfo;	/* File information */
  int			kbytes;		/* Size of file */
  int			compression;	/* Type of compression */
  int			start_job;	/* Start the job? */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "send_document(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * See if we have a job URI or a printer URI...
  */

  if (!strcmp(uri->name, "printer-uri"))
  {
   /*
    * Got a printer URI; see if we also have a job-id attribute...
    */

    if ((attr = ippFindAttribute(con->request, "job-id",
                                 IPP_TAG_INTEGER)) == NULL)
    {
      send_ipp_status(con, IPP_BAD_REQUEST,
                      _("Got a printer-uri attribute but no job-id."));
      return;
    }

    jobid = attr->values[0].integer;
  }
  else
  {
   /*
    * Got a job URI; parse it to get the job ID...
    */

    httpSeparateURI(HTTP_URI_CODING_ALL, uri->values[0].string.text, scheme,
                    sizeof(scheme), username, sizeof(username), host,
		    sizeof(host), &port, resource, sizeof(resource));

    if (strncmp(resource, "/jobs/", 6))
    {
     /*
      * Not a valid URI!
      */

      send_ipp_status(con, IPP_BAD_REQUEST, _("Bad job-uri \"%s\"."),
                      uri->values[0].string.text);
      return;
    }

    jobid = atoi(resource + 6);
  }

 /*
  * See if the job exists...
  */

  if ((job = cupsdFindJob(jobid)) == NULL)
  {
   /*
    * Nope - return a "not found" error...
    */

    send_ipp_status(con, IPP_NOT_FOUND, _("Job #%d does not exist."), jobid);
    return;
  }

  printer = cupsdFindDest(job->dest);

 /*
  * See if the job is owned by the requesting user...
  */

  if (!validate_user(job, con, job->username, username, sizeof(username)))
  {
    send_http_error(con, con->username[0] ? HTTP_FORBIDDEN : HTTP_UNAUTHORIZED,
                    cupsdFindDest(job->dest));
    return;
  }

 /*
  * OK, see if the client is sending the document compressed - CUPS
  * only supports "none" and "gzip".
  */

  compression = CUPS_FILE_NONE;

  if ((attr = ippFindAttribute(con->request, "compression",
                               IPP_TAG_KEYWORD)) != NULL)
  {
    if (strcmp(attr->values[0].string.text, "none")
#ifdef HAVE_LIBZ
        && strcmp(attr->values[0].string.text, "gzip")
#endif /* HAVE_LIBZ */
      )
    {
      send_ipp_status(con, IPP_ATTRIBUTES, _("Unsupported compression \"%s\"."),
        	      attr->values[0].string.text);
      ippAddString(con->response, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_KEYWORD,
	           "compression", NULL, attr->values[0].string.text);
      return;
    }

#ifdef HAVE_LIBZ
    if (!strcmp(attr->values[0].string.text, "gzip"))
      compression = CUPS_FILE_GZIP;
#endif /* HAVE_LIBZ */
  }

 /*
  * Do we have a file to print?
  */

  if ((attr = ippFindAttribute(con->request, "last-document",
	                       IPP_TAG_BOOLEAN)) == NULL)
  {
    send_ipp_status(con, IPP_BAD_REQUEST,
                    _("Missing last-document attribute in request."));
    return;
  }

  if (!con->filename)
  {
   /*
    * Check for an empty request with "last-document" set to true, which is
    * used to close an "open" job by RFC 2911, section 3.3.2.
    */

    if (job->num_files > 0 && attr->values[0].boolean)
      goto last_document;

    send_ipp_status(con, IPP_BAD_REQUEST, _("No file in print request."));
    return;
  }

 /*
  * Is it a format we support?
  */

  cupsdLoadJob(job);

  if ((format = ippFindAttribute(con->request, "document-format",
                                 IPP_TAG_MIMETYPE)) != NULL)
  {
   /*
    * Grab format from client...
    */

    if (sscanf(format->values[0].string.text, "%15[^/]/%255[^;]",
               super, type) != 2)
    {
      send_ipp_status(con, IPP_BAD_REQUEST, _("Bad document-format \"%s\"."),
	              format->values[0].string.text);
      return;
    }

    ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_MIMETYPE, "document-format-supplied", NULL, ippGetString(format, 0, NULL));
  }
  else if ((default_format = cupsGetOption("document-format",
                                           printer->num_options,
					   printer->options)) != NULL)
  {
   /*
    * Use default document format...
    */

    if (sscanf(default_format, "%15[^/]/%255[^;]", super, type) != 2)
    {
      send_ipp_status(con, IPP_BAD_REQUEST,
                      _("Bad document-format-default \"%s\"."), default_format);
      return;
    }
  }
  else
  {
   /*
    * No document format attribute?  Auto-type it!
    */

    strlcpy(super, "application", sizeof(super));
    strlcpy(type, "octet-stream", sizeof(type));
  }

  _cupsRWLockRead(&MimeDatabase->lock);

  if (!strcmp(super, "application") && !strcmp(type, "octet-stream"))
  {
   /*
    * Auto-type the file...
    */

    ipp_attribute_t	*doc_name;	/* document-name attribute */


    cupsdLogJob(job, CUPSD_LOG_DEBUG, "Auto-typing file...");

    doc_name = ippFindAttribute(con->request, "document-name", IPP_TAG_NAME);
    filetype = mimeFileType(MimeDatabase, con->filename,
                            doc_name ? doc_name->values[0].string.text : NULL,
			    &compression);

    if (!filetype)
      filetype = mimeType(MimeDatabase, super, type);

    if (filetype)
      cupsdLogJob(job, CUPSD_LOG_DEBUG, "Request file type is %s/%s.",
		  filetype->super, filetype->type);

    snprintf(mimetype, sizeof(mimetype), "%s/%s", filetype->super, filetype->type);
    ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_MIMETYPE, "document-format-detected", NULL, mimetype);
  }
  else
    filetype = mimeType(MimeDatabase, super, type);

  _cupsRWUnlock(&MimeDatabase->lock);

  if (filetype)
  {
   /*
    * Replace the document-format attribute value with the auto-typed or
    * default one.
    */

    snprintf(mimetype, sizeof(mimetype), "%s/%s", filetype->super,
             filetype->type);

    if ((jformat = ippFindAttribute(job->attrs, "document-format",
                                    IPP_TAG_MIMETYPE)) != NULL)
      ippSetString(job->attrs, &jformat, 0, mimetype);
    else
      ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_MIMETYPE,
	           "document-format", NULL, mimetype);
  }
  else if (!filetype)
  {
    send_ipp_status(con, IPP_DOCUMENT_FORMAT,
                    _("Unsupported document-format \"%s/%s\"."), super, type);
    cupsdLogMessage(CUPSD_LOG_INFO,
                    "Hint: Do you have the raw file printing rules enabled?");

    if (format)
      ippAddString(con->response, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_MIMETYPE,
                   "document-format", NULL, format->values[0].string.text);

    return;
  }

  if (printer->filetypes && !cupsArrayFind(printer->filetypes, filetype))
  {
    snprintf(mimetype, sizeof(mimetype), "%s/%s", filetype->super,
             filetype->type);

    send_ipp_status(con, IPP_DOCUMENT_FORMAT,
                    _("Unsupported document-format \"%s\"."), mimetype);

    ippAddString(con->response, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_MIMETYPE,
                 "document-format", NULL, mimetype);

    return;
  }

 /*
  * Add the file to the job...
  */

  if (add_file(con, job, filetype, compression))
    return;

  if ((attr = ippFindAttribute(con->request, "document-name", IPP_TAG_NAME)) != NULL)
    ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_NAME, "document-name-supplied", NULL, ippGetString(attr, 0, NULL));

  if (stat(con->filename, &fileinfo))
    kbytes = 0;
  else
    kbytes = (fileinfo.st_size + 1023) / 1024;

  cupsdUpdateQuota(printer, job->username, 0, kbytes);

  job->koctets += kbytes;

  if ((attr = ippFindAttribute(job->attrs, "job-k-octets", IPP_TAG_INTEGER)) != NULL)
    attr->values[0].integer += kbytes;

  snprintf(filename, sizeof(filename), "%s/d%05d-%03d", RequestRoot, job->id, job->num_files);
  if (rename(con->filename, filename))
  {
    cupsdLogJob(job, CUPSD_LOG_ERROR, "Unable to rename job document file \"%s\": %s", filename, strerror(errno));

    send_ipp_status(con, IPP_INTERNAL_ERROR, _("Unable to rename job document file."));
    return;
  }

  cupsdClearString(&con->filename);

  cupsdLogJob(job, CUPSD_LOG_INFO, "File of type %s/%s queued by \"%s\".",
	      filetype->super, filetype->type, job->username);

 /*
  * Start the job if this is the last document...
  */

  last_document:

  if ((attr = ippFindAttribute(con->request, "last-document",
                               IPP_TAG_BOOLEAN)) != NULL &&
      attr->values[0].boolean)
  {
   /*
    * See if we need to add the ending sheet...
    */

    if (cupsdTimeoutJob(job))
      return;

    if (job->state_value == IPP_JOB_STOPPED)
    {
      job->state->values[0].integer = IPP_JOB_PENDING;
      job->state_value              = IPP_JOB_PENDING;

      ippSetString(job->attrs, &job->reasons, 0, "none");
    }
    else if (job->state_value == IPP_JOB_HELD)
    {
      if ((attr = ippFindAttribute(job->attrs, "job-hold-until",
                                   IPP_TAG_KEYWORD)) == NULL)
	attr = ippFindAttribute(job->attrs, "job-hold-until", IPP_TAG_NAME);

      if (!attr || !strcmp(attr->values[0].string.text, "no-hold"))
      {
	job->state->values[0].integer = IPP_JOB_PENDING;
	job->state_value              = IPP_JOB_PENDING;

	ippSetString(job->attrs, &job->reasons, 0, "none");
      }
      else
	ippSetString(job->attrs, &job->reasons, 0, "job-hold-until-specified");
    }

    job->dirty = 1;
    cupsdMarkDirty(CUPSD_DIRTY_JOBS);

    start_job = 1;
  }
  else
  {
    if ((attr = ippFindAttribute(job->attrs, "job-hold-until",
                                 IPP_TAG_KEYWORD)) == NULL)
      attr = ippFindAttribute(job->attrs, "job-hold-until", IPP_TAG_NAME);

    if (!attr || !strcmp(attr->values[0].string.text, "no-hold"))
    {
      job->state->values[0].integer = IPP_JOB_HELD;
      job->state_value              = IPP_JOB_HELD;
      job->hold_until               = time(NULL) + MultipleOperationTimeout;

      ippSetString(job->attrs, &job->reasons, 0, "job-incoming");

      job->dirty = 1;
      cupsdMarkDirty(CUPSD_DIRTY_JOBS);
    }

    start_job = 0;
  }

 /*
  * Fill in the response info...
  */

  httpAssembleURIf(HTTP_URI_CODING_ALL, job_uri, sizeof(job_uri), "ipp", NULL, con->clientname, con->clientport, "/jobs/%d", jobid);
  ippAddString(con->response, IPP_TAG_JOB, IPP_TAG_URI, "job-uri", NULL, job_uri);

  ippAddInteger(con->response, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-id", jobid);

  ippAddInteger(con->response, IPP_TAG_JOB, IPP_TAG_ENUM, "job-state", (int)job->state_value);
  ippAddString(con->response, IPP_TAG_JOB, IPP_TAG_KEYWORD, "job-state-reasons", NULL, job->reasons->values[0].string.text);

  con->response->request.status.status_code = IPP_OK;

 /*
  * Start the job if necessary...
  */

  if (start_job)
    cupsdCheckJobs();
}


/*
 * 'send_http_error()' - Send a HTTP error back to the IPP client.
 */

static void
send_http_error(
    cupsd_client_t  *con,		/* I - Client connection */
    http_status_t   status,		/* I - HTTP status code */
    cupsd_printer_t *printer)		/* I - Printer, if any */
{
  ipp_attribute_t	*uri;		/* Request URI, if any */


  if ((uri = ippFindAttribute(con->request, "printer-uri",
                              IPP_TAG_URI)) == NULL)
    uri = ippFindAttribute(con->request, "job-uri", IPP_TAG_URI);

  cupsdLogMessage(status == HTTP_FORBIDDEN ? CUPSD_LOG_ERROR : CUPSD_LOG_DEBUG,
                  "[Client %d] Returning HTTP %s for %s (%s) from %s",
                  con->number, httpStatus(status),
		  con->request ?
		      ippOpString(con->request->request.op.operation_id) :
		      "no operation-id",
		  uri ? uri->values[0].string.text : "no URI",
		  con->http->hostname);

  if (printer)
  {
    int		auth_type;		/* Type of authentication required */


    auth_type = CUPSD_AUTH_NONE;

    if (status == HTTP_UNAUTHORIZED &&
        printer->num_auth_info_required > 0 &&
        !strcmp(printer->auth_info_required[0], "negotiate") &&
	con->request &&
	(con->request->request.op.operation_id == IPP_PRINT_JOB ||
	 con->request->request.op.operation_id == IPP_CREATE_JOB ||
	 con->request->request.op.operation_id == CUPS_AUTHENTICATE_JOB))
    {
     /*
      * Creating and authenticating jobs requires Kerberos...
      */

      auth_type = CUPSD_AUTH_NEGOTIATE;
    }
    else
    {
     /*
      * Use policy/location-defined authentication requirements...
      */

      char	resource[HTTP_MAX_URI];	/* Resource portion of URI */
      cupsd_location_t *auth;		/* Pointer to authentication element */


      if (printer->type & CUPS_PRINTER_CLASS)
	snprintf(resource, sizeof(resource), "/classes/%s", printer->name);
      else
	snprintf(resource, sizeof(resource), "/printers/%s", printer->name);

      if ((auth = cupsdFindBest(resource, HTTP_POST)) == NULL ||
	  auth->type == CUPSD_AUTH_NONE)
	auth = cupsdFindPolicyOp(printer->op_policy_ptr,
				 con->request ?
				     con->request->request.op.operation_id :
				     IPP_PRINT_JOB);

      if (auth)
      {
        if (auth->type == CUPSD_AUTH_DEFAULT)
	  auth_type = cupsdDefaultAuthType();
	else
	  auth_type = auth->type;
      }
    }

    cupsdSendError(con, status, auth_type);
  }
  else
    cupsdSendError(con, status, CUPSD_AUTH_NONE);

  ippDelete(con->response);
  con->response = NULL;

  return;
}


/*
 * 'send_ipp_status()' - Send a status back to the IPP client.
 */

static void
send_ipp_status(cupsd_client_t *con,	/* I - Client connection */
                ipp_status_t   status,	/* I - IPP status code */
	        const char     *message,/* I - Status message */
	        ...)			/* I - Additional args as needed */
{
  va_list	ap;			/* Pointer to additional args */
  char		formatted[1024];	/* Formatted error message */


  va_start(ap, message);
  vsnprintf(formatted, sizeof(formatted),
            _cupsLangString(con->language, message), ap);
  va_end(ap);

  cupsdLogMessage(CUPSD_LOG_DEBUG, "%s %s: %s",
		  ippOpString(con->request->request.op.operation_id),
		  ippErrorString(status), formatted);

  con->response->request.status.status_code = status;

  if (ippFindAttribute(con->response, "attributes-charset",
                       IPP_TAG_ZERO) == NULL)
    ippAddString(con->response, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
                 "attributes-charset", NULL, "utf-8");

  if (ippFindAttribute(con->response, "attributes-natural-language",
                       IPP_TAG_ZERO) == NULL)
    ippAddString(con->response, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
                 "attributes-natural-language", NULL, DefaultLanguage);

  ippAddString(con->response, IPP_TAG_OPERATION, IPP_TAG_TEXT,
               "status-message", NULL, formatted);
}


/*
 * 'send_response()' - Send the IPP response.
 */

static int				/* O - 1 on success, 0 on failure */
send_response(cupsd_client_t *con)	/* I - Client */
{
  ipp_attribute_t	*uri;		/* Target URI */
  int			ret = 0;	/* Return value */
  static _cups_mutex_t	mutex = _CUPS_MUTEX_INITIALIZER;
					/* Mutex for logging/access */


  _cupsMutexLock(&mutex);

  if ((uri = ippFindAttribute(con->request, "printer-uri", IPP_TAG_URI)) == NULL)
  {
    if ((uri = ippFindAttribute(con->request, "job-uri", IPP_TAG_URI)) == NULL)
      uri = ippFindAttribute(con->request, "ppd-name", IPP_TAG_NAME);
  }

  cupsdLogClient(con, con->response->request.status.status_code >= IPP_STATUS_ERROR_BAD_REQUEST && con->response->request.status.status_code != IPP_STATUS_ERROR_NOT_FOUND ? CUPSD_LOG_ERROR : CUPSD_LOG_DEBUG, "Returning IPP %s for %s (%s) from %s.",  ippErrorString(con->response->request.status.status_code), ippOpString(con->request->request.op.operation_id), uri ? uri->values[0].string.text : "no URI", con->http->hostname);

  httpClearFields(con->http);

#ifdef CUPSD_USE_CHUNKING
 /*
  * Because older versions of CUPS (1.1.17 and older) and some IPP
  * clients do not implement chunking properly, we cannot use
  * chunking by default.  This may become the default in future
  * CUPS releases, or we might add a configuration directive for
  * it.
  */

  if (con->http->version == HTTP_1_1)
  {
    cupsdLogClient(con, CUPSD_LOG_DEBUG, "Transfer-Encoding: chunked");
    cupsdSetLength(con->http, 0);
  }
  else
#endif /* CUPSD_USE_CHUNKING */
  {
    size_t	length;			/* Length of response */


    length = ippLength(con->response);

    if (con->file >= 0 && !con->pipe_pid)
    {
      struct stat	fileinfo;	/* File information */

      if (!fstat(con->file, &fileinfo))
	length += (size_t)fileinfo.st_size;
    }

    cupsdLogClient(con, CUPSD_LOG_DEBUG, "Content-Length: " CUPS_LLFMT, CUPS_LLCAST length);
    httpSetLength(con->http, length);
  }

  if (cupsdSendHeader(con, HTTP_STATUS_OK, "application/ipp", CUPSD_AUTH_NONE))
  {
   /*
    * Tell the caller the response header was sent successfully...
    */

    cupsdAddSelect(httpGetFd(con->http), (cupsd_selfunc_t)cupsdReadClient, (cupsd_selfunc_t)cupsdWriteClient, con);

    ret = 1;
  }

  _cupsMutexUnlock(&mutex);

  return (ret);
}


/*
 * 'set_default()' - Set the default destination...
 */

static void
set_default(cupsd_client_t  *con,	/* I - Client connection */
            ipp_attribute_t *uri)	/* I - Printer URI */
{
  http_status_t		status;		/* Policy status */
  cups_ptype_t		dtype;		/* Destination type (printer/class) */
  cupsd_printer_t	*printer,	/* Printer */
			*oldprinter;	/* Old default printer */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "set_default(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * Is the destination valid?
  */

  if (!cupsdValidateDest(uri->values[0].string.text, &dtype, &printer))
  {
   /*
    * Bad URI...
    */

    send_ipp_status(con, IPP_NOT_FOUND,
                    _("The printer or class does not exist."));
    return;
  }

 /*
  * Check policy...
  */

  if ((status = cupsdCheckPolicy(DefaultPolicyPtr, con, NULL)) != HTTP_OK)
  {
    send_http_error(con, status, NULL);
    return;
  }

 /*
  * Set it as the default...
  */

  oldprinter     = DefaultPrinter;
  DefaultPrinter = printer;

  if (oldprinter)
    cupsdAddEvent(CUPSD_EVENT_PRINTER_STATE, oldprinter, NULL,
                  "%s is no longer the default printer.", oldprinter->name);

  cupsdAddEvent(CUPSD_EVENT_PRINTER_STATE, printer, NULL,
		"%s is now the default printer.", printer->name);

  cupsdMarkDirty(CUPSD_DIRTY_PRINTERS | CUPSD_DIRTY_CLASSES |
                 CUPSD_DIRTY_PRINTCAP);

  cupsdLogMessage(CUPSD_LOG_INFO,
                  "Default destination set to \"%s\" by \"%s\".",
		  printer->name, get_username(con));

 /*
  * Everything was ok, so return OK status...
  */

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'set_job_attrs()' - Set job attributes.
 */

static void
set_job_attrs(cupsd_client_t  *con,	/* I - Client connection */
	      ipp_attribute_t *uri)	/* I - Job URI */
{
  ipp_attribute_t	*attr,		/* Current attribute */
			*attr2;		/* Job attribute */
  int			jobid;		/* Job ID */
  cupsd_job_t		*job;		/* Current job */
  char			scheme[HTTP_MAX_URI],
					/* Method portion of URI */
			username[HTTP_MAX_URI],
					/* Username portion of URI */
			host[HTTP_MAX_URI],
					/* Host portion of URI */
			resource[HTTP_MAX_URI];
					/* Resource portion of URI */
  int			port;		/* Port portion of URI */
  int			event;		/* Events? */
  int			check_jobs;	/* Check jobs? */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "set_job_attrs(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * Start with "everything is OK" status...
  */

  con->response->request.status.status_code = IPP_OK;

 /*
  * See if we have a job URI or a printer URI...
  */

  if (!strcmp(uri->name, "printer-uri"))
  {
   /*
    * Got a printer URI; see if we also have a job-id attribute...
    */

    if ((attr = ippFindAttribute(con->request, "job-id",
                                 IPP_TAG_INTEGER)) == NULL)
    {
      send_ipp_status(con, IPP_BAD_REQUEST,
                      _("Got a printer-uri attribute but no job-id."));
      return;
    }

    jobid = attr->values[0].integer;
  }
  else
  {
   /*
    * Got a job URI; parse it to get the job ID...
    */

    httpSeparateURI(HTTP_URI_CODING_ALL, uri->values[0].string.text, scheme,
                    sizeof(scheme), username, sizeof(username), host,
		    sizeof(host), &port, resource, sizeof(resource));

    if (strncmp(resource, "/jobs/", 6))
    {
     /*
      * Not a valid URI!
      */

      send_ipp_status(con, IPP_BAD_REQUEST, _("Bad job-uri \"%s\"."),
                      uri->values[0].string.text);
      return;
    }

    jobid = atoi(resource + 6);
  }

 /*
  * See if the job exists...
  */

  if ((job = cupsdFindJob(jobid)) == NULL)
  {
   /*
    * Nope - return a "not found" error...
    */

    send_ipp_status(con, IPP_NOT_FOUND, _("Job #%d does not exist."), jobid);
    return;
  }

 /*
  * See if the job has been completed...
  */

  if (job->state_value > IPP_JOB_STOPPED)
  {
   /*
    * Return a "not-possible" error...
    */

    send_ipp_status(con, IPP_NOT_POSSIBLE,
                    _("Job #%d is finished and cannot be altered."), jobid);
    return;
  }

 /*
  * See if the job is owned by the requesting user...
  */

  if (!validate_user(job, con, job->username, username, sizeof(username)))
  {
    send_http_error(con, con->username[0] ? HTTP_FORBIDDEN : HTTP_UNAUTHORIZED,
                    cupsdFindDest(job->dest));
    return;
  }

 /*
  * See what the user wants to change.
  */

  cupsdLoadJob(job);

  check_jobs = 0;
  event      = 0;

  for (attr = con->request->attrs; attr; attr = attr->next)
  {
    if (attr->group_tag != IPP_TAG_JOB || !attr->name)
      continue;

    if (!strcmp(attr->name, "attributes-charset") ||
	!strcmp(attr->name, "attributes-natural-language") ||
	!strncmp(attr->name, "date-time-at-", 13) ||
	!strncmp(attr->name, "document-compression", 20) ||
	!strncmp(attr->name, "document-format", 15) ||
	!strcmp(attr->name, "job-detailed-status-messages") ||
	!strcmp(attr->name, "job-document-access-errors") ||
	!strcmp(attr->name, "job-id") ||
	!strcmp(attr->name, "job-impressions-completed") ||
	!strcmp(attr->name, "job-k-octets-completed") ||
	!strcmp(attr->name, "job-media-sheets-completed") ||
        !strcmp(attr->name, "job-originating-host-name") ||
        !strcmp(attr->name, "job-originating-user-name") ||
	!strcmp(attr->name, "job-pages-completed") ||
	!strcmp(attr->name, "job-printer-up-time") ||
	!strcmp(attr->name, "job-printer-uri") ||
	!strcmp(attr->name, "job-sheets") ||
	!strcmp(attr->name, "job-state-message") ||
	!strcmp(attr->name, "job-state-reasons") ||
	!strcmp(attr->name, "job-uri") ||
	!strcmp(attr->name, "number-of-documents") ||
	!strcmp(attr->name, "number-of-intervening-jobs") ||
	!strcmp(attr->name, "output-device-assigned") ||
	!strncmp(attr->name, "time-at-", 8))
    {
     /*
      * Read-only attrs!
      */

      send_ipp_status(con, IPP_ATTRIBUTES_NOT_SETTABLE,
                      _("%s cannot be changed."), attr->name);

      attr2 = ippCopyAttribute(con->response, attr, 0);
      ippSetGroupTag(con->response, &attr2, IPP_TAG_UNSUPPORTED_GROUP);
      continue;
    }

    if (!ippValidateAttribute(attr))
    {
      send_ipp_status(con, IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES, _("Bad '%s' value."), attr->name);
      ippCopyAttribute(con->response, attr, 0);
      return;
    }

    if (!strcmp(attr->name, "job-hold-until"))
    {
      const char *when = ippGetString(attr, 0, NULL);
					/* job-hold-until value */

      if ((ippGetValueTag(attr) != IPP_TAG_KEYWORD && ippGetValueTag(attr) != IPP_TAG_NAME && ippGetValueTag(attr) != IPP_TAG_NAMELANG) || ippGetCount(attr) != 1)
      {
	send_ipp_status(con, IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES, _("Unsupported 'job-hold-until' value."));
	ippCopyAttribute(con->response, attr, 0);
	return;
      }

      cupsdLogJob(job, CUPSD_LOG_DEBUG, "Setting job-hold-until to %s", when);
      cupsdSetJobHoldUntil(job, when, 0);

      if (!strcmp(when, "no-hold"))
      {
	cupsdReleaseJob(job);
	check_jobs = 1;
      }
      else
	cupsdSetJobState(job, IPP_JOB_HELD, CUPSD_JOB_DEFAULT, "Job held by \"%s\".", username);

      event |= CUPSD_EVENT_JOB_CONFIG_CHANGED | CUPSD_EVENT_JOB_STATE;
    }
    else if (!strcmp(attr->name, "job-priority"))
    {
     /*
      * Change the job priority...
      */

      if (attr->value_tag != IPP_TAG_INTEGER)
      {
	send_ipp_status(con, IPP_REQUEST_VALUE, _("Bad job-priority value."));

	attr2 = ippCopyAttribute(con->response, attr, 0);
	ippSetGroupTag(con->response, &attr2, IPP_TAG_UNSUPPORTED_GROUP);
      }
      else if (job->state_value >= IPP_JOB_PROCESSING)
      {
	send_ipp_status(con, IPP_NOT_POSSIBLE,
	                _("Job is completed and cannot be changed."));
	return;
      }
      else if (con->response->request.status.status_code == IPP_OK)
      {
        cupsdLogJob(job, CUPSD_LOG_DEBUG, "Setting job-priority to %d",
	            attr->values[0].integer);
        cupsdSetJobPriority(job, attr->values[0].integer);

	check_jobs = 1;
        event      |= CUPSD_EVENT_JOB_CONFIG_CHANGED |
	              CUPSD_EVENT_PRINTER_QUEUE_ORDER_CHANGED;
      }
    }
    else if (!strcmp(attr->name, "job-state"))
    {
     /*
      * Change the job state...
      */

      if (attr->value_tag != IPP_TAG_ENUM)
      {
	send_ipp_status(con, IPP_REQUEST_VALUE, _("Bad job-state value."));

	attr2 = ippCopyAttribute(con->response, attr, 0);
	ippSetGroupTag(con->response, &attr2, IPP_TAG_UNSUPPORTED_GROUP);
      }
      else
      {
        switch (attr->values[0].integer)
	{
	  case IPP_JOB_PENDING :
	  case IPP_JOB_HELD :
	      if (job->state_value > IPP_JOB_HELD)
	      {
		send_ipp_status(con, IPP_NOT_POSSIBLE,
		                _("Job state cannot be changed."));
		return;
	      }
              else if (con->response->request.status.status_code == IPP_OK)
	      {
		cupsdLogJob(job, CUPSD_LOG_DEBUG, "Setting job-state to %d",
			    attr->values[0].integer);
                cupsdSetJobState(job, (ipp_jstate_t)attr->values[0].integer, CUPSD_JOB_DEFAULT, "Job state changed by \"%s\"", username);
		check_jobs = 1;
	      }
	      break;

	  case IPP_JOB_PROCESSING :
	  case IPP_JOB_STOPPED :
	      if (job->state_value != (ipp_jstate_t)attr->values[0].integer)
	      {
		send_ipp_status(con, IPP_NOT_POSSIBLE,
		                _("Job state cannot be changed."));
		return;
	      }
	      break;

	  case IPP_JOB_CANCELED :
	  case IPP_JOB_ABORTED :
	  case IPP_JOB_COMPLETED :
	      if (job->state_value > IPP_JOB_PROCESSING)
	      {
		send_ipp_status(con, IPP_NOT_POSSIBLE,
		                _("Job state cannot be changed."));
		return;
	      }
              else if (con->response->request.status.status_code == IPP_OK)
	      {
		cupsdLogJob(job, CUPSD_LOG_DEBUG, "Setting job-state to %d",
			    attr->values[0].integer);
                cupsdSetJobState(job, (ipp_jstate_t)attr->values[0].integer,
		                 CUPSD_JOB_DEFAULT,
				 "Job state changed by \"%s\"", username);
                check_jobs = 1;
	      }
	      break;
	}
      }
    }
    else if (con->response->request.status.status_code != IPP_OK)
      continue;
    else if ((attr2 = ippFindAttribute(job->attrs, attr->name,
                                       IPP_TAG_ZERO)) != NULL)
    {
     /*
      * Some other value; first free the old value...
      */

      if (job->attrs->prev)
        job->attrs->prev->next = attr2->next;
      else
        job->attrs->attrs = attr2->next;

      if (job->attrs->last == attr2)
        job->attrs->last = job->attrs->prev;

      ippDeleteAttribute(NULL, attr2);

     /*
      * Then copy the attribute...
      */

      ippCopyAttribute(job->attrs, attr, 0);
    }
    else if (attr->value_tag == IPP_TAG_DELETEATTR)
    {
     /*
      * Delete the attribute...
      */

      if ((attr2 = ippFindAttribute(job->attrs, attr->name,
                                    IPP_TAG_ZERO)) != NULL)
      {
        if (job->attrs->prev)
	  job->attrs->prev->next = attr2->next;
	else
	  job->attrs->attrs = attr2->next;

        if (attr2 == job->attrs->last)
	  job->attrs->last = job->attrs->prev;

        ippDeleteAttribute(NULL, attr2);

        event |= CUPSD_EVENT_JOB_CONFIG_CHANGED;
      }
    }
    else
    {
     /*
      * Add new option by copying it...
      */

      ippCopyAttribute(job->attrs, attr, 0);

      event |= CUPSD_EVENT_JOB_CONFIG_CHANGED;
    }
  }

 /*
  * Save the job...
  */

  job->dirty = 1;
  cupsdMarkDirty(CUPSD_DIRTY_JOBS);

 /*
  * Send events as needed...
  */

  if (event & CUPSD_EVENT_PRINTER_QUEUE_ORDER_CHANGED)
    cupsdAddEvent(CUPSD_EVENT_PRINTER_QUEUE_ORDER_CHANGED,
                  cupsdFindDest(job->dest), job,
                  "Job priority changed by user.");

  if (event & CUPSD_EVENT_JOB_STATE)
    cupsdAddEvent(CUPSD_EVENT_JOB_STATE, cupsdFindDest(job->dest), job,
                  job->state_value == IPP_JOB_HELD ?
		      "Job held by user." : "Job restarted by user.");

  if (event & CUPSD_EVENT_JOB_CONFIG_CHANGED)
    cupsdAddEvent(CUPSD_EVENT_JOB_CONFIG_CHANGED, cupsdFindDest(job->dest), job,
                  "Job options changed by user.");

 /*
  * Start jobs if possible...
  */

  if (check_jobs)
    cupsdCheckJobs();
}


/*
 * 'set_printer_attrs()' - Set printer attributes.
 */

static void
set_printer_attrs(cupsd_client_t  *con,	/* I - Client connection */
                  ipp_attribute_t *uri)	/* I - Printer */
{
  http_status_t		status;		/* Policy status */
  cups_ptype_t		dtype;		/* Destination type (printer/class) */
  cupsd_printer_t	*printer;	/* Printer/class */
  ipp_attribute_t	*attr;		/* Printer attribute */
  int			changed = 0;	/* Was anything changed? */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "set_printer_attrs(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * Is the destination valid?
  */

  if (!cupsdValidateDest(uri->values[0].string.text, &dtype, &printer))
  {
   /*
    * Bad URI...
    */

    send_ipp_status(con, IPP_NOT_FOUND,
                    _("The printer or class does not exist."));
    return;
  }

 /*
  * Check policy...
  */

  if ((status = cupsdCheckPolicy(printer->op_policy_ptr, con, NULL)) != HTTP_OK)
  {
    send_http_error(con, status, printer);
    return;
  }

 /*
  * Return a list of attributes that can be set via Set-Printer-Attributes.
  */

  if ((attr = ippFindAttribute(con->request, "printer-location",
                               IPP_TAG_TEXT)) != NULL)
  {
    cupsdSetString(&printer->location, attr->values[0].string.text);
    changed = 1;
  }

  if ((attr = ippFindAttribute(con->request, "printer-geo-location", IPP_TAG_URI)) != NULL && !strncmp(attr->values[0].string.text, "geo:", 4))
  {
    cupsdSetString(&printer->geo_location, attr->values[0].string.text);
    changed = 1;
  }

  if ((attr = ippFindAttribute(con->request, "printer-organization", IPP_TAG_TEXT)) != NULL)
  {
    cupsdSetString(&printer->organization, attr->values[0].string.text);
    changed = 1;
  }

  if ((attr = ippFindAttribute(con->request, "printer-organizational-unit", IPP_TAG_TEXT)) != NULL)
  {
    cupsdSetString(&printer->organizational_unit, attr->values[0].string.text);
    changed = 1;
  }

  if ((attr = ippFindAttribute(con->request, "printer-info",
                               IPP_TAG_TEXT)) != NULL)
  {
    cupsdSetString(&printer->info, attr->values[0].string.text);
    changed = 1;
  }

 /*
  * Update the printer attributes and return...
  */

  if (changed)
  {
    printer->config_time = time(NULL);

    cupsdSetPrinterAttrs(printer);
    cupsdMarkDirty(CUPSD_DIRTY_PRINTERS);

    cupsdAddEvent(CUPSD_EVENT_PRINTER_CONFIG, printer, NULL,
                  "Printer \"%s\" description or location changed by \"%s\".",
		  printer->name, get_username(con));

    cupsdLogMessage(CUPSD_LOG_INFO,
                    "Printer \"%s\" description or location changed by \"%s\".",
                    printer->name, get_username(con));
  }

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'set_printer_defaults()' - Set printer default options from a request.
 */

static int				/* O - 1 on success, 0 on failure */
set_printer_defaults(
    cupsd_client_t  *con,		/* I - Client connection */
    cupsd_printer_t *printer)		/* I - Printer */
{
  int			i;		/* Looping var */
  ipp_attribute_t 	*attr;		/* Current attribute */
  size_t		namelen;	/* Length of attribute name */
  char			name[256],	/* New attribute name */
			value[256];	/* String version of integer attrs */


  for (attr = con->request->attrs; attr; attr = attr->next)
  {
   /*
    * Skip non-printer attributes...
    */

    if (attr->group_tag != IPP_TAG_PRINTER || !attr->name)
      continue;

    cupsdLogMessage(CUPSD_LOG_DEBUG2, "set_printer_defaults: %s", attr->name);

    if (!strcmp(attr->name, "job-sheets-default"))
    {
     /*
      * Only allow keywords and names...
      */

      if (printer->temporary)
        goto temporary_printer;

      if (attr->value_tag != IPP_TAG_NAME && attr->value_tag != IPP_TAG_KEYWORD)
        continue;

     /*
      * Only allow job-sheets-default to be set when running without a
      * system high classification level...
      */

      if (Classification)
        continue;

      cupsdSetString(&printer->job_sheets[0], attr->values[0].string.text);

      if (attr->num_values > 1)
	cupsdSetString(&printer->job_sheets[1], attr->values[1].string.text);
      else
	cupsdSetString(&printer->job_sheets[1], "none");
    }
    else if (!strcmp(attr->name, "requesting-user-name-allowed"))
    {
      if (printer->temporary)
        goto temporary_printer;

      cupsdFreeStrings(&(printer->users));

      printer->deny_users = 0;

      if (attr->value_tag == IPP_TAG_NAME &&
          (attr->num_values > 1 ||
	   strcmp(attr->values[0].string.text, "all")))
      {
	for (i = 0; i < attr->num_values; i ++)
	  cupsdAddString(&(printer->users), attr->values[i].string.text);
      }
    }
    else if (!strcmp(attr->name, "requesting-user-name-denied"))
    {
      if (printer->temporary)
        goto temporary_printer;

      cupsdFreeStrings(&(printer->users));

      printer->deny_users = 1;

      if (attr->value_tag == IPP_TAG_NAME &&
          (attr->num_values > 1 ||
	   strcmp(attr->values[0].string.text, "none")))
      {
	for (i = 0; i < attr->num_values; i ++)
	  cupsdAddString(&(printer->users), attr->values[i].string.text);
      }
    }
    else if (!strcmp(attr->name, "job-quota-period"))
    {
      if (printer->temporary)
        goto temporary_printer;

      if (attr->value_tag != IPP_TAG_INTEGER)
        continue;

      cupsdLogMessage(CUPSD_LOG_DEBUG, "Setting job-quota-period to %d...",
        	      attr->values[0].integer);
      cupsdFreeQuotas(printer);

      printer->quota_period = attr->values[0].integer;
    }
    else if (!strcmp(attr->name, "job-k-limit"))
    {
      if (printer->temporary)
        goto temporary_printer;

      if (attr->value_tag != IPP_TAG_INTEGER)
        continue;

      cupsdLogMessage(CUPSD_LOG_DEBUG, "Setting job-k-limit to %d...",
        	      attr->values[0].integer);
      cupsdFreeQuotas(printer);

      printer->k_limit = attr->values[0].integer;
    }
    else if (!strcmp(attr->name, "job-page-limit"))
    {
      if (printer->temporary)
        goto temporary_printer;

      if (attr->value_tag != IPP_TAG_INTEGER)
        continue;

      cupsdLogMessage(CUPSD_LOG_DEBUG, "Setting job-page-limit to %d...",
        	      attr->values[0].integer);
      cupsdFreeQuotas(printer);

      printer->page_limit = attr->values[0].integer;
    }
    else if (!strcmp(attr->name, "printer-op-policy"))
    {
      cupsd_policy_t *p;		/* Policy */


      if (printer->temporary)
        goto temporary_printer;

      if (attr->value_tag != IPP_TAG_NAME)
        continue;

      if ((p = cupsdFindPolicy(attr->values[0].string.text)) != NULL)
      {
	cupsdLogMessage(CUPSD_LOG_DEBUG,
                	"Setting printer-op-policy to \"%s\"...",
                	attr->values[0].string.text);
	cupsdSetString(&printer->op_policy, attr->values[0].string.text);
	printer->op_policy_ptr = p;
      }
      else
      {
	send_ipp_status(con, IPP_NOT_POSSIBLE,
                	_("Unknown printer-op-policy \"%s\"."),
                	attr->values[0].string.text);
	return (0);
      }
    }
    else if (!strcmp(attr->name, "printer-error-policy"))
    {
      if (printer->temporary)
        goto temporary_printer;

      if (attr->value_tag != IPP_TAG_NAME && attr->value_tag != IPP_TAG_KEYWORD)
        continue;

      if (strcmp(attr->values[0].string.text, "retry-current-job") &&
          ((printer->type & CUPS_PRINTER_CLASS) ||
	   (strcmp(attr->values[0].string.text, "abort-job") &&
	    strcmp(attr->values[0].string.text, "retry-job") &&
	    strcmp(attr->values[0].string.text, "stop-printer"))))
      {
	send_ipp_status(con, IPP_NOT_POSSIBLE,
                	_("Unknown printer-error-policy \"%s\"."),
                	attr->values[0].string.text);
	return (0);
      }

      cupsdLogMessage(CUPSD_LOG_DEBUG,
                      "Setting printer-error-policy to \"%s\"...",
                      attr->values[0].string.text);
      cupsdSetString(&printer->error_policy, attr->values[0].string.text);
    }

   /*
    * Skip any other non-default attributes...
    */

    namelen = strlen(attr->name);
    if (namelen < 9 || strcmp(attr->name + namelen - 8, "-default") ||
        namelen > (sizeof(name) - 1) || attr->num_values != 1)
      continue;

    if (printer->temporary)
      goto temporary_printer;

   /*
    * OK, anything else must be a user-defined default...
    */

    strlcpy(name, attr->name, sizeof(name));
    name[namelen - 8] = '\0';		/* Strip "-default" */

    switch (attr->value_tag)
    {
      case IPP_TAG_DELETEATTR :
          printer->num_options = cupsRemoveOption(name,
						  printer->num_options,
						  &(printer->options));
          cupsdLogMessage(CUPSD_LOG_DEBUG,
	                  "Deleting %s", attr->name);
          break;

      case IPP_TAG_NAME :
      case IPP_TAG_TEXT :
      case IPP_TAG_KEYWORD :
      case IPP_TAG_URI :
          printer->num_options = cupsAddOption(name,
	                                       attr->values[0].string.text,
					       printer->num_options,
					       &(printer->options));
          cupsdLogMessage(CUPSD_LOG_DEBUG,
	                  "Setting %s to \"%s\"...", attr->name,
			  attr->values[0].string.text);
          break;

      case IPP_TAG_BOOLEAN :
          printer->num_options = cupsAddOption(name,
	                                       attr->values[0].boolean ?
					           "true" : "false",
					       printer->num_options,
					       &(printer->options));
          cupsdLogMessage(CUPSD_LOG_DEBUG,
	                  "Setting %s to %s...", attr->name,
			  attr->values[0].boolean ? "true" : "false");
          break;

      case IPP_TAG_INTEGER :
      case IPP_TAG_ENUM :
          printer->num_options = cupsAddIntegerOption(name, attr->values[0].integer, printer->num_options, &(printer->options));
          cupsdLogMessage(CUPSD_LOG_DEBUG,
	                  "Setting %s to %s...", attr->name, value);
          break;

      case IPP_TAG_RANGE :
          snprintf(value, sizeof(value), "%d-%d", attr->values[0].range.lower, attr->values[0].range.upper);
          printer->num_options = cupsAddOption(name, value,
					       printer->num_options,
					       &(printer->options));
          cupsdLogMessage(CUPSD_LOG_DEBUG,
	                  "Setting %s to %s...", attr->name, value);
          break;

      case IPP_TAG_RESOLUTION :
          snprintf(value, sizeof(value), "%dx%d%s", attr->values[0].resolution.xres, attr->values[0].resolution.yres, attr->values[0].resolution.units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
          printer->num_options = cupsAddOption(name, value,
					       printer->num_options,
					       &(printer->options));
          cupsdLogMessage(CUPSD_LOG_DEBUG,
	                  "Setting %s to %s...", attr->name, value);
          break;

      default :
          /* Do nothing for other values */
	  break;
    }
  }

  return (1);

 /*
  * If we get here this is a temporary printer and you can't set defaults for
  * this kind of queue...
  */

  temporary_printer:

  send_ipp_status(con, IPP_STATUS_ERROR_NOT_POSSIBLE, _("Unable to save value for \"%s\" with a temporary printer."), attr->name);

  return (0);
}


/*
 * 'start_printer()' - Start a printer.
 */

static void
start_printer(cupsd_client_t  *con,	/* I - Client connection */
              ipp_attribute_t *uri)	/* I - Printer URI */
{
  int			i;		/* Temporary variable */
  http_status_t		status;		/* Policy status */
  cups_ptype_t		dtype;		/* Destination type (printer/class) */
  cupsd_printer_t	*printer;	/* Printer data */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "start_printer(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * Is the destination valid?
  */

  if (!cupsdValidateDest(uri->values[0].string.text, &dtype, &printer))
  {
   /*
    * Bad URI...
    */

    send_ipp_status(con, IPP_NOT_FOUND,
                    _("The printer or class does not exist."));
    return;
  }

 /*
  * Check policy...
  */

  if ((status = cupsdCheckPolicy(printer->op_policy_ptr, con, NULL)) != HTTP_OK)
  {
    send_http_error(con, status, printer);
    return;
  }

 /*
  * Start the printer...
  */

  printer->state_message[0] = '\0';

  cupsdStartPrinter(printer, 1);

  if (dtype & CUPS_PRINTER_CLASS)
    cupsdLogMessage(CUPSD_LOG_INFO, "Class \"%s\" started by \"%s\".",
                    printer->name, get_username(con));
  else
    cupsdLogMessage(CUPSD_LOG_INFO, "Printer \"%s\" started by \"%s\".",
                    printer->name, get_username(con));

  cupsdCheckJobs();

 /*
  * Check quotas...
  */

  if ((i = check_quotas(con, printer)) < 0)
  {
    send_ipp_status(con, IPP_NOT_POSSIBLE, _("Quota limit reached."));
    return;
  }
  else if (i == 0)
  {
    send_ipp_status(con, IPP_NOT_AUTHORIZED, _("Not allowed to print."));
    return;
  }

 /*
  * Everything was ok, so return OK status...
  */

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'stop_printer()' - Stop a printer.
 */

static void
stop_printer(cupsd_client_t  *con,	/* I - Client connection */
             ipp_attribute_t *uri)	/* I - Printer URI */
{
  http_status_t		status;		/* Policy status */
  cups_ptype_t		dtype;		/* Destination type (printer/class) */
  cupsd_printer_t	*printer;	/* Printer data */
  ipp_attribute_t	*attr;		/* printer-state-message attribute */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "stop_printer(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * Is the destination valid?
  */

  if (!cupsdValidateDest(uri->values[0].string.text, &dtype, &printer))
  {
   /*
    * Bad URI...
    */

    send_ipp_status(con, IPP_NOT_FOUND,
                    _("The printer or class does not exist."));
    return;
  }

 /*
  * Check policy...
  */

  if ((status = cupsdCheckPolicy(printer->op_policy_ptr, con, NULL)) != HTTP_OK)
  {
    send_http_error(con, status, printer);
    return;
  }

 /*
  * Stop the printer...
  */

  if ((attr = ippFindAttribute(con->request, "printer-state-message",
                               IPP_TAG_TEXT)) == NULL)
    strlcpy(printer->state_message, "Paused", sizeof(printer->state_message));
  else
  {
    strlcpy(printer->state_message, attr->values[0].string.text,
            sizeof(printer->state_message));
  }

  cupsdStopPrinter(printer, 1);

  if (dtype & CUPS_PRINTER_CLASS)
    cupsdLogMessage(CUPSD_LOG_INFO, "Class \"%s\" stopped by \"%s\".",
                    printer->name, get_username(con));
  else
    cupsdLogMessage(CUPSD_LOG_INFO, "Printer \"%s\" stopped by \"%s\".",
                    printer->name, get_username(con));

 /*
  * Everything was ok, so return OK status...
  */

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'url_encode_attr()' - URL-encode a string attribute.
 */

static void
url_encode_attr(ipp_attribute_t *attr,	/* I - Attribute */
                char            *buffer,/* I - String buffer */
		size_t          bufsize)/* I - Size of buffer */
{
  int	i;				/* Looping var */
  char	*bufptr,			/* Pointer into buffer */
	*bufend;			/* End of buffer */


  strlcpy(buffer, attr->name, bufsize);
  bufptr = buffer + strlen(buffer);
  bufend = buffer + bufsize - 1;

  for (i = 0; i < attr->num_values; i ++)
  {
    if (bufptr >= bufend)
      break;

    if (i)
      *bufptr++ = ',';
    else
      *bufptr++ = '=';

    if (bufptr >= bufend)
      break;

    *bufptr++ = '\'';

    bufptr = url_encode_string(attr->values[i].string.text, bufptr, (size_t)(bufend - bufptr + 1));

    if (bufptr >= bufend)
      break;

    *bufptr++ = '\'';
  }

  *bufptr = '\0';
}


/*
 * 'url_encode_string()' - URL-encode a string.
 */

static char *				/* O - End of string */
url_encode_string(const char *s,	/* I - String */
                  char       *buffer,	/* I - String buffer */
		  size_t     bufsize)	/* I - Size of buffer */
{
  char	*bufptr,			/* Pointer into buffer */
	*bufend;			/* End of buffer */
  static const char *hex = "0123456789ABCDEF";
					/* Hex digits */


  bufptr = buffer;
  bufend = buffer + bufsize - 1;

  while (*s && bufptr < bufend)
  {
    if (*s == ' ' || *s == '%' || *s == '+')
    {
      if (bufptr >= (bufend - 2))
	break;

      *bufptr++ = '%';
      *bufptr++ = hex[(*s >> 4) & 15];
      *bufptr++ = hex[*s & 15];

      s ++;
    }
    else if (*s == '\'' || *s == '\\')
    {
      if (bufptr >= (bufend - 1))
	break;

      *bufptr++ = '\\';
      *bufptr++ = *s++;
    }
    else
      *bufptr++ = *s++;
  }

  *bufptr = '\0';

  return (bufptr);
}


/*
 * 'user_allowed()' - See if a user is allowed to print to a queue.
 */

static int				/* O - 0 if not allowed, 1 if allowed */
user_allowed(cupsd_printer_t *p,	/* I - Printer or class */
             const char      *username)	/* I - Username */
{
  struct passwd	*pw;			/* User password data */
  char		baseuser[256],		/* Base username */
		*baseptr,		/* Pointer to "@" in base username */
		*name;			/* Current user name */


  if (cupsArrayCount(p->users) == 0)
    return (1);

  if (!strcmp(username, "root"))
    return (1);

  if (strchr(username, '@'))
  {
   /*
    * Strip @REALM for username check...
    */

    strlcpy(baseuser, username, sizeof(baseuser));

    if ((baseptr = strchr(baseuser, '@')) != NULL)
      *baseptr = '\0';

    username = baseuser;
  }

  pw = getpwnam(username);
  endpwent();

  for (name = (char *)cupsArrayFirst(p->users);
       name;
       name = (char *)cupsArrayNext(p->users))
  {
    if (name[0] == '@')
    {
     /*
      * Check group membership...
      */

      if (cupsdCheckGroup(username, pw, name + 1))
        break;
    }
    else if (name[0] == '#')
    {
     /*
      * Check UUID...
      */

      if (cupsdCheckGroup(username, pw, name))
        break;
    }
    else if (!_cups_strcasecmp(username, name))
      break;
  }

  return ((name != NULL) != p->deny_users);
}


/*
 * 'validate_job()' - Validate printer options and destination.
 */

static void
validate_job(cupsd_client_t  *con,	/* I - Client connection */
	     ipp_attribute_t *uri)	/* I - Printer URI */
{
  http_status_t		status;		/* Policy status */
  ipp_attribute_t	*attr;		/* Current attribute */
#ifdef HAVE_TLS
  ipp_attribute_t	*auth_info;	/* auth-info attribute */
#endif /* HAVE_TLS */
  ipp_attribute_t	*format,	/* Document-format attribute */
			*name;		/* Job-name attribute */
  cups_ptype_t		dtype;		/* Destination type (printer/class) */
  char			super[MIME_MAX_SUPER],
					/* Supertype of file */
			type[MIME_MAX_TYPE];
					/* Subtype of file */
  cupsd_printer_t	*printer;	/* Printer */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "validate_job(%p[%d], %s)", (void *)con,
                  con->number, uri->values[0].string.text);

 /*
  * OK, see if the client is sending the document compressed - CUPS
  * doesn't support compression yet...
  */

  if ((attr = ippFindAttribute(con->request, "compression",
                               IPP_TAG_KEYWORD)) != NULL)
  {
    if (strcmp(attr->values[0].string.text, "none")
#ifdef HAVE_LIBZ
        && strcmp(attr->values[0].string.text, "gzip")
#endif /* HAVE_LIBZ */
      )
    {
      send_ipp_status(con, IPP_ATTRIBUTES,
                      _("Unsupported 'compression' value \"%s\"."),
        	      attr->values[0].string.text);
      ippAddString(con->response, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_KEYWORD,
	           "compression", NULL, attr->values[0].string.text);
      return;
    }
  }

 /*
  * Is it a format we support?
  */

  if ((format = ippFindAttribute(con->request, "document-format",
                                 IPP_TAG_MIMETYPE)) != NULL)
  {
    if (sscanf(format->values[0].string.text, "%15[^/]/%255[^;]",
               super, type) != 2)
    {
      send_ipp_status(con, IPP_BAD_REQUEST,
                      _("Bad 'document-format' value \"%s\"."),
		      format->values[0].string.text);
      return;
    }

    _cupsRWLockRead(&MimeDatabase->lock);

    if ((strcmp(super, "application") || strcmp(type, "octet-stream")) &&
	!mimeType(MimeDatabase, super, type))
    {
      cupsdLogMessage(CUPSD_LOG_INFO,
                      "Hint: Do you have the raw file printing rules enabled?");
      send_ipp_status(con, IPP_DOCUMENT_FORMAT,
                      _("Unsupported 'document-format' value \"%s\"."),
		      format->values[0].string.text);
      ippAddString(con->response, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_MIMETYPE,
                   "document-format", NULL, format->values[0].string.text);

      _cupsRWUnlock(&MimeDatabase->lock);

      return;
    }

    _cupsRWUnlock(&MimeDatabase->lock);
  }

 /*
  * Is the job-hold-until value valid?
  */

  if ((attr = ippFindAttribute(con->request, "job-hold-until", IPP_TAG_ZERO)) != NULL && ((ippGetValueTag(attr) != IPP_TAG_KEYWORD && ippGetValueTag(attr) != IPP_TAG_NAME && ippGetValueTag(attr) != IPP_TAG_NAMELANG) || ippGetCount(attr) != 1 || !ippValidateAttribute(attr)))
  {
    send_ipp_status(con, IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES, _("Unsupported 'job-hold-until' value."));
    ippCopyAttribute(con->response, attr, 0);
    return;
  }

 /*
  * Is the job-name valid?
  */

  if ((name = ippFindAttribute(con->request, "job-name", IPP_TAG_ZERO)) != NULL)
  {
    if ((name->value_tag != IPP_TAG_NAME && name->value_tag != IPP_TAG_NAMELANG) ||
        name->num_values != 1 || !ippValidateAttribute(name))
    {
      if (StrictConformance)
      {
	send_ipp_status(con, IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES, _("Unsupported 'job-name' value."));
	ippCopyAttribute(con->response, name, 0);
	return;
      }
      else
      {
        cupsdLogMessage(CUPSD_LOG_WARN, "Unsupported 'job-name' value, deleting from request.");
        ippDeleteAttribute(con->request, name);
      }
    }
  }

 /*
  * Is the destination valid?
  */

  if (!cupsdValidateDest(uri->values[0].string.text, &dtype, &printer))
  {
   /*
    * Bad URI...
    */

    send_ipp_status(con, IPP_NOT_FOUND,
                    _("The printer or class does not exist."));
    return;
  }

 /*
  * Check policy...
  */

#ifdef HAVE_TLS
  auth_info = ippFindAttribute(con->request, "auth-info", IPP_TAG_TEXT);
#endif /* HAVE_TLS */

  if ((status = cupsdCheckPolicy(printer->op_policy_ptr, con, NULL)) != HTTP_OK)
  {
    send_http_error(con, status, printer);
    return;
  }
  else if (printer->num_auth_info_required == 1 &&
           !strcmp(printer->auth_info_required[0], "negotiate") &&
           !con->username[0])
  {
    send_http_error(con, HTTP_UNAUTHORIZED, printer);
    return;
  }
#ifdef HAVE_TLS
  else if (auth_info && !con->http->tls &&
           !httpAddrLocalhost(con->http->hostaddr))
  {
   /*
    * Require encryption of auth-info over non-local connections...
    */

    send_http_error(con, HTTP_UPGRADE_REQUIRED, printer);
    return;
  }
#endif /* HAVE_TLS */

 /*
  * Everything was ok, so return OK status...
  */

  con->response->request.status.status_code = IPP_OK;
}


/*
 * 'validate_name()' - Make sure the printer name only contains valid chars.
 */

static int			/* O - 0 if name is no good, 1 if good */
validate_name(const char *name)	/* I - Name to check */
{
  const char	*ptr;		/* Pointer into name */


 /*
  * Scan the whole name...
  */

  for (ptr = name; *ptr; ptr ++)
    if ((*ptr > 0 && *ptr <= ' ') || *ptr == 127 || *ptr == '/' || *ptr == '#')
      return (0);

 /*
  * All the characters are good; validate the length, too...
  */

  return ((ptr - name) < 128);
}


/*
 * 'validate_user()' - Validate the user for the request.
 */

static int				/* O - 1 if permitted, 0 otherwise */
validate_user(cupsd_job_t    *job,	/* I - Job */
              cupsd_client_t *con,	/* I - Client connection */
              const char     *owner,	/* I - Owner of job/resource */
              char           *username,	/* O - Authenticated username */
	      size_t         userlen)	/* I - Length of username */
{
  cupsd_printer_t	*printer;	/* Printer for job */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "validate_user(job=%d, con=%d, owner=\"%s\", username=%p, userlen=" CUPS_LLFMT ")", job->id, con ? con->number : 0, owner ? owner : "(null)", (void *)username, CUPS_LLCAST userlen);

 /*
  * Validate input...
  */

  if (!con || !owner || !username || userlen <= 0)
    return (0);

 /*
  * Get the best authenticated username that is available.
  */

  strlcpy(username, get_username(con), userlen);

 /*
  * Check the username against the owner...
  */

  printer = cupsdFindDest(job->dest);

  return (cupsdCheckPolicy(printer ? printer->op_policy_ptr : DefaultPolicyPtr,
                           con, owner) == HTTP_OK);
}
