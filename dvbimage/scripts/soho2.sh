#!/bin/sh
curl -s http://www.webviews.co.uk/liveimages/latino02.jpg > /tmp/latino2.jpg
convert -sample 702x576\! /tmp/latino2.jpg /tmp/dvbimage.mpg
