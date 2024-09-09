#
# Library Makefile for CUPS.
#
# Copyright © 2022-2024 by OpenPrinting.
# Copyright © 2007-2021 by Apple Inc.
# Copyright © 1997-2006 by Easy Software Products, all rights reserved.
#
# Licensed under Apache License v2.0.  See the file "LICENSE" for more
# information.
#

include ../Makedefs


#
# Object files...
#

COREOBJS	=	\
		array.o \
		auth.o \
		debug.o \
		dest.o \
		dest-job.o \
		dest-localization.o \
		dest-options.o \
		dir.o \
		dnssd.o \
		encode.o \
		file.o \
		form.o \
		getputfile.o \
		globals.o \
		hash.o \
		http.o \
		http-addr.o \
		http-addrlist.o \
		http-support.o \
		ipp.o \
		ipp-file.o \
		ipp-support.o \
		json.o \
		jwt.o \
		langprintf.o \
		language.o \
		md5.o \
		md5passwd.o \
		notify.o \
		oauth.o \
		options.o \
		pwg-media.o \
		rand.o \
		raster-error.o \
		raster-stream.o \
		raster-stubs.o \
		request.o \
		string.o \
		tempfile.o \
		thread.o \
		tls.o \
		transcode.o \
		usersys.o \
		util.o

DRIVEROBJS	=	\
		adminutil.o \
		backchannel.o \
		backend.o \
		getdevices.o \
		getifaddrs.o \
		ppd.o \
		ppd-attr.o \
		ppd-cache.o \
		ppd-conflicts.o \
		ppd-custom.o \
		ppd-emit.o \
		ppd-localize.o \
		ppd-mark.o \
		ppd-page.o \
		ppd-util.o \
		raster-interpret.o \
		raster-interstub.o \
		sidechannel.o \
		snmp.o

LIBOBJS		= \
		$(LIBCUPSOBJS)

IMAGEOBJS	= \
		raster-interstub.o \
		raster-stubs.o

TESTOBJS	= \
		cachebench.o \
		fuzzipp.o \
		rasterbench.o \
		testadmin.o \
		testarray.o \
		testcache.o \
		testclient.o \
		testconflicts.o \
		testcreds.o \
		testcups.o \
		testdest.o \
		testdnssd.o \
		testfile.o \
		testform.o \
		testgetdests.o \
		testhttp.o \
		testi18n.o \
		testipp.o \
		testjson.o \
		testjwt.o \
		testlang.o \
		testoauth.o \
		testoptions.o \
		testppd.o \
		testpwg.o \
		testraster.o \
		testsnmp.o \
		testtestpage.o \
		testthreads.o \
		tlscheck.o
OBJS	=	\
		$(LIBOBJS) \
		$(IMAGEOBJS) \
		$(TESTOBJS)


#
# Header files to install...
#

COREHEADERS	=	\
		array.h \
		base.h \
		cups.h \
		dir.h \
		dnssd.h \
		file.h \
		form.h \
		http.h \
		ipp.h \
		json.h \
		jwt.h \
		language.h \
		oauth.h \
		pwg.h \
		raster.h \
		thread.h \
		transcode.h

DRIVERHEADERS	=	\
		adminutil.h \
		backend.h \
		ppd.h \
		sidechannel.h

HEADERS		=	\
		$(LIBHEADERS)

COREHEADERSPRIV =	\
		cups-private.h \
		debug-private.h \
		file-private.h \
		http-private.h \
		ipp-private.h \
		json-private.h \
		language-private.h \
		pwg-private.h \
		raster-private.h \
		string-private.h

DRIVERHEADERSPRIV	=	\
		ppd-private.h \
		snmp-private.h

HEADERSPRIV	=	\
		$(LIBHEADERSPRIV)


#
# Targets in this directory...
#

LIBTARGETS =	\
		$(LIBCUPSIMAGE) \
		$(LIBCUPSSTATIC) \
		$(LIBCUPS) \
		libcupsimage.a

UNITTARGETS =	\
		cachebench \
		rasterbench \
		testadmin \
		testarray \
		testcache \
		testclient \
		testconflicts \
		testcreds \
		testcups \
		testdest \
		testdnssd \
		testfile \
		testform \
		testgetdests \
		testhttp \
		testi18n \
		testipp \
		testjson \
		testjwt \
		testlang \
		testoauth \
		testoptions \
		testppd \
		testpwg \
		testraster \
		testsnmp \
		testtestpage \
		testthreads \
		tlscheck

