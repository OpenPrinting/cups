/*
 * TLS support code for CUPS using OpenSSL/LibreSSL.
 *
 * Copyright © 2020-2022 by OpenPrinting
 * Copyright © 2007-2019 by Apple Inc.
 * Copyright © 1997-2007 by Easy Software Products, all rights reserved.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/**** This file is included from tls.c ****/

/*
 * Include necessary headers...
 */

#include <sys/stat.h>


/*
 * Local functions...
 */

static long		http_bio_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int		http_bio_free(BIO *data);
static int		http_bio_new(BIO *h);
static int		http_bio_puts(BIO *h, const char *str);
static int		http_bio_read(BIO *h, char *buf, int size);
static int		http_bio_write(BIO *h, const char *buf, int num);

static X509		*http_create_credential(http_credential_t *credential);
static const char	*http_default_path(char *buffer, size_t bufsize);
//static void		http_load_crl(void);
static const char	*http_make_path(char *buffer, size_t bufsize, const char *dirname, const char *filename, const char *ext);


/*
 * Local globals...
 */

static BIO_METHOD	http_bio_methods =
			{
			  BIO_TYPE_SOCKET,
			  "http",
			  http_bio_write,
			  http_bio_read,
			  http_bio_puts,
			  NULL, /* http_bio_gets, */
			  http_bio_ctrl,
			  http_bio_new,
			  http_bio_free,
			  NULL,
			};
static int		tls_auto_create = 0;
					/* Auto-create self-signed certs? */
static char		*tls_common_name = NULL;
					/* Default common name */
//static X509_CRL		*tls_crl = NULL;/* Certificate revocation list */
static char		*tls_keypath = NULL;
					/* Server cert keychain path */
static _cups_mutex_t	tls_mutex = _CUPS_MUTEX_INITIALIZER;
					/* Mutex for keychain/certs */
static int		tls_options = -1,/* Options for TLS connections */
			tls_min_version = _HTTP_TLS_1_0,
			tls_max_version = _HTTP_TLS_MAX;


/*
 * 'cupsMakeServerCredentials()' - Make a self-signed certificate and private key pair.
 *
 * @since CUPS 2.0/OS 10.10@
 */

int					// O - 1 on success, 0 on failure
cupsMakeServerCredentials(
    const char *path,			// I - Path to keychain/directory
    const char *common_name,		// I - Common name
    int        num_alt_names,		// I - Number of subject alternate names
    const char **alt_names,		// I - Subject Alternate Names
    time_t     expiration_date)		// I - Expiration date
{
#if 0
  openssl_x509_crt_t	crt;		/* Self-signed certificate */
  openssl_x509_privkey_t	key;		/* Encryption private key */
  char			temp[1024],	/* Temporary directory name */
 			crtfile[1024],	/* Certificate filename */
			keyfile[1024];	/* Private key filename */
  cups_lang_t		*language;	/* Default language info */
  cups_file_t		*fp;		/* Key/cert file */
  unsigned char		buffer[8192];	/* Buffer for x509 data */
  size_t		bytes;		/* Number of bytes of data */
  unsigned char		serial[4];	/* Serial number buffer */
  time_t		curtime;	/* Current time */
  int			result;		/* Result of GNU TLS calls */


  DEBUG_printf(("cupsMakeServerCredentials(path=\"%s\", common_name=\"%s\", num_alt_names=%d, alt_names=%p, expiration_date=%d)", path, common_name, num_alt_names, alt_names, (int)expiration_date));

 /*
  * Filenames...
  */

  if (!path)
    path = http_default_path(temp, sizeof(temp));

  if (!path || !common_name)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (0);
  }

  http_make_path(crtfile, sizeof(crtfile), path, common_name, "crt");
  http_make_path(keyfile, sizeof(keyfile), path, common_name, "key");

 /*
  * Create the encryption key...
  */

  DEBUG_puts("1cupsMakeServerCredentials: Creating key pair.");

  openssl_x509_privkey_init(&key);
  openssl_x509_privkey_generate(key, GNUTLS_PK_RSA, 2048, 0);

  DEBUG_puts("1cupsMakeServerCredentials: Key pair created.");

 /*
  * Save it...
  */

  bytes = sizeof(buffer);

  if ((result = openssl_x509_privkey_export(key, GNUTLS_X509_FMT_PEM, buffer, &bytes)) < 0)
  {
    DEBUG_printf(("1cupsMakeServerCredentials: Unable to export private key: %s", openssl_strerror(result)));
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, openssl_strerror(result), 0);
    openssl_x509_privkey_deinit(key);
    return (0);
  }
  else if ((fp = cupsFileOpen(keyfile, "w")) != NULL)
  {
    DEBUG_printf(("1cupsMakeServerCredentials: Writing private key to \"%s\".", keyfile));
    cupsFileWrite(fp, (char *)buffer, bytes);
    cupsFileClose(fp);
  }
  else
  {
    DEBUG_printf(("1cupsMakeServerCredentials: Unable to create private key file \"%s\": %s", keyfile, strerror(errno)));
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    openssl_x509_privkey_deinit(key);
    return (0);
  }

 /*
  * Create the self-signed certificate...
  */

  DEBUG_puts("1cupsMakeServerCredentials: Generating self-signed X.509 certificate.");

  language  = cupsLangDefault();
  curtime   = time(NULL);
  serial[0] = curtime >> 24;
  serial[1] = curtime >> 16;
  serial[2] = curtime >> 8;
  serial[3] = curtime;

  openssl_x509_crt_init(&crt);
  if (strlen(language->language) == 5)
    openssl_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_X520_COUNTRY_NAME, 0,
                                  language->language + 3, 2);
  else
    openssl_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_X520_COUNTRY_NAME, 0,
                                  "US", 2);
  openssl_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_X520_COMMON_NAME, 0,
                                common_name, strlen(common_name));
  openssl_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_X520_ORGANIZATION_NAME, 0,
                                common_name, strlen(common_name));
  openssl_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_X520_ORGANIZATIONAL_UNIT_NAME,
                                0, "Unknown", 7);
  openssl_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_X520_STATE_OR_PROVINCE_NAME, 0,
                                "Unknown", 7);
  openssl_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_X520_LOCALITY_NAME, 0,
                                "Unknown", 7);
