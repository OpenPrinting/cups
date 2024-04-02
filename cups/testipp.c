//
// IPP unit test program for libcups.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright © 2007-2019 by Apple Inc.
// Copyright © 1997-2005 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "file.h"
#include "string-private.h"
#include "ipp-private.h"
#include "test-internal.h"
#ifdef _WIN32
#  include <io.h>
#else
#  include <unistd.h>
#  include <fcntl.h>
#endif // _WIN32


//
// Local types...
//

typedef struct _ippdata_t
{
  size_t	rpos,			// Read position
		wused,			// Bytes used
		wsize;			// Max size of buffer
  ipp_uchar_t	*wbuffer;		// Buffer
} _ippdata_t;


//
// Local globals...
//

static ipp_uchar_t collection[] =	// Collection buffer
		{
		  0x01, 0x01,		// IPP version
		  0x00, 0x02,		// Print-Job operation
		  0x00, 0x00, 0x00, 0x01,
					// Request ID

		  IPP_TAG_OPERATION,

		  IPP_TAG_CHARSET,
		  0x00, 0x12,		// Name length + name
		  'a','t','t','r','i','b','u','t','e','s','-',
		  'c','h','a','r','s','e','t',
		  0x00, 0x05,		// Value length + value
		  'u','t','f','-','8',

		  IPP_TAG_LANGUAGE,
		  0x00, 0x1b,		// Name length + name
		  'a','t','t','r','i','b','u','t','e','s','-',
		  'n','a','t','u','r','a','l','-','l','a','n',
		  'g','u','a','g','e',
		  0x00, 0x02,		// Value length + value
		  'e','n',

		  IPP_TAG_URI,
		  0x00, 0x0b,		// Name length + name
		  'p','r','i','n','t','e','r','-','u','r','i',
		  0x00, 0x1c,			// Value length + value
		  'i','p','p',':','/','/','l','o','c','a','l',
		  'h','o','s','t','/','p','r','i','n','t','e',
		  'r','s','/','f','o','o',

		  IPP_TAG_JOB,		// job group tag

		  IPP_TAG_BEGIN_COLLECTION,
					// begCollection tag
		  0x00, 0x09,		// Name length + name
		  'm', 'e', 'd', 'i', 'a', '-', 'c', 'o', 'l',
		  0x00, 0x00,		// No value
		    IPP_TAG_MEMBERNAME,	// memberAttrName tag
		    0x00, 0x00,		// No name
		    0x00, 0x0a,		// Value length + value
		    'm', 'e', 'd', 'i', 'a', '-', 's', 'i', 'z', 'e',
		    IPP_TAG_BEGIN_COLLECTION,
					// begCollection tag
		    0x00, 0x00,		// Name length + name
		    0x00, 0x00,		// No value
		      IPP_TAG_MEMBERNAME,
					// memberAttrName tag
		      0x00, 0x00,	// No name
		      0x00, 0x0b,	// Value length + value
		      'x', '-', 'd', 'i', 'm', 'e', 'n', 's', 'i', 'o', 'n',
		      IPP_TAG_INTEGER,	// integer tag
		      0x00, 0x00,	// No name
		      0x00, 0x04,	// Value length + value
		      0x00, 0x00, 0x54, 0x56,
		      IPP_TAG_MEMBERNAME,
					// memberAttrName tag
		      0x00, 0x00,	// No name
		      0x00, 0x0b,	// Value length + value
		      'y', '-', 'd', 'i', 'm', 'e', 'n', 's', 'i', 'o', 'n',
		      IPP_TAG_INTEGER,	// integer tag
		      0x00, 0x00,	// No name
		      0x00, 0x04,	// Value length + value
		      0x00, 0x00, 0x6d, 0x24,
		    IPP_TAG_END_COLLECTION,
					// endCollection tag
		    0x00, 0x00,		// No name
		    0x00, 0x00,		// No value
		    IPP_TAG_MEMBERNAME,	// memberAttrName tag
		    0x00, 0x00,		// No name
		    0x00, 0x0b,		// Value length + value
		    'm', 'e', 'd', 'i', 'a', '-', 'c', 'o', 'l', 'o', 'r',
		    IPP_TAG_KEYWORD,	// keyword tag
		    0x00, 0x00,		// No name
		    0x00, 0x04,		// Value length + value
		    'b', 'l', 'u', 'e',

		    IPP_TAG_MEMBERNAME,	// memberAttrName tag
		    0x00, 0x00,		// No name
		    0x00, 0x0a,		// Value length + value
		    'm', 'e', 'd', 'i', 'a', '-', 't', 'y', 'p', 'e',
		    IPP_TAG_KEYWORD,	// keyword tag
		    0x00, 0x00,		// No name
		    0x00, 0x05,		// Value length + value
		    'p', 'l', 'a', 'i', 'n',
		  IPP_TAG_END_COLLECTION,
					// endCollection tag
		  0x00, 0x00,		// No name
		  0x00, 0x00,		// No value

		  IPP_TAG_BEGIN_COLLECTION,
					// begCollection tag
		  0x00, 0x00,		// No name
		  0x00, 0x00,		// No value
		    IPP_TAG_MEMBERNAME,	// memberAttrName tag
		    0x00, 0x00,		// No name
		    0x00, 0x0a,		// Value length + value
		    'm', 'e', 'd', 'i', 'a', '-', 's', 'i', 'z', 'e',
		    IPP_TAG_BEGIN_COLLECTION,
					// begCollection tag
		    0x00, 0x00,		// Name length + name
		    0x00, 0x00,		// No value
		      IPP_TAG_MEMBERNAME,
					// memberAttrName tag
		      0x00, 0x00,	// No name
		      0x00, 0x0b,	// Value length + value
		      'x', '-', 'd', 'i', 'm', 'e', 'n', 's', 'i', 'o', 'n',
		      IPP_TAG_INTEGER,	// integer tag
		      0x00, 0x00,	// No name
		      0x00, 0x04,	// Value length + value
		      0x00, 0x00, 0x52, 0x08,
		      IPP_TAG_MEMBERNAME,
					// memberAttrName tag
		      0x00, 0x00,	// No name
		      0x00, 0x0b,	// Value length + value
		      'y', '-', 'd', 'i', 'm', 'e', 'n', 's', 'i', 'o', 'n',
		      IPP_TAG_INTEGER,	// integer tag
		      0x00, 0x00,	// No name
		      0x00, 0x04,	// Value length + value
		      0x00, 0x00, 0x74, 0x04,
		    IPP_TAG_END_COLLECTION,
					// endCollection tag
		    0x00, 0x00,		// No name
		    0x00, 0x00,		// No value
		    IPP_TAG_MEMBERNAME,	// memberAttrName tag
		    0x00, 0x00,		// No name
		    0x00, 0x0b,		// Value length + value
		    'm', 'e', 'd', 'i', 'a', '-', 'c', 'o', 'l', 'o', 'r',
		    IPP_TAG_KEYWORD,	// keyword tag
		    0x00, 0x00,		// No name
		    0x00, 0x05,		// Value length + value
		    'p', 'l', 'a', 'i', 'd',

		    IPP_TAG_MEMBERNAME,	// memberAttrName tag
		    0x00, 0x00,		// No name
		    0x00, 0x0a,		// Value length + value
		    'm', 'e', 'd', 'i', 'a', '-', 't', 'y', 'p', 'e',
		    IPP_TAG_KEYWORD,	// keyword tag
		    0x00, 0x00,		// No name
		    0x00, 0x06,		// Value length + value
		    'g', 'l', 'o', 's', 's', 'y',
		  IPP_TAG_END_COLLECTION,
					// endCollection tag
		  0x00, 0x00,		// No name
		  0x00, 0x00,		// No value

		  IPP_TAG_END		// end tag
		};
