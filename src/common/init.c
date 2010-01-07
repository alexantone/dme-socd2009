/*
 * src/common/init.c
 *
 * Signal and interrupt handling. This is the home of the signaling queue system.
 * 
 *  Created on: Oct 30, 2009 
 *      Author: alex
 * -------------------------------------------------------------------------
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <common/init.h>
#include <common/net.h>


/* error handling for the main program */
extern int    err_code;
extern bool_t exit_request;

/* Forward declaration */
static int net_demux(void * cookie);

typedef struct dme_ev_reg_s {
    dme_ev_t           der_event;
    ev_handler_fnct_t *der_funcp;
} dme_ev_reg_t;

#define get_count(array) (sizeof(array) / sizeof(array[0]))

typedef struct sig_cookie_s {
    int         sc_evt;
    void       *sc_cookie;
} sig_cookie_t;

typedef struct sig_timer_cookie_s {
    int         stc_timer_idx;
    int         stc_evt;
    void       *stc_cookie;
} sig_timer_cookie_t;


static int null_func(void * cookie) {
    dbg_err("This event hasn't had a handler registered yet or it is invalid.\n"\
            "  ---- ABORTING program ! -----");
    return ERR_INIT;
}

static dme_ev_reg_t func_registry[] = {
    { DME_EV_PEER_MSG_IN, null_func },
    { DME_EV_SUP_MSG_IN, null_func },
    { DME_EV_WANT_CRITICAL_REG, null_func },
    { DME_EV_ENTERED_CRITICAL_REG, null_func },
    { DME_EV_EXITED_CRITICAL_REG, null_func },
    
    /* Supervisor events */
    { DME_SEV_MSG_IN, null_func },
    { DME_SEV_PERIODIC_WORK, null_func },

    /* Iternal events are registered statically */
    { DME_IEV_PACK_IN, net_demux },
    
    /* invalid events */
    { DME_EV_INVALID, null_func}
};
static func_registry_count = get_count(func_registry);

const char * evtostr (dme_ev_t event) {
	switch(event) {
	case DME_EV_PEER_MSG_IN: return "DME_EV_PEER_MSG_IN";
	case DME_EV_SUP_MSG_IN: return  "DME_EV_SUP_MSG_IN";
	case DME_EV_WANT_CRITICAL_REG: return "DME_EV_WANT_CRITICAL_REG";
	case DME_EV_ENTERED_CRITICAL_REG: return "DME_EV_ENTERED_CRITICAL_REG";
	case DME_EV_EXITED_CRITICAL_REG: return "DME_EV_EXITED_CRITICAL_REG";

	case DME_SEV_PERIODIC_WORK: return "DME_SEV_PERIODIC_WORK";

	case DME_IEV_PACK_IN: return "DME_IEV_PACK_IN";
	}
	return "DME_EV_INVALID";
}

#define SIGRT_NETWORK  (SIGRTMIN)
#define SIGRT_TIMEREXP (SIGRTMIN + 1)
#define SIGRT_DELIVER  (SIGRTMIN + 2)
static unsigned int tick_count = 0;

const char * sigrttostr (unsigned int signo) {
	if (signo == SIGRT_NETWORK) {
		return "SIGRT_NETWORK";
	} else if (signo == SIGRT_TIMEREXP) {
		return "SIGRT_TIMEREXP";
	} else if (signo == SIGRT_DELIVER) {
		return "SIGRT_DELIVER";
	}
	return "OTHER_SIGNAL";
}
/* 
 * Timers pool
 */
#define MAX_TIMERS      (64)

typedef enum timer_state {
    TIMER_UNUSED = 0,
    TIMER_DISARMED,
    TIMER_ARMED,
    TIMER_EXPIRED,
} timer_state_t;

static timer_t timers_pool[MAX_TIMERS] = {};
static timer_state_t timers_state[MAX_TIMERS] = {TIMER_UNUSED};

/*
 * Helper functions for events registry and timers pool.
 */

/*
 * Gets a pointer to a certain's event handler function.
 * It can also be used to modify these functions in the registry.
 */
static ev_handler_fnct_t ** get_registry_funcp (dme_ev_t event)
{   
    int ix = 0;
    
    while (ix < func_registry_count && func_registry[ix].der_event != event) {
        ix++;
    }
    
    if (ix < func_registry_count) {
        return &(func_registry[ix].der_funcp);
    }

    return &(func_registry[DME_EV_INVALID].der_funcp);
}

