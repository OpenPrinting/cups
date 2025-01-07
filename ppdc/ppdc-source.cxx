//
// Source class for the CUPS PPD Compiler.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright 2007-2018 by Apple Inc.
// Copyright 2002-2007 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

//
// Include necessary headers...
//

#include "ppdc-private.h"
#include <limits.h>
#include <math.h>
#include <unistd.h>
#include <cups/raster.h>
#include "data/epson.h"
#include "data/hp.h"
#include "data/label.h"
#ifndef _WIN32
#  include <sys/utsname.h>
#endif // !_WIN32


//
// Class globals...
//

ppdcArray	*ppdcSource::includes = 0;
const char	*ppdcSource::driver_types[] =
		{
		  "custom",
		  "ps",
		  "escp",
		  "pcl",
		  "label",
		  "epson",
		  "hp"
		};


//
// 'ppdcSource::ppdcSource()' - Load a driver source file.
//

ppdcSource::ppdcSource(const char  *f,	// I - File to read
                       cups_file_t *ffp)// I - File pointer to use
  : ppdcShared()
{
  PPDC_NEW;

  filename      = new ppdcString(f);
  base_fonts    = new ppdcArray();
  drivers       = new ppdcArray();
  po_files      = new ppdcArray();
  sizes         = new ppdcArray();
  vars          = new ppdcArray();
  cond_state    = PPDC_COND_NORMAL;
  cond_current  = cond_stack;
  cond_stack[0] = PPDC_COND_NORMAL;

  // Add standard #define variables...
#define MAKE_STRING(x) #x

  vars->add(new ppdcVariable("CUPS_VERSION", MAKE_STRING(CUPS_VERSION)));
  vars->add(new ppdcVariable("CUPS_VERSION_MAJOR", MAKE_STRING(CUPS_VERSION_MAJOR)));
  vars->add(new ppdcVariable("CUPS_VERSION_MINOR", MAKE_STRING(CUPS_VERSION_MINOR)));
  vars->add(new ppdcVariable("CUPS_VERSION_PATCH", MAKE_STRING(CUPS_VERSION_PATCH)));

#ifdef _WIN32
  vars->add(new ppdcVariable("PLATFORM_NAME", "Windows"));
  vars->add(new ppdcVariable("PLATFORM_ARCH", "X86"));

#else
  struct utsname name;			// uname information

  if (!uname(&name))
  {
    vars->add(new ppdcVariable("PLATFORM_NAME", name.sysname));
    vars->add(new ppdcVariable("PLATFORM_ARCH", name.machine));
  }
  else
  {
    vars->add(new ppdcVariable("PLATFORM_NAME", "unknown"));
    vars->add(new ppdcVariable("PLATFORM_ARCH", "unknown"));
  }
#endif // _WIN32

  if (f)
    read_file(f, ffp);
}


//
// 'ppdcSource::~ppdcSource()' - Free a driver source file.
//

ppdcSource::~ppdcSource()
{
  PPDC_DELETE;

  filename->release();
  base_fonts->release();
  drivers->release();
  po_files->release();
  sizes->release();
  vars->release();
}


//
// 'ppdcSource::add_include()' - Add an include directory.
//

void
ppdcSource::add_include(const char *d)	// I - Include directory
{
  if (!d)
    return;

  if (!includes)
    includes = new ppdcArray();

  includes->add(new ppdcString(d));
}


//
// 'ppdcSource::find_driver()' - Find a driver.
//

ppdcDriver *				// O - Driver
ppdcSource::find_driver(const char *f)	// I - Driver file name
{
  ppdcDriver	*d;			// Current driver


  for (d = (ppdcDriver *)drivers->first(); d; d = (ppdcDriver *)drivers->next())
    if (!_cups_strcasecmp(f, d->pc_file_name->value))
      return (d);

  return (NULL);
}


//
// 'ppdcSource::find_include()' - Find an include file.
//

char *					// O - Found path or NULL
ppdcSource::find_include(
    const char *f,			// I - Include filename
    const char *base,			// I - Current directory
    char       *n,			// I - Path buffer
    int        nlen)			// I - Path buffer length
{
  ppdcString	*dir;			// Include directory
  char		temp[1024],		// Temporary path
		*ptr;			// Pointer to end of path


  // Range check input...
  if (!f || !*f || !n || nlen < 2)
    return (0);

  // Check the first character to see if we have <name> or "name"...
  if (*f == '<')
  {
    // Remove the surrounding <> from the name...
    strlcpy(temp, f + 1, sizeof(temp));
    ptr = temp + strlen(temp) - 1;

    if (*ptr != '>')
    {
      _cupsLangPrintf(stderr,
                      _("ppdc: Invalid #include/#po filename \"%s\"."), n);
      return (0);
    }

    *ptr = '\0';
    f    = temp;
  }
  else
  {
    // Check for the local file relative to the current directory...
    if (base && *base && f[0] != '/')
      snprintf(n, (size_t)nlen, "%s/%s", base, f);
    else
      strlcpy(n, f, (size_t)nlen);

    if (!access(n, 0))
      return (n);
    else if (*f == '/')
    {
      // Absolute path that doesn't exist...
      return (0);
    }
  }

  // Search the include directories, if any...
  if (includes)
  {
    for (dir = (ppdcString *)includes->first(); dir; dir = (ppdcString *)includes->next())
    {
      snprintf(n, (size_t)nlen, "%s/%s", dir->value, f);
      if (!access(n, 0))
        return (n);
    }
  }

  // Search the standard include directories...
  _cups_globals_t *cg = _cupsGlobals();	// Global data

  snprintf(n, (size_t)nlen, "%s/ppdc/%s", cg->cups_datadir, f);
  if (!access(n, 0))
    return (n);

  snprintf(n, (size_t)nlen, "%s/po/%s", cg->cups_datadir, f);
  if (!access(n, 0))
    return (n);
  else
    return (0);
}


//
// 'ppdcSource::find_po()' - Find a message catalog for the given locale.
//

ppdcCatalog *				// O - Message catalog or NULL
ppdcSource::find_po(const char *l)	// I - Locale name
{
  ppdcCatalog	*cat;			// Current message catalog


  for (cat = (ppdcCatalog *)po_files->first();
       cat;
       cat = (ppdcCatalog *)po_files->next())
    if (!_cups_strcasecmp(l, cat->locale->value))
      return (cat);

  return (NULL);
}


//
// 'ppdcSource::find_size()' - Find a media size.
//

ppdcMediaSize *				// O - Size
ppdcSource::find_size(const char *s)	// I - Size name
{
  ppdcMediaSize	*m;			// Current media size


  for (m = (ppdcMediaSize *)sizes->first(); m; m = (ppdcMediaSize *)sizes->next())
    if (!_cups_strcasecmp(s, m->name->value))
      return (m);

  return (NULL);
}


//
// 'ppdcSource::find_variable()' - Find a variable.
//

ppdcVariable *				// O - Variable
ppdcSource::find_variable(const char *n)// I - Variable name
{
  ppdcVariable	*v;			// Current variable


  for (v = (ppdcVariable *)vars->first(); v; v = (ppdcVariable *)vars->next())
    if (!_cups_strcasecmp(n, v->name->value))
      return (v);

  return (NULL);
}


//
// 'ppdcSource::get_attr()' - Get an attribute.
//

ppdcAttr *				// O - Attribute
ppdcSource::get_attr(ppdcFile *fp, 	// I - File to read
                     bool     loc)	// I - Localize this attribute?
{
  char	name[1024],			// Name string
	selector[1024],			// Selector string
	*text,				// Text string
	value[1024];			// Value string


  // Get the attribute parameters:
  //
  // Attribute name selector value
  if (!get_token(fp, name, sizeof(name)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected name after %s on line %d of %s."),
		    loc ? "LocAttribute" : "Attribute", fp->line, fp->filename);
    return (0);
  }

  if (!get_token(fp, selector, sizeof(selector)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected selector after %s on line %d of %s."),
		    loc ? "LocAttribute" : "Attribute", fp->line, fp->filename);
    return (0);
  }

  if ((text = strchr(selector, '/')) != NULL)
    *text++ = '\0';

  if (!get_token(fp, value, sizeof(value)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected value after %s on line %d of %s."),
		    loc ? "LocAttribute" : "Attribute", fp->line, fp->filename);
    return (0);
  }

  return (new ppdcAttr(name, selector, text, value, loc));
}


//
// 'ppdcSource::get_boolean()' - Get a boolean value.
//

int					// O - Boolean value
ppdcSource::get_boolean(ppdcFile *fp)	// I - File to read
{
  char	buffer[256];			// String buffer


  if (!get_token(fp, buffer, sizeof(buffer)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected boolean value on line %d of %s."),
		    fp->line, fp->filename);
    return (-1);
  }

  if (!_cups_strcasecmp(buffer, "on") ||
      !_cups_strcasecmp(buffer, "yes") ||
      !_cups_strcasecmp(buffer, "true"))
    return (1);
  else if (!_cups_strcasecmp(buffer, "off") ||
	   !_cups_strcasecmp(buffer, "no") ||
	   !_cups_strcasecmp(buffer, "false"))
    return (0);
  else
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Bad boolean value (%s) on line %d of %s."),
		    buffer, fp->line, fp->filename);
    return (-1);
  }
}


//
// 'ppdcSource::get_choice()' - Get a choice.
//

ppdcChoice *				// O - Choice data
ppdcSource::get_choice(ppdcFile *fp)	// I - File to read
{
  char	name[1024],			// Name
	*text,				// Text
	code[10240];			// Code


  // Read a choice from the file:
  //
  // Choice name/text code
  if (!get_token(fp, name, sizeof(name)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected choice name/text on line %d of %s."),
		    fp->line, fp->filename);
    return (NULL);
  }

  if ((text = strchr(name, '/')) != NULL)
    *text++ = '\0';
  else
    text = name;

  if (!get_token(fp, code, sizeof(code)))
  {
    _cupsLangPrintf(stderr, _("ppdc: Expected choice code on line %d of %s."),
		    fp->line, fp->filename);
    return (NULL);
  }

  // Return the new choice
  return (new ppdcChoice(name, text, code));
}


//
// 'ppdcSource::get_color_model()' - Get an old-style color model option.
//

ppdcChoice *				// O - Choice data
ppdcSource::get_color_model(ppdcFile *fp)
					// I - File to read
{
  char		name[1024],		// Option name
		*text,			// Text option
		temp[256];		// Temporary string
  int		color_space,		// Colorspace
		color_order,		// Color order
		compression;		// Compression mode


  // Get the ColorModel parameters:
  //
  // ColorModel name/text colorspace colororder compression
  if (!get_token(fp, name, sizeof(name)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected name/text combination for ColorModel on "
		      "line %d of %s."), fp->line, fp->filename);
    return (NULL);
  }

  if ((text = strchr(name, '/')) != NULL)
    *text++ = '\0';
  else
    text = name;

  if (!get_token(fp, temp, sizeof(temp)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected colorspace for ColorModel on line %d of "
		      "%s."), fp->line, fp->filename);
    return (NULL);
  }

  if ((color_space = get_color_space(temp)) < 0)
    color_space = get_integer(temp);

  if (!get_token(fp, temp, sizeof(temp)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected color order for ColorModel on line %d of "
		      "%s."), fp->line, fp->filename);
    return (NULL);
  }

  if ((color_order = get_color_order(temp)) < 0)
    color_order = get_integer(temp);

  if (!get_token(fp, temp, sizeof(temp)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected compression for ColorModel on line %d of "
		      "%s."), fp->line, fp->filename);
    return (NULL);
  }

  compression = get_integer(temp);

  snprintf(temp, sizeof(temp),
           "<</cupsColorSpace %d/cupsColorOrder %d/cupsCompression %d>>"
	   "setpagedevice",
           color_space, color_order, compression);

  return (new ppdcChoice(name, text, temp));
}


//
// 'ppdcSource::get_color_order()' - Get an old-style color order value.
//

int					// O - Color order value
ppdcSource::get_color_order(
    const char *co)			// I - Color order string
{
  if (!_cups_strcasecmp(co, "chunked") ||
      !_cups_strcasecmp(co, "chunky"))
    return (CUPS_ORDER_CHUNKED);
  else if (!_cups_strcasecmp(co, "banded"))
    return (CUPS_ORDER_BANDED);
  else if (!_cups_strcasecmp(co, "planar"))
    return (CUPS_ORDER_PLANAR);
  else
    return (-1);
}


//
// 'ppdcSource::get_color_profile()' - Get a color profile definition.
//

