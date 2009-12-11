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

int err_code = 0;
bool_t exit_request = FALSE;

static char * fname = NULL;

/* 
 * Event handler functions.
 * These functions must properly free the cookie revieved.
 */

enum dme_lamport_states {
    IDLE,
    PENDING,
    EXECUTING,
};

static int fsm_state = IDLE;


static int supervisor_msg_in(const uint8 * const buff, const int len) {
    int ret = 0;
    
    switch(fsm_state) {
    case IDLE:
        break;
    /* The supervisor should not send a message in these states */
    case PENDING:
    case EXECUTING:
        dbg_msg("Ignoring message from supervisor (not in IDLE state).");
        break;
    default:
        dbg_err("Fatal error: FSM state corrupted");
        ret = ERR_FATAL;
        break;
    }
    
    return ret;
}


/* This is the algortihm's implementation */
static int peer_msg_in(const uint8 * const buff, const int len) {
    int ret = 0;
    
    switch(fsm_state) {
    case IDLE:
        break;
    case PENDING:
        break;
    case EXECUTING:
        break;
    default:
        dbg_err("Fatal error: FSM state corrupted");
        ret = ERR_FATAL;
        break;
    }
    
    return ret;
}

/*
 * msg_demux()
 * 
 * Checks the source of the message (peer/supervisor) and calls the coresponding
 * processing routine.
 */
int msg_demux(void * cookie)
{
    int ret = 0;
    proc_id_t source_pid;
    uint8 *buff = NULL;
    int len = 0;
    
    dbg_msg("A message arrived from the depths of internet! cookie='%s'", (char *)cookie);
    
    /* get the contents of the message */
    if (0 != (ret = dme_recv_msg(&buff, &len))) {
        return ret;
    }
    
    /* check the source of the message */
    source_pid = ntohq(*(uint64 *)buff);
    
    if (source_pid == 0) {
        /* This is from the supervisor*/
        ret = supervisor_msg_in(buff, len);
    } else if (source_pid >= 1 && source_pid <= nodes_count) {
        ret = peer_msg_in(buff, len);
    } else {
        dbg_err("Recieved possibly malformed messge:"\
                " 'Process ID'=%llu out of bounds", source_pid);
        ret = ERR_FATAL;
    }
    
    safe_free(buff);
    return ret;
}

int process_ev_want_cr(void * cookie)
{
    int ret = 0;
    dme_message_t * msg = NULL;
    
    dbg_msg("Entered DME_EV_WANT_CRITICAL_REG");
    
    if (fsm_state != IDLE) {
        dbg_err("Fatal error: DME_EV_WANT_CRITICAL_REG occured while not in IDLE state.");
        return (ret = ERR_FATAL);
    }
    
    /* TODO: Switch to the pending state and mark the time */
    fsm_state = PENDING;
    
    return ret;
}

int process_ev_entered_cr(void * cookie)
{
    int ret = 0;
    dme_message_t * msg = NULL;
    
    dbg_msg("Entered DME_EV_ENTERED_CRITICAL_REG");
    
    if (fsm_state != PENDING) {
        dbg_err("Fatal error: DME_EV_ENTERED_CRITICAL_REG occured while not in PENDING state.");
        return (ret = ERR_FATAL);
    }
    
    /* Switch to the executing state */
    fsm_state = EXECUTING;
    
    /* 
     * TODO: Start a timer that will expire after the tdelta recieved from the supervisor
     *       The tdelta is stored in a global var or in the cookie (to be discussed)
     */
    
    /* TODO: inform the supervisor by sending the tdelta measured from the time mark */
    
    return ret;
}

int process_ev_exited_cr(void * cookie)
{
    int ret = 0;
    dme_message_t * msg = NULL;
    
    if (fsm_state != EXECUTING) {
        dbg_err("Fatal error: DME_EV_EXITED_CRITICAL_REG occured while not in EXECUTING state.");
        return (ret = ERR_FATAL);
    }
    
    /* Switch to the executing state */
    fsm_state = IDLE;
    
    /* TODO: inform the supervisor and peers that the critical region is now free */
    
    return ret;
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
    
    register_event_handler(DME_EV_MSG_IN, msg_demux);
    register_event_handler(DME_EV_WANT_CRITICAL_REG, process_ev_want_cr);
    register_event_handler(DME_EV_ENTERED_CRITICAL_REG, process_ev_entered_cr);
    register_event_handler(DME_EV_EXITED_CRITICAL_REG, process_ev_exited_cr);

    schedule_event(DME_EV_WANT_CRITICAL_REG, 3, 0, NULL);
    schedule_event(DME_EV_ENTERED_CRITICAL_REG, 6, 0, NULL);
    
    /*
     * Main loop: just sit here and wait for interrups (triggered by the supervisor).
     * All work is done in interrupt handlers mapped to registered functions.
     */
    while(!exit_request) {
        wait_events();
    }
    
end:
    /*
     * Do cleanup (dealocating dynamic strucutres)
     */
    deinit_handlers();

    /* Close our listening socket */
    if (nodes[proc_id].sock_fd > 0) {
        close(nodes[proc_id].sock_fd);
    }
    
    safe_free(nodes);
    safe_free(nodes); /* test safe_free() on NULL pouinter */
    
    return res;
}
