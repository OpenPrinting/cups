#
# Makefile for the cups-proxyd
#
# Copyright © 2020 by Till Kamppeter
#
# Licensed under Apache License v2.0.  See the file "LICENSE" for more
# information.
#

# Version and
VERSION		=	1.0
prefix		=	$(DESTDIR)/usr
includedir	=	$(prefix)/include
sbindir		=	$(prefix)/sbin
libdir		=	$(prefix)/lib
cupsbackenddir	=	$(prefix)/lib/cups/backend


# Compiler/linker options...
OPTIM		=	-Os -g
CFLAGS		+=	`cups-config --cflags` `pkg-config --cflags "avahi-client" 2>/dev/null` `pkg-config --cflags "glib-2.0 >= 2.30.2" 2>/dev/null` `pkg-config --cflags "avahi-glib" 2>/dev/null` `pkg-config --cflags "gio-unix-2.0" 2>/dev/null` $(OPTIM) -DVERSION='"$(VERSION)"'
LDFLAGS		+=	$(OPTIM) `cups-config --ldflags`
LIBS		+=	`cups-config --libs` `pkg-config --libs "avahi-client" 2>/dev/null` `pkg-config --libs "glib-2.0 >= 2.30.2" 2>/dev/null` `pkg-config --libs "avahi-glib" 2>/dev/null` `pkg-config --libs "gio-unix-2.0" 2>/dev/null`

# Targets...
CUPS_PROXYD_OBJS	=	cups-proxyd.o cups-notifier.o
PROXY_OBJS		=	proxy.o
OBJS			=	$(CUPS_PROXYD_OBJS) $(PROXY_OBJS)
TARGETS			=	cups-proxyd proxy

# General build rules...
.SUFFIXES:	.c .o
.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

# Targets...
all:	$(TARGETS)

clean:
	rm -f $(TARGETS) $(OBJS) $(cups_notifier_sources)

install:	$(TARGETS)
	mkdir -p $(sbindir)
	cp cups-proxyd $(sbindir)
	mkdir -p $(cupsbackenddir)
	cp proxy $(cupsbackenddir)

cups-proxyd.o:	cups-proxyd.c cups-notifier.h
	$(CC) $(CFLAGS) -c -o cups-proxyd.o cups-proxyd.c

cups-proxyd:	$(CUPS_PROXYD_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(CUPS_PROXYD_OBJS) $(LIBS)

proxy:	$(PROXY_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(PROXY_OBJS) $(LIBS)

$(OBJS):	Makefile

# Generate sources for CUPS notifier client...
cups_notifier_sources = \
	cups-notifier.c \
	cups-notifier.h

$(cups_notifier_sources): org.cups.cupsd.Notifier.xml
	gdbus-codegen \
	    --interface-prefix org.cups.cupsd \
	    --c-namespace Cups \
	    --generate-c-code cups-notifier \
	    org.cups.cupsd.Notifier.xml
