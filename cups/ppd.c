/*
 * PPD file routines for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2007-2019 by Apple Inc.
 * Copyright © 1997-2007 by Easy Software Products, all rights reserved.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 *
 * PostScript is a trademark of Adobe Systems, Inc.
 */

/*
 * Include necessary headers.
 */

#include "cups-private.h"
#include "ppd-private.h"
#include "debug-internal.h"


/*
 * Definitions...
 */

#define PPD_KEYWORD	1		/* Line contained a keyword */
#define PPD_OPTION	2		/* Line contained an option name */
#define PPD_TEXT	4		/* Line contained human-readable text */
#define PPD_STRING	8		/* Line contained a string or code */

#define PPD_HASHSIZE	512		/* Size of hash */


/*
 * Line buffer structure...
 */

typedef struct _ppd_line_s
{
  char		*buffer;		/* Pointer to buffer */
  size_t	bufsize;		/* Size of the buffer */
} _ppd_line_t;


/*
 * Local globals...
 */

static _cups_threadkey_t ppd_globals_key = _CUPS_THREADKEY_INITIALIZER;
					/* Thread local storage key */
#ifdef HAVE_PTHREAD_H
static pthread_once_t	ppd_globals_key_once = PTHREAD_ONCE_INIT;
					/* One-time initialization object */
#endif /* HAVE_PTHREAD_H */


/*
 * Local functions...
 */

static ppd_attr_t	*ppd_add_attr(ppd_file_t *ppd, const char *name,
			              const char *spec, const char *text,
				      const char *value);
static ppd_choice_t	*ppd_add_choice(ppd_option_t *option, const char *name);
static ppd_size_t	*ppd_add_size(ppd_file_t *ppd, const char *name);
static int		ppd_compare_attrs(ppd_attr_t *a, ppd_attr_t *b);
static int		ppd_compare_choices(ppd_choice_t *a, ppd_choice_t *b);
static int		ppd_compare_coptions(ppd_coption_t *a,
			                     ppd_coption_t *b);
static int		ppd_compare_options(ppd_option_t *a, ppd_option_t *b);
static int		ppd_decode(char *string);
static void		ppd_free_filters(ppd_file_t *ppd);
static void		ppd_free_group(ppd_group_t *group);
static void		ppd_free_option(ppd_option_t *option);
static ppd_coption_t	*ppd_get_coption(ppd_file_t *ppd, const char *name);
static ppd_cparam_t	*ppd_get_cparam(ppd_coption_t *opt,
			                const char *param,
					const char *text);
static ppd_group_t	*ppd_get_group(ppd_file_t *ppd, const char *name,
			               const char *text, _ppd_globals_t *pg,
				       cups_encoding_t encoding);
static ppd_option_t	*ppd_get_option(ppd_group_t *group, const char *name);
static _ppd_globals_t	*ppd_globals_alloc(void);
#if defined(HAVE_PTHREAD_H) || defined(_WIN32)
static void		ppd_globals_free(_ppd_globals_t *g);
#endif /* HAVE_PTHREAD_H || _WIN32 */
#ifdef HAVE_PTHREAD_H
static void		ppd_globals_init(void);
#endif /* HAVE_PTHREAD_H */
static int		ppd_hash_option(ppd_option_t *option);
static int		ppd_read(cups_file_t *fp, _ppd_line_t *line,
			         char *keyword, char *option, char *text,
				 char **string, int ignoreblank,
				 _ppd_globals_t *pg);
static int		ppd_update_filters(ppd_file_t *ppd,
			                   _ppd_globals_t *pg);


/*
 * 'ppdClose()' - Free all memory used by the PPD file.
 */

void
ppdClose(ppd_file_t *ppd)		/* I - PPD file record */
{
  int			i;		/* Looping var */
  ppd_group_t		*group;		/* Current group */
  char			**font;		/* Current font */
  ppd_attr_t		**attr;		/* Current attribute */
  ppd_coption_t		*coption;	/* Current custom option */
  ppd_cparam_t		*cparam;	/* Current custom parameter */


 /*
  * Range check arguments...
  */

  if (!ppd)
    return;

 /*
  * Free all strings at the top level...
  */

  free(ppd->lang_encoding);
  free(ppd->nickname);
  free(ppd->patches);
  free(ppd->emulations);
  free(ppd->jcl_begin);
  free(ppd->jcl_end);
  free(ppd->jcl_ps);

 /*
  * Free any UI groups, subgroups, and options...
  */

  if (ppd->num_groups > 0)
  {
    for (i = ppd->num_groups, group = ppd->groups; i > 0; i --, group ++)
      ppd_free_group(group);

    free(ppd->groups);
  }

  cupsArrayDelete(ppd->options);
  cupsArrayDelete(ppd->marked);

 /*
  * Free any page sizes...
  */

  if (ppd->num_sizes > 0)
    free(ppd->sizes);

 /*
  * Free any constraints...
  */

  if (ppd->num_consts > 0)
    free(ppd->consts);

 /*
  * Free any filters...
  */

  ppd_free_filters(ppd);

 /*
  * Free any fonts...
  */

  if (ppd->num_fonts > 0)
  {
    for (i = ppd->num_fonts, font = ppd->fonts; i > 0; i --, font ++)
      free(*font);

    free(ppd->fonts);
  }

 /*
  * Free any profiles...
  */

  if (ppd->num_profiles > 0)
    free(ppd->profiles);

 /*
  * Free any attributes...
  */

  if (ppd->num_attrs > 0)
  {
    for (i = ppd->num_attrs, attr = ppd->attrs; i > 0; i --, attr ++)
    {
      free((*attr)->value);
      free(*attr);
    }

    free(ppd->attrs);
  }

  cupsArrayDelete(ppd->sorted_attrs);

 /*
  * Free custom options...
  */

  for (coption = (ppd_coption_t *)cupsArrayFirst(ppd->coptions);
       coption;
       coption = (ppd_coption_t *)cupsArrayNext(ppd->coptions))
  {
    for (cparam = (ppd_cparam_t *)cupsArrayFirst(coption->params);
         cparam;
	 cparam = (ppd_cparam_t *)cupsArrayNext(coption->params))
    {
      switch (cparam->type)
      {
        case PPD_CUSTOM_PASSCODE :
        case PPD_CUSTOM_PASSWORD :
        case PPD_CUSTOM_STRING :
            free(cparam->current.custom_string);
	    break;

	default :
	    break;
      }

      free(cparam);
    }

    cupsArrayDelete(coption->params);

    free(coption);
  }

  cupsArrayDelete(ppd->coptions);

 /*
  * Free constraints...
  */

  if (ppd->cups_uiconstraints)
  {
    _ppd_cups_uiconsts_t *consts;	/* Current constraints */


    for (consts = (_ppd_cups_uiconsts_t *)cupsArrayFirst(ppd->cups_uiconstraints);
         consts;
	 consts = (_ppd_cups_uiconsts_t *)cupsArrayNext(ppd->cups_uiconstraints))
    {
      free(consts->constraints);
      free(consts);
    }

    cupsArrayDelete(ppd->cups_uiconstraints);
  }

 /*
  * Free any PPD cache/mapping data...
  */

  if (ppd->cache)
    _ppdCacheDestroy(ppd->cache);

 /*
  * Free the whole record...
  */

  free(ppd);
}


/*
 * 'ppdErrorString()' - Returns the text associated with a status.
 *
 * @since CUPS 1.1.19/macOS 10.3@
 */

const char *				/* O - Status string */
ppdErrorString(ppd_status_t status)	/* I - PPD status */
{
  static const char * const messages[] =/* Status messages */
		{
		  _("OK"),
		  _("Unable to open PPD file"),
		  _("NULL PPD file pointer"),
		  _("Memory allocation error"),
		  _("Missing PPD-Adobe-4.x header"),
		  _("Missing value string"),
		  _("Internal error"),
		  _("Bad OpenGroup"),
		  _("OpenGroup without a CloseGroup first"),
		  _("Bad OpenUI/JCLOpenUI"),
		  _("OpenUI/JCLOpenUI without a CloseUI/JCLCloseUI first"),
		  _("Bad OrderDependency"),
		  _("Bad UIConstraints"),
		  _("Missing asterisk in column 1"),
		  _("Line longer than the maximum allowed (255 characters)"),
		  _("Illegal control character"),
		  _("Illegal main keyword string"),
		  _("Illegal option keyword string"),
		  _("Illegal translation string"),
		  _("Illegal whitespace character"),
		  _("Bad custom parameter"),
		  _("Missing option keyword"),
		  _("Bad value string"),
		  _("Missing CloseGroup"),
		  _("Bad CloseUI/JCLCloseUI"),
		  _("Missing CloseUI/JCLCloseUI")
		};


  if (status < PPD_OK || status >= PPD_MAX_STATUS)
    return (_cupsLangString(cupsLangDefault(), _("Unknown")));
  else
    return (_cupsLangString(cupsLangDefault(), messages[status]));
}


/*
 * '_ppdGetEncoding()' - Get the CUPS encoding value for the given
 *                       LanguageEncoding.
 */

cups_encoding_t				/* O - CUPS encoding value */
_ppdGetEncoding(const char *name)	/* I - LanguageEncoding string */
{
  if (!_cups_strcasecmp(name, "ISOLatin1"))
    return (CUPS_ISO8859_1);
  else if (!_cups_strcasecmp(name, "ISOLatin2"))
    return (CUPS_ISO8859_2);
  else if (!_cups_strcasecmp(name, "ISOLatin5"))
    return (CUPS_ISO8859_5);
  else if (!_cups_strcasecmp(name, "JIS83-RKSJ"))
    return (CUPS_JIS_X0213);
  else if (!_cups_strcasecmp(name, "MacStandard"))
    return (CUPS_MAC_ROMAN);
  else if (!_cups_strcasecmp(name, "WindowsANSI"))
    return (CUPS_WINDOWS_1252);
  else
    return (CUPS_UTF8);
}


/*
 * '_ppdGlobals()' - Return a pointer to thread local storage
 */

_ppd_globals_t *			/* O - Pointer to global data */
_ppdGlobals(void)
{
  _ppd_globals_t *pg;			/* Pointer to global data */


#ifdef HAVE_PTHREAD_H
 /*
  * Initialize the global data exactly once...
  */

  pthread_once(&ppd_globals_key_once, ppd_globals_init);
#endif /* HAVE_PTHREAD_H */

 /*
  * See if we have allocated the data yet...
  */

  if ((pg = (_ppd_globals_t *)_cupsThreadGetData(ppd_globals_key)) == NULL)
  {
   /*
    * No, allocate memory as set the pointer for the key...
    */

    if ((pg = ppd_globals_alloc()) != NULL)
      _cupsThreadSetData(ppd_globals_key, pg);
  }

 /*
  * Return the pointer to the data...
  */

  return (pg);
}


/*
 * 'ppdLastError()' - Return the status from the last ppdOpen*().
 *
 * @since CUPS 1.1.19/macOS 10.3@
 */

ppd_status_t				/* O - Status code */
ppdLastError(int *line)			/* O - Line number */
{
  _ppd_globals_t	*pg = _ppdGlobals();
					/* Global data */


  if (line)
    *line = pg->ppd_line;

  return (pg->ppd_status);
}


/*
 * '_ppdOpen()' - Read a PPD file into memory.
 *
 * @since CUPS 1.2/macOS 10.5@
 */

