CCFLAGS=-Wall -Wextra
CFLAGS=$(CCFLAGS)
LDFLAGS=-lavahi-common -lavahi-client

all: republish-mdns

SRCS = browse-avahi.c resolve-avahi.c republish-mdns.c
OBJS = $(SRCS:.c=.o)
BIN = republish-mdns

clean:
	rm -f $(OBJS) $(BIN)

browse-avahi.o: CFLAGS = $(CCFLAGS) -I/usr/include/avahi-client -I/usr/include/avahi-common
resolve-avahi.o: CFLAGS = $(CCFLAGS) -I/usr/include/avahi-client -I/usr/include/avahi-common

republish-mdns: $(OBJS)

depend: .depend

.depend: $(SRCS)
	rm -f ./.depend
	$(CC) $(CFLAGS) -I/usr/include/avahi-client -I/usr/include/avahi-common -MM $^ > ./.depend

include .depend

