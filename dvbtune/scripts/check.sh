#!/bin/sh 
# 
# Read a file containing "FREQ POL SRATE" lines and produce a
# XML file containing the DVB-SI data.
#
# Usage: ./check.sh < astra19.txt > astra19.xml
#
# Change the DVBTUNE variable to point to your installed version of dvbtune
# Change CARD to be 0 (first card), 1 (second card), 2 (third card) etc

DVBTUNE=../dvbtune
CARD=0

echo '<?xml version="1.0"?>'
echo '<satellite>'
while read x y z
do
  $DVBTUNE -c $CARD -f $x -p $y -s $z -i
done
echo '</satellite>'