/*  openssl_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_PKCS9_EMAIL, 0,
                                ServerAdmin, strlen(ServerAdmin));*/
  openssl_x509_crt_set_key(crt, key);
  openssl_x509_crt_set_serial(crt, serial, sizeof(serial));
  openssl_x509_crt_set_activation_time(crt, curtime);
  openssl_x509_crt_set_expiration_time(crt, curtime + 10 * 365 * 86400);
  openssl_x509_crt_set_ca_status(crt, 0);
  openssl_x509_crt_set_subject_alt_name(crt, GNUTLS_SAN_DNSNAME, common_name, (unsigned)strlen(common_name), GNUTLS_FSAN_SET);
  if (!strchr(common_name, '.'))
  {
   /*
    * Add common_name.local to the list, too...
    */

    char localname[256];                /* hostname.local */

    snprintf(localname, sizeof(localname), "%s.local", common_name);
    openssl_x509_crt_set_subject_alt_name(crt, GNUTLS_SAN_DNSNAME, localname, (unsigned)strlen(localname), GNUTLS_FSAN_APPEND);
  }
  openssl_x509_crt_set_subject_alt_name(crt, GNUTLS_SAN_DNSNAME, "localhost", 9, GNUTLS_FSAN_APPEND);
  if (num_alt_names > 0)
  {
    int i;                              /* Looping var */

    for (i = 0; i < num_alt_names; i ++)
    {
      if (strcmp(alt_names[i], "localhost"))
      {
        openssl_x509_crt_set_subject_alt_name(crt, GNUTLS_SAN_DNSNAME, alt_names[i], (unsigned)strlen(alt_names[i]), GNUTLS_FSAN_APPEND);
      }
    }
  }
  openssl_x509_crt_set_key_purpose_oid(crt, GNUTLS_KP_TLS_WWW_SERVER, 0);
  openssl_x509_crt_set_key_usage(crt, GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT);
  openssl_x509_crt_set_version(crt, 3);

  bytes = sizeof(buffer);
  if (openssl_x509_crt_get_key_id(crt, 0, buffer, &bytes) >= 0)
    openssl_x509_crt_set_subject_key_id(crt, buffer, bytes);

  openssl_x509_crt_sign(crt, crt, key);

 /*
  * Save it...
  */

  bytes = sizeof(buffer);
  if ((result = openssl_x509_crt_export(crt, GNUTLS_X509_FMT_PEM, buffer, &bytes)) < 0)
  {
    DEBUG_printf(("1cupsMakeServerCredentials: Unable to export public key and X.509 certificate: %s", openssl_strerror(result)));
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, openssl_strerror(result), 0);
    openssl_x509_crt_deinit(crt);
    openssl_x509_privkey_deinit(key);
    return (0);
  }
  else if ((fp = cupsFileOpen(crtfile, "w")) != NULL)
  {
    DEBUG_printf(("1cupsMakeServerCredentials: Writing public key and X.509 certificate to \"%s\".", crtfile));
    cupsFileWrite(fp, (char *)buffer, bytes);
    cupsFileClose(fp);
  }
  else
  {
    DEBUG_printf(("1cupsMakeServerCredentials: Unable to create public key and X.509 certificate file \"%s\": %s", crtfile, strerror(errno)));
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    openssl_x509_crt_deinit(crt);
    openssl_x509_privkey_deinit(key);
    return (0);
  }

 /*
  * Cleanup...
  */

  openssl_x509_crt_deinit(crt);
  openssl_x509_privkey_deinit(key);

  DEBUG_puts("1cupsMakeServerCredentials: Successfully created credentials.");

  return (1);
#else
  return (0);
#endif // 0
}


/*
 * 'cupsSetServerCredentials()' - Set the default server credentials.
 *
 * Note: The server credentials are used by all threads in the running process.
 * This function is threadsafe.
 *
 * @since CUPS 2.0/OS 10.10@
 */

int					// O - 1 on success, 0 on failure
cupsSetServerCredentials(
    const char *path,			// I - Path to keychain/directory
    const char *common_name,		// I - Default common name for server
    int        auto_create)		// I - 1 = automatically create self-signed certificates
{
  char	temp[1024];			// Default path buffer


  DEBUG_printf(("cupsSetServerCredentials(path=\"%s\", common_name=\"%s\", auto_create=%d)", path, common_name, auto_create));

 /*
  * Use defaults as needed...
  */

  if (!path)
    path = http_default_path(temp, sizeof(temp));

 /*
  * Range check input...
  */

  if (!path || !common_name)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (0);
  }

  _cupsMutexLock(&tls_mutex);

 /*
  * Free old values...
  */

  if (tls_keypath)
    _cupsStrFree(tls_keypath);

  if (tls_common_name)
    _cupsStrFree(tls_common_name);

 /*
  * Save the new values...
  */

  tls_keypath     = _cupsStrAlloc(path);
  tls_auto_create = auto_create;
  tls_common_name = _cupsStrAlloc(common_name);

  _cupsMutexUnlock(&tls_mutex);

  return (1);
}


/*
 * 'httpCopyCredentials()' - Copy the credentials associated with the peer in
 *                           an encrypted connection.
 *
 * @since CUPS 1.5/macOS 10.7@
 */

