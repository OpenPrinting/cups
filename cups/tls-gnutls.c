//
// TLS support code for CUPS using GNU TLS.
//
// Note: This file is included from tls.c
//
// Copyright © 2020-2024 by OpenPrinting
// Copyright © 2007-2019 by Apple Inc.
// Copyright © 1997-2007 by Easy Software Products, all rights reserved.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

//
// Local functions...
//

static gnutls_x509_privkey_t gnutls_create_key(cups_credtype_t type);
static void		gnutls_free_certs(unsigned num_certs, gnutls_x509_crt_t *certs);
static ssize_t		gnutls_http_read(gnutls_transport_ptr_t ptr, void *data, size_t length);
static ssize_t		gnutls_http_write(gnutls_transport_ptr_t ptr, const void *data, size_t length);
static gnutls_x509_crt_t gnutls_import_certs(const char *credentials, unsigned *num_certs, gnutls_x509_crt_t *certs);
static void		gnutls_load_crl(void);


//
// Local globals...
//

static gnutls_x509_crl_t tls_crl = NULL;// Certificate revocation list


//
// 'cupsAreCredentialsValidForName()' - Return whether the credentials are valid for the given name.
//

bool					// O - `true` if valid, `false` otherwise
cupsAreCredentialsValidForName(
    const char *common_name,		// I - Name to check
    const char *credentials)		// I - Credentials
{
  bool			result = false;	// Result
  unsigned		num_certs = 16;	// Number of certificates
  gnutls_x509_crt_t	certs[16];	// Certificates


  // Range check input...
  if (!common_name || !*common_name || !credentials || !*credentials)
    return (false);

  if (!gnutls_import_certs(credentials, &num_certs, certs))
    return (false);

  result = gnutls_x509_crt_check_hostname(certs[0], common_name) != 0;

  if (result)
  {
    gnutls_x509_crl_iter_t iter = NULL;	// Iterator
    unsigned char	cserial[1024],	// Certificate serial number
			rserial[1024];	// Revoked serial number
    size_t		cserial_size,	// Size of cert serial number
			rserial_size;	// Size of revoked serial number

    cupsMutexLock(&tls_mutex);

    if (gnutls_x509_crl_get_crt_count(tls_crl) > 0)
    {
      cserial_size = sizeof(cserial);
      gnutls_x509_crt_get_serial(certs[0], cserial, &cserial_size);

      rserial_size = sizeof(rserial);

      while (!gnutls_x509_crl_iter_crt_serial(tls_crl, &iter, rserial, &rserial_size, NULL))
      {
        if (cserial_size == rserial_size && !memcmp(cserial, rserial, rserial_size))
	{
	  result = false;
	  break;
	}

	rserial_size = sizeof(rserial);
      }

      gnutls_x509_crl_iter_deinit(iter);
    }

    cupsMutexUnlock(&tls_mutex);
  }

  gnutls_free_certs(num_certs, certs);

  return (result);
}


//
// 'cupsCreateCredentials()' - Make an X.509 certificate and private key pair.
//
// This function creates an X.509 certificate and private key pair.  The
// certificate and key are stored in the directory "path" or, if "path" is
// `NULL`, in a per-user or system-wide (when running as root) certificate/key
// store.  The generated certificate is signed by the named root certificate or,
// if "root_name" is `NULL`, a site-wide default root certificate.  When
// "root_name" is `NULL` and there is no site-wide default root certificate, a
// self-signed certificate is generated instead.
//
// The "ca_cert" argument specifies whether a CA certificate should be created.
//
// The "purpose" argument specifies the purpose(s) used for the credentials as a
// bitwise OR of the following constants:
//
// - `CUPS_CREDPURPOSE_SERVER_AUTH` for validating TLS servers,
// - `CUPS_CREDPURPOSE_CLIENT_AUTH` for validating TLS clients,
// - `CUPS_CREDPURPOSE_CODE_SIGNING` for validating compiled code,
// - `CUPS_CREDPURPOSE_EMAIL_PROTECTION` for validating email messages,
// - `CUPS_CREDPURPOSE_TIME_STAMPING` for signing timestamps to objects, and/or
// - `CUPS_CREDPURPOSE_OCSP_SIGNING` for Online Certificate Status Protocol
//   message signing.
//
// The "type" argument specifies the type of credentials using one of the
// following constants:
//
// - `CUPS_CREDTYPE_DEFAULT`: default type (RSA-3072 or P-384),
// - `CUPS_CREDTYPE_RSA_2048_SHA256`: RSA with 2048-bit keys and SHA-256 hash,
// - `CUPS_CREDTYPE_RSA_3072_SHA256`: RSA with 3072-bit keys and SHA-256 hash,
// - `CUPS_CREDTYPE_RSA_4096_SHA256`: RSA with 4096-bit keys and SHA-256 hash,
// - `CUPS_CREDTYPE_ECDSA_P256_SHA256`: ECDSA using the P-256 curve with SHA-256 hash,
// - `CUPS_CREDTYPE_ECDSA_P384_SHA256`: ECDSA using the P-384 curve with SHA-256 hash, or
// - `CUPS_CREDTYPE_ECDSA_P521_SHA256`: ECDSA using the P-521 curve with SHA-256 hash.
//
// The "usage" argument specifies the usage(s) for the credentials as a bitwise
// OR of the following constants:
//
// - `CUPS_CREDUSAGE_DIGITAL_SIGNATURE`: digital signatures,
// - `CUPS_CREDUSAGE_NON_REPUDIATION`: non-repudiation/content commitment,
// - `CUPS_CREDUSAGE_KEY_ENCIPHERMENT`: key encipherment,
// - `CUPS_CREDUSAGE_DATA_ENCIPHERMENT`: data encipherment,
// - `CUPS_CREDUSAGE_KEY_AGREEMENT`: key agreement,
// - `CUPS_CREDUSAGE_KEY_CERT_SIGN`: key certicate signing,
// - `CUPS_CREDUSAGE_CRL_SIGN`: certificate revocation list signing,
// - `CUPS_CREDUSAGE_ENCIPHER_ONLY`: encipherment only,
// - `CUPS_CREDUSAGE_DECIPHER_ONLY`: decipherment only,
// - `CUPS_CREDUSAGE_DEFAULT_CA`: defaults for CA certificates,
// - `CUPS_CREDUSAGE_DEFAULT_TLS`: defaults for TLS certificates, and/or
// - `CUPS_CREDUSAGE_ALL`: all usages.
//
// The "organization", "org_unit", "locality", "state_province", and "country"
// arguments specify information about the identity and geolocation of the
// issuer.
//
// The "common_name" argument specifies the common name and the "num_alt_names"
// and "alt_names" arguments specify a list of DNS hostnames for the
// certificate.
//
// The "expiration_date" argument specifies the expiration date and time as a
// Unix `time_t` value in seconds.
//

