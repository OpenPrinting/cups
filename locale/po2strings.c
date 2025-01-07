/*
 * Convert a GNU gettext .po file to an Apple .strings file.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright 2007-2017 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more information.
 *
 * Usage:
 *
 *   po2strings filename.strings filename.po
 *
 * Compile with:
 *
 *   gcc -o po2strings po2strings.c `cups-config --libs`
 */

#include <cups/cups-private.h>


/*
 * The .strings file format is simple:
 *
 * // comment
 * "msgid" = "msgstr";
 *
 * The GNU gettext .po format is also fairly simple:
 *
 *     #. comment
 *     msgid "some text"
 *     msgstr "localized text"
 *
 * The comment, msgid, and msgstr text can span multiple lines using the form:
 *
 *     #. comment
 *     #. more comments
 *     msgid ""
 *     "some long text"
 *     msgstr ""
 *     "localized text spanning "
 *     "multiple lines"
 *
 * Both the msgid and msgstr strings use standard C quoting for special
 * characters like newline and the double quote character.
 */

static char	*normalize_string(const char *idstr, char *buffer, size_t bufsize);


/*
 *   main() - Convert .po file to .strings.
 */

int					/* O - Exit code */
main(int  argc,				/* I - Number of command-line args */
     char *argv[])			/* I - Command-line arguments */
{
  int			i;		/* Looping var */
  const char		*pofile,	/* .po filename */
			*stringsfile;	/* .strings filename */
  cups_file_t		*po,		/* .po file */
			*strings;	/* .strings file */
  char			s[4096],	/* String buffer */
			*ptr,		/* Pointer into buffer */
			*temp,		/* New string */
			*msgid,		/* msgid string */
			*msgstr,	/* msgstr string */
			normalized[8192];/* Normalized msgid string */
  size_t		length;		/* Length of combined strings */
  int			use_msgid;	/* Use msgid strings for msgstr? */


 /*
  * Process command-line arguments...
  */

  pofile      = NULL;
  stringsfile = NULL;
  use_msgid   = 0;

  for (i = 1; i < argc; i ++)
  {
    if (!strcmp(argv[i], "-m"))
      use_msgid = 1;
    else if (argv[i][0] == '-')
    {
      puts("Usage: po2strings [-m] filename.po filename.strings");
      return (1);
    }
    else if (!pofile)
      pofile = argv[i];
    else if (!stringsfile)
      stringsfile = argv[i];
    else
    {
      puts("Usage: po2strings [-m] filename.po filename.strings");
      return (1);
    }
  }

  if (!pofile || !stringsfile)
  {
    puts("Usage: po2strings [-m] filename.po filename.strings");
    return (1);
  }

 /*
  * Read strings from the .po file and write to the .strings file...
  */

  if ((po = cupsFileOpen(pofile, "r")) == NULL)
  {
    perror(pofile);
    return (1);
  }

  if ((strings = cupsFileOpen(stringsfile, "w")) == NULL)
  {
    perror(stringsfile);
    cupsFileClose(po);
    return (1);
  }

  msgid = msgstr = NULL;

  while (cupsFileGets(po, s, sizeof(s)) != NULL)
  {
    if (s[0] == '#' && s[1] == '.')
    {
     /*
      * Copy comment string...
      */

      if (msgid && msgstr)
      {
       /*
        * First output the last localization string...
	*/

	if (*msgid)
	  cupsFilePrintf(strings, "\"%s\" = \"%s\";\n", msgid,
			 (use_msgid || !*msgstr) ? msgid : msgstr);

	free(msgid);
	free(msgstr);
	msgid = msgstr = NULL;
      }

      cupsFilePrintf(strings, "//%s\n", s + 2);
    }
    else if (s[0] == '#' || !s[0])
    {
     /*
      * Skip blank and file comment lines...
      */

      continue;
    }
    else
    {
     /*
      * Strip the trailing quote...
      */

      if ((ptr = strrchr(s, '\"')) == NULL)
	continue;

      *ptr = '\0';

     /*
      * Find start of value...
      */

      if ((ptr = strchr(s, '\"')) == NULL)
	continue;

      ptr ++;

     /*
      * Create or add to a message...
      */

      if (!strncmp(s, "msgid", 5))
      {
       /*
	* Output previous message as needed...
	*/

        if (msgid && msgstr)
	{
	  if (*msgid)
            cupsFilePrintf(strings, "\"%s\" = \"%s\";\n", msgid, normalize_string((use_msgid || !*msgstr) ? msgid : msgstr, normalized, sizeof(normalized)));
	}

	if (msgid)
	  free(msgid);

	if (msgstr)
	  free(msgstr);

        msgid  = strdup(ptr);
	msgstr = NULL;
      }
      else if (s[0] == '\"' && (msgid || msgstr))
      {
       /*
	* Append to current string...
	*/

        size_t ptrlen = strlen(ptr);	/* Length of string */

	length = strlen(msgstr ? msgstr : msgid);

	if ((temp = realloc(msgstr ? msgstr : msgid,
			    length + ptrlen + 1)) == NULL)
	{
	  free(msgid);
	  if (msgstr)
	    free(msgstr);
	  perror("Unable to allocate string");
	  return (1);
	}

	if (msgstr)
	{
	 /*
	  * Copy the new portion to the end of the msgstr string - safe
	  * to use strcpy because the buffer is allocated to the correct
	  * size...
	  */

	  msgstr = temp;

	  memcpy(msgstr + length, ptr, ptrlen + 1);
	}
	else
	{
	 /*
	  * Copy the new portion to the end of the msgid string - safe
	  * to use strcpy because the buffer is allocated to the correct
	  * size...
	  */

	  msgid = temp;

	  memcpy(msgid + length, ptr, ptrlen + 1);
	}
      }
      else if (!strncmp(s, "msgstr", 6) && msgid)
      {
       /*
	* Set the string...
	*/

        if (msgstr)
          free(msgstr);

	if ((msgstr = strdup(ptr)) == NULL)
	{
	  free(msgid);
	  perror("Unable to allocate msgstr");
	  return (1);
	}
      }
    }
  }

  if (msgid && msgstr)
  {
    if (*msgid)
      cupsFilePrintf(strings, "\"%s\" = \"%s\";\n", msgid, normalize_string((use_msgid || !*msgstr) ? msgid : msgstr, normalized, sizeof(normalized)));
  }

  if (msgid)
    free(msgid);

  if (msgstr)
    free(msgstr);

  cupsFileClose(po);
  cupsFileClose(strings);

  return (0);
}


