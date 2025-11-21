//
// MIME database file routines for CUPS.
//
// Copyright © 2020-2025 by OpenPrinting.
// Copyright © 2007-2014 by Apple Inc.
// Copyright © 1997-2006 by Easy Software Products, all rights reserved.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include <cups/cups.h>
#include <cups/string-private.h>
#include <cups/dir.h>
#include "mime-private.h"


//
// Local types...
//

typedef struct _mime_fcache_s		// Filter cache structure
{
  char	*name,				// Filter name
	*path;				// Full path to filter if available
} _mime_fcache_t;


//
// Local functions...
//

static const char *mime_add_fcache(cups_array_t *filtercache, const char *name, const char *filterpath);
static int	mime_compare_fcache(_mime_fcache_t *a, _mime_fcache_t *b, void *data);
static void	mime_delete_fcache(cups_array_t *filtercache);
static void	mime_load_convs(mime_t *mime, const char *filename, const char *filterpath, cups_array_t *filtercache);
static void	mime_load_types(mime_t *mime, const char *filename);


//
// 'mimeDelete()' - Delete (free) a MIME database.
//

void
mimeDelete(mime_t *mime)		// I - MIME database
{
  MIME_DEBUG("mimeDelete(mime=%p)\n", (void *)mime);

  if (!mime)
    return;

  // Free the types and filters arrays, and then the MIME database structure.
  cupsArrayDelete(mime->types);
  cupsArrayDelete(mime->filters);
  cupsArrayDelete(mime->ftypes);
  cupsArrayDelete(mime->srcs);
  free(mime);
}


//
// 'mimeDeleteFilter()' - Delete a filter from the MIME database.
//

void
mimeDeleteFilter(mime_t        *mime,	// I - MIME database
		 mime_filter_t *filter)	// I - Filter
{
  MIME_DEBUG("mimeDeleteFilter(mime=%p, filter=%p(%s/%s->%s/%s, cost=%d, maxsize=" CUPS_LLFMT "))\n", (void *)mime, (void *)filter, filter ? filter->src->super : "???", filter ? filter->src->type : "???", filter ? filter->dst->super : "???", filter ? filter->dst->super : "???", filter ? filter->cost : -1, filter ? CUPS_LLCAST filter->maxsize : CUPS_LLCAST -1);

  if (!mime || !filter)
    return;

#ifdef DEBUG
  if (!cupsArrayFind(mime->filters, filter))
    MIME_DEBUG("mimeDeleteFilter: Filter not in MIME database.\n");
#endif // DEBUG

  cupsArrayRemove(mime->filters, filter);

  // Deleting a filter invalidates the source and destination lookup caches...
  if (mime->srcs)
  {
    MIME_DEBUG("mimeDeleteFilter: Deleting source lookup cache.\n");
    cupsArrayDelete(mime->srcs);
    mime->srcs = NULL;
  }

  if (mime->ftypes)
  {
    MIME_DEBUG("mimeDeleteFilter: Deleting destination lookup cache.\n");
    cupsArrayDelete(mime->ftypes);
    mime->ftypes = NULL;
  }
}


//
// 'mimeDeleteType()' - Delete a type from the MIME database.
//

void
mimeDeleteType(mime_t      *mime,	// I - MIME database
	       mime_type_t *mt)		// I - Type
{
  MIME_DEBUG("mimeDeleteType(mime=%p, mt=%p(%s/%s))\n", (void *)mime, (void *)mt, mt ? mt->super : "???", mt ? mt->type : "???");

  if (!mime || !mt)
    return;

#ifdef DEBUG
  if (!cupsArrayFind(mime->types, mt))
    MIME_DEBUG("mimeDeleteFilter: Type not in MIME database.\n");
#endif // DEBUG

  cupsArrayRemove(mime->types, mt);
}


//
// '_mimeError()' - Show an error message.
//

