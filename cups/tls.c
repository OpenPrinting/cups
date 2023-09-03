/*
 * TLS routines for CUPS.
 *
 * Copyright © 2021-2023 by OpenPrinting.
 * Copyright @ 2007-2014 by Apple Inc.
 * Copyright @ 1997-2007 by Easy Software Products, all rights reserved.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "cups-private.h"
#include "debug-internal.h"
#include <fcntl.h>
#include <math.h>
#ifdef _WIN32
#  include <tchar.h>
#else
#  include <signal.h>
#  include <sys/time.h>
#  include <sys/resource.h>
#endif /* _WIN32 */
#ifdef HAVE_POLL
#  include <poll.h>
#endif /* HAVE_POLL */


/*
 * Include platform-specific TLS code...
 */

#ifdef HAVE_OPENSSL
#  include "tls-openssl.c"
#else /* HAVE_GNUTLS */
#  include "tls-gnutls.c"
#endif /* HAVE_OPENSSL */
