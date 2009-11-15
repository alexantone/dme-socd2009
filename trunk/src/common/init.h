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

    /* This means the event id is invalid */
    DME_EV_INVALID,
} dme_ev_t;

typedef int (ev_handler_fnct_t)(void * cookie);

int  init_handlers(int sock);
void register_event_handler(dme_ev_t event, ev_handler_fnct_t func);
int  deliver_event(dme_ev_t event, void * cookie);
void wait_events(void);


#endif /* INIT_H_ */