void
_mimeError(mime_t     *mime,		// I - MIME database
           const char *message,		// I - Printf-style message string
	   ...)				// I - Additional arguments as needed
{
  va_list	ap;			// Argument pointer
  char		buffer[8192];		// Message buffer


  if (mime->error_cb)
  {
    va_start(ap, message);
    vsnprintf(buffer, sizeof(buffer), message, ap);
    va_end(ap);

    (*mime->error_cb)(mime->error_ctx, buffer);
  }
}


//
// 'mimeFirstFilter()' - Get the first filter in the MIME database.
//

mime_filter_t *				// O - Filter or NULL
mimeFirstFilter(mime_t *mime)		// I - MIME database
{
  MIME_DEBUG("mimeFirstFilter(mime=%p)\n", (void *)mime);

  if (!mime)
  {
    MIME_DEBUG("mimeFirstFilter: Returning NULL.\n");
    return (NULL);
  }
  else
  {
    mime_filter_t *first = (mime_filter_t *)cupsArrayGetFirst(mime->filters);
					// First filter

    MIME_DEBUG("mimeFirstFilter: Returning %p.\n", (void *)first);
    return (first);
  }
}


//
// 'mimeFirstType()' - Get the first type in the MIME database.
//

mime_type_t *				// O - Type or NULL
mimeFirstType(mime_t *mime)		// I - MIME database
{
  MIME_DEBUG("mimeFirstType(mime=%p)\n", (void *)mime);

  if (!mime)
  {
    MIME_DEBUG("mimeFirstType: Returning NULL.\n");
    return (NULL);
  }
  else
  {
    mime_type_t *first = (mime_type_t *)cupsArrayGetFirst(mime->types);
					// First type

    MIME_DEBUG("mimeFirstType: Returning %p.\n", (void *)first);
    return (first);
  }
}


//
// 'mimeLoad()' - Create a new MIME database from disk.
//
// This function uses @link mimeLoadFilters@ and @link mimeLoadTypes@ to
// create a MIME database from a single directory.
//

mime_t *				// O - New MIME database
mimeLoad(const char *pathname,		// I - Directory to load
         const char *filterpath)	// I - Directory to load
{
  mime_t *mime;				// New MIME database

  MIME_DEBUG("mimeLoad(pathname=\"%s\", filterpath=\"%s\")\n", pathname, filterpath);

  mime = mimeLoadFilters(mimeLoadTypes(NULL, pathname), pathname, filterpath);
  MIME_DEBUG("mimeLoad: Returning %p.\n", (void *)mime);

  return (mime);
}


//
// 'mimeLoadFilters()' - Load filter definitions from disk.
//
// This function loads all of the .convs files from the specified directory.
// Use @link mimeLoadTypes@ to load all types before you load the filters.
//

mime_t *				// O - MIME database
mimeLoadFilters(mime_t     *mime,	// I - MIME database
                const char *pathname,	// I - Directory to load from
                const char *filterpath)	// I - Default filter program directory
{
  cups_dir_t	*dir;			// Directory
  cups_dentry_t	*dent;			// Directory entry
  char		filename[1024];		// Full filename of .convs file
  cups_array_t	*filtercache;		// Filter cache


  MIME_DEBUG("mimeLoadFilters(mime=%p, pathname=\"%s\", filterpath=\"%s\")\n", (void *)mime, pathname, filterpath);

  // Range check input...
  if (!mime || !pathname || !filterpath)
  {
    MIME_DEBUG("mimeLoadFilters: Bad arguments.\n");
    return (mime);
  }

  // Then open the directory specified by pathname...
  if ((dir = cupsDirOpen(pathname)) == NULL)
  {
    MIME_DEBUG("mimeLoadFilters: Unable to open \"%s\": %s\n", pathname, strerror(errno));
    _mimeError(mime, "Unable to open \"%s\": %s", pathname, strerror(errno));
    return (mime);
  }

  // Read all the .convs files...
  filtercache = cupsArrayNew((cups_array_func_t)mime_compare_fcache, NULL);

  while ((dent = cupsDirRead(dir)) != NULL)
  {
    if (strlen(dent->filename) > 6 && !strcmp(dent->filename + strlen(dent->filename) - 6, ".convs"))
    {
      // Load a mime.convs file...
      snprintf(filename, sizeof(filename), "%s/%s", pathname, dent->filename);
      MIME_DEBUG("mimeLoadFilters: Loading \"%s\".\n", filename);
      mime_load_convs(mime, filename, filterpath, filtercache);
    }
  }

  mime_delete_fcache(filtercache);

  cupsDirClose(dir);

  return (mime);
}


