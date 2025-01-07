//
// IPP data file functions.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright © 2007-2019 by Apple Inc.
// Copyright © 1997-2007 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "cups-private.h"


//
// Private structures...
//

struct _ipp_file_s			// IPP data file
{
  ipp_file_t		*parent;	// Parent data file, if any
  cups_file_t		*fp;		// File pointer
  char			*filename,	// Filename
			mode;		// Read/write mode
  int			indent,		// Current indentation
			column,		// Current column
			linenum,	// Current line number
			save_line;	// Saved line number
  off_t			save_pos;	// Saved position
  ipp_tag_t		group_tag;	// Current group for attributes
  ipp_t			*attrs;		// Current attributes
  int			num_vars;	// Number of variables
  cups_option_t		*vars;		// Variables
  ipp_fattr_cb_t	attr_cb;	// Attribute (filter) callback
  ipp_ferror_cb_t	error_cb;	// Error reporting callback
  ipp_ftoken_cb_t	token_cb;	// Token processing callback
  void			*cb_data;	// Callback data
  char			*buffer;	// Output buffer
  size_t		alloc_buffer;	// Size of output buffer
};


//
// Local functions...
//

static bool	expand_buffer(ipp_file_t *file, size_t buffer_size);
static bool	parse_value(ipp_file_t *file, ipp_t *ipp, ipp_attribute_t **attr, int element);
static bool	report_error(ipp_file_t *file, const char *message, ...) _CUPS_FORMAT(2,3);
static bool	write_string(ipp_file_t *file, const char *s, size_t len);


//
// 'ippFileClose()' - Close an IPP data file.
//
// This function closes the current IPP data file.  The `ipp_file_t` object can
// be reused for another file as needed.
//

bool					// O - `true` on success, `false` on error
ippFileClose(ipp_file_t *file)		// I - IPP data file
{
  bool	ret;				// Return value


  if (!file || !file->fp)
    return (false);

  if ((ret = cupsFileClose(file->fp)) == false)
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);

  free(file->filename);

  file->fp       = NULL;
  file->filename = NULL;
  file->mode     = '\0';
  file->attrs    = NULL;

  return (ret);
}


//
// 'ippFileDelete()' - Close an IPP data file and free all memory.
//
// This function closes an IPP data file, if necessary, and frees all memory
// associated with it.
//

bool					// O - `true` on success, `false` on error
ippFileDelete(ipp_file_t *file)		// I - IPP data file
{
  if (!file)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (false);
  }

  if (file->fp)
  {
    if (!ippFileClose(file))
      return (false);
  }

  cupsFreeOptions(file->num_vars, file->vars);
  free(file->buffer);
  free(file);

  return (true);
}


//
// 'ippFileExpandVars()' - Expand IPP data file and environment variables in a string.
//
// This function expands IPP data file variables of the form "$name" and
// environment variables of the form "$ENV[name]" in the source string to the
// destination string.  The
//

size_t					// O - Required size for expanded variables
ippFileExpandVars(ipp_file_t *file,	// I - IPP data file
                  char       *dst,	// I - Destination buffer
                  const char *src,	// I - Source string
                  size_t     dstsize)	// I - Size of destination buffer
{
  char		*dstptr,		// Pointer into destination
		*dstend,		// End of destination
		temp[256],		// Temporary string
		*tempptr;		// Pointer into temporary string
  const char	*value;			// Value to substitute


  // Range check input...
  if (!file || !dst || !src || dstsize < 32)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (false);
  }

  // Copy the source string to the destination, expanding variables as needed...
  dstptr = dst;
  dstend = dst + dstsize - 1;

  while (*src)
  {
    if (*src == '$')
    {
      // Substitute a string/number...
      if (!strncmp(src, "$$", 2))
      {
        // Literal $
        value = "$";
	src   += 2;
      }
      else if (!strncmp(src, "$ENV[", 5))
      {
        // Environment variable
	cupsCopyString(temp, src + 5, sizeof(temp));

	for (tempptr = temp; *tempptr; tempptr ++)
	{
	  if (*tempptr == ']')
	    break;
	}

        if (*tempptr)
	  *tempptr++ = '\0';

	value = getenv(temp);
        src   += tempptr - temp + 5;
      }
      else
      {
        // $name or ${name}
        if (src[1] == '{')
	{
	  src += 2;
	  cupsCopyString(temp, src, sizeof(temp));
	  if ((tempptr = strchr(temp, '}')) != NULL)
	    *tempptr = '\0';
	  else
	    tempptr = temp + strlen(temp);
	}
	else
	{
	  cupsCopyString(temp, src + 1, sizeof(temp));

	  for (tempptr = temp; *tempptr; tempptr ++)
	  {
	    if (!isalnum(*tempptr & 255) && *tempptr != '-' && *tempptr != '_')
	      break;
	  }

	  if (*tempptr)
	    *tempptr = '\0';
        }

        value = ippFileGetVar(file, temp);
        src   += tempptr - temp + 1;
      }

      if (value)
      {
        if (dstptr < dstend)
          cupsCopyString(dstptr, value, (size_t)(dstend - dstptr + 1));
	dstptr += strlen(value);
      }
    }
    else if (dstptr < dstend)
      *dstptr++ = *src++;
    else
      dstptr ++;
  }

  if (dstptr < dstend)
    *dstptr = '\0';
  else
    *dstend = '\0';

  return ((size_t)(dstptr - dst));
}


//
// 'ippFileGetAttribute()' - Get a single named attribute from an IPP data file.
//
// This function finds the first occurence of a named attribute in the current
// IPP attributes in the specified data file.  Unlike
// @link ippFileGetAttributes@, this function does not clear the attribute
// state.
//

ipp_attribute_t	*			// O - Attribute or `NULL` if none
ippFileGetAttribute(
    ipp_file_t *file,			// I - IPP data file
    const char *name,			// I - Attribute name
    ipp_tag_t  value_tag)		// I - Value tag or `IPP_TAG_ZERO` for any
{
  if (!file || !name)
    return (NULL);
  else
    return (ippFindAttribute(file->attrs, name, value_tag));
}


//
// 'ippFileGetAttributes()' - Get the current set of attributes from an IPP data file.
//
// This function gets the current set of attributes from an IPP data file.
//

ipp_t *					// O - IPP attributes
ippFileGetAttributes(ipp_file_t *file)	// I - IPP data file
{
  return (file ? file->attrs : NULL);
}


//
// 'ippFileGetFilename()' - Get the filename for an IPP data file.
//
// This function returns the filename associated with an IPP data file.
//

const char *				// O - Filename
ippFileGetFilename(ipp_file_t *file)	// I - IPP data file
{
  return (file ? file->filename : NULL);
}


