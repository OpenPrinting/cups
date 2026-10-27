//
// TLS routines for CUPS.
//
// Copyright © 2020-2025 by OpenPrinting.
// Copyright @ 2007-2014 by Apple Inc.
// Copyright @ 1997-2007 by Easy Software Products, all rights reserved.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "cups-private.h"
#include "dir.h"
#include <fcntl.h>
#include <math.h>
#include <sys/stat.h>
#ifdef __APPLE__
#  include <Security/Security.h>
#endif // __APPLE__
#ifdef _WIN32
#  pragma comment(lib, "crypt32.lib")	// Link in crypt32 library...
#  include <tchar.h>
#  include <wincrypt.h>
#else
#  include <poll.h>
#  include <signal.h>
#  include <sys/time.h>
#  include <sys/resource.h>
#endif // _WIN32


//
// Local globals...
//

static bool		tls_auto_create = false;
					// Auto-create self-signed certs?
static char		*tls_common_name = NULL;
					// Default common name
static char		*tls_keypath = NULL;
					// Certificate store path
static cups_mutex_t	tls_mutex = CUPS_MUTEX_INITIALIZER;
					// Mutex for certificates
static int		tls_options = -1,// Options for TLS connections
			tls_min_version = _HTTP_TLS_1_2,
			tls_max_version = _HTTP_TLS_MAX;


//
// Local functions...
//

static char		*http_copy_file(const char *path, const char *common_name, const char *ext);
static const char	*http_default_path(char *buffer, size_t bufsize);
static bool		http_default_san_cb(const char *common_name, const char *subject_alt_name, void *data);
#if defined(_WIN32) || defined(HAVE_GNUTLS)
static char		*http_der_to_pem(const unsigned char *der, size_t dersize);
#endif // _WIN32 || HAVE_GNUTLS
static const char	*http_make_path(char *buffer, size_t bufsize, const char *dirname, const char *filename, const char *ext);
static bool		http_save_file(const char *path, const char *common_name, const char *ext, const char *value);


//
// Include platform-specific TLS code...
//

#ifdef HAVE_OPENSSL
#  include "tls-openssl.c"
#else // HAVE_GNUTLS
#  include "tls-gnutls.c"
#endif // HAVE_OPENSSL


//
// 'cupsCopyCredentials()' - Copy the X.509 certificate chain to a string.
//

char *					// O - PEM-encoded certificate(s)
cupsCopyCredentials(
    const char *path,			// I - Directory path for certificate/key store or `NULL` for default
    const char *common_name)		// I - Common name
{
  return (http_copy_file(path, common_name, "crt"));
}


//
// 'cupsCopyCredentialsKey()' - Copy the private key to a string.
//

char *					// O - PEM-encoded private key
cupsCopyCredentialsKey(
    const char *path,			// I - Directory path for certificate/key store or `NULL` for default
    const char *common_name)		// I - Common name
{
  char	*key = http_copy_file(path, common_name, "key");
					// Private key

  if (!key)
    key = http_copy_file(path, common_name, "ktm");

  return (key);
}


//
// 'cupsCopyCredentialsPublicKey()' - Copy the public key for a X.509 certificate request.
//

char *					// O - PEM-encoded public key
cupsCopyCredentialsPublicKey(
    const char *path,			// I - Directory path for certificate/key store or `NULL` for default
    const char *common_name)		// I - Common name
{
  return (http_copy_file(path, common_name, "pub"));
}


//
// 'cupsCopyCredentialsRequest()' - Copy the X.509 certificate signing request to a string.
//

char *					// O - PEM-encoded X.509 certificate signing request
cupsCopyCredentialsRequest(
    const char *path,			// I - Directory path for certificate/key store or `NULL` for default
    const char *common_name)		// I - Common name
{
  return (http_copy_file(path, common_name, "csr"));
}


//
// 'cupsSaveCredentials()' - Save the credentials associated with a printer/server.
//
// This function saves the the PEM-encoded X.509 certificate chain string and
// private key (if not `NULL`) to the directory "path" or, if "path" is `NULL`,
// in a per-user or system-wide (when running as root) certificate/key store.
//

