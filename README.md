OpenPrinting CUPS v2.5b1
========================

![Version](https://img.shields.io/github/v/release/openprinting/cups?include_prereleases)
![Apache 2.0](https://img.shields.io/github/license/openprinting/cups)
[![Build and Test](https://github.com/OpenPrinting/cups/workflows/Build%20and%20Test/badge.svg)](https://github.com/OpenPrinting/cups/actions/workflows/build.yml)
[![Coverity Scan](https://img.shields.io/coverity/scan/23806)](https://scan.coverity.com/projects/openprinting-cups)

> *Note:* This branch is tracking a future CUPS 2.5.x feature release.  Check
> out the "2.4.x" branch for CUPS 2.4.x.


Introduction
------------

OpenPrinting CUPS is the most current version of CUPS, a standards-based, open
source printing system for Linux® and other Unix®-like operating systems.  CUPS
supports printing to:

- [AirPrint™][1] and [IPP Everywhere™][2] printers,
- Network and local (USB) printers with Printer Applications, and
- Network and local (USB) printers with (legacy) PPD-based printer drivers.

CUPS provides the System V ("lp") and Berkeley ("lpr") command-line interfaces,
a configurable web interface, a C API, and common print filters, drivers, and
backends for printing.  The [cups-filters][3] project provides additional
filters and drivers.

CUPS is licensed under the Apache License Version 2.0 with an exception to allow
linking against GNU GPL2-only software.  See the files `LICENSE` and `NOTICE`
for more information.

> Note: Apple maintains a separate repository for the CUPS that ships with macOS
> and iOS at <https://github.com/apple/cups>.

[1]: https://support.apple.com/en-us/HT201311
[2]: https://www.pwg.org/printers
[3]: https://github.com/openprinting/cups-filters


Reading the Documentation
-------------------------

Initial documentation to get you started is provided in the root directory of
the CUPS sources:

- `CHANGES.md`: A list of changes in the current major release of CUPS.
- `CONTRIBUTING.md`: Guidelines for contributing to the CUPS project.
- `CREDITS.md`: A list of past contributors to the CUPS project.
- `DEVELOPING.md`: Guidelines for developing code for the CUPS project.
- `INSTALL.md`: Instructions for building and installing CUPS.
- `LICENSE`: The CUPS license agreement (Apache 2.0).
- `NOTICE`: Copyright notices and exceptions to the CUPS license agreement.
- `README.md`: This file.
- `REPORTING_ISSUES.md`: Instructions what information to provide when reporting an issue.

Once you have installed the software you can access the documentation (and a
bunch of other stuff) online at <http://localhost:631/> and using the `man`
command, for example `man cups`.

If you're having trouble getting that far, the documentation is located under
the `doc/help` and `man` directories.

*Please read the documentation before asking questions.*


Setting Up Printers
-------------------

CUPS includes a web-based administration tool that allows you to manage
printers, classes, and jobs on your server.  Open <http://localhost:631/admin/>
in your browser to access the printer administration tools.  You will be asked
for the administration password (root or any other user in the "sys", "system",
"root", "admin", or "lpadmin" group on your system) when performing any
administrative function.

The `lpadmin` command is used to manage printers from the command-line.  For
example, the following command creates a print queue called "myprinter" for an
IPP Everywhere printer at address "11.22.33.44":

    lpadmin -p myprinter -E -v "ipp://11.22.33.44/ipp/print" -m everywhere

The `-p` option specifies the printer name.  The `-E` option enables the printer
and accepts new print jobs immediately.  The `-v` option specifies the *device
URI* for the printer, which tells CUPS how to communicate with the printer.  And
the `-m` option specifies the model (driver) to use, in this case the IPP
Everywhere ("everywhere") driver that is used for AirPrint and IPP Everywhere
printers as well as shared printers and printers supported through Printer
Applications.

Legacy printers are supported using PPD (PostScript Printer Description) files
that describe printer capabilities and driver programs needed for each printer.
CUPS includes several sample PPD files for common legacy printers:

   Driver                       | PPD Name
   -----------------------------|------------------------------
   Dymo Label Printers          | drv:///sample.drv/dymo.ppd
   Intellitech Intellibar       | drv:///sample.drv/intelbar.ppd
   EPSON 9-pin Series           | drv:///sample.drv/epson9.ppd
   EPSON 24-pin Series          | drv:///sample.drv/epson24.ppd
   Generic PCL Laser Printer    | drv:///sample.drv/generpcl.ppd
   Generic PostScript Printer   | drv:///sample.drv/generic.ppd
   HP DeskJet Series            | drv:///sample.drv/deskjet.ppd
   HP LaserJet Series           | drv:///sample.drv/laserjet.ppd
   OKIDATA 9-Pin Series         | drv:///sample.drv/okidata9.ppd
   OKIDATA 24-Pin Series        | drv:///sample.drv/okidat24.ppd
   Zebra CPCL Label Printer     | drv:///sample.drv/zebracpl.ppd
   Zebra EPL1 Label Printer     | drv:///sample.drv/zebraep1.ppd
   Zebra EPL2 Label Printer     | drv:///sample.drv/zebraep2.ppd
   Zebra ZPL Label Printer      | drv:///sample.drv/zebra.ppd

The sample drivers provide basic printing capabilities, but generally do not
exercise the full potential of the printers or CUPS.  Other drivers provide
greater printing capabilities.

You can run the `lpinfo -m` command to list all of the available drivers:

    lpinfo -m

Similarly, the `lpinfo -v` command lists the available printers and their device
URIs:

    lpinfo -v

Once you know the device URI and driver name, add the printer using the
`lpadmin` command:

    lpadmin -p PRINTER-NAME -E -v "DEVICE-URI" -m DRIVER-NAME


Printing Files
--------------

CUPS provides both the System V `lp` and Berkeley `lpr` commands for printing:

    lp FILENAME
    lpr FILENAME

Both the `lp` and `lpr` commands support printing options:

    lp -o media=A4 -o resolution=600dpi FILENAME
    lpr -o media=A4 -o resolution=600dpi FILENAME

CUPS recognizes many types of images files as well as PDF, PostScript, and text
files, so you can print those files directly rather than through an application.

If you have an application that generates output specifically for your printer
then you need to use the `-oraw` or `-l` options:

    lp -o raw FILENAME
    lpr -l FILENAME

This will prevent the filters from misinterpreting your print file.


Contributing Code and Translations
----------------------------------

Code contributions should be submitted as pull requests on the Github site:

    http://github.com/OpenPrinting/cups/pulls

See the file "CONTRIBUTING.md" for more details.

CUPS uses [Weblate][WL] to manage the localization of the web interface,
command-line programs, and common IPP attributes and values, and those likewise
end up as pull requests on Github.

[WL]: https://hosted.weblate.org


Legal Stuff
-----------

Copyright © 2020-2023 by OpenPrinting

Copyright © 2007-2020 by Apple Inc.

Copyright © 1997-2007 by Easy Software Products.

CUPS is provided under the terms of the Apache License, Version 2.0 with
exceptions for GPL2/LGPL2 software.  A copy of this license can be found in the
file `LICENSE`.  Additional legal information is provided in the file `NOTICE`.

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.  See the License for the
specific language governing permissions and limitations under the License.
