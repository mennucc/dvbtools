INCS=-I ../DVB/ost/include

CC=gcc $(INCS)
all: dvbdate

dvbdate: dvbdate.o options.o

install: dvbdate
	cp dvbdate /usr/bin
clean:
	rm -f *.o dvbdate