//
// 'ippFileGetLineNumber()' - Get the current line number in an IPP data file.
//
// This function returns the current line number in an IPP data file.
//

int					// O - Line number
ippFileGetLineNumber(ipp_file_t *file)	// I - IPP data file
{
  return (file ? file->linenum : 0);
}


//
// 'ippFileGetVar()' - Get the value of an IPP data file variable.
//
// This function returns the value of an IPP data file variable.  `NULL` is
// returned if the variable is not set.
//

const char *				// O - Variable value or `NULL` if none.
ippFileGetVar(ipp_file_t *file,		// I - IPP data file
	      const char *name)		// I - Variable name
{
  const char	*value;			// Value


  if (!file || !name)
    return (NULL);
  else if (!strcmp(name, "user"))
    return (cupsGetUser());
  else if ((value = cupsGetOption(name, file->num_vars, file->vars)) != NULL)
    return (value);
  else if (file->parent)
    return (cupsGetOption(name, file->parent->num_vars, file->parent->vars));
  else
    return (NULL);
}


//
// 'ippFileNew()' - Create a new IPP data file object for reading or writing.
//
// This function opens an IPP data file for reading (mode="r") or writing
// (mode="w").  If the "parent" argument is not `NULL`, all variables from the
// parent data file are copied to the new file.
//

ipp_file_t *				// O - IPP data file
ippFileNew(ipp_file_t      *parent,	// I - Parent data file or `NULL` for none
           ipp_fattr_cb_t  attr_cb,	// I - Attribute filtering callback, if any
           ipp_ferror_cb_t error_cb,	// I - Error reporting callback, if any
           void            *cb_data)	// I - Callback data, if any
{
  ipp_file_t	*file;			// IPP data file


  // Allocate memory...
  if ((file = (ipp_file_t *)calloc(1, sizeof(ipp_file_t))) == NULL)
    return (NULL);

  // Set callbacks and parent...
  file->parent   = parent;
  file->attr_cb  = attr_cb;
  file->error_cb = error_cb;
  file->cb_data  = cb_data;

  return (file);
}


//
// 'ippFileOpen()' - Open an IPP data file for reading or writing.
//
// This function opens an IPP data file for reading (mode="r") or writing
// (mode="w").  If the "parent" argument is not `NULL`, all variables from the
// parent data file are copied to the new file.
//

bool					// O - `true` on success, `false` on error
ippFileOpen(ipp_file_t *file,		// I - IPP data file
            const char *filename,	// I - Filename to open
            const char *mode)		// I - Open mode - "r" to read and "w" to write
{
  cups_file_t	*fp;			// IPP data file pointer


  // Range check input...
  if (!file || !filename || !mode || (strcmp(mode, "r") && strcmp(mode, "w")))
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (false);
  }
  else if (file->fp)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EBUSY), 0);
    return (false);
  }

  // Try opening the file...
  if ((fp = cupsFileOpen(filename, mode)) == NULL)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    return (false);
  }

  // Save the file information and return...
  file->fp       = fp;
  file->filename = strdup(filename);
  file->mode     = *mode;
  file->column   = 0;
  file->linenum  = 1;

  return (true);
}


//
// 'ippFileRead()' - Read an IPP data file.
//

bool					// O - `true` on success, `false` on error
ippFileRead(ipp_file_t      *file,	// I - IPP data file
            ipp_ftoken_cb_t token_cb,	// I - Token callback
            bool            with_groups)// I - Read attributes with GROUP directives
{
  ipp_t		*attrs = NULL;		// Active IPP message
  ipp_attribute_t *attr = NULL;		// Current attribute
  char		token[1024];		// Token string
  ipp_t		*ignored = NULL;	// Ignored attributes
  bool		ret = true;		// Return value


  // Range check input
  if (!file || file->mode != 'r')
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (false);
  }

  // Read data file, using the callback function as needed...
  while (ippFileReadToken(file, token, sizeof(token)))
  {
    if (!_cups_strcasecmp(token, "DEFINE") || !_cups_strcasecmp(token, "DEFINE-DEFAULT"))
    {
      // Define a variable...
      char	name[128],		// Variable name
		value[1024],		// Variable value
		temp[1024];		// Temporary string

      attr = NULL;

      if (ippFileReadToken(file, name, sizeof(name)) && ippFileReadToken(file, temp, sizeof(temp)))
      {
        if (_cups_strcasecmp(token, "DEFINE-DEFAULT") || !ippFileGetVar(file, name))
        {
	  ippFileExpandVars(file, value, temp, sizeof(value));
	  ippFileSetVar(file, name, value);
	}
      }
      else
      {
        report_error(file, "Missing %s name and/or value on line %d of '%s'.", token, file->linenum, file->filename);
        ret = false;
        break;
      }
    }
    else if (file->attrs && with_groups && !_cups_strcasecmp(token, "GROUP"))
    {
      // Attribute group...
      char	temp[1024];		// Temporary token
      ipp_tag_t	group_tag;		// Group tag

      if (!ippFileReadToken(file, temp, sizeof(temp)))
      {
	report_error(file, "Missing GROUP tag on line %d of '%s'.", file->linenum, file->filename);
	ret = false;
	break;
      }

      if ((group_tag = ippTagValue(temp)) == IPP_TAG_ZERO || group_tag >= IPP_TAG_UNSUPPORTED_VALUE)
      {
	report_error(file, "Bad GROUP tag '%s' on line %d of '%s'.", temp, file->linenum, file->filename);
	ret = false;
	break;
      }

      if (group_tag == file->group_tag)
	ippAddSeparator(file->attrs);

      file->group_tag = group_tag;
    }
    else if (file->attrs && !_cups_strcasecmp(token, "ATTR"))
    {
      // Attribute definition...
      char	syntax[128],		// Attribute syntax (value tag)
		name[128];		// Attribute name
      ipp_tag_t	value_tag;		// Value tag

      attr = NULL;

      if (!ippFileReadToken(file, syntax, sizeof(syntax)))
      {
        report_error(file, "Missing ATTR syntax on line %d of '%s'.", file->linenum, file->filename);
	ret = false;
	break;
      }
      else if ((value_tag = ippTagValue(syntax)) < IPP_TAG_UNSUPPORTED_VALUE)
      {
        report_error(file, "Bad ATTR syntax \"%s\" on line %d of '%s'.", syntax, file->linenum, file->filename);
	ret = false;
	break;
      }

      if (!ippFileReadToken(file, name, sizeof(name)) || !name[0])
      {
        report_error(file, "Missing ATTR name on line %d of '%s'.", file->linenum, file->filename);
	ret = false;
	break;
      }

      if (!file->attr_cb || (*file->attr_cb)(file, file->cb_data, name))
      {
        // Add this attribute...
        attrs = file->attrs;
      }
      else
      {
        // Ignore this attribute...
        if (!ignored)
          ignored = ippNew();

        attrs = ignored;
      }

      if (value_tag < IPP_TAG_INTEGER)
      {
        // Add out-of-band attribute - no value string needed...
        ippAddOutOfBand(attrs, file->group_tag, value_tag, name);
      }
      else
      {
        // Add attribute with one or more values...
        attr = ippAddString(attrs, file->group_tag, value_tag, name, NULL, NULL);

        if (!parse_value(file, attrs, &attr, 0))
        {
          ret = false;
          break;
	}
      }
    }
    else if (file->attrs && (!_cups_strcasecmp(token, "ATTR-IF-DEFINED") || !_cups_strcasecmp(token, "ATTR-IF-NOT-DEFINED")))
    {
      // Conditional attribute definition...
      char	varname[128],		// Variable name
		syntax[128],		// Attribute syntax (value tag)
		name[128];		// Attribute name
      ipp_tag_t	value_tag;		// Value tag

      if (!ippFileReadToken(file, varname, sizeof(varname)))
      {
        report_error(file, "Missing %s variable on line %d of '%s'.", token, file->linenum, file->filename);
	ret = false;
	break;
      }

      if (!ippFileReadToken(file, syntax, sizeof(syntax)))
      {
        report_error(file, "Missing %s syntax on line %d of '%s'.", token, file->linenum, file->filename);
	ret = false;
	break;
      }
      else if ((value_tag = ippTagValue(syntax)) < IPP_TAG_UNSUPPORTED_VALUE)
      {
        report_error(file, "Bad %s syntax \"%s\" on line %d of '%s'.", token, syntax, file->linenum, file->filename);
	ret = false;
	break;
      }

      if (!ippFileReadToken(file, name, sizeof(name)) || !name[0])
      {
        report_error(file, "Missing %s name on line %d of '%s'.", token, file->linenum, file->filename);
	ret = false;
	break;
      }

      if (!file->attr_cb || (*file->attr_cb)(file, file->cb_data, name))
      {
        // Add this attribute...
        attrs = file->attrs;
      }
      else if (!_cups_strcasecmp(token, "ATTR-IF-DEFINED"))
      {
        if (ippFileGetVar(file, varname))
          attrs = file->attrs;
        else
          attrs = ignored;
      }
      else			// ATTR-IF-NOT-DEFINED
      {
        if (ippFileGetVar(file, varname))
          attrs = ignored;
        else
          attrs = file->attrs;
      }

      if (value_tag < IPP_TAG_INTEGER)
      {
        // Add out-of-band attribute - no value string needed...
        ippAddOutOfBand(attrs, file->group_tag, value_tag, name);
      }
      else
      {
        // Add attribute with one or more values...
        attr = ippAddString(attrs, file->group_tag, value_tag, name, NULL, NULL);

        if (!parse_value(file, attrs, &attr, 0))
        {
	  ret = false;
	  break;
        }
      }
    }
    else if (attr && !_cups_strcasecmp(token, ","))
    {
      // Additional value...
      if (!parse_value(file, attrs, &attr, ippGetCount(attr)))
      {
        ret = false;
	break;
      }
    }
    else
    {
      // Something else...
      attr  = NULL;
      attrs = NULL;

      if (!token_cb)
      {
        ret = false;
        break;
      }
      else if ((ret = (token_cb)(file, file->cb_data, token)) == false)
      {
        break;
      }
    }
  }

  // Free any ignored attributes and return...
  ippDelete(ignored);

  return (ret);
}