ppd_file_t *				/* O - PPD file record or @code NULL@ if the PPD file could not be opened. */
_ppdOpen(
    cups_file_t		*fp,		/* I - File to read from */
    _ppd_localization_t	localization)	/* I - Localization to load */
{
  int			i, j, k;	/* Looping vars */
  _ppd_line_t		line;		/* Line buffer */
  ppd_file_t		*ppd;		/* PPD file record */
  ppd_group_t		*group,		/* Current group */
			*subgroup;	/* Current sub-group */
  ppd_option_t		*option;	/* Current option */
  ppd_choice_t		*choice;	/* Current choice */
  ppd_const_t		*constraint;	/* Current constraint */
  ppd_size_t		*size;		/* Current page size */
  int			mask;		/* Line data mask */
  char			keyword[PPD_MAX_NAME],
  					/* Keyword from file */
			name[PPD_MAX_NAME],
					/* Option from file */
			text[PPD_MAX_LINE],
					/* Human-readable text from file */
			*string,	/* Code/text from file */
			*sptr,		/* Pointer into string */
			*temp,		/* Temporary string pointer */
			**tempfonts;	/* Temporary fonts pointer */
  float			order;		/* Order dependency number */
  ppd_section_t		section;	/* Order dependency section */
  ppd_profile_t		*profile;	/* Pointer to color profile */
  char			**filter;	/* Pointer to filter */
  struct lconv		*loc;		/* Locale data */
  int			ui_keyword;	/* Is this line a UI keyword? */
  cups_lang_t		*lang;		/* Language data */
  cups_encoding_t	encoding;	/* Encoding of PPD file */
  _ppd_globals_t	*pg = _ppdGlobals();
					/* Global data */
  char			custom_name[PPD_MAX_NAME];
					/* CustomFoo attribute name */
  ppd_attr_t		*custom_attr;	/* CustomFoo attribute */
  char			ll[7],		/* Base language + '.' */
			ll_CC[7];	/* Language w/country + '.' */
  size_t		ll_len = 0,	/* Base language length */
			ll_CC_len = 0;	/* Language w/country length */
  static const char * const ui_keywords[] =
			{
#ifdef CUPS_USE_FULL_UI_KEYWORDS_LIST
 /*
  * Adobe defines some 41 keywords as "UI", meaning that they are
  * user interface elements and that they should be treated as such
  * even if the PPD creator doesn't use Open/CloseUI around them.
  *
  * Since this can cause previously invisible options to appear and
  * confuse users, the default is to only treat the PageSize and
  * PageRegion keywords this way.
  */
			  /* Boolean keywords */
			  "BlackSubstitution",
			  "Booklet",
			  "Collate",
			  "ManualFeed",
			  "MirrorPrint",
			  "NegativePrint",
			  "Sorter",
			  "TraySwitch",

			  /* PickOne keywords */
			  "AdvanceMedia",
			  "BindColor",
			  "BindEdge",
			  "BindType",
			  "BindWhen",
			  "BitsPerPixel",
			  "ColorModel",
			  "CutMedia",
			  "Duplex",
			  "FoldType",
			  "FoldWhen",
			  "InputSlot",
			  "JCLFrameBufferSize",
			  "JCLResolution",
			  "Jog",
			  "MediaColor",
			  "MediaType",
			  "MediaWeight",
			  "OutputBin",
			  "OutputMode",
			  "OutputOrder",
			  "PageRegion",
			  "PageSize",
			  "Resolution",
			  "Separations",
			  "Signature",
			  "Slipsheet",
			  "Smoothing",
			  "StapleLocation",
			  "StapleOrientation",
			  "StapleWhen",
			  "StapleX",
			  "StapleY"
#else /* !CUPS_USE_FULL_UI_KEYWORDS_LIST */
			  "PageRegion",
			  "PageSize"
#endif /* CUPS_USE_FULL_UI_KEYWORDS_LIST */
			};
  static const char * const color_keywords[] =	/* Keywords associated with color profiles */
			{
			  ".cupsICCProfile",
			  ".ColorModel",
			};


  DEBUG_printf(("_ppdOpen(fp=%p)", fp));

 /*
  * Default to "OK" status...
  */

  pg->ppd_status = PPD_OK;
  pg->ppd_line   = 0;

 /*
  * Range check input...
  */

  if (fp == NULL)
  {
    pg->ppd_status = PPD_NULL_FILE;
    return (NULL);
  }

 /*
  * If only loading a single localization set up the strings to match...
  */

  if (localization == _PPD_LOCALIZATION_DEFAULT)
  {
    if ((lang = cupsLangDefault()) == NULL)
      return (NULL);

    snprintf(ll_CC, sizeof(ll_CC), "%s.", lang->language);

   /*
    * <rdar://problem/22130168>
    * <rdar://problem/27245567>
    *
    * Need to use a different base language for some locales...
    */

    if (!strcmp(lang->language, "zh_HK"))
    {					/* Traditional Chinese + variants */
      strlcpy(ll_CC, "zh_TW.", sizeof(ll_CC));
      strlcpy(ll, "zh_", sizeof(ll));
    }
    else if (!strncmp(lang->language, "zh", 2))
      strlcpy(ll, "zh_", sizeof(ll));	/* Any Chinese variant */
    else if (!strncmp(lang->language, "jp", 2))
    {					/* Any Japanese variant */
      strlcpy(ll_CC, "ja", sizeof(ll_CC));
      strlcpy(ll, "jp", sizeof(ll));
    }
    else if (!strncmp(lang->language, "nb", 2) || !strncmp(lang->language, "no", 2))
    {					/* Any Norwegian variant */
      strlcpy(ll_CC, "nb", sizeof(ll_CC));
      strlcpy(ll, "no", sizeof(ll));
    }
    else
      snprintf(ll, sizeof(ll), "%2.2s.", lang->language);

    ll_CC_len = strlen(ll_CC);
    ll_len    = strlen(ll);

    DEBUG_printf(("2_ppdOpen: Loading localizations matching \"%s\" and \"%s\"",
                  ll_CC, ll));
  }

 /*
  * Grab the first line and make sure it reads '*PPD-Adobe: "major.minor"'...
  */

  line.buffer  = NULL;
  line.bufsize = 0;

  mask = ppd_read(fp, &line, keyword, name, text, &string, 0, pg);

  DEBUG_printf(("2_ppdOpen: mask=%x, keyword=\"%s\"...", mask, keyword));

  if (mask == 0 ||
      strcmp(keyword, "PPD-Adobe") ||
      string == NULL || string[0] != '4')
  {
   /*
    * Either this is not a PPD file, or it is not a 4.x PPD file.
    */

    if (pg->ppd_status == PPD_OK)
      pg->ppd_status = PPD_MISSING_PPDADOBE4;

    free(string);
    free(line.buffer);

    return (NULL);
  }

  DEBUG_printf(("2_ppdOpen: keyword=%s, string=%p", keyword, string));

 /*
  * Allocate memory for the PPD file record...
  */

  if ((ppd = calloc(1, sizeof(ppd_file_t))) == NULL)
  {
    pg->ppd_status = PPD_ALLOC_ERROR;

    free(string);
    free(line.buffer);

    return (NULL);
  }

  free(string);
  string = NULL;

  ppd->language_level = 2;
  ppd->color_device   = 0;
  ppd->colorspace     = PPD_CS_N;
  ppd->landscape      = -90;
  ppd->coptions       = cupsArrayNew((cups_array_func_t)ppd_compare_coptions, NULL);

 /*
  * Read lines from the PPD file and add them to the file record...
  */

  group      = NULL;
  subgroup   = NULL;
  option     = NULL;
  choice     = NULL;
  ui_keyword = 0;
  encoding   = CUPS_ISO8859_1;
  loc        = localeconv();

  while ((mask = ppd_read(fp, &line, keyword, name, text, &string, 1, pg)) != 0)
  {
    DEBUG_printf(("2_ppdOpen: mask=%x, keyword=\"%s\", name=\"%s\", "
                  "text=\"%s\", string=%d chars...", mask, keyword, name, text,
		  string ? (int)strlen(string) : 0));

    if (strncmp(keyword, "Default", 7) && !string &&
        pg->ppd_conform != PPD_CONFORM_RELAXED)
    {
     /*
      * Need a string value!
      */

      pg->ppd_status = PPD_MISSING_VALUE;

      goto error;
    }
    else if (!string)
      continue;

   /*
    * Certain main keywords (as defined by the PPD spec) may be used
    * without the usual OpenUI/CloseUI stuff.  Presumably this is just
    * so that Adobe wouldn't completely break compatibility with PPD
    * files prior to v4.0 of the spec, but it is hopelessly
    * inconsistent...  Catch these main keywords and automatically
    * create the corresponding option, as needed...
    */

    if (ui_keyword)
    {
     /*
      * Previous line was a UI keyword...
      */

      option     = NULL;
      ui_keyword = 0;
    }

   /*
    * If we are filtering out keyword localizations, see if this line needs to
    * be used...
    */

    if (localization != _PPD_LOCALIZATION_ALL &&
        (temp = strchr(keyword, '.')) != NULL &&
        ((temp - keyword) == 2 || (temp - keyword) == 5) &&
        _cups_isalpha(keyword[0]) &&
        _cups_isalpha(keyword[1]) &&
        (keyword[2] == '.' ||
         (keyword[2] == '_' && _cups_isalpha(keyword[3]) &&
          _cups_isalpha(keyword[4]) && keyword[5] == '.')))
    {
      if (localization == _PPD_LOCALIZATION_NONE ||
	  (localization == _PPD_LOCALIZATION_DEFAULT &&
	   strncmp(ll_CC, keyword, ll_CC_len) &&
	   strncmp(ll, keyword, ll_len)))
      {
	DEBUG_printf(("2_ppdOpen: Ignoring localization: \"%s\"\n", keyword));
	free(string);
	string = NULL;
	continue;
      }
      else if (localization == _PPD_LOCALIZATION_ICC_PROFILES)
      {
       /*
        * Only load localizations for the color profile related keywords...
        */

	for (i = 0;
	     i < (int)(sizeof(color_keywords) / sizeof(color_keywords[0]));
	     i ++)
	{
	  if (!_cups_strcasecmp(temp, color_keywords[i]))
	    break;
	}

	if (i >= (int)(sizeof(color_keywords) / sizeof(color_keywords[0])))
	{
	  DEBUG_printf(("2_ppdOpen: Ignoring localization: \"%s\"\n", keyword));
	  free(string);
	  string = NULL;
	  continue;
	}
      }
    }

    if (option == NULL &&
        (mask & (PPD_KEYWORD | PPD_OPTION | PPD_STRING)) ==
	    (PPD_KEYWORD | PPD_OPTION | PPD_STRING))
    {
      for (i = 0; i < (int)(sizeof(ui_keywords) / sizeof(ui_keywords[0])); i ++)
        if (!strcmp(keyword, ui_keywords[i]))
	  break;

      if (i < (int)(sizeof(ui_keywords) / sizeof(ui_keywords[0])))
      {
       /*
        * Create the option in the appropriate group...
	*/

        ui_keyword = 1;

        DEBUG_printf(("2_ppdOpen: FOUND ADOBE UI KEYWORD %s WITHOUT OPENUI!",
	              keyword));

        if (!group)
	{
          if ((group = ppd_get_group(ppd, "General", _("General"), pg,
	                             encoding)) == NULL)
	    goto error;

          DEBUG_printf(("2_ppdOpen: Adding to group %s...", group->text));
          option = ppd_get_option(group, keyword);
	  group  = NULL;
	}
	else
          option = ppd_get_option(group, keyword);

	if (option == NULL)
	{
          pg->ppd_status = PPD_ALLOC_ERROR;

          goto error;
	}

       /*
	* Now fill in the initial information for the option...
	*/

	if (!strncmp(keyword, "JCL", 3))
          option->section = PPD_ORDER_JCL;
	else
          option->section = PPD_ORDER_ANY;

	option->order = 10.0f;

	if (i < 8)
          option->ui = PPD_UI_BOOLEAN;
	else
          option->ui = PPD_UI_PICKONE;

        for (j = 0; j < ppd->num_attrs; j ++)
	  if (!strncmp(ppd->attrs[j]->name, "Default", 7) &&
	      !strcmp(ppd->attrs[j]->name + 7, keyword) &&
	      ppd->attrs[j]->value)
	  {
	    DEBUG_printf(("2_ppdOpen: Setting Default%s to %s via attribute...",
	                  option->keyword, ppd->attrs[j]->value));
	    strlcpy(option->defchoice, ppd->attrs[j]->value,
	            sizeof(option->defchoice));
	    break;
	  }

        if (!strcmp(keyword, "PageSize"))
	  strlcpy(option->text, _("Media Size"), sizeof(option->text));
	else if (!strcmp(keyword, "MediaType"))
	  strlcpy(option->text, _("Media Type"), sizeof(option->text));
	else if (!strcmp(keyword, "InputSlot"))
	  strlcpy(option->text, _("Media Source"), sizeof(option->text));
	else if (!strcmp(keyword, "ColorModel"))
	  strlcpy(option->text, _("Output Mode"), sizeof(option->text));
	else if (!strcmp(keyword, "Resolution"))
	  strlcpy(option->text, _("Resolution"), sizeof(option->text));
        else
	  strlcpy(option->text, keyword, sizeof(option->text));
      }
    }

    if (!strcmp(keyword, "LanguageLevel"))
      ppd->language_level = atoi(string);
    else if (!strcmp(keyword, "LanguageEncoding"))
    {
     /*
      * Say all PPD files are UTF-8, since we convert to UTF-8...
      */

      ppd->lang_encoding = strdup("UTF-8");
      encoding           = _ppdGetEncoding(string);
    }
    else if (!strcmp(keyword, "LanguageVersion"))
      ppd->lang_version = string;
    else if (!strcmp(keyword, "Manufacturer"))
      ppd->manufacturer = string;
    else if (!strcmp(keyword, "ModelName"))
      ppd->modelname = string;
    else if (!strcmp(keyword, "Protocols"))
      ppd->protocols = string;
    else if (!strcmp(keyword, "PCFileName"))
      ppd->pcfilename = string;
    else if (!strcmp(keyword, "NickName"))
    {
      if (encoding != CUPS_UTF8)
      {
        cups_utf8_t	utf8[256];	/* UTF-8 version of NickName */


        cupsCharsetToUTF8(utf8, string, sizeof(utf8), encoding);
	ppd->nickname = strdup((char *)utf8);
      }
      else
        ppd->nickname = strdup(string);
    }
    else if (!strcmp(keyword, "Product"))
      ppd->product = string;
    else if (!strcmp(keyword, "ShortNickName"))
      ppd->shortnickname = string;
    else if (!strcmp(keyword, "TTRasterizer"))
      ppd->ttrasterizer = string;
    else if (!strcmp(keyword, "JCLBegin"))
    {
      ppd->jcl_begin = strdup(string);
      ppd_decode(ppd->jcl_begin);	/* Decode quoted string */
    }
    else if (!strcmp(keyword, "JCLEnd"))
    {
      ppd->jcl_end = strdup(string);
      ppd_decode(ppd->jcl_end);		/* Decode quoted string */
    }
    else if (!strcmp(keyword, "JCLToPSInterpreter"))
    {
      ppd->jcl_ps = strdup(string);
      ppd_decode(ppd->jcl_ps);		/* Decode quoted string */
    }
    else if (!strcmp(keyword, "AccurateScreensSupport"))
      ppd->accurate_screens = !strcasecmp(string, "True");
    else if (!strcmp(keyword, "ColorDevice"))
      ppd->color_device = !strcasecmp(string, "True");
    else if (!strcmp(keyword, "ContoneOnly"))
      ppd->contone_only = !strcasecmp(string, "True");
    else if (!strcmp(keyword, "cupsFlipDuplex"))
      ppd->flip_duplex = !strcasecmp(string, "True");
    else if (!strcmp(keyword, "cupsManualCopies"))
      ppd->manual_copies = !strcasecmp(string, "True");
    else if (!strcmp(keyword, "cupsModelNumber"))
      ppd->model_number = atoi(string);
    else if (!strcmp(keyword, "cupsColorProfile"))
    {
      if (ppd->num_profiles == 0)
        profile = malloc(sizeof(ppd_profile_t));
      else
        profile = realloc(ppd->profiles, sizeof(ppd_profile_t) * (size_t)(ppd->num_profiles + 1));

      if (!profile)
      {
        pg->ppd_status = PPD_ALLOC_ERROR;

	goto error;
      }

      ppd->profiles     = profile;
      profile           += ppd->num_profiles;
      ppd->num_profiles ++;

      memset(profile, 0, sizeof(ppd_profile_t));
      strlcpy(profile->resolution, name, sizeof(profile->resolution));
      strlcpy(profile->media_type, text, sizeof(profile->media_type));

      profile->density      = (float)_cupsStrScand(string, &sptr, loc);
      profile->gamma        = (float)_cupsStrScand(sptr, &sptr, loc);
      profile->matrix[0][0] = (float)_cupsStrScand(sptr, &sptr, loc);
      profile->matrix[0][1] = (float)_cupsStrScand(sptr, &sptr, loc);
      profile->matrix[0][2] = (float)_cupsStrScand(sptr, &sptr, loc);
      profile->matrix[1][0] = (float)_cupsStrScand(sptr, &sptr, loc);
      profile->matrix[1][1] = (float)_cupsStrScand(sptr, &sptr, loc);
      profile->matrix[1][2] = (float)_cupsStrScand(sptr, &sptr, loc);
      profile->matrix[2][0] = (float)_cupsStrScand(sptr, &sptr, loc);
      profile->matrix[2][1] = (float)_cupsStrScand(sptr, &sptr, loc);
      profile->matrix[2][2] = (float)_cupsStrScand(sptr, &sptr, loc);
    }
    else if (!strcmp(keyword, "cupsFilter"))
    {
      if (ppd->num_filters == 0)
        filter = malloc(sizeof(char *));
      else
        filter = realloc(ppd->filters, sizeof(char *) * (size_t)(ppd->num_filters + 1));

      if (filter == NULL)
      {
        pg->ppd_status = PPD_ALLOC_ERROR;

	goto error;
      }

      ppd->filters     = filter;
      filter           += ppd->num_filters;
      ppd->num_filters ++;

     /*
      * Make a copy of the filter string...
      */

      *filter = strdup(string);
    }
    else if (!strcmp(keyword, "Throughput"))
      ppd->throughput = atoi(string);
    else if (!strcmp(keyword, "Font"))
    {
     /*
      * Add this font to the list of available fonts...
      */

      if (ppd->num_fonts == 0)
        tempfonts = (char **)malloc(sizeof(char *));
      else
        tempfonts = (char **)realloc(ppd->fonts, sizeof(char *) * (size_t)(ppd->num_fonts + 1));

      if (tempfonts == NULL)
      {
        pg->ppd_status = PPD_ALLOC_ERROR;

	goto error;
      }

      ppd->fonts                 = tempfonts;
      ppd->fonts[ppd->num_fonts] = strdup(name);
      ppd->num_fonts ++;
    }
    else if (!strncmp(keyword, "ParamCustom", 11))
    {
      ppd_coption_t	*coption;	/* Custom option */
      ppd_cparam_t	*cparam;	/* Custom parameter */
      int		corder;		/* Order number */
      char		ctype[33],	/* Data type */
			cminimum[65],	/* Minimum value */
			cmaximum[65];	/* Maximum value */


     /*
      * Get the custom option and parameter...
      */

      if ((coption = ppd_get_coption(ppd, keyword + 11)) == NULL)
      {
        pg->ppd_status = PPD_ALLOC_ERROR;

	goto error;
      }

      if ((cparam = ppd_get_cparam(coption, name, text)) == NULL)
      {
        pg->ppd_status = PPD_ALLOC_ERROR;

	goto error;
      }

      if (cparam->type != PPD_CUSTOM_UNKNOWN)
      {
        pg->ppd_status = PPD_BAD_CUSTOM_PARAM;

        goto error;
      }

     /*
      * Get the parameter data...
      */

      if (!string ||
          sscanf(string, "%d%32s%64s%64s", &corder, ctype, cminimum,
                 cmaximum) != 4)
      {
        pg->ppd_status = PPD_BAD_CUSTOM_PARAM;

	goto error;
      }

      cparam->order = corder;

      if (!strcmp(ctype, "curve"))
      {
        cparam->type = PPD_CUSTOM_CURVE;
	cparam->minimum.custom_curve = (float)_cupsStrScand(cminimum, NULL, loc);
	cparam->maximum.custom_curve = (float)_cupsStrScand(cmaximum, NULL, loc);
      }
      else if (!strcmp(ctype, "int"))
      {
        cparam->type = PPD_CUSTOM_INT;
	cparam->minimum.custom_int = atoi(cminimum);
	cparam->maximum.custom_int = atoi(cmaximum);
      }
      else if (!strcmp(ctype, "invcurve"))
      {
        cparam->type = PPD_CUSTOM_INVCURVE;
	cparam->minimum.custom_invcurve = (float)_cupsStrScand(cminimum, NULL, loc);
	cparam->maximum.custom_invcurve = (float)_cupsStrScand(cmaximum, NULL, loc);
      }
      else if (!strcmp(ctype, "passcode"))
      {
        cparam->type = PPD_CUSTOM_PASSCODE;
	cparam->minimum.custom_passcode = atoi(cminimum);
	cparam->maximum.custom_passcode = atoi(cmaximum);
      }
      else if (!strcmp(ctype, "password"))
      {
        cparam->type = PPD_CUSTOM_PASSWORD;
	cparam->minimum.custom_password = atoi(cminimum);
	cparam->maximum.custom_password = atoi(cmaximum);
      }
      else if (!strcmp(ctype, "points"))
      {
        cparam->type = PPD_CUSTOM_POINTS;
	cparam->minimum.custom_points = (float)_cupsStrScand(cminimum, NULL, loc);
	cparam->maximum.custom_points = (float)_cupsStrScand(cmaximum, NULL, loc);
      }
      else if (!strcmp(ctype, "real"))
      {
        cparam->type = PPD_CUSTOM_REAL;
	cparam->minimum.custom_real = (float)_cupsStrScand(cminimum, NULL, loc);
	cparam->maximum.custom_real = (float)_cupsStrScand(cmaximum, NULL, loc);
      }
      else if (!strcmp(ctype, "string"))
      {
        cparam->type = PPD_CUSTOM_STRING;
	cparam->minimum.custom_string = atoi(cminimum);
	cparam->maximum.custom_string = atoi(cmaximum);
      }
      else
      {
        pg->ppd_status = PPD_BAD_CUSTOM_PARAM;

	goto error;
      }

     /*
      * Now special-case for CustomPageSize...
      */

      if (!strcmp(coption->keyword, "PageSize"))
      {
	if (!strcmp(name, "Width"))
	{
	  ppd->custom_min[0] = cparam->minimum.custom_points;
	  ppd->custom_max[0] = cparam->maximum.custom_points;
	}
	else if (!strcmp(name, "Height"))
	{
	  ppd->custom_min[1] = cparam->minimum.custom_points;
	  ppd->custom_max[1] = cparam->maximum.custom_points;
	}
      }
    }
    else if (!strcmp(keyword, "HWMargins"))
    {
      for (i = 0, sptr = string; i < 4; i ++)
        ppd->custom_margins[i] = (float)_cupsStrScand(sptr, &sptr, loc);
    }
    else if (!strncmp(keyword, "Custom", 6) && !strcmp(name, "True") && !option)
    {
      ppd_option_t	*custom_option;	/* Custom option */

      DEBUG_puts("2_ppdOpen: Processing Custom option...");

     /*
      * Get the option and custom option...
      */

      if (!ppd_get_coption(ppd, keyword + 6))
      {
        pg->ppd_status = PPD_ALLOC_ERROR;

	goto error;
      }

      if (option && !_cups_strcasecmp(option->keyword, keyword + 6))
        custom_option = option;
      else
        custom_option = ppdFindOption(ppd, keyword + 6);

      if (custom_option)
      {
       /*
	* Add the "custom" option...
	*/

        if ((choice = ppdFindChoice(custom_option, "Custom")) == NULL)
	  if ((choice = ppd_add_choice(custom_option, "Custom")) == NULL)
	  {
	    DEBUG_puts("1_ppdOpen: Unable to add Custom choice!");

	    pg->ppd_status = PPD_ALLOC_ERROR;

	    goto error;
	  }

	strlcpy(choice->text, text[0] ? text : _("Custom"),
		sizeof(choice->text));

	choice->code = strdup(string);

	if (custom_option->section == PPD_ORDER_JCL)
	  ppd_decode(choice->code);
      }

     /*
      * Now process custom page sizes specially...
      */

      if (!strcmp(keyword, "CustomPageSize"))
      {
       /*
	* Add a "Custom" page size entry...
	*/

	ppd->variable_sizes = 1;

	ppd_add_size(ppd, "Custom");

	if (option && !_cups_strcasecmp(option->keyword, "PageRegion"))
	  custom_option = option;
	else
	  custom_option = ppdFindOption(ppd, "PageRegion");

        if (custom_option)
	{
	  if ((choice = ppdFindChoice(custom_option, "Custom")) == NULL)
	    if ((choice = ppd_add_choice(custom_option, "Custom")) == NULL)
	    {
	      DEBUG_puts("1_ppdOpen: Unable to add Custom choice!");

	      pg->ppd_status = PPD_ALLOC_ERROR;

	      goto error;
	    }

	  strlcpy(choice->text, text[0] ? text : _("Custom"),
		  sizeof(choice->text));
        }
      }
    }
    else if (!strcmp(keyword, "LandscapeOrientation"))
    {
      if (!strcmp(string, "Minus90"))
        ppd->landscape = -90;
      else if (!strcmp(string, "Plus90"))
        ppd->landscape = 90;
    }
    else if (!strcmp(keyword, "Emulators") && string && ppd->num_emulations == 0)
    {
     /*
      * Issue #5562: Samsung printer drivers incorrectly use Emulators keyword
      *              to configure themselves
      *
      * The Emulators keyword was loaded but never used by anything in CUPS,
      * and has no valid purpose in CUPS.  The old code was removed due to a
      * memory leak (Issue #5475), so the following (new) code supports a single
      * name for the Emulators keyword, allowing these drivers to work until we
      * remove PPD and driver support entirely in a future version of CUPS.
      */

      ppd->num_emulations = 1;
      ppd->emulations     = calloc(1, sizeof(ppd_emul_t));

      strlcpy(ppd->emulations[0].name, string, sizeof(ppd->emulations[0].name));
    }
    else if (!strcmp(keyword, "JobPatchFile"))
    {
     /*
      * CUPS STR #3421: Check for "*JobPatchFile: int: string"
      */

      if (isdigit(*string & 255))
      {
        for (sptr = string + 1; isdigit(*sptr & 255); sptr ++);

        if (*sptr == ':')
        {
         /*
          * Found "*JobPatchFile: int: string"...
          */

          pg->ppd_status = PPD_BAD_VALUE;

	  goto error;
        }
      }

      if (!name[0] && pg->ppd_conform == PPD_CONFORM_STRICT)
      {
       /*
        * Found "*JobPatchFile: string"...
        */

        pg->ppd_status = PPD_MISSING_OPTION_KEYWORD;

	goto error;
      }

      if (ppd->patches == NULL)
        ppd->patches = strdup(string);
      else
      {
        temp = realloc(ppd->patches, strlen(ppd->patches) +
	                             strlen(string) + 1);
        if (temp == NULL)
	{
          pg->ppd_status = PPD_ALLOC_ERROR;

	  goto error;
	}

        ppd->patches = temp;

        memcpy(ppd->patches + strlen(ppd->patches), string, strlen(string) + 1);
      }
    }
    else if (!strcmp(keyword, "OpenUI"))
    {
     /*
      * Don't allow nesting of options...
      */

      if (option && pg->ppd_conform == PPD_CONFORM_STRICT)
      {
        pg->ppd_status = PPD_NESTED_OPEN_UI;

	goto error;
      }

     /*
      * Add an option record to the current sub-group, group, or file...
      */

      DEBUG_printf(("2_ppdOpen: name=\"%s\" (%d)", name, (int)strlen(name)));

      if (name[0] == '*')
        _cups_strcpy(name, name + 1); /* Eliminate leading asterisk */

      for (i = (int)strlen(name) - 1; i > 0 && _cups_isspace(name[i]); i --)
        name[i] = '\0'; /* Eliminate trailing spaces */

      DEBUG_printf(("2_ppdOpen: OpenUI of %s in group %s...", name,
                    group ? group->text : "(null)"));

      if (subgroup != NULL)
        option = ppd_get_option(subgroup, name);
      else if (group == NULL)
      {
	if ((group = ppd_get_group(ppd, "General", _("General"), pg,
	                           encoding)) == NULL)
	  goto error;

        DEBUG_printf(("2_ppdOpen: Adding to group %s...", group->text));
        option = ppd_get_option(group, name);
	group  = NULL;
      }
      else
        option = ppd_get_option(group, name);

      if (option == NULL)
      {
        pg->ppd_status = PPD_ALLOC_ERROR;

	goto error;
      }

     /*
      * Now fill in the initial information for the option...
      */

      if (string && !strcmp(string, "PickMany"))
        option->ui = PPD_UI_PICKMANY;
      else if (string && !strcmp(string, "Boolean"))
        option->ui = PPD_UI_BOOLEAN;
      else if (string && !strcmp(string, "PickOne"))
        option->ui = PPD_UI_PICKONE;
      else if (pg->ppd_conform == PPD_CONFORM_STRICT)
      {
        pg->ppd_status = PPD_BAD_OPEN_UI;

	goto error;
      }
      else
        option->ui = PPD_UI_PICKONE;

      for (j = 0; j < ppd->num_attrs; j ++)
	if (!strncmp(ppd->attrs[j]->name, "Default", 7) &&
	    !strcmp(ppd->attrs[j]->name + 7, name) &&
	    ppd->attrs[j]->value)
	{
	  DEBUG_printf(("2_ppdOpen: Setting Default%s to %s via attribute...",
	                option->keyword, ppd->attrs[j]->value));
	  strlcpy(option->defchoice, ppd->attrs[j]->value,
	          sizeof(option->defchoice));
	  break;
	}

      if (text[0])
        cupsCharsetToUTF8((cups_utf8_t *)option->text, text,
	                   sizeof(option->text), encoding);
      else
      {
        if (!strcmp(name, "PageSize"))
	  strlcpy(option->text, _("Media Size"), sizeof(option->text));
	else if (!strcmp(name, "MediaType"))
	  strlcpy(option->text, _("Media Type"), sizeof(option->text));
	else if (!strcmp(name, "InputSlot"))
	  strlcpy(option->text, _("Media Source"), sizeof(option->text));
	else if (!strcmp(name, "ColorModel"))
	  strlcpy(option->text, _("Output Mode"), sizeof(option->text));
	else if (!strcmp(name, "Resolution"))
	  strlcpy(option->text, _("Resolution"), sizeof(option->text));
        else
	  strlcpy(option->text, name, sizeof(option->text));
      }

      option->section = PPD_ORDER_ANY;

      free(string);
      string = NULL;

     /*
      * Add a custom option choice if we have already seen a CustomFoo
      * attribute...
      */

      if (!_cups_strcasecmp(name, "PageRegion"))
        strlcpy(custom_name, "CustomPageSize", sizeof(custom_name));
      else
        snprintf(custom_name, sizeof(custom_name), "Custom%s", name);

      if ((custom_attr = ppdFindAttr(ppd, custom_name, "True")) != NULL)
      {
        if ((choice = ppdFindChoice(option, "Custom")) == NULL)
	  if ((choice = ppd_add_choice(option, "Custom")) == NULL)
	  {
	    DEBUG_puts("1_ppdOpen: Unable to add Custom choice!");

	    pg->ppd_status = PPD_ALLOC_ERROR;

	    goto error;
	  }

	strlcpy(choice->text,
	        custom_attr->text[0] ? custom_attr->text : _("Custom"),
		sizeof(choice->text));
        choice->code = strdup(custom_attr->value);
      }
    }
    else if (!strcmp(keyword, "JCLOpenUI"))
    {
     /*
      * Don't allow nesting of options...
      */

      if (option && pg->ppd_conform == PPD_CONFORM_STRICT)
      {
        pg->ppd_status = PPD_NESTED_OPEN_UI;

	goto error;
      }

     /*
      * Find the JCL group, and add if needed...
      */

      group = ppd_get_group(ppd, "JCL", _("JCL"), pg, encoding);

      if (group == NULL)
	goto error;

     /*
      * Add an option record to the current JCLs...
      */

      if (name[0] == '*')
        _cups_strcpy(name, name + 1);

      option = ppd_get_option(group, name);

      if (option == NULL)
      {
        pg->ppd_status = PPD_ALLOC_ERROR;

	goto error;
      }

     /*
      * Now fill in the initial information for the option...
      */

      if (string && !strcmp(string, "PickMany"))
        option->ui = PPD_UI_PICKMANY;
      else if (string && !strcmp(string, "Boolean"))
        option->ui = PPD_UI_BOOLEAN;
      else if (string && !strcmp(string, "PickOne"))
        option->ui = PPD_UI_PICKONE;
      else
      {
        pg->ppd_status = PPD_BAD_OPEN_UI;

	goto error;
      }

      for (j = 0; j < ppd->num_attrs; j ++)
	if (!strncmp(ppd->attrs[j]->name, "Default", 7) &&
	    !strcmp(ppd->attrs[j]->name + 7, name) &&
	    ppd->attrs[j]->value)
	{
	  DEBUG_printf(("2_ppdOpen: Setting Default%s to %s via attribute...",
	                option->keyword, ppd->attrs[j]->value));
	  strlcpy(option->defchoice, ppd->attrs[j]->value,
	          sizeof(option->defchoice));
	  break;
	}

      if (text[0])
        cupsCharsetToUTF8((cups_utf8_t *)option->text, text,
	                   sizeof(option->text), encoding);
      else
        strlcpy(option->text, name, sizeof(option->text));

      option->section = PPD_ORDER_JCL;
      group = NULL;

      free(string);
      string = NULL;

     /*
      * Add a custom option choice if we have already seen a CustomFoo
      * attribute...
      */

      snprintf(custom_name, sizeof(custom_name), "Custom%s", name);

      if ((custom_attr = ppdFindAttr(ppd, custom_name, "True")) != NULL)
      {
	if ((choice = ppd_add_choice(option, "Custom")) == NULL)
	{
	  DEBUG_puts("1_ppdOpen: Unable to add Custom choice!");

	  pg->ppd_status = PPD_ALLOC_ERROR;

	  goto error;
	}

	strlcpy(choice->text,
	        custom_attr->text[0] ? custom_attr->text : _("Custom"),
		sizeof(choice->text));
        choice->code = strdup(custom_attr->value);
      }
    }
    else if (!strcmp(keyword, "CloseUI"))
    {
      if ((!option || option->section == PPD_ORDER_JCL) && pg->ppd_conform == PPD_CONFORM_STRICT)
      {
        pg->ppd_status = PPD_BAD_CLOSE_UI;

	goto error;
      }

      if (option && (!_cups_strcasecmp(option->defchoice, "custom") || !_cups_strncasecmp(option->defchoice, "custom.", 7)))
      {
       /*
	* "*DefaultOption: Custom..." may set the default to a custom value
	* or (for a very small number of incompatible PPD files) select a
	* standard choice for the option, which CUPS renames to "_Custom..."
	* to avoid compatibility issues.  See which this is...
	*/

        char tchoice[PPD_MAX_NAME];	/* Temporary choice name */

	snprintf(tchoice, sizeof(tchoice), "_%s", option->defchoice);

	if (ppdFindChoice(option, tchoice))
	{
	  strlcpy(option->defchoice, tchoice, sizeof(option->defchoice));

	  DEBUG_printf(("2_ppdOpen: Reset Default%s to %s...", option->keyword, tchoice));
	}
      }

      option = NULL;

      free(string);
      string = NULL;
    }
    else if (!strcmp(keyword, "JCLCloseUI"))
    {
      if ((!option || option->section != PPD_ORDER_JCL) && pg->ppd_conform == PPD_CONFORM_STRICT)
      {
        pg->ppd_status = PPD_BAD_CLOSE_UI;

	goto error;
      }

      if (option && (!_cups_strcasecmp(option->defchoice, "custom") || !_cups_strncasecmp(option->defchoice, "custom.", 7)))
      {
       /*
	* "*DefaultOption: Custom..." may set the default to a custom value
	* or (for a very small number of incompatible PPD files) select a
	* standard choice for the option, which CUPS renames to "_Custom..."
	* to avoid compatibility issues.  See which this is...
	*/

        char tchoice[PPD_MAX_NAME];	/* Temporary choice name */

	snprintf(tchoice, sizeof(tchoice), "_%s", option->defchoice);

	if (ppdFindChoice(option, tchoice))
	{
	  strlcpy(option->defchoice, tchoice, sizeof(option->defchoice));

	  DEBUG_printf(("2_ppdOpen: Reset Default%s to %s...", option->keyword, tchoice));
	}
      }

      option = NULL;

      free(string);
      string = NULL;
    }
    else if (!strcmp(keyword, "OpenGroup"))
    {
     /*
      * Open a new group...
      */

      if (group != NULL)
      {
        pg->ppd_status = PPD_NESTED_OPEN_GROUP;

	goto error;
      }

      if (!string)
      {
        pg->ppd_status = PPD_BAD_OPEN_GROUP;

	goto error;
      }

     /*
      * Separate the group name from the text (name/text)...
      */

      if ((sptr = strchr(string, '/')) != NULL)
        *sptr++ = '\0';
      else
        sptr = string;

     /*
      * Fix up the text...
      */

      ppd_decode(sptr);

     /*
      * Find/add the group...
      */

      group = ppd_get_group(ppd, string, sptr, pg, encoding);

      if (group == NULL)
	goto error;

      free(string);
      string = NULL;
    }
    else if (!strcmp(keyword, "CloseGroup"))
    {
      group = NULL;

      free(string);
      string = NULL;
    }
    else if (!strcmp(keyword, "OrderDependency"))
    {
      order = (float)_cupsStrScand(string, &sptr, loc);

      if (!sptr || sscanf(sptr, "%40s%40s", name, keyword) != 2)
      {
        pg->ppd_status = PPD_BAD_ORDER_DEPENDENCY;

	goto error;
      }

      if (keyword[0] == '*')
        _cups_strcpy(keyword, keyword + 1);

      if (!strcmp(name, "ExitServer"))
        section = PPD_ORDER_EXIT;
      else if (!strcmp(name, "Prolog"))
        section = PPD_ORDER_PROLOG;
      else if (!strcmp(name, "DocumentSetup"))
        section = PPD_ORDER_DOCUMENT;
      else if (!strcmp(name, "PageSetup"))
        section = PPD_ORDER_PAGE;
      else if (!strcmp(name, "JCLSetup"))
        section = PPD_ORDER_JCL;
      else
        section = PPD_ORDER_ANY;

      if (option == NULL)
      {
        ppd_group_t	*gtemp;


       /*
        * Only valid for Non-UI options...
	*/

        for (i = ppd->num_groups, gtemp = ppd->groups; i > 0; i --, gtemp ++)
          if (gtemp->text[0] == '\0')
	    break;

        if (i > 0)
          for (i = 0; i < gtemp->num_options; i ++)
	    if (!strcmp(keyword, gtemp->options[i].keyword))
	    {
	      gtemp->options[i].section = section;
	      gtemp->options[i].order   = order;
	      break;
	    }
      }
      else
      {
        option->section = section;
	option->order   = order;
      }

      free(string);
      string = NULL;
    }
    else if (!strncmp(keyword, "Default", 7))
    {
      if (string == NULL)
        continue;

     /*
      * Drop UI text, if any, from value...
      */

      if (strchr(string, '/') != NULL)
        *strchr(string, '/') = '\0';

     /*
      * Assign the default value as appropriate...
      */

      if (!strcmp(keyword, "DefaultColorSpace"))
      {
       /*
        * Set default colorspace...
	*/

	if (!strcmp(string, "CMY"))
          ppd->colorspace = PPD_CS_CMY;
	else if (!strcmp(string, "CMYK"))
          ppd->colorspace = PPD_CS_CMYK;
	else if (!strcmp(string, "RGB"))
          ppd->colorspace = PPD_CS_RGB;
	else if (!strcmp(string, "RGBK"))
          ppd->colorspace = PPD_CS_RGBK;
	else if (!strcmp(string, "N"))
          ppd->colorspace = PPD_CS_N;
	else
          ppd->colorspace = PPD_CS_GRAY;
      }
      else if (option && !strcmp(keyword + 7, option->keyword))
      {
       /*
        * Set the default as part of the current option...
	*/

	strlcpy(option->defchoice, string, sizeof(option->defchoice));

        DEBUG_printf(("2_ppdOpen: Set %s to %s...", keyword, option->defchoice));
      }
      else
      {
       /*
        * Lookup option and set if it has been defined...
	*/

        ppd_option_t	*toption;	/* Temporary option */

        if ((toption = ppdFindOption(ppd, keyword + 7)) != NULL)
	{
	  if (!_cups_strcasecmp(string, "custom") || !_cups_strncasecmp(string, "custom.", 7))
	  {
	   /*
	    * "*DefaultOption: Custom..." may set the default to a custom value
	    * or (for a very small number of incompatible PPD files) select a
	    * standard choice for the option, which CUPS renames to "_Custom..."
	    * to avoid compatibility issues.  See which this is...
	    */

	    snprintf(toption->defchoice, sizeof(toption->defchoice), "_%s", string);
	    if (!ppdFindChoice(toption, toption->defchoice))
	      strlcpy(toption->defchoice, string, sizeof(toption->defchoice));
	  }
	  else
	  {
	    strlcpy(toption->defchoice, string, sizeof(toption->defchoice));
	  }

	  DEBUG_printf(("2_ppdOpen: Set %s to %s...", keyword, toption->defchoice));
	}
      }
    }
    else if (!strcmp(keyword, "UIConstraints") ||
             !strcmp(keyword, "NonUIConstraints"))
    {
      if (!string)
      {
	pg->ppd_status = PPD_BAD_UI_CONSTRAINTS;
	goto error;
      }

      if (ppd->num_consts == 0)
	constraint = calloc(2, sizeof(ppd_const_t));
      else
	constraint = realloc(ppd->consts, (size_t)(ppd->num_consts + 2) * sizeof(ppd_const_t));

      if (constraint == NULL)
      {
        pg->ppd_status = PPD_ALLOC_ERROR;

	goto error;
      }

      ppd->consts = constraint;
      constraint += ppd->num_consts;
      ppd->num_consts ++;

      switch (sscanf(string, "%40s%40s%40s%40s", constraint->option1,
                     constraint->choice1, constraint->option2,
		     constraint->choice2))
      {
        default : /* Error */
	    pg->ppd_status = PPD_BAD_UI_CONSTRAINTS;
	    goto error;

	case 2 : /* Two options... */
	   /*
	    * Check for broken constraints like "* Option"...
	    */

	    if (pg->ppd_conform == PPD_CONFORM_STRICT &&
	        (!strcmp(constraint->option1, "*") ||
	         !strcmp(constraint->choice1, "*")))
	    {
	      pg->ppd_status = PPD_BAD_UI_CONSTRAINTS;
	      goto error;
	    }

	   /*
	    * The following strcpy's are safe, as optionN and
	    * choiceN are all the same size (size defined by PPD spec...)
	    */

	    if (constraint->option1[0] == '*')
	      _cups_strcpy(constraint->option1, constraint->option1 + 1);
	    else if (pg->ppd_conform == PPD_CONFORM_STRICT)
	    {
	      pg->ppd_status = PPD_BAD_UI_CONSTRAINTS;
	      goto error;
	    }

	    if (constraint->choice1[0] == '*')
	      _cups_strcpy(constraint->option2, constraint->choice1 + 1);
	    else if (pg->ppd_conform == PPD_CONFORM_STRICT)
	    {
	      pg->ppd_status = PPD_BAD_UI_CONSTRAINTS;
	      goto error;
	    }

            constraint->choice1[0] = '\0';
            constraint->choice2[0] = '\0';
	    break;

	case 3 : /* Two options, one choice... */
	   /*
	    * Check for broken constraints like "* Option"...
	    */

	    if (pg->ppd_conform == PPD_CONFORM_STRICT &&
	        (!strcmp(constraint->option1, "*") ||
	         !strcmp(constraint->choice1, "*") ||
	         !strcmp(constraint->option2, "*")))
	    {
	      pg->ppd_status = PPD_BAD_UI_CONSTRAINTS;
	      goto error;
	    }

	   /*
	    * The following _cups_strcpy's are safe, as optionN and
	    * choiceN are all the same size (size defined by PPD spec...)
	    */

	    if (constraint->option1[0] == '*')
	      _cups_strcpy(constraint->option1, constraint->option1 + 1);
	    else if (pg->ppd_conform == PPD_CONFORM_STRICT)
	    {
	      pg->ppd_status = PPD_BAD_UI_CONSTRAINTS;
	      goto error;
	    }

	    if (constraint->choice1[0] == '*')
	    {
	      if (pg->ppd_conform == PPD_CONFORM_STRICT &&
	          constraint->option2[0] == '*')
	      {
		pg->ppd_status = PPD_BAD_UI_CONSTRAINTS;
		goto error;
	      }

	      _cups_strcpy(constraint->choice2, constraint->option2);
	      _cups_strcpy(constraint->option2, constraint->choice1 + 1);
              constraint->choice1[0] = '\0';
	    }
	    else
	    {
	      if (constraint->option2[0] == '*')
  	        _cups_strcpy(constraint->option2, constraint->option2 + 1);
	      else if (pg->ppd_conform == PPD_CONFORM_STRICT)
	      {
		pg->ppd_status = PPD_BAD_UI_CONSTRAINTS;
		goto error;
	      }

              constraint->choice2[0] = '\0';
	    }
	    break;

	case 4 : /* Two options, two choices... */
	   /*
	    * Check for broken constraints like "* Option"...
	    */

	    if (pg->ppd_conform == PPD_CONFORM_STRICT &&
	        (!strcmp(constraint->option1, "*") ||
	         !strcmp(constraint->choice1, "*") ||
	         !strcmp(constraint->option2, "*") ||
	         !strcmp(constraint->choice2, "*")))
	    {
	      pg->ppd_status = PPD_BAD_UI_CONSTRAINTS;
	      goto error;
	    }

	    if (constraint->option1[0] == '*')
	      _cups_strcpy(constraint->option1, constraint->option1 + 1);
	    else if (pg->ppd_conform == PPD_CONFORM_STRICT)
	    {
	      pg->ppd_status = PPD_BAD_UI_CONSTRAINTS;
	      goto error;
	    }

            if (pg->ppd_conform == PPD_CONFORM_STRICT &&
	        constraint->choice1[0] == '*')
	    {
	      pg->ppd_status = PPD_BAD_UI_CONSTRAINTS;
	      goto error;
	    }

	    if (constraint->option2[0] == '*')
  	      _cups_strcpy(constraint->option2, constraint->option2 + 1);
	    else if (pg->ppd_conform == PPD_CONFORM_STRICT)
	    {
	      pg->ppd_status = PPD_BAD_UI_CONSTRAINTS;
	      goto error;
	    }

            if (pg->ppd_conform == PPD_CONFORM_STRICT &&
	        constraint->choice2[0] == '*')
	    {
	      pg->ppd_status = PPD_BAD_UI_CONSTRAINTS;
	      goto error;
	    }
	    break;
      }

     /*
      * Don't add this one as an attribute...
      */

      free(string);
      string = NULL;
    }
    else if (!strcmp(keyword, "PaperDimension"))
    {
      if (!_cups_strcasecmp(name, "custom") || !_cups_strncasecmp(name, "custom.", 7))
      {
        char cname[PPD_MAX_NAME];	/* Rewrite with a leading underscore */
        snprintf(cname, sizeof(cname), "_%s", name);
        strlcpy(name, cname, sizeof(name));
      }

      if ((size = ppdPageSize(ppd, name)) == NULL)
	size = ppd_add_size(ppd, name);

      if (size == NULL)
      {
       /*
        * Unable to add or find size!
	*/

        pg->ppd_status = PPD_ALLOC_ERROR;

	goto error;
      }

      size->width  = (float)_cupsStrScand(string, &sptr, loc);
      size->length = (float)_cupsStrScand(sptr, NULL, loc);

      free(string);
      string = NULL;
    }
    else if (!strcmp(keyword, "ImageableArea"))
    {
      if (!_cups_strcasecmp(name, "custom") || !_cups_strncasecmp(name, "custom.", 7))
      {
        char cname[PPD_MAX_NAME];	/* Rewrite with a leading underscore */
        snprintf(cname, sizeof(cname), "_%s", name);
        strlcpy(name, cname, sizeof(name));
      }

      if ((size = ppdPageSize(ppd, name)) == NULL)
	size = ppd_add_size(ppd, name);

      if (size == NULL)
      {
       /*
        * Unable to add or find size!
	*/

        pg->ppd_status = PPD_ALLOC_ERROR;

	goto error;
      }

      size->left   = (float)_cupsStrScand(string, &sptr, loc);
      size->bottom = (float)_cupsStrScand(sptr, &sptr, loc);
      size->right  = (float)_cupsStrScand(sptr, &sptr, loc);
      size->top    = (float)_cupsStrScand(sptr, NULL, loc);

      free(string);
      string = NULL;
    }
    else if (option != NULL &&
             (mask & (PPD_KEYWORD | PPD_OPTION | PPD_STRING)) ==
	         (PPD_KEYWORD | PPD_OPTION | PPD_STRING) &&
	     !strcmp(keyword, option->keyword))
    {
      DEBUG_printf(("2_ppdOpen: group=%p, subgroup=%p", group, subgroup));

      if (!_cups_strcasecmp(name, "custom") || !_cups_strncasecmp(name, "custom.", 7))
      {
        char cname[PPD_MAX_NAME];	/* Rewrite with a leading underscore */
        snprintf(cname, sizeof(cname), "_%s", name);
        strlcpy(name, cname, sizeof(name));
      }

      if (!strcmp(keyword, "PageSize"))
      {
       /*
        * Add a page size...
	*/

        if (ppdPageSize(ppd, name) == NULL)
	  ppd_add_size(ppd, name);
      }

     /*
      * Add the option choice...
      */

      if ((choice = ppd_add_choice(option, name)) == NULL)
      {
        pg->ppd_status = PPD_ALLOC_ERROR;

	goto error;
      }

      if (text[0])
        cupsCharsetToUTF8((cups_utf8_t *)choice->text, text,
	                   sizeof(choice->text), encoding);
      else if (!strcmp(name, "True"))
        strlcpy(choice->text, _("Yes"), sizeof(choice->text));
      else if (!strcmp(name, "False"))
        strlcpy(choice->text, _("No"), sizeof(choice->text));
      else
        strlcpy(choice->text, name, sizeof(choice->text));

      if (option->section == PPD_ORDER_JCL)
        ppd_decode(string);		/* Decode quoted string */

      choice->code = string;
      string       = NULL;		/* Don't add as an attribute below */
    }

   /*
    * Add remaining lines with keywords and string values as attributes...
    */

    if (string &&
        (mask & (PPD_KEYWORD | PPD_STRING)) == (PPD_KEYWORD | PPD_STRING))
      ppd_add_attr(ppd, keyword, name, text, string);
    else
      free(string);
  }

 /*
  * Check for a missing CloseUI/JCLCloseUI...
  */

  if (option && pg->ppd_conform == PPD_CONFORM_STRICT)
  {
    pg->ppd_status = PPD_MISSING_CLOSE_UI;
    goto error;
  }

 /*
  * Check for a missing CloseGroup...
  */

  if (group && pg->ppd_conform == PPD_CONFORM_STRICT)
  {
    pg->ppd_status = PPD_MISSING_CLOSE_GROUP;
    goto error;
  }

  free(line.buffer);

 /*
  * Reset language preferences...
  */

#ifdef DEBUG
  if (!cupsFileEOF(fp))
    DEBUG_printf(("1_ppdOpen: Premature EOF at %lu...\n",
                  (unsigned long)cupsFileTell(fp)));
#endif /* DEBUG */

  if (pg->ppd_status != PPD_OK)
  {
   /*
    * Had an error reading the PPD file, cannot continue!
    */

    ppdClose(ppd);

    return (NULL);
  }

 /*
  * Update the filters array as needed...
  */

  if (!ppd_update_filters(ppd, pg))
  {
    ppdClose(ppd);

    return (NULL);
  }

 /*
  * Create the sorted options array and set the option back-pointer for
  * each choice and custom option...
  */

  ppd->options = cupsArrayNew2((cups_array_func_t)ppd_compare_options, NULL,
                               (cups_ahash_func_t)ppd_hash_option,
			       PPD_HASHSIZE);

  for (i = ppd->num_groups, group = ppd->groups;
       i > 0;
       i --, group ++)
  {
    for (j = group->num_options, option = group->options;
         j > 0;
	 j --, option ++)
    {
      ppd_coption_t	*coption;	/* Custom option */


      cupsArrayAdd(ppd->options, option);

      for (k = 0; k < option->num_choices; k ++)
        option->choices[k].option = option;

      if ((coption = ppdFindCustomOption(ppd, option->keyword)) != NULL)
        coption->option = option;
    }
  }

 /*
  * Create an array to track the marked choices...
  */

  ppd->marked = cupsArrayNew((cups_array_func_t)ppd_compare_choices, NULL);

 /*
  * Return the PPD file structure...
  */

  return (ppd);

 /*
  * Common exit point for errors to save code size...
  */

  error:

  free(string);
  free(line.buffer);

  ppdClose(ppd);

  return (NULL);
}


