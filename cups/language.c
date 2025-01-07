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

static _cups_mutex_t	lang_mutex = _CUPS_MUTEX_INITIALIZER;
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

#ifdef __APPLE__
typedef struct
{
  const char * const language;		/* Language ID */
  const char * const locale;		/* Locale ID */
} _apple_language_locale_t;

static const _apple_language_locale_t apple_language_locale[] =
{					/* Language to locale ID LUT */
  { "en",         "en_US" },
  { "nb",         "no" },
  { "nb_NO",      "no" },
  { "zh-Hans",    "zh_CN" },
  { "zh_HANS",    "zh_CN" },
  { "zh-Hant",    "zh_TW" },
  { "zh_HANT",    "zh_TW" },
  { "zh-Hant_CN", "zh_TW" }
};
#endif /* __APPLE__ */


/*
 * Local functions...
 */


#ifdef __APPLE__
static const char	*appleLangDefault(void);
#  ifdef CUPS_BUNDLEDIR
#    ifndef CF_RETURNS_RETAINED
#      if __has_feature(attribute_cf_returns_retained)
#        define CF_RETURNS_RETAINED __attribute__((cf_returns_retained))
#      else
#        define CF_RETURNS_RETAINED
#      endif /* __has_feature(attribute_cf_returns_retained) */
#    endif /* !CF_RETURNED_RETAINED */
static cups_array_t	*appleMessageLoad(const char *locale) CF_RETURNS_RETAINED;
#  endif /* CUPS_BUNDLEDIR */
#endif /* __APPLE__ */
static cups_lang_t	*cups_cache_lookup(const char *name, cups_encoding_t encoding);
static int		cups_message_compare(_cups_message_t *m1, _cups_message_t *m2);
static void		cups_message_free(_cups_message_t *m);
static void		cups_message_load(cups_lang_t *lang);
static void		cups_message_puts(cups_file_t *fp, const char *s);
static int		cups_read_strings(cups_file_t *fp, int flags, cups_array_t *a);
static void		cups_unquote(char *d, const char *s);


#ifdef __APPLE__
/*
 * '_cupsAppleLanguage()' - Get the Apple language identifier associated with a
 *                          locale ID.
 */

const char *				/* O - Language ID */
_cupsAppleLanguage(const char *locale,	/* I - Locale ID */
                   char       *language,/* I - Language ID buffer */
                   size_t     langsize)	/* I - Size of language ID buffer */
{
  int		i;			/* Looping var */
  CFStringRef	localeid,		/* CF locale identifier */
		langid;			/* CF language identifier */


 /*
  * Copy the locale name and convert, as needed, to the Apple-specific
  * locale identifier...
  */

  switch (strlen(locale))
  {
    default :
        /*
	 * Invalid locale...
	 */

	 strlcpy(language, "en", langsize);
	 break;

    case 2 :
        strlcpy(language, locale, langsize);
        break;

    case 5 :
        strlcpy(language, locale, langsize);

	if (language[2] == '-')
	{
	 /*
	  * Convert ll-cc to ll_CC...
	  */

	  language[2] = '_';
	  language[3] = (char)toupper(language[3] & 255);
	  language[4] = (char)toupper(language[4] & 255);
	}
	break;
  }

  for (i = 0;
       i < (int)(sizeof(apple_language_locale) /
		 sizeof(apple_language_locale[0]));
       i ++)
    if (!strcmp(locale, apple_language_locale[i].locale))
    {
      strlcpy(language, apple_language_locale[i].language, sizeof(language));
      break;
    }

 /*
  * Attempt to map the locale ID to a language ID...
  */

  if ((localeid = CFStringCreateWithCString(kCFAllocatorDefault, language,
                                            kCFStringEncodingASCII)) != NULL)
  {
    if ((langid = CFLocaleCreateCanonicalLanguageIdentifierFromString(
                      kCFAllocatorDefault, localeid)) != NULL)
    {
      CFStringGetCString(langid, language, (CFIndex)langsize, kCFStringEncodingASCII);
      CFRelease(langid);
    }

    CFRelease(localeid);
  }

 /*
  * Return what we got...
  */

  return (language);
}


