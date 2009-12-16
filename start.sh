#!/bin/bash
NPROC=`head -n1 dme.conf | cut -d ' ' -f 1`

for (( ix = 1; ix <= NPROC; ix++ )) ; do
        echo "Starting process $ix ..."
        xterm -e "./build/lamport -i $ix -f dme.conf" &
done

echo "Starting supervisor ..."
xterm -e "./build/supervisor -f dme.conf" &