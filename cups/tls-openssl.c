//
// TLS support code for CUPS using OpenSSL/LibreSSL.
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

#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/obj_mac.h>


//
// Local functions...
//

static long		http_bio_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int		http_bio_free(BIO *data);
static int		http_bio_new(BIO *h);
static int		http_bio_puts(BIO *h, const char *str);
static int		http_bio_read(BIO *h, char *buf, int size);
static int		http_bio_write(BIO *h, const char *buf, int num);

static bool		openssl_add_ext(STACK_OF(X509_EXTENSION) *exts, int nid, const char *value);
static X509_NAME	*openssl_create_name(const char *organization, const char *org_unit, const char *locality, const char *state_province, const char *country, const char *common_name, const char *email);
static EVP_PKEY		*openssl_create_key(cups_credtype_t type);
static X509_EXTENSION	*openssl_create_san(const char *common_name, size_t num_alt_names, const char * const *alt_names);
static time_t		openssl_get_date(X509 *cert, int which);
//static void		openssl_load_crl(void);
static STACK_OF(X509 *)	openssl_load_x509(const char *credentials);


//
// Local globals...
//

static BIO_METHOD	*tls_bio_method = NULL;
					// OpenSSL BIO method
static const char * const tls_purpose_oids[] =
{					// OIDs for each key purpose value
  "1.3.6.1.5.5.7.3.1",			// serverAuth
  "1.3.6.1.5.5.7.3.2",			// clientAuth
  "1.3.6.1.5.5.7.3.3",			// codeSigning
  "1.3.6.1.5.5.7.3.4",			// emailProtection
  "1.3.6.1.5.5.7.3.8",			// timeStamping
  "1.3.6.1.5.5.7.3.9"			// OCSPSigning
};
static const char * const tls_usage_strings[] =
{					// Strings for each key usage value
  "digitalSignature",
  "nonRepudiation",
  "keyEncipherment",
  "dataEncipherment",
  "keyAgreement",
  "keyCertSign",
  "cRLSign",
  "encipherOnly",
  "decipherOnly"
};


//
// 'cupsAreCredentialsValidForName()' - Return whether the credentials are valid
//                                      for the given name.
//

