INCS=-I /usr/include/libxml2
CFLAGS= -Wall $(INCS)
CC=gcc
all: dvbtune

# Delete the following line if you are not using a 
# "NEWSTRUCT" driver.  If you are using a "NEWSTRUCT"
# driver, it must be later than October 10th 2002

NEWSTRUCT=1

ifdef NEWSTRUCT
  CFLAGS += -DNEWSTRUCT
  INCS += -I ../DVB/include
else
  INCS += -I ../DVB/ost/include
endif

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
