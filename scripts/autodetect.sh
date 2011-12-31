#!/bin/bash

python videomixer.py 3 640 480 >videomixer.out 2>&1&
oldimage=''

sleep 5

#load the slides video
node addsource.js 0 file://../data/slides.mov
node detectsource.js 0 ../data/images/ 30

while true
do
  line=`tail -1 videomixer.out`
  image=`echo $line | grep spotted | cut -f8 -d' '`
  if [ "X"$image != "X"$oldimage ] && [ "X"$image != "X" ]
  then
     oldimage=$image
     echo image spotted
     node resizesource.js 0 640 480 
     sleep 15
     node resizesource.js 0 320 240 
  fi
  sleep 1
done
