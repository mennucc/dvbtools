#!/bin/sh
curl -s http://212.187.231.150/studiocam/xfmstudiocam_0000000001.jpg > /tmp/xfm.jpg
convert -sample 702x576\! /tmp/xfm.jpg /tmp/dvbimage.mpg
