/*
 * Array test program for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright 2007-2014 by Apple Inc.
 * Copyright 1997-2006 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more information.
 */

/*
 * Include necessary headers...
 */

#include "string-private.h"
#include "debug-private.h"
#include "array-private.h"
#include "dir.h"


/*
 * Local functions...
 */

static double	get_seconds(void);
static int	load_words(const char *filename, cups_array_t *array);


/*
 * 'main()' - Main entry.
 */

int					/* O - Exit status */
main(void)
{
  int		i;			/* Looping var */
  cups_array_t	*array,			/* Test array */
		*dup_array;		/* Duplicate array */
  int		status;			/* Exit status */
  char		*text;			/* Text from array */
  char		word[256];		/* Word from file */
  double	start,			/* Start time */
		end;			/* End time */
  cups_dir_t	*dir;			/* Current directory */
  cups_dentry_t	*dent;			/* Directory entry */
  char		*saved[32];		/* Saved entries */
  void		*data;			/* User data for arrays */


 /*
  * No errors so far...
  */

  status = 0;

 /*
  * cupsArrayNew()
  */

  fputs("cupsArrayNew: ", stdout);

  data  = (void *)"testarray";
  array = cupsArrayNew((cups_array_func_t)strcmp, data);

  if (array)
    puts("PASS");
  else
  {
    puts("FAIL (returned NULL, expected pointer)");
    status ++;
  }

 /*
  * cupsArrayUserData()
  */

  fputs("cupsArrayUserData: ", stdout);
  if (cupsArrayUserData(array) == data)
    puts("PASS");
  else
  {
    printf("FAIL (returned %p instead of %p!)\n", cupsArrayUserData(array),
           data);
    status ++;
  }

 /*
  * cupsArrayAdd()
  */

  fputs("cupsArrayAdd: ", stdout);

  if (!cupsArrayAdd(array, strdup("One Fish")))
  {
    puts("FAIL (\"One Fish\")");
    status ++;
  }
  else
  {
    if (!cupsArrayAdd(array, strdup("Two Fish")))
    {
      puts("FAIL (\"Two Fish\")");
      status ++;
    }
    else
    {
      if (!cupsArrayAdd(array, strdup("Red Fish")))
      {
	puts("FAIL (\"Red Fish\")");
	status ++;
      }
      else
      {
        if (!cupsArrayAdd(array, strdup("Blue Fish")))
	{
	  puts("FAIL (\"Blue Fish\")");
	  status ++;
	}
	else
	  puts("PASS");
      }
    }
  }

 /*
  * cupsArrayCount()
  */

  fputs("cupsArrayCount: ", stdout);
  if (cupsArrayCount(array) == 4)
    puts("PASS");
  else
  {
    printf("FAIL (returned %d, expected 4)\n", cupsArrayCount(array));
    status ++;
  }

 /*
  * cupsArrayFirst()
  */

  fputs("cupsArrayFirst: ", stdout);
  if ((text = (char *)cupsArrayFirst(array)) != NULL &&
      !strcmp(text, "Blue Fish"))
    puts("PASS");
  else
  {
    printf("FAIL (returned \"%s\", expected \"Blue Fish\")\n", text);
    status ++;
  }

 /*
  * cupsArrayNext()
  */

  fputs("cupsArrayNext: ", stdout);
  if ((text = (char *)cupsArrayNext(array)) != NULL &&
      !strcmp(text, "One Fish"))
    puts("PASS");
  else
  {
    printf("FAIL (returned \"%s\", expected \"One Fish\")\n", text);
    status ++;
  }

 /*
  * cupsArrayLast()
  */

  fputs("cupsArrayLast: ", stdout);
  if ((text = (char *)cupsArrayLast(array)) != NULL &&
      !strcmp(text, "Two Fish"))
    puts("PASS");
  else
  {
    printf("FAIL (returned \"%s\", expected \"Two Fish\")\n", text);
    status ++;
  }

 /*
  * cupsArrayPrev()
  */

  fputs("cupsArrayPrev: ", stdout);
  if ((text = (char *)cupsArrayPrev(array)) != NULL &&
      !strcmp(text, "Red Fish"))
    puts("PASS");
  else
  {
    printf("FAIL (returned \"%s\", expected \"Red Fish\")\n", text);
    status ++;
  }

 /*
  * cupsArrayFind()
  */

  fputs("cupsArrayFind: ", stdout);
  if ((text = (char *)cupsArrayFind(array, (void *)"One Fish")) != NULL &&
      !strcmp(text, "One Fish"))
    puts("PASS");
  else
  {
    printf("FAIL (returned \"%s\", expected \"One Fish\")\n", text);
    status ++;
  }

 /*
  * cupsArrayCurrent()
  */

  fputs("cupsArrayCurrent: ", stdout);
  if ((text = (char *)cupsArrayCurrent(array)) != NULL &&
      !strcmp(text, "One Fish"))
    puts("PASS");
  else
  {
    printf("FAIL (returned \"%s\", expected \"One Fish\")\n", text);
    status ++;
  }

 /*
  * cupsArrayDup()
  */

  fputs("cupsArrayDup: ", stdout);
  if ((dup_array = cupsArrayDup(array)) != NULL &&
      cupsArrayCount(dup_array) == 4)
    puts("PASS");
  else
  {
    printf("FAIL (returned %p with %d elements, expected pointer with 4 elements)\n", (void *)dup_array, cupsArrayCount(dup_array));
    status ++;
  }

 /*
  * cupsArrayRemove()
  */

  fputs("cupsArrayRemove: ", stdout);
  if (cupsArrayRemove(array, (void *)"One Fish") &&
      cupsArrayCount(array) == 3)
    puts("PASS");
  else
  {
    printf("FAIL (returned 0 with %d elements, expected 1 with 4 elements)\n",
           cupsArrayCount(array));
    status ++;
  }

 /*
  * cupsArrayClear()
  */

  fputs("cupsArrayClear: ", stdout);
  cupsArrayClear(array);
  if (cupsArrayCount(array) == 0)
    puts("PASS");
  else
  {
    printf("FAIL (%d elements, expected 0 elements)\n",
           cupsArrayCount(array));
    status ++;
  }

 /*
  * Now load this source file and grab all of the unique words...
  */

  fputs("Load unique words: ", stdout);
  fflush(stdout);

  start = get_seconds();

  if ((dir = cupsDirOpen(".")) == NULL)
  {
    puts("FAIL (cupsDirOpen failed)");
    status ++;
  }
  else
  {
    while ((dent = cupsDirRead(dir)) != NULL)
    {
      i = (int)strlen(dent->filename) - 2;

      if (i > 0 && dent->filename[i] == '.' &&
          (dent->filename[i + 1] == 'c' ||
	   dent->filename[i + 1] == 'h'))
	load_words(dent->filename, array);
    }

    cupsDirClose(dir);

    end = get_seconds();

    printf("%d words in %.3f seconds (%.0f words/sec), ", cupsArrayCount(array),
           end - start, cupsArrayCount(array) / (end - start));
    fflush(stdout);

    for (text = (char *)cupsArrayFirst(array); text;)
    {
     /*
      * Copy this word to the word buffer (safe because we strdup'd from
      * the same buffer in the first place... :)
      */

      strlcpy(word, text, sizeof(word));

     /*
      * Grab the next word and compare...
      */

      if ((text = (char *)cupsArrayNext(array)) == NULL)
	break;

      if (strcmp(word, text) >= 0)
	break;
    }

    if (text)
    {
      printf("FAIL (\"%s\" >= \"%s\"!)\n", word, text);
      status ++;
    }
    else
      puts("PASS");
  }

 /*
  * Test deleting with iteration...
  */

  fputs("Delete While Iterating: ", stdout);

  text = (char *)cupsArrayFirst(array);
  cupsArrayRemove(array, text);
  free(text);

  text = (char *)cupsArrayNext(array);
  if (!text)
  {
    puts("FAIL (cupsArrayNext returned NULL!)");
    status ++;
  }
  else
    puts("PASS");

 /*
  * Test save/restore...
  */

  fputs("cupsArraySave: ", stdout);

  for (i = 0, text = (char *)cupsArrayFirst(array);
       i < 32;
       i ++, text = (char *)cupsArrayNext(array))
  {
    saved[i] = text;

    if (!cupsArraySave(array))
      break;
  }

  if (i < 32)
    printf("FAIL (depth = %d)\n", i);
  else
    puts("PASS");

  fputs("cupsArrayRestore: ", stdout);

  while (i > 0)
  {
    i --;

    text = cupsArrayRestore(array);
    if (text != saved[i])
      break;
  }

  if (i)
    printf("FAIL (depth = %d)\n", i);
  else
    puts("PASS");

 /*
  * Delete the arrays...
  */

  cupsArrayDelete(array);
  cupsArrayDelete(dup_array);

 /*
  * Test the array with string functions...
  */

  fputs("_cupsArrayNewStrings(\" \\t\\nfoo bar\\tboo\\nfar\", ' '): ", stdout);
  array = _cupsArrayNewStrings(" \t\nfoo bar\tboo\nfar", ' ');
  if (!array)
  {
    status = 1;
    puts("FAIL (unable to create array)");
  }
  else if (cupsArrayCount(array) != 4)
  {
    status = 1;
    printf("FAIL (got %d elements, expected 4)\n", cupsArrayCount(array));
  }
  else if (strcmp(text = (char *)cupsArrayFirst(array), "bar"))
  {
    status = 1;
    printf("FAIL (first element \"%s\", expected \"bar\")\n", text);
  }
  else if (strcmp(text = (char *)cupsArrayNext(array), "boo"))
  {
    status = 1;
    printf("FAIL (first element \"%s\", expected \"boo\")\n", text);
  }
  else if (strcmp(text = (char *)cupsArrayNext(array), "far"))
  {
    status = 1;
    printf("FAIL (first element \"%s\", expected \"far\")\n", text);
  }
  else if (strcmp(text = (char *)cupsArrayNext(array), "foo"))
  {
    status = 1;
    printf("FAIL (first element \"%s\", expected \"foo\")\n", text);
  }
  else
    puts("PASS");

  fputs("_cupsArrayAddStrings(array, \"foo2,bar2\", ','): ", stdout);
  _cupsArrayAddStrings(array, "foo2,bar2", ',');

  if (cupsArrayCount(array) != 6)
  {
    status = 1;
    printf("FAIL (got %d elements, expected 6)\n", cupsArrayCount(array));
  }
  else if (strcmp(text = (char *)cupsArrayFirst(array), "bar"))
  {
    status = 1;
    printf("FAIL (first element \"%s\", expected \"bar\")\n", text);
  }
  else if (strcmp(text = (char *)cupsArrayNext(array), "bar2"))
  {
    status = 1;
    printf("FAIL (first element \"%s\", expected \"bar2\")\n", text);
  }
  else if (strcmp(text = (char *)cupsArrayNext(array), "boo"))
  {
    status = 1;
    printf("FAIL (first element \"%s\", expected \"boo\")\n", text);
  }
  else if (strcmp(text = (char *)cupsArrayNext(array), "far"))
  {
    status = 1;
    printf("FAIL (first element \"%s\", expected \"far\")\n", text);
  }
  else if (strcmp(text = (char *)cupsArrayNext(array), "foo"))
  {
    status = 1;
    printf("FAIL (first element \"%s\", expected \"foo\")\n", text);
  }
  else if (strcmp(text = (char *)cupsArrayNext(array), "foo2"))
  {
    status = 1;
    printf("FAIL (first element \"%s\", expected \"foo2\")\n", text);
  }
  else
    puts("PASS");

  cupsArrayDelete(array);

 /*
  * Summarize the results and return...
  */

  if (!status)
    puts("\nALL TESTS PASSED!");
  else
    printf("\n%d TEST(S) FAILED!\n", status);

  return (status);
}


/*
 * 'get_seconds()' - Get the current time in seconds...
 */

#ifdef _WIN32
#  include <windows.h>


static double
get_seconds(void)
{
}
#else
#  include <sys/time.h>


static double
get_seconds(void)
{
  struct timeval	curtime;	/* Current time */


  gettimeofday(&curtime, NULL);
  return (curtime.tv_sec + 0.000001 * curtime.tv_usec);
}
#endif /* _WIN32 */


/*
 * 'load_words()' - Load words from a file.
 */

static int				/* O - 1 on success, 0 on failure */
load_words(const char   *filename,	/* I - File to load */
           cups_array_t *array)		/* I - Array to add to */
{
  FILE		*fp;			/* Test file */
  char		word[256];		/* Word from file */


  if ((fp = fopen(filename, "r")) == NULL)
  {
    perror(filename);
    return (0);
  }

  while (fscanf(fp, "%255s", word) == 1)
  {
    if (!cupsArrayFind(array, word))
      cupsArrayAdd(array, strdup(word));
  }

  fclose(fp);

  return (1);
}
