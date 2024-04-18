/*
 * I18N/language support for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright 2007-2017 by Apple Inc.
 * Copyright 1997-2007 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more information.
 */

/*
 * Include necessary headers...
 */

#include "cups-private.h"
#include "debug-internal.h"
#ifdef HAVE_LANGINFO_H
#  include <langinfo.h>
#endif /* HAVE_LANGINFO_H */
#ifdef _WIN32
#  include <io.h>
#else
#  include <unistd.h>
#endif /* _WIN32 */
#ifdef HAVE_COREFOUNDATION_H
#  include <CoreFoundation/CoreFoundation.h>
#endif /* HAVE_COREFOUNDATION_H */


/*
 * Local globals...
 */

static cups_mutex_t	lang_mutex = CUPS_MUTEX_INITIALIZER;
					/* Mutex to control access to cache */
static cups_lang_t	*lang_cache = NULL;
					/* Language string cache */
static const char * const lang_encodings[] =
			{		/* Encoding strings */
			  "us-ascii",		"iso-8859-1",
			  "iso-8859-2",		"iso-8859-3",
			  "iso-8859-4",		"iso-8859-5",
			  "iso-8859-6",		"iso-8859-7",
			  "iso-8859-8",		"iso-8859-9",
			  "iso-8859-10",	"utf-8",
			  "iso-8859-13",	"iso-8859-14",
			  "iso-8859-15",	"cp874",
			  "cp1250",		"cp1251",
			  "cp1252",		"cp1253",
			  "cp1254",		"cp1255",
			  "cp1256",		"cp1257",
			  "cp1258",		"koi8-r",
			  "koi8-u",		"iso-8859-11",
			  "iso-8859-16",	"mac",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "cp932",		"cp936",
			  "cp949",		"cp950",
			  "cp1361",		"bg18030",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "unknown",		"unknown",
			  "euc-cn",		"euc-jp",
			  "euc-kr",		"euc-tw",
			  "shift_jisx0213"
			};


/*
 * Local functions...
 */


static cups_lang_t	*cups_cache_lookup(const char *name, cups_encoding_t encoding);
static int		cups_message_compare(_cups_message_t *m1, _cups_message_t *m2,
                                void *data);
static _cups_message_t	*cups_message_copy(_cups_message_t *m, void *data);
static void		cups_message_free(_cups_message_t *m, void *data);
static void		cups_message_load(cups_lang_t *lang);
static void		cups_message_puts(cups_file_t *fp, const char *s);
static int		cups_read_strings(cups_file_t *fp, int flags, cups_array_t *a);
static void		cups_unquote(char *d, const char *s);


/*
 * '_cupsEncodingName()' - Return the character encoding name string
 *                         for the given encoding enumeration.
 */

const char *				/* O - Character encoding */
_cupsEncodingName(
    cups_encoding_t encoding)		/* I - Encoding value */
{
  if (encoding < CUPS_US_ASCII ||
      encoding >= (cups_encoding_t)(sizeof(lang_encodings) / sizeof(lang_encodings[0])))
  {
    DEBUG_printf("1_cupsEncodingName(encoding=%d) = out of range (\"%s\")", encoding, lang_encodings[0]);
    return (lang_encodings[0]);
  }
  else
  {
    DEBUG_printf("1_cupsEncodingName(encoding=%d) = \"%s\"", encoding, lang_encodings[encoding]);
    return (lang_encodings[encoding]);
  }
}


/*
 * 'cupsLangDefault()' - Return the default language.
 */

cups_lang_t *				/* O - Language data */
cupsLangDefault(void)
{
  return (cupsLangGet(NULL));
}


/*
 * 'cupsLangEncoding()' - Return the character encoding (us-ascii, etc.)
 *                        for the given language.
 */

const char *				/* O - Character encoding */
cupsLangEncoding(cups_lang_t *lang)	/* I - Language data */
{
  if (lang == NULL)
    return ((char*)lang_encodings[0]);
  else
    return ((char*)lang_encodings[lang->encoding]);
}


