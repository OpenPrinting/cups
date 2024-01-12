//
// Private IPP definitions for CUPS.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright © 2007-2018 by Apple Inc.
// Copyright © 1997-2006 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _CUPS_IPP_PRIVATE_H_
#  define _CUPS_IPP_PRIVATE_H_
#  include <cups/cups.h>
#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus


//
// Constants...
//

#  define IPP_BUF_SIZE	(IPP_MAX_LENGTH + 2)
					// Size of buffer
#  define _IPP_MAX_FIND	8		// Size of find stack


//
// Structures...
//

typedef union _ipp_request_u		// Request Header
{
  struct				// Any Header
  {
    ipp_uchar_t	version[2];		// Protocol version number
    int		op_status;		// Operation ID or status code
    int		request_id;		// Request ID
  }		any;

  struct				// Operation Header
  {
    ipp_uchar_t	version[2];		// Protocol version number
    ipp_op_t	operation_id;		// Operation ID
    int		request_id;		// Request ID
  }		op;

  struct				// Status Header
  {
    ipp_uchar_t	version[2];		// Protocol version number
    ipp_status_t status_code;		// Status code
    int		request_id;		// Request ID
  }		status;

  struct				// Event Header
  {
    ipp_uchar_t	version[2];		// Protocol version number
    ipp_status_t status_code;		// Status code
    int		request_id;		// Request ID
  }		event;
} _ipp_request_t;

typedef union _ipp_value_u		// Attribute Value
{
  int		integer;		// Integer/enumerated value

  char		boolean;		// Boolean value

  ipp_uchar_t	date[11];		// Date/time value

  struct
  {
    int		xres,			// Horizontal resolution
		yres;			// Vertical resolution
    ipp_res_t	units;			// Resolution units
  }		resolution;		// Resolution value

  struct
  {
    int		lower,			// Lower value
		upper;			// Upper value
  }		range;			// Range of integers value

  struct
  {
    char	*language;		// Language code
    char	*text;			// String
  }		string;			// String with language value

  struct
  {
    int		length;			// Length of attribute
    void	*data;			// Data in attribute
  }		unknown;		// Unknown attribute type

  ipp_t		*collection;		// Collection value
} _ipp_value_t;

struct _ipp_attribute_s			// IPP attribute
{
  ipp_attribute_t *next;		// Next attribute in list
  ipp_tag_t	group_tag,		// Job/Printer/Operation group tag
		value_tag;		// What type of value is it?
  char		*name;			// Name of attribute
  int		num_values;		// Number of values
  _ipp_value_t	values[1];		// Values
};

typedef struct _ipp_find_s		// IPP find data
{
  ipp_attribute_t	*attr;		// Current attribute
  int			idx;		// Current attribute index for hierarchical search
  bool			atend;		// At the end of the message?
} _ipp_find_t;

struct _ipp_s				// IPP Request/Response/Notification
{
  ipp_state_t		state;		// State of request
  _ipp_request_t	request;	// Request header
  ipp_attribute_t	*attrs;		// Attributes
  ipp_attribute_t	*last;		// Last attribute in list
  ipp_attribute_t	*current;	// Current attribute (for read/write)
  ipp_tag_t		curtag;		// Current attribute group tag
  ipp_attribute_t	*prev;		// Previous attribute (for read)
  int			use;		// Use count
  _ipp_find_t		fstack[_IPP_MAX_FIND];
					// Find stack
  _ipp_find_t		*find;		// Current find
};

typedef struct _ipp_option_s		// Attribute mapping data
{
  int		multivalue;		// Option has multiple values?
  const char	*name;			// Option/attribute name
  ipp_tag_t	value_tag;		// Value tag for this attribute
  ipp_tag_t	group_tag;		// Group tag for this attribute
  ipp_tag_t	alt_group_tag;		/* Alternate group tag for this
					 * attribute */
  const ipp_op_t *operations;		// Allowed operations for this attr
} _ipp_option_t;


//
// Functions...
//

#ifdef DEBUG
extern const char	*_ippCheckOptions(void) _CUPS_PRIVATE;
#endif // DEBUG
extern _ipp_option_t	*_ippFindOption(const char *name) _CUPS_PRIVATE;


#  ifdef __cplusplus
}
#  endif // __cplusplus
#endif // !_CUPS_IPP_H_
