---
title: CUPS Programming Manual
author: Michael R Sweet
copyright: Copyright © 2020-2025 by OpenPrinting. All Rights Reserved.
version: 2.5.0
...

> Please [file issues on GitHub](https://github.com/openprinting/cups/issues) to
> provide feedback on this document.


# Introduction

CUPS provides the "cups" library to talk to the different parts of CUPS and with
Internet Printing Protocol (IPP) printers. The "cups" library functions are
accessed by including the `<cups/cups.h>` header.

CUPS is based on the Internet Printing Protocol ("IPP"), which allows clients
(applications) to communicate with a server (the scheduler, printers, etc.) to
get a list of destinations, send print jobs, and so forth.  You identify which
server you want to communicate with using a pointer to the opaque structure
`http_t`.  The `CUPS_HTTP_DEFAULT` constant can be used when you want to talk to
the CUPS scheduler.


## Guidelines

When writing software (other than printer drivers) that uses the "cups" library:

- Do not use undocumented or deprecated APIs,
- Do not rely on pre-configured printers,
- Do not assume that printers support specific features or formats, and
- Do not rely on implementation details (PPDs, etc.)

CUPS is designed to insulate users and developers from the implementation
details of printers and file formats.  The goal is to allow an application to
supply a print file in a standard format with the user intent ("print four
copies, two-sided on A4 media, and staple each copy") and have the printing
system manage the printer communication and format conversion needed.

Similarly, printer and job management applications can use standard query
operations to obtain the status information in a common, generic form and use
standard management operations to control the state of those printers and jobs.

> **Note:**
>
> CUPS printer drivers necessarily depend on specific file formats and certain
> implementation details of the CUPS software.  Please consult the Postscript
> and raster printer driver developer documentation on the
> [OpenPrinting CUPS website](https://openprinting.github.io/cups) for more
> information.


## Terms Used in This Document

A *Destination* is a printer or print queue that accepts print jobs.  A
*Print Job* is a collection of one or more documents that are processed by a
destination using options supplied when creating the job.  A *Document* is a
file (JPEG image, PDF file, etc.) suitable for printing.  An *Option* controls
some aspect of printing, such as the media used. *Media* is the sheets or roll
that is printed on.  An *Attribute* is an option encoded for an Internet
Printing Protocol (IPP) request.


## Compiling Programs That Use the CUPS API

The CUPS libraries can be used from any C, C++, or Objective-C program.
The method of compiling against the libraries varies depending on the
operating system and installation of CUPS. The following sections show how
to compile a simple program (shown below) in two common environments.

The following simple program lists the available destinations:

```c
#include <stdio.h>
#include <cups/cups.h>

int print_dest(void *user_data, unsigned flags, cups_dest_t *dest)
{
  if (dest->instance)
    printf("%s/%s\n", dest->name, dest->instance);
  else
    puts(dest->name);

  return (1);
}

int main(void)
{
  cupsEnumDests(CUPS_DEST_FLAGS_NONE, 1000, NULL, 0, 0, print_dest, NULL);

  return (0);
}
```


### Compiling with Xcode

In Xcode, choose *New Project...* from the *File* menu (or press SHIFT+CMD+N),
then select the *Command Line Tool* under the macOS Application project type.
Click *Next* and enter a name for the project, for example "firstcups".  Click
*Next* and choose a project directory. The click *Next* to create the project.

In the project window, click on the *Build Phases* group and expand the
*Link Binary with Libraries* section. Click *+*, type "libcups" to show the
library, and then double-click on `libcups.tbd`.

Finally, click on the `main.c` file in the sidebar and copy the example program
to the file.  Build and run (CMD+R) to see the list of destinations.


### Compiling with GCC

From the command-line, create a file called `simple.c` using your favorite
editor, copy the example to this file, and save.  Then run the following command
to compile it with GCC and run it:

    gcc -o simple `pkg-config --cflags cups` simple.c `pkg-config --libs cups`
    ./simple

The `pkg-config` command provides the compiler flags (`pkg-config --cflags cups`)
and libraries (`pkg-config --libs cups`) needed for the local system.


# Working with Destinations

Destinations, which in CUPS represent individual printers or classes
(collections or pools) of printers, are represented by the `cups_dest_t`
structure which includes the name \(`name`), instance \(`instance`, saved
options/settings), whether the destination is the default for the user
\(`is_default`), and the options and basic information associated with that
destination \(`num_options` and `options`).

Historically destinations have been manually maintained by the administrator of
a system or network, but CUPS also supports dynamic discovery of destinations on
the current network.


## Finding Available Destinations

The [`cupsEnumDests`](@@) function finds all of the available destinations:

```c
int
cupsEnumDests(unsigned flags, int msec, int *cancel,
              cups_ptype_t type, cups_ptype_t mask,
              cups_dest_cb_t cb, void *user_data)
```

The `flags` argument specifies enumeration options, which at present must be
`CUPS_DEST_FLAGS_NONE`.

The `msec` argument specifies the maximum amount of time that should be used for
enumeration in milliseconds - interactive applications should keep this value to
5000 or less when run on the main thread.

The `cancel` argument points to an integer variable that, when set to a non-zero
value, will cause enumeration to stop as soon as possible.  It can be `NULL` if
not needed.

The `type` and `mask` arguments are bitfields that allow the caller to filter
the destinations based on categories and/or capabilities.  The destination's
"printer-type" value is masked by the `mask` value and compared to the `type`
value when filtering.  For example, to only enumerate destinations that are
hosted on the local system, pass `CUPS_PTYPE_LOCAL` for the `type` argument
and `CUPS_PTYPE_DISCOVERED` for the `mask` argument.  The following constants
can be used for filtering:

- `CUPS_PTYPE_CLASS`: A collection of destinations.
- `CUPS_PTYPE_FAX`: A facsimile device.
- `CUPS_PTYPE_LOCAL`: A local printer or class.  This constant has the value 0
  (no bits set) and is only used for the `type` argument and is paired with the
  `CUPS_PTYPE_REMOTE` or `CUPS_PTYPE_DISCOVERED` constant passed in the
  `mask` argument.
- `CUPS_PTYPE_REMOTE`: A remote (shared) printer or class.
- `CUPS_PTYPE_DISCOVERED`: An available network printer or class.
- `CUPS_PTYPE_BW`: Can do B&W printing.
- `CUPS_PTYPE_COLOR`: Can do color printing.
- `CUPS_PTYPE_DUPLEX`: Can do two-sided printing.
- `CUPS_PTYPE_STAPLE`: Can staple output.
- `CUPS_PTYPE_COLLATE`: Can quickly collate copies.
- `CUPS_PTYPE_PUNCH`: Can punch output.
- `CUPS_PTYPE_COVER`: Can cover output.
- `CUPS_PTYPE_BIND`: Can bind output.
- `CUPS_PTYPE_SORT`: Can sort output (mailboxes, etc.)
- `CUPS_PTYPE_SMALL`: Can print on Letter/Legal/A4-size media.
- `CUPS_PTYPE_MEDIUM`: Can print on Tabloid/B/C/A3/A2-size media.
- `CUPS_PTYPE_LARGE`: Can print on D/E/A1/A0-size media.
- `CUPS_PTYPE_VARIABLE`: Can print on rolls and custom-size media.

The `cb` argument specifies a function to call for every destination that is
found:

```c
typedef int (*cups_dest_cb_t)(void *user_data,
                              unsigned flags,
                              cups_dest_t *dest);
```

The callback function receives a copy of the `user_data` argument along with a
bitfield \(`flags`) and the destination that was found.  The `flags` argument
can have any of the following constant (bit) values set:

- `CUPS_DEST_FLAGS_MORE`: There are more destinations coming.
- `CUPS_DEST_FLAGS_REMOVED`: The destination has gone away and should be removed
  from the list of destinations a user can select.
- `CUPS_DEST_FLAGS_ERROR`: An error occurred.  The reason for the error can be
  found by calling the [`cupsGetError`](@@) and/or [`cupsGetErrorString`](@@)
  functions.

The callback function returns `0` to stop enumeration or `1` to continue.

> **Note:**
>
> The callback function will likely be called multiple times for the
> same destination, so it is up to the caller to suppress any duplicate
> destinations.

The following example shows how to use `cupsEnumDests` to get a filtered array
of destinations:

```c
typedef struct
{
  int num_dests;
  cups_dest_t *dests;
} my_user_data_t;

int
my_dest_cb(my_user_data_t *user_data, unsigned flags,
           cups_dest_t *dest)
{
  if (flags & CUPS_DEST_FLAGS_REMOVED)
  {
   /*
    * Remove destination from array...
    */

    user_data->num_dests =
        cupsRemoveDest(dest->name, dest->instance,
                       user_data->num_dests,
                       &(user_data->dests));
  }
  else
  {
   /*
    * Add destination to array...
    */

    user_data->num_dests =
        cupsCopyDest(dest, user_data->num_dests,
                     &(user_data->dests));
  }

  return (1);
}

int
my_get_dests(cups_ptype_t type, cups_ptype_t mask,
             cups_dest_t **dests)
{
  my_user_data_t user_data = { 0, NULL };

  if (!cupsEnumDests(CUPS_DEST_FLAGS_NONE, 1000, NULL, type,
                     mask, (cups_dest_cb_t)my_dest_cb,
                     &user_data))
  {
   /*
    * An error occurred, free all of the destinations and
    * return...
    */

    cupsFreeDests(user_data.num_dests, user_data.dests);

    *dests = NULL;

    return (0);
  }

 /*
  * Return the destination array...
  */

  *dests = user_data.dests;

  return (user_data.num_dests);
}
```


## Basic Destination Information

The `num_options` and `options` members of the `cups_dest_t` structure provide
basic attributes about the destination in addition to the user default options
and values for that destination.  The following names are predefined for various
destination attributes:

- "auth-info-required": The type of authentication required for printing to this
  destination: "none", "username,password", "domain,username,password", or
  "negotiate" (Kerberos).
- "printer-info": The human-readable description of the destination such as "My
  Laser Printer".
- "printer-is-accepting-jobs": "true" if the destination is accepting new jobs,
  "false" otherwise.
- "printer-is-shared": "true" if the destination is being shared with other
  computers, "false" otherwise.
- "printer-location": The human-readable location of the destination such as
  "Lab 4".
- "printer-make-and-model": The human-readable make and model of the destination
  such as "ExampleCorp LaserPrinter 4000 Series".
- "printer-state": "3" if the destination is idle, "4" if the destination is
  printing a job, and "5" if the destination is stopped.
- "printer-state-change-time": The UNIX time when the destination entered the
  current state.
- "printer-state-reasons": Additional comma-delimited state keywords for the
  destination such as "media-tray-empty-error" and "toner-low-warning".
- "printer-type": The `cups_ptype_t` value associated with the destination.
- "printer-uri-supported": The URI associated with the destination; if not set,
  this destination was discovered but is not yet setup as a local printer.

Use the [`cupsGetOption`](@@) function to retrieve the value.  For example, the
following code gets the make and model of a destination:

```c
const char *model = cupsGetOption("printer-make-and-model",
                                  dest->num_options,
                                  dest->options);
```


## Detailed Destination Information

Once a destination has been chosen, the [`cupsCopyDestInfo`](@@) function can be
used to gather detailed information about the destination:

```c
cups_dinfo_t *
cupsCopyDestInfo(http_t *http, cups_dest_t *dest);
```

The `http` argument specifies a connection to the CUPS scheduler and is
typically the constant `CUPS_HTTP_DEFAULT`.  The `dest` argument specifies the
destination to query.

The `cups_dinfo_t` structure that is returned contains a snapshot of the
supported options and their supported, ready, and default values.  It also can
report constraints between different options and values, and recommend changes
to resolve those constraints.


### Getting Supported Options and Values

The [`cupsCheckDestSupported`](@@) function can be used to test whether a
particular option or option and value is supported:

```c
int
cupsCheckDestSupported(http_t *http, cups_dest_t *dest,
                       cups_dinfo_t *info,
                       const char *option,
                       const char *value);
```

The `option` argument specifies the name of the option to check.  The following
constants can be used to check the various standard options:

- `CUPS_COPIES`: Controls the number of copies that are produced.
- `CUPS_FINISHINGS`: A comma-delimited list of integer constants that control
  the finishing processes that are applied to the job, including stapling,
  punching, and folding.
- `CUPS_MEDIA`: Controls the media size that is used, typically one of the
  following: `CUPS_MEDIA_3X5`, `CUPS_MEDIA_4X6`, `CUPS_MEDIA_5X7`,
  `CUPS_MEDIA_8X10`, `CUPS_MEDIA_A3`, `CUPS_MEDIA_A4`, `CUPS_MEDIA_A5`,
  `CUPS_MEDIA_A6`, `CUPS_MEDIA_ENV10`, `CUPS_MEDIA_ENVDL`, `CUPS_MEDIA_LEGAL`,
  `CUPS_MEDIA_LETTER`, `CUPS_MEDIA_PHOTO_L`, `CUPS_MEDIA_SUPERBA3`, or
  `CUPS_MEDIA_TABLOID`.
- `CUPS_MEDIA_SOURCE`: Controls where the media is pulled from, typically either
  `CUPS_MEDIA_SOURCE_AUTO` or `CUPS_MEDIA_SOURCE_MANUAL`.
- `CUPS_MEDIA_TYPE`: Controls the type of media that is used, typically one of
  the following: `CUPS_MEDIA_TYPE_AUTO`, `CUPS_MEDIA_TYPE_ENVELOPE`,
  `CUPS_MEDIA_TYPE_LABELS`, `CUPS_MEDIA_TYPE_LETTERHEAD`,
  `CUPS_MEDIA_TYPE_PHOTO`, `CUPS_MEDIA_TYPE_PHOTO_GLOSSY`,
  `CUPS_MEDIA_TYPE_PHOTO_MATTE`, `CUPS_MEDIA_TYPE_PLAIN`, or
  `CUPS_MEDIA_TYPE_TRANSPARENCY`.
- `CUPS_NUMBER_UP`: Controls the number of document pages that are placed on
  each media side.
- `CUPS_ORIENTATION`: Controls the orientation of document pages placed on the
  media: `CUPS_ORIENTATION_PORTRAIT` or `CUPS_ORIENTATION_LANDSCAPE`.
- `CUPS_PRINT_COLOR_MODE`: Controls whether the output is in color
  \(`CUPS_PRINT_COLOR_MODE_COLOR`), grayscale
  \(`CUPS_PRINT_COLOR_MODE_MONOCHROME`), or either
  \(`CUPS_PRINT_COLOR_MODE_AUTO`).
- `CUPS_PRINT_QUALITY`: Controls the generate quality of the output:
  `CUPS_PRINT_QUALITY_DRAFT`, `CUPS_PRINT_QUALITY_NORMAL`, or
  `CUPS_PRINT_QUALITY_HIGH`.
- `CUPS_SIDES`: Controls whether prints are placed on one or both sides of the
  media: `CUPS_SIDES_ONE_SIDED`, `CUPS_SIDES_TWO_SIDED_PORTRAIT`, or
  `CUPS_SIDES_TWO_SIDED_LANDSCAPE`.

If the `value` argument is `NULL`, the `cupsCheckDestSupported` function returns
whether the option is supported by the destination.  Otherwise, the function
returns whether the specified value of the option is supported.

The [`cupsFindDestSupported`](@@) function returns the IPP attribute containing
the supported values for a given option:

```c
ipp_attribute_t *
cupsFindDestSupported(http_t *http, cups_dest_t *dest,
                      cups_dinfo_t *dinfo,
                      const char *option);
```

For example, the following code prints the supported finishing processes for a
destination, if any, to the standard output:

```c
cups_dinfo_t *info = cupsCopyDestInfo(CUPS_HTTP_DEFAULT,
                                      dest);

if (cupsCheckDestSupported(CUPS_HTTP_DEFAULT, dest, info,
                           CUPS_FINISHINGS, NULL))
{
  ipp_attribute_t *finishings =
      cupsFindDestSupported(CUPS_HTTP_DEFAULT, dest, info,
                            CUPS_FINISHINGS);
  int i, count = ippGetCount(finishings);

  puts("finishings supported:");
  for (i = 0; i < count; i ++)
  {
    int val = ippGetInteger(finishings, i);
    printf("  %d (%s)\n", val,
           ippEnumString("finishings", val));
}
else
{
  puts("finishings not supported.");
}
```

The "job-creation-attributes" option can be queried to get a list of supported
options.  For example, the following code prints the list of supported options
to the standard output:

```c
ipp_attribute_t *attrs =
    cupsFindDestSupported(CUPS_HTTP_DEFAULT, dest, info,
                          "job-creation-attributes");
int i, count = ippGetCount(attrs);

for (i = 0; i < count; i ++)
  puts(ippGetString(attrs, i, NULL));
```


### Getting Default Values

There are two sets of default values - user defaults that are available via the
`num_options` and `options` members of the `cups_dest_t` structure, and
destination defaults that available via the `cups_dinfo_t` structure and the
[`cupsFindDestDefault`](@@) function which returns the IPP attribute containing
the default value(s) for a given option:

```c
ipp_attribute_t *
cupsFindDestDefault(http_t *http, cups_dest_t *dest,
                    cups_dinfo_t *dinfo,
                    const char *option);
```

The user defaults from [`cupsGetOption`](@@) should always take preference over
the destination defaults.  For example, the following code prints the default
finishings value(s) to the standard output:

```c
const char *def_value =
    cupsGetOption(CUPS_FINISHINGS, dest->num_options,
                  dest->options);
ipp_attribute_t *def_attr =
    cupsFindDestDefault(CUPS_HTTP_DEFAULT, dest, info,
                        CUPS_FINISHINGS);

if (def_value != NULL)
{
  printf("Default finishings: %s\n", def_value);
}
else
{
  int i, count = ippGetCount(def_attr);

  printf("Default finishings: %d",
         ippGetInteger(def_attr, 0));
  for (i = 1; i < count; i ++)
    printf(",%d", ippGetInteger(def_attr, i));
  putchar('\n');
}
```


### Getting Ready (Loaded) Values

The finishings and media options also support queries for the ready, or loaded,
values.  For example, a printer may have punch and staple finishers installed
but be out of staples - the supported values will list both punch and staple
finishing processes but the ready values will only list the punch processes.
Similarly, a printer may support hundreds of different sizes of media but only
have a single size loaded at any given time - the ready values are limited to
the media that is actually in the printer.

The [`cupsFindDestReady`](@@) function finds the IPP attribute containing the
ready values for a given option:

```c
ipp_attribute_t *
cupsFindDestReady(http_t *http, cups_dest_t *dest,
                  cups_dinfo_t *dinfo, const char *option);
```

For example, the following code lists the ready finishing processes:

```c
ipp_attribute_t *ready_finishings =
    cupsFindDestReady(CUPS_HTTP_DEFAULT, dest, info,
                      CUPS_FINISHINGS);

if (ready_finishings != NULL)
{
  int i, count = ippGetCount(ready_finishings);

  puts("finishings ready:");
  for (i = 0; i < count; i ++)
  {
    int val = ippGetInteger(ready_finishings, i);
    printf("  %d (%s)\n", val,
           ippEnumString("finishings", val));
}
else
{
  puts("no finishings are ready.");
}
```


### Media Options

CUPS provides functions for querying the dimensions, margins, color, source
(tray/roll), and type for each of the supported media size options.  The
`cups_media_t` structure is used to describe media:

```c
typedef struct cups_media_s
{
  char media[128];
  char color[128];
  char source[128];
  char type[128];
  int width, length;
  int bottom, left, right, top;
} cups_media_t;
```

The "media" member specifies a PWG self-describing media size name such as
"na\_letter\_8.5x11in", "iso\_a4\_210x297mm", etc.  The "color" member specifies
a PWG media color name such as "white", "blue", etc.  The "source" member
specifies a standard keyword for the paper tray or roll such as "tray-1",
"manual", "by-pass-tray" (multi-purpose tray), etc.  The "type" member specifies
a PWG media type name such as "stationery" (plain paper), "photographic",
"envelope", "transparency", etc.

The `width` and `length` members specify the dimensions of the media in
hundredths of millimeters (1/2540th of an inch).  The `bottom`, `left`, `right`,
and `top` members specify the margins of the printable area, also in hundredths
of millimeters.

The [`cupsGetDestMediaByName2`](@@) and [`cupsGetDestMediaBySize2`](@@)
functions lookup the media information using a standard media size name or
dimensions in hundredths of millimeters:

```c
bool
cupsGetDestMediaByName2(http_t *http, cups_dest_t *dest,
                        cups_dinfo_t *dinfo,
                        const char *name,
                        unsigned flags, cups_media_t *media);

bool
cupsGetDestMediaBySize2(http_t *http, cups_dest_t *dest,
                        cups_dinfo_t *dinfo,
                        int width, int length,
                        unsigned flags, cups_media_t *media);
```

The `name`, `width`, and `length` arguments specify the size to lookup.  The
`flags` argument specifies a bitfield controlling various lookup options:

- `CUPS_MEDIA_FLAGS_DEFAULT`: Find the closest size supported by the printer.
- `CUPS_MEDIA_FLAGS_BORDERLESS`: Find a borderless size.
- `CUPS_MEDIA_FLAGS_DUPLEX`: Find a size compatible with two-sided printing.
- `CUPS_MEDIA_FLAGS_EXACT`: Find an exact match for the size.
- `CUPS_MEDIA_FLAGS_READY`: If the printer supports media sensing or
  configuration of the media in each tray/source, find the size amongst the
  "ready" media.

If a matching size is found for the destination, the size information is stored
in the structure pointed to by the `media` argument and `true` is returned.
Otherwise `false` is returned.

For example, the following code prints the margins for two-sided printing on US
Letter media:

```c
cups_media_t media:

if (cupsGetDestMediaByName2(CUPS_HTTP_DEFAULT, dest, info,
                            CUPS_MEDIA_LETTER,
                            CUPS_MEDIA_FLAGS_DUPLEX, &size))
{
  puts("Margins for duplex US Letter:");
  printf("  Bottom: %.2fin\n", media.bottom / 2540.0);
  printf("    Left: %.2fin\n", media.left / 2540.0);
  printf("   Right: %.2fin\n", media.right / 2540.0);
  printf("     Top: %.2fin\n", media.top / 2540.0);
}
else
{
  puts("Margins for duplex US Letter are not available.");
}
```

You can also enumerate all of the sizes that match a given `flags` value using
the [`cupsGetDestMediaByIndex2`](@@) and [`cupsGetDestMediaCount`](@@)
functions:

```c
bool
cupsGetDestMediaByIndex2(http_t *http, cups_dest_t *dest,
                         cups_dinfo_t *dinfo, size_t n,
                         unsigned flags, cups_media_t *media);

int
cupsGetDestMediaCount(http_t *http, cups_dest_t *dest,
                      cups_dinfo_t *dinfo, unsigned flags);
```

For example, the following code prints the list of ready media and corresponding
margins:

```c
cups_media_t media;
size_t i;
size_t count = (size_t)cupsGetDestMediaCount(CUPS_HTTP_DEFAULT,
                                             dest, info,
                                             CUPS_MEDIA_FLAGS_READY);

for (i = 0; i < count; i ++)
{
  if (cupsGetDestMediaByIndex2(CUPS_HTTP_DEFAULT, dest, info,
                               i, CUPS_MEDIA_FLAGS_READY,
                               &media))
  {
    printf("%s:\n", media.name);
    printf("   Width: %.2fin\n", media.width / 2540.0);
    printf("  Length: %.2fin\n", media.length / 2540.0);
    printf("  Bottom: %.2fin\n", media.bottom / 2540.0);
    printf("    Left: %.2fin\n", media.left / 2540.0);
    printf("   Right: %.2fin\n", media.right / 2540.0);
    printf("     Top: %.2fin\n", media.top / 2540.0);
  }
}
```

Finally, the [`cupsGetDestMediaDefault2`](@@) function returns the default
media:

```c
int
cupsGetDestMediaDefault2(http_t *http, cups_dest_t *dest,
                         cups_dinfo_t *dinfo, unsigned flags,
                         cups_media_t *media);
```


### Localizing Options and Values

CUPS provides three functions to get localized, human-readable strings in the
user's current locale for options and values: [`cupsLocalizeDestMedia2`](@@),
[`cupsLocalizeDestOption`](@@), and [`cupsLocalizeDestValue`](@@):

```c
const char *
cupsLocalizeDestMedia2(http_t *http, cups_dest_t *dest,
                       cups_dinfo_t *info, unsigned flags,
                       cups_media_t *media);

const char *
cupsLocalizeDestOption(http_t *http, cups_dest_t *dest,
                       cups_dinfo_t *info,
                       const char *option);

const char *
cupsLocalizeDestValue(http_t *http, cups_dest_t *dest,
                      cups_dinfo_t *info,
                      const char *option, const char *value);
```

> **Note:**
>
> These functions require a valid `http_t` connection to work.  Use the
> [`cupsConnectDest`](@@) function to connect to the destination for its
> localization information.

For example, the following code will list the localized media names for a
destination:

```c
char resource[256];
http_t *http = cupsConnectDest(dest, CUPS_DEST_FLAGS_NONE,
                               /*msec*/30000, /*cancel*/NULL,
                               resource, sizeof(resource),
                               /*dest_cb*/NULL, /*user_data*/NULL);

size_t i;
size_t count = (size_t)cupsGetDestMediaCount(http, dest, info,
                                             CUPS_MEDIA_FLAGS_DEFAULT);
cups_media_t media;
for (i = 0; i < count; i ++)
{
  if (cupsGetDestMediaByIndex2(http, dest, info, i,
                               CUPS_MEDIA_FLAGS_DEFAULT, &media))
    printf("%s: %s\n", media.name,
           cupsLocalizeDestMedia2(http, dest, info,
                                  CUPS_MEDIA_FLAGS_DEFAULT, &media));
}
```


## Submitting a Print Job

Once you are ready to submit a print job, you create a job using the
[`cupsCreateDestJob`](@@) function:

```c
ipp_status_t
cupsCreateDestJob(http_t *http, cups_dest_t *dest,
                  cups_dinfo_t *info, int *job_id,
                  const char *title, int num_options,
                  cups_option_t *options);
```

The `title` argument specifies a name for the print job such as "My Document".
The `num_options` and `options` arguments specify the options for the print
job which are allocated using the `cupsAddOption` function.

When successful, the job's numeric identifier is stored in the integer pointed
to by the `job_id` argument and `IPP_STATUS_OK` is returned.  Otherwise, an IPP
error status is returned.

For example, the following code creates a new job that will print 42 copies of a
two-sided US Letter document:

```c
int job_id = 0;
int num_options = 0;
cups_option_t *options = NULL;

num_options = cupsAddOption(CUPS_COPIES, "42",
                            num_options, &options);
num_options = cupsAddOption(CUPS_MEDIA, CUPS_MEDIA_LETTER,
                            num_options, &options);
num_options = cupsAddOption(CUPS_SIDES,
                            CUPS_SIDES_TWO_SIDED_PORTRAIT,
                            num_options, &options);

if (cupsCreateDestJob(CUPS_HTTP_DEFAULT, dest, info,
                      &job_id, "My Document", num_options,
                      options) == IPP_STATUS_OK)
  printf("Created job: %d\n", job_id);
else
  printf("Unable to create job: %s\n",
         cupsGetErrorString());
```

Once the job is created, you submit documents for the job using the
[`cupsStartDestDocument`](@@), [`cupsWriteRequestData`](@@), and
[`cupsFinishDestDocument`](@@) functions:

```c
http_status_t
cupsStartDestDocument(http_t *http, cups_dest_t *dest,
                      cups_dinfo_t *info, int job_id,
                      const char *docname,
                      const char *format,
                      int num_options,
                      cups_option_t *options,
                      int last_document);

http_status_t
cupsWriteRequestData(http_t *http, const char *buffer,
                     size_t length);

ipp_status_t
cupsFinishDestDocument(http_t *http, cups_dest_t *dest,
                       cups_dinfo_t *info);
```

The `docname` argument specifies the name of the document, typically the
original filename.  The `format` argument specifies the MIME media type of the
document, including the following constants:

- `CUPS_FORMAT_AUTO`: "application/octet-stream"
- `CUPS_FORMAT_JPEG`: "image/jpeg"
- `CUPS_FORMAT_PDF`: "application/pdf"
- `CUPS_FORMAT_TEXT`: "text/plain"

The `num_options` and `options` arguments specify per-document print options,
which at present must be 0 and `NULL`.  The `last_document` argument specifies
whether this is the last document in the job.

For example, the following code submits a PDF file to the job that was just
created:

```c
FILE *fp = fopen("filename.pdf", "rb");
size_t bytes;
char buffer[65536];

if (cupsStartDestDocument(CUPS_HTTP_DEFAULT, dest, info,
                          job_id, "filename.pdf", 0, NULL,
                          1) == HTTP_STATUS_CONTINUE)
{
  while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0)
  {
    if (cupsWriteRequestData(CUPS_HTTP_DEFAULT, buffer,
                             bytes) != HTTP_STATUS_CONTINUE)
      break;
  }

  if (cupsFinishDestDocument(CUPS_HTTP_DEFAULT, dest,
                             info) == IPP_STATUS_OK)
    puts("Document send succeeded.");
  else
    printf("Document send failed: %s\n",
           cupsGetErrorString());
}

fclose(fp);
```


# Sending IPP Requests

CUPS provides a rich API for sending IPP requests to the scheduler or printers,
typically from management or utility applications whose primary purpose is not
to send print jobs.


## Connecting to the Scheduler or Printer

The connection to the scheduler or printer is represented by the HTTP connection
type `http_t`.  The [`cupsConnectDest`](@@) function connects to the scheduler
or printer associated with the destination:

```c
http_t *
cupsConnectDest(cups_dest_t *dest, unsigned flags, int msec,
                int *cancel, char *resource,
                size_t resourcesize, cups_dest_cb_t cb,
                void *user_data);
```

The `dest` argument specifies the destination to connect to.

The `flags` argument specifies whether you want to connect to the scheduler
(`CUPS_DEST_FLAGS_NONE`) or device/printer (`CUPS_DEST_FLAGS_DEVICE`) associated
with the destination.

The `msec` argument specifies how long you are willing to wait for the
connection to be established in milliseconds.  Specify a value of `-1` to wait
indefinitely.

The `cancel` argument specifies the address of an integer variable that can be
set to a non-zero value to cancel the connection.  Specify a value of `NULL`
to not provide a cancel variable.

The `resource` and `resourcesize` arguments specify the address and size of a
character string array to hold the path to use when sending an IPP request.

The `cb` and `user_data` arguments specify a destination callback function that
returns 1 to continue connecting or 0 to stop.  The destination callback works
the same way as the one used for the [`cupsEnumDests`](@@) function.

On success, a HTTP connection is returned that can be used to send IPP requests
and get IPP responses.

For example, the following code connects to the printer associated with a
destination with a 30 second timeout:

```c
char resource[256];
http_t *http = cupsConnectDest(dest, CUPS_DEST_FLAGS_DEVICE,
                               30000, /*cancel*/NULL, resource,
                               sizeof(resource),
                               /*cb*/NULL, /*user_data*/NULL);
```


## Creating an IPP Request

IPP requests are represented by the IPP message type `ipp_t` and each IPP
attribute in the request is representing using the type `ipp_attribute_t`.  Each
IPP request includes an operation code (`IPP_OP_CREATE_JOB`,
`IPP_OP_GET_PRINTER_ATTRIBUTES`, etc.) and a 32-bit integer identifier.

The `ippNewRequest` function creates a new IPP request:

```c
ipp_t *
ippNewRequest(ipp_op_t op);
```

The `op` argument specifies the IPP operation code for the request.  For
example, the following code creates an IPP Get-Printer-Attributes request:

```c
ipp_t *request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
```

The request identifier is automatically set to a unique value for the current
process.

Each IPP request starts with two IPP attributes, "attributes-charset" and
"attributes-natural-language", followed by IPP attribute(s) that specify the
target of the operation.  The `ippNewRequest` automatically adds the correct
"attributes-charset" and "attributes-natural-language" attributes, but you must
add the target attribute(s).  For example, the following code adds the
"printer-uri" attribute to the IPP Get-Printer-Attributes request to specify
which printer is being queried:

```c
const char *printer_uri = cupsGetOption("device-uri",
                                        dest->num_options,
                                        dest->options);

ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
             "printer-uri", /*language*/NULL, printer_uri);
```c

> **Note:**
>
> If we wanted to query the scheduler instead of the device, we would look
> up the "printer-uri-supported" option instead of the "device-uri" value.

The [`ippAddString`](@@) function adds the "printer-uri" attribute to the IPP
request.  The `IPP_TAG_OPERATION` argument specifies that the attribute is part
of the operation.  The `IPP_TAG_URI` argument specifies that the value is a
Universal Resource Identifier (URI) string.  The `NULL` argument specifies there
is no language (English, French, Japanese, etc.) associated with the string, and
the `printer_uri` argument specifies the string value.

The IPP Get-Printer-Attributes request also supports an IPP attribute called
"requested-attributes" that lists the attributes and values you are interested
in.  For example, the following code requests the printer state attributes:

```c
static const char * const requested_attributes[] =
{
  "printer-state",
  "printer-state-message",
  "printer-state-reasons"
};

ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
              "requested-attributes", 3, /*language*/NULL,
              requested_attributes);
```

The [`ippAddStrings`](@@) function adds an attribute with one or more strings,
in this case three.  The `IPP_TAG_KEYWORD` argument specifies that the strings
are keyword values, which are used for attribute names.  All strings use the
same language (`NULL` for none), and the attribute will contain the three
strings in the array `requested_attributes`.

CUPS provides many functions to adding attributes of different types:

- [`ippAddBoolean`](@@) adds a boolean (`IPP_TAG_BOOLEAN`) attribute with one
  value.
- [`ippAddInteger`](@@) adds an enum (`IPP_TAG_ENUM`) or integer
  (`IPP_TAG_INTEGER`) attribute with one value.
- [`ippAddIntegers`](@@) adds an enum or integer attribute with one or more
  values.
- [`ippAddOctetString`](@@) adds an octetString attribute with one value.
- [`ippAddOutOfBand`](@@) adds a admin-defined (`IPP_TAG_ADMINDEFINE`), default
  (`IPP_TAG_DEFAULT`), delete-attribute (`IPP_TAG_DELETEATTR`), no-value
  (`IPP_TAG_NOVALUE`), not-settable (`IPP_TAG_NOTSETTABLE`), unknown
  (`IPP_TAG_UNKNOWN`), or unsupported (`IPP_TAG_UNSUPPORTED_VALUE`) out-of-band
  attribute.
- [`ippAddRange`](@@) adds a rangeOfInteger attribute with one range.
- [`ippAddRanges`](@@) adds a rangeOfInteger attribute with one or more ranges.
- [`ippAddResolution`](@@) adds a resolution attribute with one resolution.
- [`ippAddResolutions`](@@) adds a resolution attribute with one or more
  resolutions.
- [`ippAddString`](@@) adds a charset (`IPP_TAG_CHARSET`), keyword
  (`IPP_TAG_KEYWORD`), mimeMediaType (`IPP_TAG_MIMETYPE`), name (`IPP_TAG_NAME`
  and `IPP_TAG_NAMELANG`), naturalLanguage (`IPP_TAG_NATURAL_LANGUAGE`), text
  (`IPP_TAG_TEXT` and `IPP_TAG_TEXTLANG`), uri (`IPP_TAG_URI`), or uriScheme
  (`IPP_TAG_URISCHEME`) attribute with one value.
- [`ippAddStrings`](@@) adds a charset, keyword, mimeMediaType, name,
  naturalLanguage, text, uri, or uriScheme attribute with one or more values.


## Sending the IPP Request

Once you have created the IPP request, you can send it using the
[`cupsDoRequest`](@@) function.  For example, the following code sends the IPP
Get-Printer-Attributes request to the destination and saves the response:

```c
ipp_t *response = cupsDoRequest(http, request, resource);
```

For requests like Send-Document that include a file, the
[`cupsDoFileRequest`](@@) function should be used:

```c
ipp_t *response = cupsDoFileRequest(http, request, resource,
                                    filename);
```

Both `cupsDoRequest` and `cupsDoFileRequest` free the IPP request.  If a valid
IPP response is received, it is stored in a new IPP message (`ipp_t`) and
returned to the caller.  Otherwise `NULL` is returned.

The status from the most recent request can be queried using the
[`cupsGetError`](@@) function, for example:

```c
if (cupsGetError() >= IPP_STATUS_ERROR_BAD_REQUEST)
{
  /* request failed */
}
```

A human-readable error message is also available using the `cupsGetErrorString`
function:

```c
if (cupsGetError() >= IPP_STATUS_ERROR_BAD_REQUEST)
{
  /* request failed */
  printf("Request failed: %s\n", cupsGetErrorString());
}
```


## Processing the IPP Response

Each response to an IPP request is also an IPP message (`ipp_t`) with its own
IPP attributes (`ipp_attribute_t`) that includes a status code (`IPP_STATUS_OK`,
`IPP_STATUS_ERROR_BAD_REQUEST`, etc.) and the corresponding 32-bit integer
identifier from the request.

For example, the following code finds the printer state attributes and prints
their values:

```c
ipp_attribute_t *attr;

if ((attr = ippFindAttribute(response, "printer-state",
                             IPP_TAG_ENUM)) != NULL)
{
  printf("printer-state=%s\n",
         ippEnumString("printer-state", ippGetInteger(attr, 0)));
}
else
  puts("printer-state=unknown");

if ((attr = ippFindAttribute(response, "printer-state-message",
                             IPP_TAG_TEXT)) != NULL)
{
  printf("printer-state-message=\"%s\"\n",
         ippGetString(attr, 0, NULL)));
}

if ((attr = ippFindAttribute(response, "printer-state-reasons",
                             IPP_TAG_KEYWORD)) != NULL)
{
  int i, count = ippGetCount(attr);

  puts("printer-state-reasons=");
  for (i = 0; i < count; i ++)
    printf("    %s\n", ippGetString(attr, i, NULL)));
}
```

The [`ippGetCount`](@@) function returns the number of values in an attribute.

The [`ippGetInteger`](@@) and [`ippGetString`](@@) functions return a single
integer or string value from an attribute.

The [`ippEnumString`](@@) function converts a enum value to its keyword (string)
equivalent.

Once you are done using the IPP response message, free it using the
[`ippDelete`](@@) function:

```c
ippDelete(response);
```


# Authentication and Authorization

CUPS supports authentication and authorization using HTTP Basic, HTTP Digest,
peer credentials when communicating over domain sockets, and OAuth/OpenID
Connect.

Peer credential authorization happens automatically when connected over a
domain socket.  Other types of authentication requires the application to
handle `HTTP_STATUS_UNAUTHORIZED` responses beyond simply calling
[`cupsDoAuthentication`](@@).


## Authentication Using Passwords

When you call [`cupsDoAuthentication`](@@) and the HTTP server requires the
"Basic" or "Digest" authentication schemes, CUPS normally requests a password
from the console.  GUI applications should set a password callback using the
[`cupsSetPasswordCB2`](@@) function:

```c
void
cupsSetPasswordCB2(cups_password_cb2_t cb, void *cb_data);
```

The password callback is called when needed and is responsible for setting
the current user name using [`cupsSetUser`](@@) and returning a (password)
string:

```c
const char *
cups_password_cb(const char *prompt, http_t *http,
                 const char *method, const char *resource,
                 void *cb_data);
```

The "prompt" argument is a string from CUPS that should be displayed to the
user.

The "http" argument is the connection hosting the request that is being
authenticated.  The password callback can call the [`httpGetField`](@@) and
[`httpGetSubField`](@@) functions to look for additional details concerning the
authentication challenge.

The "method" argument specifies the HTTP method used for the request and is
typically "GET", "POST", or "PUT".

The "resource" argument specifies the path or URI used for the request.

The "cb_data" argument provides the data pointer from the
[`cupsSetPasswordCB2`](@@) call.


## Authorization using OAuth/OpenID Connect

When you call [`cupsDoAuthentication`](@@) and the HTTP server requires the
"Bearer" authentication scheme, CUPS will call an OAuth callback that you
register using the [`cupsSetOAuthCB`](@@) function:

```c
void
cupsSetOAuthCB(cups_oauth_cb_t cb, void *cb_data);
```

The OAuth callback is called when needed and is responsible for performing any
necessary authorization and returning an access token string:

```c
const char *
cups_oauth_cb(http_t *http, const char *realm, const char *scope,
              const char *resource, void *cb_data);
```

The "http" argument is the connection hosting the request that is being
authenticated.  The OAuth callback can call the [`httpGetField`](@@) and
[`httpGetSubField`](@@) functions to look for additional details concerning the
authentication challenge.

The "realm" and "scope" arguments provide the "realm" and "scope" parameters, if
any, from the "WWW-Authenticate" header.

The "resource" argument specifies the path or URI used for the request.

The "cb_data" argument provides the data pointer from the
[`cupsSetOAuthCB`](@@) call.


### OAuth Client Functions

CUPS provides a generic OAuth/OpenID client for authorizing access to printers
and other network resources.  The following functions are provided:

- [`cupsOAuthClearTokens`](@@): Clear all cached tokens.
- [`cupsOAuthCopyAccessToken`](@@): Copy the cached access token.
- [`cupsOAuthCopyClientId`](@@): Copy the cached client ID.
- [`cupsOAuthCopyRefreshToken`](@@): Copy the cached refresh token.
- [`cupsOAuthCopyUserId`](@@): Copy the cached user ID.
- [`cupsOAuthGetAuthorizationCode`](@@): Get an authorization code using a web
  browser.
- [`cupsOAuthGetClientId`](@@): Get a client ID using dynamic client
  registration.
- [`cupsOAuthGetDeviceGrant`](@@): Get a device authorization grant code.
- [`cupsOAuthGetJWKS`](@@): Get the key set for an authorization server.
- [`cupsOAuthGetMetadata`](@@): Get the metadata for an authorization server.
- [`cupsOAuthGetTokens`](@@): Get access and refresh tokens for an
  authorization/grant code.
- [`cupsOAuthGetUserId`](@@): Get the user ID associated with an access token.
- [`cupsOAuthMakeAuthorizationURL`](@@): Make the URL for web browser
  authorization.
- [`cupsOAuthMakeBase64Random`](@@): Make a Base64-encoded string of random
  bytes.
- [`cupsOAuthSaveClientData`](@@): Save a client ID and secret for an
  authorization server.
- [`cupsOAuthSaveTokens`](@@): Save access and refresh tokens for an
  authorization server.

Once you have an access token you use the [`httpSetAuthString`](@@) function to
use it for a HTTP connection:

```c
http_t *http;
char   *access_token;

httpSetAuthString(http, "Bearer", access_token);
```


### Authorizing Using a Web Browser

Users can authorize using their preferred web browser via the
[`cupsOAuthGetAuthorizationCode`](@@) function, which returns an authorization
grant code string.  The following code gets the authorization server metadata,
authorizes access through the web browser, and then obtains a HTTP Bearer access
token:

```c
http_t      *http;           // HTTP connection
const char  *auth_uri;       // Base URL for Authorization Server
cups_json_t *metadata;       // Authorization Server metadata
const char  *printer_uri;    // Printer URI
char        *auth_code;      // Authorization grant code
char        *access_token;   // Access token
time_t      access_expires;  // Date/time when access token expires

// Get the metadata for the authorization server.
metadata = cupsOAuthGetMetadata(auth_uri);

if (metadata == NULL)
{
  // Handle error getting metadata from authorization server.
}

// Bring up the web browser to authorize and get an authorization code.
auth_code = cupsOAuthGetAuthorizationCode(auth_uri, metadata, printer_uri,
                                          /*scopes*/NULL,
                                          /*redirect_uri*/NULL);

if (auth_code == NULL)
{
  // Unable to authorize.
}

// Get the access code from the authorization code.
access_token = cupsOAuthGetTokens(auth_uri, metadata, printer_uri, auth_code,
                                  CUPS_OGRANT_AUTHORIZATION_CODE,
                                  /*redirect_uri*/NULL, &access_expires);

if (access_token == NULL)
{
  // Unable to get access token.
}

// Set the Bearer token for authorization.
httpSetAuthString(http, "Bearer", access_token);
free(access_token);
```


### Authorizing Using a Mobile Device

Users can authorize using a mobile device via the
[`cupsOAuthGetDeviceGrant`](@@) function, which returns a JSON object with the
mobile authorization URLs, user (verification) code string, and device grant
code.  The following code gets the authorization server metadata, gets the
mobile device authorization information, and then obtains a HTTP Bearer access
token:

```c
http_t      *http;           // HTTP connection
const char  *auth_uri;       // Base URL for Authorization Server
cups_json_t *metadata;       // Authorization Server metadata
const char  *printer_uri;    // Printer URI
cups_json_t *device_grant;   // Device authorization grant object
const char  *device_code;    // Device grant code
const char  *verify_url;     // Mobile device URL
const char  *verify_urlc;    // Mobile device URL with user code
const char  *user_code;      // User code
char        *access_token;   // Access token
time_t      access_expires;  // Date/time when access token expires

// Get the metadata for the authorization server.
metadata = cupsOAuthGetMetadata(auth_uri);

if (metadata == NULL)
{
  // Handle error getting metadata from authorization server.
}

// Get a device authorization grant for mobile authorization.
device_grant = cupsOAuthGetDeviceGrant(auth_uri, metadata, printer_uri,
                                       /*scopes*/NULL);

device_code = cupsJSONGetString(
                  cupsJSONFind(device_grant, CUPS_ODEVGRANT_DEVICE_CODE));
verify_url  = cupsJSONGetString(
                  cupsJSONFind(device_grant, CUPS_ODEVGRANT_VERIFICATION_URI));
verify_urlc = cupsJSONGetString(
                  cupsJSONFind(device_grant, CUPS_ODEVGRANT_VERIFICATION_URI_COMPLETE));
user_code   = cupsJSONGetString(
                  cupsJSONFind(device_grant, CUPS_ODEVGRANT_USER_CODE));

if (device_code == NULL || verify_url == NULL || verify_urlc == NULL ||
    user_code == NULL)
{
  // Unable to authorize.
}

// Show the URLs and user code to the user (links and/or QR codes).
printf("Open this URL: %s\n", verify_urlc);

// Get the access code from the authorization code.
do
{
  // Delay check for several seconds.
  sleep(5);

  // Try getting an access token.
  access_token = cupsOAuthGetTokens(auth_uri, metadata, printer_uri, device_code,
                                    CUPS_OGRANT_DEVICE_CODE,
                                    /*redirect_uri*/NULL, &access_expires);
}
while (access_token == NULL && access_expires > 0);
       // Continue checking until we have an access token or
       // the device code has expired.

if (access_token == NULL)
{
  // Unable to get access token.
}

// Set the Bearer token for authorization.
httpSetAuthString(http, "Bearer", access_token);
free(access_token);
```


### Supported OAuth Standards

The following standards are supported:

- [OpenID Connect Core v1.0](https://openid.net/specs/openid-connect-core-1_0.html)
- [RFC 6749: The OAuth 2.0 Authorization Framework](https://datatracker.ietf.org/doc/html/rfc6749)
- [RFC 6750: The OAuth 2.0 Authorization Framework: Bearer Token Usage](https://datatracker.ietf.org/doc/html/rfc6750)
- [RFC 7591: OAuth 2.0 Dynamic Client Registration Protocol](https://datatracker.ietf.org/doc/html/rfc7591)
- [RFC 7636: Proof Key for Code Exchange by OAuth Public Clients](https://datatracker.ietf.org/doc/html/rfc7636)
- [RFC 8252: OAuth 2.0 for Native Apps](https://datatracker.ietf.org/doc/html/rfc8252)
- [RFC 8414: OAuth 2.0 Authorization Server Metadata](https://datatracker.ietf.org/doc/html/rfc8414)
- [RFC 8628: OAuth 2.0 Device Authorization Grant](https://datatracker.ietf.org/doc/html/rfc8628)
- [RFC 8693: OAuth 2.0 Token Exchange](https://datatracker.ietf.org/doc/html/rfc8693)
- [RFC 9068: JSON Web Token (JWT) Profile for OAuth 2.0 Access Tokens](https://datatracker.ietf.org/doc/html/rfc9068)


# IPP Data File API

The IPP data file API provides functions to read and write IPP attributes and
other commands or data using a common base format that supports tools such as
`ipptool` and `ippeveprinter`.


## Creating an IPP Data File

The [`ippFileNew`](@@) function creates a new IPP data file (`ipp_file_t`)
object:

```c
ipp_file_t *parent = NULL;
void *data;
ipp_file_t *file = ippFileNew(parent, attr_cb, error_cb, data);
```

The "parent" IPP data file pointer is typically used to support nested files and
is normally `NULL` for a new file.  The "data" argument supplies your
application data to the callbacks.  The "attr_cb" callback function is used to
filter IPP attributes; return `true` to include the attribute and `false` to
ignore it:

```c
bool
attr_cb(ipp_file_t *file, void *cb_data, const char *name)
{
  ... determine whether to use an attribute named "name" ...
}
```

The "error_cb" callback function is used to record/report errors when reading
the file:

```c
bool
error_cb(ipp_file_t *file, void *cb_data, const char *error)
{
  ... display/record error and return `true` to continue or `false` to stop ...
}
```


## Reading a Data File

The [`ippFileOpen`](@@) function opens the specified data file and
[`ippFileRead`](@@) reads from it:

```c
if (ippFileOpen(file, "somefile", "r"))
{
  // Opened successfully, now read it...
  ippFileRead(file, token_cb, /*with_groups*/false);
  ippFileClose(file);
}
```

The token callback function passed to `ippFileRead` handles custom directives in
your data file:

```c
bool
token_cb(ipp_file_t *file, void *cb_data, const char *token)
{
  ... handle token, return `true` to continue or `false` to stop ...
}
```

The "token" parameter contains the token to be processed.  The callback can use
the [`ippFileReadToken`](@@) function to read additional tokens from the file
and the [`ippFileExpandToken`](@@) function to expand any variables in the token
string.  Return `false` to stop reading the file and `true` to continue.  The
default `NULL` callback reports an unknown token error through the error
callback end returns `false`.

Once read, you call the [`ippFileGetAttributes`](@@) function to get the IPP
attributes from the file.


## Variables

Each IPP data file object has associated variables that can be used when reading
the file.  The default set of variables is:

- "date-current": Current date in ISO-8601 format
- "date-start": Start date (when file opened) in ISO-8601 format
- "filename": Associated data/document filename, if any
- "filetype": MIME media type of associated data/document filename, if any
- "hostname": Hostname or IP address from the "uri" value, if any
- "port": Port number from the "uri" value, if any
- "resource": Resource path from the "uri" value, if any
- "scheme": URI scheme from the "uri" value, if any
- "uri": URI, if any
- "uriuser": Username from the "uri" value, if any
- "uripassword": Password from the "uri" value, if any
- "user": Current login user

The [`ippFileGetVar`](@@), [`ippFileSetVar`](@@), and [`ippFileSetVarf`](@@)
functions get and set file variables, respectively.


## Writing IPP Data Files

As when reading an IPP data file, the [`ippFileNew`](@@) function creates a new
file object, [`ippFileOpen`](@@) opens the file, and [`ippFileClose`](@@) closes
the file.  However, you call [`ippFileWriteAttributes`](@@) to write the
attributes in an IPP message (`ipp_t`), [`ippFileWriteComment`](@@) to write a
comment in the file, and [`ippWriteToken`](@@) or [`ippWriteTokenf`](@@) to
write a token or value to the file.