/*
 * '_cupsAppleLocale()' - Get the locale associated with an Apple language ID.
 */

const char *					/* O - Locale */
_cupsAppleLocale(CFStringRef languageName,	/* I - Apple language ID */
                 char        *locale,		/* I - Buffer for locale */
		 size_t      localesize)	/* I - Size of buffer */
{
  int		i;			/* Looping var */
  CFStringRef	localeName;		/* Locale as a CF string */
#ifdef DEBUG
  char          temp[1024];             /* Temporary string */


  if (!CFStringGetCString(languageName, temp, (CFIndex)sizeof(temp), kCFStringEncodingASCII))
    temp[0] = '\0';

  DEBUG_printf(("_cupsAppleLocale(languageName=%p(%s), locale=%p, localsize=%d)", (void *)languageName, temp, (void *)locale, (int)localesize));
#endif /* DEBUG */

  localeName = CFLocaleCreateCanonicalLocaleIdentifierFromString(kCFAllocatorDefault, languageName);

  if (localeName)
  {
   /*
    * Copy the locale name and tweak as needed...
    */

    if (!CFStringGetCString(localeName, locale, (CFIndex)localesize, kCFStringEncodingASCII))
      *locale = '\0';

    DEBUG_printf(("_cupsAppleLocale: locale=\"%s\"", locale));

    CFRelease(localeName);

   /*
    * Map new language identifiers to locales...
    */

    for (i = 0;
	 i < (int)(sizeof(apple_language_locale) /
		   sizeof(apple_language_locale[0]));
	 i ++)
    {
      size_t len = strlen(apple_language_locale[i].language);

      if (!strcmp(locale, apple_language_locale[i].language) ||
          (!strncmp(locale, apple_language_locale[i].language, len) && (locale[len] == '_' || locale[len] == '-')))
      {
        DEBUG_printf(("_cupsAppleLocale: Updating locale to \"%s\".", apple_language_locale[i].locale));
	strlcpy(locale, apple_language_locale[i].locale, localesize);
	break;
      }
    }
  }
  else
  {
   /*
    * Just try the Apple language name...
    */

    if (!CFStringGetCString(languageName, locale, (CFIndex)localesize, kCFStringEncodingASCII))
      *locale = '\0';
  }

  if (!*locale)
  {
    DEBUG_puts("_cupsAppleLocale: Returning NULL.");
    return (NULL);
  }

 /*
  * Convert language subtag into region subtag...
  */

  if (locale[2] == '-')
    locale[2] = '_';
  else if (locale[3] == '-')
    locale[3] = '_';

  if (!strchr(locale, '.'))
    strlcat(locale, ".UTF-8", localesize);

  DEBUG_printf(("_cupsAppleLocale: Returning \"%s\".", locale));

  return (locale);
}
#endif /* __APPLE__ */


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
    DEBUG_printf(("1_cupsEncodingName(encoding=%d) = out of range (\"%s\")",
                  encoding, lang_encodings[0]));
    return (lang_encodings[0]);
  }
  else
  {
    DEBUG_printf(("1_cupsEncodingName(encoding=%d) = \"%s\"",
                  encoding, lang_encodings[encoding]));
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

  _cupsMutexLock(&lang_mutex);

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

  _cupsMutexUnlock(&lang_mutex);
}


/*
 * 'cupsLangFree()' - Free language data.
 *
 * This does not actually free anything; use @link cupsLangFlush@ for that.
 */

void
cupsLangFree(cups_lang_t *lang)		/* I - Language to free */
{
  _cupsMutexLock(&lang_mutex);

  if (lang != NULL && lang->used > 0)
    lang->used --;

  _cupsMutexUnlock(&lang_mutex);
}