int					// O - Status of call (0 = success)
httpCopyCredentials(
    http_t	 *http,			// I - Connection to server
    cups_array_t **credentials)		// O - Array of credentials
{
#if 0
  unsigned		count;		// Number of certificates
  const openssl_datum_t *certs;		// Certificates


  DEBUG_printf(("httpCopyCredentials(http=%p, credentials=%p)", http, credentials));

  if (credentials)
    *credentials = NULL;

  if (!http || !http->tls || !credentials)
    return (-1);

  *credentials = cupsArrayNew(NULL, NULL);
  certs        = openssl_certificate_get_peers(http->tls, &count);

  DEBUG_printf(("1httpCopyCredentials: certs=%p, count=%u", certs, count));

  if (certs && count)
  {
    while (count > 0)
    {
      httpAddCredential(*credentials, certs->data, certs->size);
      certs ++;
      count --;
    }
  }

  return (0);
#else
  return (-1);
#endif // 0
}


/*
 * '_httpCreateCredentials()' - Create credentials in the internal format.
 */

http_tls_credentials_t			// O - Internal credentials
_httpCreateCredentials(
    cups_array_t *credentials)		// I - Array of credentials
{
  (void)credentials;

  return (NULL);
}


/*
 * '_httpFreeCredentials()' - Free internal credentials.
 */

void
_httpFreeCredentials(
    http_tls_credentials_t credentials)	// I - Internal credentials
{
  X509_free(credentials);
}


/*
 * 'httpCredentialsAreValidForName()' - Return whether the credentials are valid for the given name.
 *
 * @since CUPS 2.0/OS 10.10@
 */

int					// O - 1 if valid, 0 otherwise
httpCredentialsAreValidForName(
    cups_array_t *credentials,		// I - Credentials
    const char   *common_name)		// I - Name to check
{
  X509	*cert;				// Certificate
  int	result = 0;			// Result


  cert = http_create_credential((http_credential_t *)cupsArrayFirst(credentials));
  if (cert)
  {

    result = 1;
#if 0
    result = openssl_x509_crt_check_hostname(cert, common_name) != 0;

    if (result)
    {
      openssl_x509_crl_iter_t iter = NULL;
					/* Iterator */
      unsigned char	cserial[1024],	/* Certificate serial number */
			rserial[1024];	/* Revoked serial number */
      size_t		cserial_size,	/* Size of cert serial number */
			rserial_size;	/* Size of revoked serial number */

      _cupsMutexLock(&tls_mutex);

      if (openssl_x509_crl_get_crt_count(tls_crl) > 0)
      {
        cserial_size = sizeof(cserial);
        openssl_x509_crt_get_serial(cert, cserial, &cserial_size);

	rserial_size = sizeof(rserial);

        while (!openssl_x509_crl_iter_crt_serial(tls_crl, &iter, rserial, &rserial_size, NULL))
        {
          if (cserial_size == rserial_size && !memcmp(cserial, rserial, rserial_size))
	  {
	    result = 0;
	    break;
	  }

	  rserial_size = sizeof(rserial);
	}
	openssl_x509_crl_iter_deinit(iter);
      }

      _cupsMutexUnlock(&tls_mutex);
    }
#endif // 0

    X509_free(cert);
  }

  return (result);
}


/*
 * 'httpCredentialsGetTrust()' - Return the trust of credentials.
 *
 * @since CUPS 2.0/OS 10.10@
 */

