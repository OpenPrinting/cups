/*
 * IPP test program for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2007-2019 by Apple Inc.
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
#ifdef _WIN32
#  include <io.h>
#else
#  include <unistd.h>
#  include <fcntl.h>
#endif /* _WIN32 */


/*
 * Local types...
 */

typedef struct _ippdata_t
{
  size_t	rpos,			/* Read position */
		wused,			/* Bytes used */
		wsize;			/* Max size of buffer */
  ipp_uchar_t	*wbuffer;		/* Buffer */
} _ippdata_t;


/*
 * Local globals...
 */

static ipp_uchar_t collection[] =	/* Collection buffer */
		{
		  0x01, 0x01,		/* IPP version */
		  0x00, 0x02,		/* Print-Job operation */
		  0x00, 0x00, 0x00, 0x01,
					/* Request ID */

		  IPP_TAG_OPERATION,

		  IPP_TAG_CHARSET,
		  0x00, 0x12,		/* Name length + name */
		  'a','t','t','r','i','b','u','t','e','s','-',
		  'c','h','a','r','s','e','t',
		  0x00, 0x05,		/* Value length + value */
		  'u','t','f','-','8',

		  IPP_TAG_LANGUAGE,
		  0x00, 0x1b,		/* Name length + name */
		  'a','t','t','r','i','b','u','t','e','s','-',
		  'n','a','t','u','r','a','l','-','l','a','n',
		  'g','u','a','g','e',
		  0x00, 0x02,		/* Value length + value */
		  'e','n',

		  IPP_TAG_URI,
		  0x00, 0x0b,		/* Name length + name */
		  'p','r','i','n','t','e','r','-','u','r','i',
		  0x00, 0x1c,			/* Value length + value */
		  'i','p','p',':','/','/','l','o','c','a','l',
		  'h','o','s','t','/','p','r','i','n','t','e',
		  'r','s','/','f','o','o',

		  IPP_TAG_JOB,		/* job group tag */

		  IPP_TAG_BEGIN_COLLECTION,
					/* begCollection tag */
		  0x00, 0x09,		/* Name length + name */
		  'm', 'e', 'd', 'i', 'a', '-', 'c', 'o', 'l',
		  0x00, 0x00,		/* No value */
		    IPP_TAG_MEMBERNAME,	/* memberAttrName tag */
		    0x00, 0x00,		/* No name */
		    0x00, 0x0a,		/* Value length + value */
		    'm', 'e', 'd', 'i', 'a', '-', 's', 'i', 'z', 'e',
		    IPP_TAG_BEGIN_COLLECTION,
					/* begCollection tag */
		    0x00, 0x00,		/* Name length + name */
		    0x00, 0x00,		/* No value */
		      IPP_TAG_MEMBERNAME,
					/* memberAttrName tag */
		      0x00, 0x00,	/* No name */
		      0x00, 0x0b,	/* Value length + value */
		      'x', '-', 'd', 'i', 'm', 'e', 'n', 's', 'i', 'o', 'n',
		      IPP_TAG_INTEGER,	/* integer tag */
		      0x00, 0x00,	/* No name */
		      0x00, 0x04,	/* Value length + value */
		      0x00, 0x00, 0x54, 0x56,
		      IPP_TAG_MEMBERNAME,
					/* memberAttrName tag */
		      0x00, 0x00,	/* No name */
		      0x00, 0x0b,	/* Value length + value */
		      'y', '-', 'd', 'i', 'm', 'e', 'n', 's', 'i', 'o', 'n',
		      IPP_TAG_INTEGER,	/* integer tag */
		      0x00, 0x00,	/* No name */
		      0x00, 0x04,	/* Value length + value */
		      0x00, 0x00, 0x6d, 0x24,
		    IPP_TAG_END_COLLECTION,
					/* endCollection tag */
		    0x00, 0x00,		/* No name */
		    0x00, 0x00,		/* No value */
		    IPP_TAG_MEMBERNAME,	/* memberAttrName tag */
		    0x00, 0x00,		/* No name */
		    0x00, 0x0b,		/* Value length + value */
		    'm', 'e', 'd', 'i', 'a', '-', 'c', 'o', 'l', 'o', 'r',
		    IPP_TAG_KEYWORD,	/* keyword tag */
		    0x00, 0x00,		/* No name */
		    0x00, 0x04,		/* Value length + value */
		    'b', 'l', 'u', 'e',

		    IPP_TAG_MEMBERNAME,	/* memberAttrName tag */
		    0x00, 0x00,		/* No name */
		    0x00, 0x0a,		/* Value length + value */
		    'm', 'e', 'd', 'i', 'a', '-', 't', 'y', 'p', 'e',
		    IPP_TAG_KEYWORD,	/* keyword tag */
		    0x00, 0x00,		/* No name */
		    0x00, 0x05,		/* Value length + value */
		    'p', 'l', 'a', 'i', 'n',
		  IPP_TAG_END_COLLECTION,
					/* endCollection tag */
		  0x00, 0x00,		/* No name */
		  0x00, 0x00,		/* No value */

		  IPP_TAG_BEGIN_COLLECTION,
					/* begCollection tag */
		  0x00, 0x00,		/* No name */
		  0x00, 0x00,		/* No value */
		    IPP_TAG_MEMBERNAME,	/* memberAttrName tag */
		    0x00, 0x00,		/* No name */
		    0x00, 0x0a,		/* Value length + value */
		    'm', 'e', 'd', 'i', 'a', '-', 's', 'i', 'z', 'e',
		    IPP_TAG_BEGIN_COLLECTION,
					/* begCollection tag */
		    0x00, 0x00,		/* Name length + name */
		    0x00, 0x00,		/* No value */
		      IPP_TAG_MEMBERNAME,
					/* memberAttrName tag */
		      0x00, 0x00,	/* No name */
		      0x00, 0x0b,	/* Value length + value */
		      'x', '-', 'd', 'i', 'm', 'e', 'n', 's', 'i', 'o', 'n',
		      IPP_TAG_INTEGER,	/* integer tag */
		      0x00, 0x00,	/* No name */
		      0x00, 0x04,	/* Value length + value */
		      0x00, 0x00, 0x52, 0x08,
		      IPP_TAG_MEMBERNAME,
					/* memberAttrName tag */
		      0x00, 0x00,	/* No name */
		      0x00, 0x0b,	/* Value length + value */
		      'y', '-', 'd', 'i', 'm', 'e', 'n', 's', 'i', 'o', 'n',
		      IPP_TAG_INTEGER,	/* integer tag */
		      0x00, 0x00,	/* No name */
		      0x00, 0x04,	/* Value length + value */
		      0x00, 0x00, 0x74, 0x04,
		    IPP_TAG_END_COLLECTION,
					/* endCollection tag */
		    0x00, 0x00,		/* No name */
		    0x00, 0x00,		/* No value */
		    IPP_TAG_MEMBERNAME,	/* memberAttrName tag */
		    0x00, 0x00,		/* No name */
		    0x00, 0x0b,		/* Value length + value */
		    'm', 'e', 'd', 'i', 'a', '-', 'c', 'o', 'l', 'o', 'r',
		    IPP_TAG_KEYWORD,	/* keyword tag */
		    0x00, 0x00,		/* No name */
		    0x00, 0x05,		/* Value length + value */
		    'p', 'l', 'a', 'i', 'd',

		    IPP_TAG_MEMBERNAME,	/* memberAttrName tag */
		    0x00, 0x00,		/* No name */
		    0x00, 0x0a,		/* Value length + value */
		    'm', 'e', 'd', 'i', 'a', '-', 't', 'y', 'p', 'e',
		    IPP_TAG_KEYWORD,	/* keyword tag */
		    0x00, 0x00,		/* No name */
		    0x00, 0x06,		/* Value length + value */
		    'g', 'l', 'o', 's', 's', 'y',
		  IPP_TAG_END_COLLECTION,
					/* endCollection tag */
		  0x00, 0x00,		/* No name */
		  0x00, 0x00,		/* No value */

		  IPP_TAG_END		/* end tag */
		};