//
// 'ippFileReadCollection()' - Read a collection from an IPP data file.
//
// This function reads a collection value from an IPP data file.  Collection
// values are surrounded by curly braces ("{" and "}") and have "MEMBER"
// directives to define member attributes in the collection.
//

ipp_t *					// O - Collection value
ippFileReadCollection(ipp_file_t *file)	// I - IPP data file
{
  ipp_t		*col;			// Collection value
  ipp_attribute_t *attr = NULL;		// Current member attribute
  char		token[1024];		// Token string


  // Range check input...
  if (!file)
    return (NULL);

  // Read the first token to verify it is an open curly brace...
  if (!ippFileReadToken(file, token, sizeof(token)))
  {
    report_error(file, "Missing collection value on line %d of '%s'.", file->linenum, file->filename);
    return (NULL);
  }
  else if (strcmp(token, "{"))
  {
    report_error(file, "Bad collection value on line %d of '%s'.", file->linenum, file->filename);
    return (NULL);
  }

  // Parse the collection value...
  col = ippNew();

  while (ippFileReadToken(file, token, sizeof(token)))
  {
    if (!strcmp(token, "}"))
    {
      // End of collection value...
      break;
    }
    else if (!_cups_strcasecmp(token, "MEMBER"))
    {
      // Member attribute definition...
      char	syntax[128],		// Attribute syntax (value tag)
		name[128];		// Attribute name
      ipp_tag_t	value_tag;		// Value tag

      attr = NULL;

      if (!ippFileReadToken(file, syntax, sizeof(syntax)))
      {
        report_error(file, "Missing MEMBER syntax on line %d of '%s'.", file->linenum, file->filename);
	ippDelete(col);
	col = NULL;
	break;
      }
      else if ((value_tag = ippTagValue(syntax)) < IPP_TAG_UNSUPPORTED_VALUE)
      {
        report_error(file, "Bad MEMBER syntax \"%s\" on line %d of '%s'.", syntax, file->linenum, file->filename);
	ippDelete(col);
	col = NULL;
	break;
      }

      if (!ippFileReadToken(file, name, sizeof(name)) || !name[0])
      {
        report_error(file, "Missing MEMBER name on line %d of '%s'.", file->linenum, file->filename);
	ippDelete(col);
	col = NULL;
	break;
      }

      if (value_tag < IPP_TAG_INTEGER)
      {
        // Add out-of-band attribute - no value string needed...
        ippAddOutOfBand(col, IPP_TAG_ZERO, value_tag, name);
      }
      else
      {
        // Add attribute with one or more values...
        attr = ippAddString(col, IPP_TAG_ZERO, value_tag, name, NULL, NULL);

        if (!parse_value(file, col, &attr, 0))
        {
	  ippDelete(col);
	  col = NULL;
          break;
	}
      }

    }
    else if (attr && !_cups_strcasecmp(token, ","))
    {
      // Additional value...
      if (!parse_value(file, col, &attr, ippGetCount(attr)))
      {
	ippDelete(col);
	col = NULL;
	break;
      }
    }
    else
    {
      // Something else...
      report_error(file, "Unknown directive \"%s\" on line %d of '%s'.", token, file->linenum, file->filename);
      ippDelete(col);
      col  = NULL;
      attr = NULL;
      break;

    }
  }

  return (col);
}