bool					// O - `true` on success, `false` on failure
cupsSaveCredentials(
    const char *path,			// I - Directory path for certificate/key store or `NULL` for default
    const char *common_name,		// I - Common name for certificate
    const char *credentials,		// I - PEM-encoded certificate chain or `NULL` to remove
    const char *key)			// I - PEM-encoded private key or `NULL` for none
{
  bool	ret = false;			// Return value
  char	defpath[1024],			// Default path
	crtfile[1024],			// Certificate filename
	keyfile[1024],			// Key filename
	ktmfile[1024];			// Temporary key filename


  // Validate input...
  DEBUG_printf("cupsSaveCredentials(path=\"%s\", common_name=\"%s\", credentials=%p(%u), key=%p(%u))", path, common_name, (void *)credentials, credentials ? (unsigned)strlen(credentials) : 0, (void *)key, key ? (unsigned)strlen(key) : 0);

  if (!path)
    path = http_default_path(defpath, sizeof(defpath));

  if (credentials)
  {
    // Make sure it looks like a PEM-encoded cert...
    if (strncmp(credentials, "-----BEGIN CERTIFICATE-----", 27) || strstr(credentials, "-----END CERTIFICATE-----") == NULL)
    {
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad certificate."), true);
      return (false);
    }
  }

  if (key)
  {
    // Make sure it looks like a PEM-encoded private key...
    if (strncmp(key, "-----BEGIN PRIVATE KEY-----", 27) || strstr(key, "-----END PRIVATE KEY-----") == NULL)
    {
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Bad private key."), true);
      return (false);
    }
  }

  // Save or delete credentials...
  http_make_path(crtfile, sizeof(crtfile), path, common_name, "crt");
  http_make_path(keyfile, sizeof(keyfile), path, common_name, "key");
  http_make_path(ktmfile, sizeof(ktmfile), path, common_name, "ktm");

  DEBUG_printf("1cupsSaveCredentials: crtfile=\"%s\"", crtfile);
  DEBUG_printf("1cupsSaveCredentials: keyfile=\"%s\"", keyfile);
  DEBUG_printf("1cupsSaveCredentials: ktmfile=\"%s\"", ktmfile);

  if (!credentials && !key)
  {
    // Delete credentials...
    if (!unlink(crtfile) && !unlink(keyfile))
      ret = true;
    else
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), false);
  }
  else if (!credentials && key)
  {
    // Bad arguments...
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), false);
  }
  else if (!key && access(keyfile, 0) && access(ktmfile, 0))
  {
    // Missing key file...
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), false);

    DEBUG_printf("1cupsSaveCredentials: access(\"%s\", 0)=%d", keyfile, access(keyfile, 0));
    DEBUG_printf("1cupsSaveCredentials: access(\"%s\", 0)=%d", ktmfile, access(ktmfile, 0));
  }
  else if (http_save_file(path, common_name, "crt", credentials))
  {
    // Certificate saved, save or rename key file as needed...
    if (key)
    {
      ret = http_save_file(path, common_name, "key", key);
    }
    else if (!access(ktmfile, 0) && rename(ktmfile, keyfile))
    {
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), false);

      DEBUG_printf("1cupsSaveCredentials: rename(\"%s\", \"%s\") failed.", ktmfile, keyfile);
    }
    else
    {
      ret = true;
    }
  }

  return (ret);
}


//
// 'cupsSetServerCredentials()' - Set the default server credentials.
//
// Note: The server credentials are used by all threads in the running process.
// This function is threadsafe.
//

int					// O - `1` on success, `0` on failure
cupsSetServerCredentials(
    const char *path,			// I - Directory path for certificate/key store or `NULL` for default
    const char *common_name,		// I - Default common name for server
    int        auto_create)		// I - `1` = automatically create self-signed certificates
{
  char	temp[1024];			// Default path buffer


  DEBUG_printf("cupsSetServerCredentials(path=\"%s\", common_name=\"%s\", auto_create=%d)", path, common_name, auto_create);

  // Use defaults as needed...
  if (!path)
    path = http_default_path(temp, sizeof(temp));

  // Range check input...
  if (!path || !common_name)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (0);
  }

  cupsMutexLock(&tls_mutex);

  // Free old values...
  if (tls_keypath)
    _cupsStrFree(tls_keypath);

  if (tls_common_name)
    _cupsStrFree(tls_common_name);

  // Save the new values...
  tls_keypath     = _cupsStrAlloc(path);
  tls_auto_create = auto_create;
  tls_common_name = _cupsStrAlloc(common_name);

  cupsMutexUnlock(&tls_mutex);

  return (1);
}