/*
 * 'cupsLangFlush()' - Flush all language data out of the cache.
 */

void
cupsLangFlush(void)
{
  cups_lang_t	*lang,			/* Current language */
		*next;			/* Next language */


 /*
  * Free all languages in the cache...
  */

  cupsMutexLock(&lang_mutex);

  for (lang = lang_cache; lang != NULL; lang = next)
  {
   /*
    * Free all messages...
    */

    _cupsMessageFree(lang->strings);

   /*
    * Then free the language structure itself...
    */

    next = lang->next;
    free(lang);
  }

  lang_cache = NULL;

  cupsMutexUnlock(&lang_mutex);
}


/*
 * 'cupsLangFree()' - Free language data.
 *
 * This does not actually free anything; use @link cupsLangFlush@ for that.
 */

void
cupsLangFree(cups_lang_t *lang)		/* I - Language to free */
{
  cupsMutexLock(&lang_mutex);

  if (lang != NULL && lang->used > 0)
    lang->used --;

  cupsMutexUnlock(&lang_mutex);
}


/*
 * 'cupsLangGet()' - Get a language.
 */

cups_lang_t *				/* O - Language data */
cupsLangGet(const char *language)	/* I - Language or locale */
{
  int			i;		/* Looping var */
  char			locale[255];	/* Copy of locale name */
  char			langname[16],	/* Requested language name */
			country[16],	/* Country code */
			charset[16],	/* Character set */
			*csptr,		/* Pointer to CODESET string */
			*ptr,		/* Pointer into language/charset */
			real[48];	/* Real language name */
  cups_encoding_t	encoding;	/* Encoding to use */
  cups_lang_t		*lang;		/* Current language... */
  static const char * const locale_encodings[] =
		{			/* Locale charset names */
		  "ASCII",	"ISO88591",	"ISO88592",	"ISO88593",
		  "ISO88594",	"ISO88595",	"ISO88596",	"ISO88597",
		  "ISO88598",	"ISO88599",	"ISO885910",	"UTF8",
		  "ISO885913",	"ISO885914",	"ISO885915",	"CP874",
		  "CP1250",	"CP1251",	"CP1252",	"CP1253",
		  "CP1254",	"CP1255",	"CP1256",	"CP1257",
		  "CP1258",	"KOI8R",	"KOI8U",	"ISO885911",
		  "ISO885916",	"MACROMAN",	"",		"",

		  "",		"",		"",		"",
		  "",		"",		"",		"",
		  "",		"",		"",		"",
		  "",		"",		"",		"",
		  "",		"",		"",		"",
		  "",		"",		"",		"",
		  "",		"",		"",		"",
		  "",		"",		"",		"",

		  "CP932",	"CP936",	"CP949",	"CP950",
		  "CP1361",	"GB18030",	"",		"",
		  "",		"",		"",		"",
		  "",		"",		"",		"",
		  "",		"",		"",		"",
		  "",		"",		"",		"",
		  "",		"",		"",		"",
		  "",		"",		"",		"",

		  "",		"",		"",		"",
		  "",		"",		"",		"",
		  "",		"",		"",		"",
		  "",		"",		"",		"",
		  "",		"",		"",		"",
		  "",		"",		"",		"",
		  "",		"",		"",		"",
		  "",		"",		"",		"",

		  "EUCCN",	"EUCJP",	"EUCKR",	"EUCTW",
		  "SHIFT_JISX0213"
		};


  DEBUG_printf("2cupsLangGet(language=\"%s\")", language);

 /*
  * Set the charset to "unknown"...
  */

  charset[0] = '\0';

 /*
  * Use setlocale() to determine the currently set locale, and then
  * fallback to environment variables to avoid setting the locale,
  * since setlocale() is not thread-safe!
  */

  if (!language)
  {
   /*
    * First see if the locale has been set; if it is still "C" or
    * "POSIX", use the environment to get the default...
    */

#ifdef LC_MESSAGES
    ptr = setlocale(LC_MESSAGES, NULL);
#else
    ptr = setlocale(LC_ALL, NULL);
#endif /* LC_MESSAGES */

    DEBUG_printf("4cupsLangGet: current locale is \"%s\"", ptr);

    if (!ptr || !strcmp(ptr, "C") || !strcmp(ptr, "POSIX"))
    {
     /*
      * Get the character set from the LC_CTYPE locale setting...
      */

      if ((ptr = getenv("LC_CTYPE")) == NULL)
        if ((ptr = getenv("LC_ALL")) == NULL)
	  if ((ptr = getenv("LANG")) == NULL)
	    ptr = "en_US";

      if ((csptr = strchr(ptr, '.')) != NULL)
      {
       /*
        * Extract the character set from the environment...
	*/

	for (ptr = charset, csptr ++; *csptr; csptr ++)
	  if (ptr < (charset + sizeof(charset) - 1) && _cups_isalnum(*csptr))
	    *ptr++ = *csptr;

        *ptr = '\0';
      }

     /*
      * Get the locale for messages from the LC_MESSAGES locale setting...
      */

      if ((ptr = getenv("LC_MESSAGES")) == NULL)
        if ((ptr = getenv("LC_ALL")) == NULL)
	  if ((ptr = getenv("LANG")) == NULL)
	    ptr = "en_US";
    }

    if (ptr)
    {
      cupsCopyString(locale, ptr, sizeof(locale));
      language = locale;

     /*
      * CUPS STR #2575: Map "nb" to "no" for back-compatibility...
      */

      if (!strncmp(locale, "nb", 2))
        locale[1] = 'o';

      DEBUG_printf("4cupsLangGet: new language value is \"%s\"", language);
    }
  }

 /*
  * If "language" is NULL at this point, then chances are we are using
  * a language that is not installed for the base OS.
  */

  if (!language)
  {
   /*
    * Switch to the POSIX ("C") locale...
    */

    language = "C";
  }

#ifdef CODESET
 /*
  * On systems that support the nl_langinfo(CODESET) call, use
  * this value as the character set...
  */

  if (!charset[0] && (csptr = nl_langinfo(CODESET)) != NULL)
  {
   /*
    * Copy all of the letters and numbers in the CODESET string...
    */

    for (ptr = charset; *csptr; csptr ++)
      if (_cups_isalnum(*csptr) && ptr < (charset + sizeof(charset) - 1))
        *ptr++ = *csptr;

    *ptr = '\0';

    DEBUG_printf("4cupsLangGet: charset set to \"%s\" via nl_langinfo(CODESET)...", charset);
  }
#endif /* CODESET */

 /*
  * If we don't have a character set by now, default to UTF-8...
  */

  if (!charset[0])
    cupsCopyString(charset, "UTF8", sizeof(charset));

 /*
  * Parse the language string passed in to a locale string. "C" is the
  * standard POSIX locale and is copied unchanged.  Otherwise the
  * language string is converted from ll-cc[.charset] (language-country)
  * to ll_CC[.CHARSET] to match the file naming convention used by all
  * POSIX-compliant operating systems.  Invalid language names are mapped
  * to the POSIX locale.
  */

  country[0] = '\0';

  if (language == NULL || !language[0] ||
      !strcmp(language, "POSIX"))
    cupsCopyString(langname, "C", sizeof(langname));
  else
  {
   /*
    * Copy the parts of the locale string over safely...
    */

    for (ptr = langname; *language; language ++)
      if (*language == '_' || *language == '-' || *language == '.')
	break;
      else if (ptr < (langname + sizeof(langname) - 1))
        *ptr++ = (char)tolower(*language & 255);

    *ptr = '\0';

    if (*language == '_' || *language == '-')
    {
     /*
      * Copy the country code...
      */

      for (language ++, ptr = country; *language; language ++)
	if (*language == '.')
	  break;
	else if (ptr < (country + sizeof(country) - 1))
          *ptr++ = (char)toupper(*language & 255);

      *ptr = '\0';

     /*
      * Map Chinese region codes to legacy country codes.
      */

      if (!strcmp(language, "zh") && !strcmp(country, "HANS"))
        cupsCopyString(country, "CN", sizeof(country));
      if (!strcmp(language, "zh") && !strcmp(country, "HANT"))
        cupsCopyString(country, "TW", sizeof(country));
    }

    if (*language == '.' && !charset[0])
    {
     /*
      * Copy the encoding...
      */

      for (language ++, ptr = charset; *language; language ++)
        if (_cups_isalnum(*language) && ptr < (charset + sizeof(charset) - 1))
          *ptr++ = (char)toupper(*language & 255);

      *ptr = '\0';
    }

   /*
    * Force a POSIX locale for an invalid language name...
    */

    if (strlen(langname) != 2 && strlen(langname) != 3)
    {
      cupsCopyString(langname, "C", sizeof(langname));
      country[0] = '\0';
      charset[0] = '\0';
    }
  }

  DEBUG_printf("4cupsLangGet: langname=\"%s\", country=\"%s\", charset=\"%s\"", langname, country, charset);

 /*
  * Figure out the desired encoding...
  */

  encoding = CUPS_AUTO_ENCODING;

  if (charset[0])
  {
    for (i = 0;
         i < (int)(sizeof(locale_encodings) / sizeof(locale_encodings[0]));
	 i ++)
      if (!_cups_strcasecmp(charset, locale_encodings[i]))
      {
	encoding = (cups_encoding_t)i;
	break;
      }

    if (encoding == CUPS_AUTO_ENCODING)
    {
     /*
      * Map alternate names for various character sets...
      */

      if (!_cups_strcasecmp(charset, "iso-2022-jp") ||
          !_cups_strcasecmp(charset, "sjis"))
	encoding = CUPS_WINDOWS_932;
      else if (!_cups_strcasecmp(charset, "iso-2022-cn"))
	encoding = CUPS_WINDOWS_936;
      else if (!_cups_strcasecmp(charset, "iso-2022-kr"))
	encoding = CUPS_WINDOWS_949;
      else if (!_cups_strcasecmp(charset, "big5"))
	encoding = CUPS_WINDOWS_950;
    }
  }

  DEBUG_printf("4cupsLangGet: encoding=%d(%s)", encoding, encoding == CUPS_AUTO_ENCODING ? "auto" : lang_encodings[encoding]);

 /*
  * See if we already have this language/country loaded...
  */

  if (country[0])
    snprintf(real, sizeof(real), "%s_%s", langname, country);
  else
    cupsCopyString(real, langname, sizeof(real));

  cupsMutexLock(&lang_mutex);

  if ((lang = cups_cache_lookup(real, encoding)) != NULL)
  {
    cupsMutexUnlock(&lang_mutex);

    DEBUG_printf("3cupsLangGet: Using cached copy of \"%s\"...", real);

    return (lang);
  }

 /*
  * See if there is a free language available; if so, use that
  * record...
  */

  for (lang = lang_cache; lang != NULL; lang = lang->next)
    if (lang->used == 0)
      break;

  if (lang == NULL)
  {
   /*
    * Allocate memory for the language and add it to the cache.
    */

    if ((lang = calloc(1, sizeof(cups_lang_t))) == NULL)
    {
      cupsMutexUnlock(&lang_mutex);

      return (NULL);
    }

    lang->next = lang_cache;
    lang_cache = lang;
  }
  else
  {
   /*
    * Free all old strings as needed...
    */

    _cupsMessageFree(lang->strings);
    lang->strings = NULL;
  }

 /*
  * Then assign the language and encoding fields...
  */

  lang->used ++;
  cupsCopyString(lang->language, real, sizeof(lang->language));

  if (encoding != CUPS_AUTO_ENCODING)
    lang->encoding = encoding;
  else
    lang->encoding = CUPS_UTF8;

 /*
  * Return...
  */

  cupsMutexUnlock(&lang_mutex);

  return (lang);
}