bool					// O - `true` if valid, `false` otherwise
cupsAreCredentialsValidForName(
    const char *common_name,		// I - Name to check
    const char *credentials)		// I - Credentials
{
  STACK_OF(X509)	*certs;		// Certificate chain
  bool			result = false;	// Result


  if ((certs = openssl_load_x509(credentials)) != NULL)
  {
    result = X509_check_host(sk_X509_value(certs, 0), common_name, strlen(common_name), 0, NULL) != 0;

    sk_X509_free(certs);
  }

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

bool					// O - `true` on success, `false` on failure
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
  bool		result = false;		// Return value
  EVP_PKEY	*pkey;			// Key pair
  X509		*cert;			// Certificate
  X509		*root_cert = NULL;	// Root certificate, if any
  EVP_PKEY	*root_key = NULL;	// Root private key, if any
  char		defpath[1024],		// Default path
 		crtfile[1024],		// Certificate filename
		keyfile[1024],		// Private key filename
		root_crtfile[1024],	// Root certificate filename
		root_keyfile[1024];	// Root private key filename
  time_t	curtime;		// Current time
  X509_NAME	*name;			// Subject/issuer name
  ASN1_INTEGER	*serial;		// Serial number
  ASN1_TIME	*notBefore,		// Initial date
		*notAfter;		// Expiration date
  BIO		*bio;			// Output file
  char		temp[1024],		// Temporary string
		*tempptr;		// Pointer into temporary string
  STACK_OF(X509_EXTENSION) *exts;	// Extensions
  X509_EXTENSION *ext;			// Current extension
  unsigned	i;			// Looping var
  cups_credpurpose_t purpose_bit;	// Current purpose
  cups_credusage_t usage_bit;		// Current usage


  DEBUG_printf("cupsCreateCredentials(path=\"%s\", ca_cert=%s, purpose=0x%x, type=%d, usage=0x%x, organization=\"%s\", org_unit=\"%s\", locality=\"%s\", state_province=\"%s\", country=\"%s\", common_name=\"%s\", num_alt_names=%u, alt_names=%p, root_name=\"%s\", expiration_date=%ld)", path, ca_cert ? "true" : "false", purpose, type, usage, organization, org_unit, locality, state_province, country, common_name, (unsigned)num_alt_names, (void *)alt_names, root_name, (long)expiration_date);

  // Filenames...
  if (!path)
    path = http_default_path(defpath, sizeof(defpath));

  if (!path || !common_name)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (false);
  }

  // Create the encryption key...
  DEBUG_puts("1cupsCreateCredentials: Creating key pair.");

  if ((pkey = openssl_create_key(type)) == NULL)
    return (false);

  DEBUG_puts("1cupsCreateCredentials: Key pair created.");

  // Create the X.509 certificate...
  DEBUG_puts("1cupsCreateCredentials: Generating X.509 certificate.");

  if ((cert = X509_new()) == NULL)
  {
    EVP_PKEY_free(pkey);
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to create X.509 certificate."), 1);
    return (false);
  }

  curtime = time(NULL);

  notBefore = ASN1_TIME_new();
  ASN1_TIME_set(notBefore, curtime);
  X509_set_notBefore(cert, notBefore);
  ASN1_TIME_free(notBefore);

  notAfter  = ASN1_TIME_new();
  ASN1_TIME_set(notAfter, expiration_date);
  X509_set_notAfter(cert, notAfter);
  ASN1_TIME_free(notAfter);

  serial = ASN1_INTEGER_new();
  ASN1_INTEGER_set(serial, (long)curtime);
  X509_set_serialNumber(cert, serial);
  ASN1_INTEGER_free(serial);

  X509_set_pubkey(cert, pkey);

  name = openssl_create_name(organization, org_unit, locality, state_province, country, common_name, email);

  X509_set_subject_name(cert, name);

  // Try loading a root certificate...
  http_make_path(root_crtfile, sizeof(root_crtfile), path, root_name ? root_name : "_site_", "crt");
  http_make_path(root_keyfile, sizeof(root_keyfile), path, root_name ? root_name : "_site_", "key");

  if (!ca_cert && !access(root_crtfile, 0) && !access(root_keyfile, 0))
  {
    if ((bio = BIO_new_file(root_crtfile, "rb")) != NULL)
    {
      PEM_read_bio_X509(bio, &root_cert, /*cb*/NULL, /*u*/NULL);
      BIO_free(bio);

      if ((bio = BIO_new_file(root_keyfile, "rb")) != NULL)
      {
	PEM_read_bio_PrivateKey(bio, &root_key, /*cb*/NULL, /*u*/NULL);
	BIO_free(bio);
      }

      if (!root_key)
      {
        // Only use root certificate if we have the key...
        X509_free(root_cert);
        root_cert = NULL;
      }
    }

    if (!root_cert || !root_key)
    {
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to load X.509 CA certificate and private key."), 1);
      goto done;
    }
  }

  if (root_cert)
    X509_set_issuer_name(cert, X509_get_subject_name(root_cert));
  else
    X509_set_issuer_name(cert, name);

  X509_NAME_free(name);

  exts = sk_X509_EXTENSION_new_null();

  if (ca_cert)
  {
    // Add extensions that are required to make Chrome happy...
    openssl_add_ext(exts, NID_basic_constraints, "critical,CA:TRUE,pathlen:0");
  }
  else
  {
    // Add extension with DNS names and free buffer for GENERAL_NAME
    if ((ext = openssl_create_san(common_name, num_alt_names, alt_names)) == NULL)
      goto done;

    sk_X509_EXTENSION_push(exts, ext);

    // Add extensions that are required to make Chrome happy...
    openssl_add_ext(exts, NID_basic_constraints, "critical,CA:FALSE,pathlen:0");
  }

  cupsCopyString(temp, "critical", sizeof(temp));
  for (tempptr = temp + strlen(temp), i = 0, usage_bit = CUPS_CREDUSAGE_DIGITAL_SIGNATURE; i < (sizeof(tls_usage_strings) / sizeof(tls_usage_strings[0])); i ++, usage_bit *= 2)
  {
    if (!(usage & usage_bit))
      continue;

    snprintf(tempptr, sizeof(temp) - (size_t)(tempptr - temp), ",%s", tls_usage_strings[i]);

    tempptr += strlen(tempptr);
  }
  openssl_add_ext(exts, NID_key_usage, temp);

  temp[0] = '\0';
  for (tempptr = temp, i = 0, purpose_bit = CUPS_CREDPURPOSE_SERVER_AUTH; i < (sizeof(tls_purpose_oids) / sizeof(tls_purpose_oids[0])); i ++, purpose_bit *= 2)
  {
    if (!(purpose & purpose_bit))
      continue;

    if (tempptr == temp)
      cupsCopyString(temp, tls_purpose_oids[i], sizeof(temp));
    else
      snprintf(tempptr, sizeof(temp) - (size_t)(tempptr - temp), ",%s", tls_purpose_oids[i]);

    tempptr += strlen(tempptr);
  }
  openssl_add_ext(exts, NID_ext_key_usage, temp);

  openssl_add_ext(exts, NID_subject_key_identifier, "hash");
  openssl_add_ext(exts, NID_authority_key_identifier, "keyid,issuer");

  while ((ext = sk_X509_EXTENSION_pop(exts)) != NULL)
  {
    if (!X509_add_ext(cert, ext, -1))
    {
      sk_X509_EXTENSION_pop_free(exts, X509_EXTENSION_free);
      goto done;
    }
  }

  X509_set_version(cert, 2); // v3

  if (root_key)
    X509_sign(cert, root_key, EVP_sha256());
  else
    X509_sign(cert, pkey, EVP_sha256());

  // Save them...
  http_make_path(crtfile, sizeof(crtfile), path, common_name, "crt");
  http_make_path(keyfile, sizeof(keyfile), path, common_name, "key");

  if ((bio = BIO_new_file(keyfile, "wb")) == NULL)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    goto done;
  }

  if (!PEM_write_bio_PrivateKey(bio, pkey, NULL, NULL, 0, NULL, NULL))
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to write private key."), 1);
    BIO_free(bio);
    goto done;
  }

  BIO_free(bio);

  if ((bio = BIO_new_file(crtfile, "wb")) == NULL)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    goto done;
  }

  if (!PEM_write_bio_X509(bio, cert))
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to write X.509 certificate."), 1);
    BIO_free(bio);
    goto done;
  }

  if (root_cert)
    PEM_write_bio_X509(bio, root_cert);

  BIO_free(bio);

  result = true;
  DEBUG_puts("1cupsCreateCredentials: Successfully created credentials.");

  // Cleanup...
  done:

  X509_free(cert);
  EVP_PKEY_free(pkey);

  if (root_cert)
    X509_free(root_cert);
  if (root_key)
    EVP_PKEY_free(root_key);

  return (result);
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
  bool		ret = false;		// Return value
  EVP_PKEY	*pkey;			// Key pair
  X509_REQ	*csr;			// Certificate signing request
  X509_NAME	*name;			// Subject/issuer name
  X509_EXTENSION *ext;			// X509 extension
  BIO		*bio;			// Output file
  char		temp[1024],		// Temporary directory name
		*tempptr,		// Pointer into temporary string
 		csrfile[1024],		// Certificate signing request filename
		keyfile[1024];		// Private key filename
  STACK_OF(X509_EXTENSION) *exts;	// Extensions
  unsigned	i;			// Looping var
  cups_credpurpose_t purpose_bit;	// Current purpose
  cups_credusage_t usage_bit;		// Current usage


  DEBUG_printf("cupsCreateCredentialsRequest(path=\"%s\", purpose=0x%x, type=%d, usage=0x%x, organization=\"%s\", org_unit=\"%s\", locality=\"%s\", state_province=\"%s\", country=\"%s\", common_name=\"%s\", num_alt_names=%u, alt_names=%p)", path, purpose, type, usage, organization, org_unit, locality, state_province, country, common_name, (unsigned)num_alt_names, (void *)alt_names);

  // Filenames...
  if (!path)
    path = http_default_path(temp, sizeof(temp));

  if (!path || !common_name)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (false);
  }

  http_make_path(csrfile, sizeof(csrfile), path, common_name, "csr");
  http_make_path(keyfile, sizeof(keyfile), path, common_name, "key");

  // Create the encryption key...
  DEBUG_puts("1cupsCreateCredentialsRequest: Creating key pair.");

  if ((pkey = openssl_create_key(type)) == NULL)
    return (false);

  DEBUG_puts("1cupsCreateCredentialsRequest: Key pair created.");

  // Create the X.509 certificate...
  DEBUG_puts("1cupsCreateCredentialsRequest: Generating self-signed X.509 certificate.");

  if ((csr = X509_REQ_new()) == NULL)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to create X.509 certificate signing request."), 1);
    goto done;
  }

  X509_REQ_set_pubkey(csr, pkey);

  if ((name = openssl_create_name(organization, org_unit, locality, state_province, country, common_name, email)) == NULL)
    goto done;

  X509_REQ_set_subject_name(csr, name);
  X509_NAME_free(name);

  // Add extension with DNS names and free buffer for GENERAL_NAME
  exts = sk_X509_EXTENSION_new_null();

  if ((ext = openssl_create_san(common_name, num_alt_names, alt_names)) == NULL)
    goto done;

  sk_X509_EXTENSION_push(exts, ext);

  cupsCopyString(temp, "critical", sizeof(temp));
  for (tempptr = temp + strlen(temp), i = 0, usage_bit = CUPS_CREDUSAGE_DIGITAL_SIGNATURE; i < (sizeof(tls_usage_strings) / sizeof(tls_usage_strings[0])); i ++, usage_bit *= 2)
  {
    if (!(usage & usage_bit))
      continue;

    snprintf(tempptr, sizeof(temp) - (size_t)(tempptr - temp), ",%s", tls_usage_strings[i]);

    tempptr += strlen(tempptr);
  }
  openssl_add_ext(exts, NID_key_usage, temp);

  temp[0] = '\0';
  for (tempptr = temp, i = 0, purpose_bit = CUPS_CREDPURPOSE_SERVER_AUTH; i < (sizeof(tls_purpose_oids) / sizeof(tls_purpose_oids[0])); i ++, purpose_bit *= 2)
  {
    if (!(purpose & purpose_bit))
      continue;

    if (tempptr == temp)
      cupsCopyString(temp, tls_purpose_oids[i], sizeof(temp));
    else
      snprintf(tempptr, sizeof(temp) - (size_t)(tempptr - temp), ",%s", tls_purpose_oids[i]);

    tempptr += strlen(tempptr);
  }
  openssl_add_ext(exts, NID_ext_key_usage, temp);

  X509_REQ_add_extensions(csr, exts);
  X509_REQ_sign(csr, pkey, EVP_sha256());

  sk_X509_EXTENSION_pop_free(exts, X509_EXTENSION_free);

  // Save them...
  if ((bio = BIO_new_file(keyfile, "wb")) == NULL)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    goto done;
  }

  if (!PEM_write_bio_PrivateKey(bio, pkey, NULL, NULL, 0, NULL, NULL))
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to write private key."), 1);
    BIO_free(bio);
    goto done;
  }

  BIO_free(bio);

  if ((bio = BIO_new_file(csrfile, "wb")) == NULL)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    goto done;
  }

  if (!PEM_write_bio_X509_REQ(bio, csr))
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to write X.509 certificate signing request."), 1);
    BIO_free(bio);
    goto done;
  }

  BIO_free(bio);

  ret = true;
  DEBUG_puts("1cupsCreateCredentialsRequest: Successfully created signing request.");

  // Cleanup...
  done:

  X509_REQ_free(csr);
  EVP_PKEY_free(pkey);

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
  STACK_OF(X509)	*certs;		// Certificate chain


  if ((certs = openssl_load_x509(credentials)) != NULL)
  {
    result = openssl_get_date(sk_X509_value(certs, 0), 1);
    sk_X509_free(certs);
  }

  return (result);
}