/*
 * 'ppdOpen()' - Read a PPD file into memory.
 */

ppd_file_t *				/* O - PPD file record */
ppdOpen(FILE *fp)			/* I - File to read from */
{
  ppd_file_t	*ppd;			/* PPD file record */
  cups_file_t	*cf;			/* CUPS file */


 /*
  * Reopen the stdio file as a CUPS file...
  */

  if ((cf = cupsFileOpenFd(fileno(fp), "r")) == NULL)
    return (NULL);

 /*
  * Load the PPD file using the newer API...
  */

  ppd = _ppdOpen(cf, _PPD_LOCALIZATION_DEFAULT);

 /*
  * Close the CUPS file and return the PPD...
  */

  cupsFileClose(cf);

  return (ppd);
}


/*
 * 'ppdOpen2()' - Read a PPD file into memory.
 *
 * @since CUPS 1.2/macOS 10.5@
 */

ppd_file_t *				/* O - PPD file record or @code NULL@ if the PPD file could not be opened. */
ppdOpen2(cups_file_t *fp)		/* I - File to read from */
{
  return _ppdOpen(fp, _PPD_LOCALIZATION_DEFAULT);
}


/*
 * 'ppdOpenFd()' - Read a PPD file into memory.
 */

ppd_file_t *				/* O - PPD file record or @code NULL@ if the PPD file could not be opened. */
ppdOpenFd(int fd)			/* I - File to read from */
{
  cups_file_t		*fp;		/* CUPS file pointer */
  ppd_file_t		*ppd;		/* PPD file record */
  _ppd_globals_t	*pg = _ppdGlobals();
					/* Global data */


 /*
  * Set the line number to 0...
  */

  pg->ppd_line = 0;

 /*
  * Range check input...
  */

  if (fd < 0)
  {
    pg->ppd_status = PPD_NULL_FILE;

    return (NULL);
  }

 /*
  * Try to open the file and parse it...
  */

  if ((fp = cupsFileOpenFd(fd, "r")) != NULL)
  {
    ppd = ppdOpen2(fp);

    cupsFileClose(fp);
  }
  else
  {
    pg->ppd_status = PPD_FILE_OPEN_ERROR;
    ppd            = NULL;
  }

  return (ppd);
}