bool					// O - `true` on success, `false` on error
cupsCreateCredentials(
    const char         *path,		// I - Directory path for certificate/key store or `NULL` for default
    bool               ca_cert,		// I - `true` to create a CA certificate, `false` for a client/server certificate
    cups_credpurpose_t purpose,		// I - Credential purposes
    cups_credtype_t    type,		// I - Credential type
    cups_credusage_t   usage,		// I - Credential usages
    const char         *organization,	// I - Organization or `NULL` to use common name
    const char         *org_unit,	// I - Organizational unit or `NULL` for none
    const char         *locality,	// I - City/town or `NULL` for "Unknown"
    const char         *state_province,	// I - State/province or `NULL` for "Unknown"
    const char         *country,	// I - Country or `NULL` for locale-based default
    const char         *common_name,	// I - Common name
    const char         *email,		// I - Email address or `NULL` for none
    size_t             num_alt_names,	// I - Number of subject alternate names
    const char * const *alt_names,	// I - Subject Alternate Names
    const char         *root_name,	// I - Root certificate/domain name or `NULL` for site/self-signed
    time_t             expiration_date)	// I - Expiration date
{
  bool			ret = false;	// Return value
  gnutls_x509_crt_t	crt = NULL;	// New certificate
  gnutls_x509_privkey_t	key = NULL;	// Encryption private key
  gnutls_x509_crt_t	root_crt = NULL;// Root certificate
  gnutls_x509_privkey_t	root_key = NULL;// Root private key
  char			defpath[1024],	// Default path
 			crtfile[1024],	// Certificate filename
			keyfile[1024],	// Private key filename
 			*root_crtdata,	// Root certificate data
			*root_keydata;	// Root private key data
  unsigned		gnutls_usage = 0;// GNU TLS keyUsage bits
  cups_file_t		*fp;		// Key/cert file
  unsigned char		buffer[65536];	// Buffer for x509 data
  size_t		bytes;		// Number of bytes of data
  unsigned char		serial[8];	// Serial number buffer
  time_t		curtime;	// Current time
  int			err;		// Result of GNU TLS calls


  DEBUG_printf("cupsCreateCredentials(path=\"%s\", ca_cert=%s, purpose=0x%x, type=%d, usage=0x%x, organization=\"%s\", org_unit=\"%s\", locality=\"%s\", state_province=\"%s\", country=\"%s\", common_name=\"%s\", num_alt_names=%u, alt_names=%p, root_name=\"%s\", expiration_date=%ld)", path, ca_cert ? "true" : "false", purpose, type, usage, organization, org_unit, locality, state_province, country, common_name, (unsigned)num_alt_names, alt_names, root_name, (long)expiration_date);

  // Filenames...
  if (!path)
    path = http_default_path(defpath, sizeof(defpath));

  if (!path || !common_name)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    goto done;
  }

  http_make_path(crtfile, sizeof(crtfile), path, common_name, "crt");
  http_make_path(keyfile, sizeof(keyfile), path, common_name, "key");

  // Create the encryption key...
  DEBUG_puts("1cupsCreateCredentials: Creating key pair.");

  key = gnutls_create_key(type);

  DEBUG_puts("1cupsCreateCredentials: Key pair created.");

  // Save it...
  bytes = sizeof(buffer);

  if ((err = gnutls_x509_privkey_export(key, GNUTLS_X509_FMT_PEM, buffer, &bytes)) < 0)
  {
    DEBUG_printf("1cupsCreateCredentials: Unable to export private key: %s", gnutls_strerror(err));
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, gnutls_strerror(err), 0);
    goto done;
  }
  else if ((fp = cupsFileOpen(keyfile, "w")) != NULL)
  {
    DEBUG_printf("1cupsCreateCredentials: Writing private key to \"%s\".", keyfile);
    cupsFileWrite(fp, (char *)buffer, bytes);
    cupsFileClose(fp);
  }
  else
  {
    DEBUG_printf("1cupsCreateCredentials: Unable to create private key file \"%s\": %s", keyfile, strerror(errno));
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    goto done;
  }

  // Create the certificate...
  DEBUG_puts("1cupsCreateCredentials: Generating X.509 certificate.");

  curtime   = time(NULL);
  serial[0] = (unsigned char)(curtime >> 56);
  serial[1] = (unsigned char)(curtime >> 48);
  serial[2] = (unsigned char)(curtime >> 40);
  serial[3] = (unsigned char)(curtime >> 32);
  serial[4] = (unsigned char)(curtime >> 24);
  serial[5] = (unsigned char)(curtime >> 16);
  serial[6] = (unsigned char)(curtime >> 8);
  serial[7] = (unsigned char)(curtime);

  if (!organization)
    organization = common_name;
  if (!org_unit)
    org_unit = "";
  if (!locality)
    locality = "Unknown";
  if (!state_province)
    state_province = "Unknown";
  if (!country)
    country = "US";

  gnutls_x509_crt_init(&crt);
  gnutls_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_X520_COUNTRY_NAME, 0, "US", 2);
  gnutls_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_X520_COMMON_NAME, 0, common_name, strlen(common_name));
  gnutls_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_X520_ORGANIZATION_NAME, 0, organization, strlen(organization));
  gnutls_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_X520_ORGANIZATIONAL_UNIT_NAME, 0, org_unit, strlen(org_unit));
  gnutls_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_X520_STATE_OR_PROVINCE_NAME, 0, state_province, strlen(state_province));
  gnutls_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_X520_LOCALITY_NAME, 0, locality, strlen(locality));
  if (email && *email)
    gnutls_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_PKCS9_EMAIL, 0, email, (unsigned)strlen(email));
  gnutls_x509_crt_set_key(crt, key);
  gnutls_x509_crt_set_serial(crt, serial, sizeof(serial));
  gnutls_x509_crt_set_activation_time(crt, curtime);
  gnutls_x509_crt_set_expiration_time(crt, expiration_date);
  gnutls_x509_crt_set_ca_status(crt, ca_cert ? 1 : 0);
  gnutls_x509_crt_set_subject_alt_name(crt, GNUTLS_SAN_DNSNAME, common_name, (unsigned)strlen(common_name), GNUTLS_FSAN_SET);
  if (!strchr(common_name, '.'))
  {
    // Add common_name.local to the list, too...
    char localname[256];                // hostname.local

    snprintf(localname, sizeof(localname), "%s.local", common_name);
    gnutls_x509_crt_set_subject_alt_name(crt, GNUTLS_SAN_DNSNAME, localname, (unsigned)strlen(localname), GNUTLS_FSAN_APPEND);
  }
  gnutls_x509_crt_set_subject_alt_name(crt, GNUTLS_SAN_DNSNAME, "localhost", 9, GNUTLS_FSAN_APPEND);
  if (num_alt_names > 0)
  {
    size_t i;				// Looping var

    for (i = 0; i < num_alt_names; i ++)
    {
      if (strcmp(alt_names[i], "localhost"))
      {
        gnutls_x509_crt_set_subject_alt_name(crt, GNUTLS_SAN_DNSNAME, alt_names[i], (unsigned)strlen(alt_names[i]), GNUTLS_FSAN_APPEND);
      }
    }
  }

  if (purpose & CUPS_CREDPURPOSE_SERVER_AUTH)
    gnutls_x509_crt_set_key_purpose_oid(crt, GNUTLS_KP_TLS_WWW_SERVER, 0);
  if (purpose & CUPS_CREDPURPOSE_CLIENT_AUTH)
    gnutls_x509_crt_set_key_purpose_oid(crt, GNUTLS_KP_TLS_WWW_CLIENT, 0);
  if (purpose & CUPS_CREDPURPOSE_CODE_SIGNING)
    gnutls_x509_crt_set_key_purpose_oid(crt, GNUTLS_KP_CODE_SIGNING, 0);
  if (purpose & CUPS_CREDPURPOSE_EMAIL_PROTECTION)
    gnutls_x509_crt_set_key_purpose_oid(crt, GNUTLS_KP_EMAIL_PROTECTION, 0);
  if (purpose & CUPS_CREDPURPOSE_OCSP_SIGNING)
    gnutls_x509_crt_set_key_purpose_oid(crt, GNUTLS_KP_OCSP_SIGNING, 0);

  if (usage & CUPS_CREDUSAGE_DIGITAL_SIGNATURE)
    gnutls_usage |= GNUTLS_KEY_DIGITAL_SIGNATURE;
  if (usage & CUPS_CREDUSAGE_NON_REPUDIATION)
    gnutls_usage |= GNUTLS_KEY_NON_REPUDIATION;
  if (usage & CUPS_CREDUSAGE_KEY_ENCIPHERMENT)
    gnutls_usage |= GNUTLS_KEY_KEY_ENCIPHERMENT;
  if (usage & CUPS_CREDUSAGE_DATA_ENCIPHERMENT)
    gnutls_usage |= GNUTLS_KEY_DATA_ENCIPHERMENT;
  if (usage & CUPS_CREDUSAGE_KEY_AGREEMENT)
    gnutls_usage |= GNUTLS_KEY_KEY_AGREEMENT;
  if (usage & CUPS_CREDUSAGE_KEY_CERT_SIGN)
    gnutls_usage |= GNUTLS_KEY_KEY_CERT_SIGN;
  if (usage & CUPS_CREDUSAGE_CRL_SIGN)
    gnutls_usage |= GNUTLS_KEY_CRL_SIGN;
  if (usage & CUPS_CREDUSAGE_ENCIPHER_ONLY)
    gnutls_usage |= GNUTLS_KEY_ENCIPHER_ONLY;
  if (usage & CUPS_CREDUSAGE_DECIPHER_ONLY)
    gnutls_usage |= GNUTLS_KEY_DECIPHER_ONLY;

  gnutls_x509_crt_set_key_usage(crt, gnutls_usage);
  gnutls_x509_crt_set_version(crt, 3);

  bytes = sizeof(buffer);
  if (gnutls_x509_crt_get_key_id(crt, 0, buffer, &bytes) >= 0)
    gnutls_x509_crt_set_subject_key_id(crt, buffer, bytes);

  // Try loading a root certificate...
  if (!ca_cert)
  {
    root_crtdata = cupsCopyCredentials(path, root_name ? root_name : "_site_");
    root_keydata = cupsCopyCredentialsKey(path, root_name ? root_name : "_site_");

    if (root_crtdata && root_keydata)
    {
      // Load root certificate...
      gnutls_datum_t    datum;          // Datum for cert/key

      datum.data = (unsigned char *)root_crtdata;
      datum.size = strlen(root_crtdata);

      gnutls_x509_crt_init(&root_crt);
      if (gnutls_x509_crt_import(root_crt, &datum, GNUTLS_X509_FMT_PEM) < 0)
      {
        // No good, clear it...
        gnutls_x509_crt_deinit(root_crt);
        root_crt = NULL;
      }
      else
      {
        // Load root private key...
        datum.data = (unsigned char *)root_keydata;
        datum.size = strlen(root_keydata);

        gnutls_x509_privkey_init(&root_key);
        if (gnutls_x509_privkey_import(root_key, &datum, GNUTLS_X509_FMT_PEM) < 0)
        {
          // No food, clear them...
          gnutls_x509_privkey_deinit(root_key);
          root_key = NULL;

          gnutls_x509_crt_deinit(root_crt);
          root_crt = NULL;
        }
      }
    }

    free(root_crtdata);
    free(root_keydata);
  }

  if (root_crt && root_key)
  {
    gnutls_x509_crt_sign(crt, root_crt, root_key);
    gnutls_x509_crt_deinit(root_crt);
    gnutls_x509_privkey_deinit(root_key);
  }
  else
  {
    gnutls_x509_crt_sign(crt, crt, key);
  }

  // Save it...
  bytes = sizeof(buffer);
  if ((err = gnutls_x509_crt_export(crt, GNUTLS_X509_FMT_PEM, buffer, &bytes)) < 0)
  {
    DEBUG_printf("1cupsCreateCredentials: Unable to export public key and X.509 certificate: %s", gnutls_strerror(err));
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, gnutls_strerror(err), 0);
    goto done;
  }
  else if ((fp = cupsFileOpen(crtfile, "w")) != NULL)
  {
    DEBUG_printf("1cupsCreateCredentials: Writing public key and X.509 certificate to \"%s\".", crtfile);
    cupsFileWrite(fp, (char *)buffer, bytes);
    cupsFileClose(fp);
  }
  else
  {
    DEBUG_printf("1cupsCreateCredentials: Unable to create public key and X.509 certificate file \"%s\": %s", crtfile, strerror(errno));
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    goto done;
  }

  DEBUG_puts("1cupsCreateCredentials: Successfully created credentials.");

  ret = true;

  // Cleanup...
  done:

  if (crt)
    gnutls_x509_crt_deinit(crt);
  if (key)
    gnutls_x509_privkey_deinit(key);

  return (ret);
}