http_trust_t				// O - Level of trust
httpCredentialsGetTrust(
    cups_array_t *credentials,		// I - Credentials
    const char   *common_name)		// I - Common name for trust lookup
{
  http_trust_t		trust = HTTP_TRUST_OK;
					// Trusted?
#if 0
  openssl_x509_crt_t	cert;		/* Certificate */
  cups_array_t		*tcreds = NULL;	/* Trusted credentials */
  _cups_globals_t	*cg = _cupsGlobals();
					/* Per-thread globals */


  if (!common_name)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("No common name specified."), 1);
    return (HTTP_TRUST_UNKNOWN);
  }

  if ((cert = http_create_credential((http_credential_t *)cupsArrayFirst(credentials))) == NULL)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to create credentials from array."), 1);
    return (HTTP_TRUST_UNKNOWN);
  }

  if (cg->any_root < 0)
  {
    _cupsSetDefaults();
    http_load_crl();
  }

 /*
  * Look this common name up in the default keychains...
  */

  httpLoadCredentials(NULL, &tcreds, common_name);

  if (tcreds)
  {
    char	credentials_str[1024],	/* String for incoming credentials */
		tcreds_str[1024];	/* String for saved credentials */

    httpCredentialsString(credentials, credentials_str, sizeof(credentials_str));
    httpCredentialsString(tcreds, tcreds_str, sizeof(tcreds_str));

    if (strcmp(credentials_str, tcreds_str))
    {
     /*
      * Credentials don't match, let's look at the expiration date of the new
      * credentials and allow if the new ones have a later expiration...
      */

      if (!cg->trust_first)
      {
       /*
        * Do not trust certificates on first use...
	*/

        _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Trust on first use is disabled."), 1);

        trust = HTTP_TRUST_INVALID;
      }
      else if (httpCredentialsGetExpiration(credentials) <= httpCredentialsGetExpiration(tcreds))
      {
       /*
        * The new credentials are not newly issued...
	*/

        _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("New credentials are older than stored credentials."), 1);

        trust = HTTP_TRUST_INVALID;
      }
      else if (!httpCredentialsAreValidForName(credentials, common_name))
      {
       /*
        * The common name does not match the issued certificate...
	*/

        _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("New credentials are not valid for name."), 1);

        trust = HTTP_TRUST_INVALID;
      }
      else if (httpCredentialsGetExpiration(tcreds) < time(NULL))
      {
       /*
        * Save the renewed credentials...
	*/

	trust = HTTP_TRUST_RENEWED;

        httpSaveCredentials(NULL, credentials, common_name);
      }
    }

    httpFreeCredentials(tcreds);
  }
  else if (cg->validate_certs && !httpCredentialsAreValidForName(credentials, common_name))
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("No stored credentials, not valid for name."), 1);
    trust = HTTP_TRUST_INVALID;
  }
  else if (!cg->trust_first)
  {
   /*
    * See if we have a site CA certificate we can compare...
    */

    if (!httpLoadCredentials(NULL, &tcreds, "site"))
    {
      if (cupsArrayCount(credentials) != (cupsArrayCount(tcreds) + 1))
      {
       /*
        * Certificate isn't directly generated from the CA cert...
	*/

        trust = HTTP_TRUST_INVALID;
      }
      else
      {
       /*
        * Do a tail comparison of the two certificates...
	*/

        http_credential_t	*a, *b;		/* Certificates */

        for (a = (http_credential_t *)cupsArrayFirst(tcreds), b = (http_credential_t *)cupsArrayIndex(credentials, 1);
	     a && b;
	     a = (http_credential_t *)cupsArrayNext(tcreds), b = (http_credential_t *)cupsArrayNext(credentials))
	  if (a->datalen != b->datalen || memcmp(a->data, b->data, a->datalen))
	    break;

        if (a || b)
	  trust = HTTP_TRUST_INVALID;
      }

      if (trust != HTTP_TRUST_OK)
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Credentials do not validate against site CA certificate."), 1);
    }
    else
    {
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Trust on first use is disabled."), 1);
      trust = HTTP_TRUST_INVALID;
    }
  }

  if (trust == HTTP_TRUST_OK && !cg->expired_certs)
  {
    time_t	curtime;		/* Current date/time */

    time(&curtime);
    if (curtime < openssl_x509_crt_get_activation_time(cert) ||
        curtime > openssl_x509_crt_get_expiration_time(cert))
    {
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Credentials have expired."), 1);
      trust = HTTP_TRUST_EXPIRED;
    }
  }

  if (trust == HTTP_TRUST_OK && !cg->any_root && cupsArrayCount(credentials) == 1)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Self-signed credentials are blocked."), 1);
    trust = HTTP_TRUST_INVALID;
  }

  openssl_x509_crt_deinit(cert);
#endif // 0

  return (trust);
}


/*
 * 'httpCredentialsGetExpiration()' - Return the expiration date of the credentials.
 *
 * @since CUPS 2.0/OS 10.10@
 */

time_t					// O - Expiration date of credentials
httpCredentialsGetExpiration(
    cups_array_t *credentials)		// I - Credentials
{
  time_t		result = 0;	// Result
#if 0
  openssl_x509_crt_t	cert;		// Certificate


  cert = http_create_credential((http_credential_t *)cupsArrayFirst(credentials));
  if (cert)
  {
    result = openssl_x509_crt_get_expiration_time(cert);
    openssl_x509_crt_deinit(cert);
  }
#endif // 0

  return (result);
}


/*
 * 'httpCredentialsString()' - Return a string representing the credentials.
 *
 * @since CUPS 2.0/OS 10.10@
 */

size_t					// O - Total size of credentials string
httpCredentialsString(
    cups_array_t *credentials,		// I - Credentials
    char         *buffer,		// I - Buffer
    size_t       bufsize)		// I - Size of buffer
{
  http_credential_t	*first;		// First certificate
  X509			*cert;		// Certificate


  DEBUG_printf(("httpCredentialsString(credentials=%p, buffer=%p, bufsize=" CUPS_LLFMT ")", credentials, buffer, CUPS_LLCAST bufsize));

  if (!buffer)
    return (0);

  if (bufsize > 0)
    *buffer = '\0';

  if ((first = (http_credential_t *)cupsArrayFirst(credentials)) != NULL && (cert = http_create_credential(first)) != NULL)
  {
    char		name[256],	// Common name associated with cert
			issuer[256];	// Issuer associated with cert
    time_t		expiration;	// Expiration date of cert
//    struct tm		exptm;		// Expiration date/time of cert
    int			sigalg;		// Signature algorithm
    unsigned char	md5_digest[16];	// MD5 result


    X509_NAME_oneline(X509_get_subject_name(cert), name, sizeof(name));
    X509_NAME_oneline(X509_get_issuer_name(cert), issuer, sizeof(issuer));

//    ASN1_TIME_to_tm(X509_get0_notAfter(cert), &exptm);
//    expiration = mktime(&exptm);
    expiration = 0;
    sigalg     = X509_get_signature_nid(cert);

    cupsHashData("md5", first->data, first->datalen, md5_digest, sizeof(md5_digest));

    snprintf(buffer, bufsize, "%s (issued by %s) / %s / sig(%d) / %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X", name, issuer, httpGetDateString(expiration), sigalg, md5_digest[0], md5_digest[1], md5_digest[2], md5_digest[3], md5_digest[4], md5_digest[5], md5_digest[6], md5_digest[7], md5_digest[8], md5_digest[9], md5_digest[10], md5_digest[11], md5_digest[12], md5_digest[13], md5_digest[14], md5_digest[15]);
    X509_free(cert);
  }

  DEBUG_printf(("1httpCredentialsString: Returning \"%s\".", buffer));

  return (strlen(buffer));
}


/*
 * 'httpLoadCredentials()' - Load X.509 credentials from a keychain file.
 *
 * @since CUPS 2.0/OS 10.10@
 */