/*
 * '_cupsLangString()' - Get a message string.
 *
 * The returned string is UTF-8 encoded; use cupsUTF8ToCharset() to
 * convert the string to the language encoding.
 */

const char *				/* O - Localized message */
_cupsLangString(cups_lang_t *lang,	/* I - Language */
                const char  *message)	/* I - Message */
{
  const char *s;			/* Localized message */


  DEBUG_printf("_cupsLangString(lang=%p, message=\"%s\")", (void *)lang, message);

 /*
  * Range check input...
  */

  if (!lang || !message || !*message)
    return (message);

  cupsMutexLock(&lang_mutex);

 /*
  * Load the message catalog if needed...
  */

  if (!lang->strings)
    cups_message_load(lang);

  s = _cupsMessageLookup(lang->strings, message);

  cupsMutexUnlock(&lang_mutex);

  return (s);
}


/*
 * '_cupsMessageFree()' - Free a messages array.
 */

void
_cupsMessageFree(cups_array_t *a)	/* I - Message array */
{
 /*
  * Free the array...
  */

  cupsArrayDelete(a);
}


/*
 * '_cupsMessageLoad()' - Load a .po or .strings file into a messages array.
 */

cups_array_t *				/* O - New message array */
_cupsMessageLoad(cups_array_t *a,	/* I - Existing message array */
                 const char   *filename,/* I - Message catalog to load */
                 int          flags)	/* I - Load flags */
{
  cups_file_t		*fp;		/* Message file */
  _cups_message_t	m;		/* Current message */
  char			s[4096],	/* String buffer */
			msg_id[4096],	/* Message ID buffer */
			msg_str[4096],	/* Message string buffer */
			*ptr;		/* Pointer into buffer */


  DEBUG_printf("4_cupsMessageLoad(a=%p, filename=\"%s\", flags=%d)", (void *)a, filename, flags);

  if (!a)
  {
   /*
    * Create an array to hold the messages...
    */

    if ((a = _cupsMessageNew(NULL)) == NULL)
    {
      DEBUG_puts("5_cupsMessageLoad: Unable to allocate array!");
      return (NULL);
    }
  }

 /*
  * Open the message catalog file...
  */

  if ((fp = cupsFileOpen(filename, "r")) == NULL)
  {
    DEBUG_printf("5_cupsMessageLoad: Unable to open file: %s", strerror(errno));
    return (a);
  }

  if (flags & _CUPS_MESSAGE_STRINGS)
  {
    while (cups_read_strings(fp, flags, a));
  }
  else
  {
   /*
    * Read messages from the catalog file until EOF...
    *
    * The format is the GNU gettext .po format, which is fairly simple:
    *
    *     msgid "some text"
    *     msgstr "localized text"
    *
    * The ID and localized text can span multiple lines using the form:
    *
    *     msgid ""
    *     "some long text"
    *     msgstr ""
    *     "localized text spanning "
    *     "multiple lines"
    */

    m.msg = NULL;
    m.str = NULL;

    while (cupsFileGets(fp, s, sizeof(s)) != NULL)
    {
     /*
      * Skip blank and comment lines...
      */

      if (s[0] == '#' || !s[0])
	continue;

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
      * Unquote the text...
      */

      if (flags & _CUPS_MESSAGE_UNQUOTE)
	cups_unquote(ptr, ptr);

     /*
      * Create or add to a message...
      */

      if (!strncmp(s, "msgid", 5))
      {
       /*
	* Add previous message as needed...
	*/

	if (m.str && (m.str[0] || (flags & _CUPS_MESSAGE_EMPTY)))
	{
	  DEBUG_printf("5_cupsMessageLoad: Adding \"%s\"=\"%s\"", m.msg, m.str);
	  cupsArrayAdd(a, &m);
	}

       /*
	* Create a new message with the given msgid string...
	*/

        cupsCopyString(msg_id, ptr, sizeof(msg_id));
        m.msg = msg_id;
        m.str = NULL;
      }
      else if (s[0] == '\"' && (m.msg || m.str))
      {
       /*
	* Append to current string...
	*/

	if (m.str)
          cupsConcatString(msg_str, ptr, sizeof(msg_str));
	else
          cupsConcatString(msg_id, ptr, sizeof(msg_id));
      }
      else if (!strncmp(s, "msgstr", 6) && m.msg)
      {
       /*
	* Set the string...
	*/

        cupsCopyString(msg_str, ptr, sizeof(msg_str));
        m.str = msg_str;
      }
    }

   /*
    * Add the last message string to the array as needed...
    */

    if (m.msg && m.str && (m.str[0] || (flags & _CUPS_MESSAGE_EMPTY)))
    {
      DEBUG_printf("5_cupsMessageLoad: Adding \"%s\"=\"%s\"", m.msg, m.str);
      cupsArrayAdd(a, &m);
    }
  }

 /*
  * Close the message catalog file and return the new array...
  */

  cupsFileClose(fp);

  DEBUG_printf("5_cupsMessageLoad: Returning %d messages...", cupsArrayCount(a));

  return (a);
}