//
// 'cupsGetCredentialsInfo()' - Return a string describing the credentials.
//

char *					// O - Credentials description or `NULL` on error
cupsGetCredentialsInfo(
    const char *credentials,		// I - Credentials
    char       *buffer,			// I - Buffer
    size_t     bufsize)			// I - Size of buffer
{
  STACK_OF(X509)	*certs;		// Certificate chain
  X509			*cert;		// Certificate


  // Range check input...
  DEBUG_printf("cupsGetCredentialsInfo(credentials=%p, buffer=%p, bufsize=" CUPS_LLFMT ")", (void *)credentials, (void *)buffer, CUPS_LLCAST bufsize);

  if (buffer)
    *buffer = '\0';

  if (!credentials || !buffer || bufsize < 32)
  {
    DEBUG_puts("1cupsGetCredentialsInfo: Returning NULL.");
    return (NULL);
  }

  if ((certs = openssl_load_x509(credentials)) != NULL)
  {
    char		name[256],	// Common name associated with cert
			issuer[256],	// Issuer associated with cert
			expdate[256];	// Expiration data as string
    time_t		expiration;	// Expiration date of cert
    const char		*sigalg;	// Signature algorithm
    unsigned char	md5_digest[16];	// MD5 result

    cert = sk_X509_value(certs, 0);

    X509_NAME_get_text_by_NID(X509_get_subject_name(cert), NID_commonName, name, sizeof(name));
    X509_NAME_get_text_by_NID(X509_get_issuer_name(cert), NID_commonName, issuer, sizeof(issuer));
    expiration = openssl_get_date(cert, 1);

    switch (X509_get_signature_nid(cert))
    {
      case NID_ecdsa_with_SHA1 :
          sigalg = "SHA1WithECDSAEncryption";
          break;
      case NID_ecdsa_with_SHA224 :
          sigalg = "SHA224WithECDSAEncryption";
          break;
      case NID_ecdsa_with_SHA256 :
          sigalg = "SHA256WithECDSAEncryption";
          break;
      case NID_ecdsa_with_SHA384 :
          sigalg = "SHA384WithECDSAEncryption";
          break;
      case NID_ecdsa_with_SHA512 :
          sigalg = "SHA512WithECDSAEncryption";
          break;
      case NID_sha1WithRSAEncryption :
          sigalg = "SHA1WithRSAEncryption";
          break;
      case NID_sha224WithRSAEncryption :
          sigalg = "SHA224WithRSAEncryption";
          break;
      case NID_sha256WithRSAEncryption :
          sigalg = "SHA256WithRSAEncryption";
          break;
      case NID_sha384WithRSAEncryption :
          sigalg = "SHA384WithRSAEncryption";
          break;
      case NID_sha512WithRSAEncryption :
          sigalg = "SHA512WithRSAEncryption";
          break;
      default :
          sigalg = "Unknown";
          break;
    }

    cupsHashData("md5", credentials, strlen(credentials), md5_digest, sizeof(md5_digest));

    snprintf(buffer, bufsize, "%s (issued by %s) / %s / %s / %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X", name, issuer, httpGetDateString2(expiration, expdate, sizeof(expdate)), sigalg, md5_digest[0], md5_digest[1], md5_digest[2], md5_digest[3], md5_digest[4], md5_digest[5], md5_digest[6], md5_digest[7], md5_digest[8], md5_digest[9], md5_digest[10], md5_digest[11], md5_digest[12], md5_digest[13], md5_digest[14], md5_digest[15]);
    sk_X509_free(certs);
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
    const char *path,			// I - Directory path for certificate/key store or `NULL` for default
    const char *common_name,		// I - Common name for trust lookup
    const char *credentials,		// I - Credentials
    bool       require_ca)		// I - Require a CA-signed certificate?
{
  http_trust_t		trust = HTTP_TRUST_OK;
					// Trusted?
  STACK_OF(X509)	*certs;		// Certificate chain
  X509			*cert;		// Certificate
  char			*tcreds = NULL;	// Trusted credentials
  char			defpath[1024];	// Default path
  _cups_globals_t *cg = _cupsGlobals();	// Per-thread globals


  // Range check input...
  if (!path)
    path = http_default_path(defpath, sizeof(defpath));

  if (!path || !credentials || !common_name)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), false);
    return (HTTP_TRUST_UNKNOWN);
  }

  // Load the credentials...
  if ((certs = openssl_load_x509(credentials)) == NULL)
  {
    _cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, _("Unable to import credentials."), true);
    return (HTTP_TRUST_UNKNOWN);
  }

  cert = sk_X509_value(certs, 0);

  if (cg->any_root < 0)
  {
    _cupsSetDefaults();
//    openssl_load_crl();
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

        cupsSaveCredentials(path, common_name, credentials, NULL);
      }
    }

    free(tcreds);
  }
  else if ((cg->validate_certs || require_ca) && !cupsAreCredentialsValidForName(common_name, credentials))
  {
    _cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, _("No stored credentials, not valid for name."), 1);
    trust = HTTP_TRUST_INVALID;
  }
  else if (sk_X509_num(certs) > 1)
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
    if (curtime < openssl_get_date(cert, 0) || curtime > openssl_get_date(cert, 1))
    {
      _cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, _("Credentials have expired."), 1);
      trust = HTTP_TRUST_EXPIRED;
    }
  }

  sk_X509_free(certs);

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
  bool		result = false;		// Return value
  X509		*cert = NULL;		// Certificate
  X509_REQ	*crq = NULL;		// Certificate request
  X509		*root_cert = NULL;	// Root certificate, if any
  EVP_PKEY	*root_key = NULL;	// Root private key, if any
  char		defpath[1024],		// Default path
		crtfile[1024],		// Certificate filename
		root_crtfile[1024],	// Root certificate filename
		root_keyfile[1024];	// Root private key filename
  time_t	curtime;		// Current time
  ASN1_INTEGER	*serial;		// Serial number
  ASN1_TIME	*notBefore,		// Initial date
		*notAfter;		// Expiration date
  BIO		*bio;			// Output file
  char		temp[1024];		// Temporary string
  int		i, j,			// Looping vars
		num_exts;		// Number of extensions
  STACK_OF(X509_EXTENSION) *exts = NULL;// Extensions
  X509_EXTENSION *ext;			// Current extension
  cups_credpurpose_t purpose;		// Current purpose
  cups_credusage_t usage;		// Current usage
  bool		saw_usage = false,	// Saw NID_key_usage?
		saw_ext_usage = false,	// Saw NID_ext_key_usage?
		saw_san = false;	// Saw NID_subject_alt_name?


  DEBUG_printf("cupsSignCredentialsRequest(path=\"%s\", common_name=\"%s\", request=\"%s\", root_name=\"%s\", allowed_purpose=0x%x, allowed_usage=0x%x, cb=%p, cb_data=%p, expiration_date=%ld)", path, common_name, request, root_name, allowed_purpose, allowed_usage, (void *)cb, cb_data, (long)expiration_date);

  // Filenames...
  if (!path)
    path = http_default_path(defpath, sizeof(defpath));

  if (!path || !common_name || !request)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), false);
    return (false);
  }

  if (!cb)
    cb = http_default_san_cb;

  // Import the X.509 certificate request...
  DEBUG_puts("1cupsCreateCredentials: Importing X.509 certificate request.");
  if ((bio = BIO_new_mem_buf(request, (int)strlen(request))) != NULL)
  {
    PEM_read_bio_X509_REQ(bio, &crq, /*cb*/NULL, /*u*/NULL);
    BIO_free(bio);
  }

  if (!crq)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to import X.509 certificate request."), 1);
    return (false);
  }

  if (X509_REQ_verify(crq, X509_REQ_get_pubkey(crq)) < 0)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to verify X.509 certificate request."), 1);
    goto done;
  }

  // Create the X.509 certificate...
  DEBUG_puts("1cupsSignCredentialsRequest: Generating X.509 certificate.");

  if ((cert = X509_new()) == NULL)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to create X.509 certificate."), 1);
    goto done;
  }

  curtime = time(NULL);

  notBefore = ASN1_TIME_new();
  ASN1_TIME_set(notBefore, curtime);
  X509_set_notBefore(cert, notBefore);
  ASN1_TIME_free(notBefore);

  notAfter  = ASN1_TIME_new();
  ASN1_TIME_set(notAfter, expiration_date);
  X509_set_notAfter(cert, notAfter);
  ASN1_TIME_free(notAfter);

  serial = ASN1_INTEGER_new();
  ASN1_INTEGER_set(serial, (long)curtime);
  X509_set_serialNumber(cert, serial);
  ASN1_INTEGER_free(serial);

  X509_set_pubkey(cert, X509_REQ_get_pubkey(crq));

  X509_set_subject_name(cert, X509_REQ_get_subject_name(crq));
  X509_set_version(cert, 2); // v3

  // Copy/verify extensions...
  exts     = X509_REQ_get_extensions(crq);
  num_exts = sk_X509_EXTENSION_num(exts);

  for (i = 0; i < num_exts; i ++)
  {
    // Get the extension object...
    bool		add_ext = false;	// Add this extension?
    ASN1_OBJECT		*obj;			// Extension object
    ASN1_OCTET_STRING	*extdata;		// Extension data string
    unsigned char	*data = NULL;		// Extension data bytes
    int			datalen;		// Length of extension data

    ext     = sk_X509_EXTENSION_value(exts, i);
    obj     = X509_EXTENSION_get_object(ext);
    extdata = X509_EXTENSION_get_data(ext);
    datalen = i2d_ASN1_OCTET_STRING(extdata, &data);

#ifdef DEBUG
    char *tempptr;				// Pointer into string

    for (j = 0, tempptr = temp; j < datalen; j ++, tempptr += 2)
      snprintf(tempptr, sizeof(temp) - (size_t)(tempptr - temp), "%02X", data[j]);

    DEBUG_printf("1cupsSignCredentialsRequest: EXT%d=%s", OBJ_obj2nid(obj), temp);
#endif // DEBUG

    switch (OBJ_obj2nid(obj))
    {
      case NID_ext_key_usage :
          add_ext       = true;
          saw_ext_usage = true;

          if (datalen < 12 || data[2] != 0x30 || data[3] != (datalen - 4))
          {
            _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad keyUsage extension in X.509 certificate request."), 1);
	    goto done;
          }

          for (purpose = 0, j = 4; j < datalen; j += data[j + 1] + 2)
          {
            if (data[j] != 0x06 || data[j + 1] != 8 || memcmp(data + j + 2, "+\006\001\005\005\007\003", 7))
            {
	      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad keyUsage extension in X.509 certificate request."), 1);
	      goto done;
            }

            switch (data[j + 9])
            {
              case 1 :
                  purpose |= CUPS_CREDPURPOSE_SERVER_AUTH;
                  break;
              case 2 :
                  purpose |= CUPS_CREDPURPOSE_CLIENT_AUTH;
                  break;
              case 3 :
                  purpose |= CUPS_CREDPURPOSE_CODE_SIGNING;
                  break;
              case 4 :
                  purpose |= CUPS_CREDPURPOSE_EMAIL_PROTECTION;
                  break;
              case 8 :
                  purpose |= CUPS_CREDPURPOSE_TIME_STAMPING;
                  break;
              case 9 :
                  purpose |= CUPS_CREDPURPOSE_OCSP_SIGNING;
                  break;
	      default :
		  _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad keyUsage extension in X.509 certificate request."), 1);
		  goto done;
            }
          }

          DEBUG_printf("1cupsSignCredentialsRequest: purpose=0x%04x", purpose);

          if (purpose & ~allowed_purpose)
          {
            _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad keyUsage extension in X.509 certificate request."), 1);
	    goto done;
          }
          break;

      case NID_key_usage :
          add_ext   = true;
          saw_usage = true;

          if (datalen < 6 || datalen > 7 || data[2] != 0x03 || data[3] != (datalen - 4))
          {
            _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad extKeyUsage extension in X.509 certificate request."), 1);
	    goto done;
          }

          usage = 0;
          if (data[5] & 0x80)
	    usage |= CUPS_CREDUSAGE_DIGITAL_SIGNATURE;
          if (data[5] & 0x40)
	    usage |= CUPS_CREDUSAGE_NON_REPUDIATION;
          if (data[5] & 0x20)
	    usage |= CUPS_CREDUSAGE_KEY_ENCIPHERMENT;
          if (data[5] & 0x10)
	    usage |= CUPS_CREDUSAGE_DATA_ENCIPHERMENT;
          if (data[5] & 0x08)
	    usage |= CUPS_CREDUSAGE_KEY_AGREEMENT;
          if (data[5] & 0x04)
	    usage |= CUPS_CREDUSAGE_KEY_CERT_SIGN;
          if (data[5] & 0x02)
	    usage |= CUPS_CREDUSAGE_CRL_SIGN;
          if (data[5] & 0x01)
	    usage |= CUPS_CREDUSAGE_ENCIPHER_ONLY;
          if (datalen == 7 && (data[6] & 0x80))
	    usage |= CUPS_CREDUSAGE_DECIPHER_ONLY;

          DEBUG_printf("1cupsSignCredentialsRequest: usage=0x%04x", usage);

          if (usage & ~allowed_usage)
          {
            _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad extKeyUsage extension in X.509 certificate request."), 1);
	    goto done;
          }
          break;

      case NID_subject_alt_name :
          add_ext = true;
          saw_san = true;

          if (datalen < 4 || data[2] != 0x30 || data[3] != (datalen - 4))
          {
            _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad subjectAltName extension in X.509 certificate request."), 1);
	    goto done;
          }

          // Parse the SAN values (there should be an easier/standard OpenSSL API to do this!)
          for (j = 4, datalen -= 2; j < datalen; j += data[j + 1] + 2)
          {
            if (data[j] == 0x82 && data[j + 1])
            {
              // GENERAL_STRING for DNS
              memcpy(temp, data + j + 2, data[j + 1]);
              temp[data[j + 1]] = '\0';

              DEBUG_printf("1cupsSignCredentialsRequest: SAN %s", temp);

              if (!(cb)(common_name, temp, cb_data))
              {
                _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Validation of subjectAltName in X.509 certificate request failed."), 1);
                goto done;
              }
	    }
          }
          break;
    }

    OPENSSL_free(data);

    // If we get this far, the object is OK and we can add it...
    if (add_ext && !X509_add_ext(cert, ext, -1))
      goto done;
  }

  // Add basic constraints for an "edge" certificate...
  if ((ext = X509V3_EXT_conf_nid(/*conf*/NULL, /*ctx*/NULL, NID_basic_constraints, "critical,CA:FALSE,pathlen:0")) == NULL || !X509_add_ext(cert, ext, -1))
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to add extension to X.509 certificate."), 1);
    goto done;
  }

  // Add key usage extensions as needed...
  if (!saw_usage)
  {
    if ((ext = X509V3_EXT_conf_nid(/*conf*/NULL, /*ctx*/NULL, NID_key_usage, "critical,digitalSignature,keyEncipherment")) == NULL || !X509_add_ext(cert, ext, -1))
    {
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to add extension to X.509 certificate."), 1);
      goto done;
    }
  }

  if (!saw_ext_usage)
  {
    if ((ext = X509V3_EXT_conf_nid(/*conf*/NULL, /*ctx*/NULL, NID_ext_key_usage, tls_usage_strings[0])) == NULL || !X509_add_ext(cert, ext, -1))
    {
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to add extension to X.509 certificate."), 1);
      goto done;
    }
  }

  if (!saw_san)
  {
    if ((ext = openssl_create_san(common_name, /*num_alt_names*/0, /*alt_names*/NULL)) == NULL || !X509_add_ext(cert, ext, -1))
    {
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to add extension to X.509 certificate."), 1);
      goto done;
    }
  }

  // Try loading a root certificate...
  http_make_path(root_crtfile, sizeof(root_crtfile), path, root_name ? root_name : "_site_", "crt");
  http_make_path(root_keyfile, sizeof(root_keyfile), path, root_name ? root_name : "_site_", "key");

  if (!access(root_crtfile, 0) && !access(root_keyfile, 0))
  {
    if ((bio = BIO_new_file(root_crtfile, "rb")) != NULL)
    {
      PEM_read_bio_X509(bio, &root_cert, /*cb*/NULL, /*u*/NULL);
      BIO_free(bio);

      if ((bio = BIO_new_file(root_keyfile, "rb")) != NULL)
      {
	PEM_read_bio_PrivateKey(bio, &root_key, /*cb*/NULL, /*u*/NULL);
	BIO_free(bio);
      }

      if (!root_key)
      {
        // Only use root certificate if we have the key...
        X509_free(root_cert);
        root_cert = NULL;
      }
    }
  }

  if (!root_cert || !root_key)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to load X.509 CA certificate and private key."), 1);
    goto done;
  }

  X509_set_issuer_name(cert, X509_get_subject_name(root_cert));
  X509_sign(cert, root_key, EVP_sha256());

  // Save the certificate...
  http_make_path(crtfile, sizeof(crtfile), path, common_name, "crt");

  if ((bio = BIO_new_file(crtfile, "wb")) == NULL)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    goto done;
  }

  if (!PEM_write_bio_X509(bio, cert))
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to write X.509 certificate."), 1);
    BIO_free(bio);
    goto done;
  }

  PEM_write_bio_X509(bio, root_cert);

  BIO_free(bio);
  result = true;
  DEBUG_puts("1cupsSignRequest: Successfully created credentials.");

  // Cleanup...
  done:

  if (exts)
    sk_X509_EXTENSION_pop_free(exts, X509_EXTENSION_free);
  if (crq)
    X509_REQ_free(crq);
  if (cert)
    X509_free(cert);
  if (root_cert)
    X509_free(root_cert);
  if (root_key)
    EVP_PKEY_free(root_key);

  return (result);
}