//
// 'ippFileReadToken()' - Read a token from an IPP data file.
//
// This function reads a single token or value from an IPP data file, skipping
// comments and whitespace as needed.
//

bool					// O - `true` on success, `false` on error
ippFileReadToken(ipp_file_t *file,	// I - IPP data file
                 char       *token,	// I - Token buffer
                 size_t     tokensize)	// I - Size of token buffer
{
  int	ch,				// Character from file
	quote = 0;			// Quoting character
  char	*tokptr = token,		// Pointer into token buffer
	*tokend = token + tokensize - 1;// End of token buffer


  // Range check input...
  if (!file || !token || tokensize < 32)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);

    if (token)
      *token = '\0';

    return (false);
  }

  // Skip whitespace and comments...
  DEBUG_printf("1ippFileReadToken: linenum=%d, pos=%ld", file->linenum, (long)cupsFileTell(file->fp));

  while ((ch = cupsFileGetChar(file->fp)) != EOF)
  {
    if (_cups_isspace(ch))
    {
      // Whitespace...
      if (ch == '\n')
      {
        file->linenum ++;
        DEBUG_printf("1ippFileReadToken: LF in leading whitespace, linenum=%d, pos=%ld", file->linenum, (long)cupsFileTell(file->fp));
      }
    }
    else if (ch == '#')
    {
      // Comment...
      DEBUG_puts("1ippFileReadToken: Skipping comment in leading whitespace...");

      while ((ch = cupsFileGetChar(file->fp)) != EOF)
      {
        if (ch == '\n')
          break;
      }

      if (ch == '\n')
      {
        file->linenum ++;
        DEBUG_printf("1ippFileReadToken: LF at end of comment, linenum=%d, pos=%ld", file->linenum, (long)cupsFileTell(file->fp));
      }
      else
        break;
    }
    else
      break;
  }

  if (ch == EOF)
  {
    DEBUG_puts("1ippFileReadToken: EOF");
    return (false);
  }

  // Read a token...
  while (ch != EOF)
  {
    if (ch == '\n')
    {
      file->linenum ++;
      DEBUG_printf("1ippFileReadToken: LF in token, linenum=%d, pos=%ld", file->linenum, (long)cupsFileTell(file->fp));
    }

    if (ch == quote)
    {
      // End of quoted text...
      *tokptr = '\0';
      DEBUG_printf("1ippFileReadToken: Returning \"%s\" at closing quote.", token);
      return (true);
    }
    else if (!quote && _cups_isspace(ch))
    {
      // End of unquoted text...
      *tokptr = '\0';
      DEBUG_printf("1ippFileReadToken: Returning \"%s\" before whitespace.", token);
      return (true);
    }
    else if (!quote && (ch == '\'' || ch == '\"'))
    {
      // Start of quoted text or regular expression...
      quote = ch;

      DEBUG_printf("1ippFileReadToken: Start of quoted string, quote=%c, pos=%ld", quote, (long)cupsFileTell(file->fp));
    }
    else if (!quote && ch == '#')
    {
      // Start of comment...
      cupsFileSeek(file->fp, cupsFileTell(file->fp) - 1);
      *tokptr = '\0';
      DEBUG_printf("1ippFileReadToken: Returning \"%s\" before comment.", token);
      return (true);
    }
    else if (!quote && (ch == '{' || ch == '}' || ch == ','))
    {
      // Delimiter...
      if (tokptr > token)
      {
        // Return the preceding token first...
	cupsFileSeek(file->fp, cupsFileTell(file->fp) - 1);
      }
      else
      {
        // Return this delimiter by itself...
        *tokptr++ = (char)ch;
      }

      *tokptr = '\0';
      DEBUG_printf("1ippFileReadToken: Returning \"%s\".", token);
      return (true);
    }
    else
    {
      if (ch == '\\')
      {
        // Quoted character...
        DEBUG_printf("1ippFileReadToken: Quoted character at pos=%ld", (long)cupsFileTell(file->fp));

        if ((ch = cupsFileGetChar(file->fp)) == EOF)
        {
	  *token = '\0';
	  DEBUG_puts("1ippFileReadToken: EOF");
	  return (false);
	}
	else if (ch == '\n')
	{
	  file->linenum ++;
	  DEBUG_printf("1ippFileReadToken: quoted LF, linenum=%d, pos=%ld", file->linenum, (long)cupsFileTell(file->fp));
	}
	else if (ch == 'a')
	  ch = '\a';
	else if (ch == 'b')
	  ch = '\b';
	else if (ch == 'f')
	  ch = '\f';
	else if (ch == 'n')
	  ch = '\n';
	else if (ch == 'r')
	  ch = '\r';
	else if (ch == 't')
	  ch = '\t';
	else if (ch == 'v')
	  ch = '\v';
      }

      if (tokptr < tokend)
      {
        // Add to current token...
	*tokptr++ = (char)ch;
      }
      else
      {
        // Token too long...
	*tokptr = '\0';
	DEBUG_printf("1ippFileReadToken: Too long: \"%s\".", token);
	return (false);
      }
    }

    // Get the next character...
    ch = cupsFileGetChar(file->fp);
  }

  *tokptr = '\0';
  DEBUG_printf("1ippFileReadToken: Returning \"%s\" at EOF.", token);

  return (tokptr > token);
}


//
// 'ippFileRestorePosition()' - Restore the previous position in an IPP data file.
//
// This function restores the previous position in an IPP data file that is open
// for reading.
//

bool					// O - `true` on success, `false` on failure
ippFileRestorePosition(ipp_file_t *file)// I - IPP data file
{
  // Range check input...
  if (!file || file->mode != 'r' || file->save_line == 0)
    return (false);

  // Seek back to the saved position...
  if (cupsFileSeek(file->fp, file->save_pos) != file->save_pos)
    return (false);

  file->linenum   = file->save_line;
  file->save_pos  = 0;
  file->save_line = 0;

  return (true);
}


//
// 'ippFileSavePosition()' - Save the current position in an IPP data file.
//
// This function saves the current position in an IPP data file that is open
// for reading.
//

bool					// O - `true` on success, `false` on failure
ippFileSavePosition(ipp_file_t *file)	// I - IPP data file
{
  // Range check input...
  if (!file || file->mode != 'r')
    return (false);

  // Save the current position...
  file->save_pos  = cupsFileTell(file->fp);
  file->save_line = file->linenum;

  return (true);
}


//
// 'ippFileSetAttributes()' - Set the attributes for an IPP data file.
//
// This function sets the current set of attributes for an IPP data file,
// typically an empty collection created with @link ippNew@.
//

bool					// O - `true` on success, `false` otherwise
ippFileSetAttributes(ipp_file_t *file,	// I - IPP data file
                     ipp_t      *attrs)	// I - IPP attributes
{
  if (file)
  {
    file->attrs = attrs;
    return (true);
  }

  return (false);
}


