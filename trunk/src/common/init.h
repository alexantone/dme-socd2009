/*
 * /socd/src/common/init.h/init.h
 * 
 *  Created on: Oct 22, 2009 
 *      Author: alex
 * -------------------------------------------------------------------------
 */
#ifndef INIT_H_
#define INIT_H_

#include <netinet/in.h>
#include <sys/socket.h>

#include <common/defs.h>


typedef enum dme_evt_e {
    DME_EV_MSG_IN,
    DME_EV_WANT_CRITICAL_REG,
    DME_EV_ENTERED_CRITICAL_REG,
    DME_EV_EXITED_CRITICAL_REG,

    /* These events are used by the supervisor */
#define DME_SEV_MSG_IN (DME_EV_MSG_IN)
    DME_SEV_PERIODIC_WORK,

    /* This means the event id is invalid */
    DME_EV_INVALID,
} dme_ev_t;

typedef int (ev_handler_fnct_t)(void * cookie);

extern int  init_handlers(int sock);
extern int  deinit_handlers(void);
extern void register_event_handler(dme_ev_t event, ev_handler_fnct_t func);

extern int  deliver_event(dme_ev_t event, void * cookie);
extern int  schedule_event (dme_ev_t event, uint32 secs, uint32 nsecs,void * cookie);

void wait_events(void);


#endif /* INIT_H_ */
