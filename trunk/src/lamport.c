/*
 * /socd/src/lamport.c/lamport.c
 * 
 *  Created on: Nov 6, 2009 
 *      Author: alex
 * -------------------------------------------------------------------------
 */

#include <stdio.h>
#include <unistd.h>
#include <time.h>


#include <common/defs.h>
#include <common/init.h>
#include <common/fsm.h>
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
 * Structure of the lamport DME message
 */
struct lamport_message_s {
    dme_message_hdr_t lm_hdr;
    uint32            tstamp_sec;
    uint32            tstamp_nsec;
    uint32            event;
} PACKED;
typedef struct lamport_message_s lamport_message_t;

#define LAMPORT_MSG_LEN  (sizeof(lamport_message_t))
#define LAMPORT_DATA_LEN (LAMPORT_MSG_LEN - DME_MESSAGE_HEADER_LEN)

/*
 * Helper functions.
 */
static void peer_msg_add_timestamp(lamport_message_t * msg) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    msg->tstamp_sec = ntohl((uint32)ts.tv_sec);
    msg->tstamp_nsec = ntohl((uint32)ts.tv_nsec);
}

/*
 * Informs all peers that something happened in this porcess's state.
 */
int peers_send_inform_message(dme_ev_t ev) {
    lamport_message_t msg = {};
    int err = 0;
    
    switch (ev) {
    case DME_EV_WANT_CRITICAL_REG:
    case DME_EV_EXITED_CRITICAL_REG:
        /* Header section */
        msg.lm_hdr.dme_magic = ntohl(DME_MSG_MAGIC);
        msg.lm_hdr.msg_type = ntohs(MSGT_LAMPORT);
        msg.lm_hdr.length = ntohs(LAMPORT_MSG_LEN);
        msg.lm_hdr.flags = 0;
        msg.lm_hdr.process_id = ntohq(proc_id);
        
        /* lamport specific section */
        msg.event = ev;
        peer_msg_add_timestamp(&msg);
        
        /* send the message */
        err = dme_broadcast_msg((uint8*)&msg, LAMPORT_MSG_LEN);
        
        break;
    default:
        /* No nedd to send informs to peers in other cases */
        break;
    }
    
    return err;
}

/* 
 * Event handler functions.
 * These functions must properly free the cookie recieved, except fo the
 * DME_EV_SUP_MSG_IN and DME_EV_PEER_MSG_IN.
 */

static int fsm_state = PS_IDLE;

static int handle_supervisor_msg(void * cookie) {
    dbg_msg("");
    int ret = 0;
    const buff_t * buff = (buff_t *)cookie; 
    
    switch(fsm_state) {
    case PS_IDLE:
        ret = peers_send_inform_message(DME_EV_WANT_CRITICAL_REG);
        break;

    /* The supervisor should not send a message in these states */
    case PS_PENDING:
    case PS_EXECUTING:
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
static int handle_peer_msg(void * cookie) {
    dbg_msg("");
    int ret = 0;
    const buff_t * buff = (buff_t *)cookie; 
    
    switch(fsm_state) {
    case PS_IDLE:
        break;
    case PS_PENDING:
        break;
    case PS_EXECUTING:
        break;
    default:
        dbg_err("Fatal error: FSM state corrupted");
        ret = ERR_FATAL;
        break;
    }
    
    return ret;
}


int process_ev_want_cr(void * cookie)
{
    dbg_msg("");
    int ret = 0;
    lamport_message_t * msg = NULL;
    
    dbg_msg("Entered DME_EV_WANT_CRITICAL_REG");
    
    if (fsm_state != PS_IDLE) {
        dbg_err("Fatal error: DME_EV_WANT_CRITICAL_REG occured while not in IDLE state.");
        return (ret = ERR_FATAL);
    }
    
    /* TODO: Switch to the pending state and mark the time */
    fsm_state = PS_PENDING;
    
    return ret;
}

int process_ev_entered_cr(void * cookie)
{
    dbg_msg("");
    int ret = 0;
    lamport_message_t * msg = NULL;
    
    dbg_msg("Entered DME_EV_ENTERED_CRITICAL_REG");
    
    if (fsm_state != PS_PENDING) {
        dbg_err("Fatal error: DME_EV_ENTERED_CRITICAL_REG occured while not in PENDING state.");
        return (ret = ERR_FATAL);
    }
    
    /* Switch to the executing state */
    fsm_state = PS_EXECUTING;
    
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
    lamport_message_t * msg = NULL;
    dbg_msg("");
    
    if (fsm_state != PS_EXECUTING) {
        dbg_err("Fatal error: DME_EV_EXITED_CRITICAL_REG occured while not in EXECUTING state.");
        return (ret = ERR_FATAL);
    }
    
    /* Switch to the executing state */
    fsm_state = PS_IDLE;
    
    /* TODO: inform the supervisor and peers that the critical region is now free */
    
    return ret;
}


int main(int argc, char *argv[])
{
    FILE *fh;
    int res = 0;
    
    if (0 != (res = parse_peer_params(argc, argv, &proc_id, &fname))) {
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
    
    register_event_handler(DME_EV_SUP_MSG_IN, handle_supervisor_msg);
    register_event_handler(DME_EV_PEER_MSG_IN, handle_peer_msg);
    register_event_handler(DME_EV_WANT_CRITICAL_REG, process_ev_want_cr);
    register_event_handler(DME_EV_ENTERED_CRITICAL_REG, process_ev_entered_cr);
    register_event_handler(DME_EV_EXITED_CRITICAL_REG, process_ev_exited_cr);

    /*
     * Main loop: just sit here and wait for interrups (triggered by the supervisor).
     * All work is done in interrupt handlers mapped to registered functions.
     */
    wait_events();
    
end:
    /*
     * Do cleanup (dealocating dynamic strucutres)
     */
    deinit_handlers();

    /* Close our listening socket */
    if (nodes && nodes[proc_id].sock_fd > 0) {
        close(nodes[proc_id].sock_fd);
    }
    
    safe_free(nodes);
    
    return res;
}
