/*
 * /socd/src/common/net.c/net.c
 * 
 *  Created on: Nov 14, 2009 
 *      Author: alex
 * -------------------------------------------------------------------------
 */



#include <sys/socket.h>
#include <common/net.h>

/* Global variables from main process */
extern const link_info_t * const nodes;
extern const size_t nodes_count;
extern const proc_id_t proc_id;

/*
 * Send the buffer to node with process_id dest.
 */
int dme_send_msg(proc_id_t dest, uint8 * buff, size_t len)
{
    dbg_msg("(proc_id_t dest = %llu, uint8 * buff , size_t len = %d)", dest, len);
    int maxcount = nodes_count;
    struct sockaddr * dest_addr = NULL;
    
    if (dest < 0 || dest > maxcount) {
        dbg_err("Destination process id is out of bounds: %llu not in [0..%d]",
                dest, maxcount);
        return ERR_SEND_MSG;
    }
    
    dest_addr = (struct sockaddr *)&nodes[dest].listen_addr;
    
    sendto(nodes[proc_id].sock_fd, buff, len, 0, dest_addr, sizeof(*dest_addr));
    return 0;
}

/*
 * Send a message to all the other nodes (except self):
 * {1, .., nodes_count} \ { proc_id }
 */
int dme_broadcast_msg (uint8 * buff, size_t len) {
    int ix = 0;
    int ret = 0;
    
    for (ix = 1; ix < proc_id && !ret; ix++) {
        ret |= dme_send_msg(ix, buff, len);
    }
    for (ix = proc_id + 1; ix <= nodes_count && !ret; ix++) {
        ret |= dme_send_msg(ix, buff, len);
    }
    
    return ret;
}


/* 
 * Recieve a message and return an allocated buffer and length.
 * The buffer MUST BE DEALLOCATED in the calling function!
 */

#define MAX_PACK_LEN    (1024) /* To avoid fragmentation -> UDP fails */
static uint8 test_buff[MAX_PACK_LEN];

int dme_recv_msg(uint8 ** out_buff, size_t * out_len)
{
    int len = 0;
    *out_len = 0; /* initialize to 0 just to avoid reading an empty buffer */
    
    /* Determine the length of the packet first */
    len = recv(nodes[proc_id].sock_fd, test_buff, MAX_PACK_LEN, MSG_PEEK);
    
    if (len <= 0 || !(*out_buff = malloc(len))) {
        dbg_err("Could not allocate buffer of length %d", len);
        return ERR_RECV_MSG;
    }
    
    /* Recieve the real data */
    if (len != recv(nodes[proc_id].sock_fd, *out_buff, len, 0)) {
        dbg_err("The expected packet length has changed! How did this happen??");
        safe_free(*out_buff);
        return ERR_RECV_MSG;
    }
    
    /* Now it's safe to report the retrieved buffer length */
    *out_len = len;
    return 0;
}


