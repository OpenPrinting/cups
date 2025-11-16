//
// File type conversion routines for CUPS.
//
// Copyright © 2020-2025 by OpenPrinting.
// Copyright © 2007-2011 by Apple Inc.
// Copyright © 1997-2007 by Easy Software Products, all rights reserved.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include <cups/cups.h>
#include <cups/string-private.h>
#include "mime-private.h"


//
// Debug macros that used to be private API...
//

#define DEBUG_puts(x)
#define DEBUG_printf(...)


//
// Local types...
//

typedef struct _mime_typelist_s		// List of source types
{
  struct _mime_typelist_s *next;	// Next source type
  mime_type_t		*src;		// Source type
} _mime_typelist_t;


//
// Local functions...
//

static int		mime_compare_ftypess(mime_ftypes_t *a, mime_ftypes_t *b, void *data);
static int		mime_compare_filters(mime_filter_t *, mime_filter_t *, void *);
static int		mime_compare_srcs(mime_filter_t *, mime_filter_t *, void *);
static mime_ftypes_t	*mime_find_ftypes(mime_t *mime, mime_type_t *dst);
static cups_array_t	*mime_find_filters(mime_t *mime, mime_type_t *src, size_t srcsize, mime_type_t *dst, int *cost, _mime_typelist_t *visited);
static void		mime_free_ftypes(mime_ftypes_t *c, void *data);
static void		mime_free_filter(mime_filter_t *f, void *data);
static cups_array_t	*mime_get_filter_types(mime_t *mime, mime_type_t *dst, cups_array_t *srcs, int level);


//
// 'mimeAddFilter()' - Add a filter to the current MIME database.
//

mime_filter_t *				// O - New filter
mimeAddFilter(mime_t      *mime,	// I - MIME database
              mime_type_t *src,		// I - Source type
	      mime_type_t *dst,		// I - Destination type
              int         cost,		// I - Relative time/resource cost
	      const char  *filter)	// I - Filter program to run
{
  mime_filter_t	*temp;			// New filter
  mime_ftypes_t	*c;			// Filter cache


  DEBUG_printf(("mimeAddFilter(mime=%p, src=%p(%s/%s), dst=%p(%s/%s), cost=%d, "
                "filter=\"%s\")", mime,
		src, src ? src->super : "???", src ? src->type : "???",
		dst, dst ? dst->super : "???", dst ? dst->type : "???",
		cost, filter));

 /*
  * Range-check the input...
  */

  if (!mime || !src || !dst || !filter)
  {
    DEBUG_puts("1mimeAddFilter: Returning NULL.");
    return (NULL);
  }

  // See if we have a cache for this destination type...
  if ((c = mime_find_ftypes(mime, dst)) == NULL)
  {
    // No, add a cache for this type...
    if ((c = (mime_ftypes_t *)calloc(1, sizeof(mime_ftypes_t))) != NULL)
    {
      c->dst = dst;

      cupsRWLockWrite(&mime->lock);
      if (!mime->ftypes)
        mime->ftypes = cupsArrayNew3((cups_array_cb_t)mime_compare_ftypess, /*cb_data*/NULL, /*hash_cb*/NULL, /*hash_size*/0, /*copy_cb*/NULL, (cups_afree_cb_t)mime_free_ftypes);
      cupsArrayAdd(mime->ftypes, c);
      cupsRWUnlock(&mime->lock);
    }
  }

  if (c && !cupsArrayFind(c->srcs, src))
  {
    // Add source type to list of source types that can be converted to the
    // destination type...
    if (!c->srcs)
      c->srcs = cupsArrayNew3((cups_array_cb_t)_mimeCompareTypes, /*cb_data*/NULL, /*hash_cb*/NULL, /*hash_size*/0, /*copy_cb*/NULL, /*free_cb*/NULL);

    cupsArrayAdd(c->srcs, src);
  }

 /*
  * See if we already have an existing filter for the given source and
  * destination...
  */

  if ((temp = mimeFilterLookup(mime, src, dst)) != NULL)
  {
   /*
    * Yup, does the existing filter have a higher cost?  If so, copy the
    * filter and cost to the existing filter entry and return it...
    */

    if (temp->cost > cost)
    {
      DEBUG_printf(("1mimeAddFilter: Replacing filter \"%s\", cost %d.",
                    temp->filter, temp->cost));
      temp->cost = cost;
      cupsCopyString(temp->filter, filter, sizeof(temp->filter));
    }
  }
  else
  {
   /*
    * Nope, add a new one...
    */

    if (!mime->filters)
      mime->filters = cupsArrayNew3((cups_array_cb_t)mime_compare_filters, /*cb_data*/NULL, /*hash_cb*/NULL, /*hash_size*/0, /*copy_cb*/NULL, (cups_afree_cb_t)mime_free_filter);

    if (!mime->filters)
      return (NULL);

    if ((temp = calloc(1, sizeof(mime_filter_t))) == NULL)
      return (NULL);

   /*
    * Copy the information over and sort if necessary...
    */

    temp->src  = src;
    temp->dst  = dst;
    temp->cost = cost;
    cupsCopyString(temp->filter, filter, sizeof(temp->filter));

    DEBUG_puts("1mimeAddFilter: Adding new filter.");
    cupsArrayAdd(mime->filters, temp);
    cupsArrayAdd(mime->srcs, temp);
  }

 /*
  * Return the new/updated filter...
  */

  DEBUG_printf("1mimeAddFilter: Returning %p.", temp);

  return (temp);
}