ppdcProfile *				// O - Color profile
ppdcSource::get_color_profile(
    ppdcFile *fp)			// I - File to read
{
  char		resolution[1024],	// Resolution/media type
		*media_type;		// Media type
  int		i;			// Looping var
  float		g,			// Gamma value
		d,			// Density value
		m[9];			// Transform matrix


  // Get the ColorProfile parameters:
  //
  // ColorProfile resolution/mediatype gamma density m00 m01 m02 ... m22
  if (!get_token(fp, resolution, sizeof(resolution)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected resolution/mediatype following "
		      "ColorProfile on line %d of %s."),
		    fp->line, fp->filename);
    return (NULL);
  }

  if ((media_type = strchr(resolution, '/')) != NULL)
    *media_type++ = '\0';
  else
    media_type = resolution;

  g = get_float(fp);
  d = get_float(fp);
  for (i = 0; i < 9; i ++)
    m[i] = get_float(fp);

  return (new ppdcProfile(resolution, media_type, d, g, m));
}


//
// 'ppdcSource::get_color_space()' - Get an old-style colorspace value.
//

int					// O - Colorspace value
ppdcSource::get_color_space(
    const char *cs)			// I - Colorspace string
{
  if (!_cups_strcasecmp(cs, "w"))
    return (CUPS_CSPACE_W);
  else if (!_cups_strcasecmp(cs, "rgb"))
    return (CUPS_CSPACE_RGB);
  else if (!_cups_strcasecmp(cs, "rgba"))
    return (CUPS_CSPACE_RGBA);
  else if (!_cups_strcasecmp(cs, "k"))
    return (CUPS_CSPACE_K);
  else if (!_cups_strcasecmp(cs, "cmy"))
    return (CUPS_CSPACE_CMY);
  else if (!_cups_strcasecmp(cs, "ymc"))
    return (CUPS_CSPACE_YMC);
  else if (!_cups_strcasecmp(cs, "cmyk"))
    return (CUPS_CSPACE_CMYK);
  else if (!_cups_strcasecmp(cs, "ymck"))
    return (CUPS_CSPACE_YMCK);
  else if (!_cups_strcasecmp(cs, "kcmy"))
    return (CUPS_CSPACE_KCMY);
  else if (!_cups_strcasecmp(cs, "kcmycm"))
    return (CUPS_CSPACE_KCMYcm);
  else if (!_cups_strcasecmp(cs, "gmck"))
    return (CUPS_CSPACE_GMCK);
  else if (!_cups_strcasecmp(cs, "gmcs"))
    return (CUPS_CSPACE_GMCS);
  else if (!_cups_strcasecmp(cs, "white"))
    return (CUPS_CSPACE_WHITE);
  else if (!_cups_strcasecmp(cs, "gold"))
    return (CUPS_CSPACE_GOLD);
  else if (!_cups_strcasecmp(cs, "silver"))
    return (CUPS_CSPACE_SILVER);
  else if (!_cups_strcasecmp(cs, "CIEXYZ"))
    return (CUPS_CSPACE_CIEXYZ);
  else if (!_cups_strcasecmp(cs, "CIELab"))
    return (CUPS_CSPACE_CIELab);
  else if (!_cups_strcasecmp(cs, "RGBW"))
    return (CUPS_CSPACE_RGBW);
  else if (!_cups_strcasecmp(cs, "ICC1"))
    return (CUPS_CSPACE_ICC1);
  else if (!_cups_strcasecmp(cs, "ICC2"))
    return (CUPS_CSPACE_ICC2);
  else if (!_cups_strcasecmp(cs, "ICC3"))
    return (CUPS_CSPACE_ICC3);
  else if (!_cups_strcasecmp(cs, "ICC4"))
    return (CUPS_CSPACE_ICC4);
  else if (!_cups_strcasecmp(cs, "ICC5"))
    return (CUPS_CSPACE_ICC5);
  else if (!_cups_strcasecmp(cs, "ICC6"))
    return (CUPS_CSPACE_ICC6);
  else if (!_cups_strcasecmp(cs, "ICC7"))
    return (CUPS_CSPACE_ICC7);
  else if (!_cups_strcasecmp(cs, "ICC8"))
    return (CUPS_CSPACE_ICC8);
  else if (!_cups_strcasecmp(cs, "ICC9"))
    return (CUPS_CSPACE_ICC9);
  else if (!_cups_strcasecmp(cs, "ICCA"))
    return (CUPS_CSPACE_ICCA);
  else if (!_cups_strcasecmp(cs, "ICCB"))
    return (CUPS_CSPACE_ICCB);
  else if (!_cups_strcasecmp(cs, "ICCC"))
    return (CUPS_CSPACE_ICCC);
  else if (!_cups_strcasecmp(cs, "ICCD"))
    return (CUPS_CSPACE_ICCD);
  else if (!_cups_strcasecmp(cs, "ICCE"))
    return (CUPS_CSPACE_ICCE);
  else if (!_cups_strcasecmp(cs, "ICCF"))
    return (CUPS_CSPACE_ICCF);
  else
    return (-1);
}


//
// 'ppdcSource::get_constraint()' - Get a constraint.
//

ppdcConstraint *			// O - Constraint
ppdcSource::get_constraint(ppdcFile *fp)// I - File to read
{
  char		temp[1024],		// One string to rule them all
		*ptr,			// Pointer into string
		*option1,		// Constraint option 1
		*choice1,		// Constraint choice 1
		*option2,		// Constraint option 2
		*choice2;		// Constraint choice 2


  // Read the UIConstaints parameter in one of the following forms:
  //
  // UIConstraints "*Option1 *Option2"
  // UIConstraints "*Option1 Choice1 *Option2"
  // UIConstraints "*Option1 *Option2 Choice2"
  // UIConstraints "*Option1 Choice1 *Option2 Choice2"
  if (!get_token(fp, temp, sizeof(temp)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected constraints string for UIConstraints on "
		      "line %d of %s."), fp->line, fp->filename);
    return (NULL);
  }

  for (ptr = temp; isspace(*ptr); ptr ++);

  if (*ptr != '*')
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Option constraint must *name on line %d of %s."),
		    fp->line, fp->filename);
    return (NULL);
  }

  option1 = ptr;

  for (; *ptr && !isspace(*ptr); ptr ++);
  for (; isspace(*ptr); *ptr++ = '\0');

  if (*ptr != '*')
  {
    choice1 = ptr;

    for (; *ptr && !isspace(*ptr); ptr ++);
    for (; isspace(*ptr); *ptr++ = '\0');
  }
  else
    choice1 = NULL;

  if (*ptr != '*')
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected two option names on line %d of %s."),
		    fp->line, fp->filename);
    return (NULL);
  }

  option2 = ptr;

  for (; *ptr && !isspace(*ptr); ptr ++);
  for (; isspace(*ptr); *ptr++ = '\0');

  if (*ptr)
    choice2 = ptr;
  else
    choice2 = NULL;

  return (new ppdcConstraint(option1, choice1, option2, choice2));
}


//
// 'ppdcSource::get_custom_size()' - Get a custom media size definition from a file.
//

ppdcMediaSize *				// O - Media size
ppdcSource::get_custom_size(ppdcFile *fp)
					// I - File to read
{
  char		name[1024],		// Name
		*text,			// Text
		size_code[10240],	// PageSize code
		region_code[10240];	// PageRegion
  float		width,			// Width
		length,			// Length
		left,			// Left margin
		bottom,			// Bottom margin
		right,			// Right margin
		top;			// Top margin


  // Get the name, text, width, length, margins, and code:
  //
  // CustomMedia name/text width length left bottom right top size-code region-code
  if (!get_token(fp, name, sizeof(name)))
    return (NULL);

  if ((text = strchr(name, '/')) != NULL)
    *text++ = '\0';
  else
    text = name;

  if ((width = get_measurement(fp)) < 0.0f)
    return (NULL);

  if ((length = get_measurement(fp)) < 0.0f)
    return (NULL);

  if ((left = get_measurement(fp)) < 0.0f)
    return (NULL);

  if ((bottom = get_measurement(fp)) < 0.0f)
    return (NULL);

  if ((right = get_measurement(fp)) < 0.0f)
    return (NULL);

  if ((top = get_measurement(fp)) < 0.0f)
    return (NULL);

  if (!get_token(fp, size_code, sizeof(size_code)))
    return (NULL);

  if (!get_token(fp, region_code, sizeof(region_code)))
    return (NULL);

  // Return the new media size...
  return (new ppdcMediaSize(name, text, width, length, left, bottom,
                            right, top, size_code, region_code));
}


//
// 'ppdcSource::get_duplex()' - Get a duplex option.
//

void
ppdcSource::get_duplex(ppdcFile   *fp,	// I - File to read from
                       ppdcDriver *d)	// I - Current driver
{
  char		temp[256];		// Duplex keyword
  ppdcAttr	*attr;			// cupsFlipDuplex attribute
  ppdcGroup	*g;			// Current group
  ppdcOption	*o;			// Duplex option


  // Duplex {boolean|none|normal|flip}
  if (!get_token(fp, temp, sizeof(temp)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected duplex type after Duplex on line %d of "
		      "%s."), fp->line, fp->filename);
    return;
  }

  if (cond_state)
    return;

  if (!_cups_strcasecmp(temp, "none") || !_cups_strcasecmp(temp, "false") ||
      !_cups_strcasecmp(temp, "no") || !_cups_strcasecmp(temp, "off"))
  {
    g = d->find_group("General");
    if ((o = g->find_option("Duplex")) != NULL)
      g->options->remove(o);

    for (attr = (ppdcAttr *)d->attrs->first();
         attr;
	 attr = (ppdcAttr *)d->attrs->next())
      if (!strcmp(attr->name->value, "cupsFlipDuplex"))
      {
        d->attrs->remove(attr);
	break;
      }
  }
  else if (!_cups_strcasecmp(temp, "normal") || !_cups_strcasecmp(temp, "true") ||
	   !_cups_strcasecmp(temp, "yes") || !_cups_strcasecmp(temp, "on") ||
	   !_cups_strcasecmp(temp, "flip") || !_cups_strcasecmp(temp, "rotated") ||
	   !_cups_strcasecmp(temp, "manualtumble"))
  {
    g = d->find_group("General");
    o = g->find_option("Duplex");

    if (!o)
    {
      o = new ppdcOption(PPDC_PICKONE, "Duplex", "2-Sided Printing",
                	 !_cups_strcasecmp(temp, "flip") ? PPDC_SECTION_PAGE :
			                             PPDC_SECTION_ANY, 10.0f);
      o->add_choice(new ppdcChoice("None", "Off (1-Sided)",
                        	   "<</Duplex false>>setpagedevice"));
      o->add_choice(new ppdcChoice("DuplexNoTumble", "Long-Edge (Portrait)",
                                   "<</Duplex true/Tumble false>>setpagedevice"));
      o->add_choice(new ppdcChoice("DuplexTumble", "Short-Edge (Landscape)",
                                   "<</Duplex true/Tumble true>>setpagedevice"));

      g->add_option(o);
    }

    for (attr = (ppdcAttr *)d->attrs->first();
         attr;
	 attr = (ppdcAttr *)d->attrs->next())
      if (!strcmp(attr->name->value, "cupsFlipDuplex"))
      {
        if (_cups_strcasecmp(temp, "flip"))
          d->attrs->remove(attr);
	break;
      }

    if (!_cups_strcasecmp(temp, "flip") && !attr)
      d->add_attr(new ppdcAttr("cupsFlipDuplex", NULL, NULL, "true"));

    for (attr = (ppdcAttr *)d->attrs->first();
         attr;
	 attr = (ppdcAttr *)d->attrs->next())
      if (!strcmp(attr->name->value, "cupsBackSide"))
      {
        d->attrs->remove(attr);
	break;
      }

    if (!_cups_strcasecmp(temp, "flip"))
      d->add_attr(new ppdcAttr("cupsBackSide", NULL, NULL, "Flipped"));
    else if (!_cups_strcasecmp(temp, "rotated"))
      d->add_attr(new ppdcAttr("cupsBackSide", NULL, NULL, "Rotated"));
    else if (!_cups_strcasecmp(temp, "manualtumble"))
      d->add_attr(new ppdcAttr("cupsBackSide", NULL, NULL, "ManualTumble"));
    else
      d->add_attr(new ppdcAttr("cupsBackSide", NULL, NULL, "Normal"));
  }
  else
    _cupsLangPrintf(stderr,
                    _("ppdc: Unknown duplex type \"%s\" on line %d of %s."),
		    temp, fp->line, fp->filename);
}


//
// 'ppdcSource::get_filter()' - Get a filter.
//

