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

#include "common/util.h"

/* Info to be included in adjacency matrix cells */
typedef struct link_info_s {
    uint64 link_speed;
    sockaddr src_addr;
    sockaddr dst_addr;
    int sock_fd;
} link_info_t;



#endif /* INIT_H_ */