/*
 * '_cupsMessageLookup()' - Lookup a message string.
 */

const char *				/* O - Localized message */
_cupsMessageLookup(cups_array_t *a,	/* I - Message array */
                   const char   *m)	/* I - Message */
{
  _cups_message_t	key,		/* Search key */
			*match;		/* Matching message */


  DEBUG_printf("_cupsMessageLookup(a=%p, m=\"%s\")", (void *)a, m);

 /*
  * Lookup the message string; if it doesn't exist in the catalog,
  * then return the message that was passed to us...
  */

  key.msg = (char *)m;
  match   = (_cups_message_t *)cupsArrayFind(a, &key);

  if (match && match->str)
    return (match->str);
  else
    return (m);
}


/*
 * '_cupsMessageNew()' - Make a new message catalog array.
 */

cups_array_t *				/* O - Array */
_cupsMessageNew(void *context)		/* I - User data */
{
  return (cupsArrayNew3((cups_array_cb_t)cups_message_compare, context,
                        (cups_ahash_cb_t)NULL, 0,
			(cups_acopy_cb_t)cups_message_copy,
			(cups_afree_cb_t)cups_message_free));
}


/*
 * '_cupsMessageSave()' - Save a message catalog array.
 */

int					/* O - 0 on success, -1 on failure */
_cupsMessageSave(const char   *filename,/* I - Output filename */
                 int          flags,	/* I - Format flags */
                 cups_array_t *a)	/* I - Message array */
{
  cups_file_t		*fp;		/* Output file */
  _cups_message_t	*m;		/* Current message */


 /*
  * Output message catalog file...
  */

  if ((fp = cupsFileOpen(filename, "w")) == NULL)
    return (-1);

 /*
  * Write each message...
  */

  if (flags & _CUPS_MESSAGE_STRINGS)
  {
    for (m = (_cups_message_t *)cupsArrayFirst(a); m; m = (_cups_message_t *)cupsArrayNext(a))
    {
      cupsFilePuts(fp, "\"");
      cups_message_puts(fp, m->msg);
      cupsFilePuts(fp, "\" = \"");
      cups_message_puts(fp, m->str);
      cupsFilePuts(fp, "\";\n");
    }
  }
  else
  {
    for (m = (_cups_message_t *)cupsArrayFirst(a); m; m = (_cups_message_t *)cupsArrayNext(a))
    {
      cupsFilePuts(fp, "msgid \"");
      cups_message_puts(fp, m->msg);
      cupsFilePuts(fp, "\"\nmsgstr \"");
      cups_message_puts(fp, m->str);
      cupsFilePuts(fp, "\"\n");
    }
  }

  return (cupsFileClose(fp));
}


