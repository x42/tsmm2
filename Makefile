PREFIX ?= /usr/local
bindir = $(PREFIX)/bin
mandir = $(PREFIX)/share/man/man1
CFLAGS ?= -Wall -O3

VERSION=0.2

###############################################################################

ifeq ($(shell pkg-config --exists cairo pango pangocairo || echo no), no)
  $(error "mandatory pango/cairo libraries were not found.")
endif

override CFLAGS+=`pkg-config --cflags cairo pango pangocairo`
override LOADLIBES=`pkg-config --libs cairo pango pangocairo` -lm

ifeq ($(shell pkg-config --exists libpng zlib && echo yes), yes)
  override CFLAGS+=`pkg-config --cflags libpng zlib` -DCUSTOM_PNG_WRITER
  override LOADLIBES+=`pkg-config --libs libpng`
else
  $(warning ***)
  $(warning *** zlib or libpng was not found.)
  $(warning *** Bulding without custon PNG writer.)
  $(warning *** This is not optimal and about 20-30% slower.)
  $(warning ***)
endif

###############################################################################

all: tsmm2

man: tsmm2.1

tsmm2: tsmm2.c

tsmm2.1: tsmm2
	help2man -N -n 'Time Stamped Movie Maker' -o tsmm2.1 ./tsmm2

clean:
	rm -f tsmm2

install: install-bin install-man

uninstall: uninstall-bin uninstall-man

install-bin: tsmm2
	install -d $(DESTDIR)$(bindir)
	install -m755 tsmm2 $(DESTDIR)$(bindir)

uninstall-bin:
	rm -f $(DESTDIR)$(bindir)/tsmm2
	-rmdir $(DESTDIR)$(bindir)

install-man:
	install -d $(DESTDIR)$(mandir)
	install -m644 tsmm2.1 $(DESTDIR)$(mandir)

uninstall-man:
	rm -f $(DESTDIR)$(mandir)/tsmm2.1
	-rmdir $(DESTDIR)$(mandir)

.PHONY: all clean man install uninstall install-man install-bin uninstall-man uninstall-bin