//
// 'ippFileSetGroupTag()' - Set the group tag for an IPP data file.
//
// This function sets the group tag associated with attributes that are read
// from an IPP data file.
//

bool					// O - `true` on success, `false` otherwise
ippFileSetGroupTag(ipp_file_t *file,	// I - IPP data file
                   ipp_tag_t  group_tag)// I - Group tag
{
  if (file && group_tag >= IPP_TAG_OPERATION && group_tag <= IPP_TAG_SYSTEM)
  {
    file->group_tag = group_tag;
    return (true);
  }

  return (false);
}


//
// 'ippFileSetVar()' - Set an IPP data file variable to a constant value.
//
// This function sets an IPP data file variable to a constant value.  Setting
// the "uri" variable also initializes the "scheme", "uriuser", "hostname",
// "port", and "resource" variables.
//

bool					// O - `true` on success, `false` on failure
ippFileSetVar(ipp_file_t *file,		// I - IPP data file
              const char *name,		// I - Variable name
              const char *value)	// I - Value
{
  if (!file || !name || !value)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (false);
  }

  // Save new variable...
  if (!strcmp(name, "uri"))
  {
    // Also set URI component values...
    char	uri[1024],		// New printer URI
		resolved[1024],		// Resolved mDNS URI
		scheme[32],		// URI scheme
		userpass[256],		// URI username:password
		*password,		// Pointer to password
		hostname[256],		// URI hostname
		resource[256];		// URI resource path
    int		port;			// URI port number
    http_uri_status_t uri_status;	// URI decoding status

    if (strstr(value, "._tcp"))
    {
      // Resolve URI...
      if (!httpResolveURI(value, resolved, sizeof(resolved), HTTP_RESOLVE_DEFAULT, NULL, NULL))
      {
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(ENOENT), 0);
	return (false);
      }

      value = resolved;
    }

    if ((uri_status = httpSeparateURI(HTTP_URI_CODING_ALL, value, scheme, sizeof(scheme), userpass, sizeof(userpass), hostname, sizeof(hostname), &port, resource, sizeof(resource))) < HTTP_URI_STATUS_OK)
    {
      // Bad URI...
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, httpURIStatusString(uri_status), 0);
      return (false);
    }
    else
    {
      // Valid URI...
      if ((password = strchr(userpass, ':')) != NULL)
      {
        // Separate and save password from URI...
        *password++ = '\0';

        file->num_vars = cupsAddOption("uripassword", password, file->num_vars, &file->vars);
      }

      file->num_vars = cupsAddOption("scheme", scheme, file->num_vars, &file->vars);
      file->num_vars = cupsAddOption("uriuser", userpass, file->num_vars, &file->vars);
      file->num_vars = cupsAddOption("hostname", hostname, file->num_vars, &file->vars);
      file->num_vars = cupsAddIntegerOption("port", port, file->num_vars, &file->vars);
      file->num_vars = cupsAddOption("resource", resource, file->num_vars, &file->vars);

      // Reassemble URI without username or password...
      httpAssembleURI(HTTP_URI_CODING_ALL, uri, sizeof(uri), scheme, NULL, hostname, port, resource);
      file->num_vars = cupsAddOption("uri", uri, file->num_vars, &file->vars);
    }
  }
  else
  {
    // Set another variable...
    file->num_vars = cupsAddOption(name, value, file->num_vars, &file->vars);
  }

  return (true);
}


//
// 'ippFileSetVarf()' - Set an IPP data file variable to a formatted value.
//
// This function sets an IPP data file variable to a formatted value.  Setting
// the "uri" variable also initializes the "scheme", "uriuser", "hostname",
// "port", and "resource" variables.
//

bool					// O - `true` on success, `false` on error
ippFileSetVarf(ipp_file_t *file,	// I - IPP data file
               const char *name,	// I - Variable name
               const char *value,	// I - Printf-style value
               ...)			// I - Additional arguments as needed
{
  va_list	ap;			// Pointer to arguments
  char		buffer[16384];		// Value buffer


  if (!file || !name || !value)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (false);
  }

  va_start(ap, value);
  vsnprintf(buffer, sizeof(buffer), value, ap);
  va_end(ap);

  return (ippFileSetVar(file, name, buffer));
}


//
// 'ippFileWriteAttributes()' - Write an IPP message to an IPP data file.
//
// This function writes an IPP message to an IPP data file using the attribute
// filter specified in the call to @link ippFileOpen@.  If "with_group" is
// `true`, "GROUP" directives are written as necessary to place the attributes
// in the correct groups.
//

