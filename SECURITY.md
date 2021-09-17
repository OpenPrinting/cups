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

Report all security issues to "security AT msweet.org".  Expect a response
within 5 business days.  Any proposed embargo date should be at least 30 days
and no more than 90 days in the future.


PGP Public Key
--------------

The following PGP public key can be used for signing security messages.

```
-----BEGIN PGP PUBLIC KEY BLOCK-----
Comment: GPGTools - https://gpgtools.org

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
WkiKlA==
=0GzT
-----END PGP PUBLIC KEY BLOCK-----
```
