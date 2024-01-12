/*
 * IPP fuzzing program for CUPS.
 *
 * Copyright © 2022-2024 by OpenPrinting.
 * Copyright © 2007-2021 by Apple Inc.
 * Copyright © 1997-2005 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "file.h"
#include "string-private.h"
#include "ipp-private.h"
#include <spawn.h>
#include <sys/wait.h>
#ifdef _WIN32
#  include <io.h>
#else
#  include <unistd.h>
#  include <fcntl.h>
#endif /* _WIN32 */


/*
 * Local types...
 */

typedef struct _ippdata_t		// Data
{
  size_t	wused,			// Bytes used
		wsize;			// Max size of buffer
  ipp_uchar_t	*wbuffer;		// Buffer
} _ippdata_t;




/*
 * Local functions...
 */

void	fuzzdata(_ippdata_t *data);
void	hex_dump(ipp_uchar_t *buffer, size_t bytes);
ssize_t	write_cb(_ippdata_t *data, ipp_uchar_t *buffer, size_t bytes);


/*
 * 'main()' - Main entry.
 */

int				// O - Exit status
main(int  argc,			// I - Number of command-line arguments
     char *argv[])		// I - Command-line arguments
{
  ipp_state_t	state;		// State
  cups_file_t	*fp;		// File pointer
  ipp_t		*request;	// Request


  if (argc == 1)
  {
    // Generate a Print-Job request with all common attribute types...
    int		i;		// Looping var
    char	filename[256];	// Test filename
    const char	*fuzz_args[3];	// Arguments for sub-process
    pid_t	fuzz_pid;	// Sub-process ID
    int		fuzz_status;	// Exit status
    ipp_t	*media_col,	// media-col collection
		*media_size;	// media-size collection
    _ippdata_t	data;		// IPP buffer
    ipp_uchar_t	buffer[262144];	// Write buffer data

    request = ippNewRequest(IPP_OP_PRINT_JOB);

    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, "ipp://localhost/printers/foo");
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, "john-doe");
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "job-name", NULL, "Test Job");
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, "application/pdf");
    ippAddOctetString(request, IPP_TAG_OPERATION, "job-password", "8675309", 7);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "job-password-encryption", NULL, "none");
    ippAddString(request, IPP_TAG_JOB, IPP_TAG_KEYWORD, "print-color-mode", NULL, "color");
    ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_ENUM, "print-quality", IPP_QUALITY_HIGH);
    ippAddResolution(request, IPP_TAG_JOB, "printer-resolution", 1200, 1200, IPP_RES_PER_INCH);
    ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_INTEGER, "copies", 42);
    ippAddBoolean(request, IPP_TAG_JOB, "some-boolean-option", 1);
    ippAddString(request, IPP_TAG_JOB, IPP_TAG_URISCHEME, "some-uri-scheme", NULL, "mailto");
    ippAddString(request, IPP_TAG_JOB, IPP_TAG_NAMELANG, "some-name-with-language", "es-MX", "Jose");
    ippAddString(request, IPP_TAG_JOB, IPP_TAG_TEXTLANG, "some-text-with-language", "es-MX", "¡Hola el mundo!");
    ippAddRange(request, IPP_TAG_JOB, "page-ranges", 1, 50);
    ippAddDate(request, IPP_TAG_JOB, "job-hold-until-time", ippTimeToDate(time(NULL) + 3600));
    ippAddString(request, IPP_TAG_JOB, IPP_TAG_TEXT, "job-message-to-operator", NULL, "This is a test job.");

    media_col  = ippNew();
    media_size = ippNew();
    ippAddInteger(media_size, IPP_TAG_ZERO, IPP_TAG_INTEGER, "x-dimension", 21590);
    ippAddInteger(media_size, IPP_TAG_ZERO, IPP_TAG_INTEGER, "y-dimension", 27940);
    ippAddCollection(media_col, IPP_TAG_JOB, "media-size", media_size);
    ippDelete(media_size);
    ippAddString(media_col, IPP_TAG_JOB, IPP_TAG_KEYWORD, "media-color", NULL, "blue");
    ippAddString(media_col, IPP_TAG_JOB, IPP_TAG_KEYWORD, "media-type", NULL, "stationery");

    ippAddCollection(request, IPP_TAG_JOB, "media-col", media_col);
    ippDelete(media_col);

    data.wused   = 0;
    data.wsize   = sizeof(buffer);
    data.wbuffer = buffer;

    while ((state = ippWriteIO(&data, (ipp_iocb_t)write_cb, 1, NULL, request)) != IPP_STATE_DATA)
    {
      if (state == IPP_STATE_ERROR)
	break;
    }

    if (state != IPP_STATE_DATA)
    {
      puts("Failed to create base IPP message.");
      return (1);
    }

    ippDelete(request);

    // Now iterate 1000 times and test the fuzzed request...
    for (i = 0; i < 1000; i ++)
    {
      fuzzdata(&data);

      snprintf(filename, sizeof(filename), "fuzz-%03d.ipp", i);
      if ((fp = cupsFileOpen(filename, "w")) == NULL)
      {
        perror(filename);
        return (1);
      }

      cupsFileWrite(fp, (char *)buffer, data.wused);
      cupsFileClose(fp);

      printf("%s: ", filename);
      fflush(stdout);

      fuzz_args[0] = argv[0];
      fuzz_args[1] = filename;
      fuzz_args[2] = NULL;

      if (posix_spawn(&fuzz_pid, argv[0], NULL, NULL, (char * const *)fuzz_args, NULL))
      {
        puts("FAIL");
        perror(argv[0]);
        unlink(filename);
        return (1);
      }

      while (waitpid(fuzz_pid, &fuzz_status, 0) < 0)
      {
        if (errno != EINTR && errno != EAGAIN)
        {
          puts("FAIL");
          perror(argv[0]);
          unlink(filename);
          return (1);
        }
      }

      if (fuzz_status)
      {
        puts("FAIL");
        hex_dump(buffer, data.wused);
        unlink(filename);
        return (1);
      }

      puts("PASS");
      unlink(filename);
    }
  }
  else
  {
    // Read an IPP file...
    cups_file_t	*fp;		// File pointer

    if ((fp = cupsFileOpen(argv[1], "r")) == NULL)
    {
      perror(argv[1]);
      return (1);
    }

    request = ippNew();
    do
    {
      state = ippReadIO(fp, (ipp_iocb_t)cupsFileRead, 1, NULL, request);
    }
    while (state == IPP_STATE_ATTRIBUTE);

    cupsFileClose(fp);

    fp = cupsFileOpen("/dev/null", "w");

    ippSetState(request, IPP_STATE_IDLE);

    do
    {
      state = ippWriteIO(fp, (ipp_iocb_t)cupsFileWrite, 1, NULL, request);
    }
    while (state == IPP_STATE_ATTRIBUTE);

    cupsFileClose(fp);
    ippDelete(request);
  }

  return (0);
}


