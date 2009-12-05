/*
 * /socd/src/lamport.c/lamport.c
 * 
 *  Created on: Nov 6, 2009 
 *      Author: alex
 * -------------------------------------------------------------------------
 */

#include <stdio.h>
#include <unistd.h>


#include <common/defs.h>
#include <common/init.h>
#include <common/util.h>
#include <common/net.h>

/* 
 * global vars, defined in each app
 * Don't forget to declare them in each ".c" file
 */
proc_id_t proc_id = 0;                  /* this process id */
link_info_t * nodes = NULL;             /* peer matrix */
size_t nodes_count = 0;
bool_t exit_request = FALSE;

static char * fname = NULL;

/* 
 * Event handler functions.
 * These functions must properly free the cookie revieved.
 */

int testfunc1(void * cookie)
{
    uint8 *buff = NULL;
    uint8 outbuff[256];
    int len = 0;
    proc_id_t source_pid;
    
    /*
     * Test by using this at terminal;
     * $ netcat -u localhost 9001
     */
    dbg_msg("A message arrived from the depths of internet! cookie='%s'", (char *)cookie);
    
    /* This is just a test now */
    dme_recv_msg(&buff, &len);

    /* check the source of the message */
    source_pid = ntohq(*(uint64 *)buff);
    
    if (source_pid == 0) {
        /* This is from the supervisor! Pay attention (pretend your'e working)*/
        dbg_msg("The supervisor told me something ... pretending i'm working");
    } else {
        /* This is a message from someone else. Just inform the supervisor */
        dbg_msg("Announcing the supervisor that a message arrived.\n msg[%d]:\n%s",
                len,buff);
        snprintf(outbuff, sizeof(outbuff),
                 "Dear supervisor somebody sent me this message:\n%s\0", buff);
        dme_send_msg(0, outbuff, sizeof(outbuff));
    }

    return 0;
}

int testfunc2(void * cookie)
{
    dbg_msg("**testfunc2 ** --> This message should appear after sequnetiality check!");
    return 0;
}


int main(int argc, char *argv[])
{
    FILE *fh;
    int res = 0;
    
    if (0 != (res = parse_params(argc, argv, &proc_id, &fname))) {
        dbg_err("parse_args() returned nonzero status:%d", res);
        goto end;
    }
    
    /*
     * Parse the file in fname
     */
    if (0 != (res = parse_file(fname, proc_id, &nodes, &nodes_count))) {
        dbg_err("parse_file() returned nonzero status:%d", res);
        goto end;
    }
    dbg_msg("nodes has %d elements", nodes_count);
    
    
    /*
     * Init connections (open listenning socket)
     */
    if (0 != (res = open_listen_socket(proc_id, nodes, nodes_count))) {
        dbg_err("open_listen_socket() returned nonzero status:%d", res);
        goto end;
    }

    /*
     * Register signals (for I/O, alarms, etc.)
     */
    if (0 != (res = init_handlers(nodes[proc_id].sock_fd))) {
        dbg_err("init_handlers() returned nonzero status");
        goto end;
    }
    
    register_event_handler(DME_EV_MSG_IN, testfunc1);
    register_event_handler(DME_EV_WANT_CRITICAL_REG, testfunc2);

    dbg_msg("Sleeping 5 secs before engaging in network communication."\
            "Wait for other nodes to init");
    sleep(5);
    
    dbg_msg("Sending a tex to another process id");
    
    char buff[256];
    
    sprintf(buff, "\n\n\n\tFrom: %d \n\tTo: %d \nHi there neighbour\n\0", (int)proc_id, 3 - (int)proc_id);
    dme_send_msg(3 - proc_id, buff, strlen(buff)); /* send to other node (1 or 2) */
    
    
    /*
     * Main loop: just sit here and wait for interrups.
     * All work is done in interrupt handlers mapped to registered functions.
     */
    while(!exit_request) {
        wait_events();
    }
    
end:
    /*
     * Do cleanup (dealocating dynamic strucutres)
     */

    /* Close our listening socket */
    if (nodes[proc_id].sock_fd > 0) {
        close(nodes[proc_id].sock_fd);
    }
    
    safe_free(nodes);
    safe_free(nodes); /* test safe_free() on NULL pouinter */
    
    return res;
}