//
// 'cupsCreateCredentialsRequest()' - Make an X.509 Certificate Signing Request.
//
// This function creates an X.509 certificate signing request (CSR) and
// associated private key.  The CSR and key are stored in the directory "path"
// or, if "path" is `NULL`, in a per-user or system-wide (when running as root)
// certificate/key store.
//
// The "purpose" argument specifies the purpose(s) used for the credentials as a
// bitwise OR of the following constants:
//
// - `CUPS_CREDPURPOSE_SERVER_AUTH` for validating TLS servers,
// - `CUPS_CREDPURPOSE_CLIENT_AUTH` for validating TLS clients,
// - `CUPS_CREDPURPOSE_CODE_SIGNING` for validating compiled code,
// - `CUPS_CREDPURPOSE_EMAIL_PROTECTION` for validating email messages,
// - `CUPS_CREDPURPOSE_TIME_STAMPING` for signing timestamps to objects, and/or
// - `CUPS_CREDPURPOSE_OCSP_SIGNING` for Online Certificate Status Protocol
//   message signing.
//
// The "type" argument specifies the type of credentials using one of the
// following constants:
//
// - `CUPS_CREDTYPE_DEFAULT`: default type (RSA-3072 or P-384),
// - `CUPS_CREDTYPE_RSA_2048_SHA256`: RSA with 2048-bit keys and SHA-256 hash,
// - `CUPS_CREDTYPE_RSA_3072_SHA256`: RSA with 3072-bit keys and SHA-256 hash,
// - `CUPS_CREDTYPE_RSA_4096_SHA256`: RSA with 4096-bit keys and SHA-256 hash,
// - `CUPS_CREDTYPE_ECDSA_P256_SHA256`: ECDSA using the P-256 curve with SHA-256 hash,
// - `CUPS_CREDTYPE_ECDSA_P384_SHA256`: ECDSA using the P-384 curve with SHA-256 hash, or
// - `CUPS_CREDTYPE_ECDSA_P521_SHA256`: ECDSA using the P-521 curve with SHA-256 hash.
//
// The "usage" argument specifies the usage(s) for the credentials as a bitwise
// OR of the following constants:
//
// - `CUPS_CREDUSAGE_DIGITAL_SIGNATURE`: digital signatures,
// - `CUPS_CREDUSAGE_NON_REPUDIATION`: non-repudiation/content commitment,
// - `CUPS_CREDUSAGE_KEY_ENCIPHERMENT`: key encipherment,
// - `CUPS_CREDUSAGE_DATA_ENCIPHERMENT`: data encipherment,
// - `CUPS_CREDUSAGE_KEY_AGREEMENT`: key agreement,
// - `CUPS_CREDUSAGE_KEY_CERT_SIGN`: key certicate signing,
// - `CUPS_CREDUSAGE_CRL_SIGN`: certificate revocation list signing,
// - `CUPS_CREDUSAGE_ENCIPHER_ONLY`: encipherment only,
// - `CUPS_CREDUSAGE_DECIPHER_ONLY`: decipherment only,
// - `CUPS_CREDUSAGE_DEFAULT_CA`: defaults for CA certificates,
// - `CUPS_CREDUSAGE_DEFAULT_TLS`: defaults for TLS certificates, and/or
// - `CUPS_CREDUSAGE_ALL`: all usages.
//
// The "organization", "org_unit", "locality", "state_province", and "country"
// arguments specify information about the identity and geolocation of the
// issuer.
//
// The "common_name" argument specifies the common name and the "num_alt_names"
// and "alt_names" arguments specify a list of DNS hostnames for the
// certificate.
//

bool					// O - `true` on success, `false` on error
cupsCreateCredentialsRequest(
    const char         *path,		// I - Directory path for certificate/key store or `NULL` for default
    cups_credpurpose_t purpose,		// I - Credential purposes
    cups_credtype_t    type,		// I - Credential type
    cups_credusage_t   usage,		// I - Credential usages
    const char         *organization,	// I - Organization or `NULL` to use common name
    const char         *org_unit,	// I - Organizational unit or `NULL` for none
    const char         *locality,	// I - City/town or `NULL` for "Unknown"
    const char         *state_province,	// I - State/province or `NULL` for "Unknown"
    const char         *country,	// I - Country or `NULL` for locale-based default
    const char         *common_name,	// I - Common name
    const char         *email,		// I - Email address or `NULL` for none
    size_t             num_alt_names,	// I - Number of subject alternate names
    const char * const *alt_names)	// I - Subject Alternate Names
{
  bool			ret = false;	// Return value
  gnutls_x509_crq_t	crq = NULL;	// Certificate request
  gnutls_x509_privkey_t	key = NULL;	// Private/public key pair
  char			defpath[1024],	// Default path
 			csrfile[1024],	// Certificate signing request filename
			keyfile[1024];	// Private key filename
  unsigned		gnutls_usage = 0;// GNU TLS keyUsage bits
  cups_file_t		*fp;		// Key/cert file
  unsigned char		buffer[8192];	// Buffer for key/cert data
  size_t		bytes;		// Number of bytes of data
  int			err;		// GNU TLS status


  DEBUG_printf("cupsCreateCredentialsRequest(path=\"%s\", purpose=0x%x, type=%d, usage=0x%x, organization=\"%s\", org_unit=\"%s\", locality=\"%s\", state_province=\"%s\", country=\"%s\", common_name=\"%s\", num_alt_names=%u, alt_names=%p)", path, purpose, type, usage, organization, org_unit, locality, state_province, country, common_name, (unsigned)num_alt_names, alt_names);

  // Filenames...
  if (!path)
    path = http_default_path(defpath, sizeof(defpath));

  if (!path || !common_name)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    goto done;
  }

  http_make_path(csrfile, sizeof(csrfile), path, common_name, "csr");
  http_make_path(keyfile, sizeof(keyfile), path, common_name, "key");

  // Create the encryption key...
  DEBUG_puts("1cupsCreateCredentialsRequest: Creating key pair.");

  key = gnutls_create_key(type);

  DEBUG_puts("1cupsCreateCredentialsRequest: Key pair created.");

  // Save it...
  bytes = sizeof(buffer);

  if ((err = gnutls_x509_privkey_export(key, GNUTLS_X509_FMT_PEM, buffer, &bytes)) < 0)
  {
    DEBUG_printf("1cupsCreateCredentialsRequest: Unable to export private key: %s", gnutls_strerror(err));
    goto done;
  }
  else if ((fp = cupsFileOpen(keyfile, "w")) != NULL)
  {
    DEBUG_printf("1cupsCreateCredentialsRequest: Writing private key to \"%s\".", keyfile);
    cupsFileWrite(fp, (char *)buffer, bytes);
    cupsFileClose(fp);
  }
  else
  {
    DEBUG_printf("1cupsCreateCredentialsRequest: Unable to create private key file \"%s\": %s", keyfile, strerror(errno));
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    goto done;
  }

  // Create the certificate...
  DEBUG_puts("1cupsCreateCredentialsRequest: Generating X.509 certificate request.");

  if (!organization)
    organization = common_name;
  if (!org_unit)
    org_unit = "";
  if (!locality)
    locality = "Unknown";
  if (!state_province)
    state_province = "Unknown";
  if (!country)
    country = "US";

  gnutls_x509_crq_init(&crq);
  gnutls_x509_crq_set_dn_by_oid(crq, GNUTLS_OID_X520_COUNTRY_NAME, 0, country, (unsigned)strlen(country));
  gnutls_x509_crq_set_dn_by_oid(crq, GNUTLS_OID_X520_COMMON_NAME, 0, common_name, (unsigned)strlen(common_name));
  gnutls_x509_crq_set_dn_by_oid(crq, GNUTLS_OID_X520_ORGANIZATION_NAME, 0, organization, (unsigned)strlen(organization));
  gnutls_x509_crq_set_dn_by_oid(crq, GNUTLS_OID_X520_ORGANIZATIONAL_UNIT_NAME, 0, org_unit, (unsigned)strlen(org_unit));
  gnutls_x509_crq_set_dn_by_oid(crq, GNUTLS_OID_X520_STATE_OR_PROVINCE_NAME, 0, state_province, (unsigned)strlen(state_province));
  gnutls_x509_crq_set_dn_by_oid(crq, GNUTLS_OID_X520_LOCALITY_NAME, 0, locality, (unsigned)strlen(locality));
  if (email && *email)
    gnutls_x509_crq_set_dn_by_oid(crq, GNUTLS_OID_PKCS9_EMAIL, 0, email, (unsigned)strlen(email));
  gnutls_x509_crq_set_key(crq, key);
  gnutls_x509_crq_set_subject_alt_name(crq, GNUTLS_SAN_DNSNAME, common_name, (unsigned)strlen(common_name), GNUTLS_FSAN_SET);
  if (!strchr(common_name, '.'))
  {
    // Add common_name.local to the list, too...
    char localname[256];                // hostname.local

    snprintf(localname, sizeof(localname), "%s.local", common_name);
    gnutls_x509_crq_set_subject_alt_name(crq, GNUTLS_SAN_DNSNAME, localname, (unsigned)strlen(localname), GNUTLS_FSAN_APPEND);
  }
  gnutls_x509_crq_set_subject_alt_name(crq, GNUTLS_SAN_DNSNAME, "localhost", 9, GNUTLS_FSAN_APPEND);
  if (num_alt_names > 0)
  {
    size_t i;				// Looping var

    for (i = 0; i < num_alt_names; i ++)
    {
      if (strcmp(alt_names[i], "localhost"))
      {
        gnutls_x509_crq_set_subject_alt_name(crq, GNUTLS_SAN_DNSNAME, alt_names[i], (unsigned)strlen(alt_names[i]), GNUTLS_FSAN_APPEND);
      }
    }
  }

  if (purpose & CUPS_CREDPURPOSE_SERVER_AUTH)
    gnutls_x509_crq_set_key_purpose_oid(crq, GNUTLS_KP_TLS_WWW_SERVER, 0);
  if (purpose & CUPS_CREDPURPOSE_CLIENT_AUTH)
    gnutls_x509_crq_set_key_purpose_oid(crq, GNUTLS_KP_TLS_WWW_CLIENT, 0);
  if (purpose & CUPS_CREDPURPOSE_CODE_SIGNING)
    gnutls_x509_crq_set_key_purpose_oid(crq, GNUTLS_KP_CODE_SIGNING, 0);
  if (purpose & CUPS_CREDPURPOSE_EMAIL_PROTECTION)
    gnutls_x509_crq_set_key_purpose_oid(crq, GNUTLS_KP_EMAIL_PROTECTION, 0);
  if (purpose & CUPS_CREDPURPOSE_OCSP_SIGNING)
    gnutls_x509_crq_set_key_purpose_oid(crq, GNUTLS_KP_OCSP_SIGNING, 0);

  if (usage & CUPS_CREDUSAGE_DIGITAL_SIGNATURE)
    gnutls_usage |= GNUTLS_KEY_DIGITAL_SIGNATURE;
  if (usage & CUPS_CREDUSAGE_NON_REPUDIATION)
    gnutls_usage |= GNUTLS_KEY_NON_REPUDIATION;
  if (usage & CUPS_CREDUSAGE_KEY_ENCIPHERMENT)
    gnutls_usage |= GNUTLS_KEY_KEY_ENCIPHERMENT;
  if (usage & CUPS_CREDUSAGE_DATA_ENCIPHERMENT)
    gnutls_usage |= GNUTLS_KEY_DATA_ENCIPHERMENT;
  if (usage & CUPS_CREDUSAGE_KEY_AGREEMENT)
    gnutls_usage |= GNUTLS_KEY_KEY_AGREEMENT;
  if (usage & CUPS_CREDUSAGE_KEY_CERT_SIGN)
    gnutls_usage |= GNUTLS_KEY_KEY_CERT_SIGN;
  if (usage & CUPS_CREDUSAGE_CRL_SIGN)
    gnutls_usage |= GNUTLS_KEY_CRL_SIGN;
  if (usage & CUPS_CREDUSAGE_ENCIPHER_ONLY)
    gnutls_usage |= GNUTLS_KEY_ENCIPHER_ONLY;
  if (usage & CUPS_CREDUSAGE_DECIPHER_ONLY)
    gnutls_usage |= GNUTLS_KEY_DECIPHER_ONLY;

  gnutls_x509_crq_set_key_usage(crq, gnutls_usage);
  gnutls_x509_crq_set_version(crq, 3);

  gnutls_x509_crq_sign2(crq, key, GNUTLS_DIG_SHA256, 0);

  // Save it...
  bytes = sizeof(buffer);
  if ((err = gnutls_x509_crq_export(crq, GNUTLS_X509_FMT_PEM, buffer, &bytes)) < 0)
  {
    DEBUG_printf("1cupsCreateCredentialsRequest: Unable to export public key and X.509 certificate request: %s", gnutls_strerror(err));
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, gnutls_strerror(err), 0);
    goto done;
  }
  else if ((fp = cupsFileOpen(csrfile, "w")) != NULL)
  {
    DEBUG_printf("1cupsCreateCredentialsRequest: Writing public key and X.509 certificate request to \"%s\".", csrfile);
    cupsFileWrite(fp, (char *)buffer, bytes);
    cupsFileClose(fp);
  }
  else
  {
    DEBUG_printf("1cupsCreateCredentialsRequest: Unable to create public key and X.509 certificate request file \"%s\": %s", csrfile, strerror(errno));
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    goto done;
  }

  DEBUG_puts("1cupsCreateCredentialsRequest: Successfully created credentials request.");

  ret = true;

  // Cleanup...
  done:

  if (crq)
    gnutls_x509_crq_deinit(crq);
  if (key)
    gnutls_x509_privkey_deinit(key);

  return (ret);
}


