#!/bin/sh
PTNUM="2"
for loop in a b c d
do
        echo $loop
        PA="/dev/sd"$loop

        if find $PA
        then
            echo === $PA$PTNUM === exist
            e2fsck -y -v $PA$PTNUM
           
    fi
done