ppdcFilter *				// O - Filter
ppdcSource::get_filter(ppdcFile *fp)	// I - File to read
{
  char	type[1024],			// MIME type
	program[1024],			// Filter program
	*ptr;				// Pointer into MIME type
  int	cost;				// Relative cost


  // Read filter parameters in one of the following formats:
  //
  // Filter "type cost program"
  // Filter type cost program

  if (!get_token(fp, type, sizeof(type)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected a filter definition on line %d of %s."),
		    fp->line, fp->filename);
    return (NULL);
  }

  if ((ptr = strchr(type, ' ')) != NULL)
  {
    // Old-style filter definition in one string...
    *ptr++ = '\0';
    cost = strtol(ptr, &ptr, 10);

    while (isspace(*ptr))
      ptr ++;

    strlcpy(program, ptr, sizeof(program));
  }
  else
  {
    cost = get_integer(fp);

    if (!get_token(fp, program, sizeof(program)))
    {
      _cupsLangPrintf(stderr,
                      _("ppdc: Expected a program name on line %d of %s."),
		      fp->line, fp->filename);
      return (NULL);
    }
  }

  if (!type[0])
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Invalid empty MIME type for filter on line %d of "
		      "%s."), fp->line, fp->filename);
    return (NULL);
  }

  if (cost < 0 || cost > 200)
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Invalid cost for filter on line %d of %s."),
		    fp->line, fp->filename);
    return (NULL);
  }

  if (!program[0])
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Invalid empty program name for filter on line %d "
		      "of %s."), fp->line, fp->filename);
    return (NULL);
  }

  return (new ppdcFilter(type, program, cost));
}


//
// 'ppdcSource::get_float()' - Get a single floating-point number.
//

float					// O - Number
ppdcSource::get_float(ppdcFile *fp)	// I - File to read
{
  char	temp[256],			// String buffer
	*ptr;				// Pointer into buffer
  float	val;				// Floating point value


  // Get the number from the file and range-check...
  if (!get_token(fp, temp, sizeof(temp)))
  {
    _cupsLangPrintf(stderr, _("ppdc: Expected real number on line %d of %s."),
		    fp->line, fp->filename);
    return (-1.0f);
  }

  val = (float)strtod(temp, &ptr);

  if (*ptr)
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Unknown trailing characters in real number \"%s\" "
		      "on line %d of %s."), temp, fp->line, fp->filename);
    return (-1.0f);
  }
  else
    return (val);
}


//
// 'ppdcSource::get_font()' - Get a font definition.
//

ppdcFont *				// O - Font data
ppdcSource::get_font(ppdcFile *fp)	// I - File to read
{
  char			name[256],	// Font name
			encoding[256],	// Font encoding
			version[256],	// Font version
			charset[256],	// Font charset
			temp[256];	// Font status string
  ppdcFontStatus	status;		// Font status enumeration


  // Read font parameters as follows:
  //
  // Font *
  // Font name encoding version charset status
  // %font name encoding version charset status
  //
  // "Name" is the PostScript font name.
  //
  // "Encoding" is the default encoding of the font: Standard, ISOLatin1,
  // Special, Expert, ExpertSubset, etc.
  //
  // "Version" is the version number string.
  //
  // "Charset" specifies the characters that are included in the font:
  // Standard, Special, Expert, Adobe-Identity, etc.
  //
  // "Status" is the keyword ROM or Disk.
  if (!get_token(fp, name, sizeof(name)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected name after Font on line %d of %s."),
		    fp->line, fp->filename);
    return (0);
  }

  if (!strcmp(name, "*"))
  {
    // Include all base fonts...
    encoding[0] = '\0';
    version[0]  = '\0';
    charset[0]  = '\0';
    status      = PPDC_FONT_ROM;
  }
  else
  {
    // Load a full font definition...
    if (!get_token(fp, encoding, sizeof(encoding)))
    {
      _cupsLangPrintf(stderr,
                      _("ppdc: Expected encoding after Font on line %d of "
		        "%s."), fp->line, fp->filename);
      return (0);
    }

    if (!get_token(fp, version, sizeof(version)))
    {
      _cupsLangPrintf(stderr,
                      _("ppdc: Expected version after Font on line %d of "
		        "%s."), fp->line, fp->filename);
      return (0);
    }

    if (!get_token(fp, charset, sizeof(charset)))
    {
      _cupsLangPrintf(stderr,
                      _("ppdc: Expected charset after Font on line %d of "
		        "%s."), fp->line, fp->filename);
      return (0);
    }

    if (!get_token(fp, temp, sizeof(temp)))
    {
      _cupsLangPrintf(stderr,
                      _("ppdc: Expected status after Font on line %d of %s."),
		      fp->line, fp->filename);
      return (0);
    }

    if (!_cups_strcasecmp(temp, "ROM"))
      status = PPDC_FONT_ROM;
    else if (!_cups_strcasecmp(temp, "Disk"))
      status = PPDC_FONT_DISK;
    else
    {
      _cupsLangPrintf(stderr,
                      _("ppdc: Bad status keyword %s on line %d of %s."),
		      temp, fp->line, fp->filename);
      return (0);
    }
  }

//  printf("Font %s %s %s %s %s\n", name, encoding, version, charset, temp);

  return (new ppdcFont(name, encoding, version, charset, status));
}


//
// 'ppdcSource::get_generic()' - Get a generic old-style option.
//

ppdcChoice *				// O - Choice data
ppdcSource::get_generic(ppdcFile   *fp,	// I - File to read
                        const char *keyword,
					// I - Keyword name
                        const char *tattr,
					// I - Text attribute
			const char *nattr)
					// I - Numeric attribute
{
  char		name[1024],		// Name
		*text,			// Text
		command[256];		// Command string
  int		val;			// Numeric value


  // Read one of the following parameters:
  //
  // Foo name/text
  // Foo integer name/text
  if (nattr)
    val = get_integer(fp);
  else
    val = 0;

  if (!get_token(fp, name, sizeof(name)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected name/text after %s on line %d of %s."),
		    keyword, fp->line, fp->filename);
    return (NULL);
  }

  if ((text = strchr(name, '/')) != NULL)
    *text++ = '\0';
  else
    text = name;

  if (nattr)
  {
    if (tattr)
      snprintf(command, sizeof(command),
               "<</%s(%s)/%s %d>>setpagedevice",
               tattr, name, nattr, val);
    else
      snprintf(command, sizeof(command),
               "<</%s %d>>setpagedevice",
               nattr, val);
  }
  else
    snprintf(command, sizeof(command),
             "<</%s(%s)>>setpagedevice",
             tattr, name);

  return (new ppdcChoice(name, text, command));
}


//
// 'ppdcSource::get_group()' - Get an option group.
//

ppdcGroup *				// O - Group
ppdcSource::get_group(ppdcFile   *fp,	// I - File to read
                      ppdcDriver *d)	// I - Printer driver
{
  char		name[1024],		// UI name
		*text;			// UI text
  ppdcGroup	*g;			// Group


  // Read the Group parameters:
  //
  // Group name/text
  if (!get_token(fp, name, sizeof(name)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected group name/text on line %d of %s."),
		    fp->line, fp->filename);
    return (NULL);
  }

  if ((text = strchr(name, '/')) != NULL)
    *text++ = '\0';
  else
    text = name;

  // See if the group already exists...
  if ((g = d->find_group(name)) == NULL)
  {
    // Nope, add a new one...
    g = new ppdcGroup(name, text);
  }

  return (g);
}


//
// 'ppdcSource::get_installable()' - Get an installable option.
//

ppdcOption *				// O - Option
ppdcSource::get_installable(ppdcFile *fp)
					// I - File to read
{
  char		name[1024],		// Name for installable option
		*text;			// Text for installable option
  ppdcOption	*o;			// Option


  // Read the parameter for an installable option:
  //
  // Installable name/text
  if (!get_token(fp, name, sizeof(name)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected name/text after Installable on line %d "
		      "of %s."), fp->line, fp->filename);
    return (NULL);
  }

  if ((text = strchr(name, '/')) != NULL)
    *text++ = '\0';
  else
    text = name;

  // Create the option...
  o = new ppdcOption(PPDC_BOOLEAN, name, text, PPDC_SECTION_ANY, 10.0f);

  // Add the false and true choices...
  o->add_choice(new ppdcChoice("False", "Not Installed", ""));
  o->add_choice(new ppdcChoice("True", "Installed", ""));

  return (o);
}


//
// 'ppdcSource::get_integer()' - Get an integer value from a string.
//

#define PPDC_XX	-1			// Bad
#define PPDC_EQ	0			// ==
#define PPDC_NE	1			// !=
#define PPDC_LT	2			// <
#define PPDC_LE	3			// <=
#define PPDC_GT	4			// >
#define PPDC_GE	5			// >=

int					// O - Integer value
ppdcSource::get_integer(const char *v)	// I - Value string
{
  long		val;			// Value
  long		temp,			// Temporary value
		temp2;			// Second temporary value
  char		*newv,			// New value string pointer
		ch;			// Temporary character
  ppdcVariable	*var;			// #define variable
  int		compop;			// Comparison operator


  // Parse the value string...
  if (!v)
    return (-1);

  if (isdigit(*v & 255) || *v == '-' || *v == '+')
  {
    // Return a simple integer value
    val = strtol(v, (char **)&v, 0);
    if (*v || val == LONG_MIN)
      return (-1);
    else
      return ((int)val);
  }
  else if (*v == '(')
  {
    // Evaluate and expression in any of the following formats:
    //
    // (number number ... number)   Bitwise OR of all numbers
    // (NAME == value)              1 if equal, 0 otherwise
    // (NAME != value)              1 if not equal, 0 otherwise
    // (NAME < value)               1 if less than, 0 otherwise
    // (NAME <= value)              1 if less than or equal, 0 otherwise
    // (NAME > value)               1 if greater than, 0 otherwise
    // (NAME >= value)              1 if greater than or equal, 0 otherwise

    v ++;
    val = 0;

    while (*v && *v != ')')
    {
      // Skip leading whitespace...
      while (*v && isspace(*v & 255))
        v ++;

      if (!*v || *v == ')')
        break;

      if (isdigit(*v & 255) || *v == '-' || *v == '+')
      {
        // Bitwise OR a number...
	temp = strtol(v, &newv, 0);

	if (!*newv || newv == v || !(isspace(*newv) || *newv == ')') ||
	    temp == LONG_MIN)
	  return (-1);
      }
      else
      {
        // NAME logicop value
	for (newv = (char *)v + 1;
	     *newv && (isalnum(*newv & 255) || *newv == '_');
	     newv ++)
	  /* do nothing */;

        ch    = *newv;
	*newv = '\0';

        if ((var = find_variable(v)) != NULL)
	{
	  if (!var->value || !var->value->value || !var->value->value[0])
	    temp = 0;
	  else if (isdigit(var->value->value[0] & 255) ||
	           var->value->value[0] == '-' ||
	           var->value->value[0] == '+')
            temp = strtol(var->value->value, NULL, 0);
	  else
	    temp = 1;
	}
	else
	  temp = 0;

        *newv = ch;
	while (isspace(*newv & 255))
	  newv ++;

        if (!strncmp(newv, "==", 2))
	{
	  compop = PPDC_EQ;
	  newv += 2;
	}
        else if (!strncmp(newv, "!=", 2))
        {
	  compop = PPDC_NE;
	  newv += 2;
	}
        else if (!strncmp(newv, "<=", 2))
        {
	  compop = PPDC_LE;
	  newv += 2;
	}
	else if (*newv == '<')
        {
	  compop = PPDC_LT;
	  newv ++;
	}
        else if (!strncmp(newv, ">=", 2))
        {
	  compop = PPDC_GE;
	  newv += 2;
	}
	else if (*newv == '>')
	{
	  compop = PPDC_GT;
	  newv ++;
	}
	else
	  compop = PPDC_XX;

        if (compop != PPDC_XX)
	{
	  while (isspace(*newv & 255))
	    newv ++;

          if (*newv == ')' || !*newv)
	    return (-1);

	  if (isdigit(*newv & 255) || *newv == '-' || *newv == '+')
	  {
	    // Get the second number...
	    temp2 = strtol(newv, &newv, 0);
	    if (!*newv || newv == v || !(isspace(*newv) || *newv == ')') ||
		temp == LONG_MIN)
	      return (-1);
          }
	  else
	  {
	    // Lookup the second name...
	    for (v = newv, newv ++;
		 *newv && (isalnum(*newv & 255) || *newv == '_');
		 newv ++);

	    ch    = *newv;
	    *newv = '\0';

	    if ((var = find_variable(v)) != NULL)
	    {
	      if (!var->value || !var->value->value || !var->value->value[0])
		temp2 = 0;
	      else if (isdigit(var->value->value[0] & 255) ||
		       var->value->value[0] == '-' ||
		       var->value->value[0] == '+')
		temp2 = strtol(var->value->value, NULL, 0);
	      else
		temp2 = 1;
	    }
	    else
	      temp2 = 0;

	    *newv = ch;
          }

	  // Do the comparison...
	  switch (compop)
	  {
	    case PPDC_EQ :
	        temp = temp == temp2;
		break;
	    case PPDC_NE :
	        temp = temp != temp2;
		break;
	    case PPDC_LT :
	        temp = temp < temp2;
		break;
	    case PPDC_LE :
	        temp = temp <= temp2;
		break;
	    case PPDC_GT :
	        temp = temp > temp2;
		break;
	    case PPDC_GE :
	        temp = temp >= temp2;
		break;
	  }
	}
      }

      val |= temp;
      v   = newv;
    }

    if (*v == ')' && !v[1])
      return ((int)val);
    else
      return (-1);
  }
  else if ((var = find_variable(v)) != NULL)
  {
    // NAME by itself returns 1 if the #define variable is not blank and
    // not "0"...
    return (var->value->value && var->value->value[0] &&
            strcmp(var->value->value, "0"));
  }
  else
  {
    // Anything else is an error...
    return (-1);
  }
}