/*
 * Returns the index of the first free timer in the pool or -1 otherwise. 
 */
static int get_free_timer(void) {
    static int last_timer = 0;
    int ix;
    
    ix = last_timer;
    do {
        if (timers_state[ix] == TIMER_UNUSED) {
            last_timer = ix;
            return last_timer;
        }
        
        /* wrap ix around MAX_TIMERS */
        ix = (++ix < MAX_TIMERS) ? ix : 0;
    } while (ix != last_timer);
    
    /* We got to last_timer, meaning that no free slot was found */
    return -1;
}

/*
 * Signal handling functions ( ... sending the right signals :) )
 */

/*
 * Event delivery handler. This function dispaches the functions in func_registry.
 */
static void queued_event_handler(int sig, siginfo_t *siginfo, void * context)
{
	dbg_msg();
    int err;
    /* This function should only be registered to SIGRT_DELIVER */
    if (siginfo->si_signo != SIGRT_DELIVER) {
        return;
    }
    
    /* save data from inside the container */
    sig_cookie_t * sc = siginfo->si_ptr;
    int   evt    = sc->sc_evt;
    void *cookie = sc->sc_cookie;
    
    /* Discard the container after recieving the data */
    safe_free(sc);
    
    /* Call the function registered to the sc->sc_evt event */
    dbg_msg("Handling event %s (%d)", evtostr(evt), evt);
    err = (*get_registry_funcp(evt))(cookie);
    
    /* If there was a fata error terminate the program */
    if (err >= ERR_FATAL) {
        err_code = err;
        exit_request = TRUE;
    }
}


/*
 * SIGRT_TIMEREXP handler
 */
static void timer_expire_handler(int sig, siginfo_t *siginfo, void * context)
{
    dbg_msg("");
    /* This function should only be registerd to SIGRT_TIMEREXP */
    if (siginfo->si_signo != SIGRT_TIMEREXP) {
        return;
    }
    
    sig_timer_cookie_t * stc = siginfo->si_ptr;
    dbg_msg("timer_idx=%d", stc->stc_timer_idx);
    timers_state[stc->stc_timer_idx] = TIMER_UNUSED;
    timer_delete(timers_pool[stc->stc_timer_idx]);
    deliver_event(stc->stc_evt, stc->stc_cookie);
    safe_free(stc);
    
    return;
}

static void networkio_handler (int sig, siginfo_t *siginfo, void * context)
{
    dbg_msg("");
    /* Just queue a DME_IEV_PACK_IN event */
    deliver_event(DME_IEV_PACK_IN, NULL);
}

/*
 * net_demux()
 * 
 * Checks the source(magic) of the message (peer/supervisor) and calls the
 * DME_IEV_PACK_IN registered processing routine.
 * The cookie is ignored.
 */
static int net_demux(void * cookie)
{
    dbg_msg("");
    int err = 0;
    uint32 magic;
    buff_t buff = {NULL, 0};
    
    /* get the contents of the message */
    if (0 != (err = dme_recv_msg(&buff.data, &buff.len))) {
        return err;
    }

    /* check the magic of the mesage */
    magic = ntohl(*(uint32 *)buff.data);
    
    if (magic == SUP_MSG_MAGIC) {
        err = (*get_registry_funcp(DME_EV_SUP_MSG_IN))(&buff);
    } else if (magic == DME_MSG_MAGIC) {
        err = (*get_registry_funcp(DME_EV_PEER_MSG_IN))(&buff);
    } else {
        dbg_err("Recieved possibly malformed messge:"\
                " MAGIC=0X%08X . Ignoring packet.", magic);
        err = ERR_BAD_MAGIC;
    }
    
    /* Free the allocated buffer data (if any) */ 
    safe_free(buff.data);
    
    /* If there was a fatal error terminate the program */
    if (err >= ERR_FATAL) {
        err_code = err;
        exit_request = TRUE;
    }

    return err;
}




/* 
 * Events functions
 */

/*
 * Registers a function to handle a certain event
 */
