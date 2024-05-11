CPPFLAGS += -I/usr/include/libnl3
CXXFLAGS += -std=gnu++20 -Wall -g -Os
LDLIBS += -lnl-3 -lnl-route-3

all: symrouted

install: all symrouted.service
	mkdir -p $(DESTDIR)/usr/sbin
	install symrouted $(DESTDIR)/usr/sbin
	mkdir -p $(DESTDIR)/usr/lib/systemd/system
	install -m 644 symrouted.service $(DESTDIR)/usr/lib/systemd/system

clean:
	-rm symrouted

.PHONY: clean