/*
 * '_ppdOpenFile()' - Read a PPD file into memory.
 */

ppd_file_t *				/* O - PPD file record or @code NULL@ if the PPD file could not be opened. */
_ppdOpenFile(const char		  *filename,	/* I - File to read from */
	     _ppd_localization_t  localization)	/* I - Localization to load */
{
  cups_file_t		*fp;		/* File pointer */
  ppd_file_t		*ppd;		/* PPD file record */
  _ppd_globals_t	*pg = _ppdGlobals();
					/* Global data */


 /*
  * Set the line number to 0...
  */

  pg->ppd_line = 0;

 /*
  * Range check input...
  */

  if (filename == NULL)
  {
    pg->ppd_status = PPD_NULL_FILE;

    return (NULL);
  }

 /*
  * Try to open the file and parse it...
  */

  if ((fp = cupsFileOpen(filename, "r")) != NULL)
  {
    ppd = _ppdOpen(fp, localization);

    cupsFileClose(fp);
  }
  else
  {
    pg->ppd_status = PPD_FILE_OPEN_ERROR;
    ppd            = NULL;
  }

  return (ppd);
}


/*
 * 'ppdOpenFile()' - Read a PPD file into memory.
 */

ppd_file_t *				/* O - PPD file record or @code NULL@ if the PPD file could not be opened. */
ppdOpenFile(const char *filename)	/* I - File to read from */
{
  return _ppdOpenFile(filename, _PPD_LOCALIZATION_DEFAULT);
}


