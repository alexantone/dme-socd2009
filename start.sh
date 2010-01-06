#!/bin/bash
NPROC=`head -n1 dme.conf | cut -d ' ' -f 1`

for (( ix = 1; ix <= NPROC; ix++ )) ; do
        echo "Starting process $ix ..."
        xtermcmd="./build/lamport -i $ix -f dme.conf;\
                  echo -e '\n\n-----------Execution finished----------\n';\
                  read" 
        xterm -geometry 140x20 -T "Process $ix" -e "$xtermcmd" &
done

echo "Starting supervisor ..."
xterm -geometry 140x20 -T "Supervisor" -e ./build/supervisor -f dme.conf -t 30 -r 70 &