static ipp_uchar_t bad_collection[] =	// Collection buffer (bad encoding)
		{
		  0x01, 0x01,		// IPP version
		  0x00, 0x02,		// Print-Job operation
		  0x00, 0x00, 0x00, 0x01,
					// Request ID

		  IPP_TAG_OPERATION,

		  IPP_TAG_CHARSET,
		  0x00, 0x12,		// Name length + name
		  'a','t','t','r','i','b','u','t','e','s','-',
		  'c','h','a','r','s','e','t',
		  0x00, 0x05,		// Value length + value
		  'u','t','f','-','8',

		  IPP_TAG_LANGUAGE,
		  0x00, 0x1b,		// Name length + name
		  'a','t','t','r','i','b','u','t','e','s','-',
		  'n','a','t','u','r','a','l','-','l','a','n',
		  'g','u','a','g','e',
		  0x00, 0x02,		// Value length + value
		  'e','n',

		  IPP_TAG_URI,
		  0x00, 0x0b,		// Name length + name
		  'p','r','i','n','t','e','r','-','u','r','i',
		  0x00, 0x1c,			// Value length + value
		  'i','p','p',':','/','/','l','o','c','a','l',
		  'h','o','s','t','/','p','r','i','n','t','e',
		  'r','s','/','f','o','o',

		  IPP_TAG_JOB,		// job group tag

		  IPP_TAG_BEGIN_COLLECTION,
					// begCollection tag
		  0x00, 0x09,		// Name length + name
		  'm', 'e', 'd', 'i', 'a', '-', 'c', 'o', 'l',
		  0x00, 0x00,		// No value
		    IPP_TAG_BEGIN_COLLECTION,
					// begCollection tag
		    0x00, 0x0a,		// Name length + name
		    'm', 'e', 'd', 'i', 'a', '-', 's', 'i', 'z', 'e',
		    0x00, 0x00,		// No value
		      IPP_TAG_INTEGER,	// integer tag
		      0x00, 0x0b,	// Name length + name
		      'x', '-', 'd', 'i', 'm', 'e', 'n', 's', 'i', 'o', 'n',
		      0x00, 0x04,	// Value length + value
		      0x00, 0x00, 0x54, 0x56,
		      IPP_TAG_INTEGER,	// integer tag
		      0x00, 0x0b,	// Name length + name
		      'y', '-', 'd', 'i', 'm', 'e', 'n', 's', 'i', 'o', 'n',
		      0x00, 0x04,	// Value length + value
		      0x00, 0x00, 0x6d, 0x24,
		    IPP_TAG_END_COLLECTION,
					// endCollection tag
		    0x00, 0x00,		// No name
		    0x00, 0x00,		// No value
		  IPP_TAG_END_COLLECTION,
					// endCollection tag
		  0x00, 0x00,		// No name
		  0x00, 0x00,		// No value

		  IPP_TAG_END		// end tag
		};