//
// 'httpCopyPeerCredentials()' - Copy the credentials associated with the peer in an encrypted connection.
//

char *					// O - PEM-encoded X.509 certificate chain or `NULL`
httpCopyPeerCredentials(http_t *http)	// I - Connection to server
{
  char		*credentials = NULL;	// Return value
  size_t	alloc_creds = 0;	// Allocated size
  STACK_OF(X509) *chain;		// Certificate chain


  DEBUG_printf("httpCopyPeerCredentials(http=%p)", (void *)http);

  if (http && http->tls)
  {
    // Get the chain of certificates for the remote end...
    chain = SSL_get_peer_cert_chain(http->tls);

    DEBUG_printf("1httpCopyPeerCredentials: chain=%p", (void *)chain);

    if (chain)
    {
      // Loop through the certificates, adding them to the string...
      int	i,			// Looping var
		count;			// Number of certs

      for (i = 0, count = sk_X509_num(chain); i < count; i ++)
      {
	X509	*cert = sk_X509_value(chain, i);
					  // Current certificate
	BIO	*bio = BIO_new(BIO_s_mem());
					  // Memory buffer for cert

        DEBUG_printf("1httpCopyPeerCredentials: chain[%d/%d]=%p", i + 1, count, (void *)cert);

#ifdef DEBUG
	char subjectName[256], issuerName[256];
	X509_NAME_get_text_by_NID(X509_get_subject_name(cert), NID_commonName, subjectName, sizeof(subjectName));
	X509_NAME_get_text_by_NID(X509_get_issuer_name(cert), NID_commonName, issuerName, sizeof(issuerName));
	DEBUG_printf("1httpCopyPeerCredentials: subjectName=\"%s\", issuerName=\"%s\"", subjectName, issuerName);

	STACK_OF(GENERAL_NAME) *names;	// subjectAltName values
	names = X509_get_ext_d2i(cert, NID_subject_alt_name, /*crit*/NULL, /*idx*/NULL);
	DEBUG_printf("1httpCopyPeerCredentials: subjectAltNames=%p(%d)", (void *)names, names ? sk_GENERAL_NAME_num(names) : 0);
        if (names)
          GENERAL_NAMES_free(names);
#endif // DEBUG

	if (bio)
	{
	  long	bytes;			// Number of bytes
	  char	*buffer;		// Pointer to bytes

	  if (PEM_write_bio_X509(bio, cert))
	  {
	    if ((bytes = BIO_get_mem_data(bio, &buffer)) > 0)
	    {
	      // Expand credentials string...
	      if ((credentials = realloc(credentials, alloc_creds + (size_t)bytes + 1)) != NULL)
	      {
	        // Copy PEM-encoded data...
	        memcpy(credentials + alloc_creds, buffer, bytes);
	        credentials[alloc_creds + (size_t)bytes] = '\0';
	        alloc_creds += (size_t)bytes;
	      }
	    }
	  }

	  BIO_free(bio);

	  if (!credentials)
	    break;
	}
      }
    }
  }

  DEBUG_printf("1httpCopyPeerCredentials: Returning \"%s\".", credentials);

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
  _http_tls_credentials_t *hcreds;	// Credentials


  DEBUG_printf("_httpCreateCredentials(credentials=\"%s\", key=\"%s\")", credentials, key);

  if (!credentials || !*credentials || !key || !*key)
    return (NULL);

  if ((hcreds = calloc(1, sizeof(_http_tls_credentials_t))) == NULL)
    return (NULL);

  hcreds->use = 1;

  // Load the certificates...
  if ((hcreds->certs = openssl_load_x509(credentials)) == NULL)
  {
    _httpFreeCredentials(hcreds);
    hcreds = NULL;
  }
  else
  {
    // Load the private key...
    BIO	*bio;				// Basic I/O for string

    if ((bio = BIO_new_mem_buf(key, strlen(key))) == NULL)
    {
      _httpFreeCredentials(hcreds);
      hcreds = NULL;
    }

    if (!PEM_read_bio_PrivateKey(bio, &hcreds->key, NULL, NULL))
    {
      _httpFreeCredentials(hcreds);
      hcreds = NULL;
    }
  }

  DEBUG_printf("1_httpCreateCredentials: Returning %p.", (void *)hcreds);

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

  sk_X509_free(hcreds->certs);
  free(hcreds);
}


