#
# Top-level Makefile for CUPS.
#
# Copyright © 2020-2023 by OpenPrinting
# Copyright © 2007-2019 by Apple Inc.
# Copyright © 1997-2007 by Easy Software Products, all rights reserved.
#
# Licensed under Apache License v2.0.  See the file "LICENSE" for more
# information.
#

include Makedefs


#
# Directories to make...
#

DIRS	=	cups $(BUILDDIRS)


#
# Test suite options - normally blank, override with make command...
#

TESTOPTIONS	=


#
# Make all targets...
#

all:
	chmod +x cups-config
	echo Using ARCHFLAGS="$(ARCHFLAGS)"
	echo Using ALL_CFLAGS="$(ALL_CFLAGS)"
	echo Using ALL_CXXFLAGS="$(ALL_CXXFLAGS)"
	echo Using CC="$(CC)"
	echo Using CXX="$(CC)"
	echo Using DSOFLAGS="$(DSOFLAGS)"
	echo Using LDFLAGS="$(LDFLAGS)"
	echo Using LIBS="$(LIBS)"
	for dir in $(DIRS); do\
		echo Making all in $$dir... ;\
		(cd $$dir ; $(MAKE) $(MFLAGS) all $(UNITTESTS)) || exit 1;\
	done


#
# Make library targets...
#

libs:
	echo Using ARCHFLAGS="$(ARCHFLAGS)"
	echo Using ALL_CFLAGS="$(ALL_CFLAGS)"
	echo Using ALL_CXXFLAGS="$(ALL_CXXFLAGS)"
	echo Using CC="$(CC)"
	echo Using CXX="$(CC)"
	echo Using DSOFLAGS="$(DSOFLAGS)"
	echo Using LDFLAGS="$(LDFLAGS)"
	echo Using LIBS="$(LIBS)"
	for dir in $(DIRS); do\
		echo Making libraries in $$dir... ;\
		(cd $$dir ; $(MAKE) $(MFLAGS) libs) || exit 1;\
	done


#
# Make unit test targets...
#

unittests:
	echo Using ARCHFLAGS="$(ARCHFLAGS)"
	echo Using ALL_CFLAGS="$(ALL_CFLAGS)"
	echo Using ALL_CXXFLAGS="$(ALL_CXXFLAGS)"
	echo Using CC="$(CC)"
	echo Using CXX="$(CC)"
	echo Using DSOFLAGS="$(DSOFLAGS)"
	echo Using LDFLAGS="$(LDFLAGS)"
	echo Using LIBS="$(LIBS)"
	for dir in $(DIRS); do\
		echo Making all in $$dir... ;\
		(cd $$dir ; $(MAKE) $(MFLAGS) unittests) || exit 1;\
	done


#
# Remove object and target files...
#

clean:
	for dir in $(DIRS); do\
		echo Cleaning in $$dir... ;\
		(cd $$dir; $(MAKE) $(MFLAGS) clean) || exit 1;\
	done


#
# Remove all non-distribution files...
#

distclean:	clean
	$(RM) Makedefs config.h config.log config.status
	$(RM) conf/cups-files.conf conf/cupsd.conf conf/mime.convs conf/pam.std conf/snmp.conf
	$(RM) cups-config
	$(RM) cups.pc
	$(RM) desktop/cups.desktop
	$(RM) doc/index.html
	$(RM) packaging/cups.list
	$(RM) scheduler/cups-lpd.xinetd scheduler/cups.sh scheduler/cups.xml scheduler/org.cups.cups-lpd.plist scheduler/cups-lpdAT.service scheduler/cups.path scheduler/cups.service scheduler/cups.socket
	$(RM) templates/header.tmpl
	-$(RM) doc/*/index.html
	-$(RM) templates/*/header.tmpl
	-$(RM) -r autom4te*.cache cups/charmaps cups/locale


#
# Make dependencies
#

depend:
	for dir in $(DIRS); do\
		echo Making dependencies in $$dir... ;\
		(cd $$dir; $(MAKE) $(MFLAGS) depend) || exit 1;\
	done


#
# Run the STACK tool on the sources, available here:
#
#    http://css.csail.mit.edu/stack/
#
# Do the following to pass options to configure:
#
#    make CONFIGFLAGS="--foo --bar" stack
#