bool					// O - `true` on success, `false` on error
ippFileWriteAttributes(
    ipp_file_t *file,			// I - IPP data file
    ipp_t      *ipp,			// I - IPP attributes to write
    bool       with_groups)		// I - `true` to include GROUPs, `false` otherwise
{
  bool			ret = true;	// Return value
  ipp_attribute_t	*attr;		// Current attribute
  const char		*name;		// Attribute name
  ipp_tag_t		group_tag,	// Group tag
			value_tag;	// Value tag
  int			i,		// Looping var
			count;		// Number of values


  // Range check input...
  if (!file || file->mode != 'w' || !ipp)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (false);
  }

  // Make sure we are on a new line...
  if (file->column)
  {
    cupsFilePutChar(file->fp, '\n');
    file->column = 0;
  }

  // Loop through the attributes...
  for (attr = ippGetFirstAttribute(ipp); attr; attr = ippGetNextAttribute(ipp))
  {
    if ((name = ippGetName(attr)) == NULL)
      continue;

    if (file->attr_cb && !(*file->attr_cb)(file, file->cb_data, name))
      continue;

    count     = ippGetCount(attr);
    group_tag = ippGetGroupTag(attr);
    value_tag = ippGetValueTag(attr);

    if (with_groups && group_tag != IPP_TAG_ZERO && group_tag != file->group_tag)
    {
      ret &= ippFileWriteToken(file, "GROUP");
      ret &= ippFileWriteTokenf(file, "%s\n", ippTagString(group_tag));
      file->group_tag = group_tag;
    }

    ret &= ippFileWriteToken(file, group_tag == IPP_TAG_ZERO ? "MEMBER" : "ATTR");
    ret &= ippFileWriteToken(file, ippTagString(value_tag));
    ret &= ippFileWriteToken(file, name);

    switch (value_tag)
    {
      case IPP_TAG_INTEGER :
      case IPP_TAG_ENUM :
	  for (i = 0; i < count; i ++)
	    ret &= cupsFilePrintf(file->fp, "%s%d", i ? "," : " ", ippGetInteger(attr, i));
	  break;

      case IPP_TAG_BOOLEAN :
	  ret &= cupsFilePuts(file->fp, ippGetBoolean(attr, 0) ? " true" : " false");

	  for (i = 1; i < count; i ++)
	    ret &= cupsFilePuts(file->fp, ippGetBoolean(attr, 1) ? ",true" : ",false");
	  break;

      case IPP_TAG_RANGE :
	  for (i = 0; i < count; i ++)
	  {
	    int upper, lower = ippGetRange(attr, i, &upper);
					// Upper/lower range values

	    ret &= cupsFilePrintf(file->fp, "%s%d-%d", i ? "," : " ", lower, upper);
	  }
	  break;

      case IPP_TAG_RESOLUTION :
	  for (i = 0; i < count; i ++)
	  {
	    ipp_res_t	units;		// Resolution units
	    int		yres, xres = ippGetResolution(attr, i, &yres, &units);
					// X/Y resolution

            if (xres == yres)
	      ret &= cupsFilePrintf(file->fp, "%s%d%s", i ? "," : " ", xres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
	    else
	      ret &= cupsFilePrintf(file->fp, "%s%dx%d%s", i ? "," : " ", xres, yres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
	  }
	  break;

      case IPP_TAG_DATE :
	  for (i = 0; i < count; i ++)
	  {
	    time_t	utctime = ippDateToTime(ippGetDate(attr, i));
					// Date/time value
            struct tm	utcdate;	// Date/time components

	    // Get the UTC date and time corresponding to this date value...
            gmtime_r(&utctime, &utcdate);

	    ret &= cupsFilePrintf(file->fp, "%s%04d-%02d-%02dT%02d:%02d:%02dZ", i ? "," : " ", utcdate.tm_year + 1900, utcdate.tm_mon + 1, utcdate.tm_mday, utcdate.tm_hour, utcdate.tm_min, utcdate.tm_sec);
	  }
	  break;

      case IPP_TAG_STRING :
	  for (i = 0; i < count; i ++)
	  {
	    int		len;		// Length of octetString
	    const char	*s = (const char *)ippGetOctetString(attr, i, &len);
					// octetString value

	    ret &= cupsFilePuts(file->fp, i ? "," : " ");
	    ret &= write_string(file, s, (size_t)len);
	  }
	  break;

      case IPP_TAG_TEXT :
      case IPP_TAG_TEXTLANG :
      case IPP_TAG_NAME :
      case IPP_TAG_NAMELANG :
      case IPP_TAG_KEYWORD :
      case IPP_TAG_URI :
      case IPP_TAG_URISCHEME :
      case IPP_TAG_CHARSET :
      case IPP_TAG_LANGUAGE :
      case IPP_TAG_MIMETYPE :
	  for (i = 0; i < count; i ++)
	  {
	    const char *s = ippGetString(attr, i, NULL);
					// String value

	    ret &= cupsFilePuts(file->fp, i ? "," : " ");
	    ret &= write_string(file, s, strlen(s));
	  }
	  break;

      case IPP_TAG_BEGIN_COLLECTION :
	  file->indent += 4;
	  for (i = 0; i < count; i ++)
	  {
	    ret &= cupsFilePuts(file->fp, i ? ",{\n" : " {\n");
	    ret &= ippFileWriteAttributes(file, ippGetCollection(attr, i), false);
	    ret &= cupsFilePrintf(file->fp, "%*s}", file->indent - 4, "");
	  }
	  file->indent -= 4;
	  break;

      default :
	  /* Out-of-band value */
	  break;
    }

    // Finish with a newline after the attribute definition
    ret &= cupsFilePutChar(file->fp, '\n');
  }

  return (ret);
}


//
// 'ippFileWriteComment()' - Write a comment to an IPP data file.
//
// This function writes a comment to an IPP data file.  Every line in the string
// is prefixed with the "#" character and indented as needed.
//

bool					// O - `true` on success, `false` on error
ippFileWriteComment(ipp_file_t *file,	// I - IPP data file
                    const char *comment,// I - Printf-style comment string
                    ...)		// I - Additional arguments as needed
{
  bool		ret = true;		// Return value
  va_list	ap,			// Pointer to arguments
		ap2;			// Copy of arguments
  int		bufsize;		// Size of formatted string
  const char	*start,			// Start of comment line
		*ptr;			// Pointer into comment


  // Range check input...
  if (!file || file->mode != 'w' || !comment)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (false);
  }

  // Format the comment...
  va_start(ap, comment);
  va_copy(ap2, ap);

  if ((bufsize = vsnprintf(file->buffer, file->alloc_buffer, comment, ap2)) >= (int)file->alloc_buffer)
  {
    if (!expand_buffer(file, (size_t)bufsize + 1))
    {
      va_end(ap2);
      va_end(ap);
      return (false);
    }

    vsnprintf(file->buffer, file->alloc_buffer, comment, ap);
  }

  va_end(ap2);
  va_end(ap);

  // Make sure we start on a new line...
  if (file->column > 0)
  {
    ret &= cupsFilePutChar(file->fp, '\n');
    file->column = 0;
  }

  for (start = file->buffer, ptr = start; *ptr; start = ptr)
  {
    // Find the end of the line...
    while (*ptr && *ptr != '\n')
      ptr ++;

    // Write this line...
    ret &= cupsFilePrintf(file->fp, "%*s# ", file->indent, "");
    ret &= cupsFileWrite(file->fp, start, (size_t)(ptr - start));
    ret &= cupsFilePutChar(file->fp, '\n');

    // Skip newline, if any...
    if (*ptr)
      ptr ++;
  }

  return (ret);
}


//
// 'ippFileWriteToken()' - Write a token or value string to an IPP data file.
//
// This function writes a token or value string to an IPP data file, quoting
// and indenting the string as needed.
//

bool					// O - `true` on success, `false` on error
ippFileWriteToken(ipp_file_t *file,	// I - IPP data file
                  const char *token)	// I - Token/value string
{
  const char	*ptr;			// Pointer into token/value string
  bool		ret = true;		// Return value


  // Range check input...
  if (!file || file->mode != 'w' || !token)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (false);
  }

  // Handle indentation...
  if (!strcmp(token, "}"))
  {
    // Add newline before '}' as needed and unindent...
    if (file->column > 0)
    {
      ret &= cupsFilePutChar(file->fp, '\n');
      file->column = 0;
    }

    if (file->indent > 0)
      file->indent -= 4;
  }

  if (file->column == 0 && file->indent > 0)
  {
    ret &= cupsFilePrintf(file->fp, "%*s", file->indent, "");
    file->column += file->indent;
  }
  else if (strcmp(token, "{") && strcmp(token, "}"))
  {
    ret &= cupsFilePutChar(file->fp, ' ');
    file->column ++;
  }

  // Look for whitespace or special characters...
  for (ptr = token; *ptr; ptr ++)
  {
    if (strchr(" \t\'\"\\", *ptr))
      break;
  }

  if (*ptr)
  {
    // Need to quote the string
    ret &= write_string(file, token, strlen(token));
  }
  else if (!strcmp(token, "{"))
  {
    // Add newline after '{' and indent...
    ret &= cupsFilePuts(file->fp, "{\n");
    file->column = 0;
    file->indent += 4;
  }
  else if (!strcmp(token, "}"))
  {
    // Add newline after '}'...
    ret &= cupsFilePuts(file->fp, "}\n");
    file->column = 0;
  }
  else
  {
    // Just write the string as-is...
    ret &= cupsFilePuts(file->fp, token);

    if ((ptr = token + strlen(token) - 1) >= token && *ptr == '\n')
    {
      // New line...
      file->column = 0;
    }
    else
    {
      // Existing line...
      file->column += strlen(token);
    }
  }

  return (ret);
}


