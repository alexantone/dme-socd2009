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
#include <fcntl.h>
#include <common/init.h>

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
    (*get_registry_funcp(evt))(cookie);
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
    
    
    sigemptyset(&SIGRTMINblock_set);
    sigaddset(&SIGRTMINblock_set, SIGRTMIN);

    /* Register the SIGRTMIN general handler finction */
    struct sigaction sa;
    sa.sa_sigaction = sig_handler;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_mask = SIGRTMINblock_set;
    
    if (res = sigaction(SIGRTMIN, &sa, NULL) < 0) {
        dbg_err("Cound not register general handler for SIGRTMIN!");
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


void wait_events(void)
{
    int signo;
    
    /* We seqentialize the SIGRTMIN signals */
    sigwait(&SIGRTMINblock_set, &signo);
    dbg_msg("Caught signal %d", signo);
}