//
// 'cupsGetCredentialsExpiration()' - Return the expiration date of the credentials.
//

time_t					// O - Expiration date of credentials
cupsGetCredentialsExpiration(
    const char *credentials)		// I - Credentials
{
  time_t		result = 0;	// Result
  unsigned		num_certs = 16;	// Number of certificates
  gnutls_x509_crt_t	certs[16];	// Certificates


  if (gnutls_import_certs(credentials, &num_certs, certs))
  {
    result = gnutls_x509_crt_get_expiration_time(certs[0]);
    gnutls_free_certs(num_certs, certs);
  }

  return (result);
}


//
// 'cupsGetCredentialsInfo()' - Return a string describing the credentials.
//

char *					// O - Credential description string
cupsGetCredentialsInfo(
    const char *credentials,		// I - Credentials
    char       *buffer,			// I - Buffer
    size_t     bufsize)			// I - Size of buffer
{
  unsigned		num_certs = 16;	// Number of certificates
  gnutls_x509_crt_t	certs[16];	// Certificates


  DEBUG_printf("httpCredentialsString(credentials=%p, buffer=%p, bufsize=" CUPS_LLFMT ")", credentials, buffer, CUPS_LLCAST bufsize);

  if (buffer)
    *buffer = '\0';

  if (!credentials || !buffer || bufsize < 32)
  {
    DEBUG_puts("1cupsGetCredentialsInfo: Returning NULL.");
    return (NULL);
  }

  if (gnutls_import_certs(credentials, &num_certs, certs))
  {
    char		name[256],	// Common name associated with cert
			issuer[256];	// Issuer associated with cert
    size_t		len;		// Length of string
    time_t		expiration;	// Expiration date of cert
    char		expstr[256];	// Expiration date as string */
    int			sigalg;		// Signature algorithm
    unsigned char	md5_digest[16];	// MD5 result

    len = sizeof(name) - 1;
    if (gnutls_x509_crt_get_dn_by_oid(certs[0], GNUTLS_OID_X520_COMMON_NAME, 0, 0, name, &len) >= 0)
      name[len] = '\0';
    else
      cupsCopyString(name, "unknown", sizeof(name));

    len = sizeof(issuer) - 1;
    if (gnutls_x509_crt_get_issuer_dn_by_oid(certs[0], GNUTLS_OID_X520_ORGANIZATION_NAME, 0, 0, issuer, &len) >= 0)
      issuer[len] = '\0';
    else
      cupsCopyString(issuer, "unknown", sizeof(issuer));

    expiration = gnutls_x509_crt_get_expiration_time(certs[0]);
    sigalg     = gnutls_x509_crt_get_signature_algorithm(certs[0]);

    cupsHashData("md5", credentials, strlen(credentials), md5_digest, sizeof(md5_digest));

    snprintf(buffer, bufsize, "%s (issued by %s) / %s / %s / %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X", name, issuer, httpGetDateString2(expiration, expstr, sizeof(expstr)), gnutls_sign_get_name((gnutls_sign_algorithm_t)sigalg), md5_digest[0], md5_digest[1], md5_digest[2], md5_digest[3], md5_digest[4], md5_digest[5], md5_digest[6], md5_digest[7], md5_digest[8], md5_digest[9], md5_digest[10], md5_digest[11], md5_digest[12], md5_digest[13], md5_digest[14], md5_digest[15]);

    gnutls_free_certs(num_certs, certs);
  }

  DEBUG_printf("1cupsGetCredentialsInfo: Returning \"%s\".", buffer);

  return (buffer);
}


//
// 'cupsGetCredentialsTrust()' - Return the trust of credentials.
//
// This function determines the level of trust for the supplied credentials.
// The "path" parameter specifies the certificate/key store for known
// credentials and certificate authorities.  The "common_name" parameter
// specifies the FQDN of the service being accessed such as
// "printer.example.com".  The "credentials" parameter provides the credentials
// being evaluated, which are usually obtained with the
// @link httpCopyPeerCredentials@ function.  The "require_ca" parameter
// specifies whether a CA-signed certificate is required for trust.
//
// The `AllowAnyRoot`, `AllowExpiredCerts`, `TrustOnFirstUse`, and
// `ValidateCerts` options in the "client.conf" file (or corresponding
// preferences file on macOS) control the trust policy, which defaults to
// AllowAnyRoot=Yes, AllowExpiredCerts=No, TrustOnFirstUse=Yes, and
// ValidateCerts=No.  When the "require_ca" parameter is `true` the AllowAnyRoot
// and TrustOnFirstUse policies are turned off ("No").
//
// The returned trust value can be one of the following:
//
// - `HTTP_TRUST_OK`: Credentials are OK/trusted
// - `HTTP_TRUST_INVALID`: Credentials are invalid
// - `HTTP_TRUST_EXPIRED`: Credentials are expired
// - `HTTP_TRUST_RENEWED`: Credentials have been renewed
// - `HTTP_TRUST_UNKNOWN`: Credentials are unknown/new
//

http_trust_t				// O - Level of trust
cupsGetCredentialsTrust(
    const char *path,	        	// I - Directory path for certificate/key store or `NULL` for default
    const char *common_name,		// I - Common name for trust lookup
    const char *credentials,		// I - Credentials
    bool       require_ca)		// I - Require a CA-signed certificate?
{
  http_trust_t		trust = HTTP_TRUST_OK;
					// Trusted?
  char			defpath[1024],	// Default path
 			*tcreds = NULL;	// Trusted credentials
  unsigned		num_certs = 16;	// Number of certificates
  gnutls_x509_crt_t	certs[16];	// Certificates
  _cups_globals_t	*cg = _cupsGlobals();
					// Per-thread globals


  // Range check input...
  if (!path)
    path = http_default_path(defpath, sizeof(defpath));

  if (!path || !credentials || !common_name)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), false);
    return (HTTP_TRUST_UNKNOWN);
  }

  // Load the credentials...
  if (!gnutls_import_certs(credentials, &num_certs, certs))
  {
    _cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, _("Unable to import credentials."), 1);
    return (HTTP_TRUST_UNKNOWN);
  }

  if (cg->any_root < 0)
  {
    _cupsSetDefaults();
    gnutls_load_crl();
  }

  // Look this common name up in the default keychains...
  if ((tcreds = cupsCopyCredentials(path, common_name)) != NULL)
  {
    char	credentials_str[1024],	// String for incoming credentials
		tcreds_str[1024];	// String for saved credentials

    cupsGetCredentialsInfo(credentials, credentials_str, sizeof(credentials_str));
    cupsGetCredentialsInfo(tcreds, tcreds_str, sizeof(tcreds_str));

    if (strcmp(credentials_str, tcreds_str))
    {
      // Credentials don't match, let's look at the expiration date of the new
      // credentials and allow if the new ones have a later expiration...
      if (!cg->trust_first || require_ca)
      {
        // Do not trust certificates on first use...
        _cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, _("Trust on first use is disabled."), 1);

        trust = HTTP_TRUST_INVALID;
      }
      else if (cupsGetCredentialsExpiration(credentials) <= cupsGetCredentialsExpiration(tcreds))
      {
        // The new credentials are not newly issued...
        _cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, _("New credentials are older than stored credentials."), 1);

        trust = HTTP_TRUST_INVALID;
      }
      else if (!cupsAreCredentialsValidForName(common_name, credentials))
      {
        // The common name does not match the issued certificate...
        _cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, _("New credentials are not valid for name."), 1);

        trust = HTTP_TRUST_INVALID;
      }
      else if (cupsGetCredentialsExpiration(tcreds) < time(NULL))
      {
        // Save the renewed credentials...
	trust = HTTP_TRUST_RENEWED;

        cupsSaveCredentials(path, common_name, credentials, /*key*/NULL);
      }
    }

    free(tcreds);
  }
  else if ((cg->validate_certs || require_ca) && !cupsAreCredentialsValidForName(common_name, credentials))
  {
    _cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, _("No stored credentials, not valid for name."), 1);
    trust = HTTP_TRUST_INVALID;
  }
  else if (num_certs > 1)
  {
    if (!http_check_roots(credentials))
    {
      // See if we have a site CA certificate we can compare...
      if ((tcreds = cupsCopyCredentials(path, "_site_")) != NULL)
      {
	size_t	credslen,		// Length of credentials
		  tcredslen;		// Length of trust root


	// Do a tail comparison of the root...
	credslen  = strlen(credentials);
	tcredslen = strlen(tcreds);
	if (credslen <= tcredslen || strcmp(credentials + (credslen - tcredslen), tcreds))
	{
	  // Certificate isn't directly generated from the CA cert...
	  trust = HTTP_TRUST_INVALID;
	}

	if (trust != HTTP_TRUST_OK)
	  _cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, _("Credentials do not validate against site CA certificate."), 1);

	free(tcreds);
      }
    }
  }
  else if (require_ca)
  {
    _cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, _("Credentials are not CA-signed."), 1);
    trust = HTTP_TRUST_INVALID;
  }
  else if (!cg->trust_first)
  {
    _cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, _("Trust on first use is disabled."), 1);
    trust = HTTP_TRUST_INVALID;
  }
  else if (!cg->any_root || require_ca)
  {
    _cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, _("Self-signed credentials are blocked."), 1);
    trust = HTTP_TRUST_INVALID;
  }

  if (trust == HTTP_TRUST_OK && !cg->expired_certs)
  {
    time_t	curtime;		// Current date/time

    time(&curtime);
    if (curtime < gnutls_x509_crt_get_activation_time(certs[0]) || curtime > gnutls_x509_crt_get_expiration_time(certs[0]))
    {
      _cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, _("Credentials have expired."), 1);
      trust = HTTP_TRUST_EXPIRED;
    }
  }

  gnutls_free_certs(num_certs, certs);

  return (trust);
}


