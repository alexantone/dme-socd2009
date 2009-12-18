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
static struct timespec sup_tstamp;      /* used for performance measurements */

/*
 * Lamport specifics
 */
enum lmaport_msg_types {
    MTYPE_REQUEST,
    MTYPE_REPLY,
    MTYPE_RELEASE,
};

/*
 * Structure of the lamport DME message
 */
struct lamport_message_s {
    dme_message_hdr_t lm_hdr;
    uint32            type;             /* REQUEST/REPLY/RELEASE */
    uint32            tstamp_sec;
    uint32            tstamp_nsec;
    proc_id_t         pid;              /* even though is redundant it's used to mirror the theory */
} PACKED;
typedef struct lamport_message_s lamport_message_t;

#define LAMPORT_MSG_LEN  (sizeof(lamport_message_t))
#define LAMPORT_DATA_LEN (LAMPORT_MSG_LEN - DME_MESSAGE_HEADER_LEN)

typedef struct request_queue_elem_s {
    uint32 sec_tstamp;
    uint32 nsec_tstamp;
    proc_id_t pid;
    struct request_queue_elem_s * next;
} request_queue_elem_t;

static request_queue_elem_t * request_queue = NULL;
static bool_t * replies = NULL;   /* Keep status of REPLY messages from peers */

/*
 * Helper functions.
 */

/*
 * Give a result similar to strcmp
 */
static int req_elmt_cmp(const request_queue_elem_t * const a,
                        const request_queue_elem_t * const b) {
    if ((a->sec_tstamp < b->sec_tstamp) ||
        (a->sec_tstamp == b->sec_tstamp) && (a->nsec_tstamp < b->nsec_tstamp) ||
        (a->sec_tstamp == b->sec_tstamp) && (a->nsec_tstamp == b->nsec_tstamp) && (a->pid < b->pid))
    {
        return -1;
    }
    return 1;
}

static void request_queue_insert(request_queue_elem_t * const elmt) {
    request_queue_elem_t * cx = request_queue;  /* current element */
    request_queue_elem_t * px = request_queue;  /* previous element */
    /* if queue is empty just create the queue */
    if (!request_queue) {
        request_queue = elmt;
        request_queue->next = NULL;
        return;
    }
    
    /* search for the right spot to insert this element */
    while (req_elmt_cmp(elmt, cx) > 0 && cx) {
        px = cx;
        cx = cx->next;
    }
    
    if (cx == request_queue) {
        /* The element must become the new head of queue */
        request_queue = elmt;
        elmt->next = cx;
    } else {
        /* Normal insertion */
        px->next = elmt;
        elmt->next = cx;
    }
}

static void request_queue_pop(void){
    request_queue_elem_t * px = request_queue;
    if (request_queue) {
        request_queue = request_queue->next;
        safe_free(px);
    }
}

/* 
 * Checks if it's this processes turn to enter the CS
 */
static bool_t my_turn(void) {
    int ix;
    bool_t keep_going = TRUE;
    /* First check if all the replies arrived from the other peers */
    for (ix = 1; ix < proc_id && keep_going; ix++) {
        keep_going = keep_going && replies[ix];
    }
    for (ix = proc_id + 1; ix <= nodes_count && keep_going; ix++) {
        keep_going = keep_going && replies[ix];
    }
    
    /* If all preers replied and we're on top then it's our turn */
    return (keep_going && (request_queue && request_queue->pid == proc_id));
}

static void peer_msg_add_timestamp(lamport_message_t * msg) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    msg->tstamp_sec = htonl((uint32)ts.tv_sec);
    msg->tstamp_nsec = htonl((uint32)ts.tv_nsec);
}

/*
 * Prepare a lamport message for network sending.
 */
