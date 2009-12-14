/*
 * /socd/src/common/fsm.h/fsm.h
 * 
 *  Created on: Dec 13, 2009 
 *      Author: alex
 * -------------------------------------------------------------------------
 */

#ifndef FSM_H_
#define FSM_H_

#include <common/defs.h>

extern bool_t critical_region_is_idlle(void);
extern bool_t critical_region_is_free(void);

extern bool_t critical_region_is_sane(void);

extern int critical_region_pending_get_count(void);

#endif /* FSM_H_ */
