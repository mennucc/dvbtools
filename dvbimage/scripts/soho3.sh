#!/bin/sh
curl -s http://www.webviews.co.uk/liveimages/latino03.jpg > /tmp/latino3.jpg
convert -sample 702x576\! /tmp/latino3.jpg /tmp/dvbimage.mpg
