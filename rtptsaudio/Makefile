CFLAGS =  -g -Wall -O2 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -DFPM_DEFAULT -DHAVE_CONFIG_H

OBJ=rtptsaudio.o rtp.o mpegtools/ctools.o mpegtools/remux.o mpegtools/transform.o mpegtools/ringbuffy.o libmad/bit.o libmad/decoder.o libmad/fixed.o libmad/frame.o libmad/huffman.o libmad/layer12.o libmad/layer3.o libmad/stream.o libmad/synth.o libmad/timer.o libmad/version.o

CC   = gcc    

all: rtptsaudio

clean:
	rm -f rtptsaudio $(OBJ) *~

rtptsaudio: $(OBJ)
	$(CC) $(OBJ) $(LIBS) -o $@