//
// '_httpTLSInitialize()' - Initialize the TLS stack.
//

void
_httpTLSInitialize(void)
{
  // OpenSSL no longer requires explicit initialization...
}


//
// '_httpTLSPending()' - Return the number of pending TLS-encrypted bytes.
//

size_t					// O - Bytes available
_httpTLSPending(http_t *http)		// I - HTTP connection
{
  return ((size_t)SSL_pending(http->tls));
}


//
// '_httpTLSRead()' - Read from a SSL/TLS connection.
//

int					// O - Bytes read
_httpTLSRead(http_t *http,		// I - Connection to server
	     char   *buf,		// I - Buffer to store data
	     int    len)		// I - Length of buffer
{
  int bytes = SSL_read((SSL *)(http->tls), buf, len);
					// Bytes read

  DEBUG_printf("7_httpTLSRead(http=%p, buf=%p, len=%d) returning %d", (void *)http, (void *)buf, len, bytes);

  return (bytes);
}


//
// '_httpTLSStart()' - Set up SSL/TLS support on a connection.
//

bool					// O - `true` on success, `false` on failure
_httpTLSStart(http_t *http)		// I - Connection to server
{
  const char	*keypath;		// Certificate store path
  BIO		*bio;			// Basic input/output context
  SSL_CTX	*context;		// Encryption context
  char		hostname[256],		// Hostname
		cipherlist[256];	// List of cipher suites
  unsigned long	error;			// Error code, if any
  static const uint16_t versions[] =	// SSL/TLS versions
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


  DEBUG_printf("3_httpTLSStart(http=%p)", (void *)http);

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

  if (http->mode == _HTTP_MODE_CLIENT)
  {
    // Negotiate a TLS connection as a client...
    context = SSL_CTX_new(TLS_client_method());
    if (http->tls_credentials)
    {
      int	i,			// Looping var
		count;			// Number of certificates

      DEBUG_puts("4_httpTLSStart: Using client certificate.");
      SSL_CTX_use_certificate(context, sk_X509_value(http->tls_credentials->certs, 0));
      SSL_CTX_use_PrivateKey(context, http->tls_credentials->key);

      count = sk_X509_num(http->tls_credentials->certs);
      for (i = 1; i < count; i ++)
        SSL_CTX_add_extra_chain_cert(context, sk_X509_value(http->tls_credentials->certs, i));
    }
  }
  else
  {
    // Negotiate a TLS connection as a server
    char	crtfile[1024],		// Certificate file
		keyfile[1024];		// Private key file
    const char	*cn,			// Common name to lookup
		*cnptr;			// Pointer into common name
    bool	have_creds = false;	// Have credentials?

    context = SSL_CTX_new(TLS_server_method());

    // Find the TLS certificate...
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
        // Unable to get local socket address so use default...
	DEBUG_printf("4_httpTLSStart: Unable to get socket address: %s", strerror(errno));
	hostname[0] = '\0';
      }
      else if (httpAddrIsLocalhost(&addr))
      {
        // Local access top use default...
	hostname[0] = '\0';
      }
      else
      {
        // Lookup the socket address...
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

      if (!cupsCreateCredentials(tls_keypath, false, CUPS_CREDPURPOSE_SERVER_AUTH, CUPS_CREDTYPE_DEFAULT, CUPS_CREDUSAGE_DEFAULT_TLS, NULL, NULL, NULL, NULL, NULL, cn, NULL, 0, NULL, NULL, time(NULL) + 3650 * 86400))
      {
	DEBUG_puts("4_httpTLSStart: cupsCreateCredentials failed.");
	http->error  = errno = EINVAL;
	http->status = HTTP_STATUS_ERROR;
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to create server credentials."), 1);
	SSL_CTX_free(context);
        cupsMutexUnlock(&tls_mutex);

	return (false);
      }
    }

    cupsMutexUnlock(&tls_mutex);

    DEBUG_printf("4_httpTLSStart: Using private key file '%s'.", keyfile);
    DEBUG_printf("4_httpTLSStart: Using certificate file '%s'.", crtfile);

    if (!SSL_CTX_use_PrivateKey_file(context, keyfile, SSL_FILETYPE_PEM) || !SSL_CTX_use_certificate_chain_file(context, crtfile))
    {
      // Unable to load private key or certificate...
      DEBUG_puts("4_httpTLSStart: Unable to use private key or certificate chain file.");
      if ((error = ERR_get_error()) != 0)
        _cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, ERR_error_string(error, NULL), 0);

      http->status = HTTP_STATUS_ERROR;
      http->error  = EIO;

      SSL_CTX_free(context);

      return (false);
    }
  }

  // Set TLS options...
  cupsCopyString(cipherlist, "HIGH:!DH:+DHE", sizeof(cipherlist));
  if ((tls_options & _HTTP_TLS_ALLOW_RC4) && http->mode == _HTTP_MODE_CLIENT)
    cupsConcatString(cipherlist, ":+RC4", sizeof(cipherlist));
  else
    cupsConcatString(cipherlist, ":!RC4", sizeof(cipherlist));
  if (tls_options & _HTTP_TLS_DENY_CBC)
    cupsConcatString(cipherlist, ":!SHA1:!SHA256:!SHA384", sizeof(cipherlist));
  cupsConcatString(cipherlist, ":@STRENGTH", sizeof(cipherlist));

  DEBUG_printf("4_httpTLSStart: cipherlist='%s', tls_min_version=%d, tls_max_version=%d", cipherlist, tls_min_version, tls_max_version);

  SSL_CTX_set_min_proto_version(context, versions[tls_min_version]);
  SSL_CTX_set_max_proto_version(context, versions[tls_max_version]);
  SSL_CTX_set_cipher_list(context, cipherlist);

  // Setup a TLS session
  cupsMutexLock(&tls_mutex);
  if (!tls_bio_method)
  {
    tls_bio_method = BIO_meth_new(BIO_get_new_index(), "http");
    BIO_meth_set_ctrl(tls_bio_method, http_bio_ctrl);
    BIO_meth_set_create(tls_bio_method, http_bio_new);
    BIO_meth_set_destroy(tls_bio_method, http_bio_free);
    BIO_meth_set_read(tls_bio_method, http_bio_read);
    BIO_meth_set_puts(tls_bio_method, http_bio_puts);
    BIO_meth_set_write(tls_bio_method, http_bio_write);
  }

  bio = BIO_new(tls_bio_method);
  cupsMutexUnlock(&tls_mutex);

  BIO_ctrl(bio, BIO_C_SET_FILE_PTR, 0, (char *)http);

  http->tls = SSL_new(context);
  SSL_set_bio(http->tls, bio, bio);

  if (http->mode == _HTTP_MODE_CLIENT)
  {
    // Negotiate as a client...
    DEBUG_printf("4_httpTLSStart: Setting server name TLS extension to '%s'...", http->hostname);
    SSL_set_tlsext_host_name(http->tls, http->hostname);

    DEBUG_puts("4_httpTLSStart: Calling SSL_connect...");
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

      DEBUG_printf("4_httpTLSStart: Returning false (%s)", ERR_error_string(error, NULL));

      return (false);
    }
  }
  else
  {
    // Negotiate as a server...
    DEBUG_puts("4_httpTLSStart: Calling SSL_accept...");
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

      DEBUG_printf("4_httpTLSStart: Returning false (%s)", ERR_error_string(error, NULL));

      return (false);
    }
  }

  DEBUG_puts("4_httpTLSStart: Returning true.");

  return (true);
}


