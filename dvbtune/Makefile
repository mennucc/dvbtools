INCS=-I /usr/include/libxml2
CFLAGS= -Wall $(INCS)
LDFLAGS=-lxml2
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
si.o: si.c si.h

dvbtune.o: dvbtune.c tune.h si.h

dvbtune: dvbtune.o tune.o si.o

xml2vdr: xml2vdr.c

clean:
	rm -f *.o *~ dvbtune xml2vdr
