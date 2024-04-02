//
// JSON API unit tests for CUPS.
//
// Copyright © 2022-2024 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "cups.h"
#include "json.h"
#include "test-internal.h"


//
// 'main()' - Main entry.
//

int					// O - Exit status
main(int  argc,				// I - Number of command-line arguments
     char *argv[])			// I - Command-line arguments
{
  int		i;			// Looping var
  cups_json_t	*json;			// JSON root object


  if (argc == 1)
  {
    // Do unit tests...
    cups_json_t	*current,		// Current node
		*parent;		// Current parent node
    cups_jtype_t type;			// Node type
    size_t	count;			// Number of children
    char	*s;			// JSON string
    time_t	last_modified = 0;	// Last-Modified value
    static const char * const types[] =	// Node types
    {
      "CUPS_JTYPE_NULL",		// Null value
      "CUPS_JTYPE_FALSE",		// Boolean false value
      "CUPS_JTYPE_TRUE",		// Boolean true value
      "CUPS_JTYPE_NUMBER",		// Number value
      "CUPS_JTYPE_STRING",		// String value
      "CUPS_JTYPE_ARRAY",		// Array value
      "CUPS_JTYPE_OBJECT",		// Object value
      "CUPS_JTYPE_KEY"			// Object key (string)
    };

    testBegin("cupsJSONNew(root object)");
    json = cupsJSONNew(NULL, NULL, CUPS_JTYPE_OBJECT);
    testEnd(json != NULL);

    testBegin("cupsJSONGetCount(root)");
    count = cupsJSONGetCount(json);
    testEndMessage(count == 0, "%u", (unsigned)count);

    testBegin("cupsJSONGetType(root)");
    type = cupsJSONGetType(json);
    testEndMessage(type == CUPS_JTYPE_OBJECT, "%s", types[type]);

    testBegin("cupsJSONNewKey('string')");
    current = cupsJSONNewKey(json, NULL, "string");
    testEnd(current != NULL);

    testBegin("cupsJSONGetType(key)");
    type = cupsJSONGetType(current);
    testEndMessage(type == CUPS_JTYPE_KEY, "%s", types[type]);

    testBegin("cupsJSONNewString('value')");
    current = cupsJSONNewString(json, current, "value");
    testEnd(current != NULL);

    testBegin("cupsJSONGetType(string)");
    type = cupsJSONGetType(current);
    testEndMessage(type == CUPS_JTYPE_STRING, "%s", types[type]);

    testBegin("cupsJSONNewKey('number')");
    current = cupsJSONNewKey(json, NULL, "number");
    testEnd(current != NULL);

    testBegin("cupsJSONNewNumber(42)");
    current = cupsJSONNewNumber(json, current, 42);
    testEnd(current != NULL);

    testBegin("cupsJSONGetType(number)");
    type = cupsJSONGetType(current);
    testEndMessage(type == CUPS_JTYPE_NUMBER, "%s", types[type]);

    testBegin("cupsJSONNewKey('null')");
    current = cupsJSONNewKey(json, NULL, "null");
    testEnd(current != NULL);

    testBegin("cupsJSONNew(null)");
    current = cupsJSONNew(json, current, CUPS_JTYPE_NULL);
    testEnd(current != NULL);

    testBegin("cupsJSONGetType(null)");
    type = cupsJSONGetType(current);
    testEndMessage(type == CUPS_JTYPE_NULL, "%s", types[type]);

    testBegin("cupsJSONNewKey('false')");
    current = cupsJSONNewKey(json, NULL, "false");
    testEnd(current != NULL);

    testBegin("cupsJSONNew(false)");
    current = cupsJSONNew(json, current, CUPS_JTYPE_FALSE);
    testEnd(current != NULL);

    testBegin("cupsJSONGetType(false)");
    type = cupsJSONGetType(current);
    testEndMessage(type == CUPS_JTYPE_FALSE, "%s", types[type]);

    testBegin("cupsJSONNewKey('true')");
    current = cupsJSONNewKey(json, NULL, "true");
    testEnd(current != NULL);

    testBegin("cupsJSONNew(true)");
    current = cupsJSONNew(json, current, CUPS_JTYPE_TRUE);
    testEnd(current != NULL);

    testBegin("cupsJSONGetType(true)");
    type = cupsJSONGetType(current);
    testEndMessage(type == CUPS_JTYPE_TRUE, "%s", types[type]);

    testBegin("cupsJSONNewKey('array')");
    current = cupsJSONNewKey(json, NULL, "array");
    testEnd(current != NULL);

    testBegin("cupsJSONNew(array)");
    parent = cupsJSONNew(json, current, CUPS_JTYPE_ARRAY);
    testEnd(parent != NULL);

    testBegin("cupsJSONGetType(array)");
    type = cupsJSONGetType(parent);
    testEndMessage(type == CUPS_JTYPE_ARRAY, "%s", types[type]);

    testBegin("cupsJSONNewString(array, 'foo')");
    current = cupsJSONNewString(parent, NULL, "foo");
    testEnd(current != NULL);

    testBegin("cupsJSONNewString(array, 'bar')");
    current = cupsJSONNewString(parent, current, "bar");
    testEnd(current != NULL);

    testBegin("cupsJSONNewNumber(array, 0.5)");
    current = cupsJSONNewNumber(parent, current, 0.5);
    testEnd(current != NULL);

    testBegin("cupsJSONNewNumber(array, 123456789123456789.0)");
    current = cupsJSONNewNumber(parent, current, 123456789123456789.0);
    testEnd(current != NULL);

    testBegin("cupsJSONNew(array, null)");
    current = cupsJSONNew(parent, current, CUPS_JTYPE_NULL);
    testEnd(current != NULL);

    testBegin("cupsJSONNewKey('object')");
    current = cupsJSONNewKey(json, NULL, "object");
    testEnd(current != NULL);

    testBegin("cupsJSONNew(object)");
    parent = cupsJSONNew(json, current, CUPS_JTYPE_OBJECT);
    testEnd(parent != NULL);

    testBegin("cupsJSONNewKey(object, 'a')");
    current = cupsJSONNewKey(parent, NULL, "a");
    testEnd(current != NULL);

    testBegin("cupsJSONNewString(object, 'one')");
    current = cupsJSONNewString(parent, current, "one");
    testEnd(current != NULL);

    testBegin("cupsJSONNewKey(object, 'b')");
    current = cupsJSONNewKey(parent, current, "b");
    testEnd(current != NULL);

    testBegin("cupsJSONNewNumber(object, 2)");
    current = cupsJSONNewNumber(parent, current, 2);
    testEnd(current != NULL);

    testBegin("cupsJSONGetCount(root)");
    count = cupsJSONGetCount(json);
    testEndMessage(count == 14, "%u", (unsigned)count);

    testBegin("cupsJSONExportFile(root, 'test.json')");
    if (cupsJSONExportFile(json, "test.json"))
    {
      testEnd(true);

      testBegin("cupsJSONImportFile('test.json')");
      parent = cupsJSONImportFile("test.json");
      testEnd(parent != NULL);

      cupsJSONDelete(parent);
    }
    else
    {
      testEndMessage(false, "%s", cupsGetErrorString());
    }

    testBegin("cupsJSONExportString(root)");
    if ((s = cupsJSONExportString(json)) != NULL)
    {
      testEnd(true);

      testBegin("cupsJSONImportString('%s')", s);
      parent = cupsJSONImportString(s);
      testEnd(parent != NULL);

      cupsJSONDelete(parent);
      free(s);
    }
    else
    {
      testEndMessage(false, "%s", cupsGetErrorString());
    }

    testBegin("cupsJSONDelete(root)");
    cupsJSONDelete(json);
    testEnd(true);

    testBegin("cupsJSONImportURL('https://accounts.google.com/.well-known/openid-configuration', no last modified)");
    json = cupsJSONImportURL("https://accounts.google.com/.well-known/openid-configuration", &last_modified);

    if (json)
    {
      char	last_modified_date[256];// Last-Modified string value

      testEnd(true);
      cupsJSONDelete(json);

      testBegin("cupsJSONImportURL('https://accounts.google.com/.well-known/openid-configuration', since %s)", httpGetDateString2(last_modified, last_modified_date, sizeof(last_modified_date)));
      json = cupsJSONImportURL("https://accounts.google.com/.well-known/openid-configuration", &last_modified);

      if (json)
        testEnd(true);
      else if (cupsGetError() == IPP_STATUS_OK_EVENTS_COMPLETE)
        testEndMessage(true, "no change from last request");
      else
        testEndMessage(false, cupsGetErrorString());

      cupsJSONDelete(json);
    }
    else if (cupsGetError() == IPP_STATUS_ERROR_SERVICE_UNAVAILABLE)
    {
      testEndMessage(true, "%s", cupsGetErrorString());
    }
    else
    {
      testEndMessage(false, "%s", cupsGetErrorString());
    }

    if (!testsPassed)
      return (1);
  }
  else
  {
    // Try loading JSON files/strings on the command-line...
    for (i = 1; i < argc; i ++)
    {
      if (argv[i][0] == '{')
      {
        // Load JSON string...
        if ((json = cupsJSONImportString(argv[i])) != NULL)
          printf("string%d: OK, %u key/value pairs in root object.\n", i, (unsigned)(cupsJSONGetCount(json) / 2));
        else
          fprintf(stderr, "string%d: %s\n", i, cupsGetErrorString());
      }
      else if ((json = cupsJSONImportFile(argv[i])) != NULL)
	printf("%s: OK, %u key/value pairs in root object.\n", argv[i], (unsigned)(cupsJSONGetCount(json) / 2));
      else
	fprintf(stderr, "%s: %s\n", argv[i], cupsGetErrorString());

      cupsJSONDelete(json);
    }
  }

  return (0);
}
