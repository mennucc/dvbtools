INCS=-I ../DVB/ost/include

all: dvbtext

dvbtext: dvbtext.c tables.h
	gcc -Wall $(INCS) -o dvbtext dvbtext.c

clean:
	rm -f  *.o *~ dvbtext