/*
 * 'cupsLangGet()' - Get a language.
 */

cups_lang_t *				/* O - Language data */
cupsLangGet(const char *language)	/* I - Language or locale */
{
  int			i;		/* Looping var */
#ifndef __APPLE__
  char			locale[255];	/* Copy of locale name */
#endif /* !__APPLE__ */
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


  DEBUG_printf(("2cupsLangGet(language=\"%s\")", language));

#ifdef __APPLE__
 /*
  * Set the character set to UTF-8...
  */

  strlcpy(charset, "UTF8", sizeof(charset));

 /*
  * Apple's setlocale doesn't give us the user's localization
  * preference so we have to look it up this way...
  */

  if (!language)
  {
    if (!getenv("SOFTWARE") || (language = getenv("LANG")) == NULL)
      language = appleLangDefault();

    DEBUG_printf(("4cupsLangGet: language=\"%s\"", language));
  }

#else
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

#  ifdef LC_MESSAGES
    ptr = setlocale(LC_MESSAGES, NULL);
#  else
    ptr = setlocale(LC_ALL, NULL);
#  endif /* LC_MESSAGES */

    DEBUG_printf(("4cupsLangGet: current locale is \"%s\"", ptr));

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
      strlcpy(locale, ptr, sizeof(locale));
      language = locale;

     /*
      * CUPS STR #2575: Map "nb" to "no" for back-compatibility...
      */

      if (!strncmp(locale, "nb", 2))
        locale[1] = 'o';

      DEBUG_printf(("4cupsLangGet: new language value is \"%s\"", language));
    }
  }
#endif /* __APPLE__ */

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

    DEBUG_printf(("4cupsLangGet: charset set to \"%s\" via "
                  "nl_langinfo(CODESET)...", charset));
  }
#endif /* CODESET */

 /*
  * If we don't have a character set by now, default to UTF-8...
  */

  if (!charset[0])
    strlcpy(charset, "UTF8", sizeof(charset));

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
    strlcpy(langname, "C", sizeof(langname));
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
        strlcpy(country, "CN", sizeof(country));
      if (!strcmp(language, "zh") && !strcmp(country, "HANT"))
        strlcpy(country, "TW", sizeof(country));
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
      strlcpy(langname, "C", sizeof(langname));
      country[0] = '\0';
      charset[0] = '\0';
    }
  }

  DEBUG_printf(("4cupsLangGet: langname=\"%s\", country=\"%s\", charset=\"%s\"",
                langname, country, charset));

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

  DEBUG_printf(("4cupsLangGet: encoding=%d(%s)", encoding,
                encoding == CUPS_AUTO_ENCODING ? "auto" :
		    lang_encodings[encoding]));

 /*
  * See if we already have this language/country loaded...
  */

  if (country[0])
    snprintf(real, sizeof(real), "%s_%s", langname, country);
  else
    strlcpy(real, langname, sizeof(real));

  _cupsMutexLock(&lang_mutex);

  if ((lang = cups_cache_lookup(real, encoding)) != NULL)
  {
    _cupsMutexUnlock(&lang_mutex);

    DEBUG_printf(("3cupsLangGet: Using cached copy of \"%s\"...", real));

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
      _cupsMutexUnlock(&lang_mutex);

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
  strlcpy(lang->language, real, sizeof(lang->language));

  if (encoding != CUPS_AUTO_ENCODING)
    lang->encoding = encoding;
  else
    lang->encoding = CUPS_UTF8;

 /*
  * Return...
  */

  _cupsMutexUnlock(&lang_mutex);

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


  DEBUG_printf(("_cupsLangString(lang=%p, message=\"%s\")", (void *)lang, message));

 /*
  * Range check input...
  */

  if (!lang || !message || !*message)
    return (message);

  _cupsMutexLock(&lang_mutex);

 /*
  * Load the message catalog if needed...
  */

  if (!lang->strings)
    cups_message_load(lang);

  s = _cupsMessageLookup(lang->strings, message);

  _cupsMutexUnlock(&lang_mutex);

  return (s);
}