//
// '_httpTLSStop()' - Shut down SSL/TLS on a connection.
//

void
_httpTLSStop(http_t *http)		// I - Connection to server
{
  SSL_CTX	*context;		// Context for encryption


  context = SSL_get_SSL_CTX(http->tls);

  SSL_shutdown(http->tls);
  SSL_CTX_free(context);
  SSL_free(http->tls);

  http->tls = NULL;
}


//
// '_httpTLSWrite()' - Write to a SSL/TLS connection.
//

int					// O - Bytes written
_httpTLSWrite(http_t     *http,		// I - Connection to server
	      const char *buf,		// I - Buffer holding data
	      int        len)		// I - Length of buffer
{
  return (SSL_write(http->tls, buf, len));
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
// 'http_bio_ctrl()' - Control the HTTP connection.
//

static long				// O - Result/data
http_bio_ctrl(BIO  *h,			// I - BIO data
              int  cmd,			// I - Control command
	      long arg1,		// I - First argument
	      void *arg2)		// I - Second argument
{
  DEBUG_printf("8http_bio_ctl(h=%p, cmd=%d, arg1=%ld, arg2=%p)", (void *)h, cmd, arg1, arg2);

  (void)arg1;

  switch (cmd)
  {
    default :
        return (0);

    case BIO_CTRL_RESET :
        BIO_set_data(h, NULL);
	return (0);

    case BIO_C_SET_FILE_PTR :
        BIO_set_data(h, arg2);
        BIO_set_init(h, 1);
	return (1);

    case BIO_C_GET_FILE_PTR :
        if (arg2)
	{
	  *((void **)arg2) = BIO_get_data(h);
	  return (1);
	}
	else
	  return (0);

    case BIO_CTRL_DUP :
    case BIO_CTRL_FLUSH :
        return (1);
  }
}


//
// 'http_bio_free()' - Free OpenSSL data.
//

static int				// O - 1 on success, 0 on failure
http_bio_free(BIO *h)			// I - BIO data
{
  DEBUG_printf("8http_bio_free(h=%p)", (void *)h);

  if (!h)
    return (0);

  if (BIO_get_shutdown(h))
    BIO_set_init(h, 0);

  return (1);
}


//
// 'http_bio_new()' - Initialize an OpenSSL BIO structure.
//

static int				// O - 1 on success, 0 on failure
http_bio_new(BIO *h)			// I - BIO data
{
  DEBUG_printf("8http_bio_new(h=%p)", (void *)h);

  if (!h)
    return (0);

  BIO_set_init(h, 0);
  BIO_set_data(h, NULL);

  return (1);
}


//
// 'http_bio_puts()' - Send a string for OpenSSL.
//

static int				// O - Bytes written
http_bio_puts(BIO        *h,		// I - BIO data
              const char *str)		// I - String to write
{
  DEBUG_printf("8http_bio_puts(h=%p, str=\"%s\")", (void *)h, str);

#ifdef WIN32
  return (send(((http_t *)BIO_get_data(h))->fd, str, (int)strlen(str), 0));
#else
  return ((int)send(((http_t *)BIO_get_data(h))->fd, str, strlen(str), 0));
#endif // WIN32
}


//
// 'http_bio_read()' - Read data for OpenSSL.
//

static int				// O - Bytes read
http_bio_read(BIO  *h,			// I - BIO data
              char *buf,		// I - Buffer
	      int  size)		// I - Number of bytes to read
{
  http_t	*http;			// HTTP connection
  int		bytes;			// Bytes read


  DEBUG_printf("8http_bio_read(h=%p, buf=%p, size=%d)", (void *)h, (void *)buf, size);

  http = (http_t *)BIO_get_data(h);
  DEBUG_printf("9http_bio_read: http=%p", (void *)http);

  if (!http->blocking)
  {
    // Make sure we have data before we read...
    if (!_httpWait(http, 10000, 0))
    {
#ifdef WIN32
      http->error = WSAETIMEDOUT;
#else
      http->error = ETIMEDOUT;
#endif // WIN32

      DEBUG_puts("9http_bio_read: Timeout, returning -1.");
      return (-1);
    }
  }

  bytes = (int)recv(http->fd, buf, (size_t)size, 0);
  DEBUG_printf("9http_bio_read: Returning %d.", bytes);

  return (bytes);
}


//
// 'http_bio_write()' - Write data for OpenSSL.
//

static int				// O - Bytes written
http_bio_write(BIO        *h,		// I - BIO data
               const char *buf,		// I - Buffer to write
	       int        num)		// I - Number of bytes to write
{
  int	bytes;				// Bytes written


  DEBUG_printf("8http_bio_write(h=%p, buf=%p, num=%d)", (void *)h, (void *)buf, num);

  bytes = (int)send(((http_t *)BIO_get_data(h))->fd, buf, (size_t)num, 0);

  DEBUG_printf("9http_bio_write: Returning %d.", bytes);
  return (bytes);
}


//
// 'openssl_add_ext()' - Add an extension.
//

static bool				// O - `true` on success, `false` on error
openssl_add_ext(
    STACK_OF(X509_EXTENSION) *exts,	// I - Stack of extensions
    int                      nid,	// I - Extension ID
    const char               *value)	// I - Value
{
  X509_EXTENSION *ext = NULL;		// Extension


  DEBUG_printf("3openssl_add_ext(exts=%p, nid=%d, value=\"%s\")", (void *)exts, nid, value);

  // Create and add the extension...
  if ((ext = X509V3_EXT_conf_nid(/*conf*/NULL, /*ctx*/NULL, nid, value)) == NULL)
  {
    DEBUG_puts("4openssl_add_ext: Unable to create extension, returning false.");
    return (false);
  }

  sk_X509_EXTENSION_push(exts, ext);

  return (true);
}


//
// 'openssl_create_key()' - Create a suitable key pair for a certificate/signing request.
//

static EVP_PKEY *			// O - Key pair
openssl_create_key(
    cups_credtype_t type)		// I - Type of key
{
  EVP_PKEY	*pkey;			// Key pair
  EVP_PKEY_CTX	*ctx;			// Key generation context
  int		algid;			// Algorithm NID
  int		bits = 0;		// Bits
  int		curveid = 0;		// Curve NID


  switch (type)
  {
    case CUPS_CREDTYPE_ECDSA_P256_SHA256 :
        algid   = EVP_PKEY_EC;
        curveid = NID_secp256k1;
	break;

    case CUPS_CREDTYPE_ECDSA_P384_SHA256 :
        algid   = EVP_PKEY_EC;
        curveid = NID_secp384r1;
	break;

    case CUPS_CREDTYPE_ECDSA_P521_SHA256 :
        algid   = EVP_PKEY_EC;
        curveid = NID_secp521r1;
	break;

    case CUPS_CREDTYPE_RSA_2048_SHA256 :
        algid = EVP_PKEY_RSA;
        bits  = 2048;
	break;

    default :
    case CUPS_CREDTYPE_RSA_3072_SHA256 :
        algid = EVP_PKEY_RSA;
        bits  = 3072;
	break;

    case CUPS_CREDTYPE_RSA_4096_SHA256 :
        algid = EVP_PKEY_RSA;
        bits  = 4096;
	break;
  }

  pkey = NULL;

  if ((ctx = EVP_PKEY_CTX_new_id(algid, NULL)) == NULL)
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to create private key context."), 1);
  else if (EVP_PKEY_keygen_init(ctx) <= 0)
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to initialize private key context."), 1);
  else if (bits && EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, bits) <= 0)
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to configure private key context."), 1);
  else if (curveid && EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, curveid) <= 0)
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to configure private key context."), 1);
  else if (EVP_PKEY_keygen(ctx, &pkey) <= 0)
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to create private key."), 1);

  EVP_PKEY_CTX_free(ctx);

  return (pkey);
}


