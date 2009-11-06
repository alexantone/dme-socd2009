/*
 * /socd/src/common/defs.h/defs.h
 * 
 *  Created on: Nov 6, 2009 
 *      Author: alex
 * -------------------------------------------------------------------------
 */

#ifndef DEFS_H_
#define DEFS_H_

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

#define ERR_BADARGS     1


/* Info to be included in adjacency matrix cells */
typedef struct link_info_s {
    uint64 link_speed;
    struct sockaddr listen_addr;
    int sock_fd;
    uint64 proc_id
} link_info_t;

/* Data typedefs */
typedef int8_t  int8
typedef int16_t int16
typedef int32_t int32
typedef int64_t int64

typedef u_int8_t  uint8
typedef u_int16_t uint16
typedef u_int32_t uint32
typedef u_int64_t uint64


#define TRUE    (1)
#define FALSE   (0)
typedef u_int8_t  bool_t


#endif /* DEFS_H_ */