/*
 * '_cupsMessageFree()' - Free a messages array.
 */

void
_cupsMessageFree(cups_array_t *a)	/* I - Message array */
{
#if defined(__APPLE__) && defined(CUPS_BUNDLEDIR)
 /*
  * Release the cups.strings dictionary as needed...
  */

  if (cupsArrayUserData(a))
    CFRelease((CFDictionaryRef)cupsArrayUserData(a));
#endif /* __APPLE__ && CUPS_BUNDLEDIR */

 /*
  * Free the array...
  */

  cupsArrayDelete(a);
}


/*
 * '_cupsMessageLoad()' - Load a .po or .strings file into a messages array.
 */

cups_array_t *				/* O - New message array */
_cupsMessageLoad(const char *filename,	/* I - Message catalog to load */
                 int        flags)	/* I - Load flags */
{
  cups_file_t		*fp;		/* Message file */
  cups_array_t		*a;		/* Message array */
  _cups_message_t	*m;		/* Current message */
  char			s[4096],	/* String buffer */
			*ptr,		/* Pointer into buffer */
			*temp;		/* New string */
  size_t		length,		/* Length of combined strings */
			ptrlen;		/* Length of string */


  DEBUG_printf(("4_cupsMessageLoad(filename=\"%s\")", filename));

 /*
  * Create an array to hold the messages...
  */

  if ((a = _cupsMessageNew(NULL)) == NULL)
  {
    DEBUG_puts("5_cupsMessageLoad: Unable to allocate array!");
    return (NULL);
  }

 /*
  * Open the message catalog file...
  */

  if ((fp = cupsFileOpen(filename, "r")) == NULL)
  {
    DEBUG_printf(("5_cupsMessageLoad: Unable to open file: %s",
                  strerror(errno)));
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

    m = NULL;

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

	if (m)
	{
	  if (m->str && (m->str[0] || (flags & _CUPS_MESSAGE_EMPTY)))
	  {
	    cupsArrayAdd(a, m);
	  }
	  else
	  {
	   /*
	    * Translation is empty, don't add it... (STR #4033)
	    */

	    free(m->msg);
	    if (m->str)
	      free(m->str);
	    free(m);
	  }
	}

       /*
	* Create a new message with the given msgid string...
	*/

	if ((m = (_cups_message_t *)calloc(1, sizeof(_cups_message_t))) == NULL)
	  break;

	if ((m->msg = strdup(ptr)) == NULL)
	{
	  free(m);
	  m = NULL;
	  break;
	}
      }
      else if (s[0] == '\"' && m)
      {
       /*
	* Append to current string...
	*/

	length = strlen(m->str ? m->str : m->msg);
	ptrlen = strlen(ptr);

	if ((temp = realloc(m->str ? m->str : m->msg, length + ptrlen + 1)) == NULL)
	{
	  if (m->str)
	    free(m->str);
	  free(m->msg);
	  free(m);
	  m = NULL;
	  break;
	}

	if (m->str)
	{
	 /*
	  * Copy the new portion to the end of the msgstr string - safe
	  * to use memcpy because the buffer is allocated to the correct
	  * size...
	  */

	  m->str = temp;

	  memcpy(m->str + length, ptr, ptrlen + 1);
	}
	else
	{
	 /*
	  * Copy the new portion to the end of the msgid string - safe
	  * to use memcpy because the buffer is allocated to the correct
	  * size...
	  */

	  m->msg = temp;

	  memcpy(m->msg + length, ptr, ptrlen + 1);
	}
      }
      else if (!strncmp(s, "msgstr", 6) && m)
      {
       /*
	* Set the string...
	*/

	if ((m->str = strdup(ptr)) == NULL)
	{
	  free(m->msg);
	  free(m);
	  m = NULL;
          break;
	}
      }
    }

   /*
    * Add the last message string to the array as needed...
    */

    if (m)
    {
      if (m->str && (m->str[0] || (flags & _CUPS_MESSAGE_EMPTY)))
      {
	cupsArrayAdd(a, m);
      }
      else
      {
       /*
	* Translation is empty, don't add it... (STR #4033)
	*/

	free(m->msg);
	if (m->str)
	  free(m->str);
	free(m);
      }
    }
  }

 /*
  * Close the message catalog file and return the new array...
  */

  cupsFileClose(fp);

  DEBUG_printf(("5_cupsMessageLoad: Returning %d messages...", cupsArrayCount(a)));

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


  DEBUG_printf(("_cupsMessageLookup(a=%p, m=\"%s\")", (void *)a, m));

 /*
  * Lookup the message string; if it doesn't exist in the catalog,
  * then return the message that was passed to us...
  */

  key.msg = (char *)m;
  match   = (_cups_message_t *)cupsArrayFind(a, &key);