//
// 'ippFileWriteTokenf()' - Write a formatted token or value string to an IPP data file.
//
// This function writes a formatted token or value string to an IPP data file,
// quoting and indenting the string as needed.
//

bool					// O - `true` on success, `false` on error
ippFileWriteTokenf(ipp_file_t *file,	// I - IPP data file
                   const char *token,	// I - Printf-style token/value string
                   ...)			// I - Additional arguments as needed
{
  va_list	ap,			// Pointer to arguments
		ap2;			// Copy of arguments
  int		bufsize;		// Size of formatted string


  // Range check input...
  if (!file || file->mode != 'w' || !token)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (false);
  }

  // Format the message...
  va_start(ap, token);
  va_copy(ap2, ap);

  if ((bufsize = vsnprintf(file->buffer, file->alloc_buffer, token, ap2)) >= (int)file->alloc_buffer)
  {
    if (!expand_buffer(file, (size_t)bufsize + 1))
    {
      va_end(ap2);
      va_end(ap);
      return (false);
    }

    vsnprintf(file->buffer, file->alloc_buffer, token, ap);
  }

  va_end(ap2);
  va_end(ap);

  return (ippFileWriteToken(file, file->buffer));
}


//
// 'expand_buffer()' - Expand the output buffer of the IPP data file as needed.
//

static bool				// O - `true` on success, `false` on failure
expand_buffer(ipp_file_t *file,		// I - IPP data file
              size_t     buffer_size)	// I - Required size
{
  char	*buffer;			// New buffer pointer


  // If we already have enough, return right away...
  if (buffer_size <= file->alloc_buffer)
    return (true);

  // Try allocating/expanding the current buffer...
  if ((buffer = realloc(file->buffer, buffer_size)) == NULL)
    return (false);

  // Save new buffer and size...
  file->buffer       = buffer;
  file->alloc_buffer = buffer_size;

  return (true);
}


//
// 'parse_value()' - Parse an IPP value.
//