/*
 * 'cups_cache_lookup()' - Lookup a language in the cache...
 */

static cups_lang_t *			/* O - Language data or NULL */
cups_cache_lookup(
    const char      *name,		/* I - Name of locale */
    cups_encoding_t encoding)		/* I - Encoding of locale */
{
  cups_lang_t	*lang;			/* Current language */


  DEBUG_printf("7cups_cache_lookup(name=\"%s\", encoding=%d(%s))", name, encoding, encoding == CUPS_AUTO_ENCODING ? "auto" : lang_encodings[encoding]);

 /*
  * Loop through the cache and return a match if found...
  */

  for (lang = lang_cache; lang != NULL; lang = lang->next)
  {
    DEBUG_printf("9cups_cache_lookup: lang=%p, language=\"%s\", encoding=%d(%s)", (void *)lang, lang->language, lang->encoding, lang_encodings[lang->encoding]);

    if (!strcmp(lang->language, name) &&
        (encoding == CUPS_AUTO_ENCODING || encoding == lang->encoding))
    {
      lang->used ++;

      DEBUG_puts("8cups_cache_lookup: returning match!");

      return (lang);
    }
  }

  DEBUG_puts("8cups_cache_lookup: returning NULL!");

  return (NULL);
}


/*
 * 'cups_message_compare()' - Compare two messages.
 */

