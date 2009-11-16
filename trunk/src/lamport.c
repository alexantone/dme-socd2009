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
uint64 proc_id = 0;                     /* this process id */
link_info_t * nodes = NULL;             /* peer matrix */
bool_t exit_request = FALSE;

static char * fname = NULL;

/* 
 * Event handler functions.
 * These functions must properly free the cookie revieved.
 */

int testfunc1(void * cookie) 
{
    uint8 *buff = NULL;
    int len = 0;
    /*
     * Test by using this at terminal;
     * $ netcat -u localhost 9001
     */
    dbg_msg("A message arrived from the depths of internet! cookie='%s'", (char *)cookie);
    dbg_msg("Go check the socket! :p");
    
    /* This is just a test now */
    dme_recv_msg(&buff, &len);
    dbg_msg("Oh goodie! recieved message[%d]: %s", len, buff);
    
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
    if (0 != (res = parse_file(fname, proc_id, &nodes))) {
        dbg_err("parse_file() returned nonzero status:%d", res);
        goto end;
    }
    dbg_msg("nodes has %d elements", sizeof(nodes));
    
    
    /*
     * Init connections (open listenning socket)
     */
    if (0 != (res = open_listen_socket(proc_id, nodes))) {
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

    register_event_handler(20, testfunc1); /* This should cause an error */
    
    
    
    dbg_msg("Sleeping 2 secs before delivering event DME_EV_MSG_IN");
    sleep(2);
    
    deliver_event(DME_EV_MSG_IN, "GREAT SUCCESS");
    deliver_event(DME_EV_MSG_IN, "O'rly ?");
    
    deliver_event(20, "This should generate 'how we got here???' msg");
    
    
    dbg_msg("Sleeping 5 secs before engaging in network communication."\
            "Wait for other nodes to init");
    sleep(5);
    
    dbg_msg("Sending a tex to another process id");
    
    char * buff[256];
    
    sprintf(buff, "\n\n\n\t\t *** This is a message from peer process %d ****\n\n\n\n", proc_id);
    dme_send_msg((proc_id +1) % 2, buff, strlen(buff)); /* send to other node (1 or 0) */
    
    
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