//
// 'openssl_create_name()' - Create an X.509 name value for a certificate/signing request.
//

static X509_NAME *			// O - X.509 name value
openssl_create_name(
    const char      *organization,	// I - Organization or `NULL` to use common name
    const char      *org_unit,		// I - Organizational unit or `NULL` for none
    const char      *locality,		// I - City/town or `NULL` for "Unknown"
    const char      *state_province,	// I - State/province or `NULL` for "Unknown"
    const char      *country,		// I - Country or `NULL` for locale-based default
    const char      *common_name,	// I - Common name
    const char      *email)		// I - Email address or `NULL` for none
{
  X509_NAME	*name;			// Subject/issuer name
  cups_lang_t	*language;		// Default language info
  const char	*langname;		// Language name


  language = cupsLangDefault();
  langname = language->language;
  name     = X509_NAME_new();
  if (country)
    X509_NAME_add_entry_by_txt(name, SN_countryName, MBSTRING_ASC, (unsigned char *)country, -1, -1, 0);
  else if (strlen(langname) == 5)
    X509_NAME_add_entry_by_txt(name, SN_countryName, MBSTRING_ASC, (unsigned char *)langname + 3, -1, -1, 0);
  else
    X509_NAME_add_entry_by_txt(name, SN_countryName, MBSTRING_ASC, (unsigned char *)"US", -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, SN_commonName, MBSTRING_ASC, (unsigned char *)common_name, -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, SN_organizationName, MBSTRING_ASC, (unsigned char *)(organization ? organization : common_name), -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, SN_organizationalUnitName, MBSTRING_ASC, (unsigned char *)(org_unit ? org_unit : ""), -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, SN_stateOrProvinceName, MBSTRING_ASC, (unsigned char *)(state_province ? state_province : "Unknown"), -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, SN_localityName, MBSTRING_ASC, (unsigned char *)(locality ? locality : "Unknown"), -1, -1, 0);
  if (email && *email)
    X509_NAME_add_entry_by_txt(name, "emailAddress", MBSTRING_ASC, (unsigned char *)email, -1, -1, 0);

  return (name);
}


