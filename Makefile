CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
PREFIX  ?= /usr/local

GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS   := $(shell pkg-config --libs gtk+-3.0)

all: lxlocale

lxlocale: lxlocale.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ $< $(GTK_LIBS)

install: lxlocale
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 lxlocale $(DESTDIR)$(PREFIX)/bin/lxlocale
	install -d $(DESTDIR)$(PREFIX)/share/applications
	install -m 644 lxlocale.desktop $(DESTDIR)$(PREFIX)/share/applications/lxlocale.desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/lxlocale
	rm -f $(DESTDIR)$(PREFIX)/share/applications/lxlocale.desktop

clean:
	rm -f lxlocale

.PHONY: all install uninstall clean
