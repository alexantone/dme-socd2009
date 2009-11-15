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
#include <netinet/in.h>


#define ERR_BADARGS     1
#define ERR_BADFILE     2
#define ERR_MALLOC      3
#define ERR_INIT        4

/* Data typedefs */
typedef int8_t  int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef u_int8_t  uint8;
typedef u_int16_t uint16;
typedef u_int32_t uint32;
typedef u_int64_t uint64;

typedef uint64 proc_id_t;

/* symbolic names to speeds */
#define KBITPS (1 << 10)
#define MBITPS (1 << 20)
#define GBITPS (1 << 30)

/* Info to be included in adjacency matrix cells */
typedef struct link_info_s {
    uint64 link_speeds[50];            /* Link speeds in bps to other nodes */
    struct sockaddr_in listen_addr;    /* Address on which current process listens */
    int sock_fd;                       /* The socket bound to the listen address */
    proc_id_t proc_id;                  /* Process ID */ 
} link_info_t;


/*
 * Debuging macros
 */
#define dbg_msg(format, args...) \
fprintf(stdout, "debug: %s:%d %s() -> " format "\n", __FILE__, __LINE__, __func__, ##args)

#define dbg_err(format, args...) \
fprintf(stderr, "error: %s:%d %s() -> " format "\n", __FILE__, __LINE__, __func__, ##args)


#define TRUE    (1)
#define FALSE   (0)
typedef u_int8_t  bool_t;


#endif /* DEFS_H_ */
