/*
 * Directory routines for CUPS.
 *
 * This set of APIs abstracts enumeration of directory entries.
 *
 * Copyright © 2022-2024 by OpenPrinting.
 * Copyright © 2007-2021 by Apple Inc.
 * Copyright © 1997-2005 by Easy Software Products, all rights reserved.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "string-private.h"
#include "debug-internal.h"
#include "dir.h"


/*
 * Windows implementation...
 */

#ifdef _WIN32
#  include <windows.h>

/*
 * Types and structures...
 */

struct _cups_dir_s			/**** Directory data structure ****/
{
  char		directory[1024];	/* Directory filename */
  HANDLE	dir;			/* Directory handle */
  cups_dentry_t	entry;			/* Directory entry */
};


/*
 * '_cups_dir_time()' - Convert a FILETIME value to a UNIX time value.
 */

time_t					/* O - UNIX time */
_cups_dir_time(FILETIME ft)		/* I - File time */
{
  ULONGLONG	val;			/* File time in 0.1 usecs */


 /*
  * Convert file time (1/10 microseconds since Jan 1, 1601) to UNIX
  * time (seconds since Jan 1, 1970).  There are 11,644,732,800 seconds
  * between them...
  */

  val = ft.dwLowDateTime + ((ULONGLONG)ft.dwHighDateTime << 32);
  return ((time_t)(val / 10000000 - 11644732800));
}


/*
 * 'cupsDirClose()' - Close a directory.
 *
 * @since CUPS 1.2/macOS 10.5@
 */

void
cupsDirClose(cups_dir_t *dp)		/* I - Directory pointer */
{
 /*
  * Range check input...
  */

  if (!dp)
    return;

 /*
  * Close an open directory handle...
  */

  if (dp->dir != INVALID_HANDLE_VALUE)
    FindClose(dp->dir);

 /*
  * Free memory used...
  */

  free(dp);
}


/*
 * 'cupsDirOpen()' - Open a directory.
 *
 * @since CUPS 1.2/macOS 10.5@
 */

cups_dir_t *				/* O - Directory pointer or @code NULL@ if the directory could not be opened. */
cupsDirOpen(const char *directory)	/* I - Directory name */
{
  cups_dir_t	*dp;			/* Directory */


 /*
  * Range check input...
  */

  if (!directory)
    return (NULL);

 /*
  * Allocate memory for the directory structure...
  */

  dp = (cups_dir_t *)calloc(1, sizeof(cups_dir_t));
  if (!dp)
    return (NULL);

 /*
  * Copy the directory name for later use...
  */

  dp->dir = INVALID_HANDLE_VALUE;

  snprintf(dp->directory, sizeof(dp->directory), "%s\\*",  directory);

 /*
  * Return the new directory structure...
  */

  return (dp);
}


/*
 * 'cupsDirRead()' - Read the next directory entry.
 *
 * @since CUPS 1.2/macOS 10.5@
 */

cups_dentry_t *				/* O - Directory entry or @code NULL@ if there are no more */
cupsDirRead(cups_dir_t *dp)		/* I - Directory pointer */
{
  WIN32_FIND_DATAA	entry;		/* Directory entry data */


 /*
  * Range check input...
  */

  if (!dp)
    return (NULL);

 /*
  * See if we have already started finding files...
  */

  if (dp->dir == INVALID_HANDLE_VALUE)
  {
   /*
    * No, find the first file...
    */

    dp->dir = FindFirstFileA(dp->directory, &entry);
    if (dp->dir == INVALID_HANDLE_VALUE)
      return (NULL);
  }
  else if (!FindNextFileA(dp->dir, &entry))
    return (NULL);

 /*
  * Loop until we have something other than "." or ".."...
  */

  while (!strcmp(entry.cFileName, ".") || !strcmp(entry.cFileName, ".."))
  {
    if (!FindNextFileA(dp->dir, &entry))
      return (NULL);
  }

 /*
  * Copy the name over and convert the file information...
  */

  strlcpy(dp->entry.filename, entry.cFileName, sizeof(dp->entry.filename));

  if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    dp->entry.fileinfo.st_mode = 0755 | S_IFDIR;
  else
    dp->entry.fileinfo.st_mode = 0644 | S_IFREG;

  dp->entry.fileinfo.st_atime = _cups_dir_time(entry.ftLastAccessTime);
  dp->entry.fileinfo.st_ctime = _cups_dir_time(entry.ftCreationTime);
  dp->entry.fileinfo.st_mtime = _cups_dir_time(entry.ftLastWriteTime);
  dp->entry.fileinfo.st_size  = entry.nFileSizeLow + ((unsigned long long)entry.nFileSizeHigh << 32);

 /*
  * Return the entry...
  */

  return (&(dp->entry));
}