void
register_event_handler (dme_ev_t event, ev_handler_fnct_t func)
{
    if (event >= DME_EV_INVALID || event < 0) {
        dbg_err("Invalid event!");
    } else if(event >= DME_INTERNAL_EV_START) {
        dbg_err("Can not register handler for internal events!");
    } else {
        dbg_msg("get_reg_fp=%-10p *get_reg_fp=%-10p func=%-10p *func=%-10p",
                get_registry_funcp(event), *get_registry_funcp(event),
                func, *func);
        
        *(get_registry_funcp(event)) = func;
        
        dbg_msg("get_reg_fp=%-10p *get_reg_fp=%-10p func=%-10p *func=%-10p",
                get_registry_funcp(event), *get_registry_funcp(event),
                func, *func);
    }
}

/*
 * Delivers (queues) an event.
 */
int
deliver_event (dme_ev_t event, void * cookie)
{
    dbg_msg("++ Queuing event %s (%d), cookie@%p", evtostr(event), event, cookie);
    /*
     * sigqueue the stuff
     */
    int res = 0;
    
    /* create container to transport the event and cookie */
    sig_cookie_t * psc = malloc(sizeof(sig_cookie_t));
    psc->sc_evt    = event;
    psc->sc_cookie = cookie;
    
    res = sigqueue(getpid(), SIGRT_DELIVER, (sigval_t)(void *)psc);
    
    return res;
}

/*
 * Handles immediately an event
 */
int
handle_event(dme_ev_t event, void * cookie) {
	dbg_msg();
    int err;

    /* Call the function registered to the sc->sc_evt event */
    dbg_msg("Handling event %s (%d)", evtostr(event), event);
    err = (*get_registry_funcp(event))(cookie);

    /* If there was a fata error terminate the program */
    if (err >= ERR_FATAL) {
        err_code = err;
        exit_request = TRUE;
    }
}

/*
 * Deliver an event after tdelta.
 */
int
schedule_event (dme_ev_t event, uint32 secs, uint32 nsecs,void * cookie) {
    dbg_msg("Schedule event=%s(%d) in %u.%u with cookie@0x%p",
    		evtostr(event), event, secs, nsecs, cookie);
    int res = 0;
    sigevent_t timer_expire_ev = {};
    timer_t * tp = NULL;
    int16 tidx = -1;
    struct itimerspec tspec = {};
    sig_timer_cookie_t * pstc = NULL;
    
    if ((tidx = get_free_timer()) >= 0) {
        /* create container to transport the timer_idx, event and cookie */
        sig_timer_cookie_t * pstc = malloc(sizeof(sig_timer_cookie_t));
        pstc->stc_timer_idx = tidx;
        pstc->stc_evt    = event;
        pstc->stc_cookie = cookie;
        
        timer_expire_ev.sigev_notify = SIGEV_SIGNAL;
        timer_expire_ev.sigev_signo = SIGRT_TIMEREXP;
        timer_expire_ev.sigev_value.sival_ptr = pstc;
        
        tp = &timers_pool[tidx];
        timer_create(CLOCK_MONOTONIC, &timer_expire_ev, tp);
        tspec.it_value.tv_sec = secs;
        tspec.it_value.tv_nsec = nsecs;
        timer_settime(*tp, 0, &tspec, NULL);
        timers_state[tidx] = TIMER_ARMED;
    } else {
        res = 1;
    }
    
    dbg_msg("INFO: %s", res ? "Error scheduling event" : "Event scheduled");
    return res;
}

/*
 * Wait for events (mapped on SIGRTMIN).
 * This should be used in a loop.
 */
void wait_events(void)
{	
	siginfo_t sinfo;
	sigset_t waitset;
	int signo;
	sigemptyset(&waitset);

	sigprocmask(SIG_BLOCK, &waitset, NULL);

    sigaddset(&waitset, SIGRT_DELIVER);
    sigaddset(&waitset, SIGRT_TIMEREXP);
    sigaddset(&waitset, SIGRT_NETWORK);

    /* Forced exit (^Z) */
    sigaddset(&waitset, SIGTSTP);
	
    while(!exit_request) {
        signo = sigwaitinfo(&waitset, &sinfo);
        dbg_msg("-----------------------------------------------------------");
        dbg_msg("TICK = %-4d : signal %s (%d) occured ",
        		tick_count++, sigrttostr(signo), signo);
        if (signo == SIGRT_DELIVER) {
        	queued_event_handler(signo, &sinfo, NULL);
        } else if (signo == SIGRT_NETWORK) {
        	networkio_handler(signo, &sinfo, NULL);
        } else if (signo == SIGRT_TIMEREXP) {
        	timer_expire_handler(signo, &sinfo, NULL);
        } else if (signo == SIGTSTP){
        	dbg_msg("Forced exit!");
        	exit_request = TRUE;
        } else {
        	/* Ignore */
        }
    };
    dbg_msg("Exit requested >>>>>>>>>>>>>> EXIT stage right!");
}

