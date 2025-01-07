//
// File/directory test program for CUPS.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright © 2007-2018 by Apple Inc.
// Copyright © 1997-2007 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "string-private.h"
#include "debug-private.h"
#include "cups.h"
#include "file.h"
#include "dir.h"
#include "test-internal.h"
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
#  include <io.h>
#else
#  include <unistd.h>
#endif // _WIN32
#include <fcntl.h>


//
// Local functions...
//

static int	count_lines(cups_file_t *fp);
static int	random_tests(void);
static int	read_write_tests(bool compression);


//
// 'main()' - Main entry.
//

int					// O - Exit status
main(int  argc,				// I - Number of command-line arguments
     char *argv[])			// I - Command-line arguments
{
  int		status;			// Exit status
  int		i;			// Looping var
  char		filename[1024];		// Filename buffer
  cups_file_t	*fp;			// File pointer
#ifndef _WIN32
  int		fds[2];			// Open file descriptors
  cups_file_t	*fdfile;		// File opened with cupsFileOpenFd()
#endif // !_WIN32
  int		count;			// Number of lines in file


  if (argc == 1)
  {
    // Do uncompressed file tests...
    status = read_write_tests(false);

    // Do compressed file tests...
    status += read_write_tests(true);

    // Do uncompressed random I/O tests...
    status += random_tests();

#ifndef _WIN32
    // Test fdopen and close without reading...
    pipe(fds);
    close(fds[1]);

    testBegin("cupsFileOpenFd(fd, \"r\")");

    if ((fdfile = cupsFileOpenFd(fds[0], "r")) == NULL)
    {
      testEnd(false);
      status ++;
    }
    else
    {
      // Able to open file, now close without reading.  If we don't return
      // before the alarm fires, that is a failure and we will crash on the
      // alarm signal...
      testEnd(true);
      testBegin("cupsFileClose(no read)");

      alarm(5);
      cupsFileClose(fdfile);
      alarm(0);

      testEnd(true);
    }
#endif // !_WIN32

    // Count lines in test file, rewind, then count again.
    testBegin("cupsFileOpen(\"testfile.txt\", \"r\")");

    if ((fp = cupsFileOpen("testfile.txt", "r")) == NULL)
    {
      testEnd(false);
      status ++;
    }
    else
    {
      testEnd(true);
      testBegin("cupsFileGets");

      if ((count = count_lines(fp)) != 477)
      {
        testEndMessage(false, "got %d lines, expected 477", count);
	status ++;
      }
      else
      {
        testEnd(true);
	testBegin("cupsFileRewind");

	if (cupsFileRewind(fp) != 0)
	{
	  testEnd(false);
	  status ++;
	}
	else
	{
	  testEnd(true);
	  testBegin("cupsFileGets");

	  if ((count = count_lines(fp)) != 477)
	  {
	    testEndMessage(false, "got %d lines, expected 477", count);
	    status ++;
	  }
	  else
	  {
	    testEnd(true);
	  }
        }
      }

      cupsFileClose(fp);
    }

    // Test path functions...
    testBegin("cupsFileFind");
#ifdef _WIN32
    if (cupsFileFind("notepad.exe", "C:/WINDOWS", 1, filename, sizeof(filename)) && cupsFileFind("notepad.exe", "C:/WINDOWS;C:/WINDOWS/SYSTEM32", 1, filename, sizeof(filename)))
#else
    if (cupsFileFind("cat", "/bin", 1, filename, sizeof(filename)) && cupsFileFind("cat", "/bin:/usr/bin", 1, filename, sizeof(filename)))
#endif // _WIN32
    {
      testEndMessage(true, "%s", filename);
    }
    else
    {
      testEnd(false);
      status ++;
    }

    // Test directory functions...
    testBegin("mkdir(\"test.d\")");
    if (mkdir("test.d", 0777))
    {
      testEndMessage(false, "%s", strerror(errno));
      status ++;
    }
    else
    {
      int		num_files;	// Number of files seen
      cups_dir_t	*dir;		// Directory pointer
      cups_dentry_t	*dent;		// Directory entry

      testEnd(true);

      testBegin("cupsDirOpen(test.d)");
      if ((dir = cupsDirOpen("test.d")) == NULL)
      {
        testEndMessage(false, "%s", strerror(errno));
        status ++;
      }
      else
      {
        testEnd(true);
        testBegin("cupsDirRead");
        if ((dent = cupsDirRead(dir)) != NULL)
        {
          testEndMessage(false, "Got '%s', expected NULL", dent->filename);
          status ++;
        }
        else
        {
          testEnd(true);
	}

        cupsDirClose(dir);
      }

      // Create some files...
      for (i = 0; i < 10; i ++)
      {
        snprintf(filename, sizeof(filename), "test.d/testfile%d.txt", i);
        testBegin("cupsFileOpen(%s)", filename);
        if ((fp = cupsFileOpen(filename, "w")) == NULL)
        {
          testEndMessage(false, "%s", strerror(errno));
          status ++;
          break;
        }
        else
        {
          testEnd(true);
          cupsFilePuts(fp, "This is a test.\n");
          cupsFileClose(fp);
        }
      }

      if (i >= 10)
      {
	testBegin("cupsDirOpen(test.d)");
	if ((dir = cupsDirOpen("test.d")) == NULL)
	{
	  testEndMessage(false, "%s", strerror(errno));
	  status ++;
	}
	else
	{
	  testEnd(true);
	  testBegin("cupsDirRead");
	  for (num_files = 0; (dent = cupsDirRead(dir)) != NULL; num_files ++)
	    testMessage("Got '%s'...", dent->filename);

	  if (num_files != 10)
	  {
	    testEndMessage(false, "Got %d files, expected 10", num_files);
	    status ++;
	  }
	  else
	  {
	    testEnd(true);
	  }

	  cupsDirClose(dir);
	}
      }

      // Cleanup
      for (i = 0; i < 10; i ++)
      {
        snprintf(filename, sizeof(filename), "test.d/testfile%d.txt", i);
        unlink(filename);
      }

      rmdir("test.d");
    }
  }
  else
  {
    // Cat the filename on the command-line...
    char	line[8192];		// Line from file

    if ((fp = cupsFileOpen(argv[1], "r")) == NULL)
    {
      perror(argv[1]);
      status = 1;
    }
    else if (argc == 2)
    {
      status = 0;

      while (cupsFileGets(fp, line, sizeof(line)))
        puts(line);

      if (!cupsFileEOF(fp))
        perror(argv[1]);

      cupsFileClose(fp);
    }
    else
    {
      status = 0;
      ssize_t bytes;

      while ((bytes = cupsFileRead(fp, line, sizeof(line))) > 0)
        printf("%s: %d bytes\n", argv[1], (int)bytes);

      if (cupsFileEOF(fp))
        printf("%s: EOF\n", argv[1]);
      else
        perror(argv[1]);

      cupsFileClose(fp);
    }
  }

  return (status);
}