/*
 * 'cupsDirRewind()' - Rewind to the start of the directory.
 *
 * @since CUPS 1.2/macOS 10.5@
 */

void
cupsDirRewind(cups_dir_t *dp)		/* I - Directory pointer */
{
 /*
  * Range check input...
  */

  if (!dp)
    return;

 /*
  * Close an open directory handle...
  */

  if (dp->dir != INVALID_HANDLE_VALUE)
  {
    FindClose(dp->dir);
    dp->dir = INVALID_HANDLE_VALUE;
  }
}


#else

/*
 * POSIX implementation...
 */

#  include <sys/types.h>
#  include <dirent.h>


/*
 * Types and structures...
 */

struct _cups_dir_s			/**** Directory data structure ****/
{
  char		directory[1024];	/* Directory filename */
  DIR		*dir;			/* Directory file */
  cups_dentry_t	entry;			/* Directory entry */
};


/*
 * 'cupsDirClose()' - Close a directory.
 *
 * @since CUPS 1.2/macOS 10.5@
 */

void
cupsDirClose(cups_dir_t *dp)		/* I - Directory pointer */
{
  DEBUG_printf(("cupsDirClose(dp=%p)", (void *)dp));

 /*
  * Range check input...
  */

  if (!dp)
    return;

 /*
  * Close the directory and free memory...
  */

  closedir(dp->dir);
  free(dp);
}


/*
 * 'cupsDirOpen()' - Open a directory.
 *
 * @since CUPS 1.2/macOS 10.5@
 */

cups_dir_t *				/* O - Directory pointer or @code NULL@ if the directory could not be opened. */
cupsDirOpen(const char *directory)	/* I - Directory name */
{
  cups_dir_t	*dp;			/* Directory */


  DEBUG_printf(("cupsDirOpen(directory=\"%s\")", directory));

 /*
  * Range check input...
  */

  if (!directory)
    return (NULL);

 /*
  * Allocate memory for the directory structure...
  */

  dp = (cups_dir_t *)calloc(1, sizeof(cups_dir_t));
  if (!dp)
    return (NULL);

 /*
  * Open the directory...
  */

  dp->dir = opendir(directory);
  if (!dp->dir)
  {
    free(dp);
    return (NULL);
  }

 /*
  * Copy the directory name for later use...
  */

  strlcpy(dp->directory, directory, sizeof(dp->directory));

 /*
  * Return the new directory structure...
  */

  return (dp);
}


/*
 * 'cupsDirRead()' - Read the next directory entry.
 *
 * @since CUPS 1.2/macOS 10.5@
 */

cups_dentry_t *				/* O - Directory entry or @code NULL@ when there are no more */
cupsDirRead(cups_dir_t *dp)		/* I - Directory pointer */
{
  struct dirent	*entry;			/* Pointer to entry */
  char		filename[1024];		/* Full filename */


  DEBUG_printf(("2cupsDirRead(dp=%p)", (void *)dp));

 /*
  * Range check input...
  */

  if (!dp)
    return (NULL);

 /*
  * Try reading an entry that is not "." or ".."...
  */

  for (;;)
  {
   /*
    * Read the next entry...
    */

    if ((entry = readdir(dp->dir)) == NULL)
    {
      DEBUG_puts("3cupsDirRead: readdir() returned a NULL pointer!");
      return (NULL);
    }

    DEBUG_printf(("4cupsDirRead: readdir() returned \"%s\"...", entry->d_name));

   /*
    * Skip "." and ".."...
    */

    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
      continue;

   /*
    * Copy the name over and get the file information...
    */

    strlcpy(dp->entry.filename, entry->d_name, sizeof(dp->entry.filename));

    snprintf(filename, sizeof(filename), "%s/%s", dp->directory, entry->d_name);

    if (stat(filename, &(dp->entry.fileinfo)))
    {
      DEBUG_printf(("3cupsDirRead: stat() failed for \"%s\" - %s...", filename,
                    strerror(errno)));
      continue;
    }

   /*
    * Return the entry...
    */

    return (&(dp->entry));
  }
}


/*
 * 'cupsDirRewind()' - Rewind to the start of the directory.
 *
 * @since CUPS 1.2/macOS 10.5@
 */

void
cupsDirRewind(cups_dir_t *dp)		/* I - Directory pointer */
{
  DEBUG_printf(("cupsDirRewind(dp=%p)", (void *)dp));

 /*
  * Range check input...
  */

  if (!dp)
    return;

 /*
  * Rewind the directory...
  */

  rewinddir(dp->dir);
}
#endif /* _WIN32 */