//
// '_httpTLSSetOptions()' - Set TLS protocol and cipher suite options.
//

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


//
// 'http_copy_file()' - Copy the contents of a file to a string.
//

static char *				// O - Contents of file or `NULL` on error
http_copy_file(const char *path,	// I - Directory
               const char *common_name,	// I - Common name
               const char *ext)		// I - Extension
{
  char		*s = NULL;		// String
  int		fd;			// File descriptor
  char		defpath[1024],		// Default path
		filename[1024];		// Filename
  struct stat	fileinfo;		// File information


  DEBUG_printf("3http_copy_file(path=\"%s\", common_name=\"%s\", ext=\"%s\")", path, common_name, ext);

  if (!common_name)
    return (NULL);

  if (!path)
    path = http_default_path(defpath, sizeof(defpath));

  if ((fd = open(http_make_path(filename, sizeof(filename), path, common_name, ext), O_RDONLY)) < 0)
  {
    DEBUG_printf("4http_copy_file: open() failed: %s", strerror(errno));
    return (NULL);
  }

  if (fstat(fd, &fileinfo))
  {
    DEBUG_printf("4http_copy_file: fstat() failed: %s", strerror(errno));
    goto done;
  }

  DEBUG_printf("4http_copy_file: st_size=%lu", (unsigned long)fileinfo.st_size);

  if (fileinfo.st_size > 65536)
  {
    close(fd);
    return (NULL);
  }

  if ((s = calloc(1, (size_t)fileinfo.st_size + 1)) == NULL)
  {
    DEBUG_printf("4http_copy_file: calloc() failed: %s", strerror(errno));
    close(fd);
    return (NULL);
  }

  if (read(fd, s, (size_t)fileinfo.st_size) < 0)
  {
    DEBUG_printf("4http_copy_file: read() failed: %s", strerror(errno));
    free(s);
    s = NULL;
  }

  done:

  close(fd);

  DEBUG_printf("4http_copy_file: Returning \"%s\"", s);

  return (s);
}


//
// 'http_default_path()' - Get the default credential store path.
//

static const char *			// O - Path or NULL on error
http_default_path(
    char   *buffer,			// I - Path buffer
    size_t bufsize)			// I - Size of path buffer
{
  _cups_globals_t	*cg = _cupsGlobals();
					// Pointer to library globals


  if (cg->userconfig)
  {
    snprintf(buffer, bufsize, "%s/ssl", cg->userconfig);

    if (!_cupsDirCreate(buffer, 0700))
    {
      DEBUG_printf("4http_default_path: Failed to make directory '%s': %s", buffer, strerror(errno));
      return (NULL);
    }
  }
  else
  {
    snprintf(buffer, bufsize, "%s/ssl", cg->sysconfig);

    if (!_cupsDirCreate(buffer, 0700))
    {
      DEBUG_printf("4http_default_path: Failed to make directory '%s': %s", buffer, strerror(errno));
      return (NULL);
    }
  }

  DEBUG_printf("4http_default_path: Using default path \"%s\".", buffer);

  return (buffer);
}


//
// 'http_default_san_cb()' - Validate a subjectAltName value.
//

static bool				// O - `true` if OK, `false` otherwise
http_default_san_cb(
    const char *common_name,		// I - Common name value
    const char *subject_alt_name,	// I - subjectAltName value
    void       *data)			// I - Callback data (unused)
{
  size_t	common_len;		// Common name length


  (void)data;

  if (!_cups_strcasecmp(subject_alt_name, common_name) || !_cups_strcasecmp(subject_alt_name, "localhost"))
    return (true);

  common_len = strlen(common_name);

  return (!_cups_strncasecmp(subject_alt_name, common_name, common_len) && subject_alt_name[common_len] == '.');
}


#if defined(_WIN32) || defined(HAVE_GNUTLS)
//
// 'http_der_to_pem()' - Convert DER format certificate data to PEM.
//

