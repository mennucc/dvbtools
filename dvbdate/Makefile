
# Delete the following line if you are not using a 
# "NEWSTRUCT" driver.  If you are using a "NEWSTRUCT"
# driver, it must be later than October 10th 2002

NEWSTRUCT=1

ifdef NEWSTRUCT
  CFLAGS += -DNEWSTRUCT
endif

INCS=-I ../DVB/ost/include

CC=gcc $(INCS)
all: dvbdate

dvbdate: dvbdate.o options.o

install: dvbdate
	cp dvbdate /usr/bin
clean:
	rm -f *.o dvbdate

