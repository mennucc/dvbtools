INCS=-I ../DVB/ost/include -I /usr/include/libxml2
CFLAGS= -Wall $(INCS)
CC=gcc
all: dvbtune

tune.o: tune.c tune.h

dvbtune: dvbtune.c tune.o

xml2vdr: xml2vdr.c
	 $(CC) $(CFLAGS) -o xml2vdr xml2vdr.c -lxml2

clean:
	rm -f *.o *~ dvbtune xml2vdr
