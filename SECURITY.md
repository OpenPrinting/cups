Security Policy
===============

This file describes how security issues are reported and handled, and what the
expectations are for security issues reported to this project.


Responsible Disclosure
----------------------

With *responsible disclosure*, a security issue (and its fix) is disclosed only
after a mutually-agreed period of time (the "embargo date").  The issue and fix
are shared amongst and reviewed by the key stakeholders (Linux distributions,
OS vendors, etc.) and the CERT/CC.  Fixes are released to the public on the
agreed-upon date.

> Responsible disclosure applies only to production releases.  A security
> vulnerability that only affects unreleased code can be fixed immediately
> without coordination.  Vendors *should not* package and release unstable
> snapshots, beta releases, or release candidates of this software.


Supported Versions
------------------

All production releases of this software are subject to this security policy.  A
production release is tagged and given a semantic version number of the form:

    MAJOR.MINOR.PATCH

where "MAJOR" is an integer starting at 1 and "MINOR" and "PATCH" are integers
starting at 0.  A feature release has a "PATCH" value of 0, for example:

    1.0.0
    1.1.0
    2.0.0

Beta releases and release candidates are *not* prodution releases and use
semantic version numbers of the form:

    MAJOR.MINORbNUMBER
    MAJOR.MINORrcNUMBER

where "MAJOR" and "MINOR" identify the new feature release version number and
"NUMBER" identifies a beta or release candidate number starting at 1, for
example:

    1.0b1
    1.0b2
    1.0rc1


Reporting a Vulnerability
-------------------------

Github supports private security advisories and OpenPrinting CUPS enabled
their usage, report all security issue via them. Reporters can file a security
advisory by clicking on `New issue` at tab `Issues` and choose
`Report a vulnerability`.  Provide details, impact, reproducer, affected
versions, workarounds and patch for the vulnerability if there are any and
estimate severity when creating the advisory.

Expect a response within 5 business days.


How We Respond to Vulnerability Reports
---------------------------------------

First, we take every report seriously.  There are (conservatively) over a
billion systems using CUPS, so any security issue can affect a lot of people.

Members of the OpenPrinting security team will try to verify/reproduce the
reported issues in a timely fashion.  Please keep in mind that many members of
the security team are volunteers or are only employed part-time to maintain
CUPS, so your patience is appreciated!

Sometimes a reported issue is actually in another project's code.  For these
issues we may ask you to re-submit your report to the correct project - an
enhancement request has been submitted to GitHub to correct this limitation for
projects hosted on GitHub.

Other times we may verify the issue exists but disagree on the severity or
scope of the issue.  We assess vulnerabilities based on our supported platforms
and common configurations because we need to be able to test and verify issues
and fixes on those supported platforms.

Similar issues (if multiple vulnerabilities are reported) will be combined if
they share a common root cause.  We don't mean any disrespect by doing this, we
just want to make sure your issues are truly and efficiently addressed in full.

Once we have verified things, we will work towards providing a fix as quickly
as possible.  Fixes are typically developed against the "master" branch, then
backported as needed to cover shipping CUPS releases on our supported platforms.

Once we have the fixes ready, we request a CVE, coordinate an embargo date, and
announce it on `distros@vs.openwall.org` mailing list.  The embargo period is
typically 7-10 days long but can be longer.

The embargo starts a flurry of activity - hundred of developers supporting every
Linux distribution, the various BSD flavors, macOS, and ChromeOS will queue up
the security updates for their respective OS releases on the embargo date.