//
// 'cupsSignCredentialsRequest()' - Sign an X.509 certificate signing request to produce an X.509 certificate chain.
//
// This function creates an X.509 certificate from a signing request.  The
// certificate is stored in the directory "path" or, if "path" is `NULL`, in a
// per-user or system-wide (when running as root) certificate/key store.  The
// generated certificate is signed by the named root certificate or, if
// "root_name" is `NULL`, a site-wide default root certificate.  When
// "root_name" is `NULL` and there is no site-wide default root certificate, a
// self-signed certificate is generated instead.
//
// The "allowed_purpose" argument specifies the allowed purpose(s) used for the
// credentials as a bitwise OR of the following constants:
//
// - `CUPS_CREDPURPOSE_SERVER_AUTH` for validating TLS servers,
// - `CUPS_CREDPURPOSE_CLIENT_AUTH` for validating TLS clients,
// - `CUPS_CREDPURPOSE_CODE_SIGNING` for validating compiled code,
// - `CUPS_CREDPURPOSE_EMAIL_PROTECTION` for validating email messages,
// - `CUPS_CREDPURPOSE_TIME_STAMPING` for signing timestamps to objects, and/or
// - `CUPS_CREDPURPOSE_OCSP_SIGNING` for Online Certificate Status Protocol
//   message signing.
//
// The "allowed_usage" argument specifies the allowed usage(s) for the
// credentials as a bitwise OR of the following constants:
//
// - `CUPS_CREDUSAGE_DIGITAL_SIGNATURE`: digital signatures,
// - `CUPS_CREDUSAGE_NON_REPUDIATION`: non-repudiation/content commitment,
// - `CUPS_CREDUSAGE_KEY_ENCIPHERMENT`: key encipherment,
// - `CUPS_CREDUSAGE_DATA_ENCIPHERMENT`: data encipherment,
// - `CUPS_CREDUSAGE_KEY_AGREEMENT`: key agreement,
// - `CUPS_CREDUSAGE_KEY_CERT_SIGN`: key certicate signing,
// - `CUPS_CREDUSAGE_CRL_SIGN`: certificate revocation list signing,
// - `CUPS_CREDUSAGE_ENCIPHER_ONLY`: encipherment only,
// - `CUPS_CREDUSAGE_DECIPHER_ONLY`: decipherment only,
// - `CUPS_CREDUSAGE_DEFAULT_CA`: defaults for CA certificates,
// - `CUPS_CREDUSAGE_DEFAULT_TLS`: defaults for TLS certificates, and/or
// - `CUPS_CREDUSAGE_ALL`: all usages.
//
// The "cb" and "cb_data" arguments specify a function and its data that are
// used to validate any subjectAltName values in the signing request:
//
// ```
// bool san_cb(const char *common_name, const char *alt_name, void *cb_data) {
//   ... return true if OK and false if not ...
// }
// ```
//
// If `NULL`, a default validation function is used that allows "localhost" and
// variations of the common name.
//
// The "expiration_date" argument specifies the expiration date and time as a
// Unix `time_t` value in seconds.
//

bool					// O - `true` on success, `false` on failure
cupsSignCredentialsRequest(
    const char         *path,		// I - Directory path for certificate/key store or `NULL` for default
    const char         *common_name,	// I - Common name to use
    const char         *request,	// I - PEM-encoded CSR
    const char         *root_name,	// I - Root certificate
    cups_credpurpose_t allowed_purpose,	// I - Allowed credential purpose(s)
    cups_credusage_t   allowed_usage,	// I - Allowed credential usage(s)
    cups_cert_san_cb_t cb,		// I - subjectAltName callback or `NULL` to allow just .local
    void               *cb_data,	// I - Callback data
    time_t             expiration_date)	// I - Certificate expiration date
{
  bool			ret = false;	// Return value
  int			i,		// Looping var
			err;		// GNU TLS error code, if any
  gnutls_x509_crq_t	crq = NULL;	// Certificate request
  gnutls_x509_crt_t	crt = NULL;	// Certificate
  gnutls_x509_crt_t	root_crt = NULL;// Root certificate
  gnutls_x509_privkey_t	root_key = NULL;// Root private key
  gnutls_datum_t	datum;		// Datum
  char			defpath[1024],	// Default path
			temp[1024],	// Temporary string
 			crtfile[1024],	// Certificate filename
 			*root_crtdata,	// Root certificate data
			*root_keydata;	// Root private key data
  size_t		tempsize;	// Size of temporary string
  cups_credpurpose_t	purpose;	// Credential purpose(s)
  unsigned		gnutls_usage;	// GNU TLS keyUsage bits
  cups_credusage_t	usage;		// Credential usage(s)
  cups_file_t		*fp;		// Key/cert file
  unsigned char		buffer[65536];	// Buffer for x509 data
  size_t		bytes;		// Number of bytes of data
  unsigned char		serial[8];	// Serial number buffer
  time_t		curtime;	// Current time


  DEBUG_printf("cupsSignCredentialsRequest(path=\"%s\", common_name=\"%s\", request=\"%s\", root_name=\"%s\", allowed_purpose=0x%x, allowed_usage=0x%x, cb=%p, cb_data=%p, expiration_date=%ld)", path, common_name, request, root_name, allowed_purpose, allowed_usage, cb, cb_data, (long)expiration_date);

  // Filenames...
  if (!path)
    path = http_default_path(defpath, sizeof(defpath));

  if (!path || !common_name || !request)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    goto done;
  }

  if (!cb)
    cb = http_default_san_cb;

  // Import the request...
  gnutls_x509_crq_init(&crq);

  datum.data = (unsigned char *)request;
  datum.size = strlen(request);

  if ((err = gnutls_x509_crq_import(crq, &datum, GNUTLS_X509_FMT_PEM)) < 0)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, gnutls_strerror(err), 0);
    goto done;
  }

  // Create the certificate...
  DEBUG_puts("1cupsSignCredentialsRequest: Generating X.509 certificate.");

  curtime   = time(NULL);
  serial[0] = (unsigned char)(curtime >> 56);
  serial[1] = (unsigned char)(curtime >> 48);
  serial[2] = (unsigned char)(curtime >> 40);
  serial[3] = (unsigned char)(curtime >> 32);
  serial[4] = (unsigned char)(curtime >> 24);
  serial[5] = (unsigned char)(curtime >> 16);
  serial[6] = (unsigned char)(curtime >> 8);
  serial[7] = (unsigned char)(curtime);

  gnutls_x509_crt_init(&crt);
  gnutls_x509_crt_set_crq(crt, crq);

#if 0
  tempsize = sizeof(temp) - 1;
  if (gnutls_x509_crq_get_dn_by_oid(crq, GNUTLS_OID_X520_COUNTRY_NAME, 0, 0, temp, &tempsize) >= 0)
    temp[tempsize] = '\0';
  else
    cupsCopyString(temp, "US", sizeof(temp));
  gnutls_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_X520_COUNTRY_NAME, 0, temp, (unsigned)strlen(temp));

  gnutls_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_X520_COMMON_NAME, 0, common_name, strlen(common_name));

  tempsize = sizeof(temp) - 1;
  if (gnutls_x509_crq_get_dn_by_oid(crq, GNUTLS_OID_X520_ORGANIZATION_NAME, 0, 0, temp, &tempsize) >= 0)
  {
    temp[tempsize] = '\0';
    gnutls_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_X520_ORGANIZATION_NAME, 0, temp, (unsigned)strlen(temp));
  }

  tempsize = sizeof(temp) - 1;
  if (gnutls_x509_crq_get_dn_by_oid(crq, GNUTLS_OID_X520_ORGANIZATIONAL_UNIT_NAME, 0, 0, temp, &tempsize) >= 0)
  {
    temp[tempsize] = '\0';
    gnutls_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_X520_ORGANIZATIONAL_UNIT_NAME, 0, temp, (unsigned)strlen(temp));
  }

  tempsize = sizeof(temp) - 1;
  if (gnutls_x509_crq_get_dn_by_oid(crq, GNUTLS_OID_X520_STATE_OR_PROVINCE_NAME, 0, 0, temp, &tempsize) >= 0)
    temp[tempsize] = '\0';
  else
    cupsCopyString(temp, "Unknown", sizeof(temp));
  gnutls_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_X520_STATE_OR_PROVINCE_NAME, 0, temp, strlen(temp));

  tempsize = sizeof(temp) - 1;
  if (gnutls_x509_crq_get_dn_by_oid(crq, GNUTLS_OID_X520_LOCALITY_NAME, 0, 0, temp, &tempsize) >= 0)
    temp[tempsize] = '\0';
  else
    cupsCopyString(temp, "Unknown", sizeof(temp));
  gnutls_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_X520_LOCALITY_NAME, 0, temp, strlen(temp));
#endif // 0

  gnutls_x509_crt_set_serial(crt, serial, sizeof(serial));
  gnutls_x509_crt_set_activation_time(crt, curtime);
  gnutls_x509_crt_set_expiration_time(crt, expiration_date);
  gnutls_x509_crt_set_ca_status(crt, 0);

  for (i = 0; i < 100; i ++)
  {
    unsigned type;			// Name type

    tempsize = sizeof(temp) - 1;
    if (gnutls_x509_crq_get_subject_alt_name(crq, i, temp, &tempsize, &type, NULL) < 0)
      break;

    temp[tempsize] = '\0';

    DEBUG_printf("1cupsSignCredentialsRequest: SAN %s", temp);

    if (type != GNUTLS_SAN_DNSNAME || (cb)(common_name, temp, cb_data))
    {
      // Good subjectAltName
//      gnutls_x509_crt_set_subject_alt_name(crt, type, temp, (unsigned)strlen(temp), i ? GNUTLS_FSAN_APPEND : GNUTLS_FSAN_SET);
    }
    else
    {
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Validation of subjectAltName in X.509 certificate request failed."), 1);
      goto done;
    }
  }

  for (purpose = 0, i = 0; i < 100; i ++)
  {
    tempsize = sizeof(temp) - 1;
    if (gnutls_x509_crq_get_key_purpose_oid(crq, i, temp, &tempsize, NULL) < 0)
      break;
    temp[tempsize] = '\0';

    if (!strcmp(temp, GNUTLS_KP_TLS_WWW_SERVER))
      purpose |= CUPS_CREDPURPOSE_SERVER_AUTH;
    if (!strcmp(temp, GNUTLS_KP_TLS_WWW_CLIENT))
      purpose |= CUPS_CREDPURPOSE_CLIENT_AUTH;
    if (!strcmp(temp, GNUTLS_KP_CODE_SIGNING))
      purpose |= CUPS_CREDPURPOSE_CODE_SIGNING;
    if (!strcmp(temp, GNUTLS_KP_EMAIL_PROTECTION))
      purpose |= CUPS_CREDPURPOSE_EMAIL_PROTECTION;
    if (!strcmp(temp, GNUTLS_KP_OCSP_SIGNING))
      purpose |= CUPS_CREDPURPOSE_OCSP_SIGNING;
  }
  DEBUG_printf("1cupsSignCredentialsRequest: purpose=0x%04x", purpose);

  if (purpose & ~allowed_purpose)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad keyUsage extension in X.509 certificate request."), 1);
    goto done;
  }