//
// 'ppdcSource::get_integer()' - Get an integer value from a file.
//

int					// O - Integer value
ppdcSource::get_integer(ppdcFile *fp)	// I - File to read
{
  char	temp[1024];			// String buffer


  if (!get_token(fp, temp, sizeof(temp)))
  {
    _cupsLangPrintf(stderr, _("ppdc: Expected integer on line %d of %s."),
		    fp->line, fp->filename);
    return (-1);
  }
  else
    return (get_integer(temp));
}


//
// 'ppdcSource::get_measurement()' - Get a measurement value.
//

float					// O - Measurement value in points
ppdcSource::get_measurement(ppdcFile *fp)
					// I - File to read
{
  char	buffer[256],			// Number buffer
	*ptr;				// Pointer into buffer
  float	val;				// Measurement value


  // Grab a token from the file...
  if (!get_token(fp, buffer, sizeof(buffer)))
    return (-1.0f);

  // Get the floating point value of "s" and skip all digits and decimal points.
  val = (float)strtod(buffer, &ptr);

  // Check for a trailing unit specifier...
  if (!_cups_strcasecmp(ptr, "mm"))
    val *= 72.0f / 25.4f;
  else if (!_cups_strcasecmp(ptr, "cm"))
    val *= 72.0f / 2.54f;
  else if (!_cups_strcasecmp(ptr, "m"))
    val *= 72.0f / 0.0254f;
  else if (!_cups_strcasecmp(ptr, "in"))
    val *= 72.0f;
  else if (!_cups_strcasecmp(ptr, "ft"))
    val *= 72.0f * 12.0f;
  else if (_cups_strcasecmp(ptr, "pt") && *ptr)
    return (-1.0f);

  return (val);
}


//
// 'ppdcSource::get_option()' - Get an option definition.
//

ppdcOption *				// O - Option
ppdcSource::get_option(ppdcFile   *fp,	// I - File to read
                       ppdcDriver *d,	// I - Printer driver
		       ppdcGroup  *g)	// I - Current group
{
  char		name[1024],		// UI name
		*text,			// UI text
		type[256];		// UI type string
  ppdcOptType	ot;			// Option type value
  ppdcOptSection section;		// Option section
  float		order;			// Option order
  ppdcOption	*o;			// Option
  ppdcGroup	*mg;			// Matching group, if any


  // Read the Option parameters:
  //
  // Option name/text type section order
  if (!get_token(fp, name, sizeof(name)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected option name/text on line %d of %s."),
		    fp->line, fp->filename);
    return (NULL);
  }

  if ((text = strchr(name, '/')) != NULL)
    *text++ = '\0';
  else
    text = name;

  if (!get_token(fp, type, sizeof(type)))
  {
    _cupsLangPrintf(stderr, _("ppdc: Expected option type on line %d of %s."),
		    fp->line, fp->filename);
    return (NULL);
  }

  if (!_cups_strcasecmp(type, "boolean"))
    ot = PPDC_BOOLEAN;
  else if (!_cups_strcasecmp(type, "pickone"))
    ot = PPDC_PICKONE;
  else if (!_cups_strcasecmp(type, "pickmany"))
    ot = PPDC_PICKMANY;
  else
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Invalid option type \"%s\" on line %d of %s."),
		    type, fp->line, fp->filename);
    return (NULL);
  }

  if (!get_token(fp, type, sizeof(type)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected option section on line %d of %s."),
		    fp->line, fp->filename);
    return (NULL);
  }

  if (!_cups_strcasecmp(type, "AnySetup"))
    section = PPDC_SECTION_ANY;
  else if (!_cups_strcasecmp(type, "DocumentSetup"))
    section = PPDC_SECTION_DOCUMENT;
  else if (!_cups_strcasecmp(type, "ExitServer"))
    section = PPDC_SECTION_EXIT;
  else if (!_cups_strcasecmp(type, "JCLSetup"))
    section = PPDC_SECTION_JCL;
  else if (!_cups_strcasecmp(type, "PageSetup"))
    section = PPDC_SECTION_PAGE;
  else if (!_cups_strcasecmp(type, "Prolog"))
    section = PPDC_SECTION_PROLOG;
  else
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Invalid option section \"%s\" on line %d of "
		      "%s."), type, fp->line, fp->filename);
    return (NULL);
  }

  order = get_float(fp);

  // See if the option already exists...
  if ((o = d->find_option_group(name, &mg)) == NULL)
  {
    // Nope, add a new one...
    o = new ppdcOption(ot, name, text, section, order);
  }
  else if (o->type != ot)
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Option %s redefined with a different type on line "
		      "%d of %s."), name, fp->line, fp->filename);
    return (NULL);
  }
  else if (g != mg)
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Option %s defined in two different groups on line "
		      "%d of %s."), name, fp->line, fp->filename);
    return (NULL);
  }

  return (o);
}


//
// 'ppdcSource::get_po()' - Get a message catalog.
//

ppdcCatalog *				// O - Message catalog
ppdcSource::get_po(ppdcFile *fp)	// I - File to read
{
  char		locale[32],		// Locale name
		poname[1024],		// Message catalog filename
		basedir[1024],		// Base directory
		*baseptr,		// Pointer into directory
		pofilename[1024];	// Full filename of message catalog
  ppdcCatalog	*cat;			// Message catalog


  // Read the #po parameters:
  //
  // #po locale "filename.po"
  if (!get_token(fp, locale, sizeof(locale)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected locale after #po on line %d of %s."),
		    fp->line, fp->filename);
    return (NULL);
  }

  if (!get_token(fp, poname, sizeof(poname)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected filename after #po %s on line %d of "
		      "%s."), locale, fp->line, fp->filename);
    return (NULL);
  }

  // See if the locale is already loaded...
  if (find_po(locale))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Duplicate #po for locale %s on line %d of %s."),
		    locale, fp->line, fp->filename);
    return (NULL);
  }

  // Figure out the current directory...
  strlcpy(basedir, fp->filename, sizeof(basedir));

  if ((baseptr = strrchr(basedir, '/')) != NULL)
    *baseptr = '\0';
  else
    strlcpy(basedir, ".", sizeof(basedir));

  // Find the po file...
  pofilename[0] = '\0';

  if (!poname[0] ||
      find_include(poname, basedir, pofilename, sizeof(pofilename)))
  {
    // Found it, so load it...
    cat = new ppdcCatalog(locale, pofilename);

    // Reset the filename to the name supplied by the user...
    cat->filename->release();
    cat->filename = new ppdcString(poname);

    // Return the catalog...
    return (cat);
  }
  else
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Unable to find #po file %s on line %d of %s."),
		    poname, fp->line, fp->filename);
    return (NULL);
  }
}


//
// 'ppdcSource::get_resolution()' - Get an old-style resolution option.
//

ppdcChoice *				// O - Choice data
ppdcSource::get_resolution(ppdcFile *fp)// I - File to read
{
  char		name[1024],		// Name
		*text,			// Text
		temp[256],		// Temporary string
		command[256],		// Command string
		*commptr;		// Pointer into command
  int		xdpi, ydpi,		// X + Y resolution
		color_order,		// Color order
		color_space,		// Colorspace
		compression,		// Compression mode
		depth,			// Bits per color
		row_count,		// Row count
		row_feed,		// Row feed
		row_step;		// Row step/interval


  // Read the resolution parameters:
  //
  // Resolution colorspace bits row-count row-feed row-step name/text
  if (!get_token(fp, temp, sizeof(temp)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected override field after Resolution on line "
		      "%d of %s."), fp->line, fp->filename);
    return (NULL);
  }

  color_order = get_color_order(temp);
  color_space = get_color_space(temp);
  compression = get_integer(temp);

  depth       = get_integer(fp);
  row_count   = get_integer(fp);
  row_feed    = get_integer(fp);
  row_step    = get_integer(fp);

  if (!get_token(fp, name, sizeof(name)))
  {
    _cupsLangPrintf(stderr,
		    _("ppdc: Expected name/text after Resolution on line %d of "
		      "%s."), fp->line, fp->filename);
    return (NULL);
  }

  if ((text = strchr(name, '/')) != NULL)
    *text++ = '\0';
  else
    text = name;

  switch (sscanf(name, "%dx%d", &xdpi, &ydpi))
  {
    case 1 :
        ydpi = xdpi;
        break;
    case 2 :
        break;
    default :
        _cupsLangPrintf(stderr,
                  _("ppdc: Bad resolution name \"%s\" on line %d of "
        "%s."), name, fp->line, fp->filename);
        break;
}

  // Create the necessary PS commands...
  snprintf(command, sizeof(command),
           "<</HWResolution[%d %d]/cupsBitsPerColor %d/cupsRowCount %d"
           "/cupsRowFeed %d/cupsRowStep %d",
	   xdpi, ydpi, depth, row_count, row_feed, row_step);
  commptr = command + strlen(command);

  if (color_order >= 0)
  {
    snprintf(commptr, sizeof(command) - (size_t)(commptr - command),
             "/cupsColorOrder %d", color_order);
    commptr += strlen(commptr);
  }

  if (color_space >= 0)
  {
    snprintf(commptr, sizeof(command) - (size_t)(commptr - command),
             "/cupsColorSpace %d", color_space);
    commptr += strlen(commptr);
  }

  if (compression >= 0)
  {
    snprintf(commptr, sizeof(command) - (size_t)(commptr - command),
             "/cupsCompression %d", compression);
    commptr += strlen(commptr);
  }

  snprintf(commptr, sizeof(command) - (size_t)(commptr - command), ">>setpagedevice");

  // Return the new choice...
  return (new ppdcChoice(name, text, command));
}


//
// 'ppdcSource::get_simple_profile()' - Get a simple color profile definition.
//

ppdcProfile *				// O - Color profile
ppdcSource::get_simple_profile(ppdcFile *fp)
					// I - File to read
{
  char		resolution[1024],	// Resolution/media type
		*media_type;		// Media type
  float		m[9];			// Transform matrix
  float		kd, rd, g;		// Densities and gamma
  float		red, green, blue;	// RGB adjustments
  float		yellow;			// Yellow density
  float		color;			// Color density values


  // Get the SimpleColorProfile parameters:
  //
  // SimpleColorProfile resolution/mediatype black-density yellow-density
  //     red-density gamma red-adjust green-adjust blue-adjust
  if (!get_token(fp, resolution, sizeof(resolution)))
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Expected resolution/mediatype following "
		      "SimpleColorProfile on line %d of %s."),
		    fp->line, fp->filename);
    return (NULL);
  }

  if ((media_type = strchr(resolution, '/')) != NULL)
    *media_type++ = '\0';
  else
    media_type = resolution;

  // Collect the profile parameters...
  kd     = get_float(fp);
  yellow = get_float(fp);
  rd     = get_float(fp);
  g      = get_float(fp);
  red    = get_float(fp);
  green  = get_float(fp);
  blue   = get_float(fp);

  // Build the color profile...
  color = 0.5f * rd / kd - kd;
  m[0]  = 1.0f;				// C
  m[1]  = color + blue;			// C + M (blue)
  m[2]  = color - green;		// C + Y (green)
  m[3]  = color - blue;			// M + C (blue)
  m[4]  = 1.0f;				// M
  m[5]  = color + red;			// M + Y (red)
  m[6]  = yellow * (color + green);	// Y + C (green)
  m[7]  = yellow * (color - red);	// Y + M (red)
  m[8]  = yellow;			// Y

  if (m[1] > 0.0f)
  {
    m[3] -= m[1];
    m[1] = 0.0f;
  }
  else if (m[3] > 0.0f)
  {
    m[1] -= m[3];
    m[3] = 0.0f;
  }

  if (m[2] > 0.0f)
  {
    m[6] -= m[2];
    m[2] = 0.0f;
  }
  else if (m[6] > 0.0f)
  {
    m[2] -= m[6];
    m[6] = 0.0f;
  }

  if (m[5] > 0.0f)
  {
    m[7] -= m[5];
    m[5] = 0.0f;
  }
  else if (m[7] > 0.0f)
  {
    m[5] -= m[7];
    m[7] = 0.0f;
  }

  // Return the new profile...
  return (new ppdcProfile(resolution, media_type, kd, g, m));
}