//
// 'mimeFilter()' - Find the fastest way to convert from one type to another.
//

cups_array_t *				// O - Array of filters to run
mimeFilter(mime_t      *mime,		// I - MIME database
           mime_type_t *src,		// I - Source file type
	   mime_type_t *dst,		// I - Destination file type
	   int         *cost)		// O - Cost of filters
{
  DEBUG_printf(("mimeFilter(mime=%p, src=%p(%s/%s), dst=%p(%s/%s), "
                "cost=%p(%d))", mime,
        	src, src ? src->super : "???", src ? src->type : "???",
		dst, dst ? dst->super : "???", dst ? dst->type : "???",
		cost, cost ? *cost : 0));

  return (mimeFilter2(mime, src, 0, dst, cost));
}


//
// 'mimeFilter2()' - Find the fastest way to convert from one type to another,
//                   including file size.
//

cups_array_t *				// O - Array of filters to run
mimeFilter2(mime_t      *mime,		// I - MIME database
            mime_type_t *src,		// I - Source file type
	    size_t      srcsize,	// I - Size of source file
	    mime_type_t *dst,		// I - Destination file type
	    int         *cost)		// O - Cost of filters
{
  cups_array_t	*filters;		// Array of filters to run


 /*
  * Range-check the input...
  */

  DEBUG_printf(("mimeFilter2(mime=%p, src=%p(%s/%s), srcsize=" CUPS_LLFMT
                ", dst=%p(%s/%s), cost=%p(%d))", mime,
        	src, src ? src->super : "???", src ? src->type : "???",
		CUPS_LLCAST srcsize,
		dst, dst ? dst->super : "???", dst ? dst->type : "???",
		cost, cost ? *cost : 0));

  if (cost)
    *cost = 0;

  if (!mime || !src || !dst)
    return (NULL);

 /*
  * (Re)build the source lookup array as needed...
  */

  if (!mime->srcs)
  {
    mime_filter_t	*current;	// Current filter

    mime->srcs = cupsArrayNew((cups_array_cb_t)mime_compare_srcs, NULL);

    for (current = mimeFirstFilter(mime);
         current;
	 current = mimeNextFilter(mime))
      cupsArrayAdd(mime->srcs, current);
  }

 /*
  * Find the filters...
  */

  filters = mime_find_filters(mime, src, srcsize, dst, cost, NULL);

  DEBUG_printf(("1mimeFilter2: Returning %d filter(s), cost %d:",
                cupsArrayCount(filters), cost ? *cost : -1));
#ifdef DEBUG
  {
    mime_filter_t	*filter;	// Current filter

    for (filter = (mime_filter_t *)cupsArrayFirst(filters);
         filter;
	 filter = (mime_filter_t *)cupsArrayNext(filters))
      DEBUG_printf(("1mimeFilter2: %s/%s %s/%s %d %s", filter->src->super,
                    filter->src->type, filter->dst->super, filter->dst->type,
		    filter->cost, filter->filter));
  }
#endif // DEBUG

  return (filters);
}


