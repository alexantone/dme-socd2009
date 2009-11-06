/*
 * /socd/src/common/util.c/util.c
 * 
 *  Created on: Oct 23, 2009 
 *      Author: alex
 * 
 */

#include <stdio.h>

#include "common/util.h"

/*
 * getopt() defines and lllib variables
 */
#define OPT_STRING "i:f"

extern char *optarg;
extern int optind, opterr, optopt;


#define USAGE_MESSAGE \
"Usage:\n"\
"       <algorithm-name>  -i <process-id> -f <config-file>\n"

int parse_params(int argc, char * argv[],
                  uint64 *out_proc_id, char ** out_fname)
{
    char optchar = '\0';
    
    if (!out_proc_id || !out_fname) {
        return 1;
    }

    while ((optchar = getopt(argc, argv, OPT_STRING)) != -1) {
        switch(optchar) {
        case 'i':
            *out_proc_id = strtoull(optarg, NULL, BASE_10);
            break;

        case 'f':
            *out_fname = strdup(optarg);
            break;

        default:
            /* Print usage */
            fprintf(stdout, USAGE_MESSAGE);

            exit(ERR_BADARGS);
            break;
        }
    }
    
    return 0;
}

uint64 get_msg_delay_usec(uint64 link_speed, size_t msg_length)
{
    return (msg_length * 1000000 / link_speed);
}