//
// 'fuzzdata()' - Mutate a buffer for fuzzing purposes...
//

void
fuzzdata(_ippdata_t *data)		// I - Data buffer
{
  int		i,			// Looping vars
		pos,			// Position in buffer
		pos2,			// Second position in buffer
		len;			// Number of bytes
  ipp_uchar_t	temp[16];		// Temporary buffer


  // Mutate a few times...
  for (i = 0; i < 32; i ++)
  {
    // Each cycle replace or swap bytes
    switch ((len = CUPS_RAND() & 7))
    {
      case 0 :
      case 1 :
      case 2 :
      case 3 :
      case 4 :
      case 5 :
      case 6 :
          // Replace bytes
          len ++;
          pos = CUPS_RAND() % (data->wused - len);
          while (len > 0)
          {
            data->wbuffer[pos ++] = CUPS_RAND();
            len --;
          }
          break;

      case 7 :
          // Swap bytes
          len  = (CUPS_RAND() & 7) + 1;
          pos  = CUPS_RAND() % (data->wused - len);
          pos2 = CUPS_RAND() % (data->wused - len);
          memmove(temp, data->wbuffer + pos, len);
          memmove(data->wbuffer + pos, data->wbuffer + pos2, len);
          memmove(data->wbuffer + pos2, temp, len);
          break;
    }
  }
}


//
// 'hex_dump()' - Produce a hex dump of a buffer.
//

void
hex_dump(ipp_uchar_t *buffer,		// I - Buffer to dump
         size_t      bytes)		// I - Number of bytes
{
  size_t	i, j;			// Looping vars
  int		ch;			// Current ASCII char


 /*
  * Show lines of 16 bytes at a time...
  */

  for (i = 0; i < bytes; i += 16)
  {
   /*
    * Show the offset...
    */

    printf("%04x ", (unsigned)i);

   /*
    * Then up to 16 bytes in hex...
    */

    for (j = 0; j < 16; j ++)
      if ((i + j) < bytes)
        printf(" %02x", buffer[i + j]);
      else
        printf("   ");

   /*
    * Then the ASCII representation of the bytes...
    */

    putchar(' ');
    putchar(' ');

    for (j = 0; j < 16 && (i + j) < bytes; j ++)
    {
      ch = buffer[i + j] & 127;

      if (ch < ' ' || ch == 127)
        putchar('.');
      else
        putchar(ch);
    }

    putchar('\n');
  }
}


/*
 * 'write_cb()' - Write data into a buffer.
 */

ssize_t					/* O - Number of bytes written */
write_cb(_ippdata_t   *data,		/* I - Data */
         ipp_uchar_t *buffer,		/* I - Buffer to write */
	 size_t      bytes)		/* I - Number of bytes to write */
{
  size_t	count;			/* Number of bytes */


 /*
  * Loop until all bytes are written...
  */

  if ((count = data->wsize - data->wused) > bytes)
    count = bytes;

  memcpy(data->wbuffer + data->wused, buffer, count);
  data->wused += count;

 /*
  * Return the number of bytes written...
  */

  return ((ssize_t)count);
}
