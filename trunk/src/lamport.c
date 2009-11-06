/*
 * /socd/src/lamport.c/lamport.c
 * 
 *  Created on: Nov 6, 2009 
 *      Author: alex
 * -------------------------------------------------------------------------
 */

#include <stdio.h>
#include <unistd.h>


#include "common/defs.h"
#include "common/init.h"
#include "common/util.h"

/* 
 * Static global vars, defined in each app
 * Don't forget to declare them in each ".c" file
 */
static char * fname = NULL;
static uint64 proc_id = 0;

bool_t exitreq;



int main(int argc, char *argv[])
{
    FILE *fh;
    
    if (0 != parse_params(argc, argv, &proc_id, &fname)) {
        dbg_err("Params were");
    }
    
    /*
     * Parse the file in fname
     */
    
    /*
     * Register signals (for I/O, alarms, etc.)
     */
    
    
    /*
     * Init connections (open sockets)
     */
    
    /*
     * Main loop: just sit htere and wait for interrups.
     * All work is done in interrupt handlers
     */
    
    while(!exit_req) {
        pause()
    }
    
    /*
     * Do cleanup (dealoocatin dynamic strucutres)
     */
    
    
}