/*
 * 'ppdSetConformance()' - Set the conformance level for PPD files.
 *
 * @since CUPS 1.1.20/macOS 10.4@
 */

void
ppdSetConformance(ppd_conform_t c)	/* I - Conformance level */
{
  _ppd_globals_t	*pg = _ppdGlobals();
					/* Global data */


  pg->ppd_conform = c;
}


/*
 * 'ppd_add_attr()' - Add an attribute to the PPD data.
 */

static ppd_attr_t *			/* O - New attribute */
ppd_add_attr(ppd_file_t *ppd,		/* I - PPD file data */
             const char *name,		/* I - Attribute name */
             const char *spec,		/* I - Specifier string, if any */
	     const char *text,		/* I - Text string, if any */
	     const char *value)		/* I - Value of attribute */
{
  ppd_attr_t	**ptr,			/* New array */
		*temp;			/* New attribute */


 /*
  * Range check input...
  */

  if (ppd == NULL || name == NULL || spec == NULL)
    return (NULL);

 /*
  * Create the array as needed...
  */

  if (!ppd->sorted_attrs)
    ppd->sorted_attrs = cupsArrayNew((cups_array_func_t)ppd_compare_attrs,
                                     NULL);

 /*
  * Allocate memory for the new attribute...
  */

  if (ppd->num_attrs == 0)
    ptr = malloc(sizeof(ppd_attr_t *));
  else
    ptr = realloc(ppd->attrs, (size_t)(ppd->num_attrs + 1) * sizeof(ppd_attr_t *));

  if (ptr == NULL)
    return (NULL);

  ppd->attrs = ptr;
  ptr += ppd->num_attrs;

  if ((temp = calloc(1, sizeof(ppd_attr_t))) == NULL)
    return (NULL);

  *ptr = temp;

  ppd->num_attrs ++;

 /*
  * Copy data over...
  */

  if (!_cups_strcasecmp(spec, "custom") || !_cups_strncasecmp(spec, "custom.", 7))
  {
    temp->spec[0] = '_';
    strlcpy(temp->spec + 1, spec, sizeof(temp->spec) - 1);
  }
  else {
      strlcpy(temp->spec, spec, sizeof(temp->spec));
  }

  strlcpy(temp->name, name, sizeof(temp->name));
  strlcpy(temp->text, text, sizeof(temp->text));
  temp->value = (char *)value;

 /*
  * Add the attribute to the sorted array...
  */

  cupsArrayAdd(ppd->sorted_attrs, temp);

 /*
  * Return the attribute...
  */

  return (temp);
}