//
// 'mimeFilterLookup()' - Lookup a filter.
//

mime_filter_t *				// O - Filter for src->dst
mimeFilterLookup(mime_t      *mime,	// I - MIME database
                 mime_type_t *src,	// I - Source type
                 mime_type_t *dst)	// I - Destination type
{
  mime_filter_t	key,			// Key record for filter search
		*filter;		// Matching filter


  DEBUG_printf("2mimeFilterLookup(mime=%p, src=%p(%s/%s), dst=%p(%s/%s))", mime, src, src ? src->super : "???", src ? src->type : "???", dst, dst ? dst->super : "???", dst ? dst->type : "???");

  key.src = src;
  key.dst = dst;

  filter = (mime_filter_t *)cupsArrayFind(mime->filters, &key);
  DEBUG_printf("3mimeFilterLookup: Returning %p(%s).", filter, filter ? filter->filter : "???");
  return (filter);
}


//
// 'mimeGetFilterTypes()' - Get a list of source MIME media types that can be filtered to a destination type.
//

cups_array_t *				// O - Array of source types or `NULL` for none
mimeGetFilterTypes(mime_t       *mime,	// I - MIME database
		   mime_type_t  *dst,	// I - Destination media type
		   cups_array_t *srcs)	// I - Array of source types or `NULL` for none
{
  // Range check input...
  if (!mime || !dst)
    return (srcs);

  // Get source types...
  return (mime_get_filter_types(mime, dst, srcs, 0));
}


//
// 'mime_compare_ftypess()' - Compare two filter caches.
//

static int				// O - Result of comparison
mime_compare_ftypess(
    mime_ftypes_t *a,			// I - First cache
    mime_ftypes_t *b,			// I - Second cache
    void          *data)		// I - Callback data (not used)
{
  return (_mimeCompareTypes(a->dst, b->dst, data));
}


//
// 'mime_compare_filters()' - Compare two filters.
//

static int                              // O - Comparison result
mime_compare_filters(mime_filter_t *f0, // I - First filter
                     mime_filter_t *f1, // I - Second filter
                     void *data)        // I - Callback data (not nused)
{
  int	ret;				// Result of comparison


  if ((ret = _mimeCompareTypes(f0->src, f1->src, data)) != 0)
    return (ret);
  else
    return (_mimeCompareTypes(f0->dst, f1->dst, data));
}


//
// 'mime_compare_srcs()' - Compare two filter source types.
//

static int				// O - Comparison result
mime_compare_srcs(
    mime_filter_t *f0,			// I - First filter
    mime_filter_t *f1,			// I - Second filter
    void          *data)		// I - Callback data (not used)
{
  return (_mimeCompareTypes(f0->src, f1->src, data));
}


//
// 'mime_find_ftypes()' - Find a filter cache.
//

static mime_ftypes_t *			// O - Matching cache
mime_find_ftypes(mime_t      *mime,	// I - MIME database
                 mime_type_t *dst)	// I - Destination type
{
  mime_ftypes_t	key,			// Search key
		*match;			// Matching cache


  // Lookup the destination type in the array...
  key.dst = dst;

  cupsRWLockRead(&mime->lock);
  match = (mime_ftypes_t *)cupsArrayFind(mime->ftypes, &key);
  cupsRWUnlock(&mime->lock);

  return (match);
}


