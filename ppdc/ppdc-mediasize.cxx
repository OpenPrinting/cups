//
// Shared media size class for the CUPS PPD Compiler.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright 2007-2009 by Apple Inc.
// Copyright 2002-2005 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more information.
//

//
// Include necessary headers...
//

#include "ppdc-private.h"


//
// 'ppdcMediaSize::ppdcMediaSize()' - Create a new media size.
//

ppdcMediaSize::ppdcMediaSize(const char *n,	// I - Name of media size
                             const char *t,	// I - Text of media size
			     float      w,	// I - Width in points
			     float      l,	// I - Length in points
                             float      lm,	// I - Left margin in points
			     float      bm,	// I - Bottom margin in points
			     float      rm,	// I - Right margin in points
			     float      tm,	// I - Top margin in points
			     const char *sc,	// I - PageSize code, if any
			     const char *rc)	// I - PageRegion code, if any
  : ppdcShared()
{
  PPDC_NEW;

  name        = new ppdcString(n);
  text        = new ppdcString(t);
  width       = w;
  length      = l;
  left        = lm;
  bottom      = bm;
  right       = rm;
  top         = tm;
  size_code   = new ppdcString(sc);
  region_code = new ppdcString(rc);

  if (left < 0.0f)
    left = 0.0f;
  if (bottom < 0.0f)
    bottom = 0.0f;
  if (right < 0.0f)
    right = 0.0f;
  if (top < 0.0f)
    top = 0.0f;
}


//
// 'ppdcMediaSize::~ppdcMediaSize()' - Destroy a media size.
//

ppdcMediaSize::~ppdcMediaSize()
{
  PPDC_DELETE;

  name->release();
  text->release();
  size_code->release();
  region_code->release();
}
