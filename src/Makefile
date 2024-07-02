CPPFLAGS += -I/usr/include/libnl3
CXXFLAGS += -std=gnu++20 -Wall -g -Os
LDLIBS += -lnl-3 -lnl-route-3
INSTALL ?= install
prefix ?= /usr

all: symrouted

install: all
	mkdir -p "$(DESTDIR)$(prefix)/sbin"
	$(INSTALL) -p -m 755 -t "$(DESTDIR)$(prefix)/sbin" symrouted
	mkdir -p "$(DESTDIR)$(prefix)/lib/systemd/system"
	$(INSTALL) -p -m 644 -t "$(DESTDIR)$(prefix)/lib/systemd/system" symrouted.service

clean:
	-rm symrouted

.PHONY: clean install all
