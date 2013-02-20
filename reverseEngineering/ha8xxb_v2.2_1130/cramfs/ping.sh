#!/bin/sh

while true;do
	ping -c 1 192.168.2.142 > /dev/null 2&>1
	sleep 1
done

