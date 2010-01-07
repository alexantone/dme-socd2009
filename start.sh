#!/bin/bash
NPROC=`head -n1 dme.conf | cut -d ' ' -f 1`
ALGORITHM=lamport
[ -n "$1" ] && ALGORITHM="$1"

for (( ix = 1; ix <= NPROC; ix++ )) ; do
        echo "Starting process $ix ..."
        xtermcmd="./build/$ALGORITHM -i $ix -f dme.conf;\
                  read -p'----------Execution finished----------'"
        xterm -geometry 140x20 -T "$ALGORITHM - Process $ix" -e "$xtermcmd" &
done

echo "Starting supervisor ..."
xterm -geometry 140x20 -T "Supervisor" -e "./build/supervisor -f dme.conf -t 30 -r 70; read -p'--exited--'" &