static ipp_uchar_t mixed[] =		// Mixed value buffer
		{
		  0x01, 0x01,		// IPP version
		  0x00, 0x02,		// Print-Job operation
		  0x00, 0x00, 0x00, 0x01,
					// Request ID

		  IPP_TAG_OPERATION,

		  IPP_TAG_INTEGER,	// integer tag
		  0x00, 0x1f,		// Name length + name
		  'n', 'o', 't', 'i', 'f', 'y', '-', 'l', 'e', 'a', 's', 'e',
		  '-', 'd', 'u', 'r', 'a', 't', 'i', 'o', 'n', '-', 's', 'u',
		  'p', 'p', 'o', 'r', 't', 'e', 'd',
		  0x00, 0x04,		// Value length + value
		  0x00, 0x00, 0x00, 0x01,

		  IPP_TAG_RANGE,	// rangeOfInteger tag
		  0x00, 0x00,		// No name
		  0x00, 0x08,		// Value length + value
		  0x00, 0x00, 0x00, 0x10,
		  0x00, 0x00, 0x00, 0x20,

		  IPP_TAG_END		// end tag
		};


//
// Local functions...
//

void	print_attributes(ipp_t *ipp, int indent);
ssize_t	read_cb(_ippdata_t *data, ipp_uchar_t *buffer, size_t bytes);
ssize_t	read_hex(cups_file_t *fp, ipp_uchar_t *buffer, size_t bytes);
bool	token_cb(ipp_file_t *f, void *user_data, const char *token);
ssize_t	write_cb(_ippdata_t *data, ipp_uchar_t *buffer, size_t bytes);


//
// 'main()' - Main entry.
//

