//
// PPD file import methods for the CUPS PPD Compiler.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright 2007-2011 by Apple Inc.
// Copyright 2002-2006 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more information.
//

//
// Include necessary headers...
//

#include "ppdc-private.h"
#include <cups/ppd.h>


//
// 'ppdcSource::import_ppd()' - Import a PPD file.
//

int					// O - 1 on success, 0 on failure
ppdcSource::import_ppd(const char *f)	// I - Filename
{
  int		i, j, k;		// Looping vars
  cups_file_t	*fp;			// File
  char		line[256],		// Comment line
		*ptr;			// Pointer into line
  int		cost;			// Cost for filter
  ppd_file_t	*ppd;			// PPD file data
  ppd_group_t	*group;			// PPD group
  ppd_option_t	*option;		// PPD option
  ppd_choice_t	*choice;		// PPD choice
  ppd_attr_t	*attr;			// PPD attribute
  ppd_const_t	*constraint;		// PPD UI constraint
  ppd_const_t	*constraint2;		// Temp PPD UI constraint
  ppd_size_t	*size;			// PPD page size
  ppdcDriver	*driver;		// Driver
  ppdcFilter	*filter;		// Current filter
  ppdcFont	*font;			// Font
  ppdcGroup	*cgroup;		// UI group
  ppdcOption	*coption;		// UI option
  ppdcChoice	*cchoice;		// UI choice
  ppdcConstraint *cconstraint;		// UI constraint
  ppdcMediaSize	*csize;			// Media size


  // Try opening the PPD file...
  if ((ppd = ppdOpenFile(f)) == NULL)
    return (0);

  // All PPD files need a PCFileName attribute...
  if (!ppd->pcfilename)
  {
    ppdClose(ppd);
    return (0);
  }

  // See if the driver has already been imported...
  if (find_driver(ppd->pcfilename))
  {
    ppdClose(ppd);
    return (1);
  }

  // Create a new PPD file...
  if ((fp = cupsFileOpen(f, "r")) == NULL)
  {
    ppdClose(ppd);
    return (0);
  }

  driver       = new ppdcDriver();
  driver->type = PPDC_DRIVER_PS;

  drivers->add(driver);

  // Read the initial comments from the PPD file and use them as the
  // copyright/license text...
  cupsFileGets(fp, line, sizeof(line));
				      // Skip *PPD-Adobe-M.m

  while (cupsFileGets(fp, line, sizeof(line)))
    if (strncmp(line, "*%", 2))
      break;
    else if (strncmp(line, "*%%%% ", 6))
    {
      for (ptr = line + 2; isspace(*ptr); ptr ++);

      driver->add_copyright(ptr);
    }

  cupsFileClose(fp);

  // Then add the stuff from the PPD file...
  if (ppd->modelname && ppd->manufacturer &&
      !_cups_strncasecmp(ppd->modelname, ppd->manufacturer,
		   strlen(ppd->manufacturer)))
  {
    ptr = ppd->modelname + strlen(ppd->manufacturer);

    while (isspace(*ptr))
      ptr ++;
  }
  else
    ptr = ppd->modelname;

  if (ppd->nickname)
    driver->add_attr(new ppdcAttr("NickName", NULL, NULL, ppd->nickname));

  if (ppd->shortnickname)
    driver->add_attr(new ppdcAttr("ShortNickName", NULL, NULL,
				  ppd->shortnickname));

  driver->manufacturer        = new ppdcString(ppd->manufacturer);
  driver->model_name          = new ppdcString(ptr);
  driver->pc_file_name        = new ppdcString(ppd->pcfilename);
  attr = ppdFindAttr(ppd, "FileVersion", NULL);
  driver->version             = new ppdcString(attr ? attr->value : NULL);
  driver->model_number        = ppd->model_number;
  driver->manual_copies       = ppd->manual_copies;
  driver->color_device        = ppd->color_device;
  driver->throughput          = ppd->throughput;
  driver->variable_paper_size = ppd->variable_sizes;
  driver->max_width           = ppd->custom_max[0];
  driver->max_length          = ppd->custom_max[1];
  driver->min_width           = ppd->custom_min[0];
  driver->min_length          = ppd->custom_min[1];
  driver->left_margin         = ppd->custom_margins[0];
  driver->bottom_margin       = ppd->custom_margins[1];
  driver->right_margin        = ppd->custom_margins[2];
  driver->top_margin          = ppd->custom_margins[3];

  for (i = 0; i < ppd->num_filters; i ++)
  {
    strlcpy(line, ppd->filters[i], sizeof(line));

    for (ptr = line; *ptr; ptr ++)
      if (isspace(*ptr & 255))
	break;
    *ptr++ = '\0';

    cost = strtol(ptr, &ptr, 10);

    while (isspace(*ptr & 255))
      ptr ++;

    filter = new ppdcFilter(line, ptr, cost);
    driver->add_filter(filter);
  }

  attr = ppdFindAttr(ppd, "DefaultFont", NULL);
  driver->default_font  = new ppdcString(attr ? attr->value : NULL);

  // Collect media sizes...
  ppd_option_t	*region_option,		// PageRegion option
		      *size_option;		// PageSize option
  ppd_choice_t	*region_choice,		// PageRegion choice
		      *size_choice;		// PageSize choice

  region_option = ppdFindOption(ppd, "PageRegion");
  size_option   = ppdFindOption(ppd, "PageSize");

  for (i = ppd->num_sizes, size = ppd->sizes; i > 0; i --, size ++)
  {
    // Don't do custom size here...
    if (!_cups_strcasecmp(size->name, "Custom"))
      continue;

    // Get the code for the PageSize and PageRegion options...
    region_choice = ppdFindChoice(region_option, size->name);
    size_choice   = ppdFindChoice(size_option, size->name);

    // Create a new media size record and add it to the driver...
    csize = new ppdcMediaSize(size->name, size_choice->text, size->width,
			      size->length, size->left, size->bottom,
			      size->width - size->right,
			      size->length - size->top,
			      size_choice->code, region_choice->code);

     driver->add_size(csize);

     if (!_cups_strcasecmp(size_option->defchoice, size->name))
       driver->set_default_size(csize);
  }

  // Now all of the options...
  for (i = ppd->num_groups, group = ppd->groups; i > 0; i --, group ++)
  {
    cgroup = new ppdcGroup(group->name, group->text);
    driver->add_group(cgroup);

    for (j = group->num_options, option = group->options; j > 0; j --, option ++)
    {
      if (!strcmp(option->keyword, "PageSize") || !strcmp(option->keyword, "PageRegion"))
	continue;

      coption = new ppdcOption((ppdcOptType)option->ui, option->keyword,
			       option->text, (ppdcOptSection)option->section,
			       option->order);
      cgroup->add_option(coption);

      for (k = option->num_choices, choice = option->choices; k > 0; k --, choice ++)
      {
	if (!strcmp(choice->choice, "Custom"))
	  continue;

	cchoice = new ppdcChoice(choice->choice, choice->text, choice->code);
	coption->add_choice(cchoice);

	if (!_cups_strcasecmp(option->defchoice, choice->choice))
	  coption->set_defchoice(cchoice);
      }
    }
  }

  // Now the constraints...
  for (i = ppd->num_consts, constraint = ppd->consts;
       i > 0;
       i --, constraint ++)
  {
    // Look for mirrored constraints...
    for (j = i - 1, constraint2 = constraint + 1;
	 j > 0;
	 j --, constraint2 ++)
      if (!strcmp(constraint->option1, constraint2->option2) &&
	  !strcmp(constraint->choice1, constraint2->choice2) &&
	  !strcmp(constraint->option2, constraint2->option1) &&
	  !strcmp(constraint->choice2, constraint2->choice1))
	break;

    if (j)
      continue;

    cconstraint = new ppdcConstraint(constraint->option2, constraint->choice2,
				     constraint->option1, constraint->choice1);
    driver->add_constraint(cconstraint);
  }

  for (i = 0; i < ppd->num_attrs; i ++)
  {
    attr = ppd->attrs[i];

    if (!strcmp(attr->name, "Font"))
    {
      // Font...
      char		encoding[256],	// Encoding string
		      version[256],	// Version string
		      charset[256],	// Charset string
		      status[256];	// Status string
      ppdcFontStatus	fstatus;	// Status enumeration


      if (sscanf(attr->value, "%s%*[^\"]\"%[^\"]\"%s%s", encoding, version,
		 charset, status) != 4)
      {
	_cupsLangPrintf(stderr, _("ppdc: Bad font attribute: %s"),
			attr->value);
	continue;
      }

      if (!strcmp(status, "ROM"))
	fstatus = PPDC_FONT_ROM;
      else
	fstatus = PPDC_FONT_DISK;

      font = new ppdcFont(attr->spec, encoding, version, charset, fstatus);

      driver->add_font(font);
    }
    else if (!strcmp(attr->name, "CustomPageSize"))
    {
      driver->set_custom_size_code(attr->value);
    }
    else if ((strncmp(attr->name, "Default", 7) ||
	      !strcmp(attr->name, "DefaultColorSpace")) &&
	     strcmp(attr->name, "ColorDevice") &&
	     strcmp(attr->name, "Manufacturer") &&
	     strcmp(attr->name, "ModelName") &&
	     strcmp(attr->name, "MaxMediaHeight") &&
	     strcmp(attr->name, "MaxMediaWidth") &&
	     strcmp(attr->name, "NickName") &&
	     strcmp(attr->name, "ParamCustomPageSize") &&
	     strcmp(attr->name, "ShortNickName") &&
	     strcmp(attr->name, "Throughput") &&
	     strcmp(attr->name, "PCFileName") &&
	     strcmp(attr->name, "FileVersion") &&
	     strcmp(attr->name, "FormatVersion") &&
	     strcmp(attr->name, "HWMargins") &&
	     strcmp(attr->name, "VariablePaperSize") &&
	     strcmp(attr->name, "LanguageEncoding") &&
	     strcmp(attr->name, "LanguageVersion") &&
	     strcmp(attr->name, "cupsFilter") &&
	     strcmp(attr->name, "cupsFlipDuplex") &&
	     strcmp(attr->name, "cupsLanguages") &&
	     strcmp(attr->name, "cupsManualCopies") &&
	     strcmp(attr->name, "cupsModelNumber") &&
	     strcmp(attr->name, "cupsVersion"))
    {
      if ((ptr = strchr(attr->name, '.')) != NULL &&
	  ((ptr - attr->name) == 2 || (ptr - attr->name) == 5))
      {
	// Might be a localization attribute; test further...
	if (isalpha(attr->name[0] & 255) &&
	    isalpha(attr->name[1] & 255) &&
	    (attr->name[2] == '.' ||
	     (attr->name[2] == '_' && isalpha(attr->name[3] & 255) &&
	      isalpha(attr->name[4] & 255))))
	  continue;
      }

      // Attribute...
      driver->add_attr(new ppdcAttr(attr->name, attr->spec, attr->text,
				    attr->value));
    }
    else if (!strncmp(attr->name, "Default", 7) &&
	     !ppdFindOption(ppd, attr->name + 7) &&
	     strcmp(attr->name, "DefaultFont") &&
	     strcmp(attr->name, "DefaultImageableArea") &&
	     strcmp(attr->name, "DefaultPaperDimension") &&
	     strcmp(attr->name, "DefaultFont"))
    {
      // Default attribute...
      driver->add_attr(new ppdcAttr(attr->name, attr->spec, attr->text,
				    attr->value));
    }
  }

  ppdClose(ppd);

  return (1);
}