//
// 'ppdcSource::get_size()' - Get a media size definition from a file.
//

ppdcMediaSize *				// O - Media size
ppdcSource::get_size(ppdcFile *fp)	// I - File to read
{
  char		name[1024],		// Name
		*text;			// Text
  float		width,			// Width
		length;			// Length


  // Get the name, text, width, and length:
  //
  // #media name/text width length
  if (!get_token(fp, name, sizeof(name)))
    return (NULL);

  if ((text = strchr(name, '/')) != NULL)
    *text++ = '\0';
  else
    text = name;

  if ((width = get_measurement(fp)) < 0.0f)
    return (NULL);

  if ((length = get_measurement(fp)) < 0.0f)
    return (NULL);

  // Return the new media size...
  return (new ppdcMediaSize(name, text, width, length, 0.0f, 0.0f, 0.0f, 0.0f));
}


//
// 'ppdcSource::get_token()' - Get a token from a file.
//

char *					// O - Token string or NULL
ppdcSource::get_token(ppdcFile *fp,	// I - File to read
                      char     *buffer,	// I - Buffer
		      int      buflen)	// I - Length of buffer
{
  char		*bufptr,		// Pointer into string buffer
		*bufend;		// End of string buffer
  int		ch,			// Character from file
		nextch,			// Next char in file
		quote,			// Quote character used...
		empty,			// Empty input?
		startline;		// Start line for quote
  char		name[256],		// Name string
		*nameptr;		// Name pointer
  ppdcVariable	*var;			// Variable pointer


  // Mark the beginning and end of the buffer...
  bufptr = buffer;
  bufend = buffer + buflen - 1;

  // Loop intil we've read a token...
  quote     = 0;
  startline = 0;
  empty     = 1;

  while ((ch = fp->get()) != EOF)
  {
    if (isspace(ch) && !quote)
    {
      if (empty)
        continue;
      else
        break;
    }
    else if (ch == '$')
    {
      // Variable substitution
      empty = 0;

      for (nameptr = name; (ch = fp->peek()) != EOF;)
      {
        if (!isalnum(ch) && ch != '_')
	  break;
	else if (nameptr < (name + sizeof(name) - 1))
	  *nameptr++ = (char)fp->get();
      }

      if (nameptr == name)
      {
        // Just substitute this character...
	if (ch == '$')
	{
	  // $$ = $
	  if (bufptr < bufend)
	    *bufptr++ = (char)fp->get();
	}
	else
	{
	  // $ch = $ch
          _cupsLangPrintf(stderr,
	                  _("ppdc: Bad variable substitution ($%c) on line %d "
			    "of %s."), ch, fp->line, fp->filename);

	  if (bufptr < bufend)
	    *bufptr++ = '$';
	}
      }
      else
      {
        // Substitute the variable value...
	*nameptr = '\0';
	var = find_variable(name);
	if (var)
	{
	  strlcpy(bufptr, var->value->value, (size_t)(bufend - bufptr + 1));
	  bufptr += strlen(bufptr);
	}
	else
	{
	  if (!(cond_state & PPDC_COND_SKIP))
	    _cupsLangPrintf(stderr,
			    _("ppdc: Undefined variable (%s) on line %d of "
			      "%s."), name, fp->line, fp->filename);

	  snprintf(bufptr, (size_t)(bufend - bufptr + 1), "$%s", name);
	  bufptr += strlen(bufptr);
	}
      }
    }
    else if (ch == '/' && !quote)
    {
      // Possibly a comment...
      nextch = fp->peek();

      if (nextch == '*')
      {
        // C comment...
	fp->get();
	ch = fp->get();
	while ((nextch = fp->get()) != EOF)
	{
	  if (ch == '*' && nextch == '/')
	    break;

	  ch = nextch;
	}

        if (nextch == EOF)
          break;
      }
      else if (nextch == '/')
      {
        // C++ comment...
        while ((nextch = fp->get()) != EOF)
          if (nextch == '\n')
	    break;

        if (nextch == EOF)
          break;
      }
      else
      {
        // Not a comment...
        empty = 0;

	if (bufptr < bufend)
	  *bufptr++ = (char)ch;
      }
    }
    else if (ch == '\'' || ch == '\"')
    {
      empty = 0;

      if (quote == ch)
      {
        // Ending the current quoted string...
        quote = 0;
      }
      else if (quote)
      {
        // Insert the opposing quote char...
	if (bufptr < bufend)
          *bufptr++ = (char)ch;
      }
      else
      {
        // Start a new quoted string...
        startline = fp->line;
        quote     = ch;
      }
    }
    else if ((ch == '(' || ch == '<') && !quote)
    {
      empty     = 0;
      quote     = ch;
      startline = fp->line;

      if (bufptr < bufend)
	*bufptr++ = (char)ch;
    }
    else if ((ch == ')' && quote == '(') || (ch == '>' && quote == '<'))
    {
      quote = 0;

      if (bufptr < bufend)
	*bufptr++ = (char)ch;
    }
    else if (ch == '\\')
    {
      empty = 0;

      if ((ch = fp->get()) == EOF)
        break;

      if (bufptr < bufend)
        *bufptr++ = (char)ch;
    }
    else if (bufptr < bufend)
    {
      empty = 0;

      *bufptr++ = (char)ch;

      if ((ch == '{' || ch == '}') && !quote)
        break;
    }
  }

  if (quote)
  {
    _cupsLangPrintf(stderr,
                    _("ppdc: Unterminated string starting with %c on line %d "
		      "of %s."), quote, startline, fp->filename);
    return (NULL);
  }

  if (empty)
    return (NULL);
  else
  {
    *bufptr = '\0';
    return (buffer);
  }
}


//
// 'ppdcSource::get_variable()' - Get a variable definition.
//

ppdcVariable *				// O - Variable
ppdcSource::get_variable(ppdcFile *fp)	// I - File to read
{
  char		name[1024],		// Name
		value[1024];		// Value


  // Get the name and value:
  //
  // #define name value
  if (!get_token(fp, name, sizeof(name)))
    return (NULL);

  if (!get_token(fp, value, sizeof(value)))
    return (NULL);

  // Set the variable...
  return (set_variable(name, value));
}


//
// 'ppdcSource::quotef()' - Write a formatted, quoted string...
//

int					// O - Number bytes on success, -1 on failure
ppdcSource::quotef(cups_file_t *fp,	// I - File to write to
                   const char  *format,	// I - Printf-style format string
		   ...)			// I - Additional args as needed
{
  va_list	ap;			// Pointer to additional arguments
  int		bytes;			// Bytes written
  char		sign,			// Sign of format width
		size,			// Size character (h, l, L)
		type;			// Format type character
  const char	*bufformat;		// Start of format
  int		width,			// Width of field
		prec;			// Number of characters of precision
  char		tformat[100];		// Temporary format string for fprintf()
  char		*s;			// Pointer to string
  int		slen;			// Length of string
  int		i;			// Looping var


  // Range check input...
  if (!fp || !format)
    return (-1);

  // Loop through the format string, formatting as needed...
  va_start(ap, format);

  bytes = 0;

  while (*format)
  {
    if (*format == '%')
    {
      bufformat = format;
      format ++;

      if (*format == '%')
      {
        cupsFilePutChar(fp, *format++);
	bytes ++;
	continue;
      }
      else if (strchr(" -+#\'", *format))
        sign = *format++;
      else
        sign = 0;

      width = 0;
      while (isdigit(*format))
        width = width * 10 + *format++ - '0';

      if (*format == '.')
      {
        format ++;
	prec = 0;

	while (isdigit(*format))
          prec = prec * 10 + *format++ - '0';
      }
      else
        prec = -1;

      if (*format == 'l' && format[1] == 'l')
      {
        size = 'L';
	format += 2;
      }
      else if (*format == 'h' || *format == 'l' || *format == 'L')
        size = *format++;
      else
        size = '\0';

      if (!*format)
        break;

      type = *format++;

      switch (type)
      {
	case 'E' : // Floating point formats
	case 'G' :
	case 'e' :
	case 'f' :
	case 'g' :
	    if ((format - bufformat + 1) > (int)sizeof(tformat))
	      break;

	    memcpy(tformat, bufformat, (size_t)(format - bufformat));
	    tformat[format - bufformat] = '\0';

	    bytes += cupsFilePrintf(fp, tformat, va_arg(ap, double));
	    break;

        case 'B' : // Integer formats
	case 'X' :
	case 'b' :
        case 'd' :
	case 'i' :
	case 'o' :
	case 'u' :
	case 'x' :
	    if ((format - bufformat + 1) > (int)sizeof(tformat))
	      break;

	    memcpy(tformat, bufformat, (size_t)(format - bufformat));
	    tformat[format - bufformat] = '\0';

#  ifdef HAVE_LONG_LONG
            if (size == 'L')
	      bytes += cupsFilePrintf(fp, tformat, va_arg(ap, long long));
	    else
#  endif /* HAVE_LONG_LONG */
            if (size == 'l')
	      bytes += cupsFilePrintf(fp, tformat, va_arg(ap, long));
	    else
	      bytes += cupsFilePrintf(fp, tformat, va_arg(ap, int));
	    break;

	case 'p' : // Pointer value
	    if ((format - bufformat + 1) > (int)sizeof(tformat))
	      break;

	    memcpy(tformat, bufformat, (size_t)(format - bufformat));
	    tformat[format - bufformat] = '\0';

	    bytes += cupsFilePrintf(fp, tformat, va_arg(ap, void *));
	    break;

        case 'c' : // Character or character array
	    if (width <= 1)
	    {
	      bytes ++;
	      cupsFilePutChar(fp, va_arg(ap, int));
	    }
	    else
	    {
	      cupsFileWrite(fp, va_arg(ap, char *), (size_t)width);
	      bytes += width;
	    }
	    break;

	case 's' : // String
	    if ((s = va_arg(ap, char *)) == NULL)
	      s = (char *)"(nil)";

	    slen = (int)strlen(s);
	    if (slen > width && prec != width)
	      width = slen;

            if (slen > width)
	      slen = width;

            if (sign != '-')
	    {
	      for (i = width - slen; i > 0; i --, bytes ++)
	        cupsFilePutChar(fp, ' ');
	    }

            for (i = slen; i > 0; i --, s ++, bytes ++)
	    {
	      if (*s == '\\' || *s == '\"')
	      {
	        cupsFilePutChar(fp, '\\');
		bytes ++;
	      }

	      cupsFilePutChar(fp, *s);
	    }

            if (sign == '-')
	    {
	      for (i = width - slen; i > 0; i --, bytes ++)
	        cupsFilePutChar(fp, ' ');
	    }
	    break;
      }
    }
    else
    {
      cupsFilePutChar(fp, *format++);
      bytes ++;
    }
  }

  va_end(ap);

  // Return the number of characters written.
  return (bytes);
}


//
// 'ppdcSource::read_file()' - Read a driver source file.
//

void
ppdcSource::read_file(const char  *f,	// I - File to read
                      cups_file_t *ffp)	// I - File pointer to use
{
  ppdcFile *fp = new ppdcFile(f, ffp);
  scan_file(fp);
  delete fp;

  if (cond_current != cond_stack)
    _cupsLangPrintf(stderr, _("ppdc: Missing #endif at end of \"%s\"."), f);
}


//
// 'ppdcSource::scan_file()' - Scan a driver source file.
//