static int                                /* O - Result of comparison */
cups_message_compare(_cups_message_t *m1, /* I - First message */
                     _cups_message_t *m2, /* I - Second message */
                     void *data)          /* Unused */
{
  (void)data;
  return (strcmp(m1->msg, m2->msg));
}


//
// 'cups_message_copy()' - Copy a message.
//

static _cups_message_t *		// O - New message
cups_message_copy(_cups_message_t *m,	// I - Message
                  void            *data)// I - Callback data (unused)
{
  _cups_message_t	*newm;		// New message


  (void)data;

  if ((newm = (_cups_message_t *)malloc(sizeof(_cups_message_t))) != NULL)
  {
    newm->msg = strdup(m->msg);
    newm->str = strdup(m->str);

    if (!newm->msg || !newm->str)
    {
      free(newm->msg);
      free(newm->str);
      free(newm);

      newm = NULL;
    }
  }

  return (newm);
}

/*
 * 'cups_message_free()' - Free a message.
 */

static void
cups_message_free(_cups_message_t *m,	/* I - Message */
		  void            *data)/* I - Callback data (unused) */
{
  (void)data;

  free(m->msg);
  free(m->str);
  free(m);
}


/*
 * 'cups_message_load()' - Load the message catalog for a language.
 */

static void
cups_message_load(cups_lang_t *lang)	/* I - Language */
{
  char			filename[1024];	/* Filename for language locale file */
  _cups_globals_t	*cg = _cupsGlobals();
  					/* Pointer to library globals */


  snprintf(filename, sizeof(filename), "%s/%s/cups_%s.po", cg->localedir,
	   lang->language, lang->language);

  if (strchr(lang->language, '_') && access(filename, 0))
  {
   /*
    * Country localization not available, look for generic localization...
    */

    snprintf(filename, sizeof(filename), "%s/%.2s/cups_%.2s.po", cg->localedir,
             lang->language, lang->language);

    if (access(filename, 0))
    {
     /*
      * No generic localization, so use POSIX...
      */

      DEBUG_printf("4cups_message_load: access(\"%s\", 0): %s", filename, strerror(errno));

      snprintf(filename, sizeof(filename), "%s/C/cups_C.po", cg->localedir);
    }
  }

 /*
  * Read the strings from the file...
  */

  lang->strings = _cupsMessageLoad(NULL, filename, _CUPS_MESSAGE_UNQUOTE);
}