/*
 * 'ppd_add_choice()' - Add a choice to an option.
 */

static ppd_choice_t *			/* O - Named choice */
ppd_add_choice(ppd_option_t *option,	/* I - Option */
               const char   *name)	/* I - Name of choice */
{
  ppd_choice_t	*choice;		/* Choice */


  if (option->num_choices == 0)
    choice = malloc(sizeof(ppd_choice_t));
  else
    choice = realloc(option->choices, sizeof(ppd_choice_t) * (size_t)(option->num_choices + 1));

  if (choice == NULL)
    return (NULL);

  option->choices = choice;
  choice += option->num_choices;
  option->num_choices ++;

  memset(choice, 0, sizeof(ppd_choice_t));
  strlcpy(choice->choice, name, sizeof(choice->choice));

  return (choice);
}


/*
 * 'ppd_add_size()' - Add a page size.
 */

static ppd_size_t *			/* O - Named size */
ppd_add_size(ppd_file_t *ppd,		/* I - PPD file */
             const char *name)		/* I - Name of size */
{
  ppd_size_t	*size;			/* Size */


  if (ppd->num_sizes == 0)
    size = malloc(sizeof(ppd_size_t));
  else
    size = realloc(ppd->sizes, sizeof(ppd_size_t) * (size_t)(ppd->num_sizes + 1));

  if (size == NULL)
    return (NULL);

  ppd->sizes = size;
  size += ppd->num_sizes;
  ppd->num_sizes ++;

  memset(size, 0, sizeof(ppd_size_t));
  strlcpy(size->name, name, sizeof(size->name));

  return (size);
}


/*
 * 'ppd_compare_attrs()' - Compare two attributes.
 */

static int				/* O - Result of comparison */
ppd_compare_attrs(ppd_attr_t *a,	/* I - First attribute */
                  ppd_attr_t *b)	/* I - Second attribute */
{
  return (_cups_strcasecmp(a->name, b->name));
}


/*
 * 'ppd_compare_choices()' - Compare two choices...
 */

static int				/* O - Result of comparison */
ppd_compare_choices(ppd_choice_t *a,	/* I - First choice */
                    ppd_choice_t *b)	/* I - Second choice */
{
  return (strcmp(a->option->keyword, b->option->keyword));
}


/*
 * 'ppd_compare_coptions()' - Compare two custom options.
 */

static int				/* O - Result of comparison */
ppd_compare_coptions(ppd_coption_t *a,	/* I - First option */
                     ppd_coption_t *b)	/* I - Second option */
{
  return (_cups_strcasecmp(a->keyword, b->keyword));
}


/*
 * 'ppd_compare_options()' - Compare two options.
 */