static ipp_uchar_t bad_collection[] =	/* Collection buffer (bad encoding) */
		{
		  0x01, 0x01,		/* IPP version */
		  0x00, 0x02,		/* Print-Job operation */
		  0x00, 0x00, 0x00, 0x01,
					/* Request ID */

		  IPP_TAG_OPERATION,

		  IPP_TAG_CHARSET,
		  0x00, 0x12,		/* Name length + name */
		  'a','t','t','r','i','b','u','t','e','s','-',
		  'c','h','a','r','s','e','t',
		  0x00, 0x05,		/* Value length + value */
		  'u','t','f','-','8',

		  IPP_TAG_LANGUAGE,
		  0x00, 0x1b,		/* Name length + name */
		  'a','t','t','r','i','b','u','t','e','s','-',
		  'n','a','t','u','r','a','l','-','l','a','n',
		  'g','u','a','g','e',
		  0x00, 0x02,		/* Value length + value */
		  'e','n',

		  IPP_TAG_URI,
		  0x00, 0x0b,		/* Name length + name */
		  'p','r','i','n','t','e','r','-','u','r','i',
		  0x00, 0x1c,			/* Value length + value */
		  'i','p','p',':','/','/','l','o','c','a','l',
		  'h','o','s','t','/','p','r','i','n','t','e',
		  'r','s','/','f','o','o',

		  IPP_TAG_JOB,		/* job group tag */

		  IPP_TAG_BEGIN_COLLECTION,
					/* begCollection tag */
		  0x00, 0x09,		/* Name length + name */
		  'm', 'e', 'd', 'i', 'a', '-', 'c', 'o', 'l',
		  0x00, 0x00,		/* No value */
		    IPP_TAG_BEGIN_COLLECTION,
					/* begCollection tag */
		    0x00, 0x0a,		/* Name length + name */
		    'm', 'e', 'd', 'i', 'a', '-', 's', 'i', 'z', 'e',
		    0x00, 0x00,		/* No value */
		      IPP_TAG_INTEGER,	/* integer tag */
		      0x00, 0x0b,	/* Name length + name */
		      'x', '-', 'd', 'i', 'm', 'e', 'n', 's', 'i', 'o', 'n',
		      0x00, 0x04,	/* Value length + value */
		      0x00, 0x00, 0x54, 0x56,
		      IPP_TAG_INTEGER,	/* integer tag */
		      0x00, 0x0b,	/* Name length + name */
		      'y', '-', 'd', 'i', 'm', 'e', 'n', 's', 'i', 'o', 'n',
		      0x00, 0x04,	/* Value length + value */
		      0x00, 0x00, 0x6d, 0x24,
		    IPP_TAG_END_COLLECTION,
					/* endCollection tag */
		    0x00, 0x00,		/* No name */
		    0x00, 0x00,		/* No value */
		  IPP_TAG_END_COLLECTION,
					/* endCollection tag */
		  0x00, 0x00,		/* No name */
		  0x00, 0x00,		/* No value */

		  IPP_TAG_END		/* end tag */
		};

