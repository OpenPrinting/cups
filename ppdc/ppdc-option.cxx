//
// Option class for the CUPS PPD Compiler.
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
// 'ppdcOption::ppdcOption()' - Create a new option.
//

ppdcOption::ppdcOption(ppdcOptType    ot,	// I - Option type
                       const char     *n,	// I - Option name
		       const char     *t,	// I - Option text
		       ppdcOptSection s,	// I - Section
                       float          o)	// I - Ordering number
  : ppdcShared()
{
  PPDC_NEW;

  type      = ot;
  name      = new ppdcString(n);
  text      = new ppdcString(t);
  section   = s;
  order     = o;
  choices   = new ppdcArray();
  defchoice = 0;
}


//
// 'ppdcOption::ppdcOption()' - Copy a new option.
//

ppdcOption::ppdcOption(ppdcOption *o)		// I - Template option
{
  PPDC_NEW;

  o->name->retain();
  o->text->retain();
  if (o->defchoice)
    o->defchoice->retain();

  type      = o->type;
  name      = o->name;
  text      = o->text;
  section   = o->section;
  order     = o->order;
  choices   = new ppdcArray(o->choices);
  defchoice = o->defchoice;
}


//
// 'ppdcOption::~ppdcOption()' - Destroy an option.
//

ppdcOption::~ppdcOption()
{
  PPDC_DELETE;

  name->release();
  text->release();
  if (defchoice)
    defchoice->release();
  choices->release();
}


//
// 'ppdcOption::find_choice()' - Find an option choice.
//

ppdcChoice *					// O - Choice or NULL
ppdcOption::find_choice(const char *n)		// I - Name of choice
{
  ppdcChoice	*c;				// Current choice


  for (c = (ppdcChoice *)choices->first(); c; c = (ppdcChoice *)choices->next())
    if (!_cups_strcasecmp(n, c->name->value))
      return (c);

  return (0);
}


//
// 'ppdcOption::set_defchoice()' - Set the default choice.
//

void
ppdcOption::set_defchoice(ppdcChoice *c)	// I - Choice
{
  if (defchoice)
    defchoice->release();

  if (c->name)
    c->name->retain();

  defchoice = c->name;
}