TARGETS	=	\
		$(LIBTARGETS)


#
# Make all targets...
#

all:		$(TARGETS)


#
# Make library targets...
#

libs:		$(LIBTARGETS)


#
# Run unit tests...
#

test:		unittests
	echo "Tests started at $$(date)..." >test.log
	echo Running array API tests...
	./testarray >>test.log
	echo Running file API tests...
	./testfile >>test.log
	echo Running form API tests...
	./testform >>test.log
	echo Running HTTP API tests...
	./testhttp >>test.log
	echo Running IPP API tests...
	./testipp >>test.log
	echo Running internationalization API tests...
	./testi18n >>test.log
	echo Running JSON API tests...
	./testjson >>test.log
	echo Running JWT API tests...
	./testjwt >>test.log
	echo Creating locale directory structure...
	$(RM) -r locale
	for po in ../locale/cups_*.po; do \
		lang=`basename $$po .po | sed -e '1,$$s/^cups_//'`; \
		$(MKDIR) locale/$$lang; \
		$(LN) ../../$$po locale/$$lang; \
	done
	echo Running language API tests...
	echo ls -al locale >>test.log
	ls -al locale >>test.log
	LOCALEDIR=locale ./testlang >>test.log
	echo Running option API tests...
	./testoptions >>test.log
	echo Running PPD API tests...
	./testppd >>test.log
	echo Running PWG API tests...
	./testpwg test.ppd >>test.log
	echo Running raster API tests...
	./testraster >>test.log
	echo Running raster testpage API tests...
	./testtestpage >>test.log
#	echo Fuzzing IPP API...
#	./fuzzipp >>test.log
	echo "Tests finished at $$(date)..." >>test.log


#
# Make unit tests...
#

unittests:	$(UNITTARGETS)


#
# Remove object and target files...
#

clean:
	$(RM) $(OBJS) $(TARGETS) $(UNITTARGETS) fuzzipp
	$(RM) libcups.so libcups.dylib
	$(RM) libcupsimage.so libcupsimage.dylib


#
# Update dependencies (without system header dependencies...)
#

depend:
	$(CC) -MM $(ALL_CFLAGS) $(OBJS:.o=.c) | sed -e '1,$$s/ \/usr\/include\/[^ ]*//g' -e '1,$$s/ \/usr\/local\/include\/[^ ]*//g' >Dependencies


#
# Run oclint to check code coverage...
#

oclint:
	oclint -o=oclint.html -html $(LIBOBJS:.o=.c) -- $(ALL_CFLAGS)


#
# Install all targets...
#

install:	all install-data install-headers install-libs install-exec


#
# Install data files...
#

install-data:


#
# Install programs...
#

install-exec:


#
# Install headers...
#

install-headers:
	echo Installing header files into $(INCLUDEDIR)/cups...
	$(INSTALL_DIR) -m 755 $(INCLUDEDIR)/cups
	for file in $(HEADERS); do \
		$(INSTALL_DATA) $$file $(INCLUDEDIR)/cups; \
	done


#
# Install libraries...
#

install-libs: $(LIBTARGETS) $(INSTALLSTATIC)
	echo Installing libraries in $(LIBDIR)...
	$(INSTALL_DIR) -m 755 $(LIBDIR)
	$(INSTALL_LIB) $(LIBCUPS) $(LIBDIR)
	if test $(LIBCUPS) = "libcups.so.2"; then \
		$(RM) $(LIBDIR)/`basename $(LIBCUPS) .2`; \
		$(LN) $(LIBCUPS) $(LIBDIR)/`basename $(LIBCUPS) .2`; \
	fi
	if test $(LIBCUPS) = "libcups.2.dylib"; then \
		$(RM) $(LIBDIR)/libcups.dylib; \
		$(LN) $(LIBCUPS) $(LIBDIR)/libcups.dylib; \
	fi
	-if test "x$(LIBCUPSIMAGE)" != x; then \
		$(INSTALL_LIB) $(LIBCUPSIMAGE) $(LIBDIR); \
	fi
	-if test "x$(LIBCUPSIMAGE)" = "xlibcupsimage.so.2"; then \
		$(RM) $(LIBDIR)/`basename $(LIBCUPSIMAGE) .2`; \
		$(LN) $(LIBCUPSIMAGE) $(LIBDIR)/`basename $(LIBCUPSIMAGE) .2`; \
	fi
	-if test "x$(LIBCUPSIMAGE)" = "xlibcupsimage.2.dylib"; then \
		$(RM) $(LIBDIR)/libcupsimage.dylib; \
		$(LN) $(LIBCUPSIMAGE) $(LIBDIR)/libcupsimage.dylib; \
	fi
	if test "x$(SYMROOT)" != "x"; then \
		$(INSTALL_DIR) $(SYMROOT); \
		cp $(LIBCUPS) $(SYMROOT); \
		dsymutil $(SYMROOT)/$(LIBCUPS); \
		if test "x$(LIBCUPSIMAGE)" != x; then \
			cp $(LIBCUPSIMAGE) $(SYMROOT); \
			dsymutil $(SYMROOT)/$(LIBCUPSIMAGE); \
		fi; \
	fi