static ipp_uchar_t mixed[] =		/* Mixed value buffer */
		{
		  0x01, 0x01,		/* IPP version */
		  0x00, 0x02,		/* Print-Job operation */
		  0x00, 0x00, 0x00, 0x01,
					/* Request ID */

		  IPP_TAG_OPERATION,

		  IPP_TAG_INTEGER,	/* integer tag */
		  0x00, 0x1f,		/* Name length + name */
		  'n', 'o', 't', 'i', 'f', 'y', '-', 'l', 'e', 'a', 's', 'e',
		  '-', 'd', 'u', 'r', 'a', 't', 'i', 'o', 'n', '-', 's', 'u',
		  'p', 'p', 'o', 'r', 't', 'e', 'd',
		  0x00, 0x04,		/* Value length + value */
		  0x00, 0x00, 0x00, 0x01,

		  IPP_TAG_RANGE,	/* rangeOfInteger tag */
		  0x00, 0x00,		/* No name */
		  0x00, 0x08,		/* Value length + value */
		  0x00, 0x00, 0x00, 0x10,
		  0x00, 0x00, 0x00, 0x20,

		  IPP_TAG_END		/* end tag */
		};


/*
 * Local functions...
 */

void	hex_dump(const char *title, ipp_uchar_t *buffer, size_t bytes);
void	print_attributes(ipp_t *ipp, int indent);
ssize_t	read_cb(_ippdata_t *data, ipp_uchar_t *buffer, size_t bytes);
ssize_t	read_hex(cups_file_t *fp, ipp_uchar_t *buffer, size_t bytes);
int	token_cb(_ipp_file_t *f, _ipp_vars_t *v, void *user_data, const char *token);
ssize_t	write_cb(_ippdata_t *data, ipp_uchar_t *buffer, size_t bytes);


/*
 * 'main()' - Main entry.
 */