int					// O - 0 on success, -1 on error
httpLoadCredentials(
    const char   *path,			// I  - Keychain/PKCS#12 path
    cups_array_t **credentials,		// IO - Credentials
    const char   *common_name)		// I  - Common name for credentials
{
  cups_file_t		*fp;		// Certificate file
  char			filename[1024],	// filename.crt
			temp[1024],	// Temporary string
			line[256];	// Base64-encoded line
  unsigned char		*data = NULL;	// Buffer for cert data
  size_t		alloc_data = 0,	// Bytes allocated
			num_data = 0;	// Bytes used
  int			decoded;	// Bytes decoded
  int			in_certificate = 0;
					// In a certificate?


  if (!credentials || !common_name)
    return (-1);

  if (!path)
    path = http_default_path(temp, sizeof(temp));
  if (!path)
    return (-1);

  http_make_path(filename, sizeof(filename), path, common_name, "crt");

  if ((fp = cupsFileOpen(filename, "r")) == NULL)
    return (-1);

  while (cupsFileGets(fp, line, sizeof(line)))
  {
    if (!strcmp(line, "-----BEGIN CERTIFICATE-----"))
    {
      if (in_certificate)
      {
       /*
	* Missing END CERTIFICATE...
	*/

        httpFreeCredentials(*credentials);
	*credentials = NULL;
        break;
      }

      in_certificate = 1;
    }
    else if (!strcmp(line, "-----END CERTIFICATE-----"))
    {
      if (!in_certificate || !num_data)
      {
       /*
	* Missing data...
	*/

        httpFreeCredentials(*credentials);
	*credentials = NULL;
        break;
      }

      if (!*credentials)
        *credentials = cupsArrayNew(NULL, NULL);

      if (httpAddCredential(*credentials, data, num_data))
      {
        httpFreeCredentials(*credentials);
	*credentials = NULL;
        break;
      }

      num_data       = 0;
      in_certificate = 0;
    }
    else if (in_certificate)
    {
      if (alloc_data == 0)
      {
        data       = malloc(2048);
	alloc_data = 2048;

        if (!data)
	  break;
      }
      else if ((num_data + strlen(line)) >= alloc_data)
      {
        unsigned char *tdata = realloc(data, alloc_data + 1024);
					/* Expanded buffer */

	if (!tdata)
	{
	  httpFreeCredentials(*credentials);
	  *credentials = NULL;
	  break;
	}

	data       = tdata;
        alloc_data += 1024;
      }

      decoded = alloc_data - num_data;
      httpDecode64_2((char *)data + num_data, &decoded, line);
      num_data += (size_t)decoded;
    }
  }

  cupsFileClose(fp);

  if (in_certificate)
  {
   /*
    * Missing END CERTIFICATE...
    */

    httpFreeCredentials(*credentials);
    *credentials = NULL;
  }

  if (data)
    free(data);

  return (*credentials ? 0 : -1);
}


/*
 * 'httpSaveCredentials()' - Save X.509 credentials to a keychain file.
 *
 * @since CUPS 2.0/OS 10.10@
 */

int					// O - -1 on error, 0 on success
httpSaveCredentials(
    const char   *path,			// I - Keychain/PKCS#12 path
    cups_array_t *credentials,		// I - Credentials
    const char   *common_name)		// I - Common name for credentials
{
  cups_file_t		*fp;		// Certificate file
  char			filename[1024],	// filename.crt
			nfilename[1024],// filename.crt.N
			temp[1024],	// Temporary string
			line[256];	// Base64-encoded line
  const unsigned char	*ptr;		// Pointer into certificate
  ssize_t		remaining;	// Bytes left
  http_credential_t	*cred;		// Current credential


  if (!credentials || !common_name)
    return (-1);

  if (!path)
    path = http_default_path(temp, sizeof(temp));
  if (!path)
    return (-1);

  http_make_path(filename, sizeof(filename), path, common_name, "crt");
  snprintf(nfilename, sizeof(nfilename), "%s.N", filename);

  if ((fp = cupsFileOpen(nfilename, "w")) == NULL)
    return (-1);

  fchmod(cupsFileNumber(fp), 0600);

  for (cred = (http_credential_t *)cupsArrayFirst(credentials);
       cred;
       cred = (http_credential_t *)cupsArrayNext(credentials))
  {
    cupsFilePuts(fp, "-----BEGIN CERTIFICATE-----\n");
    for (ptr = cred->data, remaining = (ssize_t)cred->datalen; remaining > 0; remaining -= 45, ptr += 45)
    {
      httpEncode64_2(line, sizeof(line), (char *)ptr, remaining > 45 ? 45 : remaining);
      cupsFilePrintf(fp, "%s\n", line);
    }
    cupsFilePuts(fp, "-----END CERTIFICATE-----\n");
  }

  cupsFileClose(fp);

  return (rename(nfilename, filename));
}


/*
 * '_httpTLSInitialize()' - Initialize the TLS stack.
 */

void
_httpTLSInitialize(void)
{
  // OpenSSL no longer requires explicit initialization...
}


/*
 * '_httpTLSPending()' - Return the number of pending TLS-encrypted bytes.
 */

size_t					// O - Bytes available
_httpTLSPending(http_t *http)		// I - HTTP connection
{
  return ((size_t)SSL_pending(http->tls));
}


/*
 * '_httpTLSRead()' - Read from a SSL/TLS connection.
 */

int					// O - Bytes read
_httpTLSRead(http_t *http,		// I - Connection to server
	     char   *buf,		// I - Buffer to store data
	     int    len)		// I - Length of buffer
{
  return (SSL_read((SSL *)(http->tls), buf, len));
}


/*
 * '_httpTLSSetOptions()' - Set TLS protocol and cipher suite options.
 */

