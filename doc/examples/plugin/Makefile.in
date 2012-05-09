CC += -std=gnu99
CFLAGS ?= -O3 -g2 -Wall -Wextra
CFLAGS += -fPIC -DPIC

LD = $(CC)
LDFLAGS += -shared
LDLIBS = -lpthread

DESTDIR ?= /usr/local


all: saw.so

saw.so:	saw.o 
	$(LD) -o $@ $(LDFLAGS) $^ $(LDLIBS)

install: saw.so
	install saw.so $(DESTDIR)/lib/eegdev

uninstall:
	$(RM) $(DESTDIR)/lib/eegdev/saw.so

clean:
	$(RM)	saw.so saw.o 

