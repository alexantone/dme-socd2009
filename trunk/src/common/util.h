/*
 * /socd/src/common/util.h/util.h
 * 
 *  Created on: Oct 23, 2009 
 *      Author: alex
 * 
 */

#ifndef UTIL_H_
#define UTIL_H_


#include <sys/types.h>


/* Data typedefs */
typedef int8_t  int8
typedef int16_t int16
typedef int32_t int32
typedef int64_t int64

typedef u_int8_t  uint8
typedef u_int16_t uint16
typedef u_int32_t uint32
typedef u_int64_t uint64

extern uint64 msg_delay_usec(uint64 link_speed, size_t msg_length);


#endif /* UTIL_H_ */