void
_httpTLSSetOptions(int options,		// I - Options
                   int min_version,	// I - Minimum TLS version
                   int max_version)	// I - Maximum TLS version
{
  if (!(options & _HTTP_TLS_SET_DEFAULT) || tls_options < 0)
  {
    tls_options     = options;
    tls_min_version = min_version;
    tls_max_version = max_version;
  }
}


/*
 * '_httpTLSStart()' - Set up SSL/TLS support on a connection.
 */

int					// O - 0 on success, -1 on failure
_httpTLSStart(http_t *http)		// I - Connection to server
{
  BIO		*bio;			// Basic input/output context
  SSL_CTX	*context;		// Encryption context
  char		hostname[256],		// Hostname
		cipherlist[256];	// List of cipher suites
  unsigned long	error;			// Error code, if any
  static const int versions[] =		// SSL/TLS versions
  {
    TLS1_VERSION,			// No more SSL support in OpenSSL
    TLS1_VERSION,			// TLS/1.0
    TLS1_1_VERSION,			// TLS/1.1
    TLS1_2_VERSION,			// TLS/1.2
#ifdef TLS1_3_VERSION
    TLS1_3_VERSION,			// TLS/1.3
    TLS1_3_VERSION			// TLS/1.3 (max)
#else
    TLS1_2_VERSION,			// TLS/1.2
    TLS1_2_VERSION			// TLS/1.2 (max)
#endif // TLS1_3_VERSION
  };


  DEBUG_printf(("3_httpTLSStart(http=%p)", http));

  if (tls_options < 0)
  {
    DEBUG_puts("4_httpTLSStart: Setting defaults.");
    _cupsSetDefaults();
    DEBUG_printf(("4_httpTLSStart: tls_options=%x", tls_options));
  }

  if (http->mode == _HTTP_MODE_SERVER && !tls_keypath)
  {
    DEBUG_puts("4_httpTLSStart: cupsSetServerCredentials not called.");
    http->error  = errno = EINVAL;
    http->status = HTTP_STATUS_ERROR;
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Server credentials not set."), 1);

    return (-1);
  }

  if (http->mode == _HTTP_MODE_CLIENT)
  {
    // Negotiate a TLS connection as a client...
    context = SSL_CTX_new(TLS_client_method());
  }
  else
  {
    // Negotiate a TLS connection as a server
    char	crtfile[1024],		// Certificate file
		keyfile[1024];		// Private key file
    const char	*cn,			// Common name to lookup
		*cnptr;			// Pointer into common name
    int		have_creds = 0;		// Have credentials?

    context = SSL_CTX_new(TLS_server_method());

    // Find the TLS certificate...
    if (http->fields[HTTP_FIELD_HOST])
    {
      // Use hostname for TLS upgrade...
      strlcpy(hostname, http->fields[HTTP_FIELD_HOST], sizeof(hostname));
    }
    else
    {
      // Resolve hostname from connection address...
      http_addr_t	addr;		// Connection address
      socklen_t		addrlen;	// Length of address

      addrlen = sizeof(addr);
      if (getsockname(http->fd, (struct sockaddr *)&addr, &addrlen))
      {
        // Unable to get local socket address so use default...
	DEBUG_printf(("4_httpTLSStart: Unable to get socket address: %s", strerror(errno)));
	hostname[0] = '\0';
      }
      else if (httpAddrLocalhost(&addr))
      {
        // Local access top use default...
	hostname[0] = '\0';
      }
      else
      {
        // Lookup the socket address...
	httpAddrLookup(&addr, hostname, sizeof(hostname));
        DEBUG_printf(("4_httpTLSStart: Resolved socket address to \"%s\".", hostname));
      }
    }

    if (isdigit(hostname[0] & 255) || hostname[0] == '[')
      hostname[0] = '\0';		// Don't allow numeric addresses

    if (hostname[0])
      cn = hostname;
    else
      cn = tls_common_name;

    if (cn)
    {
      // First look in the CUPS keystore...
      http_make_path(crtfile, sizeof(crtfile), tls_keypath, cn, "crt");
      http_make_path(keyfile, sizeof(keyfile), tls_keypath, cn, "key");

      if (access(crtfile, R_OK) || access(keyfile, R_OK))
      {
        // No CUPS-managed certs, look for CA certs...
        char cacrtfile[1024], cakeyfile[1024];	// CA cert files

        snprintf(cacrtfile, sizeof(cacrtfile), "/etc/letsencrypt/live/%s/fullchain.pem", cn);
        snprintf(cakeyfile, sizeof(cakeyfile), "/etc/letsencrypt/live/%s/privkey.pem", cn);

        if ((access(cacrtfile, R_OK) || access(cakeyfile, R_OK)) && (cnptr = strchr(cn, '.')) != NULL)
        {
          // Try just domain name...
          cnptr ++;
          if (strchr(cnptr, '.'))
          {
            snprintf(cacrtfile, sizeof(cacrtfile), "/etc/letsencrypt/live/%s/fullchain.pem", cnptr);
            snprintf(cakeyfile, sizeof(cakeyfile), "/etc/letsencrypt/live/%s/privkey.pem", cnptr);
          }
        }

        if (!access(cacrtfile, R_OK) && !access(cakeyfile, R_OK))
        {
          // Use the CA certs...
          strlcpy(crtfile, cacrtfile, sizeof(crtfile));
          strlcpy(keyfile, cakeyfile, sizeof(keyfile));
        }
      }

      have_creds = !access(crtfile, R_OK) && !access(keyfile, R_OK);
    }

    if (!have_creds && tls_auto_create && cn)
    {
      DEBUG_printf(("4_httpTLSStart: Auto-create credentials for \"%s\".", cn));

      if (!cupsMakeServerCredentials(tls_keypath, cn, 0, NULL, time(NULL) + 365 * 86400))
      {
	DEBUG_puts("4_httpTLSStart: cupsMakeServerCredentials failed.");
	http->error  = errno = EINVAL;
	http->status = HTTP_STATUS_ERROR;
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to create server credentials."), 1);

	return (-1);
      }
    }

    SSL_CTX_use_PrivateKey_file(context, keyfile, SSL_FILETYPE_PEM);
    SSL_CTX_use_certificate_file(context, crtfile, SSL_FILETYPE_PEM);
  }

  // Set TLS options...
  strlcpy(cipherlist, "HIGH:!DH:+DHE", sizeof(cipherlist));
  if ((tls_options & _HTTP_TLS_ALLOW_RC4) && http->mode == _HTTP_MODE_CLIENT)
    strlcat(cipherlist, ":+RC4", sizeof(cipherlist));
  else
    strlcat(cipherlist, ":!RC4", sizeof(cipherlist));
  if (tls_options & _HTTP_TLS_DENY_CBC)
    strlcat(cipherlist, ":!SHA1:!SHA256:!SHA384", sizeof(cipherlist));
  strlcat(cipherlist, ":@STRENGTH", sizeof(cipherlist));

  SSL_CTX_set_min_proto_version(context, versions[tls_min_version]);
  SSL_CTX_set_max_proto_version(context, versions[tls_max_version]);
  SSL_CTX_set_cipher_list(context, cipherlist);

  // Setup a TLS session
  bio = BIO_new(&http_bio_methods);
  BIO_ctrl(bio, BIO_C_SET_FILE_PTR, 0, (char *)http);

  http->tls = SSL_new(context);
  SSL_set_bio(http->tls, bio, bio);

  if (http->mode == _HTTP_MODE_CLIENT)
  {
    // Negotiate as a server...
    if (SSL_connect(http->tls) < 1)
    {
      // Failed
      if ((error = ERR_get_error()) != 0)
        _cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, ERR_error_string(error, NULL), 0);

      http->status = HTTP_STATUS_ERROR;
      http->error  = EPIPE;

      SSL_CTX_free(context);

      SSL_free(http->tls);
      http->tls = NULL;

      return (-1);
    }
  }
  else
  {
    // Negotiate as a server...
    if (SSL_accept(http->tls) < 1)
    {
      // Failed
      if ((error = ERR_get_error()) != 0)
        _cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, ERR_error_string(error, NULL), 0);

      http->status = HTTP_STATUS_ERROR;
      http->error  = EPIPE;

      SSL_CTX_free(context);

      SSL_free(http->tls);
      http->tls = NULL;

      return (-1);
    }
  }

  return (0);
}