.PHONY: stack
stack:
	stack-build ./configure $(CONFIGFLAGS)
	stack-build $(MAKE) $(MFLAGS) clean all
	poptck
	$(MAKE) $(MFLAGS) distclean
	$(RM) */*.ll
	$(RM) */*.ll.out


#
# Generate a ctags file...
#

ctags:
	ctags -R .


#
# Install everything...
#

install:	install-data install-headers install-libs install-exec


#
# Install data files...
#

install-data:
	echo Making all in cups...
	(cd cups; $(MAKE) $(MFLAGS) all)
	for dir in $(DIRS); do\
		echo Installing data files in $$dir... ;\
		(cd $$dir; $(MAKE) $(MFLAGS) install-data) || exit 1;\
	done
	echo Installing cups-config script...
	$(INSTALL_DIR) -m 755 $(BINDIR)
	$(INSTALL_SCRIPT) cups-config $(BINDIR)/cups-config
	echo Installing cups.pc file...
	$(INSTALL_DIR) -m 755 $(CUPS_PKGCONFPATH)
	$(INSTALL_DATA) cups.pc $(CUPS_PKGCONFPATH)/cups.pc


#
# Install header files...
#

install-headers:
	for dir in $(DIRS); do\
		echo Installing header files in $$dir... ;\
		(cd $$dir; $(MAKE) $(MFLAGS) install-headers) || exit 1;\
	done
	if test "x$(privateinclude)" != x; then \
		echo Installing config.h into $(PRIVATEINCLUDE)...; \
		$(INSTALL_DIR) -m 755 $(PRIVATEINCLUDE); \
		$(INSTALL_DATA) config.h $(PRIVATEINCLUDE)/config.h; \
	fi


#
# Install programs...
#

install-exec:	all
	for dir in $(DIRS); do\
		echo Installing programs in $$dir... ;\
		(cd $$dir; $(MAKE) $(MFLAGS) install-exec) || exit 1;\
	done


#
# Install libraries...
#

install-libs:	libs
	for dir in $(DIRS); do\
		echo Installing libraries in $$dir... ;\
		(cd $$dir; $(MAKE) $(MFLAGS) install-libs) || exit 1;\
	done


#
# Uninstall object and target files...
#

uninstall:
	for dir in $(DIRS); do\
		echo Uninstalling in $$dir... ;\
		(cd $$dir; $(MAKE) $(MFLAGS) uninstall) || exit 1;\
	done
	echo Uninstalling cups-config script...
	$(RM) $(BINDIR)/cups-config
	-$(RMDIR) $(BINDIR)
	echo Uninstalling cups.pc file...
	$(RM) $(CUPS_PKGCONFPATH)/cups.pc
	-$(RMDIR) $(CUPS_PKGCONFPATH)


#
# Run the test suite...
#

testserver:	all unittests
	echo Running CUPS test server...
	cd test; ./run-stp-tests.sh $(TESTOPTIONS)


check test:	all unittests
	echo Running CUPS test suite...
	cd test; ./run-stp-tests.sh 1 0 n n

debugcheck debugtest:	all unittests
	echo Running CUPS test suite with debug printfs...
	cd test; ./run-stp-tests.sh 1 0 n y


#
# Create HTML documentation using codedoc (http://www.msweet.org/codedoc)...
#

apihelp:
	for dir in cups filter; do\
		echo Generating API help in $$dir... ;\
		(cd $$dir; $(MAKE) $(MFLAGS) apihelp) || exit 1;\
	done


#
# Lines of code computation...
#

sloc:
	for dir in cups scheduler; do \
		(cd $$dir; $(MAKE) $(MFLAGS) sloc) || exit 1;\
	done


#
# Make software distributions using EPM (http://www.msweet.org/)...
#

EPMFLAGS	=	-v --output-dir dist $(EPMARCH)

bsd deb epm pkg rpm slackware:
	epm $(EPMFLAGS) -f $@ cups packaging/cups.list

.PHONY:	dist
dist:	all
	$(RM) -r dist
	$(MAKE) $(MFLAGS) epm
	case `uname` in \
		*BSD*) $(MAKE) $(MFLAGS) bsd;; \
		Linux*) test ! -x /usr/bin/rpm || $(MAKE) $(MFLAGS) rpm;; \
		SunOS*) $(MAKE) $(MFLAGS) pkg;; \
	esac


#
# Don't run top-level build targets in parallel...
#

.NOTPARALLEL:
