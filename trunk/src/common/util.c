/*
 * /socd/src/common/util.c/util.c
 * 
 *  Created on: Oct 23, 2009 
 *      Author: alex
 * 
 */

#include "common/util.h"

uint64 msg_delay_usec(uint64 link_speed, size_t msg_length)
{
    return (msg_length * 1000000 / link_speed);
}