/*
 * '_httpTLSStop()' - Shut down SSL/TLS on a connection.
 */

void
_httpTLSStop(http_t *http)		// I - Connection to server
{
  SSL_CTX	*context;		// Context for encryption


  context = SSL_get_SSL_CTX(http->tls);

  SSL_shutdown(http->tls);
  SSL_CTX_free(context);
  SSL_free(http->tls);
}


/*
 * '_httpTLSWrite()' - Write to a SSL/TLS connection.
 */

int					// O - Bytes written
_httpTLSWrite(http_t     *http,		// I - Connection to server
	      const char *buf,		// I - Buffer holding data
	      int        len)		// I - Length of buffer
{
  return (SSL_write(http->tls, buf, len));
}


/*
 * 'http_bio_ctrl()' - Control the HTTP connection.
 */

static long				// O - Result/data
http_bio_ctrl(BIO  *h,			// I - BIO data
              int  cmd,			// I - Control command
	      long arg1,		// I - First argument
	      void *arg2)		// I - Second argument
{
  switch (cmd)
  {
    default :
        return (0);

    case BIO_CTRL_RESET :
        h->ptr = NULL;
	return (0);

    case BIO_C_SET_FILE_PTR :
        h->ptr  = arg2;
	h->init = 1;
	return (1);

    case BIO_C_GET_FILE_PTR :
        if (arg2)
	{
	  *((void **)arg2) = h->ptr;
	  return (1);
	}
	else
	  return (0);

    case BIO_CTRL_DUP :
    case BIO_CTRL_FLUSH :
        return (1);
  }
}


/*
 * 'http_bio_free()' - Free OpenSSL data.
 */

static int				// O - 1 on success, 0 on failure
http_bio_free(BIO *h)			// I - BIO data
{
  if (!h)
    return (0);

  if (h->shutdown)
  {
    h->init  = 0;
    h->flags = 0;
  }

  return (1);
}


/*
 * 'http_bio_new()' - Initialize an OpenSSL BIO structure.
 */

static int				// O - 1 on success, 0 on failure
http_bio_new(BIO *h)			// I - BIO data
{
  if (!h)
    return (0);

  h->init  = 0;
  h->num   = 0;
  h->ptr   = NULL;
  h->flags = 0;

  return (1);
}


/*
 * 'http_bio_puts()' - Send a string for OpenSSL.
 */

static int				// O - Bytes written
http_bio_puts(BIO        *h,		// I - BIO data
              const char *str)		// I - String to write
{
#ifdef WIN32
  return (send(((http_t *)h->ptr)->fd, str, (int)strlen(str), 0));
#else
  return ((int)send(((http_t *)h->ptr)->fd, str, strlen(str), 0));
#endif // WIN32
}


/*
 * 'http_bio_read()' - Read data for OpenSSL.
 */

static int				// O - Bytes read
http_bio_read(BIO  *h,			// I - BIO data
              char *buf,		// I - Buffer
	      int  size)		// I - Number of bytes to read
{
  http_t	*http;			// HTTP connection


  http = (http_t *)h->ptr;

  if (!http->blocking)
  {
   /*
    * Make sure we have data before we read...
    */

    if (!_httpWait(http, 10000, 0))
    {
#ifdef WIN32
      http->error = WSAETIMEDOUT;
#else
      http->error = ETIMEDOUT;
#endif // WIN32

      return (-1);
    }
  }

  return ((int)recv(http->fd, buf, (size_t)size, 0));
}


