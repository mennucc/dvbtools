#!/bin/sh
curl -s http://www.webviews.co.uk/liveimages/latino01.jpg > /tmp/latino1.jpg
convert -sample 702x576\! /tmp/latino1.jpg /tmp/dvbimage.mpg
