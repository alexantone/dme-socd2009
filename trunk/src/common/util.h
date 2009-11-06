/*
 * /socd/src/common/util.h/util.h
 * 
 *  Created on: Oct 23, 2009 
 *      Author: alex
 * 
 */

#ifndef UTIL_H_
#define UTIL_H_

#include <string.h>

#include "common/defs.h"

#define BASE_10         10
#define BASE_16         16


/*
 * Debuging macros
 */
#define dbg_msg(format, args...) \
fprintf(stdout, "error: %s:&d %s() -> %s", __FILE__, __LINE__, __func__, format, ##args)

#define dbg_err(err_msg, args...) \
fprintf(stderr, "error: %s:&d %s() -> %s", __FILE__, __LINE__, __func__, format, ##args)




/*
 * Export functions in "util.c" to be available for other modules.
 */
extern int parse_params(int argc, char * argv[],
                  uint64 *out_proc_id, char ** out_fname);

extern uint64 get_msg_delay_usec(uint64 link_speed, size_t msg_length);

#endif /* UTIL_H_ */