#if defined(__APPLE__) && defined(CUPS_BUNDLEDIR)
  if (!match && cupsArrayUserData(a))
  {
   /*
    * Try looking the string up in the cups.strings dictionary...
    */

    CFDictionaryRef	dict;		/* cups.strings dictionary */
    CFStringRef		cfm,		/* Message as a CF string */
			cfstr;		/* Localized text as a CF string */

    dict       = (CFDictionaryRef)cupsArrayUserData(a);
    cfm        = CFStringCreateWithCString(kCFAllocatorDefault, m, kCFStringEncodingUTF8);
    match      = calloc(1, sizeof(_cups_message_t));
    match->msg = strdup(m);
    cfstr      = cfm ? CFDictionaryGetValue(dict, cfm) : NULL;

    if (cfstr)
    {
      char	buffer[1024];		/* Message buffer */

      CFStringGetCString(cfstr, buffer, sizeof(buffer), kCFStringEncodingUTF8);
      match->str = strdup(buffer);

      DEBUG_printf(("1_cupsMessageLookup: Found \"%s\" as \"%s\"...", m, buffer));
    }
    else
    {
      match->str = strdup(m);

      DEBUG_printf(("1_cupsMessageLookup: Did not find \"%s\"...", m));
    }

    cupsArrayAdd(a, match);

    if (cfm)
      CFRelease(cfm);
  }
#endif /* __APPLE__ && CUPS_BUNDLEDIR */

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
  return (cupsArrayNew3((cups_array_func_t)cups_message_compare, context,
                        (cups_ahash_func_t)NULL, 0,
			(cups_acopy_func_t)NULL,
			(cups_afree_func_t)cups_message_free));
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


#ifdef __APPLE__
/*
 * 'appleLangDefault()' - Get the default locale string.
 */

