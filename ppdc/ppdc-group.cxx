//
// Group class for the CUPS PPD Compiler.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright 2007-2011 by Apple Inc.
// Copyright 2002-2005 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more information.
//

//
// Include necessary headers...
//

#include "ppdc-private.h"


//
// 'ppdcGroup::ppdcGroup()' - Create a new group.
//

ppdcGroup::ppdcGroup(const char *n,	// I - Name of group
                     const char *t)	// I - Text of group
{
  PPDC_NEWVAL(n);

  name    = new ppdcString(n);
  text    = new ppdcString(t);
  options = new ppdcArray();
}


//
// 'ppdcGroup::ppdcGroup()' - Copy a new group.
//

ppdcGroup::ppdcGroup(ppdcGroup *g)	// I - Group template
{
  PPDC_NEWVAL(g->name->value);

  g->name->retain();
  g->text->retain();

  name = g->name;
  text = g->text;

  options = new ppdcArray();
  for (ppdcOption *o = (ppdcOption *)g->options->first();
       o;
       o = (ppdcOption *)g->options->next())
    options->add(new ppdcOption(o));
}


//
// 'ppdcGroup::~ppdcGroup()' - Destroy a group.
//

ppdcGroup::~ppdcGroup()
{
  PPDC_DELETEVAL(name ? name->value : NULL);

  name->release();
  text->release();
  options->release();

  name = text = 0;
  options = 0;
}


//
// 'ppdcGroup::find_option()' - Find an option in a group.
//

ppdcOption *
ppdcGroup::find_option(const char *n)	// I - Name of option
{
  ppdcOption	*o;			// Current option


  for (o = (ppdcOption *)options->first(); o; o = (ppdcOption *)options->next())
    if (!_cups_strcasecmp(n, o->name->value))
      return (o);

  return (0);
}