//
// 'mime_find_filters()' - Find the filters to convert from one type to another.
//

static cups_array_t *			// O - Array of filters to run
mime_find_filters(
    mime_t           *mime,		// I - MIME database
    mime_type_t      *src,		// I - Source file type
    size_t           srcsize,		// I - Size of source file
    mime_type_t      *dst,		// I - Destination file type
    int              *cost,		// O - Cost of filters
    _mime_typelist_t *list)		// I - Source types we've used
{
  int			tempcost,	// Temporary cost
			mincost;	// Current minimum
  cups_array_t		*temp,		// Temporary filter
			*mintemp;	// Current minimum
  mime_filter_t		*current,	// Current filter
			srckey;		// Source type key
  _mime_typelist_t	listnode,	// New list node
			*listptr;	// Pointer in list


  DEBUG_printf("2mime_find_filters(mime=%p, src=%p(%s/%s), srcsize=" CUPS_LLFMT ", dst=%p(%s/%s), cost=%p, list=%p)", mime, src, src->super, src->type, CUPS_LLCAST srcsize, dst, dst->super, dst->type, cost, list);

 /*
  * See if there is a filter that can convert the files directly...
  */

  if ((current = mimeFilterLookup(mime, src, dst)) != NULL &&
      (current->maxsize == 0 || srcsize <= current->maxsize))
  {
   /*
    * Got a direct filter!
    */

    DEBUG_puts("3mime_find_filters: Direct filter found.");

    if ((mintemp = cupsArrayNew(NULL, NULL)) == NULL)
    {
      DEBUG_puts("3mime_find_filters: Returning NULL (out of memory).");
      return (NULL);
    }

    cupsArrayAdd(mintemp, current);

    mincost = current->cost;

    if (!cost)
    {
      DEBUG_printf(("3mime_find_filters: Returning 1 filter, cost %d:",
                    mincost));
      DEBUG_printf(("3mime_find_filters: %s/%s %s/%s %d %s",
                    current->src->super, current->src->type,
                    current->dst->super, current->dst->type,
		    current->cost, current->filter));
      return (mintemp);
    }
  }
  else
  {
   /*
    * No direct filter...
    */

    mintemp = NULL;
    mincost = 9999999;
  }

 /*
  * Initialize this node in the type list...
  */

  listnode.next = list;

 /*
  * OK, now look for filters from the source type to any other type...
  */

  srckey.src = src;

  for (current = (mime_filter_t *)cupsArrayFind(mime->srcs, &srckey);
       current && current->src == src;
       current = (mime_filter_t *)cupsArrayNext(mime->srcs))
  {
   /*
    * See if we have already tried the destination type as a source
    * type (this avoids extra filter looping...)
    */

    mime_type_t *current_dst;		// Current destination type

    if (current->maxsize > 0 && srcsize > current->maxsize)
      continue;

    for (listptr = list, current_dst = current->dst;
	 listptr;
	 listptr = listptr->next)
      if (current_dst == listptr->src)
	break;

    if (listptr)
      continue;

   /*
    * See if we have any filters that can convert from the destination type
    * of this filter to the final type...
    */

    listnode.src = current->src;

    cupsArraySave(mime->srcs);
    temp = mime_find_filters(mime, current->dst, srcsize, dst, &tempcost,
                             &listnode);
    cupsArrayRestore(mime->srcs);

    if (!temp)
      continue;

    if (!cost)
    {
      DEBUG_printf(("3mime_find_filters: Returning %d filter(s), cost %d:",
		    cupsArrayCount(temp), tempcost));

#ifdef DEBUG
      for (current = (mime_filter_t *)cupsArrayFirst(temp);
	   current;
	   current = (mime_filter_t *)cupsArrayNext(temp))
	DEBUG_printf(("3mime_find_filters: %s/%s %s/%s %d %s",
		      current->src->super, current->src->type,
		      current->dst->super, current->dst->type,
		      current->cost, current->filter));
#endif // DEBUG

      return (temp);
    }

   /*
    * Found a match; see if this one is less costly than the last (if
    * any...)
    */

    tempcost += current->cost;

    if (tempcost < mincost)
    {
      cupsArrayDelete(mintemp);

     /*
      * Hey, we got a match!  Add the current filter to the beginning of the
      * filter list...
      */

      mintemp = temp;
      mincost = tempcost;
      cupsArrayInsert(mintemp, current);
    }
    else
      cupsArrayDelete(temp);
  }

  if (mintemp)
  {
   /*
    * Hey, we got a match!
    */

    DEBUG_printf(("3mime_find_filters: Returning %d filter(s), cost %d:",
                  cupsArrayCount(mintemp), mincost));

#ifdef DEBUG
    for (current = (mime_filter_t *)cupsArrayFirst(mintemp);
         current;
	 current = (mime_filter_t *)cupsArrayNext(mintemp))
      DEBUG_printf(("3mime_find_filters: %s/%s %s/%s %d %s",
                    current->src->super, current->src->type,
                    current->dst->super, current->dst->type,
		    current->cost, current->filter));
#endif // DEBUG

    if (cost)
      *cost = mincost;

    return (mintemp);
  }

  DEBUG_puts("3mime_find_filters: Returning NULL (no matches).");

  return (NULL);
}


