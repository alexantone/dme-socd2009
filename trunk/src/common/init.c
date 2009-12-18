/*
 * /socd/src/common/init.c/init.c
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
static void deliver_event_handler(int sig, siginfo_t *siginfo, void * context)
{
    int err;
    /* This fuinction should only be registerd to SIGRTMIN */
    if (siginfo->si_signo != SIGRTMIN) {
        return;
    }
    
    /* save data from inside the container */
    sig_cookie_t * sc = siginfo->si_ptr;
    int   evt    = sc->sc_evt;
    void *cookie = sc->sc_cookie;
    
    /* Discard the container after recieving the data */
    safe_free(sc);
    
    /* Call the function registered to the sc->sc_evt event */
    err = (*get_registry_funcp(evt))(cookie);
    
    /* If there was a fata error terminate the program */
    if (err >= ERR_FATAL) {
        err_code = err;
        exit_request = TRUE;
    }
}


/*
 * SIGUSR1 handler
 */
static void timer_expire_handler(int sig, siginfo_t *siginfo, void * context)
{
    dbg_msg("");
    /* This function should only be registerd to SIGUSR1 */
    if (siginfo->si_signo != SIGUSR1) {
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

static void sigio_handler (int sig)
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
    
    /* If there was a fata error terminate the program */
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
    dbg_msg("event = %d cookie@%p", event, cookie);
    /*
     * sigqueue the stuff
     */
    int res = 0;
    
    /* create container to transport the event and cookie */
    sig_cookie_t * psc = malloc(sizeof(sig_cookie_t));
    psc->sc_evt    = event;
    psc->sc_cookie = cookie;
    
    res = sigqueue(getpid(), SIGRTMIN, (sigval_t)(void *)psc);
    
    return res;
}


/*
 * Deliver an event after tdelta.
 */
int
schedule_event (dme_ev_t event, uint32 secs, uint32 nsecs,void * cookie) {
    dbg_msg("Schedule event=%d in %u.%u with cookie@%p", event, secs, nsecs, cookie);
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
        timer_expire_ev.sigev_signo = SIGUSR1;
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
    
    return res;
}

/*
 * Wait for events (mapped on SIGRTMIN).
 * This should be used in a loop.
 */
void wait_events(void)
{
    while(!exit_request) {
        pause();
        dbg_msg("Bump! Sig(nal)happened");
    }
    dbg_msg("Exit requested >>>>>>>>>>>>>> EXIT stage right!");
}

/*
 * events module initialisation.
 */
static sigset_t SIGRTMINblock_set;
static sigset_t SIGUSR1block_set;

/* 
 * Does initial signal handling.
 */
int
init_handlers (int sock)
{
    int res = 0;
    
    /* init signal masks (block SIGRTMIN but allow SIGUSR1) */
    sigemptyset(&SIGRTMINblock_set);
    sigaddset(&SIGRTMINblock_set, SIGRTMIN);
    
    sigemptyset(&SIGUSR1block_set);

    /* Register the SIGRTMIN handler (used for event delivery) */
    struct sigaction deliver_ev_sa;
    deliver_ev_sa.sa_sigaction = deliver_event_handler;
    deliver_ev_sa.sa_flags = SA_SIGINFO;
    //deliver_ev_sa.sa_mask = SIGRTMINblock_set;
    
    if (res = sigaction(SIGRTMIN, &deliver_ev_sa, NULL) < 0) {
        dbg_err("Cound not register handler for SIGRTMIN!");
        goto out;
    }
    
    /* init timers */
    /* Register the SIGUSR1 handler (used for timer expiration) */
    struct sigaction timer_sa;
    timer_sa.sa_sigaction = timer_expire_handler;
    timer_sa.sa_flags = SA_SIGINFO;
    timer_sa.sa_mask = SIGUSR1block_set;
    
    if (res = sigaction(SIGUSR1, &timer_sa, NULL) < 0) {
        dbg_err("Cound not register handler for SIGUSR1!");
        goto out;
    }

    
    /* Register handler for Network IO */
    dbg_msg("The current socket is %d", sock);
    signal(SIGRTMAX, sigio_handler);
    
    if (res = fcntl(sock, F_SETOWN, getpid()) < 0) {
        dbg_err("Could not set ownership of socket!");
        goto out;
    }
    
    if (res = fcntl(sock, F_SETSIG, SIGRTMAX) < 0) {
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
