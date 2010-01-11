/*
 * src/singhal.c
 *
 * Created on: Ian 3, 2010
 *
 * -------------------------------------------------------------------------
 */

#include <stdio.h>
#include <unistd.h>
#include <time.h>


#include "common/defs.h"
#include "common/init.h"
#include "common/fsm.h"
#include "common/util.h"
#include "common/net.h"

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
static uint32 critical_region_simulated_duration = 0;

/*
 * Singhal specifics
 */
enum singhal_msg_types {
    MTYPE_REQUEST,
    MTYPE_REPLY,
};

bool_t Requesting; 	 /* true if the fsm_state is PS_PENDING */
bool_t Executing;    /* true if the fsm_state is PS_EXECUTING */
bool_t My_priority;  /* true if the pending request of peer i has priority over the current incoming request */

int * Ri, * Ri_val;   /* The requesting set*/
int * Ii;   		  /* The information set */

/*
 * Structure of the Singhal DME message
 */
struct singhal_message_s {
    dme_message_hdr_t lm_hdr;
    uint32            type;             /* REQUEST/REPLY */
    uint32            tstamp_sec;
    uint32            tstamp_nsec;
    proc_id_t         pid;              /* even though is redundant it's used to mirror the theory */
} PACKED;
typedef struct singhal_message_s singhal_message_t;

#define SINGHAL_MSG_LEN  (sizeof(singhal_message_t))
#define SINGHAL_DATA_LEN (SINGHAL_MSG_LEN - DME_MESSAGE_HEADER_LEN)

typedef struct request_queue_elem_s {
    uint32 sec_tstamp;
    uint32 nsec_tstamp;
    proc_id_t pid;
    struct request_queue_elem_s * next;
} request_queue_elem_t;

static request_queue_elem_t * request_queue = NULL;
static request_queue_elem_t * my_request;

/*
 * Helper functions.
 */


static bool_t void_Ri(void)
{
	int ix = 1;
	for (; ix <= nodes_count; ix++) {
		if (Ri[ix] == 1) {
			return FALSE;
		}
	}

	return TRUE;
}



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
    dbg_msg("QUEUE: current top pid is %llu@%p",
            request_queue ? request_queue->pid : -1, request_queue);
    if (!request_queue) {
        request_queue = elmt;
        request_queue->next = NULL;
        dbg_msg("QUEUE: new top pid is %llu@0x%p",
                request_queue ? request_queue->pid : -1, request_queue);
        return;
    }

    /* search for the right spot to insert this element */
    while (cx && req_elmt_cmp(elmt, cx) > 0 ) {
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
    dbg_msg("QUEUE: new top pid is %llu@0x%p",
            request_queue ? request_queue->pid : -1, request_queue);
}

static void request_queue_pop(void){
    request_queue_elem_t * px = request_queue;

    dbg_msg("QUEUE: current top pid is %llu@0x%p",
            request_queue ? request_queue->pid : -1, request_queue);

    if (request_queue) {
        request_queue = request_queue->next;
        safe_free(px);
    }
    dbg_msg("QUEUE: new top pid is %llu@0x%p",
            request_queue ? request_queue->pid : -1, request_queue);
}

/*
 * Checks if it's this processes turn to enter the CS
 */
static bool_t my_turn(void) {
    int ix;
    bool_t keep_going = TRUE;
    /* First check if all the replies arrived from the other peers */
    for (ix = 1; ix < proc_id && keep_going; ix++) {
    	if (Ri[ix] == 1)
    		keep_going = keep_going && (Ri_val[ix]);
    }

    if (!keep_going) {
    	return FALSE;
    }
    dbg_msg("Passed first part");

    for (ix = proc_id + 1; ix <= nodes_count && keep_going; ix++) {
    	if (Ri[ix] == 1)
    		keep_going = keep_going && (Ri_val[ix]);
    }

    if (!keep_going) {
    	return FALSE;
    }
    dbg_msg("Passed second part");

    return TRUE;
}

static void peer_msg_add_timestamp(singhal_message_t * msg) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    msg->tstamp_sec = htonl((uint32)ts.tv_sec);
    msg->tstamp_nsec = htonl((uint32)ts.tv_nsec);
}

/*
 * Prepare a singhal message for network sending.
 */