/*
 * 'http_bio_write()' - Write data for OpenSSL.
 */

static int				// O - Bytes written
http_bio_write(BIO        *h,		// I - BIO data
               const char *buf,		// I - Buffer to write
	       int        num)		// I - Number of bytes to write
{
  return (send(((http_t *)h->ptr)->fd, buf, num, 0));
}


/*
 * 'http_create_credential()' - Create a single credential in the internal format.
 */

static X509 *				// O - Certificate
http_create_credential(
    http_credential_t *credential)	// I - Credential
{
  X509	*cert = NULL;			// Certificate
  BIO	*bio;				// Basic I/O for string



  if ((bio = BIO_new_mem_buf(credential->data, credential->datalen)) == NULL)
    return (NULL);

  PEM_read_bio_X509(bio, &cert, NULL, (void *)"");

  BIO_free(bio);

  return (cert);
}


/*
 * 'http_default_path()' - Get the default credential store path.
 */

static const char *			// O - Path or NULL on error
http_default_path(
    char   *buffer,			// I - Path buffer
    size_t bufsize)			// I - Size of path buffer
{
  _cups_globals_t	*cg = _cupsGlobals();
					// Pointer to library globals


  if (cg->home && getuid())
  {
    snprintf(buffer, bufsize, "%s/.cups", cg->home);
    if (access(buffer, 0))
    {
      DEBUG_printf(("1http_default_path: Making directory \"%s\".", buffer));
      if (mkdir(buffer, 0700))
      {
        DEBUG_printf(("1http_default_path: Failed to make directory: %s", strerror(errno)));
        return (NULL);
      }
    }

    snprintf(buffer, bufsize, "%s/.cups/ssl", cg->home);
    if (access(buffer, 0))
    {
      DEBUG_printf(("1http_default_path: Making directory \"%s\".", buffer));
      if (mkdir(buffer, 0700))
      {
        DEBUG_printf(("1http_default_path: Failed to make directory: %s", strerror(errno)));
        return (NULL);
      }
    }
  }
  else
    strlcpy(buffer, CUPS_SERVERROOT "/ssl", bufsize);

  DEBUG_printf(("1http_default_path: Using default path \"%s\".", buffer));

  return (buffer);
}


#if 0
/*
 * 'http_load_crl()' - Load the certificate revocation list, if any.
 */

static void
http_load_crl(void)
{
  _cupsMutexLock(&tls_mutex);

  if (!openssl_x509_crl_init(&tls_crl))
  {
    cups_file_t		*fp;		// CRL file
    char		filename[1024],	// site.crl
			line[256];	// Base64-encoded line
    unsigned char	*data = NULL;	// Buffer for cert data
    size_t		alloc_data = 0,	// Bytes allocated
			num_data = 0;	// Bytes used
    int			decoded;	// Bytes decoded
    openssl_datum_t	datum;		// Data record


    http_make_path(filename, sizeof(filename), CUPS_SERVERROOT, "site", "crl");

    if ((fp = cupsFileOpen(filename, "r")) != NULL)
    {
      while (cupsFileGets(fp, line, sizeof(line)))
      {
	if (!strcmp(line, "-----BEGIN X509 CRL-----"))
	{
	  if (num_data)
	  {
	   /*
	    * Missing END X509 CRL...
	    */

	    break;
	  }
	}
	else if (!strcmp(line, "-----END X509 CRL-----"))
	{
	  if (!num_data)
	  {
	   /*
	    * Missing data...
	    */

	    break;
	  }

          datum.data = data;
	  datum.size = num_data;

	  openssl_x509_crl_import(tls_crl, &datum, GNUTLS_X509_FMT_PEM);

	  num_data = 0;
	}
	else
	{
	  if (alloc_data == 0)
	  {
	    data       = malloc(2048);
	    alloc_data = 2048;

	    if (!data)
	      break;
	  }
	  else if ((num_data + strlen(line)) >= alloc_data)
	  {
	    unsigned char *tdata = realloc(data, alloc_data + 1024);
					    // Expanded buffer

	    if (!tdata)
	      break;

	    data       = tdata;
	    alloc_data += 1024;
	  }

	  decoded = alloc_data - num_data;
	  httpDecode64_2((char *)data + num_data, &decoded, line);
	  num_data += (size_t)decoded;
	}
      }

      cupsFileClose(fp);

      if (data)
	free(data);
    }
  }

  _cupsMutexUnlock(&tls_mutex);
}
#endif // 0


/*
 * 'http_make_path()' - Format a filename for a certificate or key file.
 */

static const char *			// O - Filename
http_make_path(
    char       *buffer,			// I - Filename buffer
    size_t     bufsize,			// I - Size of buffer
    const char *dirname,		// I - Directory
    const char *filename,		// I - Filename (usually hostname)
    const char *ext)			// I - Extension
{
  char	*bufptr,			// Pointer into buffer
	*bufend = buffer + bufsize - 1;	// End of buffer


  snprintf(buffer, bufsize, "%s/", dirname);
  bufptr = buffer + strlen(buffer);

  while (*filename && bufptr < bufend)
  {
    if (_cups_isalnum(*filename) || *filename == '-' || *filename == '.')
      *bufptr++ = *filename;
    else
      *bufptr++ = '_';

    filename ++;
  }

  if (bufptr < bufend)
    *bufptr++ = '.';

  strlcpy(bufptr, ext, (size_t)(bufend - bufptr + 1));

  return (buffer);
}