installstatic:
	$(INSTALL_DIR) -m 755 $(LIBDIR)
	$(INSTALL_LIB) -m 755 $(LIBCUPSSTATIC) $(LIBDIR)
	$(RANLIB) $(LIBDIR)/$(LIBCUPSSTATIC)
	$(CHMOD) 555 $(LIBDIR)/$(LIBCUPSSTATIC)
	$(INSTALL_LIB) -m 755 libcupsimage.a $(LIBDIR)
	$(RANLIB) $(LIBDIR)/libcupsimage.a
	$(CHMOD) 555 $(LIBDIR)/libcupsimage.a


#
# Uninstall object and target files...
#

uninstall:
	$(RM) $(LIBDIR)/libcups.2.dylib
	$(RM) $(LIBDIR)/libcups.a
	$(RM) $(LIBDIR)/libcups.dylib
	$(RM) $(LIBDIR)/libcups.so
	$(RM) $(LIBDIR)/libcups.so.2
	$(RM) $(LIBDIR)/libcupsimage.2.dylib
	$(RM) $(LIBDIR)/libcupsimage.a
	$(RM) $(LIBDIR)/libcupsimage.dylib
	$(RM) $(LIBDIR)/libcupsimage.so
	$(RM) $(LIBDIR)/libcupsimage.so.2
	-$(RMDIR) $(LIBDIR)
	for file in $(HEADERS); do \
		$(RM) $(INCLUDEDIR)/cups/$$file; \
	done
	-$(RMDIR) $(INCLUDEDIR)/cups


#
# libcups.so.2
#

libcups.so.2:	$(LIBOBJS)
	echo Linking $@...
	$(DSO) $(ARCHFLAGS) $(ALL_DSOFLAGS) -o $@ $(LIBOBJS) $(LIBS)
	$(RM) `basename $@ .2`
	$(LN) $@ `basename $@ .2`


#
# libcups.2.dylib
#

libcups.2.dylib:	$(LIBOBJS)
	echo Linking $@...
	$(DSO) $(ARCHFLAGS) $(ALL_DSOFLAGS) -o $@ \
		-install_name $(libdir)/$@ \
		-current_version 2.15.0 \
		-compatibility_version 2.0.0 \
		$(LIBOBJS) $(LIBS)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@
	$(RM) libcups.dylib
	$(LN) $@ libcups.dylib


#
# libcups.la
#

libcups.la:    $(LIBOBJS)
	echo Linking $@...
	$(LD_CC) $(ARCHFLAGS) $(ALL_DSOFLAGS) -o $@ $(LIBOBJS:.o=.lo) \
		-rpath $(LIBDIR) -version-info 2:15 $(LIBS)


#
# libcups.a
#

libcups.a:	$(LIBOBJS)
	echo Archiving $@...
	$(RM) $@
	$(AR) $(ARFLAGS) $@ $(LIBOBJS)
	$(RANLIB) $@


#
# libcups2.def (Windows DLL exports file...)
#

libcups2.def: $(LIBOBJS) $(IMAGEOBJS) Makefile
	echo Generating $@...
	echo "LIBRARY libcups2" >libcups2.def
	echo "VERSION 2.15" >>libcups2.def
	echo "EXPORTS" >>libcups2.def
	(nm $(LIBOBJS) $(IMAGEOBJS) 2>/dev/null | grep "T _" | awk '{print $$3}'; \
	 echo __cups_strcpy; echo __cups_gettimeofday) | \
		grep -v -E \
		    -e 'cups_debug|Apple|BackChannel|Backend|FileCheck|Filter|GSSService|SetNegotiate|SideChannel|SNMP' \
		    -e 'Block$$' | \
		sed -e '1,$$s/^_//' | sort >>libcups2.def


