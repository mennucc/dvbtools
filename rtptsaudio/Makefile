CFLAGS =  -g -Wall -O2 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
OBJ=rtptsaudio.o rtp.o mpegtools/ctools.o mpegtools/remux.o mpegtools/transform.o mpegtools/ringbuffy.o
LIBS=-lmad
CC   = gcc    

all: rtptsaudio

clean:
	rm -f rtptsaudio $(OBJ) *~

rtptsaudio: $(OBJ)
	$(CC) $(OBJ) $(LIBS) -o $@