static int				/* O - Result of comparison */
ppd_compare_options(ppd_option_t *a,	/* I - First option */
                    ppd_option_t *b)	/* I - Second option */
{
  return (_cups_strcasecmp(a->keyword, b->keyword));
}


/*
 * 'ppd_decode()' - Decode a string value...
 */

static int				/* O - Length of decoded string */
ppd_decode(char *string)		/* I - String to decode */
{
  char	*inptr,				/* Input pointer */
	*outptr;			/* Output pointer */


  inptr  = string;
  outptr = string;

  while (*inptr != '\0')
    if (*inptr == '<' && isxdigit(inptr[1] & 255))
    {
     /*
      * Convert hex to 8-bit values...
      */

      inptr ++;
      while (isxdigit(*inptr & 255))
      {
	if (_cups_isalpha(*inptr))
	  *outptr = (char)((tolower(*inptr) - 'a' + 10) << 4);
	else
	  *outptr = (char)((*inptr - '0') << 4);

	inptr ++;

        if (!isxdigit(*inptr & 255))
	  break;

	if (_cups_isalpha(*inptr))
	  *outptr |= (char)(tolower(*inptr) - 'a' + 10);
	else
	  *outptr |= (char)(*inptr - '0');

	inptr ++;
	outptr ++;
      }

      while (*inptr != '>' && *inptr != '\0')
	inptr ++;
      while (*inptr == '>')
	inptr ++;
    }
    else
      *outptr++ = *inptr++;

  *outptr = '\0';

  return ((int)(outptr - string));
}


/*
 * 'ppd_free_filters()' - Free the filters array.
 */

static void
ppd_free_filters(ppd_file_t *ppd)	/* I - PPD file */
{
  int	i;				/* Looping var */
  char	**filter;			/* Current filter */


  if (ppd->num_filters > 0)
  {
    for (i = ppd->num_filters, filter = ppd->filters; i > 0; i --, filter ++)
      free(*filter);

    free(ppd->filters);

    ppd->num_filters = 0;
    ppd->filters     = NULL;
  }
}


/*
 * 'ppd_free_group()' - Free a single UI group.
 */

static void
ppd_free_group(ppd_group_t *group)	/* I - Group to free */
{
  int		i;			/* Looping var */
  ppd_option_t	*option;		/* Current option */
  ppd_group_t	*subgroup;		/* Current sub-group */


  if (group->num_options > 0)
  {
    for (i = group->num_options, option = group->options;
         i > 0;
	 i --, option ++)
      ppd_free_option(option);

    free(group->options);
  }

  if (group->num_subgroups > 0)
  {
    for (i = group->num_subgroups, subgroup = group->subgroups;
         i > 0;
	 i --, subgroup ++)
      ppd_free_group(subgroup);

    free(group->subgroups);
  }
}


/*
 * 'ppd_free_option()' - Free a single option.
 */

static void
ppd_free_option(ppd_option_t *option)	/* I - Option to free */
{
  int		i;			/* Looping var */
  ppd_choice_t	*choice;		/* Current choice */


  if (option->num_choices > 0)
  {
    for (i = option->num_choices, choice = option->choices;
         i > 0;
         i --, choice ++)
    {
      free(choice->code);
    }

    free(option->choices);
  }
}


/*
 * 'ppd_get_coption()' - Get a custom option record.
 */

static ppd_coption_t	*		/* O - Custom option... */
ppd_get_coption(ppd_file_t *ppd,	/* I - PPD file */
                const char *name)	/* I - Name of option */
{
  ppd_coption_t	*copt;			/* New custom option */


 /*
  * See if the option already exists...
  */

  if ((copt = ppdFindCustomOption(ppd, name)) != NULL)
    return (copt);

 /*
  * Not found, so create the custom option record...
  */

  if ((copt = calloc(1, sizeof(ppd_coption_t))) == NULL)
    return (NULL);

  strlcpy(copt->keyword, name, sizeof(copt->keyword));

  copt->params = cupsArrayNew((cups_array_func_t)NULL, NULL);

  cupsArrayAdd(ppd->coptions, copt);

 /*
  * Return the new record...
  */

  return (copt);
}


/*
 * 'ppd_get_cparam()' - Get a custom parameter record.
 */

static ppd_cparam_t *			/* O - Extended option... */
ppd_get_cparam(ppd_coption_t *opt,	/* I - PPD file */
               const char    *param,	/* I - Name of parameter */
	       const char    *text)	/* I - Human-readable text */
{
  ppd_cparam_t	*cparam;		/* New custom parameter */


 /*
  * See if the parameter already exists...
  */

  if ((cparam = ppdFindCustomParam(opt, param)) != NULL)
    return (cparam);

 /*
  * Not found, so create the custom parameter record...
  */

  if ((cparam = calloc(1, sizeof(ppd_cparam_t))) == NULL)
    return (NULL);

  cparam->type = PPD_CUSTOM_UNKNOWN;
  strlcpy(cparam->name, param, sizeof(cparam->name));
  strlcpy(cparam->text, text[0] ? text : param, sizeof(cparam->text));

 /*
  * Add this record to the array...
  */

  cupsArrayAdd(opt->params, cparam);

 /*
  * Return the new record...
  */

  return (cparam);
}


/*
 * 'ppd_get_group()' - Find or create the named group as needed.
 */

static ppd_group_t *			/* O - Named group */
ppd_get_group(ppd_file_t      *ppd,	/* I - PPD file */
              const char      *name,	/* I - Name of group */
	      const char      *text,	/* I - Text for group */
              _ppd_globals_t  *pg,	/* I - Global data */
	      cups_encoding_t encoding)	/* I - Encoding of text */
{
  int		i;			/* Looping var */
  ppd_group_t	*group;			/* Group */


  DEBUG_printf(("7ppd_get_group(ppd=%p, name=\"%s\", text=\"%s\", cg=%p)",
                ppd, name, text, pg));

  for (i = ppd->num_groups, group = ppd->groups; i > 0; i --, group ++)
    if (!strcmp(group->name, name))
      break;

  if (i == 0)
  {
    DEBUG_printf(("8ppd_get_group: Adding group %s...", name));

    if (pg->ppd_conform == PPD_CONFORM_STRICT && strlen(text) >= sizeof(group->text))
    {
      pg->ppd_status = PPD_ILLEGAL_TRANSLATION;

      return (NULL);
    }

    if (ppd->num_groups == 0)
      group = malloc(sizeof(ppd_group_t));
    else
      group = realloc(ppd->groups, (size_t)(ppd->num_groups + 1) * sizeof(ppd_group_t));

    if (group == NULL)
    {
      pg->ppd_status = PPD_ALLOC_ERROR;

      return (NULL);
    }

    ppd->groups = group;
    group += ppd->num_groups;
    ppd->num_groups ++;

    memset(group, 0, sizeof(ppd_group_t));
    strlcpy(group->name, name, sizeof(group->name));

    cupsCharsetToUTF8((cups_utf8_t *)group->text, text,
	               sizeof(group->text), encoding);
  }

  return (group);
}


/*
 * 'ppd_get_option()' - Find or create the named option as needed.
 */

static ppd_option_t *			/* O - Named option */
ppd_get_option(ppd_group_t *group,	/* I - Group */
               const char  *name)	/* I - Name of option */
{
  int		i;			/* Looping var */
  ppd_option_t	*option;		/* Option */


  DEBUG_printf(("7ppd_get_option(group=%p(\"%s\"), name=\"%s\")",
                group, group->name, name));

  for (i = group->num_options, option = group->options; i > 0; i --, option ++)
    if (!strcmp(option->keyword, name))
      break;

  if (i == 0)
  {
    if (group->num_options == 0)
      option = malloc(sizeof(ppd_option_t));
    else
      option = realloc(group->options, (size_t)(group->num_options + 1) * sizeof(ppd_option_t));

    if (option == NULL)
      return (NULL);

    group->options = option;
    option += group->num_options;
    group->num_options ++;

    memset(option, 0, sizeof(ppd_option_t));
    strlcpy(option->keyword, name, sizeof(option->keyword));
  }

  return (option);
}


/*
 * 'ppd_globals_alloc()' - Allocate and initialize global data.
 */

static _ppd_globals_t *		/* O - Pointer to global data */
ppd_globals_alloc(void)
{
  return ((_ppd_globals_t *)calloc(1, sizeof(_ppd_globals_t)));
}


/*
 * 'ppd_globals_free()' - Free global data.
 */

#if defined(HAVE_PTHREAD_H) || defined(_WIN32)
static void
ppd_globals_free(_ppd_globals_t *pg)	/* I - Pointer to global data */
{
  free(pg);
}
#endif /* HAVE_PTHREAD_H || _WIN32 */


#ifdef HAVE_PTHREAD_H
/*
 * 'ppd_globals_init()' - Initialize per-thread globals...
 */

static void
ppd_globals_init(void)
{
 /*
  * Register the global data for this thread...
  */

  pthread_key_create(&ppd_globals_key, (void (*)(void *))ppd_globals_free);
}
#endif /* HAVE_PTHREAD_H */


/*
 * 'ppd_hash_option()' - Generate a hash of the option name...
 */

static int				/* O - Hash index */
ppd_hash_option(ppd_option_t *option)	/* I - Option */
{
  int		hash = 0;		/* Hash index */
  const char	*k;			/* Pointer into keyword */


  for (hash = option->keyword[0], k = option->keyword + 1; *k;)
    hash = (int)(33U * (unsigned)hash) + *k++;

  return (hash & 511);
}


/*
 * 'ppd_read()' - Read a line from a PPD file, skipping comment lines as
 *                necessary.
 */

