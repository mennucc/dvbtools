INCS=-I /usr/include/libxml2
CFLAGS= -Wall $(INCS)
CC=gcc
all: dvbtune

INCS += -I ../DVB/include

ifdef UK
  CFLAGS += -DUK
endif

ifdef FINLAND
  CFLAGS += -DFINLAND
endif

ifdef FINLAND2
  CFLAGS += -DFINLAND2
endif

tune.o: tune.c tune.h dvb_defaults.h

dvbtune: dvbtune.c tune.o

xml2vdr: xml2vdr.c
	 $(CC) $(CFLAGS) -o xml2vdr xml2vdr.c -lxml2

clean:
	rm -f *.o *~ dvbtune xml2vdr
