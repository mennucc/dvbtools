#!/bin/sh
# Scan German Cable TV broadcasts.

# NOTE: CHANGE CARD TO SUIT YOUR SYSTEM - FIRST CARD IS "0", SECOND IS "1" etc

CARD=0
DVBTUNE=../dvbtune
XML2VDR=../xml2vdr

get_frequency_list() {
	cat << END | grep -v "^#"
#322000000
#330000000
#338000000
346000000
354000000
362000000
370000000
378000000
#386000000
394000000
402000000
410000000
426000000
434000000
442000000
#450000000
END
}

get_frequency_list_test() {
	cat << END | grep -v "^#"
346000000
END
}



get_channels() {
	echo '<?xml version="1.0"?>'
	echo '<satellite>'
	get_frequency_list | while read frequency; do
		$DVBTUNE -c $CARD -f $frequency -i -s 6900
		sleep 1
	done
	echo '</satellite>'
}

LISTXMLORIG="`get_channels`"
echo "$LISTXMLORIG" >listorig.xml


LISTXML="`echo "$LISTXMLORIG" | tr -d '\206\207\005' | sed 's/Ö/Oe/g' | sed 's/Ä/Ae/g' | sed 's/Ü/ue/g' | sed 's/ä/Ae/g' | sed 's/ö/Oe/g' | sed 's/ü/ue/g' `"
echo "$LISTXML" >list.xml

LISTVDR="`echo "$LISTXML" | $XML2VDR - | sed 's/\&amp;/\&/g' | sed 's/000:/:/g`"
echo "$LISTVDR" >list.vdr

# Sort output
echo ":Television"
echo "$LISTVDR" | grep -i "(TV)" | sed 's/ (TV)//g' | sed 's/^ *//g' | sed 's/::/:0:/g'
echo ":Radio"
echo "$LISTVDR" | grep -i "(RADIO)" | sed 's/ (RADIO)//g' | sed 's/^ *//g' | sed 's/::/:0:/g'