//
// 'mimeLoadTypes()' - Load type definitions from disk.
//
// This function loads all of the .types files from the specified directory.
// Use @link mimeLoadFilters@ to load all filters after you load the types.
//

mime_t *				// O - MIME database
mimeLoadTypes(mime_t     *mime,		// I - MIME database or @code NULL@ to create a new one
              const char *pathname)	// I - Directory to load from
{
  cups_dir_t	*dir;			// Directory
  cups_dentry_t	*dent;			// Directory entry
  char		filename[1024];		// Full filename of .types file


  MIME_DEBUG("mimeLoadTypes(mime=%p, pathname=\"%s\")\n", (void *)mime, pathname);

  // First open the directory specified by pathname...
  if ((dir = cupsDirOpen(pathname)) == NULL)
  {
    MIME_DEBUG("mimeLoadTypes: Unable to open \"%s\": %s\n", pathname, strerror(errno));
    MIME_DEBUG("mimeLoadTypes: Returning %p.\n", (void *)mime);
    _mimeError(mime, "Unable to open \"%s\": %s", pathname, strerror(errno));
    return (mime);
  }

  // If "mime" is NULL, make a new, empty database...
  if (!mime)
    mime = mimeNew();

  if (!mime)
  {
    cupsDirClose(dir);
    MIME_DEBUG("mimeLoadTypes: Returning NULL.\n");
    return (NULL);
  }

  // Read all the .types files...
  while ((dent = cupsDirRead(dir)) != NULL)
  {
    if (strlen(dent->filename) > 6 && !strcmp(dent->filename + strlen(dent->filename) - 6, ".types"))
    {
      // Load a mime.types file...
      snprintf(filename, sizeof(filename), "%s/%s", pathname, dent->filename);
      MIME_DEBUG("mimeLoadTypes: Loading \"%s\".\n", filename);
      mime_load_types(mime, filename);
    }
  }

  cupsDirClose(dir);

  MIME_DEBUG("mimeLoadTypes: Returning %p.\n", (void *)mime);

  return (mime);
}


//
// 'mimeNew()' - Create a new, empty MIME database.
//

mime_t *				// O - MIME database
mimeNew(void)
{
  return ((mime_t *)calloc(1, sizeof(mime_t)));
}


//
// 'mimeNextFilter()' - Get the next filter in the MIME database.
//

mime_filter_t *				// O - Filter or NULL
mimeNextFilter(mime_t *mime)		// I - MIME database
{
  MIME_DEBUG("mimeNextFilter(mime=%p)\n", (void *)mime);

  if (!mime)
  {
    MIME_DEBUG("mimeNextFilter: Returning NULL.\n");
    return (NULL);
  }
  else
  {
    mime_filter_t *next = (mime_filter_t *)cupsArrayGetNext(mime->filters);
					// Next filter

    MIME_DEBUG("mimeNextFilter: Returning %p.\n", (void *)next);
    return (next);
  }
}


//
// 'mimeNextType()' - Get the next type in the MIME database.
//

mime_type_t *				// O - Type or NULL
mimeNextType(mime_t *mime)		// I - MIME database
{
  MIME_DEBUG("mimeNextType(mime=%p)\n", (void *)mime);

  if (!mime)
  {
    MIME_DEBUG("mimeNextType: Returning NULL.\n");
    return (NULL);
  }
  else
  {
    mime_type_t *next = (mime_type_t *)cupsArrayGetNext(mime->types);
					// Next type

    MIME_DEBUG("mimeNextType: Returning %p.\n", (void *)next);
    return (next);
  }
}