/*
 * 'normalize_string()' - Normalize a msgid string.
 *
 * This function converts ASCII ellipsis and double quotes to their Unicode
 * counterparts.
 */

static char *				/* O - Normalized string */
normalize_string(const char *idstr,	/* I - msgid string */
                 char       *buffer,	/* I - Normalized string buffer */
                 size_t     bufsize)	/* I - Size of string buffer */
{
  char	*bufptr = buffer,		/* Pointer into buffer */
	*bufend = buffer + bufsize - 3;	/* End of buffer */
  int	quote = 0,			/* Quote direction */
	html = 0;			/* HTML text */


  while (*idstr && bufptr < bufend)
  {
    if (!strncmp(idstr, "<A ", 3))
      html = 1;
    else if (html && *idstr == '>')
      html = 0;

    if (*idstr == '.' && idstr[1] == '.' && idstr[2] == '.')
    {
     /*
      * Convert ... to Unicode ellipsis...
      */

      *bufptr++ = (char)0xE2;
      *bufptr++ = (char)0x80;
      *bufptr++ = (char)0xA6;
      idstr += 2;
    }
    else if (!html && *idstr == '\\' && idstr[1] == '\"')
    {
      if (quote)
      {
       /*
        * Convert second \" to Unicode right (curley) double quote.
        */

	*bufptr++ = (char)0xE2;
	*bufptr++ = (char)0x80;
	*bufptr++ = (char)0x9D;
	quote     = 0;
      }
      else if (strchr(idstr + 2, '\"') != NULL)
      {
       /*
        * Convert first \" to Unicode left (curley) double quote.
        */

	*bufptr++ = (char)0xE2;
	*bufptr++ = (char)0x80;
	*bufptr++ = (char)0x9C;
	quote     = 1;
      }
      else
      {
       /*
        * Convert lone \" to Unicode double prime.
        */

        *bufptr++ = (char)0xE2;
        *bufptr++ = (char)0x80;
        *bufptr++ = (char)0xB3;
      }

      idstr ++;
    }
    else if (*idstr == '\'')
    {
      if (strchr(idstr + 1, '\'') == NULL || quote)
      {
       /*
        * Convert second ' (or ' used for a contraction) to Unicode right
        * (curley) single quote.
        */

	*bufptr++ = (char)0xE2;
	*bufptr++ = (char)0x80;
	*bufptr++ = (char)0x99;
	quote     = 0;
      }
      else
      {
       /*
        * Convert first ' to Unicode left (curley) single quote.
        */

	*bufptr++ = (char)0xE2;
	*bufptr++ = (char)0x80;
	*bufptr++ = (char)0x98;
	quote     = 1;
      }
    }
    else
      *bufptr++ = *idstr;

    idstr ++;
  }

  *bufptr = '\0';

  return (buffer);
}