static char *				// O - PEM string
http_der_to_pem(
     const unsigned char *der,		// I - DER-encoded data
     size_t              dersize)	// I - Size of DER-encoded data
{
  char		*pem,			// PEM-encoded string
		*pemptr;		// Pointer into PEM-encoded string
  int		col;			// Current column
  size_t	pemsize;		// Size of PEM-encoded string
  static const char *base64 =		// Base64 alphabet
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


  // Calculate the size, accounting for Base64 expansion, line wrapping at
  // column 64, and the BEGIN/END CERTIFICATE text...
  pemsize = 2 * dersize + /*"-----BEGIN CERTIFICATE-----\n"*/28 + /*"-----END CERTIFICATE-----\n"*/26 + 1;

  if ((pem = calloc(1, pemsize)) == NULL)
    return (NULL);

  cupsCopyString(pem, "-----BEGIN CERTIFICATE-----\n", pemsize);
  for (pemptr = pem + strlen(pem), col = 0; dersize > 0; der += 3)
  {
    // Encode the up to 3 characters as 4 Base64 numbers...
    switch (dersize)
    {
      case 1 :
          *pemptr ++ = base64[(der[0] & 255) >> 2];
	  *pemptr ++ = base64[((der[0] & 255) << 4) & 63];
	  *pemptr ++ = '=';
	  *pemptr ++ = '=';
	  dersize = 0;
          break;
      case 2 :
          *pemptr ++ = base64[(der[0] & 255) >> 2];
	  *pemptr ++ = base64[(((der[0] & 255) << 4) | ((der[1] & 255) >> 4)) & 63];
	  *pemptr ++ = base64[((der[1] & 255) << 2) & 63];
	  *pemptr ++ = '=';
	  dersize = 0;
          break;
      default :
          *pemptr ++ = base64[(der[0] & 255) >> 2];
	  *pemptr ++ = base64[(((der[0] & 255) << 4) | ((der[1] & 255) >> 4)) & 63];
	  *pemptr ++ = base64[(((der[1] & 255) << 2) | ((der[2] & 255) >> 6)) & 63];
	  *pemptr ++ = base64[der[2] & 63];
	  dersize -= 3;
          break;
    }

    // Add a newline as needed...
    col += 4;
    if (col >= 64)
    {
      *pemptr++ = '\n';
      col = 0;
    }
  }

  if (col > 0)
    *pemptr++ = '\n';
  *pemptr = '\0';

  cupsConcatString(pem, "-----END CERTIFICATE-----\n", pemsize);

  // Return the encoded string...
  return (pem);
}
#endif // _WIN32 || HAVE_GNUTLS


//
// 'http_make_path()' - Format a filename for a certificate or key file.
//

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


  DEBUG_printf("3http_make_path(buffer=%p, bufsize=%u, dirname=\"%s\", filename=\"%s\", ext=\"%s\")", (void *)buffer, (unsigned)bufsize, dirname, filename, ext);

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

  if (bufptr < bufend && filename[-1] != '.')
    *bufptr++ = '.';

  cupsCopyString(bufptr, ext, (size_t)(bufend - bufptr + 1));

  DEBUG_printf("4http_make_path: Returning \"%s\".", buffer);

  return (buffer);
}


//
// 'http_save_file()' - Save a string to a file.
//

static bool				// O - `true` on success, `false` on failure
http_save_file(const char *path,	// I - Directory path for certificate/key store or `NULL` for default
               const char *common_name,	// I - Common name
               const char *ext,		// I - Extension
	       const char *value)	// I - String value
{
  char	defpath[1024],			// Default path
	filename[1024];			// Output filename
  int	fd;				// File descriptor


  // Range check input...
  if (!common_name)
    return (false);

  // Get default path as needed...
  if (!path)
    path = http_default_path(defpath, sizeof(defpath));

  http_make_path(filename, sizeof(filename), path, common_name, ext);

  if (!value)
  {
    unlink(filename);
    return (true);
  }

  if ((fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, (strcmp(ext, "key") && strcmp(ext, "ktm")) ? 0644 : 0600)) < 0)
    return (false);

  if (write(fd, value, strlen(value)) < 0)
  {
    close(fd);
    unlink(filename);
    return (false);
  }

  close(fd);

  return (true);
}