//
// 'mimeNumFilters()' - Get the number of filters in a MIME database.
//

int
mimeNumFilters(mime_t *mime)		// I - MIME database
{
  MIME_DEBUG("mimeNumFilters(mime=%p\n)", (void *)mime);

  if (!mime)
  {
    MIME_DEBUG("mimeNumFilters: Returning 0.\n");
    return (0);
  }
  else
  {
    MIME_DEBUG("mimeNumFilters: Returning %d.\n", (int)cupsArrayGetCount(mime->filters));
    return ((int)cupsArrayGetCount(mime->filters));
  }
}


//
// 'mimeNumTypes()' - Get the number of types in a MIME database.
//

int
mimeNumTypes(mime_t *mime)		// I - MIME database
{
  MIME_DEBUG("mimeNumTypes(mime=%p\n)", (void *)mime);

  if (!mime)
  {
    MIME_DEBUG("mimeNumTypes: Returning 0.\n");
    return (0);
  }
  else
  {
    MIME_DEBUG("mimeNumTypes: Returning %d.\n", (int)cupsArrayGetCount(mime->types));
    return ((int)cupsArrayGetCount(mime->types));
  }
}


//
// 'mimeSetErrorCallback()' - Set the callback for error messages.
//

void
mimeSetErrorCallback(
    mime_t          *mime,		// I - MIME database
    mime_error_cb_t cb,			// I - Callback function
    void            *ctx)		// I - Context pointer for callback
{
  if (mime)
  {
    mime->error_cb  = cb;
    mime->error_ctx = ctx;
  }
}


//
// 'mime_add_fcache()' - Add a filter to the filter cache.
//

static const char *			// O - Full path to filter or NULL
mime_add_fcache(
    cups_array_t *filtercache,		// I - Filter cache
    const char   *name,			// I - Filter name
    const char   *filterpath)		// I - Filter path
{
  _mime_fcache_t	key,		// Search key
			*temp;		// New filter cache
  char			path[1024];	// Full path to filter


  MIME_DEBUG("mime_add_fcache(filtercache=%p, name=\"%s\", filterpath=\"%s\")", filtercache, name, filterpath);

  key.name = (char *)name;
  if ((temp = (_mime_fcache_t *)cupsArrayFind(filtercache, &key)) != NULL)
  {
    MIME_DEBUG("mime_add_fcache: Returning \"%s\".\n", temp->path);
    return (temp->path);
  }

  if ((temp = calloc(1, sizeof(_mime_fcache_t))) == NULL)
  {
    MIME_DEBUG("mime_add_fcache: Returning NULL.\n");
    return (NULL);
  }

  temp->name = strdup(name);

  if (cupsFileFind(name, filterpath, 1, path, sizeof(path)))
    temp->path = strdup(path);

  cupsArrayAdd(filtercache, temp);

  MIME_DEBUG("mime_add_fcache: Returning \"%s\".\n", temp->path);
  return (temp->path);
}


//
// 'mime_compare_fcache()' - Compare two filter cache entries.
//

static int				// O - Result of comparison
mime_compare_fcache(
    _mime_fcache_t *a,			// I - First entry
    _mime_fcache_t *b,			// I - Second entry
    void           *data)		// I - Callback data (not used)
{
  (void)data;

  return (strcmp(a->name, b->name));
}


//
// 'mime_delete_fcache()' - Free all memory used by the filter cache.
//

static void
mime_delete_fcache(
    cups_array_t *filtercache)		// I - Filter cache
{
  _mime_fcache_t	*current;	// Current cache entry


  MIME_DEBUG("mime_delete_fcache(filtercache=%p)\n", (void *)filtercache);

  for (current = (_mime_fcache_t *)cupsArrayGetFirst(filtercache); current; current = (_mime_fcache_t *)cupsArrayGetNext(filtercache))
  {
    free(current->name);
    free(current->path);
    free(current);
  }

  cupsArrayDelete(filtercache);
}


