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
    int maxcount = nodes_count;
    struct sockaddr * dest_addr = NULL;
    
    if (dest < 0 || dest > maxcount) {
        dbg_err("Destination process id is out of bounds: %llu not in [0..%d]",
                dest, maxcount);
        return 1;
    }
    
    dest_addr = &nodes[dest].listen_addr;
    
    sendto(nodes[proc_id].sock_fd, buff, len, 0, dest_addr, sizeof(*dest_addr));
    
}

/*
 * Send a message to all the nodes (1..nodes_count)
 */
int dme_broadcast_msg (uint8 * buff, size_t len) {
    int ix = 0;
    for (ix = 1; ix <= nodes_count; ix++) {
        dme_send_msg(ix, buff, len);
    }
}


/*
 * The buff must be deallocated in the calling function!
 */

#define MAX_PACK_LEN    (1024) /* To avoid fragmentation -> UDP fails */
static uint8 test_buff[MAX_PACK_LEN];

int dme_recv_msg(uint8 ** out_buff, size_t * out_len)
{
    /* TODO: Source checking */
    int len = 0;
    *out_len = 0; /* initialize to 0 just to avoid reading an empty buffer */
    
    /* Determine the length of the packet first */
    len = recv(nodes[proc_id].sock_fd, test_buff, MAX_PACK_LEN, MSG_PEEK);
    
    if (!(*out_buff = malloc(len))) {
        dbg_err("Could not allocate buffer of length %d", len);
        return -1;
    }
    
    /* Recieve the real data */
    if (len != recv(nodes[proc_id].sock_fd, *out_buff, len, 0)) {
        dbg_err("The expected packet length has changed! How did this happen??");
        safe_free(*out_buff);
        return -1;
    }
    
    /* Now it's safe to report the revieved buffer length */
    *out_len = len;
    
}