static int				/* O - Bitmask of fields read */
ppd_read(cups_file_t    *fp,		/* I - File to read from */
         _ppd_line_t    *line,		/* I - Line buffer */
         char           *keyword,	/* O - Keyword from line */
	 char           *option,	/* O - Option from line */
         char           *text,		/* O - Human-readable text from line */
	 char           **string,	/* O - Code/string data */
         int            ignoreblank,	/* I - Ignore blank lines? */
	 _ppd_globals_t *pg)		/* I - Global data */
{
  int		ch,			/* Character from file */
		col,			/* Column in line */
		colon,			/* Colon seen? */
		endquote,		/* Waiting for an end quote */
		mask,			/* Mask to be returned */
		startline,		/* Start line */
		textlen;		/* Length of text */
  char		*keyptr,		/* Keyword pointer */
		*optptr,		/* Option pointer */
		*textptr,		/* Text pointer */
		*strptr,		/* Pointer into string */
		*lineptr;		/* Current position in line buffer */


 /*
  * Now loop until we have a valid line...
  */

  *string   = NULL;
  col       = 0;
  startline = pg->ppd_line + 1;

  if (!line->buffer)
  {
    line->bufsize = 1024;
    line->buffer  = malloc(1024);

    if (!line->buffer)
      return (0);
  }

  do
  {
   /*
    * Read the line...
    */

    lineptr  = line->buffer;
    endquote = 0;
    colon    = 0;

    while ((ch = cupsFileGetChar(fp)) != EOF)
    {
      if (lineptr >= (line->buffer + line->bufsize - 1))
      {
       /*
        * Expand the line buffer...
	*/

        char *temp;			/* Temporary line pointer */


        line->bufsize += 1024;
	if (line->bufsize > 262144)
	{
	 /*
	  * Don't allow lines longer than 256k!
	  */

          pg->ppd_line   = startline;
          pg->ppd_status = PPD_LINE_TOO_LONG;

	  return (0);
	}

        temp = realloc(line->buffer, line->bufsize);
	if (!temp)
	{
          pg->ppd_line   = startline;
          pg->ppd_status = PPD_LINE_TOO_LONG;

	  return (0);
	}

        lineptr      = temp + (lineptr - line->buffer);
	line->buffer = temp;
      }

      if (ch == '\r' || ch == '\n')
      {
       /*
	* Line feed or carriage return...
	*/

        pg->ppd_line ++;
	col = 0;

	if (ch == '\r')
	{
	 /*
          * Check for a trailing line feed...
	  */

	  if ((ch = cupsFilePeekChar(fp)) == EOF)
	  {
	    ch = '\n';
	    break;
	  }

	  if (ch == 0x0a)
	    cupsFileGetChar(fp);
	}

	if (lineptr == line->buffer && ignoreblank)
          continue;			/* Skip blank lines */

	ch = '\n';

	if (!endquote)			/* Continue for multi-line text */
          break;

	*lineptr++ = '\n';
      }
      else if (ch < ' ' && ch != '\t' && pg->ppd_conform == PPD_CONFORM_STRICT)
      {
       /*
        * Other control characters...
	*/

        pg->ppd_line   = startline;
        pg->ppd_status = PPD_ILLEGAL_CHARACTER;

        return (0);
      }
      else if (ch != 0x1a)
      {
       /*
	* Any other character...
	*/

	*lineptr++ = (char)ch;
	col ++;

	if (col > (PPD_MAX_LINE - 1))
	{
	 /*
          * Line is too long...
	  */

          pg->ppd_line   = startline;
          pg->ppd_status = PPD_LINE_TOO_LONG;

          return (0);
	}

	if (ch == ':' && strncmp(line->buffer, "*%", 2) != 0)
	  colon = 1;

	if (ch == '\"' && colon)
	  endquote = !endquote;
      }
    }

    if (endquote)
    {
     /*
      * Didn't finish this quoted string...
      */

      while ((ch = cupsFileGetChar(fp)) != EOF)
        if (ch == '\"')
	  break;
	else if (ch == '\r' || ch == '\n')
	{
	  pg->ppd_line ++;
	  col = 0;

	  if (ch == '\r')
	  {
	   /*
            * Check for a trailing line feed...
	    */

	    if ((ch = cupsFilePeekChar(fp)) == EOF)
	      break;
	    if (ch == 0x0a)
	      cupsFileGetChar(fp);
	  }
	}
	else if (ch < ' ' && ch != '\t' && pg->ppd_conform == PPD_CONFORM_STRICT)
	{
	 /*
          * Other control characters...
	  */

          pg->ppd_line   = startline;
          pg->ppd_status = PPD_ILLEGAL_CHARACTER;

          return (0);
	}
	else if (ch != 0x1a)
	{
	  col ++;

	  if (col > (PPD_MAX_LINE - 1))
	  {
	   /*
            * Line is too long...
	    */

            pg->ppd_line   = startline;
            pg->ppd_status = PPD_LINE_TOO_LONG;

            return (0);
	  }
	}
    }

    if (ch != '\n')
    {
     /*
      * Didn't finish this line...
      */

      while ((ch = cupsFileGetChar(fp)) != EOF)
	if (ch == '\r' || ch == '\n')
	{
	 /*
	  * Line feed or carriage return...
	  */

          pg->ppd_line ++;
	  col = 0;

	  if (ch == '\r')
	  {
	   /*
            * Check for a trailing line feed...
	    */

	    if ((ch = cupsFilePeekChar(fp)) == EOF)
	      break;
	    if (ch == 0x0a)
	      cupsFileGetChar(fp);
	  }

	  break;
	}
	else if (ch < ' ' && ch != '\t' && pg->ppd_conform == PPD_CONFORM_STRICT)
	{
	 /*
          * Other control characters...
	  */

          pg->ppd_line   = startline;
          pg->ppd_status = PPD_ILLEGAL_CHARACTER;

          return (0);
	}
	else if (ch != 0x1a)
	{
	  col ++;

	  if (col > (PPD_MAX_LINE - 1))
	  {
	   /*
            * Line is too long...
	    */

            pg->ppd_line   = startline;
            pg->ppd_status = PPD_LINE_TOO_LONG;

            return (0);
	  }
	}
    }

    if (lineptr > line->buffer && lineptr[-1] == '\n')
      lineptr --;

    *lineptr = '\0';

    DEBUG_printf(("9ppd_read: LINE=\"%s\"", line->buffer));

   /*
    * The dynamically created PPDs for older style macOS
    * drivers include a large blob of data inserted as comments
    * at the end of the file.  As an optimization we can stop
    * reading the PPD when we get to the start of this data.
    */

    if (!strcmp(line->buffer, "*%APLWORKSET START"))
      return (0);

    if (ch == EOF && lineptr == line->buffer)
      return (0);

   /*
    * Now parse it...
    */

    mask    = 0;
    lineptr = line->buffer + 1;

    keyword[0] = '\0';
    option[0]  = '\0';
    text[0]    = '\0';
    *string    = NULL;

    if ((!line->buffer[0] ||		/* Blank line */
         !strncmp(line->buffer, "*%", 2) || /* Comment line */
         !strcmp(line->buffer, "*End")) && /* End of multi-line string */
        ignoreblank)			/* Ignore these? */
    {
      startline = pg->ppd_line + 1;
      continue;
    }

    if (!strcmp(line->buffer, "*"))	/* (Bad) comment line */
    {
      if (pg->ppd_conform == PPD_CONFORM_RELAXED)
      {
	startline = pg->ppd_line + 1;
	continue;
      }
      else
      {
        pg->ppd_line   = startline;
        pg->ppd_status = PPD_ILLEGAL_MAIN_KEYWORD;

        return (0);
      }
    }

    if (line->buffer[0] != '*')		/* All lines start with an asterisk */
    {
     /*
      * Allow lines consisting of just whitespace...
      */

      for (lineptr = line->buffer; *lineptr; lineptr ++)
        if (*lineptr && !_cups_isspace(*lineptr))
	  break;

      if (*lineptr)
      {
        pg->ppd_status = PPD_MISSING_ASTERISK;
        return (0);
      }
      else if (ignoreblank)
        continue;
      else
        return (0);
    }

   /*
    * Get a keyword...
    */

    keyptr = keyword;

    while (*lineptr && *lineptr != ':' && !_cups_isspace(*lineptr))
    {
      if (*lineptr <= ' ' || *lineptr > 126 || *lineptr == '/' ||
          (keyptr - keyword) >= (PPD_MAX_NAME - 1))
      {
        pg->ppd_status = PPD_ILLEGAL_MAIN_KEYWORD;
	return (0);
      }

      *keyptr++ = *lineptr++;
    }

    *keyptr = '\0';

    if (!strcmp(keyword, "End"))
      continue;

    mask |= PPD_KEYWORD;

    if (_cups_isspace(*lineptr))
    {
     /*
      * Get an option name...
      */

      while (_cups_isspace(*lineptr))
        lineptr ++;

      optptr = option;

      while (*lineptr && !_cups_isspace(*lineptr) && *lineptr != ':' &&
             *lineptr != '/')
      {
	if (*lineptr <= ' ' || *lineptr > 126 ||
	    (optptr - option) >= (PPD_MAX_NAME - 1))
        {
          pg->ppd_status = PPD_ILLEGAL_OPTION_KEYWORD;
	  return (0);
	}

        *optptr++ = *lineptr++;
      }

      *optptr = '\0';

      if (_cups_isspace(*lineptr) && pg->ppd_conform == PPD_CONFORM_STRICT)
      {
        pg->ppd_status = PPD_ILLEGAL_WHITESPACE;
	return (0);
      }

      while (_cups_isspace(*lineptr))
	lineptr ++;

      mask |= PPD_OPTION;

      if (*lineptr == '/')
      {
       /*
        * Get human-readable text...
	*/

        lineptr ++;

	textptr = text;

	while (*lineptr != '\0' && *lineptr != '\n' && *lineptr != ':')
	{
	  if (((unsigned char)*lineptr < ' ' && *lineptr != '\t') ||
	      (textptr - text) >= (PPD_MAX_LINE - 1))
	  {
	    pg->ppd_status = PPD_ILLEGAL_TRANSLATION;
	    return (0);
	  }

	  *textptr++ = *lineptr++;
        }

	*textptr = '\0';
	textlen  = ppd_decode(text);

	if (textlen > PPD_MAX_TEXT && pg->ppd_conform == PPD_CONFORM_STRICT)
	{
	  pg->ppd_status = PPD_ILLEGAL_TRANSLATION;
	  return (0);
	}

	mask |= PPD_TEXT;
      }
    }

    if (_cups_isspace(*lineptr) && pg->ppd_conform == PPD_CONFORM_STRICT)
    {
      pg->ppd_status = PPD_ILLEGAL_WHITESPACE;
      return (0);
    }

    while (_cups_isspace(*lineptr))
      lineptr ++;

    if (*lineptr == ':')
    {
     /*
      * Get string after trimming leading and trailing whitespace...
      */

      lineptr ++;
      while (_cups_isspace(*lineptr))
        lineptr ++;

      strptr = lineptr + strlen(lineptr) - 1;
      while (strptr >= lineptr && _cups_isspace(*strptr))
        *strptr-- = '\0';

      if (*strptr == '\"')
      {
       /*
        * Quoted string by itself, remove quotes...
	*/

        *strptr = '\0';
	lineptr ++;
      }

      *string = strdup(lineptr);

      mask |= PPD_STRING;
    }
  }
  while (mask == 0);

  return (mask);
}


/*
 * 'ppd_update_filters()' - Update the filters array as needed.
 *
 * This function re-populates the filters array with cupsFilter2 entries that
 * have been stripped of the destination MIME media types and any maxsize hints.
 *
 * (All for backwards-compatibility)
 */

static int				/* O - 1 on success, 0 on failure */
ppd_update_filters(ppd_file_t     *ppd,	/* I - PPD file */
                   _ppd_globals_t *pg)	/* I - Global data */
{
  ppd_attr_t	*attr;			/* Current cupsFilter2 value */
  char		srcsuper[16],		/* Source MIME media type */
		srctype[256],
		dstsuper[16],		/* Destination MIME media type */
		dsttype[256],
		*ptr,			/* Pointer into command to run */
		buffer[1024],		/* Re-written cupsFilter value */
		**filter;		/* Current filter */
  int		cost;			/* Cost of filter */

  char		program[1024] = { 0 };	/* Command to run */

  DEBUG_printf(("4ppd_update_filters(ppd=%p, cg=%p)", ppd, pg));

 /*
  * See if we have any cupsFilter2 lines...
  */

  if ((attr = ppdFindAttr(ppd, "cupsFilter2", NULL)) == NULL)
  {
    DEBUG_puts("5ppd_update_filters: No cupsFilter2 keywords present.");
    return (1);
  }

 /*
  * Yes, free the cupsFilter-defined filters and re-build...
  */

  ppd_free_filters(ppd);

  do
  {
   /*
    * Parse the cupsFilter2 string:
    *
    *   src/type dst/type cost program
    *   src/type dst/type cost maxsize(n) program
    */

    DEBUG_printf(("5ppd_update_filters: cupsFilter2=\"%s\"", attr->value));

    if (sscanf(attr->value, "%15[^/]/%255s%*[ \t]%15[^/]/%255s%d%*[ \t]%1023[^\n]",
	       srcsuper, srctype, dstsuper, dsttype, &cost, program) != 6)
    {
      DEBUG_puts("5ppd_update_filters: Bad cupsFilter2 line.");
      pg->ppd_status = PPD_BAD_VALUE;

      return (0);
    }

    DEBUG_printf(("5ppd_update_filters: srcsuper=\"%s\", srctype=\"%s\", "
                  "dstsuper=\"%s\", dsttype=\"%s\", cost=%d, program=\"%s\"",
		  srcsuper, srctype, dstsuper, dsttype, cost, program));

    if (!strncmp(program, "maxsize(", 8) &&
        (ptr = strchr(program + 8, ')')) != NULL)
    {
      DEBUG_puts("5ppd_update_filters: Found maxsize(nnn).");

      ptr ++;
      while (_cups_isspace(*ptr))
	ptr ++;

      _cups_strcpy(program, ptr);
      DEBUG_printf(("5ppd_update_filters: New program=\"%s\"", program));
    }

   /*
    * Convert to cupsFilter format:
    *
    *   src/type cost program
    */

    snprintf(buffer, sizeof(buffer), "%s/%s %d %s", srcsuper, srctype, cost,
             program);
    DEBUG_printf(("5ppd_update_filters: Adding \"%s\".", buffer));

   /*
    * Add a cupsFilter-compatible string to the filters array.
    */

    if (ppd->num_filters == 0)
      filter = malloc(sizeof(char *));
    else
      filter = realloc(ppd->filters, sizeof(char *) * (size_t)(ppd->num_filters + 1));

    if (filter == NULL)
    {
      DEBUG_puts("5ppd_update_filters: Out of memory.");
      pg->ppd_status = PPD_ALLOC_ERROR;

      return (0);
    }

    ppd->filters     = filter;
    filter           += ppd->num_filters;
    ppd->num_filters ++;

    *filter = strdup(buffer);
  }
  while ((attr = ppdFindNextAttr(ppd, "cupsFilter2", NULL)) != NULL);

  DEBUG_puts("5ppd_update_filters: Completed OK.");
  return (1);
}
