/*
 * Authentication certificate routines for the CUPS scheduler.
 *
 * Copyright © 2020-2026 by OpenPrinting.
 * Copyright © 2007-2016 by Apple Inc.
 * Copyright © 1997-2006 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "cupsd.h"
#ifdef HAVE_ACL_INIT
#  include <sys/acl.h>
#  ifdef HAVE_MEMBERSHIP_H
#    include <membership.h>
#  endif /* HAVE_MEMBERSHIP_H */
#endif /* HAVE_ACL_INIT */


/*
 * Local functions...
 */

static int	ctcompare(const char *a, const char *b);
static unsigned	get_rand(void);


/*
 * 'cupsdAddCert()' - Add a certificate.
 */

void
cupsdAddCert(int        pid,		/* I - Process ID */
             const char *username,	/* I - Username */
             int        type)		/* I - AuthType for username */
{
  int		i;			/* Looping var */
  cupsd_cert_t	*cert;			/* Current certificate */
  int		fd;			/* Certificate file */
  char		filename[1024];		/* Certificate filename */
  static const char hex[] = "0123456789ABCDEF";
					/* Hex constants... */


  cupsdLogMessage(CUPSD_LOG_DEBUG, "cupsdAddCert: Adding certificate for PID %d", pid);

 /*
  * Allocate memory for the certificate...
  */

  if ((cert = calloc(1, sizeof(cupsd_cert_t))) == NULL)
    return;

 /*
  * Fill in the certificate information...
  */

  cert->pid  = pid;
  cert->type = type;
  strlcpy(cert->username, username, sizeof(cert->username));

  for (i = 0; i < 32; i ++)
    cert->certificate[i] = hex[get_rand() & 15];

 /*
  * Save the certificate to a file readable only by the User and Group
  * (or root and SystemGroup for PID == 0)...
  */

  snprintf(filename, sizeof(filename), "%s/certs/%d", StateDir, pid);
  unlink(filename);

  if ((fd = open(filename, O_WRONLY | O_CREAT | O_EXCL, 0400)) < 0)
  {
    cupsdLogMessage(CUPSD_LOG_ERROR,
                    "Unable to create certificate file %s - %s",
                    filename, strerror(errno));
    free(cert);
    return;
  }

  if (pid == 0)
  {
#if defined(HAVE_ACL_INIT) && !CUPS_SNAP
    acl_t		acl;		/* ACL information */
    acl_entry_t		entry;		/* ACL entry */
    acl_permset_t	permset;	/* Permissions */
#  ifdef HAVE_MBR_UID_TO_UUID
    uuid_t		group;		/* Group ID */
#  endif /* HAVE_MBR_UID_TO_UUID */
    static int		acls_not_supported = 0;
					/* Only warn once */
#endif /* HAVE_ACL_INIT && !CUPS_SNAP */


   /*
    * Root certificate...
    */

    fchmod(fd, 0440);

    /* ACLs do not work when cupsd is running in a Snap, and certificates
       need root as group owner to be only accessible for CUPS and not the
       unprivileged sub-processes */
#if CUPS_SNAP
    fchown(fd, RunUser, 0);
#else
    fchown(fd, RunUser, SystemGroupIDs[0]);

    cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdAddCert: NumSystemGroups=%d", NumSystemGroups);

#  ifdef HAVE_ACL_INIT
    if (NumSystemGroups > 1)
    {
     /*
      * Set POSIX ACLs for the root certificate so that all system
      * groups can access it...
      */

      int	j;			/* Looping var */

#    ifdef HAVE_MBR_UID_TO_UUID
     /*
      * On macOS, ACLs use UUIDs instead of GIDs...
      */

      acl = acl_init(NumSystemGroups - 1);

      for (i = 1; i < NumSystemGroups; i ++)
      {
       /*
        * Add each group ID to the ACL...
	*/

        for (j = 0; j < i; j ++)
	  if (SystemGroupIDs[j] == SystemGroupIDs[i])
            break;

        if (j < i)
          continue;			/* Skip duplicate groups */

        acl_create_entry(&acl, &entry);
	acl_get_permset(entry, &permset);
	acl_add_perm(permset, ACL_READ_DATA);
	acl_set_tag_type(entry, ACL_EXTENDED_ALLOW);
	mbr_gid_to_uuid((gid_t)SystemGroupIDs[i], group);
	acl_set_qualifier(entry, &group);
	acl_set_permset(entry, permset);
      }

#    else
     /*
      * POSIX ACLs need permissions for owner, group, other, and mask
      * in addition to the rest of the system groups...
      */

      acl = acl_init(NumSystemGroups + 3);

      /* Owner */
      acl_create_entry(&acl, &entry);
      acl_get_permset(entry, &permset);
      acl_add_perm(permset, ACL_READ);
      acl_set_tag_type(entry, ACL_USER_OBJ);
      acl_set_permset(entry, permset);

      /* Group */
      acl_create_entry(&acl, &entry);
      acl_get_permset(entry, &permset);
      acl_add_perm(permset, ACL_READ);
      acl_set_tag_type(entry, ACL_GROUP_OBJ);
      acl_set_permset(entry, permset);

      /* Others */
      acl_create_entry(&acl, &entry);
      acl_get_permset(entry, &permset);
      acl_add_perm(permset, 0);
      acl_set_tag_type(entry, ACL_OTHER);
      acl_set_permset(entry, permset);

      /* Mask */
      acl_create_entry(&acl, &entry);
      acl_get_permset(entry, &permset);
      acl_add_perm(permset, ACL_READ);
      acl_set_tag_type(entry, ACL_MASK);
      acl_set_permset(entry, permset);

      for (i = 1; i < NumSystemGroups; i ++)
      {
       /*
        * Add each group ID to the ACL...
	*/

        for (j = 0; j < i; j ++)
	  if (SystemGroupIDs[j] == SystemGroupIDs[i])
            break;

        if (j < i)
          continue;			/* Skip duplicate groups */

        acl_create_entry(&acl, &entry);
	acl_get_permset(entry, &permset);
	acl_add_perm(permset, ACL_READ);
	acl_set_tag_type(entry, ACL_GROUP);
	acl_set_qualifier(entry, SystemGroupIDs + i);
	acl_set_permset(entry, permset);
      }

      if (acl_valid(acl))
      {
        char *text, *textptr;		/* Temporary string */

        cupsdLogMessage(CUPSD_LOG_ERROR, "ACL did not validate: %s",
	                strerror(errno));
        text = acl_to_text(acl, NULL);
	for (textptr = strchr(text, '\n');
	     textptr;
	     textptr = strchr(textptr + 1, '\n'))
	  *textptr = ',';

	cupsdLogMessage(CUPSD_LOG_ERROR, "ACL: %s", text);
	acl_free(text);
      }
#    endif /* HAVE_MBR_UID_TO_UUID */

      if (acl_set_fd(fd, acl))
      {
	if (errno != EOPNOTSUPP || !acls_not_supported)
	  cupsdLogMessage(CUPSD_LOG_ERROR,
			  "Unable to set ACLs on root certificate \"%s\" - %s",
			  filename, strerror(errno));

	if (errno == EOPNOTSUPP)
	  acls_not_supported = 1;
      }

      acl_free(acl);
    }
#  endif /* HAVE_ACL_INIT */
#endif /* CUPS_SNAP */

    RootCertTime = time(NULL);
  }
  else
  {
   /*
    * CGI certificate...
    */

    fchmod(fd, 0400);
    fchown(fd, User, Group);
  }

  write(fd, cert->certificate, strlen(cert->certificate));
  close(fd);

 /*
  * Insert the certificate at the front of the list...
  */

  cert->next = Certs;
  Certs      = cert;
}


