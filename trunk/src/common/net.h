/*
 * /socd/src/common/net.h/net.h
 * 
 *  Created on: Nov 14, 2009 
 *      Author: alex
 * -------------------------------------------------------------------------
 */

#ifndef NET_H_
#define NET_H_

#include <common/defs.h>

extern int dme_send_msg(proc_id_t dest, uint8 * buff, size_t len);
extern int dme_recv_msg(uint8 ** out_buff, size_t * out_len);

extern int dme_broadcast_msg(uint8 * buff, size_t len);


#endif /* NET_H_ */