void
ppdcSource::scan_file(ppdcFile   *fp,	// I - File to read
                      ppdcDriver *td,	// I - Driver template
		      bool       inc)	// I - Including?
{
  ppdcDriver	*d;			// Current driver
  ppdcGroup	*g,			// Current group
		*mg,			// Matching group
		*general,		// General options group
		*install;		// Installable options group
  ppdcOption	*o;			// Current option
  ppdcChoice	*c;			// Current choice
  char		temp[256],		// Token from file...
		*ptr;			// Pointer into token
  int		isdefault;		// Default option?


  // Initialize things as needed...
  if (inc && td)
  {
    d = td;
    d->retain();
  }
  else
    d = new ppdcDriver(td);

  if ((general = d->find_group("General")) == NULL)
  {
    general = new ppdcGroup("General", NULL);
    d->add_group(general);
  }

  if ((install = d->find_group("InstallableOptions")) == NULL)
  {
    install = new ppdcGroup("InstallableOptions", "Installable Options");
    d->add_group(install);
  }

  // Loop until EOF or }
  o = 0;
  g = general;

  while (get_token(fp, temp, sizeof(temp)))
  {
    if (temp[0] == '*')
    {
      // Mark the next choice as the default
      isdefault = 1;

      for (ptr = temp; ptr[1]; ptr ++)
        *ptr = ptr[1];

      *ptr = '\0';
    }
    else
    {
      // Don't mark the next choice as the default
      isdefault = 0;
    }

    if (!_cups_strcasecmp(temp, "}"))
    {
      // Close this one out...
      break;
    }
    else if (!_cups_strcasecmp(temp, "{"))
    {
      // Open a new child...
      scan_file(fp, d);
    }
    else if (!_cups_strcasecmp(temp, "#if"))
    {
      if ((cond_current - cond_stack) >= 100)
      {
        _cupsLangPrintf(stderr,
	                _("ppdc: Too many nested #if's on line %d of %s."),
			fp->line, fp->filename);
	break;
      }

      cond_current ++;
      if (get_integer(fp) > 0)
        *cond_current = PPDC_COND_SATISFIED;
      else
      {
        *cond_current = PPDC_COND_SKIP;
	cond_state    |= PPDC_COND_SKIP;
      }
    }
    else if (!_cups_strcasecmp(temp, "#elif"))
    {
      if (cond_current == cond_stack)
      {
        _cupsLangPrintf(stderr, _("ppdc: Missing #if on line %d of %s."),
	                fp->line, fp->filename);
        break;
      }

      if (*cond_current & PPDC_COND_SATISFIED)
      {
        get_integer(fp);
	*cond_current |= PPDC_COND_SKIP;
      }
      else if (get_integer(fp) > 0)
      {
        *cond_current |= PPDC_COND_SATISFIED;
	*cond_current &= ~PPDC_COND_SKIP;
      }
      else
        *cond_current |= PPDC_COND_SKIP;

      // Update the current state
      int *cond_temp = cond_current;	// Temporary stack pointer

      cond_state = PPDC_COND_NORMAL;
      while (cond_temp > cond_stack)
        if (*cond_temp & PPDC_COND_SKIP)
	{
	  cond_state = PPDC_COND_SKIP;
	  break;
	}
	else
	  cond_temp --;
    }
    else if (!_cups_strcasecmp(temp, "#else"))
    {
      if (cond_current == cond_stack)
      {
        _cupsLangPrintf(stderr, _("ppdc: Missing #if on line %d of %s."),
		        fp->line, fp->filename);
        break;
      }

      if (*cond_current & PPDC_COND_SATISFIED)
	*cond_current |= PPDC_COND_SKIP;
      else
      {
        *cond_current |= PPDC_COND_SATISFIED;
	*cond_current &= ~PPDC_COND_SKIP;
      }

      // Update the current state
      int *cond_temp = cond_current;	// Temporary stack pointer

      cond_state = PPDC_COND_NORMAL;
      while (cond_temp > cond_stack)
        if (*cond_temp & PPDC_COND_SKIP)
	{
	  cond_state = PPDC_COND_SKIP;
	  break;
	}
	else
	  cond_temp --;
    }
    else if (!_cups_strcasecmp(temp, "#endif"))
    {
      if (cond_current == cond_stack)
      {
        _cupsLangPrintf(stderr, _("ppdc: Missing #if on line %d of %s."),
	                fp->line, fp->filename);
        break;
      }

      cond_current --;

      // Update the current state
      int *cond_temp = cond_current;	// Temporary stack pointer

      cond_state = PPDC_COND_NORMAL;
      while (cond_temp > cond_stack)
        if (*cond_temp & PPDC_COND_SKIP)
	{
	  cond_state = PPDC_COND_SKIP;
	  break;
	}
	else
	  cond_temp --;
    }
    else if (!_cups_strcasecmp(temp, "#define"))
    {
      // Get the variable...
      get_variable(fp);
    }
    else if (!_cups_strcasecmp(temp, "#include"))
    {
      // #include filename
      char	basedir[1024],		// Base directory
		*baseptr,		// Pointer into directory
		inctemp[1024],		// Initial filename
		incname[1024];		// Include filename
      ppdcFile	*incfile;		// Include file
      int	*old_current = cond_current;
					// Previous current stack


      // Get the include name...
      if (!get_token(fp, inctemp, sizeof(inctemp)))
      {
        _cupsLangPrintf(stderr,
	                _("ppdc: Expected include filename on line %d of "
			  "%s."), fp->line, fp->filename);
        break;
      }

      if (cond_state)
        continue;

      // Figure out the current directory...
      strlcpy(basedir, fp->filename, sizeof(basedir));

      if ((baseptr = strrchr(basedir, '/')) != NULL)
	*baseptr = '\0';
      else
	strlcpy(basedir, ".", sizeof(basedir));

      // Find the include file...
      if (find_include(inctemp, basedir, incname, sizeof(incname)))
      {
	// Open the include file, scan it, and then close it...
	incfile = new ppdcFile(incname);
	scan_file(incfile, d, true);
	delete incfile;

	if (cond_current != old_current)
	  _cupsLangPrintf(stderr, _("ppdc: Missing #endif at end of \"%s\"."),
	                  incname);
      }
      else
      {
	// Can't find it!
	_cupsLangPrintf(stderr,
		        _("ppdc: Unable to find include file \"%s\" on line %d "
			  "of %s."), inctemp, fp->line, fp->filename);
	break;
      }
    }
    else if (!_cups_strcasecmp(temp, "#media"))
    {
      ppdcMediaSize	*m;		// Media size


      // Get a media size...
      m = get_size(fp);
      if (m)
      {
        if (cond_state)
	  m->release();
	else
          sizes->add(m);
      }
    }
    else if (!_cups_strcasecmp(temp, "#po"))
    {
      ppdcCatalog	*cat;		// Message catalog


      // Get a message catalog...
      cat = get_po(fp);
      if (cat)
      {
        if (cond_state)
	  cat->release();
	else
	  po_files->add(cat);
      }
    }
    else if (!_cups_strcasecmp(temp, "Attribute") ||
             !_cups_strcasecmp(temp, "LocAttribute"))
    {
      ppdcAttr	*a;			// Attribute


      // Get an attribute...
      a = get_attr(fp, !_cups_strcasecmp(temp, "LocAttribute"));
      if (a)
      {
        if (cond_state)
	  a->release();
	else
          d->add_attr(a);
      }
    }
    else if (!_cups_strcasecmp(temp, "Choice"))
    {
      // Get a choice...
      c = get_choice(fp);
      if (!c)
        break;

      if (cond_state)
      {
        c->release();
        continue;
      }

      // Add it to the current option...
      if (!o)
      {
        c->release();
        _cupsLangPrintf(stderr,
	                _("ppdc: Choice found on line %d of %s with no "
			  "Option."), fp->line, fp->filename);
        break;
      }

      o->add_choice(c);

      if (isdefault)
        o->set_defchoice(c);
    }
    else if (!_cups_strcasecmp(temp, "ColorDevice"))
    {
      // ColorDevice boolean
      if (cond_state)
        get_boolean(fp);
      else
        d->color_device = get_boolean(fp);
    }
    else if (!_cups_strcasecmp(temp, "ColorModel"))
    {
      // Get the color model
      c = get_color_model(fp);
      if (!c)
        continue;

      if (cond_state)
      {
        c->release();
        continue;
      }

      // Add the choice to the ColorModel option...
      if ((o = d->find_option("ColorModel")) == NULL)
      {
	// Create the ColorModel option...
	o = new ppdcOption(PPDC_PICKONE, "ColorModel", "Color Mode", PPDC_SECTION_ANY, 10.0f);
	g = general;
	g->add_option(o);
      }

      o->add_choice(c);

      if (isdefault)
	o->set_defchoice(c);

      o = NULL;
    }
    else if (!_cups_strcasecmp(temp, "ColorProfile"))
    {
      ppdcProfile	*p;		// Color profile


      // Get the color profile...
      p = get_color_profile(fp);

      if (p)
      {
        if (cond_state)
	  p->release();
	else
          d->profiles->add(p);
      }
    }
    else if (!_cups_strcasecmp(temp, "Copyright"))
    {
      // Copyright string
      char	copytemp[8192],		// Copyright string
		*copyptr,		// Pointer into string
		*copyend;		// Pointer to end of string


      // Get the copyright string...
      if (!get_token(fp, copytemp, sizeof(temp)))
      {
        _cupsLangPrintf(stderr,
	                _("ppdc: Expected string after Copyright on line %d "
			  "of %s."), fp->line, fp->filename);
	break;
      }

      if (cond_state)
        continue;

      // Break it up into individual lines...
      for (copyptr = copytemp; copyptr; copyptr = copyend)
      {
        if ((copyend = strchr(copyptr, '\n')) != NULL)
	  *copyend++ = '\0';

        d->copyright->add(new ppdcString(copyptr));
      }
    }
    else if (!_cups_strcasecmp(temp, "CustomMedia"))
    {
      ppdcMediaSize	*m;		// Media size


      // Get a custom media size...
      m = get_custom_size(fp);

      if (cond_state)
      {
        m->release();
        continue;
      }

      if (m)
        d->sizes->add(m);

      if (isdefault)
        d->set_default_size(m);
    }
    else if (!_cups_strcasecmp(temp, "Cutter"))
    {
      // Cutter boolean
      int	have_cutter;		// Have a paper cutter?


      have_cutter = get_boolean(fp);
      if (have_cutter <= 0 || cond_state)
        continue;

      if (!d->find_option("CutMedia"))
      {
        o = new ppdcOption(PPDC_BOOLEAN, "CutMedia", "Cut Media", PPDC_SECTION_ANY, 10.0f);

	g = general;
	g->add_option(o);

	c = new ppdcChoice("False", NULL, "<</CutMedia 0>>setpagedevice");
	o->add_choice(c);
	o->set_defchoice(c);

	c = new ppdcChoice("True", NULL, "<</CutMedia 4>>setpagedevice");
	o->add_choice(c);
        o = NULL;
      }
    }
    else if (!_cups_strcasecmp(temp, "Darkness"))
    {
      // Get the darkness choice...
      c = get_generic(fp, "Darkness", NULL, "cupsCompression");
      if (!c)
        continue;

      if (cond_state)
      {
        c->release();
        continue;
      }

      // Add the choice to the cupsDarkness option...
      if ((o = d->find_option_group("cupsDarkness", &mg)) == NULL)
      {
	// Create the cupsDarkness option...
	o = new ppdcOption(PPDC_PICKONE, "cupsDarkness", "Darkness", PPDC_SECTION_ANY, 10.0f);
	g = general;
	g->add_option(o);
      }
      else if (mg != general)
      {
	_cupsLangPrintf(stderr,
			_("ppdc: Option %s defined in two different groups on "
			  "line %d of %s."), "cupsDarkness", fp->line,
		        fp->filename);
	c->release();
	continue;
      }

      o->add_choice(c);

      if (isdefault)
	o->set_defchoice(c);

      o = NULL;
    }
    else if (!_cups_strcasecmp(temp, "DriverType"))
    {
      int	i;			// Looping var


      // DriverType keyword
      if (!get_token(fp, temp, sizeof(temp)))
      {
        _cupsLangPrintf(stderr,
	                _("ppdc: Expected driver type keyword following "
			  "DriverType on line %d of %s."),
			fp->line, fp->filename);
        continue;
      }

      if (cond_state)
        continue;

      for (i = 0; i < (int)(sizeof(driver_types) / sizeof(driver_types[0])); i ++)
        if (!_cups_strcasecmp(temp, driver_types[i]))
	  break;

      if (i < (int)(sizeof(driver_types) / sizeof(driver_types[0])))
        d->type = (ppdcDrvType)i;
      else if (!_cups_strcasecmp(temp, "dymo"))
        d->type = PPDC_DRIVER_LABEL;
      else
        _cupsLangPrintf(stderr,
	                _("ppdc: Unknown driver type %s on line %d of %s."),
			temp, fp->line, fp->filename);
    }
    else if (!_cups_strcasecmp(temp, "Duplex"))
      get_duplex(fp, d);
    else if (!_cups_strcasecmp(temp, "Filter"))
    {
      ppdcFilter	*f;		// Filter


      // Get the filter value...
      f = get_filter(fp);
      if (f)
      {
        if (cond_state)
	  f->release();
	else
          d->filters->add(f);
      }
    }
    else if (!_cups_strcasecmp(temp, "Finishing"))
    {
      // Get the finishing choice...
      c = get_generic(fp, "Finishing", "OutputType", NULL);
      if (!c)
        continue;

      if (cond_state)
      {
        c->release();
        continue;
      }

      // Add the choice to the cupsFinishing option...
      if ((o = d->find_option_group("cupsFinishing", &mg)) == NULL)
      {
	// Create the cupsFinishing option...
	o = new ppdcOption(PPDC_PICKONE, "cupsFinishing", "Finishing", PPDC_SECTION_ANY, 10.0f);
	g = general;
	g->add_option(o);
      }
      else if (mg != general)
      {
	_cupsLangPrintf(stderr,
			_("ppdc: Option %s defined in two different groups on "
			  "line %d of %s."), "cupsFinishing", fp->line,
		        fp->filename);
	c->release();
	continue;
      }

      o->add_choice(c);

      if (isdefault)
	o->set_defchoice(c);

      o = NULL;
    }
    else if (!_cups_strcasecmp(temp, "Font") ||
             !_cups_strcasecmp(temp, "#font"))
    {
      ppdcFont	*f;			// Font


      // Get a font...
      f = get_font(fp);
      if (f)
      {
        if (cond_state)
	  f->release();
	else
	{
	  if (!_cups_strcasecmp(temp, "#font"))
	    base_fonts->add(f);
	  else
	    d->add_font(f);

	  if (isdefault)
	    d->set_default_font(f);
	}
      }
    }
    else if (!_cups_strcasecmp(temp, "Group"))
    {
      // Get a group...
      ppdcGroup *tempg = get_group(fp, d);

      if (!tempg)
        break;

      if (cond_state)
      {
        if (!d->find_group(tempg->name->value))
          tempg->release();
      }
      else
      {
	if (!d->find_group(tempg->name->value))
	  d->add_group(tempg);

        g = tempg;
      }
    }
    else if (!_cups_strcasecmp(temp, "HWMargins"))
    {
      // HWMargins left bottom right top
      d->left_margin   = get_measurement(fp);
      d->bottom_margin = get_measurement(fp);
      d->right_margin  = get_measurement(fp);
      d->top_margin    = get_measurement(fp);
    }
    else if (!_cups_strcasecmp(temp, "InputSlot"))
    {
      // Get the input slot choice...
      c = get_generic(fp, "InputSlot", NULL, "MediaPosition");
      if (!c)
        continue;

      if (cond_state)
      {
        c->release();
        continue;
      }

      // Add the choice to the InputSlot option...

      if ((o = d->find_option_group("InputSlot", &mg)) == NULL)
      {
	// Create the InputSlot option...
	o = new ppdcOption(PPDC_PICKONE, "InputSlot", "Media Source",
	                   PPDC_SECTION_ANY, 10.0f);
	g = general;
	g->add_option(o);
      }
      else if (mg != general)
      {
	_cupsLangPrintf(stderr,
			_("ppdc: Option %s defined in two different groups on "
			  "line %d of %s."), "InputSlot", fp->line,
		        fp->filename);
	c->release();
	continue;
      }

      o->add_choice(c);

      if (isdefault)
	o->set_defchoice(c);

      o = NULL;
    }
    else if (!_cups_strcasecmp(temp, "Installable"))
    {
      // Get the installable option...
      o = get_installable(fp);

      // Add it as needed...
      if (o)
      {
        if (cond_state)
	  o->release();
	else
          install->add_option(o);

        o = NULL;
      }
    }
    else if (!_cups_strcasecmp(temp, "ManualCopies"))
    {
      // ManualCopies boolean
      if (cond_state)
        get_boolean(fp);
      else
        d->manual_copies = get_boolean(fp);
    }
    else if (!_cups_strcasecmp(temp, "Manufacturer"))
    {
      // Manufacturer name
      char	name[256];		// Model name string


      if (!get_token(fp, name, sizeof(name)))
      {
        _cupsLangPrintf(stderr,
			_("ppdc: Expected name after Manufacturer on line %d "
			  "of %s."), fp->line, fp->filename);
	break;
      }

      if (!cond_state)
        d->set_manufacturer(name);
    }
    else if (!_cups_strcasecmp(temp, "MaxSize"))
    {
      // MaxSize width length
      if (cond_state)
      {
        get_measurement(fp);
	get_measurement(fp);
      }
      else
      {
	d->max_width  = get_measurement(fp);
	d->max_length = get_measurement(fp);
      }
    }
    else if (!_cups_strcasecmp(temp, "MediaSize"))
    {
      // MediaSize keyword
      char		name[41];	// Media size name
      ppdcMediaSize	*m,		// Matching media size...
			*dm;		// Driver media size...


      if (get_token(fp, name, sizeof(name)) == NULL)
      {
        _cupsLangPrintf(stderr,
	                _("ppdc: Expected name after MediaSize on line %d of "
			  "%s."), fp->line, fp->filename);
	break;
      }

      if (cond_state)
        continue;

      m = find_size(name);

      if (!m)
      {
        _cupsLangPrintf(stderr,
	                _("ppdc: Unknown media size \"%s\" on line %d of "
			  "%s."), name, fp->line, fp->filename);
	break;
      }

      // Add this size to the driver...
      dm = new ppdcMediaSize(m->name->value, m->text->value,
                             m->width, m->length, d->left_margin,
			     d->bottom_margin, d->right_margin,
			     d->top_margin);
      d->sizes->add(dm);

      if (isdefault)
        d->set_default_size(dm);
    }
    else if (!_cups_strcasecmp(temp, "MediaType"))
    {
      // Get the media type choice...
      c = get_generic(fp, "MediaType", "MediaType", "cupsMediaType");
      if (!c)
        continue;

      if (cond_state)
      {
        c->release();
        continue;
      }

      // Add the choice to the MediaType option...
      if ((o = d->find_option_group("MediaType", &mg)) == NULL)
      {
	// Create the MediaType option...
	o = new ppdcOption(PPDC_PICKONE, "MediaType", "Media Type",
	                   PPDC_SECTION_ANY, 10.0f);
	g = general;
	g->add_option(o);
      }
      else if (mg != general)
      {
	_cupsLangPrintf(stderr,
			_("ppdc: Option %s defined in two different groups on "
			  "line %d of %s."), "MediaType", fp->line,
		        fp->filename);
	c->release();
	continue;
      }

      o->add_choice(c);

      if (isdefault)
	o->set_defchoice(c);

      o = NULL;
    }
    else if (!_cups_strcasecmp(temp, "MinSize"))
    {
      // MinSize width length
      if (cond_state)
      {
        get_measurement(fp);
	get_measurement(fp);
      }
      else
      {
	d->min_width  = get_measurement(fp);
	d->min_length = get_measurement(fp);
      }
    }
    else if (!_cups_strcasecmp(temp, "ModelName"))
    {
      // ModelName name
      char	name[256];		// Model name string


      if (!get_token(fp, name, sizeof(name)))
      {
        _cupsLangPrintf(stderr,
	                _("ppdc: Expected name after ModelName on line %d of "
			  "%s."), fp->line, fp->filename);
	break;
      }

      if (!cond_state)
        d->set_model_name(name);
    }
    else if (!_cups_strcasecmp(temp, "ModelNumber"))
    {
      // ModelNumber number
      if (cond_state)
        get_integer(fp);
      else
        d->model_number = get_integer(fp);
    }
    else if (!_cups_strcasecmp(temp, "Option"))
    {
      // Get an option...
      ppdcOption *tempo = get_option(fp, d, g);

      if (!tempo)
        break;

      if (cond_state)
      {
        if (!g->find_option(tempo->name->value))
	  tempo->release();
      }
      else
      {
        if (!g->find_option(tempo->name->value))
	  g->add_option(tempo);

        o = tempo;
      }
    }
    else if (!_cups_strcasecmp(temp, "FileName"))
    {
      // FileName name
      char	name[256];		// Filename string


      if (!get_token(fp, name, sizeof(name)))
      {
        _cupsLangPrintf(stderr,
	                _("ppdc: Expected name after FileName on line %d of "
			  "%s."), fp->line, fp->filename);
	break;
      }

      if (!cond_state)
        d->set_file_name(name);
    }
    else if (!_cups_strcasecmp(temp, "PCFileName"))
    {
      // PCFileName name
      char	name[256];		// PC filename string


      if (!get_token(fp, name, sizeof(name)))
      {
        _cupsLangPrintf(stderr,
	                _("ppdc: Expected name after PCFileName on line %d of "
			  "%s."), fp->line, fp->filename);
	break;
      }

      if (!cond_state)
        d->set_pc_file_name(name);
    }
    else if (!_cups_strcasecmp(temp, "Resolution"))
    {
      // Get the resolution choice...
      c = get_resolution(fp);
      if (!c)
        continue;

      if (cond_state)
      {
        c->release();
        continue;
      }

      // Add the choice to the Resolution option...
      if ((o = d->find_option_group("Resolution", &mg)) == NULL)
      {
	// Create the Resolution option...
	o = new ppdcOption(PPDC_PICKONE, "Resolution", NULL, PPDC_SECTION_ANY,
	                   10.0f);
	g = general;
	g->add_option(o);
      }
      else if (mg != general)
      {
	_cupsLangPrintf(stderr,
			_("ppdc: Option %s defined in two different groups on "
			  "line %d of %s."), "Resolution", fp->line,
		        fp->filename);
	c->release();
	continue;
      }

      o->add_choice(c);

      if (isdefault)
	o->set_defchoice(c);

      o = NULL;
    }
    else if (!_cups_strcasecmp(temp, "SimpleColorProfile"))
    {
      ppdcProfile	*p;		// Color profile


      // Get the color profile...
      p = get_simple_profile(fp);

      if (p)
      {
        if (cond_state)
	  p->release();
	else
          d->profiles->add(p);
      }
    }
    else if (!_cups_strcasecmp(temp, "Throughput"))
    {
      // Throughput number
      if (cond_state)
        get_integer(fp);
      else
        d->throughput = get_integer(fp);
    }
    else if (!_cups_strcasecmp(temp, "UIConstraints"))
    {
      ppdcConstraint	*con;		// Constraint


      con = get_constraint(fp);

      if (con)
      {
        if (cond_state)
	  con->release();
	else
	  d->constraints->add(con);
      }
    }
    else if (!_cups_strcasecmp(temp, "VariablePaperSize"))
    {
      // VariablePaperSize boolean
      if (cond_state)
        get_boolean(fp);
      else
	d->variable_paper_size = get_boolean(fp);
    }
    else if (!_cups_strcasecmp(temp, "Version"))
    {
      // Version string
      char	name[256];		// Model name string


      if (!get_token(fp, name, sizeof(name)))
      {
        _cupsLangPrintf(stderr,
	                _("ppdc: Expected string after Version on line %d of "
			  "%s."), fp->line, fp->filename);
	break;
      }

      if (!cond_state)
        d->set_version(name);
    }
    else
    {
      _cupsLangPrintf(stderr,
                      _("ppdc: Unknown token \"%s\" seen on line %d of %s."),
		      temp, fp->line, fp->filename);
      break;
    }
  }

  // Done processing this block, is there anything to save?
  if (!inc)
  {
    if (!d->pc_file_name || !d->model_name || !d->manufacturer || !d->version ||
	!d->sizes->count)
    {
      // Nothing to save...
      d->release();
    }
    else
    {
      // Got a driver, save it...
      drivers->add(d);
    }
  }
  else if (inc && td)
    td->release();
}