#if 0
  if (purpose == 0 || (purpose & CUPS_CREDPURPOSE_SERVER_AUTH))
    gnutls_x509_crt_set_key_purpose_oid(crt, GNUTLS_KP_TLS_WWW_SERVER, 0);
  if (purpose & CUPS_CREDPURPOSE_CLIENT_AUTH)
    gnutls_x509_crt_set_key_purpose_oid(crt, GNUTLS_KP_TLS_WWW_CLIENT, 0);
  if (purpose & CUPS_CREDPURPOSE_CODE_SIGNING)
    gnutls_x509_crt_set_key_purpose_oid(crt, GNUTLS_KP_CODE_SIGNING, 0);
  if (purpose & CUPS_CREDPURPOSE_EMAIL_PROTECTION)
    gnutls_x509_crt_set_key_purpose_oid(crt, GNUTLS_KP_EMAIL_PROTECTION, 0);
  if (purpose & CUPS_CREDPURPOSE_OCSP_SIGNING)
    gnutls_x509_crt_set_key_purpose_oid(crt, GNUTLS_KP_OCSP_SIGNING, 0);
#endif // 0

  if (gnutls_x509_crq_get_key_usage(crq, &gnutls_usage, NULL) < 0)
  {
    // No keyUsage, use default for TLS...
    gnutls_usage = GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT;
  }
  else
  {
    // Got keyUsage, convert to CUPS bitfield
    usage = 0;
    if (gnutls_usage & GNUTLS_KEY_DIGITAL_SIGNATURE)
      usage |= CUPS_CREDUSAGE_DIGITAL_SIGNATURE;
    if (gnutls_usage & GNUTLS_KEY_NON_REPUDIATION)
      usage |= CUPS_CREDUSAGE_NON_REPUDIATION;
    if (gnutls_usage & GNUTLS_KEY_KEY_ENCIPHERMENT)
      usage |= CUPS_CREDUSAGE_KEY_ENCIPHERMENT;
    if (gnutls_usage & GNUTLS_KEY_DATA_ENCIPHERMENT)
      usage |= CUPS_CREDUSAGE_DATA_ENCIPHERMENT;
    if (gnutls_usage & GNUTLS_KEY_KEY_AGREEMENT)
      usage |= CUPS_CREDUSAGE_KEY_AGREEMENT;
    if (gnutls_usage & GNUTLS_KEY_KEY_CERT_SIGN)
      usage |= CUPS_CREDUSAGE_KEY_CERT_SIGN;
    if (gnutls_usage & GNUTLS_KEY_CRL_SIGN)
      usage |= CUPS_CREDUSAGE_CRL_SIGN;
    if (gnutls_usage & GNUTLS_KEY_ENCIPHER_ONLY)
      usage |= CUPS_CREDUSAGE_ENCIPHER_ONLY;
    if (gnutls_usage & GNUTLS_KEY_DECIPHER_ONLY)
      usage |= CUPS_CREDUSAGE_DECIPHER_ONLY;

    DEBUG_printf("1cupsSignCredentialsRequest: usage=0x%04x", usage);

    if (usage & ~allowed_usage)
    {
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad extKeyUsage extension in X.509 certificate request."), 1);
      goto done;
    }
  }
//  gnutls_x509_crt_set_key_usage(crt, gnutls_usage);

  gnutls_x509_crt_set_version(crt, 3);

  bytes = sizeof(buffer);
  if (gnutls_x509_crt_get_key_id(crt, 0, buffer, &bytes) >= 0)
    gnutls_x509_crt_set_subject_key_id(crt, buffer, bytes);

  // Try loading a root certificate...
  root_crtdata = cupsCopyCredentials(path, root_name ? root_name : "_site_");
  root_keydata = cupsCopyCredentialsKey(path, root_name ? root_name : "_site_");

  if (root_crtdata && root_keydata)
  {
    // Load root certificate...
    datum.data = (unsigned char *)root_crtdata;
    datum.size = strlen(root_crtdata);

    gnutls_x509_crt_init(&root_crt);
    if (gnutls_x509_crt_import(root_crt, &datum, GNUTLS_X509_FMT_PEM) < 0)
    {
      // No good, clear it...
      gnutls_x509_crt_deinit(root_crt);
      root_crt = NULL;
    }
    else
    {
      // Load root private key...
      datum.data = (unsigned char *)root_keydata;
      datum.size = strlen(root_keydata);

      gnutls_x509_privkey_init(&root_key);
      if (gnutls_x509_privkey_import(root_key, &datum, GNUTLS_X509_FMT_PEM) < 0)
      {
        // No food, clear them...
        gnutls_x509_privkey_deinit(root_key);
        root_key = NULL;

        gnutls_x509_crt_deinit(root_crt);
        root_crt = NULL;
      }
    }
  }

  free(root_crtdata);
  free(root_keydata);

  if (!root_crt || !root_key)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to load X.509 CA certificate and private key."), 1);
    goto done;
  }

  gnutls_x509_crt_sign(crt, root_crt, root_key);

  // Save it...
  http_make_path(crtfile, sizeof(crtfile), path, common_name, "crt");

  bytes = sizeof(buffer);
  if ((err = gnutls_x509_crt_export(crt, GNUTLS_X509_FMT_PEM, buffer, &bytes)) < 0)
  {
    DEBUG_printf("1cupsSignCredentialsRequest: Unable to export public key and X.509 certificate: %s", gnutls_strerror(err));
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, gnutls_strerror(err), 0);
    goto done;
  }
  else if ((fp = cupsFileOpen(crtfile, "w")) != NULL)
  {
    DEBUG_printf("1cupsSignCredentialsRequest: Writing public key and X.509 certificate to \"%s\".", crtfile);
    cupsFileWrite(fp, (char *)buffer, bytes);
    cupsFileClose(fp);
  }
  else
  {
    DEBUG_printf("1cupsSignCredentialsRequest: Unable to create public key and X.509 certificate file \"%s\": %s", crtfile, strerror(errno));
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    goto done;
  }

  DEBUG_puts("1cupsSignCredentialsRequest: Successfully created credentials.");

  ret = true;

  // Cleanup...
  done:

  if (crq)
    gnutls_x509_crq_deinit(crq);
  if (crt)
    gnutls_x509_crt_deinit(crt);
  if (root_crt)
    gnutls_x509_crt_deinit(root_crt);
  if (root_key)
    gnutls_x509_privkey_deinit(root_key);

  return (ret);
}


//
// 'httpCopyPeerCredentials()' - Copy the credentials associated with the peer in an encrypted connection.
//

char *					// O - Credentials string
httpCopyPeerCredentials(http_t *http)	// I - HTTP connection
{
  char		*credentials = NULL;	// Return value
  size_t	alloc_creds = 0;	// Allocated size
  unsigned	count;			// Number of certificates
  const gnutls_datum_t *certs;		// Certificates


  DEBUG_printf("httpCopyPeerCredentials(http=%p)", http);

  if (http && http->tls)
  {
    // Get the list of peer certificates...
    certs = gnutls_certificate_get_peers(http->tls, &count);

    DEBUG_printf("1httpCopyPeerCredentials: certs=%p, count=%u", certs, count);

    if (certs && count)
    {
      // Add them to the credentials string...
      while (count > 0)
      {
	// Expand credentials string...
	char *pem = http_der_to_pem(certs->data, certs->size);
					// PEM-encoded certificate
	size_t	pemsize;		// Length of PEM-encoded certificate

	if (pem && (credentials = realloc(credentials, alloc_creds + (pemsize = strlen(pem)) + 1)) != NULL)
	{
	  // Copy PEM-encoded data...
	  memcpy(credentials + alloc_creds, pem, pemsize);
	  credentials[alloc_creds + pemsize] = '\0';
	  alloc_creds += pemsize;
	}

        free(pem);

        certs ++;
        count --;
      }
    }
  }

  DEBUG_printf("1httpCopyPeerCredentials: Returning %p.", credentials);

  return (credentials);
}


//
// '_httpCreateCredentials()' - Create credentials in the internal format.
//

_http_tls_credentials_t *		// O - Internal credentials
_httpCreateCredentials(
    const char *credentials,		// I - Credentials string
    const char *key)			// I - Private key string
{
  int			err;		// Result from GNU TLS
  _http_tls_credentials_t *hcreds;	// Credentials
  gnutls_datum_t	cdatum,		// Credentials record
			kdatum;		// Key record


  DEBUG_printf("_httpCreateCredentials(credentials=\"%s\", key=\"%s\")", credentials, key);

  if ((hcreds = calloc(1, sizeof(_http_tls_credentials_t))) == NULL)
    return (NULL);

  if ((err = gnutls_certificate_allocate_credentials(&hcreds->creds)) < 0)
  {
    DEBUG_printf("1_httpCreateCredentials: allocate_credentials error: %s", gnutls_strerror(err));
    free(hcreds);
    return (NULL);
  }

  hcreds->use  = 1;

  if (credentials && *credentials && key && *key)
  {
    cdatum.data = (void *)credentials;
    cdatum.size = strlen(credentials);
    kdatum.data = (void *)key;
    kdatum.size = strlen(key);

    if ((err = gnutls_certificate_set_x509_key_mem(hcreds->creds, &cdatum, &kdatum, GNUTLS_X509_FMT_PEM)) < 0)
    {
      DEBUG_printf("1_httpCreateCredentials: set_x509_key_mem error: %s", gnutls_strerror(err));

      gnutls_certificate_free_credentials(hcreds->creds);
      free(hcreds);
      hcreds = NULL;
    }
  }

  DEBUG_printf("1_httpCreateCredentials: Returning %p.", hcreds);

  return (hcreds);
}