static const char *			/* O - Locale string */
appleLangDefault(void)
{
  CFBundleRef		bundle;		/* Main bundle (if any) */
  CFArrayRef		bundleList;	/* List of localizations in bundle */
  CFPropertyListRef 	localizationList = NULL;
					/* List of localization data */
  CFStringRef		languageName;	/* Current name */
  char			*lang;		/* LANG environment variable */
  _cups_globals_t	*cg = _cupsGlobals();
  					/* Pointer to library globals */


  DEBUG_puts("2appleLangDefault()");

 /*
  * Only do the lookup and translation the first time.
  */

  if (!cg->language[0])
  {
    if (getenv("SOFTWARE") != NULL && (lang = getenv("LANG")) != NULL)
    {
      DEBUG_printf(("3appleLangDefault: Using LANG=%s", lang));
      strlcpy(cg->language, lang, sizeof(cg->language));
      return (cg->language);
    }
    else if ((bundle = CFBundleGetMainBundle()) != NULL &&
             (bundleList = CFBundleCopyBundleLocalizations(bundle)) != NULL)
    {
      CFURLRef resources = CFBundleCopyResourcesDirectoryURL(bundle);

      DEBUG_puts("3appleLangDefault: Getting localizationList from bundle.");

      if (resources)
      {
        CFStringRef	cfpath = CFURLCopyPath(resources);
	char		path[1024];

        if (cfpath)
	{
	 /*
	  * See if we have an Info.plist file in the bundle...
	  */

	  CFStringGetCString(cfpath, path, sizeof(path), kCFStringEncodingUTF8);
	  DEBUG_printf(("3appleLangDefault: Got a resource URL (\"%s\")", path));
	  strlcat(path, "Contents/Info.plist", sizeof(path));

          if (!access(path, R_OK))
	    localizationList = CFBundleCopyPreferredLocalizationsFromArray(bundleList);
	  else
	    DEBUG_puts("3appleLangDefault: No Info.plist, ignoring resource URL...");

	  CFRelease(cfpath);
	}

	CFRelease(resources);
      }
      else
        DEBUG_puts("3appleLangDefault: No resource URL.");

      CFRelease(bundleList);
    }

    if (!localizationList)
    {
      DEBUG_puts("3appleLangDefault: Getting localizationList from preferences.");

      localizationList =
	  CFPreferencesCopyAppValue(CFSTR("AppleLanguages"),
				    kCFPreferencesCurrentApplication);
    }

    if (localizationList)
    {
#ifdef DEBUG
      if (CFGetTypeID(localizationList) == CFArrayGetTypeID())
        DEBUG_printf(("3appleLangDefault: Got localizationList, %d entries.",
                      (int)CFArrayGetCount(localizationList)));
      else
        DEBUG_puts("3appleLangDefault: Got localizationList but not an array.");
#endif /* DEBUG */

      if (CFGetTypeID(localizationList) == CFArrayGetTypeID() &&
	  CFArrayGetCount(localizationList) > 0)
      {
	languageName = CFArrayGetValueAtIndex(localizationList, 0);

	if (languageName &&
	    CFGetTypeID(languageName) == CFStringGetTypeID())
	{
	  if (_cupsAppleLocale(languageName, cg->language, sizeof(cg->language)))
	    DEBUG_printf(("3appleLangDefault: cg->language=\"%s\"",
			  cg->language));
	  else
	    DEBUG_puts("3appleLangDefault: Unable to get locale.");
	}
      }

      CFRelease(localizationList);
    }

   /*
    * If we didn't find the language, default to en_US...
    */

    if (!cg->language[0])
    {
      DEBUG_puts("3appleLangDefault: Defaulting to en_US.");
      strlcpy(cg->language, "en_US.UTF-8", sizeof(cg->language));
    }
  }
  else
    DEBUG_printf(("3appleLangDefault: Using previous locale \"%s\".", cg->language));

 /*
  * Return the cached locale...
  */

  return (cg->language);
}


#  ifdef CUPS_BUNDLEDIR
/*
 * 'appleMessageLoad()' - Load a message catalog from a localizable bundle.
 */