//
// 'openssl_create_san()' - Create a list of subjectAltName values for a certificate/signing request.
//

static X509_EXTENSION *			// O - Extension
openssl_create_san(
    const char         *common_name,	// I - Common name
    size_t             num_alt_names,	// I - Number of alternate names
    const char * const *alt_names)	// I - List of alternate names
{
  char		temp[2048],		// Temporary string
		*tempptr;		// Pointer into temporary string
  size_t	i;			// Looping var


  // Add the common name
  snprintf(temp, sizeof(temp), "DNS:%s", common_name);
  tempptr = temp + strlen(temp);

  if (strstr(common_name, ".local") == NULL)
  {
    // Add common_name.local to the list, too...
    char	localname[256],		// hostname.local
		*localptr;		// Pointer into localname

    cupsCopyString(localname, common_name, sizeof(localname));
    if ((localptr = strchr(localname, '.')) != NULL)
      *localptr = '\0';

    snprintf(tempptr, sizeof(temp) - (size_t)(tempptr - temp), ",DNS:%s.local", localname);
    tempptr += strlen(tempptr);
  }

  // Add any alternate names...
  for (i = 0; i < num_alt_names; i ++)
  {
    if (strcmp(alt_names[i], "localhost"))
    {
      snprintf(tempptr, sizeof(temp) - (size_t)(tempptr - temp), ",DNS:%s", alt_names[i]);
      tempptr += strlen(tempptr);
    }
  }

  // Return the stack
  return (X509V3_EXT_conf_nid(/*conf*/NULL, /*ctx*/NULL, NID_subject_alt_name, temp));
}


//
// 'openssl_get_date()' - Get the notBefore or notAfter date of a certificate.
//

static time_t				// O - UNIX time in seconds
openssl_get_date(X509 *cert,		// I - Certificate
                 int  which)		// I - 0 for notBefore, 1 for notAfter
{
  struct tm	exptm;			// Expiration date components


  if (which)
    ASN1_TIME_to_tm(X509_get0_notAfter(cert), &exptm);
  else
    ASN1_TIME_to_tm(X509_get0_notBefore(cert), &exptm);

  return (mktime(&exptm));
}


#if 0
//
// 'openssl_load_crl()' - Load the certificate revocation list, if any.
//

static void
openssl_load_crl(void)
{
  cupsMutexLock(&tls_mutex);

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
	  httpDecode64((char *)data + num_data, &decoded, line, NULL);
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
#endif // 0


//
// 'openssl_load_x509()' - Load a stack of X.509 certificates.
//

static STACK_OF(X509) *			// O - Stack of X.509 certificates
openssl_load_x509(
    const char *credentials)		// I - Credentials string
{
  STACK_OF(X509)	*certs = NULL;	// Certificate chain
  X509			*cert = NULL;	// Current certificate
  BIO			*bio;		// Basic I/O for string


  // Range check input...
  if (!credentials || !*credentials)
    return (NULL);

  // Make a BIO memory buffer for the string...
  if ((bio = BIO_new_mem_buf(credentials, strlen(credentials))) == NULL)
    return (NULL);

  // Read all the X509 certificates from the string...
  while (PEM_read_bio_X509(bio, &cert, NULL, (void *)""))
  {
    if (!certs)
    {
      // Make a new stack of X509 certs...
      certs = sk_X509_new_null();
    }

    if (certs)
    {
      // Add the X509 certificate...
      sk_X509_push(certs, cert);
    }
    else
    {
      // Unable to add, free and stop...
      X509_free(cert);
      break;
    }

    cert = NULL;
  }

  BIO_free(bio);

  return (certs);
}
