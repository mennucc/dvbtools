#!/bin/sh
# Scan the UK DVB-T broadcasts from the Crystal Palace transmitter
#
# USAGE: ./scan_uk-t.sh > channels.xml
#        ../xml2vdr channels.xml > channels.conf
#
# See http://www.linuxstb.org/dvb-t/ for instructions on how to find
# the frequencies for your local transmitter.
#
# If you are a DVB-T user outside the UK, you will need to amend tune.c
# with the detailed tuning parameters for your country.

# NOTE: CHANGE CARD TO SUIT YOUR SYSTEM - FIRST CARD IS "0", SECOND IS "1" etc

CARD=1
DVBTUNE=../dvbtune

echo '<?xml version="1.0"?>'
echo '<satellite>'
$DVBTUNE -c $CARD -f 505833000 -i
$DVBTUNE -c $CARD -f 481833000 -i
$DVBTUNE -c $CARD -f 561833000 -i
$DVBTUNE -c $CARD -f 529833000 -i
$DVBTUNE -c $CARD -f 578166000 -i
$DVBTUNE -c $CARD -f 537833000 -i
echo '</satellite>'