//
// 'ppdcSource::set_variable()' - Set a variable.
//

ppdcVariable *				// O - Variable
ppdcSource::set_variable(
    const char *name,			// I - Name
    const char *value)			// I - Value
{
  ppdcVariable	*v;			// Variable


  // See if the variable exists already...
  v = find_variable(name);
  if (v)
  {
    // Change the variable value...
    v->set_value(value);
  }
  else
  {
    // Create a new variable and add it...
    v = new ppdcVariable(name, value);
    vars->add(v);
  }

  return (v);
}


//
// 'ppdcSource::write_file()' - Write the current source data to a file.
//

int					// O - 0 on success, -1 on error
ppdcSource::write_file(const char *f)	// I - File to write
{
  cups_file_t	*fp;			// Output file
  char		bckname[1024];		// Backup file
  ppdcDriver	*d;			// Current driver
  ppdcString	*st;			// Current string
  ppdcAttr	*a;			// Current attribute
  ppdcConstraint *co;			// Current constraint
  ppdcFilter	*fi;			// Current filter
  ppdcFont	*fo;			// Current font
  ppdcGroup	*g;			// Current group
  ppdcOption	*o;			// Current option
  ppdcChoice	*ch;			// Current choice
  ppdcProfile	*p;			// Current color profile
  ppdcMediaSize	*si;			// Current media size
  float		left,			// Current left margin
		bottom,			// Current bottom margin
		right,			// Current right margin
		top;			// Current top margin
  int		dtused[PPDC_DRIVER_MAX];// Driver type usage...


  // Rename the current file, if any, to .bck...
  snprintf(bckname, sizeof(bckname), "%s.bck", f);
  rename(f, bckname);

  // Open the output file...
  fp = cupsFileOpen(f, "w");

  if (!fp)
  {
    // Can't create file; restore backup and return...
    rename(bckname, f);
    return (-1);
  }

  cupsFilePuts(fp, "// CUPS PPD Compiler " CUPS_SVERSION "\n\n");

  // Include standard files...
  cupsFilePuts(fp, "// Include necessary files...\n");
  cupsFilePuts(fp, "#include <font.defs>\n");
  cupsFilePuts(fp, "#include <media.defs>\n");

  memset(dtused, 0, sizeof(dtused));

  for (d = (ppdcDriver *)drivers->first(); d; d = (ppdcDriver *)drivers->next())
    if (d->type > PPDC_DRIVER_PS && !dtused[d->type])
    {
      cupsFilePrintf(fp, "#include <%s.h>\n", driver_types[d->type]);
      dtused[d->type] = 1;
    }

  // Output each driver...
  for (d = (ppdcDriver *)drivers->first(); d; d = (ppdcDriver *)drivers->next())
  {
    // Start the driver...
    cupsFilePrintf(fp, "\n// %s %s\n", d->manufacturer->value,
                   d->model_name->value);
    cupsFilePuts(fp, "{\n");

    // Write the copyright strings...
    for (st = (ppdcString *)d->copyright->first();
         st;
	 st = (ppdcString *)d->copyright->next())
      quotef(fp, "  Copyright \"%s\"\n", st->value);

    // Write other strings and values...
    if (d->manufacturer && d->manufacturer->value)
      quotef(fp, "  Manufacturer \"%s\"\n", d->manufacturer->value);
    if (d->model_name->value)
      quotef(fp, "  ModelName \"%s\"\n", d->model_name->value);
    if (d->file_name && d->file_name->value)
      quotef(fp, "  FileName \"%s\"\n", d->file_name->value);
    if (d->pc_file_name && d->pc_file_name->value)
      quotef(fp, "  PCFileName \"%s\"\n", d->pc_file_name->value);
    if (d->version && d->version->value)
      quotef(fp, "  Version \"%s\"\n", d->version->value);

    cupsFilePrintf(fp, "  DriverType %s\n", driver_types[d->type]);

    if (d->model_number)
    {
      switch (d->type)
      {
	case PPDC_DRIVER_LABEL :
	    cupsFilePuts(fp, "  ModelNumber ");

	    switch (d->model_number)
	    {
	      case DYMO_3x0 :
		  cupsFilePuts(fp, "$DYMO_3x0\n");
		  break;

	      case ZEBRA_EPL_LINE :
		  cupsFilePuts(fp, "$ZEBRA_EPL_LINE\n");
		  break;

	      case ZEBRA_EPL_PAGE :
		  cupsFilePuts(fp, "$ZEBRA_EPL_PAGE\n");
		  break;

	      case ZEBRA_ZPL :
		  cupsFilePuts(fp, "$ZEBRA_ZPL\n");
		  break;

	      case ZEBRA_CPCL :
		  cupsFilePuts(fp, "$ZEBRA_CPCL\n");
		  break;

	      case INTELLITECH_PCL :
		  cupsFilePuts(fp, "$INTELLITECH_PCL\n");
		  break;

	      default :
		  cupsFilePrintf(fp, "%d\n", d->model_number);
		  break;
	    }
	    break;

	case PPDC_DRIVER_EPSON :
	    cupsFilePuts(fp, "  ModelNumber ");

	    switch (d->model_number)
	    {
	      case EPSON_9PIN :
		  cupsFilePuts(fp, "$EPSON_9PIN\n");
		  break;

	      case EPSON_24PIN :
		  cupsFilePuts(fp, "$EPSON_24PIN\n");
		  break;

	      case EPSON_COLOR :
		  cupsFilePuts(fp, "$EPSON_COLOR\n");
		  break;

	      case EPSON_PHOTO :
		  cupsFilePuts(fp, "$EPSON_PHOTO\n");
		  break;

	      case EPSON_ICOLOR :
		  cupsFilePuts(fp, "$EPSON_ICOLOR\n");
		  break;

	      case EPSON_IPHOTO :
		  cupsFilePuts(fp, "$EPSON_IPHOTO\n");
		  break;

	      default :
		  cupsFilePrintf(fp, "%d\n", d->model_number);
	          break;
	    }
	    break;

	case PPDC_DRIVER_HP :
	    cupsFilePuts(fp, "  ModelNumber ");
	    switch (d->model_number)
	    {
	      case HP_LASERJET :
	          cupsFilePuts(fp, "$HP_LASERJET\n");
		  break;

	      case HP_DESKJET :
	          cupsFilePuts(fp, "$HP_DESKJET\n");
		  break;

	      case HP_DESKJET2 :
	          cupsFilePuts(fp, "$HP_DESKJET2\n");
		  break;

	      default :
		  cupsFilePrintf(fp, "%d\n", d->model_number);
		  break;
	    }

	    cupsFilePuts(fp, ")\n");
	    break;

        default :
            cupsFilePrintf(fp, "  ModelNumber %d\n", d->model_number);
	    break;
      }
    }

    if (d->manual_copies)
      cupsFilePuts(fp, "  ManualCopies Yes\n");

    if (d->color_device)
      cupsFilePuts(fp, "  ColorDevice Yes\n");

    if (d->throughput)
      cupsFilePrintf(fp, "  Throughput %d\n", d->throughput);

    // Output all of the attributes...
    for (a = (ppdcAttr *)d->attrs->first();
         a;
	 a = (ppdcAttr *)d->attrs->next())
      if (a->text->value && a->text->value[0])
	quotef(fp, "  Attribute \"%s\" \"%s/%s\" \"%s\"\n",
               a->name->value, a->selector->value ? a->selector->value : "",
	       a->text->value, a->value->value ? a->value->value : "");
      else
	quotef(fp, "  Attribute \"%s\" \"%s\" \"%s\"\n",
               a->name->value, a->selector->value ? a->selector->value : "",
	       a->value->value ? a->value->value : "");

    // Output all of the constraints...
    for (co = (ppdcConstraint *)d->constraints->first();
         co;
	 co = (ppdcConstraint *)d->constraints->next())
    {
      if (co->option1->value[0] == '*')
	cupsFilePrintf(fp, "  UIConstraints \"%s %s", co->option1->value,
		       co->choice1->value ? co->choice1->value : "");
      else
	cupsFilePrintf(fp, "  UIConstraints \"*%s %s", co->option1->value,
		       co->choice1->value ? co->choice1->value : "");

      if (co->option2->value[0] == '*')
	cupsFilePrintf(fp, " %s %s\"\n", co->option2->value,
		       co->choice2->value ? co->choice2->value : "");
      else
	cupsFilePrintf(fp, " *%s %s\"\n", co->option2->value,
		       co->choice2->value ? co->choice2->value : "");
    }

    // Output all of the filters...
    for (fi = (ppdcFilter *)d->filters->first();
         fi;
	 fi = (ppdcFilter *)d->filters->next())
      cupsFilePrintf(fp, "  Filter \"%s %d %s\"\n",
                     fi->mime_type->value, fi->cost, fi->program->value);

    // Output all of the fonts...
    for (fo = (ppdcFont *)d->fonts->first();
         fo;
	 fo = (ppdcFont *)d->fonts->next())
      if (!strcmp(fo->name->value, "*"))
        cupsFilePuts(fp, "  Font *\n");
      else
	cupsFilePrintf(fp, "  Font \"%s\" \"%s\" \"%s\" \"%s\" %s\n",
        	       fo->name->value, fo->encoding->value,
		       fo->version->value, fo->charset->value,
		       fo->status == PPDC_FONT_ROM ? "ROM" : "Disk");

    // Output all options...
    for (g = (ppdcGroup *)d->groups->first();
         g;
	 g = (ppdcGroup *)d->groups->next())
    {
      if (g->options->count == 0)
        continue;

      if (g->text->value && g->text->value[0])
        quotef(fp, "  Group \"%s/%s\"\n", g->name->value, g->text->value);
      else
        cupsFilePrintf(fp, "  Group \"%s\"\n", g->name->value);

      for (o = (ppdcOption *)g->options->first();
           o;
	   o = (ppdcOption *)g->options->next())
      {
        if (o->choices->count == 0)
	  continue;

	if (o->text->value && o->text->value[0])
          quotef(fp, "    Option \"%s/%s\"", o->name->value, o->text->value);
	else
          cupsFilePrintf(fp, "    Option \"%s\"", o->name->value);

        cupsFilePrintf(fp, " %s %s %.1f\n",
		       o->type == PPDC_BOOLEAN ? "Boolean" :
			   o->type == PPDC_PICKONE ? "PickOne" : "PickMany",
		       o->section == PPDC_SECTION_ANY ? "AnySetup" :
			   o->section == PPDC_SECTION_DOCUMENT ? "DocumentSetup" :
			   o->section == PPDC_SECTION_EXIT ? "ExitServer" :
			   o->section == PPDC_SECTION_JCL ? "JCLSetup" :
			   o->section == PPDC_SECTION_PAGE ? "PageSetup" :
			   "Prolog",
		       o->order);

        for (ch = (ppdcChoice *)o->choices->first();
	     ch;
	     ch = (ppdcChoice *)o->choices->next())
	{
	  if (ch->text->value && ch->text->value[0])
            quotef(fp, "      %sChoice \"%s/%s\" \"%s\"\n",
	    	   o->defchoice == ch->name ? "*" : "",
                   ch->name->value, ch->text->value,
		   ch->code->value ? ch->code->value : "");
	  else
            quotef(fp, "      %sChoice \"%s\" \"%s\"\n",
	           o->defchoice == ch->name ? "*" : "",
		   ch->name->value,
		   ch->code->value ? ch->code->value : "");
	}
      }
    }

    // Output all of the color profiles...
    for (p = (ppdcProfile *)d->profiles->first();
         p;
	 p = (ppdcProfile *)d->profiles->next())
      cupsFilePrintf(fp, "  ColorProfile \"%s/%s\" %.3f %.3f "
                	 "%.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n",
        	     p->resolution->value, p->media_type->value,
		     p->density, p->gamma,
		     p->profile[0], p->profile[1], p->profile[2],
		     p->profile[3], p->profile[4], p->profile[5],
		     p->profile[6], p->profile[7], p->profile[8]);

    // Output all of the media sizes...
    left   = 0.0;
    bottom = 0.0;
    right  = 0.0;
    top    = 0.0;

    for (si = (ppdcMediaSize *)d->sizes->first();
         si;
	 si = (ppdcMediaSize *)d->sizes->next())
      if (si->size_code->value && si->region_code->value)
      {
        // Output a custom media size...
	quotef(fp, "  %sCustomMedia \"%s/%s\" %.2f %.2f %.2f %.2f %.2f %.2f \"%s\" \"%s\"\n",
	       si->name == d->default_size ? "*" : "", si->name->value,
	       si->text->value, si->width, si->length, si->left, si->bottom,
	       si->right, si->top, si->size_code->value,
	       si->region_code->value);
      }
      else
      {
        // Output a standard media size...
	if (fabs(left - si->left) > 0.1 ||
            fabs(bottom - si->bottom) > 0.1 ||
            fabs(right - si->right) > 0.1 ||
            fabs(top - si->top) > 0.1)
	{
          cupsFilePrintf(fp, "  HWMargins %.2f %.2f %.2f %.2f\n",
	        	 si->left, si->bottom, si->right, si->top);

          left   = si->left;
	  bottom = si->bottom;
	  right  = si->right;
	  top    = si->top;
	}

	cupsFilePrintf(fp, "  %sMediaSize %s\n",
	               si->name == d->default_size ? "*" : "",
        	       si->name->value);
      }

    if (d->variable_paper_size)
    {
      cupsFilePuts(fp, "  VariablePaperSize Yes\n");

      if (fabs(left - d->left_margin) > 0.1 ||
          fabs(bottom - d->bottom_margin) > 0.1 ||
          fabs(right - d->right_margin) > 0.1 ||
          fabs(top - d->top_margin) > 0.1)
      {
        cupsFilePrintf(fp, "  HWMargins %.2f %.2f %.2f %.2f\n",
	               d->left_margin, d->bottom_margin, d->right_margin,
		       d->top_margin);
      }

      cupsFilePrintf(fp, "  MinSize %.2f %.2f\n", d->min_width, d->min_length);
      cupsFilePrintf(fp, "  MaxSize %.2f %.2f\n", d->max_width, d->max_length);
    }

    // End the driver...
    cupsFilePuts(fp, "}\n");
  }

  // Close the file and return...
  cupsFileClose(fp);

  return (0);
}
