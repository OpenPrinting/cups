Security Policy
===============

This file describes how security issues are reported and handled by
OpenPrinting, and what the expectations are for security issues reported to
this project.


Supported Versions
------------------

This security policy only applies to production releases of this software.  A
production release is tagged and given a semantic version number of the form
"MAJOR.MINOR.PATCH" where "MAJOR" is an integer starting at 1 and "MINOR" and
"PATCH" are integers starting at 0.

> *Note:* Please report security vulnerabilities that only affect unreleased
> code as regular bugs.


Is the Issue a Bug or a Security Vulnerability?
-----------------------------------------------

OpenPrinting has defined criteria for identifying whether issues are handled as
regular project bugs or as security vulnerabilities that use the "Reporting a
Vulnerability" process below.

The following kinds of issues are generally treated as security vulnerabilities
by OpenPrinting:

- Daemon/service crashes/hangs caused by a network request,
- Remote code execution code via software provided by OpenPrinting,
- Privilege escalation that allows unauthorized actions or information
  disclosure,
- Common weaknesses (buffer overflow, divide-by-zero, input validation,
  use-after-free, etc.) that lead to a demonstrated (not theoretical) exploit.

The following kinds of issues are generally treated as regular bugs by
OpenPrinting:

- Vulnerabilities caused by mis-configuration
- Issues caused by incorrect API usage
- Issues that only exist in non-production software

Regular bugs should be reported to the project using the GitHub (public) issue
tracker page at <https://github.com/OpenPrinting/cups/issues>.


Reporting a Security Vulnerability
----------------------------------

Vulnerabilities should be reported to the project using the GitHub (private)
security advisory page at
<https://github.com/OpenPrinting/cups/security/advisories>.

Provide details, impact, reproducer, affected versions, workarounds, and a patch
for the vulnerability, if applicable.

You can expect a response within 5 business days.


How OpenPrinting Responds to Vulnerability Reports
--------------------------------------------------

First, OpenPrinting takes every report seriously.  There are (conservatively)
several billion devices/systems using CUPS, so any security issue can affect a
lot of people!

Members of the OpenPrinting security team will try to verify/reproduce the
reported issues in a timely fashion.  Please keep in mind that many members of
the security team are volunteers or are only employed part-time to maintain
CUPS, so your patience is appreciated.

Sometimes a reported issue is actually in another project's code.  For these
issues, we may ask you to re-submit your report to the correct project - an
enhancement request has been submitted to GitHub to correct this limitation for
projects hosted on GitHub.

Other times we may verify the issue exists but disagree on the severity or
scope of the issue.  We assess vulnerabilities based on our supported platforms
and common configurations because we need to be able to test and verify issues
and fixes on those supported platforms.

> *Note:* GitHub uses CVSS base metrics which require the default configuration
> of the affected project.  This is most important for the attack vector metric
> in CVSS because the default cupsd configuration only listens on the loopback
> and domain socket addresses.

The final CVSS score determines how the vulnerability is disclosed - see below
for details.

Similar issues (if multiple vulnerabilities are reported) will be combined if
they share a common root cause.  We don't mean any disrespect by doing this, we
just want to make sure your issues are truly and efficiently addressed in full.

Once we have verified things, we will work towards providing a fix as quickly
as possible.  Fixes are typically developed against the "master" branch, then
backported as needed to cover shipping CUPS releases on our supported platforms.


Responsible Disclosure
----------------------

With *responsible disclosure*, the issue and its fixes are shared amongst and
reviewed by the key stakeholders (Linux distributions, OS vendors, etc. on the
`distros@vs.openwall.org` mailing list) and the CERT/CC.  OpenPrinting requests
a CVE when we have agreed-upon fixes ready.

If the final CVSS score is 7 or more, or if a key stakeholder requests it,
OpenPrinting coordinates a mutually-agreed period of time (the "embargo date")
for when the fixes will be released.  Otherwise, the fixes are pushed to the
public repository immediately and included in a subsequent production release
when convenient.

> *Note:* An embargo starts a flurry of activity.  Hundreds of developers
> supporting every Linux distribution, the various BSD flavors, macOS, and
> ChromeOS queue up security updates for their respective OS releases on the
> embargo date.  OpenPrinting wants to limit the embargo process to high
> severity issues to better manage limited developer resources.