/*
 * 'cupsdDeleteCert()' - Delete a single certificate.
 */

void
cupsdDeleteCert(int pid)		/* I - Process ID */
{
  cupsd_cert_t	*cert,			/* Current certificate */
		*prev;			/* Previous certificate */
  char		filename[1024];		/* Certificate file */


  for (prev = NULL, cert = Certs; cert != NULL; prev = cert, cert = cert->next)
    if (cert->pid == pid)
    {
     /*
      * Remove this certificate from the list...
      */

      cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdDeleteCert: Removing certificate for PID %d.", pid);

      if (prev == NULL)
        Certs = cert->next;
      else
        prev->next = cert->next;

      free(cert);

     /*
      * Delete the file and return...
      */

      snprintf(filename, sizeof(filename), "%s/certs/%d", StateDir, pid);
      if (unlink(filename))
	cupsdLogMessage(CUPSD_LOG_ERROR, "Unable to remove %s!", filename);

      return;
    }
}


/*
 * 'cupsdDeleteAllCerts()' - Delete all certificates...
 */

void
cupsdDeleteAllCerts(void)
{
  cupsd_cert_t	*cert,			/* Current certificate */
		*next;			/* Next certificate */
  char		filename[1024];		/* Certificate file */


 /*
  * Loop through each certificate, deleting them...
  */

  for (cert = Certs; cert != NULL; cert = next)
  {
   /*
    * Delete the file...
    */

    snprintf(filename, sizeof(filename), "%s/certs/%d", StateDir, cert->pid);
    if (unlink(filename))
      cupsdLogMessage(CUPSD_LOG_ERROR, "Unable to remove %s!", filename);

   /*
    * Free memory...
    */

    next = cert->next;
    free(cert);
  }

  Certs        = NULL;
  RootCertTime = 0;
}