int				// O - Exit status
main(int  argc,			// I - Number of command-line arguments
     char *argv[])		// I - Command-line arguments
{
  _ippdata_t	data;		// IPP buffer
  ipp_uchar_t	buffer[8192];	// Write buffer data
  ipp_t		*cols[2],	// Collections
		*size;		// media-size collection
  ipp_t		*request;	// Request
  ipp_attribute_t *media_col,	// media-col attribute
		*media_size,	// media-size attribute
		*attr;		// Other attribute
  ipp_state_t	state;		// State
  size_t	length;		// Length of data
  cups_file_t	*fp;		// File pointer
  size_t	i;		// Looping var
  int		status;		// Status of tests (0 = success, 1 = fail)
#ifdef DEBUG
  const char	*name;		// Option name
#endif // DEBUG


  status = 0;

  if (argc == 1)
  {
    // Test request generation code...
    testBegin("Create Sample Request");

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

    length = ippGetLength(request);
    if (length != sizeof(collection))
    {
      testEndMessage(false, "wrong ippGetLength(), %d instead of %d bytes",
             (int)length, (int)sizeof(collection));
      status = 1;
    }
    else
      testEnd(true);

    // Write test #1...
    testBegin("Write Sample to Memory");

    data.wused   = 0;
    data.wsize   = sizeof(buffer);
    data.wbuffer = buffer;

    while ((state = ippWriteIO(&data, (ipp_io_cb_t)write_cb, 1, NULL,
                               request)) != IPP_STATE_DATA)
      if (state == IPP_STATE_ERROR)
	break;

    if (state != IPP_STATE_DATA)
    {
      testEndMessage(false, "%d bytes written", (int)data.wused);
      status = 1;
    }
    else if (data.wused != sizeof(collection))
    {
      testEndMessage(false, "wrote %d bytes, expected %d bytes", (int)data.wused,
             (int)sizeof(collection));
      testError("Bytes Written");
      testHexDump(data.wbuffer, data.wused);
      testError("Baseline");
      testHexDump(collection, sizeof(collection));
      status = 1;
    }
    else if (memcmp(data.wbuffer, collection, data.wused))
    {
      for (i = 0; i < data.wused; i ++)
        if (data.wbuffer[i] != collection[i])
	  break;

      testEndMessage(false, "output does not match baseline at 0x%04x", (unsigned)i);
      testError("Bytes Written");
      testHexDump(data.wbuffer, data.wused);
      testError("Baseline");
      testHexDump(collection, sizeof(collection));
      status = 1;
    }
    else
      testEnd(true);

    ippDelete(request);

    // Read the data back in and confirm...
    testBegin("Read Sample from Memory");

    request   = ippNew();
    data.rpos = 0;

    while ((state = ippReadIO(&data, (ipp_io_cb_t)read_cb, 1, NULL,
                              request)) != IPP_STATE_DATA)
    {
      if (state == IPP_STATE_ERROR)
	break;
    }

    length = ippGetLength(request);

    if (state != IPP_STATE_DATA)
    {
      testEndMessage(false, "%d bytes read", (int)data.rpos);
      status = 1;
    }
    else if (data.rpos != data.wused)
    {
      testEndMessage(false, "read %d bytes, expected %d bytes", (int)data.rpos,
             (int)data.wused);
      print_attributes(request, 8);
      status = 1;
    }
    else if (length != sizeof(collection))
    {
      testEndMessage(false, "wrong ippLength(), %d instead of %d bytes",
             (int)length, (int)sizeof(collection));
      print_attributes(request, 8);
      status = 1;
    }
    else
      testEnd(true);

    testBegin("ippFindAttribute(media-col)");
    if ((media_col = ippFindAttribute(request, "media-col",
                                      IPP_TAG_BEGIN_COLLECTION)) == NULL)
    {
      if ((media_col = ippFindAttribute(request, "media-col",
                                        IPP_TAG_ZERO)) == NULL)
        testEndMessage(false, "not found");
      else
        testEndMessage(false, "wrong type - %s", ippTagString(media_col->value_tag));

      status = 1;
    }
    else if (media_col->num_values != 2)
    {
      testEndMessage(false, "wrong count - %d", media_col->num_values);
      status = 1;
    }
    else
      testEnd(true);

    if (media_col)
    {
      testBegin("ippFindAttribute(media-size 1)");
      if ((media_size = ippFindAttribute(media_col->values[0].collection,
					 "media-size",
					 IPP_TAG_BEGIN_COLLECTION)) == NULL)
      {
	if ((media_size = ippFindAttribute(media_col->values[0].collection,
					   "media-col",
					   IPP_TAG_ZERO)) == NULL)
	  testEndMessage(false, "not found");
	else
	  testEndMessage(false, "wrong type - %s",
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
	    testEndMessage(false, "missing x-dimension");
	  else
	    testEndMessage(false, "wrong type for x-dimension - %s",
		   ippTagString(attr->value_tag));

	  status = 1;
	}
	else if (attr->values[0].integer != 21590)
	{
	  testEndMessage(false, "wrong value for x-dimension - %d",
		 attr->values[0].integer);
	  status = 1;
	}
	else if ((attr = ippFindAttribute(media_size->values[0].collection,
					  "y-dimension",
					  IPP_TAG_INTEGER)) == NULL)
	{
	  if ((attr = ippFindAttribute(media_size->values[0].collection,
				       "y-dimension", IPP_TAG_ZERO)) == NULL)
	    testEndMessage(false, "missing y-dimension");
	  else
	    testEndMessage(false, "wrong type for y-dimension - %s",
		   ippTagString(attr->value_tag));

	  status = 1;
	}
	else if (attr->values[0].integer != 27940)
	{
	  testEndMessage(false, "wrong value for y-dimension - %d",
		 attr->values[0].integer);
	  status = 1;
	}
	else
	  testEnd(true);
      }

      testBegin("ippFindAttribute(media-size 2)");
      if ((media_size = ippFindAttribute(media_col->values[1].collection,
					 "media-size",
					 IPP_TAG_BEGIN_COLLECTION)) == NULL)
      {
	if ((media_size = ippFindAttribute(media_col->values[1].collection,
					   "media-col",
					   IPP_TAG_ZERO)) == NULL)
	  testEndMessage(false, "not found");
	else
	  testEndMessage(false, "wrong type - %s",
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
	    testEndMessage(false, "missing x-dimension");
	  else
	    testEndMessage(false, "wrong type for x-dimension - %s",
		   ippTagString(attr->value_tag));

	  status = 1;
	}
	else if (attr->values[0].integer != 21000)
	{
	  testEndMessage(false, "wrong value for x-dimension - %d",
		 attr->values[0].integer);
	  status = 1;
	}
	else if ((attr = ippFindAttribute(media_size->values[0].collection,
					  "y-dimension",
					  IPP_TAG_INTEGER)) == NULL)
	{
	  if ((attr = ippFindAttribute(media_size->values[0].collection,
				       "y-dimension", IPP_TAG_ZERO)) == NULL)
	    testEndMessage(false, "missing y-dimension");
	  else
	    testEndMessage(false, "wrong type for y-dimension - %s",
		   ippTagString(attr->value_tag));

	  status = 1;
	}
	else if (attr->values[0].integer != 29700)
	{
	  testEndMessage(false, "wrong value for y-dimension - %d",
		 attr->values[0].integer);
	  status = 1;
	}
	else
	  testEnd(true);
      }
    }

    // Test hierarchical find...
    testBegin("ippFindAttribute(media-col/media-size/x-dimension)");
    if ((attr = ippFindAttribute(request, "media-col/media-size/x-dimension", IPP_TAG_INTEGER)) != NULL)
    {
      if (ippGetInteger(attr, 0) != 21590)
      {
        testEndMessage(false, "wrong value for x-dimension - %d", ippGetInteger(attr, 0));
        status = 1;
      }
      else
        testEnd(true);
    }
    else
    {
      testEndMessage(false, "not found");
      status = 1;
    }

    testBegin("ippFindNextAttribute(media-col/media-size/x-dimension)");
    if ((attr = ippFindNextAttribute(request, "media-col/media-size/x-dimension", IPP_TAG_INTEGER)) != NULL)
    {
      if (ippGetInteger(attr, 0) != 21000)
      {
        testEndMessage(false, "wrong value for x-dimension - %d", ippGetInteger(attr, 0));
        status = 1;
      }
      else
        testEnd(true);
    }
    else
    {
      testEndMessage(false, "not found");
      status = 1;
    }

    testBegin("ippFindNextAttribute(media-col/media-size/x-dimension) again");
    if ((attr = ippFindNextAttribute(request, "media-col/media-size/x-dimension", IPP_TAG_INTEGER)) != NULL)
    {
      testEndMessage(false, "got %d, expected nothing", ippGetInteger(attr, 0));
      status = 1;
    }
    else
      testEnd(true);

    ippDelete(request);

    // Read the bad collection data and confirm we get an error...
    testBegin("Read Bad Collection from Memory");

    request = ippNew();
    data.rpos    = 0;
    data.wused   = sizeof(bad_collection);
    data.wsize   = sizeof(bad_collection);
    data.wbuffer = bad_collection;

    while ((state = ippReadIO(&data, (ipp_io_cb_t)read_cb, 1, NULL, request)) != IPP_STATE_DATA)
      if (state == IPP_STATE_ERROR)
	break;

    ippDelete(request);

    if (state != IPP_STATE_ERROR)
      testEndMessage(false, "read successful");
    else
      testEnd(true);

    // Read the mixed data and confirm we converted everything to rangeOfInteger
    // values...
    testBegin("Read Mixed integer/rangeOfInteger from Memory");

    request = ippNew();
    data.rpos    = 0;
    data.wused   = sizeof(mixed);
    data.wsize   = sizeof(mixed);
    data.wbuffer = mixed;

    while ((state = ippReadIO(&data, (ipp_io_cb_t)read_cb, 1, NULL,
                              request)) != IPP_STATE_DATA)
      if (state == IPP_STATE_ERROR)
	break;

    length = ippGetLength(request);

    if (state != IPP_STATE_DATA)
    {
      testEndMessage(false, "%d bytes read", (int)data.rpos);
      status = 1;
    }
    else if (data.rpos != sizeof(mixed))
    {
      testEndMessage(false, "read %d bytes, expected %d bytes", (int)data.rpos,
             (int)sizeof(mixed));
      print_attributes(request, 8);
      status = 1;
    }
    else if (length != (sizeof(mixed) + 4))
    {
      testEndMessage(false, "wrong ippLength(), %d instead of %d bytes",
             (int)length, (int)sizeof(mixed) + 4);
      print_attributes(request, 8);
      status = 1;
    }
    else
      testEnd(true);

    testBegin("ippFindAttribute(notify-lease-duration-supported)");
    if ((attr = ippFindAttribute(request, "notify-lease-duration-supported",
                                 IPP_TAG_ZERO)) == NULL)
    {
      testEndMessage(false, "not found");
      status = 1;
    }
    else if (attr->value_tag != IPP_TAG_RANGE)
    {
      testEndMessage(false, "wrong type - %s", ippTagString(attr->value_tag));
      status = 1;
    }
    else if (attr->num_values != 2)
    {
      testEndMessage(false, "wrong count - %d", attr->num_values);
      status = 1;
    }
    else if (attr->values[0].range.lower != 1 ||
             attr->values[0].range.upper != 1 ||
             attr->values[1].range.lower != 16 ||
             attr->values[1].range.upper != 32)
    {
      testEndMessage(false, "wrong values - %d,%d and %d,%d",
             attr->values[0].range.lower,
             attr->values[0].range.upper,
             attr->values[1].range.lower,
             attr->values[1].range.upper);
      status = 1;
    }
    else
      testEnd(true);

    ippDelete(request);

#ifdef DEBUG
    // Test that private option array is sorted...
    testBegin("_ippCheckOptions");
    if ((name = _ippCheckOptions()) == NULL)
    {
      testEnd(true);
    }
    else
    {
      testEndMessage(false, "\"%s\" out of order", name);
      status = 1;
    }
#endif // DEBUG

    // Test _ippFindOption() private API...
    testBegin("_ippFindOption(\"printer-type\")");
    if (_ippFindOption("printer-type"))
      testEnd(true);
    else
    {
      testEnd(false);
      status = 1;
    }
  }
  else
  {
    // Read IPP files...
    for (i = 1; i < (size_t)argc; i ++)
    {
      if (strlen(argv[i]) > 5 && !strcmp(argv[i] + strlen(argv[i]) - 5, ".test"))
      {
        // Read an ASCII IPP message...
        ipp_file_t *file;		// IPP data file

        file    = ippFileNew(NULL, NULL, NULL, NULL);
        request = ippNew();

        ippFileOpen(file, argv[i], "r");
        ippFileRead(file, token_cb, true);
        ippFileDelete(file);
      }
      else if (strlen(argv[i]) > 4 && !strcmp(argv[i] + strlen(argv[i]) - 4, ".hex"))
      {
        // Read a hex-encoded IPP message...
	if ((fp = cupsFileOpen(argv[i], "r")) == NULL)
	{
	  printf("Unable to open \"%s\" - %s\n", argv[i], strerror(errno));
	  status = 1;
	  continue;
	}

	request = ippNew();
	while ((state = ippReadIO(fp, (ipp_io_cb_t)read_hex, 1, NULL, request)) == IPP_STATE_ATTRIBUTE);

	if (state != IPP_STATE_DATA)
	{
	  printf("Error reading IPP message from \"%s\": %s\n", argv[i], cupsGetErrorString());
	  status = 1;

	  ippDelete(request);
	  request = NULL;
	}

        cupsFileClose(fp);
      }
      else
      {
        // Read a raw (binary) IPP message...
	if ((fp = cupsFileOpen(argv[i], "r")) == NULL)
	{
	  printf("Unable to open \"%s\" - %s\n", argv[i], strerror(errno));
	  status = 1;
	  continue;
	}

	request = ippNew();
	while ((state = ippReadIO(fp, (ipp_io_cb_t)cupsFileRead, 1, NULL,
				  request)) == IPP_STATE_ATTRIBUTE);

	if (state != IPP_STATE_DATA)
	{
	  printf("Error reading IPP message from \"%s\": %s\n", argv[i], cupsGetErrorString());
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


//
// 'print_attributes()' - Print the attributes in a request...
//

void
print_attributes(ipp_t *ipp,		// I - IPP request
                 int   indent)		// I - Indentation
{
  ipp_tag_t		group;		// Current group
  ipp_attribute_t	*attr;		// Current attribute
  char                  buffer[2048];   // Value string


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

      testError("\n%*s%s:\n", indent - 4, "", ippTagString(group));
    }

    ippAttributeString(attr, buffer, sizeof(buffer));

    testError("%*s%s (%s%s): %s\n", indent, "", attr->name ? attr->name : "(null)", attr->num_values > 1 ? "1setOf " : "", ippTagString(attr->value_tag), buffer);
  }
}


//
// 'read_cb()' - Read data from a buffer.
//

ssize_t					// O - Number of bytes read
read_cb(_ippdata_t   *data,		// I - Data
        ipp_uchar_t *buffer,		// O - Buffer to read
	size_t      bytes)		// I - Number of bytes to read
{
  size_t	count;			// Number of bytes


  // Copy bytes from the data buffer to the read buffer...
  if ((count = data->wsize - data->rpos) > bytes)
    count = bytes;

  memcpy(buffer, data->wbuffer + data->rpos, count);
  data->rpos += count;

  // Return the number of bytes read...
  return ((ssize_t)count);
}


//
// 'read_hex()' - Read a hex dump of an IPP request.
//

ssize_t					// O - Number of bytes read
read_hex(cups_file_t *fp,		// I - File to read from
         ipp_uchar_t *buffer,		// I - Buffer to read
         size_t      bytes)		// I - Number of bytes to read
{
  size_t	total = 0;		// Total bytes read
  static char	hex[256] = "";		// Line from file
  static char	*hexptr = NULL;		// Pointer in line


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


//
// 'token_cb()' - Token callback for ASCII IPP data file parser.
//

bool					// O - `true` on success, `false` on failure
token_cb(ipp_file_t *f,			// I - IPP file data
         void       *user_data,		// I - User data pointer
         const char *token)		// I - Token string
{
  (void)user_data;

  // TODO: Add a custom directive to test this.
  if (strcasecmp(token, "TEST"))
  {
    fprintf(stderr, "Unknown directive '%s' on line %d of '%s'.\n", token, ippFileGetLineNumber(f), ippFileGetFilename(f));
    return (false);
  }

  return (true);
}


//
// 'write_cb()' - Write data into a buffer.
//

ssize_t					// O - Number of bytes written
write_cb(_ippdata_t   *data,		// I - Data
         ipp_uchar_t *buffer,		// I - Buffer to write
	 size_t      bytes)		// I - Number of bytes to write
{
  size_t	count;			// Number of bytes


  // Loop until all bytes are written...
  if ((count = data->wsize - data->wused) > bytes)
    count = bytes;

  memcpy(data->wbuffer + data->wused, buffer, count);
  data->wused += count;

  // Return the number of bytes written...
  return ((ssize_t)count);
}