//
// 'mime_load_convs()' - Load a xyz.convs file.
//

static void
mime_load_convs(
    mime_t       *mime,			// I - MIME database
    const char   *filename,		// I - Convs file to load
    const char   *filterpath,		// I - Path for filters
    cups_array_t *filtercache)		// I - Filter program cache
{
  cups_file_t	*fp;			// Convs file
  char		line[1024],		// Input line from file
		*lineptr,		// Current position in line
		super[MIME_MAX_SUPER],	// Super-type name
		type[MIME_MAX_TYPE],	// Type name
		*temp,			// Temporary pointer
		*filter;		// Filter program
  mime_type_t	*temptype,		// MIME type looping var
		*dsttype;		// Destination MIME type
  int		cost;			// Cost of filter


  MIME_DEBUG("mime_load_convs(mime=%p, filename=\"%s\", filterpath=\"%s\", filtercache=%p)\n", (void *)mime, filename, filterpath, (void *)filtercache);

  // First try to open the file...
  if ((fp = cupsFileOpen(filename, "r")) == NULL)
  {
    MIME_DEBUG("mime_load_convs: Unable to open \"%s\": %s\n", filename, strerror(errno));
    _mimeError(mime, "Unable to open \"%s\": %s", filename, strerror(errno));
    return;
  }

  // Then read each line from the file, skipping any comments in the file...
  while (cupsFileGets(fp, line, sizeof(line)) != NULL)
  {
    // Skip blank lines and lines starting with a #...
    if (!line[0] || line[0] == '#')
      continue;

    // Strip trailing whitespace...
    for (lineptr = line + strlen(line) - 1; lineptr >= line && isspace(*lineptr & 255); lineptr --)
      *lineptr = '\0';

    // Extract the destination super-type and type names from the middle of
    // the line.
    lineptr = line;
    while (*lineptr != ' ' && *lineptr != '\t' && *lineptr != '\0')
      lineptr ++;

    while (*lineptr == ' ' || *lineptr == '\t')
      lineptr ++;

    temp = super;

    while (*lineptr != '/' && *lineptr != '\n' && *lineptr != '\0' && (temp - super + 1) < MIME_MAX_SUPER)
      *temp++ = (char)tolower(*lineptr++ & 255);

    *temp = '\0';

    if (*lineptr != '/')
      continue;

    lineptr ++;
    temp = type;

    while (*lineptr != ' ' && *lineptr != '\t' && *lineptr != '\n' && *lineptr != '\0' && (temp - type + 1) < MIME_MAX_TYPE)
      *temp++ = (char)tolower(*lineptr++ & 255);

    *temp = '\0';

    if (*lineptr == '\0' || *lineptr == '\n')
      continue;

    if ((dsttype = mimeType(mime, super, type)) == NULL)
    {
      MIME_DEBUG("mime_load_convs: Destination type %s/%s not found.\n", super, type);
      continue;
    }

    // Then get the cost and filter program...
    while (*lineptr == ' ' || *lineptr == '\t')
      lineptr ++;

    if (*lineptr < '0' || *lineptr > '9')
      continue;

    cost = atoi(lineptr);

    while (*lineptr != ' ' && *lineptr != '\t' && *lineptr != '\0')
      lineptr ++;
    while (*lineptr == ' ' || *lineptr == '\t')
      lineptr ++;

    if (*lineptr == '\0' || *lineptr == '\n')
      continue;

    filter = lineptr;

    if (strcmp(filter, "-"))
    {
      // Verify that the filter exists and is executable...
      if (!mime_add_fcache(filtercache, filter, filterpath))
      {
        MIME_DEBUG("mime_load_convs: Filter %s not found in %s.\n", filter, filterpath);
        _mimeError(mime, "Filter \"%s\" not found.", filter);
        continue;
      }
    }

    // Finally, get the source super-type and type names from the beginning of
    // the line.  We do it here so we can support wildcards...
    lineptr = line;
    temp    = super;

    while (*lineptr != '/' && *lineptr != '\n' && *lineptr != '\0' && (temp - super + 1) < MIME_MAX_SUPER)
      *temp++ = (char)tolower(*lineptr++ & 255);

    *temp = '\0';

    if (*lineptr != '/')
      continue;

    lineptr ++;
    temp = type;

    while (*lineptr != ' ' && *lineptr != '\t' && *lineptr != '\n' && *lineptr != '\0' && (temp - type + 1) < MIME_MAX_TYPE)
      *temp++ = (char)tolower(*lineptr++ & 255);

    *temp = '\0';

    if (!strcmp(super, "*") && !strcmp(type, "*"))
    {
      // Force * / * to be "application/octet-stream"...
      cupsCopyString(super, "application", sizeof(super));
      cupsCopyString(type, "octet-stream", sizeof(type));
    }

    // Add the filter to the MIME database, supporting wildcards as needed...
    for (temptype = (mime_type_t *)cupsArrayGetFirst(mime->types); temptype; temptype = (mime_type_t *)cupsArrayGetNext(mime->types))
    {
      if ((super[0] == '*' || !strcmp(temptype->super, super)) && (type[0] == '*' || !strcmp(temptype->type, type)))
	mimeAddFilter(mime, temptype, dsttype, cost, filter);
    }
  }

  cupsFileClose(fp);
}