/*
 * events module initialization.
 */
static sigset_t SIGRT_DELIVER_block_set;
static sigset_t SIGRT_TIMEREXP_block_set;
static sigset_t SIGRT_NETWORK_block_set;

/* 
 * Does initial signal handling.
 */
int
init_handlers (int sock)
{
    int res = 0;
    
    /* init signal masks (block SIGRT_DELIVER but allow others) */
    sigemptyset(&SIGRT_DELIVER_block_set);
    sigaddset(&SIGRT_DELIVER_block_set, SIGRT_DELIVER);
    
    /* Sequentialize timer expirations */
    sigemptyset(&SIGRT_TIMEREXP_block_set);
    sigaddset(&SIGRT_TIMEREXP_block_set, SIGRT_DELIVER);
    sigaddset(&SIGRT_TIMEREXP_block_set, SIGRT_TIMEREXP);
    sigaddset(&SIGRT_TIMEREXP_block_set, SIGRT_NETWORK);


    /* Sequentialize network I/O */
    sigemptyset(&SIGRT_NETWORK_block_set);
    sigaddset(&SIGRT_NETWORK_block_set, SIGRT_DELIVER);
    sigaddset(&SIGRT_NETWORK_block_set, SIGRT_TIMEREXP);
    sigaddset(&SIGRT_NETWORK_block_set, SIGRT_NETWORK);

    sigprocmask(SIG_BLOCK, &SIGRT_DELIVER_block_set, NULL);
    sigprocmask(SIG_BLOCK, &SIGRT_TIMEREXP_block_set, NULL);
    sigprocmask(SIG_BLOCK, &SIGRT_NETWORK_block_set, NULL);

    /* Register the SIGRT_DELIVER handler (used for event delivery) */
    struct sigaction deliver_ev_sa;
    deliver_ev_sa.sa_sigaction = queued_event_handler;
    deliver_ev_sa.sa_flags = SA_SIGINFO;
    deliver_ev_sa.sa_mask = SIGRT_DELIVER_block_set;
    
    if (res = sigaction(SIGRT_DELIVER, &deliver_ev_sa, NULL) < 0) {
        dbg_err("Cound not register handler for SIGRT_DELIVER!");
        goto out;
    }
    
    /* init timers */
    /* Register the SIGRT_TIMEREXP handler (used for timer expiration) */
    struct sigaction timer_sa;
    timer_sa.sa_sigaction = timer_expire_handler;
    timer_sa.sa_flags = SA_SIGINFO;
    timer_sa.sa_mask = SIGRT_TIMEREXP_block_set;
    
    if (res = sigaction(SIGRT_TIMEREXP, &timer_sa, NULL) < 0) {
        dbg_err("Cound not register handler for SIGRT_TIMEREXP!");
        goto out;
    }

    
    /* Register handler for Network IO */
    dbg_msg("The current socket is %d", sock);
    struct sigaction network_sa;
    network_sa.sa_sigaction = networkio_handler;
    network_sa.sa_flags = SA_SIGINFO;
    network_sa.sa_mask = SIGRT_NETWORK_block_set;
    
    if (res = sigaction(SIGRT_NETWORK, &network_sa, NULL) < 0) {
        dbg_err("Cound not register handler for SIGRT_NETWORK!");
        goto out;
    }

    if (res = fcntl(sock, F_SETOWN, getpid()) < 0) {
        dbg_err("Could not set ownership of socket!");
        goto out;
    }
    
    if (res = fcntl(sock, F_SETSIG, SIGRT_NETWORK) < 0) {
        dbg_err("Could not change signal!");
        goto out;
    }
    
    if (res = fcntl(sock, F_SETFL, O_NONBLOCK | O_ASYNC) < 0) {
        dbg_err("Could not set socket in async mode!");
        goto out;
    }
    
    
out:
    return res;
}

int
deinit_handlers(void) {
    /* deinit timers */
    return 0;
}