//
// '_httpFreeCredentials()' - Free internal credentials.
//

void
_httpFreeCredentials(
    _http_tls_credentials_t *hcreds)	// I - Internal credentials
{
  if (!hcreds)
    return;

  if (hcreds->use)
    hcreds->use --;

  if (hcreds->use)
    return;

  gnutls_certificate_free_credentials(hcreds->creds);
  free(hcreds);
}


//
// '_httpTLSInitialize()' - Initialize the TLS stack.
//

void
_httpTLSInitialize(void)
{
  // Initialize GNU TLS...
  gnutls_global_init();
}


//
// '_httpTLSPending()' - Return the number of pending TLS-encrypted bytes.
//

size_t					// O - Bytes available
_httpTLSPending(http_t *http)		// I - HTTP connection
{
  return (gnutls_record_check_pending(http->tls));
}


//
// '_httpTLSRead()' - Read from a SSL/TLS connection.
//

int					// O - Bytes read
_httpTLSRead(http_t *http,		// I - Connection to server
	     char   *buf,		// I - Buffer to store data
	     int    len)		// I - Length of buffer
{
  ssize_t	result;			// Return value


  result = gnutls_record_recv(http->tls, buf, (size_t)len);

  if (result < 0 && !errno)
  {
    // Convert GNU TLS error to errno value...
    switch (result)
    {
      case GNUTLS_E_INTERRUPTED :
	  errno = EINTR;
	  break;

      case GNUTLS_E_AGAIN :
          errno = EAGAIN;
          break;

      default :
          errno = EPIPE;
          break;
    }

    result = -1;
  }

  return ((int)result);
}


//
// '_httpTLSStart()' - Set up SSL/TLS support on a connection.
//

bool					// O - `true` on success, `false` on failure
_httpTLSStart(http_t *http)		// I - Connection to server
{
  const char		*keypath;	// Certificate store path
  char			hostname[256],	// Hostname
			*hostptr;	// Pointer into hostname
  int			status;		// Status of handshake
  _http_tls_credentials_t *credentials = NULL;
					// TLS credentials
  char			priority_string[2048];
					// Priority string
  int			version;	// Current version
  double		old_timeout;	// Old timeout value
  http_timeout_cb_t	old_cb;		// Old timeout callback
  void			*old_data;	// Old timeout data
  _cups_globals_t	*cg = _cupsGlobals();
					// Per-thread globals
  static const char * const versions[] =// SSL/TLS versions
  {
    "VERS-SSL3.0",
    "VERS-TLS1.0",
    "VERS-TLS1.1",
    "VERS-TLS1.2",
    "VERS-TLS1.3",
    "VERS-TLS-ALL"
  };


  DEBUG_printf("3_httpTLSStart(http=%p)", http);

  if (tls_options < 0)
  {
    DEBUG_puts("4_httpTLSStart: Setting defaults.");
    _cupsSetDefaults();
    DEBUG_printf("4_httpTLSStart: tls_options=%x", tls_options);
  }

  cupsMutexLock(&tls_mutex);
  keypath = tls_keypath;
  cupsMutexUnlock(&tls_mutex);

  if (http->mode == _HTTP_MODE_SERVER && !keypath)
  {
    DEBUG_puts("4_httpTLSStart: cupsSetServerCredentials not called.");
    http->error  = errno = EINVAL;
    http->status = HTTP_STATUS_ERROR;
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Server credentials not set."), 1);

    return (false);
  }

  status = gnutls_init(&http->tls, http->mode == _HTTP_MODE_CLIENT ? GNUTLS_CLIENT : GNUTLS_SERVER);
  if (!status)
    status = gnutls_set_default_priority(http->tls);

  if (status)
  {
    http->error  = EIO;
    http->status = HTTP_STATUS_ERROR;

    DEBUG_printf("4_httpTLSStart: Unable to initialize common TLS parameters: %s", gnutls_strerror(status));
    _cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, gnutls_strerror(status), 0);

    gnutls_deinit(http->tls);
    http->tls = NULL;

    return (false);
  }

  if (http->mode == _HTTP_MODE_CLIENT)
  {
    // Client: get the hostname to use for TLS...
    if (httpAddrIsLocalhost(http->hostaddr))
    {
      cupsCopyString(hostname, "localhost", sizeof(hostname));
    }
    else
    {
      // Otherwise make sure the hostname we have does not end in a trailing dot.
      cupsCopyString(hostname, http->hostname, sizeof(hostname));
      if ((hostptr = hostname + strlen(hostname) - 1) >= hostname && *hostptr == '.')
	*hostptr = '\0';
    }

    status = gnutls_server_name_set(http->tls, GNUTLS_NAME_DNS, hostname, strlen(hostname));
    if (!status && (credentials = _httpUseCredentials(cg->tls_credentials)) == NULL)
    {
      if ((credentials = _httpCreateCredentials(NULL, NULL)) == NULL)
        status = -1;
    }
  }
  else
  {
    // Server: get certificate and private key...
    char	crtfile[1024],		// Certificate file
		keyfile[1024];		// Private key file
    const char	*cn,			// Common name to lookup
		*cnptr;			// Pointer into common name
    bool	have_creds = false;	// Have credentials?

    if (http->fields[HTTP_FIELD_HOST])
    {
      // Use hostname for TLS upgrade...
      cupsCopyString(hostname, http->fields[HTTP_FIELD_HOST], sizeof(hostname));
    }
    else
    {
      // Resolve hostname from connection address...
      http_addr_t	addr;		// Connection address
      socklen_t		addrlen;	// Length of address

      addrlen = sizeof(addr);
      if (getsockname(http->fd, (struct sockaddr *)&addr, &addrlen))
      {
	DEBUG_printf("4_httpTLSStart: Unable to get socket address: %s", strerror(errno));
	hostname[0] = '\0';
      }
      else if (httpAddrIsLocalhost(&addr))
      {
	hostname[0] = '\0';
      }
      else
      {
	httpAddrLookup(&addr, hostname, sizeof(hostname));
        DEBUG_printf("4_httpTLSStart: Resolved socket address to \"%s\".", hostname);
      }
    }

    if (isdigit(hostname[0] & 255) || hostname[0] == '[')
      hostname[0] = '\0';		// Don't allow numeric addresses

    cupsMutexLock(&tls_mutex);

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
          cupsCopyString(crtfile, cacrtfile, sizeof(crtfile));
          cupsCopyString(keyfile, cakeyfile, sizeof(keyfile));
        }
      }

      have_creds = !access(crtfile, R_OK) && !access(keyfile, R_OK);
    }

    if (!have_creds && tls_auto_create && cn)
    {
      DEBUG_printf("4_httpTLSStart: Auto-create credentials for \"%s\".", cn);

      if (!cupsCreateCredentials(tls_keypath, false, CUPS_CREDPURPOSE_SERVER_AUTH, CUPS_CREDTYPE_DEFAULT, CUPS_CREDUSAGE_DEFAULT_TLS, NULL, NULL, NULL, NULL, NULL, cn, /*email*/NULL, 0, NULL, NULL, time(NULL) + 3650 * 86400))
      {
	DEBUG_puts("4_httpTLSStart: cupsCreateCredentials failed.");
	http->error  = errno = EINVAL;
	http->status = HTTP_STATUS_ERROR;
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to create server credentials."), 1);
	cupsMutexUnlock(&tls_mutex);

	return (false);
      }
    }

    cupsMutexUnlock(&tls_mutex);

    DEBUG_printf("4_httpTLSStart: Using certificate \"%s\" and private key \"%s\".", crtfile, keyfile);

    if ((credentials = calloc(1, sizeof(_http_tls_credentials_t))) == NULL)
    {
      DEBUG_puts("4_httpTLSStart: cupsCreateCredentials failed.");
      http->error  = errno = EINVAL;
      http->status = HTTP_STATUS_ERROR;
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to create server credentials."), 1);
      cupsMutexUnlock(&tls_mutex);
      return (false);
    }

    credentials->use = 1;
    if ((status = gnutls_certificate_allocate_credentials(&credentials->creds)) >= 0)
      status = gnutls_certificate_set_x509_key_file(credentials->creds, crtfile, keyfile, GNUTLS_X509_FMT_PEM);
  }

  if (!status && credentials)
    status = gnutls_credentials_set(http->tls, GNUTLS_CRD_CERTIFICATE, credentials->creds);

  if (status)
  {
    http->error  = EIO;
    http->status = HTTP_STATUS_ERROR;

    DEBUG_printf("4_httpTLSStart: Unable to complete client/server setup: %s", gnutls_strerror(status));
    _cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, gnutls_strerror(status), 0);

    gnutls_deinit(http->tls);
    _httpFreeCredentials(credentials);
    http->tls = NULL;

    return (false);
  }

  cupsCopyString(priority_string, "NORMAL", sizeof(priority_string));

  if (tls_max_version < _HTTP_TLS_MAX)
  {
    // Require specific TLS versions...
    cupsConcatString(priority_string, ":-VERS-TLS-ALL", sizeof(priority_string));
    for (version = tls_min_version; version <= tls_max_version; version ++)
    {
      cupsConcatString(priority_string, ":+", sizeof(priority_string));
      cupsConcatString(priority_string, versions[version], sizeof(priority_string));
    }
  }
  else if (tls_min_version == _HTTP_TLS_SSL3)
  {
    // Allow all versions of TLS and SSL/3.0...
    cupsConcatString(priority_string, ":+VERS-TLS-ALL:+VERS-SSL3.0", sizeof(priority_string));
  }
  else
  {
    // Require a minimum version...
    cupsConcatString(priority_string, ":+VERS-TLS-ALL", sizeof(priority_string));
    for (version = 0; version < tls_min_version; version ++)
    {
      cupsConcatString(priority_string, ":-", sizeof(priority_string));
      cupsConcatString(priority_string, versions[version], sizeof(priority_string));
    }
  }

  if (tls_options & _HTTP_TLS_ALLOW_RC4)
    cupsConcatString(priority_string, ":+ARCFOUR-128", sizeof(priority_string));
  else
    cupsConcatString(priority_string, ":!ARCFOUR-128", sizeof(priority_string));

  cupsConcatString(priority_string, ":!ANON-DH", sizeof(priority_string));

  if (tls_options & _HTTP_TLS_DENY_CBC)
    cupsConcatString(priority_string, ":!AES-128-CBC:!AES-256-CBC:!CAMELLIA-128-CBC:!CAMELLIA-256-CBC:!3DES-CBC", sizeof(priority_string));

#ifdef HAVE_GNUTLS_PRIORITY_SET_DIRECT
  gnutls_priority_set_direct(http->tls, priority_string, NULL);

