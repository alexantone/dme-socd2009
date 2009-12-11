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

/* error handling for the main program */
extern int    err_code;
extern bool_t exit_request;

/* Timers pool */
#define MAX_TIMERS      (64)
typedef enum timer_state {
    TIMER_UNUSED = 0,
    TIMER_DISARMED,
    TIMER_ARMED,
    TIMER_EXPIRED,
} timer_state_t;

static timer_t * timers_pool[MAX_TIMERS] = {};
static timer_state_t timers_state[MAX_TIMERS] = {TIMER_UNUSED};


typedef struct dme_ev_reg_s {
    dme_ev_t           der_event;
    ev_handler_fnct_t *der_funcp;
} dme_ev_reg_t;

#define get_count(array) (sizeof(array) / sizeof(array[0]))

typedef struct sig_cookie_s {
    int         sc_evt;
    void       *sc_cookie;
} sig_cookie_t;


static int null_func(void * cookie) {
    dbg_err("How did we get here???");
    return 0;
}

static dme_ev_reg_t func_registry[] = {
    { DME_EV_MSG_IN, NULL },
    { DME_EV_WANT_CRITICAL_REG, NULL },
    { DME_EV_ENTERED_CRITICAL_REG, NULL },
    { DME_EV_EXITED_CRITICAL_REG, NULL },
    
    /* Supervisor events */
    { DME_SEV_MSG_IN, NULL },
    { DME_SEV_PERIODIC_WORK, NULL },

    /* invalid events */
    { DME_EV_INVALID, null_func}
};
static func_registry_count = get_count(func_registry);

    

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
 * Registers a function to handle a certain event
 */
void
register_event_handler (dme_ev_t event, ev_handler_fnct_t func)
{
    if (event >= DME_EV_INVALID || event < 0) {
        dbg_err("Invalid event!");
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
 * General sighandler. This function dispaches the functions in func_registry.
 */
static void sig_handler(int sig, siginfo_t *siginfo, void * context)
{
    int err;
    /* This fuinction should only be registerd to SIGRTMIN */
    if (siginfo->si_signo != SIGRTMIN) {
        return;
    }
    
    /* save data from inside the container */
    sig_cookie_t * sc = siginfo->si_value.sival_ptr;
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
 * Returns a pointer to the first free timer in the pool or NULL otherwise. 
 */
static timer_t* get_free_timer(void) {
    static uint16 last_timer = 0;
    uint16 ix;
    
    do {
        ix = last_timer;
        if (timers_state[ix] == TIMER_UNUSED) {
            last_timer = ix;
            return timers_pool[ix];
        }
        ix++;
    } while (ix != last_timer);
}

/*
 * Helper function
 */
static void timer_expire_handler(sigval_t sval)
{
    sig_cookie_t * sc = sval.sival_ptr;
    deliver_event(sc->sc_evt, sc->sc_cookie);
    return;
}

/*
 * Deliver an event after tdelta.
 */
int
schedule_event (dme_ev_t event, uint32 secs, uint32 nsecs,void * cookie) {
    int res = 0;
    sigevent_t timer_expire_ev = {};
    timer_t * tid = NULL;
    struct itimerspec tspec = {};
    
    /* create container to transport the event and cookie */
    sig_cookie_t * psc = malloc(sizeof(sig_cookie_t));
    psc->sc_evt    = event;
    psc->sc_cookie = cookie;
    
    timer_expire_ev.sigev_notify = SIGEV_THREAD;
    timer_expire_ev.sigev_notify_function = timer_expire_handler;
    timer_expire_ev.sigev_value.sival_ptr = psc;
    
    /* TODO:: Do timer_create and timer_set */
    if (NULL != (tid = get_free_timer())) {
        timer_create(CLOCK_MONOTONIC, &timer_expire_ev, tid);
        tspec.it_value.tv_sec = secs;
        tspec.it_value.tv_nsec = nsecs;
        timer_settime(*tid, 0, &tspec, NULL);
    }
    
    return res;
}


static void sigio_handler (int sig)
{
    /* Just queue a DME_EV_MSG_IN event */
    deliver_event(DME_EV_MSG_IN, NULL);
}

static sigset_t SIGRTMINblock_set;

/* 
 * Does initial signal handling.
 */
int
init_handlers (int sock)
{
    int res = 0;
    int ix = 0;
    
    
    sigemptyset(&SIGRTMINblock_set);
    sigaddset(&SIGRTMINblock_set, SIGRTMIN);

    /* Register the SIGRTMIN general handler function */
    struct sigaction sa;
    sa.sa_sigaction = sig_handler;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_mask = SIGRTMINblock_set;
    
    if (res = sigaction(SIGRTMIN, &sa, NULL) < 0) {
        dbg_err("Cound not register general handler for SIGRTMIN!");
        goto out;
    }
    
    /* init timers */
    for (ix = 0; ix < MAX_TIMERS; ix++) {
        timers_pool[ix] = malloc(sizeof(timer_t));
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
    int ix = 0;
    for (ix = 0; ix < MAX_TIMERS; ix++) {
        timers_pool[ix] = malloc(sizeof(timer_t));
    }
    
    return 0;
}

void wait_events(void)
{
    int signo;
    
    /* We seqentialize the SIGRTMIN signals */
    sigwait(&SIGRTMINblock_set, &signo);
    dbg_msg("Caught signal %d", signo);
}


