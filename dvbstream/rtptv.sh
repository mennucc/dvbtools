#!/bin/sh
DUMPRTP=/home/dave/DVB/dvbstream-0.2/dumprtp
TS2PS=/home/dave/DVB/DVB/apps/mpegtools/ts2ps
BFR=bfr
MPLAYER='mplayer -ao sdl -framedrop -'

$DUMPRTP | $TS2PS 1 2 | $BFR -m 1024kB  | $MPLAYER
