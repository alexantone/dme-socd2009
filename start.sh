#!/bin/bash
NPROC=`head -n1 dme.conf | cut -d ' ' -f 1`

for (( ix = 1; ix <= NPROC; ix++ )) ; do
        echo "Starting process $ix ..."
        xterm -e "./build/lamport -i $ix -f dme.conf 2>&1 |tee  process$ix.log ; echo -e '\n\n-----------Execution finished-------\n'; read " &
done

echo "Starting supervisor ..."
gnome-terminal -t "Supervisor" -x ./build/supervisor -f dme.conf -t 30 -r 70 &