//
// 'count_lines()' - Count the number of lines in a file.
//

static int				// O - Number of lines
count_lines(cups_file_t *fp)		// I - File to read from
{
  int	count = 0;			// Number of lines
  char	line[1024];			// Line buffer


  while (cupsFileGets(fp, line, sizeof(line)))
    count ++;

  return (count);
}


//
// 'random_tests()' - Do random access tests.
//

static int				// O - Status
random_tests(void)
{
  int		status,			// Status of tests
		pass,			// Current pass
		count,			// Number of records read
		record,			// Current record
		num_records;		// Number of records
  off_t		pos;			// Position in file
  ssize_t	expected;		// Expected position in file
  cups_file_t	*fp;			// File
  char		buffer[512];		// Data buffer


  // Run 4 passes, each time appending to a data file and then reopening the
  // file for reading to validate random records in the file.
  for (status = 0, pass = 0; pass < 4; pass ++)
  {
    // cupsFileOpen(append)
    testBegin("cupsFileOpen(append %d)", pass);

    if ((fp = cupsFileOpen("testfile.dat", "a")) == NULL)
    {
      testEndMessage(false, "%s", strerror(errno));
      status ++;
      break;
    }
    else
    {
      testEnd(true);
    }

    // cupsFileTell()
    expected = 256 * (ssize_t)sizeof(buffer) * pass;

    testBegin("cupsFileTell()");
    if ((pos = cupsFileTell(fp)) != (off_t)expected)
    {
      testEndMessage(false, "" CUPS_LLFMT " instead of " CUPS_LLFMT "",
	     CUPS_LLCAST pos, CUPS_LLCAST expected);
      status ++;
      break;
    }
    else
    {
      testEnd(true);
    }

    // cupsFileWrite()
    testBegin("cupsFileWrite(256 512-byte records)");
    for (record = 0; record < 256; record ++)
    {
      memset(buffer, record, sizeof(buffer));
      if (!cupsFileWrite(fp, buffer, sizeof(buffer)))
        break;
    }

    if (record < 256)
    {
      testEndMessage(false, "%d: %s", record, strerror(errno));
      status ++;
      break;
    }
    else
    {
      testEnd(true);
    }

    // cupsFileTell()
    expected += 256 * (ssize_t)sizeof(buffer);

    testBegin("cupsFileTell()");
    if ((pos = cupsFileTell(fp)) != (off_t)expected)
    {
      testEndMessage(false, "" CUPS_LLFMT " instead of " CUPS_LLFMT "",
             CUPS_LLCAST pos, CUPS_LLCAST expected);
      status ++;
      break;
    }
    else
    {
      testEnd(true);
    }

    cupsFileClose(fp);

    // cupsFileOpen(read)
    testBegin("cupsFileOpen(read %d)", pass);

    if ((fp = cupsFileOpen("testfile.dat", "r")) == NULL)
    {
      testEndMessage(false, "%s", strerror(errno));
      status ++;
      break;
    }
    else
    {
      testEnd(true);
    }

    // cupsFileSeek, cupsFileRead
    testBegin("cupsFileSeek(), cupsFileRead()");

    for (num_records = (pass + 1) * 256, count = (pass + 1) * 256, record = ((int)cupsGetRand() & 65535) % num_records;
         count > 0;
	 count --, record = (record + ((int)cupsGetRand() & 31) - 16 + num_records) % num_records)
    {
      // The last record is always the first...
      if (count == 1)
        record = 0;

      // Try reading the data for the specified record, and validate the contents...
      expected = (ssize_t)sizeof(buffer) * record;

      if ((pos = cupsFileSeek(fp, expected)) != expected)
      {
        testEndMessage(false, "" CUPS_LLFMT " instead of " CUPS_LLFMT "",
	       CUPS_LLCAST pos, CUPS_LLCAST expected);
        status ++;
	break;
      }
      else
      {
	if (cupsFileRead(fp, buffer, sizeof(buffer)) != sizeof(buffer))
	{
	  testEndMessage(false, "%s", strerror(errno));
	  status ++;
	  break;
	}
	else if ((buffer[0] & 255) != (record & 255) ||
	         memcmp(buffer, buffer + 1, sizeof(buffer) - 1))
	{
	  testEndMessage(false, "Bad Data - %d instead of %d", buffer[0] & 255,
	         record & 255);
	  status ++;
	  break;
	}
      }
    }

    if (count == 0)
      testEnd(true);

    cupsFileClose(fp);
  }

  // Remove the test file...
  unlink("testfile.dat");

  // Return the test status...
  return (status);
}