static int lamport_msg_set(lamport_message_t * const msg, unsigned int msgtype) {
    if (!msg) {
        return ERR_DME_HDR;
    }
    
    /* first set the header */
    dme_header_set(&msg->lm_hdr, MSGT_LAMPORT, LAMPORT_MSG_LEN, 0);
    
    /* then the lamport specific data */
    peer_msg_add_timestamp(msg);
    msg->type = htonl(msgtype);
    msg->pid = htonq(proc_id);
    
    return 0;
}

/*
 * Parse a recieved lamport message. The space must be allready allocated in 'msg'.
 */
static int lamport_msg_parse(buff_t buff, lamport_message_t * msg) {
    lamport_message_t * src = (lamport_message_t *)buff.data;

    if (!msg || buff.data == NULL || buff.len < DME_MESSAGE_HEADER_LEN) {
        return ERR_DME_HDR;
    }
    
    /* first parse the header */
    dme_header_parse(buff, &msg->lm_hdr);
    
    /* then the lamport specific data */
    msg->tstamp_sec = ntohl(src->tstamp_sec);
    msg->tstamp_nsec = ntohl(src->tstamp_nsec);
    msg->type = ntohl(src->type);
    msg->pid = ntohq(src->pid);
    
}

/*
 * Informs the supervisor that something happened in this porcess's state.
 */