/*
 * 'cupsdFindCert()' - Find a certificate.
 */

cupsd_cert_t *				/* O - Matching certificate or NULL */
cupsdFindCert(const char *certificate)	/* I - Certificate */
{
  cupsd_cert_t	*cert;			/* Current certificate */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdFindCert(certificate=%s)", certificate);
  for (cert = Certs; cert != NULL; cert = cert->next)
    if (!ctcompare(certificate, cert->certificate))
    {
      cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdFindCert: Returning \"%s\".", cert->username);
      return (cert);
    }

  cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdFindCert: Certificate not found.");

  return (NULL);
}


/*
 * 'cupsdInitCerts()' - Initialize the certificate "system" and root
 *                      certificate.
 */

void
cupsdInitCerts(void)
{
 /*
  * Create a root certificate and return...
  */

  if (!RunUser)
    cupsdAddCert(0, "root", cupsdDefaultAuthType());
}


/*
 * 'ctcompare()' - Compare two strings in constant time.
 */

static int				/* O - 0 on match, non-zero on non-match */
ctcompare(const char *a,		/* I - First string */
          const char *b)		/* I - Second string */
{
  int	result = 0;			/* Result */


  while (*a && *b)
  {
    result |= *a ^ *b;
    a ++;
    b ++;
  }

 /*
  * The while loop finishes when *a == '\0' or *b == '\0'
  * so after the while loop either both *a and *b == '\0',
  * or one points inside a string, so when we apply bitwise OR on *a,
  * *b and result, we get a non-zero return value if the compared strings don't match.
  */

  return (result | *a | *b);
}


//
// 'get_rand()' - Return a 32-bit pseudo-random number.
//
// This function returns a 32-bit pseudo-random number suitable for use as
// one-time identifiers or nonces.  The random numbers are generated/seeded
// using system entropy.
//

static unsigned				// O - Random number
get_rand(void)
{
#ifdef __APPLE__
  // macOS/iOS arc4random() uses real entropy automatically...
  return (arc4random());

#else
  // Use a Mersenne twister random number generator seeded from /dev/urandom...
  unsigned	i,			// Looping var
		temp;			// Temporary value
  static int	first_time = 1;		// First time we ran?
  static unsigned mt_state[624],	// Mersenne twister state
		mt_index;		// Mersenne twister index


  if (first_time)
  {
    int		fd;			// "/dev/urandom" file
    struct timeval curtime;		// Current time

    // Seed the random number state...
    if ((fd = open("/dev/urandom", O_RDONLY)) >= 0)
    {
      // Read random entropy from the system...
      if (read(fd, mt_state, sizeof(mt_state[0])) < sizeof(mt_state[0]))
        mt_state[0] = 0;		// Force fallback...

      close(fd);
    }
    else
      mt_state[0] = 0;

    if (!mt_state[0])
    {
      // Fallback to using the current time in microseconds...
      gettimeofday(&curtime, NULL);
      mt_state[0] = (unsigned)(curtime.tv_sec + curtime.tv_usec);
    }

    mt_index = 0;

    for (i = 1; i < 624; i ++)
      mt_state[i] = (unsigned)((1812433253 * (mt_state[i - 1] ^ (mt_state[i - 1] >> 30))) + i);

    first_time = 0;
  }

  if (mt_index == 0)
  {
    // Generate a sequence of random numbers...
    unsigned i1 = 1, i397 = 397;	// Looping vars

    for (i = 0; i < 624; i ++)
    {
      temp        = (mt_state[i] & 0x80000000) + (mt_state[i1] & 0x7fffffff);
      mt_state[i] = mt_state[i397] ^ (temp >> 1);

      if (temp & 1)
	mt_state[i] ^= 2567483615u;

      i1 ++;
      i397 ++;

      if (i1 == 624)
	i1 = 0;

      if (i397 == 624)
	i397 = 0;
    }
  }

  // Pull 32-bits of random data...
  temp = mt_state[mt_index ++];
  temp ^= temp >> 11;
  temp ^= (temp << 7) & 2636928640u;
  temp ^= (temp << 15) & 4022730752u;
  temp ^= temp >> 18;

  if (mt_index == 624)
    mt_index = 0;

  return (temp);
#endif // __APPLE__
}