static cups_array_t *			/* O - Message catalog */
appleMessageLoad(const char *locale)	/* I - Locale ID */
{
  char			filename[1024],	/* Path to cups.strings file */
			applelang[256],	/* Apple language ID */
			baselang[4];	/* Base language */
  CFURLRef		url;		/* URL to cups.strings file */
  CFReadStreamRef	stream = NULL;	/* File stream */
  CFPropertyListRef	plist = NULL;	/* Localization file */
#ifdef DEBUG
  const char            *cups_strings = getenv("CUPS_STRINGS");
                                        /* Test strings file */
  CFErrorRef		error = NULL;	/* Error when opening file */
#endif /* DEBUG */


  DEBUG_printf(("appleMessageLoad(locale=\"%s\")", locale));

 /*
  * Load the cups.strings file...
  */

#ifdef DEBUG
  if (cups_strings)
  {
    DEBUG_puts("1appleMessageLoad: Using debug CUPS_STRINGS file.");
    strlcpy(filename, cups_strings, sizeof(filename));
  }
  else
#endif /* DEBUG */

  snprintf(filename, sizeof(filename),
           CUPS_BUNDLEDIR "/Resources/%s.lproj/cups.strings",
	   _cupsAppleLanguage(locale, applelang, sizeof(applelang)));

  if (access(filename, 0))
  {
   /*
    * <rdar://problem/22086642>
    *
    * Try with original locale string...
    */

    DEBUG_printf(("1appleMessageLoad: \"%s\": %s", filename, strerror(errno)));
    snprintf(filename, sizeof(filename), CUPS_BUNDLEDIR "/Resources/%s.lproj/cups.strings", locale);
  }

  if (access(filename, 0))
  {
   /*
    * <rdar://problem/25292403>
    *
    * Try with just the language code...
    */

    DEBUG_printf(("1appleMessageLoad: \"%s\": %s", filename, strerror(errno)));

    strlcpy(baselang, locale, sizeof(baselang));
    if (baselang[3] == '-' || baselang[3] == '_')
      baselang[3] = '\0';

    snprintf(filename, sizeof(filename), CUPS_BUNDLEDIR "/Resources/%s.lproj/cups.strings", baselang);
  }

  if (access(filename, 0))
  {
   /*
    * Try alternate lproj directory names...
    */

    DEBUG_printf(("1appleMessageLoad: \"%s\": %s", filename, strerror(errno)));

    if (!strncmp(locale, "en", 2))
      locale = "English";
    else if (!strncmp(locale, "nb", 2))
      locale = "no";
    else if (!strncmp(locale, "nl", 2))
      locale = "Dutch";
    else if (!strncmp(locale, "fr", 2))
      locale = "French";
    else if (!strncmp(locale, "de", 2))
      locale = "German";
    else if (!strncmp(locale, "it", 2))
      locale = "Italian";
    else if (!strncmp(locale, "ja", 2))
      locale = "Japanese";
    else if (!strncmp(locale, "es", 2))
      locale = "Spanish";
    else if (!strcmp(locale, "zh_HK") || !strncasecmp(locale, "zh-Hant", 7) || !strncasecmp(locale, "zh_Hant", 7))
    {
     /*
      * <rdar://problem/22130168>
      * <rdar://problem/27245567>
      *
      * Try zh_TW first, then zh...  Sigh...
      */

      if (!access(CUPS_BUNDLEDIR "/Resources/zh_TW.lproj/cups.strings", 0))
        locale = "zh_TW";
      else
        locale = "zh";
    }
    else if (strstr(locale, "_") != NULL || strstr(locale, "-") != NULL)
    {
     /*
      * Drop country code, just try language...
      */

      strlcpy(baselang, locale, sizeof(baselang));
      if (baselang[2] == '-' || baselang[2] == '_')
        baselang[2] = '\0';

      locale = baselang;
    }

    snprintf(filename, sizeof(filename),
	     CUPS_BUNDLEDIR "/Resources/%s.lproj/cups.strings", locale);
  }

  DEBUG_printf(("1appleMessageLoad: filename=\"%s\"", filename));

  url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
                                                (UInt8 *)filename,
						(CFIndex)strlen(filename), false);
  if (url)
  {
    stream = CFReadStreamCreateWithFile(kCFAllocatorDefault, url);
    if (stream)
    {
     /*
      * Read the property list containing the localization data.
      *
      * NOTE: This code currently generates a clang "potential leak"
      * warning, but the object is released in _cupsMessageFree().
      */

      CFReadStreamOpen(stream);

#ifdef DEBUG
      plist = CFPropertyListCreateWithStream(kCFAllocatorDefault, stream, 0,
                                             kCFPropertyListImmutable, NULL,
                                             &error);
      if (error)
      {
        CFStringRef	msg = CFErrorCopyDescription(error);
    					/* Error message */

        CFStringGetCString(msg, filename, sizeof(filename),
                           kCFStringEncodingUTF8);
        DEBUG_printf(("1appleMessageLoad: %s", filename));

	CFRelease(msg);
        CFRelease(error);
      }

#else
      plist = CFPropertyListCreateWithStream(kCFAllocatorDefault, stream, 0,
                                             kCFPropertyListImmutable, NULL,
                                             NULL);
#endif /* DEBUG */

      if (plist && CFGetTypeID(plist) != CFDictionaryGetTypeID())
      {
         CFRelease(plist);
         plist = NULL;
      }

      CFRelease(stream);
    }

    CFRelease(url);
  }

  DEBUG_printf(("1appleMessageLoad: url=%p, stream=%p, plist=%p", url, stream,
                plist));

 /*
  * Create and return an empty array to act as a cache for messages, passing the
  * plist as the user data.
  */

  return (_cupsMessageNew((void *)plist));
}
#  endif /* CUPS_BUNDLEDIR */
#endif /* __APPLE__ */