int				/* O - Exit status */
main(int  argc,			/* I - Number of command-line arguments */
     char *argv[])		/* I - Command-line arguments */
{
  _ippdata_t	data;		/* IPP buffer */
  ipp_uchar_t	buffer[8192];	/* Write buffer data */
  ipp_t		*cols[2],	/* Collections */
		*size;		/* media-size collection */
  ipp_t		*request;	/* Request */
  ipp_attribute_t *media_col,	/* media-col attribute */
		*media_size,	/* media-size attribute */
		*attr;		/* Other attribute */
  ipp_state_t	state;		/* State */
  size_t	length;		/* Length of data */
  cups_file_t	*fp;		/* File pointer */
  size_t	i;		/* Looping var */
  int		status;		/* Status of tests (0 = success, 1 = fail) */
#ifdef DEBUG
  const char	*name;		/* Option name */
#endif /* DEBUG */
  static const char * const test_strings[] =
  {				/* Test strings */
    "one-string",
    "two-string",
    "red-string",
    "blue-string"
  };


  status = 0;

  if (argc == 1)
  {
   /*
    * Test request generation code...
    */

    printf("Create Sample Request: ");

    request = ippNew();
    request->request.op.version[0]   = 0x01;
    request->request.op.version[1]   = 0x01;
    request->request.op.operation_id = IPP_OP_PRINT_JOB;
    request->request.op.request_id   = 1;

    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
        	 "attributes-charset", NULL, "utf-8");
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
        	 "attributes-natural-language", NULL, "en");
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
        	 "printer-uri", NULL, "ipp://localhost/printers/foo");

    cols[0] = ippNew();
    size    = ippNew();
    ippAddInteger(size, IPP_TAG_ZERO, IPP_TAG_INTEGER, "x-dimension", 21590);
    ippAddInteger(size, IPP_TAG_ZERO, IPP_TAG_INTEGER, "y-dimension", 27940);
    ippAddCollection(cols[0], IPP_TAG_JOB, "media-size", size);
    ippDelete(size);
    ippAddString(cols[0], IPP_TAG_JOB, IPP_TAG_KEYWORD, "media-color", NULL,
                 "blue");
    ippAddString(cols[0], IPP_TAG_JOB, IPP_TAG_KEYWORD, "media-type", NULL,
                 "plain");

    cols[1] = ippNew();
    size    = ippNew();
    ippAddInteger(size, IPP_TAG_ZERO, IPP_TAG_INTEGER, "x-dimension", 21000);
    ippAddInteger(size, IPP_TAG_ZERO, IPP_TAG_INTEGER, "y-dimension", 29700);
    ippAddCollection(cols[1], IPP_TAG_JOB, "media-size", size);
    ippDelete(size);
    ippAddString(cols[1], IPP_TAG_JOB, IPP_TAG_KEYWORD, "media-color", NULL,
                 "plaid");
    ippAddString(cols[1], IPP_TAG_JOB, IPP_TAG_KEYWORD, "media-type", NULL,
		 "glossy");

    ippAddCollections(request, IPP_TAG_JOB, "media-col", 2,
                      (const ipp_t **)cols);
    ippDelete(cols[0]);
    ippDelete(cols[1]);

    length = ippLength(request);
    if (length != sizeof(collection))
    {
      printf("FAIL - wrong ippLength(), %d instead of %d bytes!\n",
             (int)length, (int)sizeof(collection));
      status = 1;
    }
    else
      puts("PASS");

   /*
    * Write test #1...
    */

    printf("Write Sample to Memory: ");

    data.wused   = 0;
    data.wsize   = sizeof(buffer);
    data.wbuffer = buffer;

    while ((state = ippWriteIO(&data, (ipp_iocb_t)write_cb, 1, NULL,
                               request)) != IPP_STATE_DATA)
      if (state == IPP_STATE_ERROR)
	break;

    if (state != IPP_STATE_DATA)
    {
      printf("FAIL - %d bytes written.\n", (int)data.wused);
      status = 1;
    }
    else if (data.wused != sizeof(collection))
    {
      printf("FAIL - wrote %d bytes, expected %d bytes!\n", (int)data.wused,
             (int)sizeof(collection));
      hex_dump("Bytes Written", data.wbuffer, data.wused);
      hex_dump("Baseline", collection, sizeof(collection));
      status = 1;
    }
    else if (memcmp(data.wbuffer, collection, data.wused))
    {
      for (i = 0; i < data.wused; i ++)
        if (data.wbuffer[i] != collection[i])
	  break;

      printf("FAIL - output does not match baseline at 0x%04x!\n", (unsigned)i);
      hex_dump("Bytes Written", data.wbuffer, data.wused);
      hex_dump("Baseline", collection, sizeof(collection));
      status = 1;
    }
    else
      puts("PASS");

    ippDelete(request);

   /*
    * Read the data back in and confirm...
    */

    printf("Read Sample from Memory: ");

    request     = ippNew();
    data.rpos = 0;

    while ((state = ippReadIO(&data, (ipp_iocb_t)read_cb, 1, NULL,
                              request)) != IPP_STATE_DATA)
      if (state == IPP_STATE_ERROR)
	break;

    length = ippLength(request);

    if (state != IPP_STATE_DATA)
    {
      printf("FAIL - %d bytes read.\n", (int)data.rpos);
      status = 1;
    }
    else if (data.rpos != data.wused)
    {
      printf("FAIL - read %d bytes, expected %d bytes!\n", (int)data.rpos,
             (int)data.wused);
      print_attributes(request, 8);
      status = 1;
    }
    else if (length != sizeof(collection))
    {
      printf("FAIL - wrong ippLength(), %d instead of %d bytes!\n",
             (int)length, (int)sizeof(collection));
      print_attributes(request, 8);
      status = 1;
    }
    else
      puts("PASS");

    fputs("ippFindAttribute(media-col): ", stdout);
    if ((media_col = ippFindAttribute(request, "media-col",
                                      IPP_TAG_BEGIN_COLLECTION)) == NULL)
    {
      if ((media_col = ippFindAttribute(request, "media-col",
                                        IPP_TAG_ZERO)) == NULL)
        puts("FAIL (not found)");
      else
        printf("FAIL (wrong type - %s)\n", ippTagString(media_col->value_tag));

      status = 1;
    }
    else if (media_col->num_values != 2)
    {
      printf("FAIL (wrong count - %d)\n", media_col->num_values);
      status = 1;
    }
    else
      puts("PASS");

    if (media_col)
    {
      fputs("ippFindAttribute(media-size 1): ", stdout);
      if ((media_size = ippFindAttribute(media_col->values[0].collection,
					 "media-size",
					 IPP_TAG_BEGIN_COLLECTION)) == NULL)
      {
	if ((media_size = ippFindAttribute(media_col->values[0].collection,
					   "media-col",
					   IPP_TAG_ZERO)) == NULL)
	  puts("FAIL (not found)");
	else
	  printf("FAIL (wrong type - %s)\n",
	         ippTagString(media_size->value_tag));

	status = 1;
      }
      else
      {
	if ((attr = ippFindAttribute(media_size->values[0].collection,
				     "x-dimension", IPP_TAG_INTEGER)) == NULL)
	{
	  if ((attr = ippFindAttribute(media_size->values[0].collection,
				       "x-dimension", IPP_TAG_ZERO)) == NULL)
	    puts("FAIL (missing x-dimension)");
	  else
	    printf("FAIL (wrong type for x-dimension - %s)\n",
		   ippTagString(attr->value_tag));

	  status = 1;
	}
	else if (attr->values[0].integer != 21590)
	{
	  printf("FAIL (wrong value for x-dimension - %d)\n",
		 attr->values[0].integer);
	  status = 1;
	}
	else if ((attr = ippFindAttribute(media_size->values[0].collection,
					  "y-dimension",
					  IPP_TAG_INTEGER)) == NULL)
	{
	  if ((attr = ippFindAttribute(media_size->values[0].collection,
				       "y-dimension", IPP_TAG_ZERO)) == NULL)
	    puts("FAIL (missing y-dimension)");
	  else
	    printf("FAIL (wrong type for y-dimension - %s)\n",
		   ippTagString(attr->value_tag));

	  status = 1;
	}
	else if (attr->values[0].integer != 27940)
	{
	  printf("FAIL (wrong value for y-dimension - %d)\n",
		 attr->values[0].integer);
	  status = 1;
	}
	else
	  puts("PASS");
      }

      fputs("ippFindAttribute(media-size 2): ", stdout);
      if ((media_size = ippFindAttribute(media_col->values[1].collection,
					 "media-size",
					 IPP_TAG_BEGIN_COLLECTION)) == NULL)
      {
	if ((media_size = ippFindAttribute(media_col->values[1].collection,
					   "media-col",
					   IPP_TAG_ZERO)) == NULL)
	  puts("FAIL (not found)");
	else
	  printf("FAIL (wrong type - %s)\n",
	         ippTagString(media_size->value_tag));

	status = 1;
      }
      else
      {
	if ((attr = ippFindAttribute(media_size->values[0].collection,
				     "x-dimension",
				     IPP_TAG_INTEGER)) == NULL)
	{
	  if ((attr = ippFindAttribute(media_size->values[0].collection,
				       "x-dimension", IPP_TAG_ZERO)) == NULL)
	    puts("FAIL (missing x-dimension)");
	  else
	    printf("FAIL (wrong type for x-dimension - %s)\n",
		   ippTagString(attr->value_tag));

	  status = 1;
	}
	else if (attr->values[0].integer != 21000)
	{
	  printf("FAIL (wrong value for x-dimension - %d)\n",
		 attr->values[0].integer);
	  status = 1;
	}
	else if ((attr = ippFindAttribute(media_size->values[0].collection,
					  "y-dimension",
					  IPP_TAG_INTEGER)) == NULL)
	{
	  if ((attr = ippFindAttribute(media_size->values[0].collection,
				       "y-dimension", IPP_TAG_ZERO)) == NULL)
	    puts("FAIL (missing y-dimension)");
	  else
	    printf("FAIL (wrong type for y-dimension - %s)\n",
		   ippTagString(attr->value_tag));

	  status = 1;
	}
	else if (attr->values[0].integer != 29700)
	{
	  printf("FAIL (wrong value for y-dimension - %d)\n",
		 attr->values[0].integer);
	  status = 1;
	}
	else
	  puts("PASS");
      }
    }

   /*
    * Test hierarchical find...
    */

    fputs("ippFindAttribute(media-col/media-size/x-dimension): ", stdout);
    if ((attr = ippFindAttribute(request, "media-col/media-size/x-dimension", IPP_TAG_INTEGER)) != NULL)
    {
      if (ippGetInteger(attr, 0) != 21590)
      {
        printf("FAIL (wrong value for x-dimension - %d)\n", ippGetInteger(attr, 0));
        status = 1;
      }
      else
        puts("PASS");
    }
    else
    {
      puts("FAIL (not found)");
      status = 1;
    }

    fputs("ippFindNextAttribute(media-col/media-size/x-dimension): ", stdout);
    if ((attr = ippFindNextAttribute(request, "media-col/media-size/x-dimension", IPP_TAG_INTEGER)) != NULL)
    {
      if (ippGetInteger(attr, 0) != 21000)
      {
        printf("FAIL (wrong value for x-dimension - %d)\n", ippGetInteger(attr, 0));
        status = 1;
      }
      else
        puts("PASS");
    }
    else
    {
      puts("FAIL (not found)");
      status = 1;
    }

    fputs("ippFindNextAttribute(media-col/media-size/x-dimension) again: ", stdout);
    if ((attr = ippFindNextAttribute(request, "media-col/media-size/x-dimension", IPP_TAG_INTEGER)) != NULL)
    {
      printf("FAIL (got %d, expected nothing)\n", ippGetInteger(attr, 0));
      status = 1;
    }
    else
      puts("PASS");

    ippDelete(request);

   /*
    * Read the bad collection data and confirm we get an error...
    */

    fputs("Read Bad Collection from Memory: ", stdout);

    request = ippNew();
    data.rpos    = 0;
    data.wused   = sizeof(bad_collection);
    data.wsize   = sizeof(bad_collection);
    data.wbuffer = bad_collection;

    while ((state = ippReadIO(&data, (ipp_iocb_t)read_cb, 1, NULL, request)) != IPP_STATE_DATA)
      if (state == IPP_STATE_ERROR)
	break;

    if (state != IPP_STATE_ERROR)
      puts("FAIL (read successful)");
    else
      puts("PASS");

   /*
    * Read the mixed data and confirm we converted everything to rangeOfInteger
    * values...
    */

    fputs("Read Mixed integer/rangeOfInteger from Memory: ", stdout);

    request = ippNew();
    data.rpos    = 0;
    data.wused   = sizeof(mixed);
    data.wsize   = sizeof(mixed);
    data.wbuffer = mixed;

    while ((state = ippReadIO(&data, (ipp_iocb_t)read_cb, 1, NULL,
                              request)) != IPP_STATE_DATA)
      if (state == IPP_STATE_ERROR)
	break;

    length = ippLength(request);

    if (state != IPP_STATE_DATA)
    {
      printf("FAIL - %d bytes read.\n", (int)data.rpos);
      status = 1;
    }
    else if (data.rpos != sizeof(mixed))
    {
      printf("FAIL - read %d bytes, expected %d bytes!\n", (int)data.rpos,
             (int)sizeof(mixed));
      print_attributes(request, 8);
      status = 1;
    }
    else if (length != (sizeof(mixed) + 4))
    {
      printf("FAIL - wrong ippLength(), %d instead of %d bytes!\n",
             (int)length, (int)sizeof(mixed) + 4);
      print_attributes(request, 8);
      status = 1;
    }
    else
      puts("PASS");

    fputs("ippFindAttribute(notify-lease-duration-supported): ", stdout);
    if ((attr = ippFindAttribute(request, "notify-lease-duration-supported",
                                 IPP_TAG_ZERO)) == NULL)
    {
      puts("FAIL (not found)");
      status = 1;
    }
    else if (attr->value_tag != IPP_TAG_RANGE)
    {
      printf("FAIL (wrong type - %s)\n", ippTagString(attr->value_tag));
      status = 1;
    }
    else if (attr->num_values != 2)
    {
      printf("FAIL (wrong count - %d)\n", attr->num_values);
      status = 1;
    }
    else if (attr->values[0].range.lower != 1 ||
             attr->values[0].range.upper != 1 ||
             attr->values[1].range.lower != 16 ||
             attr->values[1].range.upper != 32)
    {
      printf("FAIL (wrong values - %d,%d and %d,%d)\n",
             attr->values[0].range.lower,
             attr->values[0].range.upper,
             attr->values[1].range.lower,
             attr->values[1].range.upper);
      status = 1;
    }
    else
      puts("PASS");

    fputs("ippDeleteValues: ", stdout);
    attr = ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "test-strings", 4, NULL, test_strings);
    if (ippGetCount(attr) != 4)
    {
      printf("FAIL (got %d values, expected 4 values)\n", ippGetCount(attr));
      status = 1;
    }
    else if (!ippDeleteValues(request, &attr, 3, 1))
    {
      puts("FAIL (returned 0)");
      status = 1;
    }
    else if (ippGetCount(attr) != 3)
    {
      printf("FAIL (got %d values, expected 3 values)\n", ippGetCount(attr));
      status = 1;
    }
    else
    {
      puts("PASS");
    }

    ippDelete(request);