//
// 'mime_free_ftypes()' - Free a filter cache entry.
//

static void
mime_free_ftypes(mime_ftypes_t *c,	// I - Filter cache data
                 void          *data)	// I - Callback data (not used)
{
  (void)data;

  free(c);
}


//
// 'mime_free_filter()' - Free a filter.
//

static void
mime_free_filter(mime_filter_t *f,	// I - Filter
                 void          *data)	// I - Callback data (not used)
{
  (void)data;

  free(f);
}


//
// 'mime_get_filter_types()' - Get a list of source types for the given destination type.
//

static cups_array_t *			// O - Source types
mime_get_filter_types(
    mime_t       *mime,			// I - MIME database
    mime_type_t  *dst,			// I - Destination type
    cups_array_t *srcs,			// I - Source types
    int          level)			// I - Recursion level
{
  mime_ftypes_t	*c;			// Filter cache data
  int		i,			// Current source type
		count;			// Number of source types
  mime_type_t	*src;			// Source type, if any


  // Lookup filters that produce the destination format...
  if ((c = mime_find_ftypes(mime, dst)) != NULL)
  {
    // Add all of the source types that can be converted to this destination type...
    for (i = 0, count = cupsArrayGetCount(c->srcs); i < count; i ++)
    {
      src = (mime_type_t *)cupsArrayGetElement(c->srcs, i);

      if (!strcmp(src->super, "printer"))
      {
	if (level < 4)
	{
	  // Add filters that can convert to this type...
	  srcs = mime_get_filter_types(mime, src, srcs, level + 1);
	}

        continue;
      }

      if (!cupsArrayFind(srcs, src))
      {
	// Make sure we have the source types array...
	if (!srcs)
	  srcs = cupsArrayNew3((cups_array_cb_t)_mimeCompareTypes, /*cb_data*/NULL, /*hash_cb*/NULL, /*hash_size*/0, /*copy_cb*/NULL, /*free_cb*/NULL);

        // Add the source to the array...
	cupsArrayAdd(srcs, src);

	if (level < 4)
	{
	  // Add filters that can convert to this type...
	  srcs = mime_get_filter_types(mime, src, srcs, level + 1);
	}
      }
    }
  }

  return (srcs);
}
