CC=gcc
OBJS=dvbsubs dvbtextsubs xml2spumux
CFLAGS+=-Wall

INCS+=-I/usr/include/libxml2 -I/usr/include/freetype2

all: $(OBJS)

dvbsubs: dvbsubs.c dvbsubs.h bitmap.o pes.o
	$(CC) $(INCS) $(CFLAGS) -o dvbsubs dvbsubs.c bitmap.o pes.o -lpng -lm -lz

dvbtextsubs: dvbtextsubs.c tables.h vtxdecode.h
	$(CC) $(INCS) $(CFLAGS) -o dvbtextsubs dvbtextsubs.c pes.o

bitmap.o: bitmap.c bitmap.h
	$(CC) $(INCS) $(CFLAGS) -c -o bitmap.o bitmap.c

render_freetype.o: render_freetype.c render_freetype.h
	$(CC) $(INCS) $(CFLAGS) -c -o render_freetype.o render_freetype.c

pes.o: pes.c pes.h
	$(CC) $(INCS) $(CFLAGS) -c -o pes.o pes.c

xml2spumux: xml2spumux.h xml2spumux.c bitmap.o render_freetype.o
	$(CC) $(INCS) $(CFLAGS) -o xml2spumux xml2spumux.c bitmap.o render_freetype.o -lxml2 -lpng -lfreetype -lm -lz

clean:
	rm -f  *.o *~ $(OBJS)