static bool				// O  - `true` on success or `false` on error
parse_value(ipp_file_t      *file,	// I  - IPP data file
            ipp_t           *ipp,	// I  - IPP message
            ipp_attribute_t **attr,	// IO - IPP attribute
            int             element)	// I  - Element number
{
  char		value[2049],		// Value string
		*valueptr,		// Pointer into value string
		temp[2049],		// Temporary string
		*tempptr;		// Pointer into temporary string
  size_t	valuelen;		// Length of value


  ippFileSavePosition(file);

  if (!ippFileReadToken(file, temp, sizeof(temp)))
  {
    report_error(file, "Missing value on line %d of '%s'.", file->linenum, file->filename);
    return (false);
  }

  ippFileExpandVars(file, value, temp, sizeof(value));

  switch (ippGetValueTag(*attr))
  {
    case IPP_TAG_BOOLEAN :
        return (ippSetBoolean(ipp, attr, element, !_cups_strcasecmp(value, "true")));

    case IPP_TAG_ENUM :
    case IPP_TAG_INTEGER :
        return (ippSetInteger(ipp, attr, element, (int)strtol(value, NULL, 0)));

    case IPP_TAG_DATE :
        {
          int		year,		// Year
			month,		// Month
			day,		// Day of month
			hour,		// Hour
			minute,		// Minute
			second,		// Second
			utc_offset = 0;	// Timezone offset from UTC
          ipp_uchar_t	date[11];	// dateTime value

          if (*value == 'P')
          {
            // Time period...
            time_t	curtime;	// Current time in seconds
            int		period = 0;	// Current period value
	    bool	saw_T = false;	// Saw time separator?

            curtime = time(NULL);

            for (valueptr = value + 1; *valueptr; valueptr ++)
            {
              if (isdigit(*valueptr & 255))
              {
                period = (int)strtol(valueptr, &valueptr, 10);

                if (!valueptr || period < 0)
                {
		  report_error(file, "Bad dateTime value \"%s\" on line %d of '%s'.", value, file->linenum, file->filename);
		  return (false);
		}
              }

              if (*valueptr == 'Y')
              {
                curtime += 365 * 86400 * period;
                period  = 0;
              }
              else if (*valueptr == 'M')
              {
                if (saw_T)
                  curtime += 60 * period;
                else
                  curtime += 30 * 86400 * period;

                period = 0;
              }
              else if (*valueptr == 'D')
              {
                curtime += 86400 * period;
                period  = 0;
              }
              else if (*valueptr == 'H')
              {
                curtime += 3600 * period;
                period  = 0;
              }
              else if (*valueptr == 'S')
              {
                curtime += period;
                period = 0;
              }
              else if (*valueptr == 'T')
              {
                saw_T  = true;
                period = 0;
              }
              else
	      {
		report_error(file, "Bad dateTime value \"%s\" on line %d of '%s'.", value, file->linenum, file->filename);
		return (false);
	      }
	    }

	    return (ippSetDate(ipp, attr, element, ippTimeToDate(curtime)));
          }
          else if (sscanf(value, "%d-%d-%dT%d:%d:%d%d", &year, &month, &day, &hour, &minute, &second, &utc_offset) < 6)
          {
            // Date/time value did not parse...
	    report_error(file, "Bad dateTime value \"%s\" on line %d of '%s'.", value, file->linenum, file->filename);
	    return (false);
          }

          date[0] = (ipp_uchar_t)(year >> 8);
          date[1] = (ipp_uchar_t)(year & 255);
          date[2] = (ipp_uchar_t)month;
          date[3] = (ipp_uchar_t)day;
          date[4] = (ipp_uchar_t)hour;
          date[5] = (ipp_uchar_t)minute;
          date[6] = (ipp_uchar_t)second;
          date[7] = 0;
          if (utc_offset < 0)
          {
            utc_offset = -utc_offset;
            date[8]    = (ipp_uchar_t)'-';
	  }
	  else
	  {
            date[8] = (ipp_uchar_t)'+';
	  }

          date[9]  = (ipp_uchar_t)(utc_offset / 100);
          date[10] = (ipp_uchar_t)(utc_offset % 100);

          return (ippSetDate(ipp, attr, element, date));
        }

    case IPP_TAG_RESOLUTION :
	{
	  int	xres,		// X resolution
		yres;		// Y resolution
	  char	*ptr;		// Pointer into value

	  xres = yres = (int)strtol(value, (char **)&ptr, 10);
	  if (ptr > value && xres > 0)
	  {
	    if (*ptr == 'x')
	      yres = (int)strtol(ptr + 1, (char **)&ptr, 10);
	  }

	  if (*value && (ptr <= value || xres <= 0 || yres <= 0 || !ptr || (_cups_strcasecmp(ptr, "dpi") && _cups_strcasecmp(ptr, "dpc") && _cups_strcasecmp(ptr, "dpcm") && _cups_strcasecmp(ptr, "other"))))
	  {
	    report_error(file, "Bad resolution value \"%s\" on line %d of '%s'.", value, file->linenum, file->filename);
	    return (false);
	  }

	  if (!_cups_strcasecmp(ptr, "dpi"))
	    return (ippSetResolution(ipp, attr, element, IPP_RES_PER_INCH, xres, yres));
	  else if (!_cups_strcasecmp(ptr, "dpc") || !_cups_strcasecmp(ptr, "dpcm"))
	    return (ippSetResolution(ipp, attr, element, IPP_RES_PER_CM, xres, yres));
	  else
	    return (ippSetResolution(ipp, attr, element, (ipp_res_t)0, xres, yres));
	}

    case IPP_TAG_RANGE :
	{
	  int	lower,			// Lower value
		upper;			// Upper value

          if (sscanf(value, "%d-%d", &lower, &upper) != 2)
          {
	    report_error(file, "Bad rangeOfInteger value \"%s\" on line %d of '%s'.", value, file->linenum, file->filename);
	    return (false);
	  }

	  return (ippSetRange(ipp, attr, element, lower, upper));
	}

    case IPP_TAG_STRING :
        valuelen = strlen(value);

        if (value[0] == '<' && value[strlen(value) - 1] == '>')
        {
          if (valuelen & 1)
          {
	    report_error(file, "Bad octetString value on line %d of '%s'.", file->linenum, file->filename);
	    return (false);
          }

          valueptr = value + 1;
          tempptr  = temp;

          while (*valueptr && *valueptr != '>' && tempptr < (temp + sizeof(temp)))
          {
	    if (!isxdigit(valueptr[0] & 255) || !isxdigit(valueptr[1] & 255))
	    {
	      report_error(file, "Bad octetString value on line %d of '%s'.", file->linenum, file->filename);
	      return (false);
	    }

            if (valueptr[0] >= '0' && valueptr[0] <= '9')
              *tempptr = (char)((valueptr[0] - '0') << 4);
	    else
              *tempptr = (char)((tolower(valueptr[0]) - 'a' + 10) << 4);

            if (valueptr[1] >= '0' && valueptr[1] <= '9')
              *tempptr |= (valueptr[1] - '0');
	    else
              *tempptr |= (tolower(valueptr[1]) - 'a' + 10);

            tempptr ++;
            valueptr += 2;
          }

	  if (*valueptr != '>')
	  {
	    if (*valueptr)
	      report_error(file, "octetString value too long on line %d of '%s'.", file->linenum, file->filename);
	    else
	      report_error(file, "Bad octetString value on line %d of '%s'.", file->linenum, file->filename);

	    return (false);
	  }

          return (ippSetOctetString(ipp, attr, element, temp, (size_t)(tempptr - temp)));
        }
        else
          return (ippSetOctetString(ipp, attr, element, value, valuelen));

    case IPP_TAG_TEXTLANG :
    case IPP_TAG_NAMELANG :
    case IPP_TAG_TEXT :
    case IPP_TAG_NAME :
    case IPP_TAG_KEYWORD :
    case IPP_TAG_URI :
    case IPP_TAG_URISCHEME :
    case IPP_TAG_CHARSET :
    case IPP_TAG_LANGUAGE :
    case IPP_TAG_MIMETYPE :
        return (ippSetString(ipp, attr, element, value));

    case IPP_TAG_BEGIN_COLLECTION :
        {
          bool	status;			// Add status
          ipp_t *col;			// Collection value

          ippFileRestorePosition(file);

          if ((col = ippFileReadCollection(file)) == NULL)
	    return (false);

	  status = ippSetCollection(ipp, attr, element, col);
	  ippDelete(col);

	  return (status);
	}

    default :
        report_error(file, "Unsupported value on line %d of '%s'.", file->linenum, file->filename);
	return (false);
  }
}


//
// 'report_error()' - Report an error.
//

static bool				// O - `true` to continue, `false` to stop
report_error(
    ipp_file_t *file,			// I - IPP data file
    const char *message,		// I - Printf-style message
    ...)				// I - Additional arguments as needed
{
  va_list	ap;			// Argument pointer
  char		buffer[8192];		// Formatted string


  va_start(ap, message);
  vsnprintf(buffer, sizeof(buffer), message, ap);
  va_end(ap);

  if (file->error_cb)
    return ((*file->error_cb)(file, file->cb_data, buffer));

  fprintf(stderr, "%s\n", buffer);
  return (false);
}


//
// 'write_string()' - Write a quoted string value.
//

static bool				// O - `true` on success, `false` on failure
write_string(ipp_file_t *file,		// I - IPP data file
             const char *s,		// I - String
             size_t     len)		// I - Length of string
{
  bool		ret = true;		// Return value
  const char	*start,			// Start of string
		*ptr,			// Pointer into string
		*end;			// End of string


  // Start with a double quote...
  ret &= cupsFilePutChar(file->fp, '\"');
  file->column ++;

  // Loop through the string...
  for (start = s, end = s + len, ptr = start; ptr < end; ptr ++)
  {
    if (*ptr == '\"' || *ptr == '\\')
    {
      // Something that needs to be quoted...
      if (ptr > start)
      {
        // Write lead-in text...
	ret &= cupsFileWrite(file->fp, start, (size_t)(ptr - start));
	file->column += ptr - start;
      }

      // Then quote the " or \...
      ret &= cupsFilePrintf(file->fp, "\\%c", *ptr);
      start = ptr + 1;
      file->column ++;
    }
  }

  if (ptr > start)
  {
    ret &= cupsFileWrite(file->fp, start, (size_t)(ptr - start));
    file->column += ptr - start;
  }

  ret &= cupsFilePutChar(file->fp, '\"');
  file->column ++;

  return (ret);
}
