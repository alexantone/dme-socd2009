#!/bin/bash
NPROC=`head -n1 dme.conf | cut -d ' ' -f 1`

ALGORITHM=lamport
SUPDEF_ARGS="-f dme.conf -t 30 -r 70 -o supervisor.log"

USAGEMSG="Usage:\n"\
"       start.sh   [algorithm]\n"\
"                  [-f <config-file>] [-r <concurency ratio>] [-t <sec interval>]\n"\
"                  [-o <out-logfile>]\n\n"\
"By default the algorithm is lamport. See build/ for other algorithms.\n"\
"Default parameter for supervisor are:\n\t'$SUPERVISOR_ARGS'\n"


[ "$1" == "-h" ] && {
    echo -e "$USAGEMSG"
    exit
}

[ -n "$1" ] &&  {
    ALGORITHM="$1"
    shift 1
}

[[ "$#" -gt 0 ]] && SUPERVISOR_ARGS="$@"

for (( ix = 1; ix <= NPROC; ix++ )) ; do
        echo "Starting process $ix ..."
        xtermcmd="./build/$ALGORITHM -i $ix -f dme.conf;\
                  read -p'----------Execution finished----------'"
        xterm -geometry 140x20 -T "$ix: $ALGORITHM - Process $ix" -e "$xtermcmd" &
        sleep 0.333
done

echo "Starting supervisor ..."
xterm -geometry 140x20 -T "Supervisor" -e "./build/supervisor $SUPDEF_ARGS $SUPERVISOR_ARGS ; read -p'--exited--'" &