#
# libcupsimage.so.2
#

libcupsimage.so.2:	$(IMAGEOBJS) libcups.so.2
	echo Linking $@...
	$(DSO) $(ARCHFLAGS) $(ALL_DSOFLAGS) -o $@ $(IMAGEOBJS) $(LINKCUPS)
	$(RM) `basename $@ .2`
	$(LN) $@ `basename $@ .2`


#
# libcupsimage.2.dylib
#

libcupsimage.2.dylib:	$(IMAGEOBJS) libcups.2.dylib
	echo Linking $@...
	$(DSO) $(ARCHFLAGS) $(ALL_DSOFLAGS) -o $@ \
		-install_name $(libdir)/$@ \
		-current_version 2.15.0 \
		-compatibility_version 2.0.0 \
		$(IMAGEOBJS) $(LINKCUPS)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@
	$(RM) libcupsimage.dylib
	$(LN) $@ libcupsimage.dylib


#
# libcupsimage.la
#

libcupsimage.la:       $(IMAGEOBJS) libcups.la
	echo Linking $@...
	$(DSO) $(ARCHFLAGS) $(ALL_DSOFLAGS) -o $@ $(IMAGEOBJS:.o=.lo) \
		$(LINKCUPS) -rpath $(LIBDIR) -version-info 2:15


#
# libcupsimage.a
#

libcupsimage.a:	$(IMAGEOBJS)
	echo Archiving $@...
	$(RM) $@
	$(AR) $(ARFLAGS) $@ $(IMAGEOBJS)
	$(RANLIB) $@


#
# cachebench (dependency on static CUPS library is intentional)
#

cachebench:	cachebench.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ALL_LDFLAGS) -o $@ cachebench.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# fuzzipp (dependency on static CUPS library is intentional)
#

fuzzipp:	fuzzipp.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ARCHFLAGS) $(ALL_LDFLAGS) -o $@ fuzzipp.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# rasterbench (dependency on static CUPS library is intentional)
#

rasterbench:	rasterbench.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ALL_LDFLAGS) -o $@ rasterbench.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testadmin (dependency on static CUPS library is intentional)
#

testadmin:	testadmin.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ALL_LDFLAGS) -o $@ testadmin.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testarray (dependency on static CUPS library is intentional)
#

testarray:	testarray.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ARCHFLAGS) $(ALL_LDFLAGS) -o $@ testarray.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testcache (dependency on static CUPS library is intentional)
#

testcache:	testcache.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ALL_LDFLAGS) -o $@ testcache.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testclient (dependency on static libraries is intentional)
#

testclient:	testclient.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ALL_LDFLAGS) -o $@ testclient.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testconflicts (dependency on static CUPS library is intentional)
#

testconflicts:	testconflicts.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ALL_LDFLAGS) -o $@ testconflicts.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testcreds (dependency on static CUPS library is intentional)
#

testcreds:	testcreds.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ARCHFLAGS) $(ALL_LDFLAGS) -o $@ testcreds.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testcups (dependency on static CUPS library is intentional)
#

testcups:	testcups.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ALL_LDFLAGS) -o $@ testcups.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testdest (dependency on static CUPS library is intentional)
#

testdest:	testdest.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ALL_LDFLAGS) -o $@ testdest.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testdnssd (dependency on static libraries is intentional)
#

testdnssd:	testdnssd.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ALL_LDFLAGS) -o $@ testdnssd.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testfile (dependency on static CUPS library is intentional)
#

testfile:	testfile.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ARCHFLAGS) $(ALL_LDFLAGS) -o $@ testfile.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testform (dependency on static CUPS library is intentional)
#

testform:	testform.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ARCHFLAGS) $(ALL_LDFLAGS) -o $@ testform.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testgetdests (dependency on static CUPS library is intentional)
#

testgetdests:	testgetdests.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ALL_LDFLAGS) -o $@ testgetdests.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testhttp (dependency on static CUPS library is intentional)
#

testhttp:	testhttp.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ARCHFLAGS) $(ALL_LDFLAGS) -o $@ testhttp.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testipp (dependency on static CUPS library is intentional)
#

testipp:	testipp.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ARCHFLAGS) $(ALL_LDFLAGS) -o $@ testipp.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testi18n (dependency on static CUPS library is intentional)
#