#ifdef DEBUG
   /*
    * Test that private option array is sorted...
    */

    fputs("_ippCheckOptions: ", stdout);
    if ((name = _ippCheckOptions()) == NULL)
      puts("PASS");
    else
    {
      printf("FAIL (\"%s\" out of order)\n", name);
      status = 1;
    }
#endif /* DEBUG */

   /*
    * Test _ippFindOption() private API...
    */

    fputs("_ippFindOption(\"printer-type\"): ", stdout);
    if (_ippFindOption("printer-type"))
      puts("PASS");
    else
    {
      puts("FAIL");
      status = 1;
    }

   /*
    * Summarize...
    */

    putchar('\n');

    if (status)
      puts("Core IPP tests failed.");
    else
      puts("Core IPP tests passed.");
  }
  else
  {
   /*
    * Read IPP files...
    */

    for (i = 1; i < (size_t)argc; i ++)
    {
      if (strlen(argv[i]) > 5 && !strcmp(argv[i] + strlen(argv[i]) - 5, ".test"))
      {
       /*
        * Read an ASCII IPP message...
        */

        _ipp_vars_t v;			/* IPP variables */

        _ippVarsInit(&v, NULL, NULL, token_cb);
        request = _ippFileParse(&v, argv[i], NULL);
        _ippVarsDeinit(&v);
      }
      else if (strlen(argv[i]) > 4 && !strcmp(argv[i] + strlen(argv[i]) - 4, ".hex"))
      {
       /*
        * Read a hex-encoded IPP message...
        */

	if ((fp = cupsFileOpen(argv[i], "r")) == NULL)
	{
	  printf("Unable to open \"%s\" - %s\n", argv[i], strerror(errno));
	  status = 1;
	  continue;
	}

	request = ippNew();
	while ((state = ippReadIO(fp, (ipp_iocb_t)read_hex, 1, NULL, request)) == IPP_STATE_ATTRIBUTE);

	if (state != IPP_STATE_DATA)
	{
	  printf("Error reading IPP message from \"%s\": %s\n", argv[i], cupsLastErrorString());
	  status = 1;

	  ippDelete(request);
	  request = NULL;
	}

        cupsFileClose(fp);
      }
      else
      {
       /*
        * Read a raw (binary) IPP message...
        */

	if ((fp = cupsFileOpen(argv[i], "r")) == NULL)
	{
	  printf("Unable to open \"%s\" - %s\n", argv[i], strerror(errno));
	  status = 1;
	  continue;
	}

	request = ippNew();
	while ((state = ippReadIO(fp, (ipp_iocb_t)cupsFileRead, 1, NULL,
				  request)) == IPP_STATE_ATTRIBUTE);

	if (state != IPP_STATE_DATA)
	{
	  printf("Error reading IPP message from \"%s\": %s\n", argv[i], cupsLastErrorString());
	  status = 1;

	  ippDelete(request);
	  request = NULL;
	}

        cupsFileClose(fp);
      }

      if (request)
      {
	printf("\n%s:\n", argv[i]);
	print_attributes(request, 4);
	ippDelete(request);
      }
    }
  }

  return (status);
}