static int supervisor_send_inform_message(dme_ev_t ev) {
    sup_message_t msg = {};
    int err = 0;
    struct timespec tnow;
    uint32 elapsed_sec;
    uint32 elapsed_nsec;
    
    switch (ev) {
    case DME_EV_ENTERED_CRITICAL_REG:
    case DME_EV_EXITED_CRITICAL_REG:
        clock_gettime(CLOCK_REALTIME, &tnow);
        elapsed_sec = (uint32)(tnow.tv_sec - sup_tstamp.tv_sec);
        elapsed_nsec = (uint32)(tnow.tv_nsec - sup_tstamp.tv_nsec);
        
        /* construct and send the message */
        sup_msg_set(&msg, ev, elapsed_sec, elapsed_nsec, 0);
        err = dme_send_msg(SUPERVISOR_PID, (uint8*)&msg, SUPERVISOR_MESSAGE_LENGTH);
        
        /* set new sup_tstamp to tnow */
        sup_tstamp.tv_sec = tnow.tv_sec;
        sup_tstamp.tv_nsec = tnow.tv_nsec;
        break;

    default:
        /* No need to send informs to the supervisor in other cases */
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
    int ix;
    const buff_t * buff = (buff_t *)cookie; 
    
    if (!buff) {
        dbg_err("Message is empty!");
        return ERR_RECV_MSG;
    }
    
    switch(fsm_state) {
    case PS_IDLE:
        /* record the time */
        clock_gettime(CLOCK_REALTIME, &sup_tstamp);

        deliver_event(DME_EV_WANT_CRITICAL_REG, NULL);

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
    lamport_message_t srcmsg = {};
    lamport_message_t dstmsg = {};
    request_queue_elem_t * req = NULL;
    int ret = 0;
    const buff_t * buff = (buff_t *)cookie;
    
    if (!buff) {
        dbg_err("Message is empty!");
        return ERR_RECV_MSG;
    }
    
    lamport_msg_parse(*buff, &srcmsg);
    
    switch(fsm_state) {
    case PS_IDLE:
    case PS_EXECUTING:
    case PS_PENDING:
        if (srcmsg.type == MTYPE_REQUEST) {
            /* Send back the REPLY message */
            lamport_msg_set(&dstmsg, MTYPE_REPLY);
            dme_send_msg(srcmsg.pid, (uint8*)&dstmsg, LAMPORT_MSG_LEN);
            
            /* insert the request in the request_queue */
            req = calloc(1, sizeof(request_queue_elem_t));
            req->sec_tstamp = srcmsg.tstamp_sec;
            req->nsec_tstamp = srcmsg.tstamp_nsec;
            req->pid = srcmsg.pid;
            request_queue_insert(req);
        } else
        if (srcmsg.type == MTYPE_RELEASE) {
            /* pop it from the request_queue */
            request_queue_pop();
            
            /* check if this process can run now */
            if (my_turn()) {
                deliver_event(DME_EV_ENTERED_CRITICAL_REG, NULL);
            }
        } else 
        /* We're waiting for replies from all other peers */
        if (fsm_state == PS_PENDING && srcmsg.type == MTYPE_REPLY) {
            replies[srcmsg.pid] = TRUE;
            /* check if this process can run now */
            if (my_turn()) {
                deliver_event(DME_EV_ENTERED_CRITICAL_REG, NULL);
            }
        } else {
            dbg_err("Protocol error: recieved a lamport type %d message while in state %d",
                    srcmsg.type, fsm_state);
            ret = ERR_RECV_MSG;
        }
        break;

    default:
        dbg_err("Fatal error: FSM state corrupted");
        ret = ERR_FATAL;
        break;
    }
    
    return ret;
}

/*
 * Course of action when requesting the CS.
 */
int process_ev_want_cr(void * cookie)
{
    lamport_message_t msg = {};
    request_queue_elem_t * req = NULL;
    int err = 0;
    
    dbg_msg("Entered DME_EV_WANT_CRITICAL_REG");
    
    if (fsm_state != PS_IDLE) {
        dbg_err("Fatal error: DME_EV_WANT_CRITICAL_REG occured while not in IDLE state.");
        return (err = ERR_FATAL);
    }
        
    
    /* Switch to the pending state and send informs to peers */
    fsm_state = PS_PENDING;
    
    /* Clear the table of REPLY messages from peers */
    memset(replies, FALSE, nodes_count * sizeof(bool_t));
    
    lamport_msg_set(&msg, MTYPE_REQUEST);
    err = dme_broadcast_msg((uint8*)&msg, LAMPORT_MSG_LEN);
    
    /* insert the request in the request_queue */
    req = calloc(1, sizeof(request_queue_elem_t));
    req->sec_tstamp = msg.tstamp_sec;
    req->nsec_tstamp = msg.tstamp_nsec;
    req->pid = msg.pid;
    request_queue_insert(req);

    return err;
}


/*
 * Course of action when entering the CS.
 */
int process_ev_entered_cr(void * cookie)
{
    dbg_msg("");
    int err = 0;
    
    dbg_msg("Entered DME_EV_ENTERED_CRITICAL_REG");
    
    if (fsm_state != PS_PENDING) {
        dbg_err("Fatal error: DME_EV_ENTERED_CRITICAL_REG occured while not in PENDING state.");
        return (err = ERR_FATAL);
    }
    
    /* Switch to the executing state and inform the supervisor */
    supervisor_send_inform_message(DME_EV_ENTERED_CRITICAL_REG);
    fsm_state = PS_EXECUTING;
    
    /* Sanity check... You can never be to sure. */
    err = critical_region_is_sane();
    
    return err;
}

/*
 * Course of action when leaving the CS.
 */
int process_ev_exited_cr(void * cookie)
{
    lamport_message_t msg = {};
    int err = 0;
    dbg_msg("");
    
    if (fsm_state != PS_EXECUTING) {
        dbg_err("Fatal error: DME_EV_EXITED_CRITICAL_REG occured while not in EXECUTING state.");
        return (err = ERR_FATAL);
    }
    
    /* inform the supervisor */
    supervisor_send_inform_message(DME_EV_EXITED_CRITICAL_REG);
    
    /* pop our request from the request queue and switch to the idle state*/
    request_queue_pop();
    fsm_state = PS_IDLE;
    
    /* inform all peers that we left the CS */
    lamport_msg_set(&msg, MTYPE_RELEASE);
    err = dme_broadcast_msg((uint8*)&msg, LAMPORT_MSG_LEN);
    
    return err;
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
    
    /* Create the reply status array */
    replies = calloc(nodes_count, sizeof(bool_t));
    
    
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