/*
 * 'cups_message_puts()' - Write a message string with quoting.
 */

static void
cups_message_puts(cups_file_t *fp,	/* I - File to write to */
                  const char  *s)	/* I - String to write */
{
  const char	*start,			/* Start of substring */
		*ptr;			/* Pointer into string */


  for (start = s, ptr = s; *ptr; ptr ++)
  {
    if (strchr("\\\"\n\t", *ptr))
    {
      if (ptr > start)
      {
	cupsFileWrite(fp, start, (size_t)(ptr - start));
	start = ptr + 1;
      }

      if (*ptr == '\\')
        cupsFileWrite(fp, "\\\\", 2);
      else if (*ptr == '\"')
        cupsFileWrite(fp, "\\\"", 2);
      else if (*ptr == '\n')
        cupsFileWrite(fp, "\\n", 2);
      else /* if (*ptr == '\t') */
        cupsFileWrite(fp, "\\t", 2);
    }
  }

  if (ptr > start)
    cupsFileWrite(fp, start, (size_t)(ptr - start));
}


/*
 * 'cups_read_strings()' - Read a pair of strings from a .strings file.
 */

static int				/* O - 1 on success, 0 on failure */
cups_read_strings(cups_file_t  *fp,	/* I - .strings file */
                  int          flags,	/* I - CUPS_MESSAGE_xxx flags */
		  cups_array_t *a)	/* I - Message catalog array */
{
  char			buffer[8192],	/* Line buffer */
			*bufptr,	/* Pointer into buffer */
			*msg,		/* Pointer to start of message */
			*str;		/* Pointer to start of translation string */
  _cups_message_t	m;		/* New message */


  while (cupsFileGets(fp, buffer, sizeof(buffer)))
  {
   /*
    * Skip any line (comments, blanks, etc.) that isn't:
    *
    *   "message" = "translation";
    */

    for (bufptr = buffer; *bufptr && isspace(*bufptr & 255); bufptr ++);

    if (*bufptr != '\"')
      continue;

   /*
    * Find the end of the message...
    */

    bufptr ++;
    for (msg = bufptr; *bufptr && *bufptr != '\"'; bufptr ++)
      if (*bufptr == '\\' && bufptr[1])
        bufptr ++;

    if (!*bufptr)
      continue;

    *bufptr++ = '\0';

    if (flags & _CUPS_MESSAGE_UNQUOTE)
      cups_unquote(msg, msg);

   /*
    * Find the start of the translation...
    */

    while (*bufptr && isspace(*bufptr & 255))
      bufptr ++;

    if (*bufptr != '=')
      continue;

    bufptr ++;
    while (*bufptr && isspace(*bufptr & 255))
      bufptr ++;

    if (*bufptr != '\"')
      continue;

   /*
    * Find the end of the translation...
    */

    bufptr ++;
    for (str = bufptr; *bufptr && *bufptr != '\"'; bufptr ++)
      if (*bufptr == '\\' && bufptr[1])
        bufptr ++;

    if (!*bufptr)
      continue;

    *bufptr++ = '\0';

    if (flags & _CUPS_MESSAGE_UNQUOTE)
      cups_unquote(str, str);

   /*
    * If we get this far we have a valid pair of strings, add them...
    */

    m.msg = msg;
    m.str = str;

    if (!cupsArrayFind(a, &m))
      cupsArrayAdd(a, &m);

    return (1);
  }

 /*
  * No more strings...
  */

  return (0);
}


/*
 * 'cups_unquote()' - Unquote characters in strings...
 */

static void
cups_unquote(char       *d,		/* O - Unquoted string */
             const char *s)		/* I - Original string */
{
  while (*s)
  {
    if (*s == '\\')
    {
      s ++;
      if (isdigit(*s))
      {
	*d = 0;

	while (isdigit(*s))
	{
	  *d = *d * 8 + *s - '0';
	  s ++;
	}

	d ++;
      }
      else
      {
	if (*s == 'n')
	  *d ++ = '\n';
	else if (*s == 'r')
	  *d ++ = '\r';
	else if (*s == 't')
	  *d ++ = '\t';
	else
	  *d++ = *s;

	s ++;
      }
    }
    else
      *d++ = *s++;
  }

  *d = '\0';
}