/*
 * 'hex_dump()' - Produce a hex dump of a buffer.
 */

void
hex_dump(const char  *title,		/* I - Title */
         ipp_uchar_t *buffer,		/* I - Buffer to dump */
         size_t      bytes)		/* I - Number of bytes */
{
  size_t	i, j;			/* Looping vars */
  int		ch;			/* Current ASCII char */


 /*
  * Show lines of 16 bytes at a time...
  */

  printf("    %s:\n", title);

  for (i = 0; i < bytes; i += 16)
  {
   /*
    * Show the offset...
    */

    printf("    %04x ", (unsigned)i);

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
 * 'print_attributes()' - Print the attributes in a request...
 */

void
print_attributes(ipp_t *ipp,		/* I - IPP request */
                 int   indent)		/* I - Indentation */
{
  ipp_tag_t		group;		/* Current group */
  ipp_attribute_t	*attr;		/* Current attribute */
  char                  buffer[2048];   /* Value string */


  for (group = IPP_TAG_ZERO, attr = ipp->attrs; attr; attr = attr->next)
  {
    if (!attr->name && indent == 4)
    {
      group = IPP_TAG_ZERO;
      putchar('\n');
      continue;
    }

    if (group != attr->group_tag)
    {
      group = attr->group_tag;

      printf("\n%*s%s:\n\n", indent - 4, "", ippTagString(group));
    }

    ippAttributeString(attr, buffer, sizeof(buffer));

    printf("%*s%s (%s%s): %s\n", indent, "", attr->name ? attr->name : "(null)", attr->num_values > 1 ? "1setOf " : "", ippTagString(attr->value_tag), buffer);
  }
}


/*
 * 'read_cb()' - Read data from a buffer.
 */

ssize_t					/* O - Number of bytes read */
read_cb(_ippdata_t   *data,		/* I - Data */
        ipp_uchar_t *buffer,		/* O - Buffer to read */
	size_t      bytes)		/* I - Number of bytes to read */
{
  size_t	count;			/* Number of bytes */


 /*
  * Copy bytes from the data buffer to the read buffer...
  */

  if ((count = data->wsize - data->rpos) > bytes)
    count = bytes;

  memcpy(buffer, data->wbuffer + data->rpos, count);
  data->rpos += count;

 /*
  * Return the number of bytes read...
  */

  return ((ssize_t)count);
}


/*
 * 'read_hex()' - Read a hex dump of an IPP request.
 */

ssize_t					/* O - Number of bytes read */
read_hex(cups_file_t *fp,		/* I - File to read from */
         ipp_uchar_t *buffer,		/* I - Buffer to read */
         size_t      bytes)		/* I - Number of bytes to read */
{
  size_t	total = 0;		/* Total bytes read */
  static char	hex[256] = "";		/* Line from file */
  static char	*hexptr = NULL;		/* Pointer in line */


  while (total < bytes)
  {
    if (!hexptr || (isspace(hexptr[0] & 255) && isspace(hexptr[1] & 255)))
    {
      if (!cupsFileGets(fp, hex, sizeof(hex)))
        break;

      hexptr = hex;
      while (isxdigit(*hexptr & 255))
        hexptr ++;
      while (isspace(*hexptr & 255))
        hexptr ++;

      if (!isxdigit(*hexptr & 255))
      {
        hexptr = NULL;
        continue;
      }
    }

    *buffer++ = (ipp_uchar_t)strtol(hexptr, &hexptr, 16);
    total ++;
  }

  return (total == 0 ? -1 : (ssize_t)total);
}


/*
 * 'token_cb()' - Token callback for ASCII IPP data file parser.
 */

int					/* O - 1 on success, 0 on failure */
token_cb(_ipp_file_t *f,		/* I - IPP file data */
         _ipp_vars_t *v,		/* I - IPP variables */
         void        *user_data,	/* I - User data pointer */
         const char  *token)		/* I - Token string */
{
  (void)v;
  (void)user_data;

  if (!token)
  {
    f->attrs     = ippNew();
    f->group_tag = IPP_TAG_PRINTER;
  }
  else
  {
    fprintf(stderr, "Unknown directive \"%s\" on line %d of \"%s\".\n", token, f->linenum, f->filename);
    return (0);
  }

  return (1);
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