static int singhal_msg_set(singhal_message_t * const msg, unsigned int msgtype) {
    if (!msg) {
        return ERR_DME_HDR;
    }

    /* first set the header */
    dme_header_set(&msg->lm_hdr, MSGT_SINGHAL, SINGHAL_MSG_LEN, 0);

    /* then the singhal specific data */
    peer_msg_add_timestamp(msg);
    msg->type = htonl(msgtype);
    msg->pid = htonq(proc_id);

    return 0;
}

/*
 * Parse a received singhal message. The space must be already allocated in 'msg'.
 */
static int singhal_msg_parse(buff_t buff, singhal_message_t * msg) {
    singhal_message_t * src = (singhal_message_t *)buff.data;

    if (!msg || buff.data == NULL || buff.len < DME_MESSAGE_HEADER_LEN) {
        return ERR_DME_HDR;
    }

    /* first parse the header */
    dme_header_parse(buff, &msg->lm_hdr);

    /* then the singhal specific data */
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
 * Sends messages only to those sites specified in the Ri (Ri[pid] == 1) or Ii (Ii[pid] == 1)
 */

int singhal_set_msg_send (uint8 * buff, size_t len, int * Xi) {
    int ix = 0;
    int ret = 0;

    for (ix = 1; ix < proc_id && !ret; ix++) {
    	if (Xi[ix] == 1 )
    		ret |= dme_send_msg(ix, buff, len);
    }
    for (ix = proc_id + 1; ix <= nodes_count && !ret; ix++) {
    	if (Xi[ix] == 1 )
    		ret |= dme_send_msg(ix, buff, len);
    }

    return ret;
}



/*
 * Event handler functions.
 * These functions must properly free the cookie recieved, except for the
 * DME_EV_SUP_MSG_IN and DME_EV_PEER_MSG_IN.
 */

static int fsm_state = PS_IDLE;

static int handle_supervisor_msg(void * cookie) {
    dbg_msg("");
    int ret = 0;
    int ix;
    const buff_t * buff = (buff_t *)cookie;
    sup_message_t srcmsg = {};

    if (!buff) {
        dbg_err("Message is empty!");
        return ERR_RECV_MSG;
    }

    switch(fsm_state) {
    case PS_IDLE:
        /* record the time */
        clock_gettime(CLOCK_REALTIME, &sup_tstamp);
        sup_msg_parse(*buff, &srcmsg);
        critical_region_simulated_duration = srcmsg.sec_tdelta;

        ret = handle_event(DME_EV_WANT_CRITICAL_REG, NULL);
        break;

    /* The supervisor should not send a message in these states */
    case PS_PENDING:
    	/* eu */
    	dbg_msg("Ignoring message from supervisor (not in IDLE state).");
    	break; /**/
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
    singhal_message_t srcmsg = {};
    singhal_message_t dstmsg = {};
    request_queue_elem_t * req = NULL;
    int ret = 0;
    const buff_t * buff = (buff_t *)cookie;
    int ix;

    if (!buff) {
        dbg_err("Message is empty!");
        return ERR_RECV_MSG;
    }

    singhal_msg_parse(*buff, &srcmsg);

    switch(fsm_state) {
    case PS_IDLE:
    	if (srcmsg.type == MTYPE_REQUEST) {
	        dbg_msg("Recieved a REQUEST message");

	       /* Send back the REPLY message */
    	   singhal_msg_set(&dstmsg, MTYPE_REPLY);
    	   dme_send_msg(srcmsg.pid, (uint8*)&dstmsg, SINGHAL_MSG_LEN);

    	   Ri[srcmsg.pid] = 1;

    	} else {
            dbg_err("Protocol error: recieved a singhal REPLY (%d) message while in state PS_IDLE",
            		srcmsg.type);
            ret = ERR_RECV_MSG;
        }
    	break;

    case PS_EXECUTING:
    	Ii[srcmsg.pid] = 1;
    	break;

    case PS_PENDING:
        if (srcmsg.type == MTYPE_REQUEST) {
            dbg_msg("Recieved a REQUEST message from %llu", srcmsg.pid);

            /*comparing timestamps from the recieved message and my_request*/
            req = calloc(1, sizeof(request_queue_elem_t));
            req->sec_tstamp = srcmsg.tstamp_sec;
            req->nsec_tstamp = srcmsg.tstamp_nsec;
            req->pid = srcmsg.pid;


            if (my_request && req_elmt_cmp(req, my_request) > 0)
            	My_priority = TRUE;
            else
            	My_priority = FALSE;

            if (My_priority) {
            	Ii[srcmsg.pid] = 1;
            	ret = handle_event(DME_EV_ENTERED_CRITICAL_REG, NULL);
            } else {
            	/* Send back the REPLY message */
                singhal_msg_set(&dstmsg, MTYPE_REPLY);
                dme_send_msg(srcmsg.pid, (uint8*)&dstmsg, SINGHAL_MSG_LEN);

                /* Insert message source site into Ri if it's not in*/
                if (Ri[srcmsg.pid] == 0){
                	Ri[srcmsg.pid] = 1;
             	    }

            }
        } else if (srcmsg.type == MTYPE_REPLY) {
        /* We're waiting for replies from all other peers */
            dbg_msg("Recieved a REPLY message from %llu", srcmsg.pid);

            Ri_val[srcmsg.pid] = 1;

            for (ix = 1 ; ix <= nodes_count; ix++) {
                dbg_msg("Ri_val[%d] = %d" , ix, Ri_val[ix] ? 0 : 1);
            }
            /* check if this process can run now */
            if (my_turn()) {
                dbg_msg("My turn now!!!");
                ret = handle_event(DME_EV_ENTERED_CRITICAL_REG, NULL);
            }
        } else {
            dbg_err("Protocol error: recieved an unknown message type (%d) while in state PS_PENDING",
                    srcmsg.type);
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
    singhal_message_t dstmsg = {};
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
    //memset(Ri, 0, nodes_count * sizeof(bool_t));


    singhal_msg_set(&dstmsg, MTYPE_REQUEST);

    if (!void_Ri()) {
    	err = singhal_set_msg_send((uint8*)&dstmsg, SINGHAL_MSG_LEN, Ri);
    } else{
    	/* remember last request*/
    	my_request = calloc(1, sizeof(request_queue_elem_t));
    	my_request->sec_tstamp = ntohl(dstmsg.tstamp_sec);
    	my_request->nsec_tstamp = ntohl(dstmsg.tstamp_nsec);
    	my_request->pid = ntohq(dstmsg.pid);

    	err = handle_event(DME_EV_ENTERED_CRITICAL_REG, NULL);
    }

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

    /* Finish our simulated work after the ammount of time specified by the supervisor */
    schedule_event(DME_EV_EXITED_CRITICAL_REG,
                   critical_region_simulated_duration, 0, NULL);

    dbg_msg("Exitting");
    return err;
}

/*
 * Course of action when leaving the CS.
 */
int process_ev_exited_cr(void * cookie)
{
    singhal_message_t msg = {};
    int err = 0;
    dbg_msg("");
    int ix;

    if (fsm_state != PS_EXECUTING) {
        dbg_err("Fatal error: DME_EV_EXITED_CRITICAL_REG occured while not in EXECUTING state.");
        return (err = ERR_FATAL);
    }

    /* inform the supervisor */
    supervisor_send_inform_message(DME_EV_EXITED_CRITICAL_REG);
    fsm_state = PS_IDLE;

    /*	Empty Ri set */
    memset(Ri, 0,( nodes_count + 1) * sizeof(Ri[0]));
    memset(Ri_val, 0,( nodes_count + 1) * sizeof(Ri_val[0]));

    /* inform all peers from Ii that we left the CS */
    singhal_msg_set(&msg, MTYPE_REPLY);

    err = singhal_set_msg_send((uint8*)&msg, SINGHAL_MSG_LEN, Ii);

    for (ix = 1; ix <= nodes_count; ix++)
    	if ( Ii[ix] == 1 )
    	{
    		Ii[ix] = 0;
    		Ri[ix] = 1;
    	}

    return err;
}


int main(int argc, char *argv[])
{
    int res = 0;
    int ix;

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

    /* Create the request set */
    Ri = calloc(nodes_count + 1, sizeof(int));
    Ri_val = calloc(nodes_count + 1, sizeof(int));

    /* Create the information set */
    Ii = calloc(nodes_count + 1, sizeof(int));

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
     * Initializing Singhal specific variables
     *
     */
    for (ix = 1 ; ix < proc_id; ix++ ) {
    	Ri[ix] = 1;
    }

    Requesting = FALSE;
    Executing = FALSE;

    /*
     * Main loop: just sit here and wait for interrupts (triggered by the supervisor).
     * All work is done in interrupt handlers mapped to registered functions.
     */
    wait_events();

end:
    /*
     * Do cleanup (deallocating dynamic structures)
     */
    deinit_handlers();

    /* Close our listening socket */
    if (nodes && nodes[proc_id].sock_fd > 0) {
        close(nodes[proc_id].sock_fd);
    }

    safe_free(nodes);

    return res;
}