#else
  gnutls_priority_t priority;		// Priority

  gnutls_priority_init(&priority, priority_string, NULL);
  gnutls_priority_set(http->tls, priority);
  gnutls_priority_deinit(priority);
#endif // HAVE_GNUTLS_PRIORITY_SET_DIRECT

  gnutls_transport_set_ptr(http->tls, (gnutls_transport_ptr_t)http);
  gnutls_transport_set_pull_function(http->tls, gnutls_http_read);
#ifdef HAVE_GNUTLS_TRANSPORT_SET_PULL_TIMEOUT_FUNCTION
  gnutls_transport_set_pull_timeout_function(http->tls, (gnutls_pull_timeout_func)httpWait);
#endif // HAVE_GNUTLS_TRANSPORT_SET_PULL_TIMEOUT_FUNCTION
  gnutls_transport_set_push_function(http->tls, gnutls_http_write);

  // Enforce a minimum timeout of 10 seconds for the TLS handshake...
  old_timeout  = http->timeout_value;
  old_cb       = http->timeout_cb;
  old_data     = http->timeout_data;

  if (!old_cb || old_timeout < 10.0)
  {
    DEBUG_puts("4_httpTLSStart: Setting timeout to 10 seconds.");
    httpSetTimeout(http, 10.0, NULL, NULL);
  }

  // Do the TLS handshake...
  while ((status = gnutls_handshake(http->tls)) != GNUTLS_E_SUCCESS)
  {
    DEBUG_printf("5_httpStartTLS: gnutls_handshake returned %d (%s)", status, gnutls_strerror(status));

    if (gnutls_error_is_fatal(status))
    {
      http->error  = EIO;
      http->status = HTTP_STATUS_ERROR;

      _cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, gnutls_strerror(status), 0);

      gnutls_deinit(http->tls);
      _httpFreeCredentials(credentials);
      http->tls = NULL;

      httpSetTimeout(http, old_timeout, old_cb, old_data);

      return (false);
    }
  }

  // Restore the previous timeout settings...
  httpSetTimeout(http, old_timeout, old_cb, old_data);

  http->tls_credentials = credentials;

  return (true);
}


//
// '_httpTLSStop()' - Shut down SSL/TLS on a connection.
//

void
_httpTLSStop(http_t *http)		// I - Connection to server
{
  int	error;				// Error code


  error = gnutls_bye(http->tls, http->mode == _HTTP_MODE_CLIENT ? GNUTLS_SHUT_RDWR : GNUTLS_SHUT_WR);
  if (error != GNUTLS_E_SUCCESS)
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, gnutls_strerror(errno), 0);

  gnutls_deinit(http->tls);
  http->tls = NULL;

  if (http->tls_credentials)
  {
    _httpFreeCredentials(http->tls_credentials);
    http->tls_credentials = NULL;
  }
}


//
// '_httpTLSWrite()' - Write to a SSL/TLS connection.
//

int					// O - Bytes written
_httpTLSWrite(http_t     *http,		// I - Connection to server
	      const char *buf,		// I - Buffer holding data
	      int        len)		// I - Length of buffer
{
  ssize_t	result;			// Return value


  DEBUG_printf("5_httpTLSWrite(http=%p, buf=%p, len=%d)", http, buf, len);

  result = gnutls_record_send(http->tls, buf, (size_t)len);

  if (result < 0 && !errno)
  {
    // Convert GNU TLS error to errno value...
    switch (result)
    {
      case GNUTLS_E_INTERRUPTED :
	  errno = EINTR;
	  break;

      case GNUTLS_E_AGAIN :
          errno = EAGAIN;
          break;

      default :
          errno = EPIPE;
          break;
    }

    result = -1;
  }

  DEBUG_printf("5_httpTLSWrite: Returning %d.", (int)result);

  return ((int)result);
}


//
// '_httpUseCredentials()' - Increment the use count for internal credentials.
//

_http_tls_credentials_t *		// O - Internal credentials
_httpUseCredentials(
    _http_tls_credentials_t *hcreds)	// I - Internal credentials
{
  if (hcreds)
    hcreds->use ++;

  return (hcreds);
}


//
// 'gnutls_create_key()' - Create a private key.
//

static gnutls_x509_privkey_t		// O - Private key
gnutls_create_key(cups_credtype_t type)	// I - Type of key
{
  gnutls_x509_privkey_t	key;		// Private key


  gnutls_x509_privkey_init(&key);

  switch (type)
  {
    case CUPS_CREDTYPE_ECDSA_P256_SHA256 :
	gnutls_x509_privkey_generate(key, GNUTLS_PK_ECDSA, GNUTLS_CURVE_TO_BITS(GNUTLS_ECC_CURVE_SECP256R1), 0);
	break;

    case CUPS_CREDTYPE_ECDSA_P384_SHA256 :
	gnutls_x509_privkey_generate(key, GNUTLS_PK_ECDSA, GNUTLS_CURVE_TO_BITS(GNUTLS_ECC_CURVE_SECP384R1), 0);
	break;

    case CUPS_CREDTYPE_ECDSA_P521_SHA256 :
	gnutls_x509_privkey_generate(key, GNUTLS_PK_ECDSA, GNUTLS_CURVE_TO_BITS(GNUTLS_ECC_CURVE_SECP521R1), 0);
	break;

    case CUPS_CREDTYPE_RSA_2048_SHA256 :
	gnutls_x509_privkey_generate(key, GNUTLS_PK_RSA, 2048, 0);
	break;

    default :
    case CUPS_CREDTYPE_RSA_3072_SHA256 :
	gnutls_x509_privkey_generate(key, GNUTLS_PK_RSA, 3072, 0);
	break;

    case CUPS_CREDTYPE_RSA_4096_SHA256 :
	gnutls_x509_privkey_generate(key, GNUTLS_PK_RSA, 4096, 0);
	break;
  }

  return (key);
}


//
// 'gnutls_free_certs()' - Free X.509 certificates.
//

static void
gnutls_free_certs(
    unsigned          num_certs,	// I - Number of certificates
    gnutls_x509_crt_t *certs)		// I - Certificates
{
  while (num_certs > 0)
  {
    gnutls_x509_crt_deinit(*certs);
    certs ++;
    num_certs --;
  }
}


//
// 'gnutls_http_read()' - Read function for the GNU TLS library.
//

static ssize_t				// O - Number of bytes read or -1 on error
gnutls_http_read(
    gnutls_transport_ptr_t ptr,		// I - Connection to server
    void                   *data,	// I - Buffer
    size_t                 length)	// I - Number of bytes to read
{
  http_t	*http;			// HTTP connection
  ssize_t	bytes;			// Bytes read


  DEBUG_printf("5gnutls_http_read(ptr=%p, data=%p, length=%d)", ptr, data, (int)length);

  http = (http_t *)ptr;

  if (!http->blocking || http->timeout_value > 0.0)
  {
    // Make sure we have data before we read...
    while (!_httpWait(http, http->wait_value, 0))
    {
      if (http->timeout_cb && (*http->timeout_cb)(http, http->timeout_data))
	continue;

      http->error = ETIMEDOUT;
      return (-1);
    }
  }

  bytes = recv(http->fd, data, length, 0);
  DEBUG_printf("5gnutls_http_read: bytes=%d", (int)bytes);
  return (bytes);
}


//
// 'gnutls_http_write()' - Write function for the GNU TLS library.
//

static ssize_t				// O - Number of bytes written or -1 on error
gnutls_http_write(
    gnutls_transport_ptr_t ptr,		// I - Connection to server
    const void             *data,	// I - Data buffer
    size_t                 length)	// I - Number of bytes to write
{
  ssize_t bytes;			// Bytes written


  DEBUG_printf("5gnutls_http_write(ptr=%p, data=%p, length=%d)", ptr, data, (int)length);
  bytes = send(((http_t *)ptr)->fd, data, length, 0);
  DEBUG_printf("5gnutls_http_write: bytes=%d", (int)bytes);

  return (bytes);
}


//
// 'gnutls_import_certs()' - Import X.509 certificates.
//

static gnutls_x509_crt_t		// O  - X.509 leaf certificate
gnutls_import_certs(
    const char        *credentials,	// I  - Credentials string
    unsigned          *num_certs,	// IO - Number of certificates
    gnutls_x509_crt_t *certs)		// O  - Certificates
{
  int			err;		// Error code, if any
  gnutls_datum_t	datum;		// Data record


  DEBUG_printf("3gnutls_import_certs(credentials=\"%s\", num_certs=%p, certs=%p)", credentials, (void *)num_certs, (void *)certs);

  // Import all certificates from the string...
  datum.data = (void *)credentials;
  datum.size = strlen(credentials);

  if ((err = gnutls_x509_crt_list_import(certs, num_certs, &datum, GNUTLS_X509_FMT_PEM, 0)) < 0)
  {
    DEBUG_printf("4gnutls_import_certs: crt_list_import error: %s", gnutls_strerror(err));
    return (NULL);
  }

  return (certs[0]);
}


//
// 'gnutls_load_crl()' - Load the certificate revocation list, if any.
//

static void
gnutls_load_crl(void)
{
  cupsMutexLock(&tls_mutex);

  if (!gnutls_x509_crl_init(&tls_crl))
  {
    cups_file_t		*fp;		// CRL file
    char		filename[1024],	// site.crl
			line[256];	// Base64-encoded line
    unsigned char	*data = NULL;	// Buffer for cert data
    size_t		alloc_data = 0,	// Bytes allocated
			num_data = 0;	// Bytes used
    size_t		decoded;	// Bytes decoded
    gnutls_datum_t	datum;		// Data record


    http_make_path(filename, sizeof(filename), CUPS_SERVERROOT, "site", "crl");

    if ((fp = cupsFileOpen(filename, "r")) != NULL)
    {
      while (cupsFileGets(fp, line, sizeof(line)))
      {
	if (!strcmp(line, "-----BEGIN X509 CRL-----"))
	{
	  if (num_data)
	  {
	    // Missing END X509 CRL...
	    break;
	  }
	}
	else if (!strcmp(line, "-----END X509 CRL-----"))
	{
	  if (!num_data)
	  {
	    // Missing data...
	    break;
	  }

          datum.data = data;
	  datum.size = num_data;

	  gnutls_x509_crl_import(tls_crl, &datum, GNUTLS_X509_FMT_PEM);

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

	  decoded = (size_t)(alloc_data - num_data);
	  httpDecode64_3((char *)data + num_data, &decoded, line, NULL);
	  num_data += (size_t)decoded;
	}
      }

      cupsFileClose(fp);

      if (data)
	free(data);
    }
  }

  cupsMutexUnlock(&tls_mutex);
}