//
// 'mime_load_types()' - Load a xyz.types file.
//

static void
mime_load_types(mime_t     *mime,	// I - MIME database
                const char *filename)	// I - Types file to load
{
  cups_file_t	*fp;			// Types file
  size_t	linelen;		// Length of line
  char		line[32768],		// Input line from file
		*lineptr,		// Current position in line
		super[MIME_MAX_SUPER],	// Super-type name
		type[MIME_MAX_TYPE],	// Type name
		*temp;			// Temporary pointer
  mime_type_t	*typeptr;		// New MIME type


  MIME_DEBUG("mime_load_types(mime=%p, filename=\"%s\")\n", (void *)mime, filename);

  // First try to open the file...
  if ((fp = cupsFileOpen(filename, "r")) == NULL)
  {
    MIME_DEBUG("mime_load_types: Unable to open \"%s\": %s\n", filename, strerror(errno));
    _mimeError(mime, "Unable to open \"%s\": %s", filename, strerror(errno));
    return;
  }

  // Then read each line from the file, skipping any comments in the file...
  while (cupsFileGets(fp, line, sizeof(line)) != NULL)
  {
    // Skip blank lines and lines starting with a #...
    if (!line[0] || line[0] == '#')
      continue;

    // While the last character in the line is a backslash, continue on to the
    // next line (and the next, etc.)
    linelen = strlen(line);

    while (line[linelen - 1] == '\\')
    {
      linelen --;

      if (cupsFileGets(fp, line + linelen, sizeof(line) - linelen) == NULL)
        line[linelen] = '\0';
      else
        linelen += strlen(line + linelen);
    }

    // Extract the super-type and type names from the beginning of the line.
    lineptr = line;
    temp    = super;

    while (*lineptr != '/' && *lineptr != '\n' && *lineptr != '\0' && (temp - super + 1) < MIME_MAX_SUPER)
      *temp++ = (char)tolower(*lineptr++ & 255);

    *temp = '\0';

    if (*lineptr != '/')
      continue;

    lineptr ++;
    temp = type;

    while (*lineptr != ' ' && *lineptr != '\t' && *lineptr != '\n' && *lineptr != '\0' && (temp - type + 1) < MIME_MAX_TYPE)
      *temp++ = (char)tolower(*lineptr++ & 255);

    *temp = '\0';

    // Add the type and rules to the MIME database...
    typeptr = mimeAddType(mime, super, type);
    mimeAddTypeRule(typeptr, lineptr);
  }

  cupsFileClose(fp);
}