/*
 * 'cups_cache_lookup()' - Lookup a language in the cache...
 */

static cups_lang_t *			/* O - Language data or NULL */
cups_cache_lookup(
    const char      *name,		/* I - Name of locale */
    cups_encoding_t encoding)		/* I - Encoding of locale */
{
  cups_lang_t	*lang;			/* Current language */


  DEBUG_printf(("7cups_cache_lookup(name=\"%s\", encoding=%d(%s))", name,
                encoding, encoding == CUPS_AUTO_ENCODING ? "auto" :
		              lang_encodings[encoding]));

 /*
  * Loop through the cache and return a match if found...
  */

  for (lang = lang_cache; lang != NULL; lang = lang->next)
  {
    DEBUG_printf(("9cups_cache_lookup: lang=%p, language=\"%s\", "
		  "encoding=%d(%s)", (void *)lang, lang->language, lang->encoding,
		  lang_encodings[lang->encoding]));

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

static int				/* O - Result of comparison */
cups_message_compare(
    _cups_message_t *m1,		/* I - First message */
    _cups_message_t *m2)		/* I - Second message */
{
  return (strcmp(m1->msg, m2->msg));
}


/*
 * 'cups_message_free()' - Free a message.
 */

static void
cups_message_free(_cups_message_t *m)	/* I - Message */
{
  if (m->msg)
    free(m->msg);

  if (m->str)
    free(m->str);

  free(m);
}


/*
 * 'cups_message_load()' - Load the message catalog for a language.
 */

static void
cups_message_load(cups_lang_t *lang)	/* I - Language */
{
#if defined(__APPLE__) && defined(CUPS_BUNDLEDIR)
  lang->strings = appleMessageLoad(lang->language);

#else
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

      DEBUG_printf(("4cups_message_load: access(\"%s\", 0): %s", filename,
                    strerror(errno)));

      snprintf(filename, sizeof(filename), "%s/C/cups_C.po", cg->localedir);
    }
  }

 /*
  * Read the strings from the file...
  */

  lang->strings = _cupsMessageLoad(filename, _CUPS_MESSAGE_UNQUOTE);
#endif /* __APPLE__ && CUPS_BUNDLEDIR */
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
  _cups_message_t	*m;		/* New message */


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

    if ((m = malloc(sizeof(_cups_message_t))) == NULL)
      break;

    m->msg = strdup(msg);
    m->str = strdup(str);

    if (m->msg && m->str)
    {
      cupsArrayAdd(a, m);
    }
    else
    {
      if (m->msg)
	free(m->msg);

      if (m->str)
	free(m->str);

      free(m);
      break;
    }

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