testi18n:	testi18n.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ARCHFLAGS) $(ALL_LDFLAGS) -o $@ testi18n.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testjson (dependency on static CUPS library is intentional)
#

testjson:	testjson.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ALL_LDFLAGS) -o $@ testjson.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testjwt (dependency on static CUPS library is intentional)
#

testjwt:	testjwt.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ALL_LDFLAGS) -o $@ testjwt.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testlang (dependency on static CUPS library is intentional)
#

testlang:	testlang.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ARCHFLAGS) $(ALL_LDFLAGS) -o $@ testlang.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testoauth (dependency on static libraries is intentional)
#

testoauth:	testoauth.o $(LIBCUPS_STATIC)
	echo Linking $@...
	$(LD_CC) $(ARCHFLAGS) $(ALL_LDFLAGS) -o $@ testoauth.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testoptions (dependency on static CUPS library is intentional)
#

testoptions:	testoptions.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ARCHFLAGS) $(ALL_LDFLAGS) -o $@ testoptions.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testppd (dependency on static CUPS library is intentional)
#

testppd:	testppd.o $(LIBCUPSSTATIC) test.ppd test2.ppd
	echo Linking $@...
	$(LD_CC) $(ARCHFLAGS) $(ALL_LDFLAGS) -o $@ testppd.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testpwg (dependency on static CUPS library is intentional)
#

testpwg:	testpwg.o $(LIBCUPSSTATIC) test.ppd
	echo Linking $@...
	$(LD_CC) $(ARCHFLAGS) $(ALL_LDFLAGS) -o $@ testpwg.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testraster (dependency on static CUPS library is intentional)
#

testraster:	testraster.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ARCHFLAGS) $(ALL_LDFLAGS) -o $@ testraster.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testsnmp (dependency on static CUPS library is intentional)
#

testsnmp:	testsnmp.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ALL_LDFLAGS) -o $@ testsnmp.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testtestpage (dependency on static CUPS library is intentional)
#

testtestpage:	testtestpage.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ALL_LDFLAGS) -o $@ testtestpage.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# testthreads (dependency on static CUPS library is intentional)
#

testthreads:	testthreads.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ALL_LDFLAGS) -o $@ testthreads.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# tlscheck (dependency on static CUPS library is intentional)
#

tlscheck:	tlscheck.o $(LIBCUPSSTATIC)
	echo Linking $@...
	$(LD_CC) $(ARCHFLAGS) $(ALL_LDFLAGS) -o $@ tlscheck.o $(LINKCUPSSTATIC)
	$(CODE_SIGN) -s "$(CODE_SIGN_IDENTITY)" $@


#
# Automatic API help files...
#

apihelp:
	echo "Generating CUPS Programming Manual..."
	$(RM) cupspm.xml
	codedoc --section "Programming" --body cupspm.md cupspm.xml \
		$(COREOBJS:.o=.c) $(COREHEADERS) tls-openssl.c \
		--coverimage cupspm.png --epub ../doc/help/cupspm.epub
	codedoc --section "Programming" --body cupspm.md cupspm.xml > ../doc/help/cupspm.html
	$(RM) cupspm.xml
	echo "Generating Administration APIs help..."
	codedoc --section "Programming" --title "Administration APIs" \
		--header api-admin.header --body api-admin.shtml \
		adminutil.c adminutil.h getdevices.c >../doc/help/api-admin.html
	echo "Generating PPD API (deprecated) help..."
	codedoc --section "Programming" --title "PPD API (DEPRECATED)" \
		--header api-ppd.header --body api-ppd.shtml \
		ppd.h ppd-*.c raster-interstub.c >../doc/help/api-ppd.html
	echo "Generating Filter and Backend Programming help..."
	codedoc --section "Programming" \
		--title "Filter and Backend Programming" \
		--header api-filter.header --body api-filter.shtml \
		backchannel.c backend.h backend.c sidechannel.c sidechannel.h \
		>../doc/help/api-filter.html


#
# Lines of code computation...
#

sloc:
	echo "libcups: \c"
	sloccount $(LIBOBJS:.o=.c) 2>/dev/null | grep "Total Physical" | awk '{print $$9}'
	echo "libcupsimage: \c"
	sloccount $(IMAGEOBJS:.o=.c) 2>/dev/null | grep "Total Physical" | awk '{print $$9}'


#
# Dependencies...
#

include Dependencies
tls.o: tls-gnutls.c tls-openssl.c
