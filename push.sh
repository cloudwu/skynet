#!/bin/sh


timestamp=` ` 
if [ $# == 1 ]
then
	timestamp='$1' 
else
   timestamp=`date "+%Y-%m-%d %H:%M:%S"` 
fi
echo "日志名：$timestamp"
git add . 
git commit -m "$timestamp" 
git push --recurse-submodules=on-demand