//
// 'read_write_tests()' - Perform read/write tests.
//

static int				// O - Status
read_write_tests(bool compression)	// I - Use compression?
{
  int		i, j;			// Looping vars
  cups_file_t	*fp;			// File
  int		status;			// Exit status
  char		line[1024],		// Line from file
		*value;			// Directive value from line
  int		linenum;		// Line number
  unsigned char	readbuf[8192],		// Read buffer
		writebuf[8192];		// Write buffer
  int		byte;			// Byte from file
  ssize_t	bytes;			// Number of bytes read/written
  off_t		length;			// Length of file
  static const char *partial_line = "partial line";
					// Partial line


  // No errors so far...
  status = 0;

  // Initialize the write buffer with random data...
  for (i = 0; i < (int)sizeof(writebuf); i ++)
    writebuf[i] = (unsigned char)cupsGetRand();

  // cupsFileOpen(write)
  testBegin("cupsFileOpen(write%s)", compression ? " compressed" : "");

  fp = cupsFileOpen(compression ? "testfile.dat.gz" : "testfile.dat", compression ? "w9" : "w");
  if (fp)
  {
    testEnd(true);

    // cupsFileIsCompressed()
    testBegin("cupsFileIsCompressed()");

    if (cupsFileIsCompressed(fp) == compression)
    {
      testEnd(true);
    }
    else
    {
      testEndMessage(false, "Got %s, expected %s", cupsFileIsCompressed(fp) ? "true" : "false", compression ? "true" : "false");
      status ++;
    }

    // cupsFilePuts()
    testBegin("cupsFilePuts()");

    if (cupsFilePuts(fp, "# Hello, World\n") > 0)
    {
      testEnd(true);
    }
    else
    {
      testEndMessage(false, "%s", strerror(errno));
      status ++;
    }

    // cupsFilePrintf()
    testBegin("cupsFilePrintf()");

    for (i = 0; i < 1000; i ++)
    {
      if (!cupsFilePrintf(fp, "TestLine %03d\n", i))
        break;
    }

    if (i >= 1000)
    {
      testEnd(true);
    }
    else
    {
      testEndMessage(false, "%s", strerror(errno));
      status ++;
    }

    // cupsFilePutChar()
    testBegin("cupsFilePutChar()");

    for (i = 0; i < 256; i ++)
    {
      if (cupsFilePutChar(fp, i))
        break;
    }

    if (i >= 256)
    {
      testEnd(true);
    }
    else
    {
      testEndMessage(false, "%s", strerror(errno));
      status ++;
    }

    // cupsFileWrite()
    testBegin("cupsFileWrite()");

    for (i = 0; i < 10000; i ++)
    {
      if (!cupsFileWrite(fp, (char *)writebuf, sizeof(writebuf)))
        break;
    }

    if (i >= 10000)
    {
      testEnd(true);
    }
    else
    {
      testEndMessage(false, "%s", strerror(errno));
      status ++;
    }

    // cupsFilePuts() with partial line...
    testBegin("cupsFilePuts(\"partial line\")");

    if (cupsFilePuts(fp, partial_line))
    {
      testEnd(true);
    }
    else
    {
      testEndMessage(false, "%s", strerror(errno));
      status ++;
    }

    // cupsFileTell()
    testBegin("cupsFileTell()");

    if ((length = cupsFileTell(fp)) == 81933283)
    {
      testEnd(true);
    }
    else
    {
      testEndMessage(false, "" CUPS_LLFMT " instead of 81933283", CUPS_LLCAST length);
      status ++;
    }

    // cupsFileClose()
    testBegin("cupsFileClose()");

    if (!cupsFileClose(fp))
    {
      testEnd(true);
    }
    else
    {
      testEndMessage(false, "%s", strerror(errno));
      status ++;
    }
  }
  else
  {
    testEndMessage(false, "%s", strerror(errno));
    status ++;
  }

  // cupsFileOpen(read)
  testBegin("cupsFileOpen(read)");

  fp = cupsFileOpen(compression ? "testfile.dat.gz" : "testfile.dat", "r");
  if (fp)
  {
    testEnd(true);

    // cupsFileGets()
    testBegin("cupsFileGets()");

    if (cupsFileGets(fp, line, sizeof(line)))
    {
      if (line[0] == '#')
      {
        testEnd(true);
      }
      else
      {
        testEndMessage(false, "Got line \"%s\", expected comment line", line);
	status ++;
      }
    }
    else
    {
      testEndMessage(false, "%s", strerror(errno));
      status ++;
    }

    // cupsFileIsCompressed()
    testBegin("cupsFileIsCompressed()");

    if (cupsFileIsCompressed(fp) == compression)
    {
      testEnd(true);
    }
    else
    {
      testEndMessage(false, "Got %s, expected %s", cupsFileIsCompressed(fp) ? "true" : "false",
             compression ? "true" : "false");
      status ++;
    }

    // cupsFileGetConf()
    linenum = 1;

    testBegin("cupsFileGetConf()");

    for (i = 0, value = NULL; i < 1000; i ++)
    {
      if (!cupsFileGetConf(fp, line, sizeof(line), &value, &linenum))
        break;
      else if (_cups_strcasecmp(line, "TestLine") || !value || atoi(value) != i ||
               linenum != (i + 2))
        break;
    }

    if (i >= 1000)
    {
      testEnd(true);
    }
    else if (line[0])
    {
      testEndMessage(false, "Line %d, directive \"%s\", value \"%s\"", linenum,
             line, value ? value : "(null)");
      status ++;
    }
    else
    {
      testEndMessage(false, "%s", strerror(errno));
      status ++;
    }

    // cupsFileGetChar()
    testBegin("cupsFileGetChar()");

    for (i = 0, byte = 0; i < 256; i ++)
    {
      if ((byte = cupsFileGetChar(fp)) != i)
        break;
    }

    if (i >= 256)
    {
      testEnd(true);
    }
    else if (byte >= 0)
    {
      testEndMessage(false, "Got %d, expected %d", byte, i);
      status ++;
    }
    else
    {
      testEndMessage(false, "%s", strerror(errno));
      status ++;
    }

    // cupsFileRead()
    testBegin("cupsFileRead()");

    for (i = 0, bytes = 0; i < 10000; i ++)
    {
      if ((bytes = cupsFileRead(fp, (char *)readbuf, sizeof(readbuf))) < 0)
        break;
      else if (memcmp(readbuf, writebuf, sizeof(readbuf)))
        break;
    }

    if (i >= 10000)
    {
      testEnd(true);
    }
    else if (bytes > 0)
    {
      for (j = 0; j < (int)sizeof(readbuf); j ++)
      {
        if (readbuf[j] != writebuf[j])
	  break;
      }

      testEndMessage(false, "Pass %d, match failed at offset %d - got %02X, expected %02X", i, j, readbuf[j], writebuf[j]);
    }
    else
    {
      testEndMessage(false, "%s", strerror(errno));
      status ++;
    }

    // cupsFileGetChar() with partial line...
    testBegin("cupsFileGetChar(partial line)");

    for (i = 0; i < (int)strlen(partial_line); i ++)
    {
      if ((byte = cupsFileGetChar(fp)) < 0)
        break;
      else if (byte != partial_line[i])
        break;
    }

    if (!partial_line[i])
    {
      testEnd(true);
    }
    else
    {
      testEndMessage(false, "got '%c', expected '%c'", byte, partial_line[i]);
      status ++;
    }

    // cupsFileTell()
    testBegin("cupsFileTell()");

    if ((length = cupsFileTell(fp)) == 81933283)
    {
      testEnd(true);
    }
    else
    {
      testEndMessage(false, "" CUPS_LLFMT " instead of 81933283", CUPS_LLCAST length);
      status ++;
    }

    // cupsFileClose()
    testBegin("cupsFileClose()");

    if (!cupsFileClose(fp))
    {
      testEnd(true);
    }
    else
    {
      testEndMessage(false, "%s", strerror(errno));
      status ++;
    }
  }
  else
  {
    testEndMessage(false, "%s", strerror(errno));
    status ++;
  }

  // Remove the test file...
  if (!status)
    unlink(compression ? "testfile.dat.gz" : "testfile.dat");

  // Return the test status...
  return (status);
}
