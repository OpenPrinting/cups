/*
 * MD5 password support for CUPS (deprecated).
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright 2007-2017 by Apple Inc.
 * Copyright 1997-2005 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more information.
 */

/*
 * Include necessary headers...
 */

#include <cups/cups.h>
#include "http-private.h"
#include "string-private.h"


/*
 * 'httpMD5()' - Compute the MD5 sum of the username:group:password.
 *
 * The function was used for HTTP Digest authentication. Since CUPS 2.4.0
 * it produces an empty string. Please use @link cupsDoAuthentication@ instead.
 *
 * @deprecated@
 */

char *					/* O - MD5 sum */
httpMD5(const char *username,		/* I - User name */
        const char *realm,		/* I - Realm name */
        const char *passwd,		/* I - Password string */
	char       md5[33])		/* O - MD5 string */
{
  (void)username;
  (void)realm;
  (void)passwd;

  md5[0] = '\0';

  return (NULL);
}


/*
 * 'httpMD5Final()' - Combine the MD5 sum of the username, group, and password
 *                    with the server-supplied nonce value, method, and
 *                    request-uri.
 *
 * The function was used for HTTP Digest authentication. Since CUPS 2.4.0
 * it produces an empty string. Please use @link cupsDoAuthentication@ instead.
 *
 * @deprecated@
 */

char *					/* O - New sum */
httpMD5Final(const char *nonce,		/* I - Server nonce value */
             const char *method,	/* I - METHOD (GET, POST, etc.) */
	     const char *resource,	/* I - Resource path */
             char       md5[33])	/* IO - MD5 sum */
{
  (void)nonce;
  (void)method;
  (void)resource;

  md5[0] = '\0';

  return (NULL);
}


/*
 * 'httpMD5String()' - Convert an MD5 sum to a character string.
 *
 * The function was used for HTTP Digest authentication. Since CUPS 2.4.0
 * it produces an empty string. Please use @link cupsDoAuthentication@ instead.
 *
 * @deprecated@
 */

char *					/* O - MD5 sum in hex */
httpMD5String(const unsigned char *sum,	/* I - MD5 sum data */
              char                md5[33])
					/* O - MD5 sum in hex */
{
  (void)sum;

  md5[0] = '\0';

  return (NULL);
}
