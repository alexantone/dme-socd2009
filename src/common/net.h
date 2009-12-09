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

/* hton() & ntoh() for 64 bit values */
static inline uint64 htonq(uint64 q) {
    return ((htonl(q >> 32)) | ((uint64)htonl((uint32) q) << 32));
}
#define ntohq(q) htonq(q)


extern int dme_send_msg(proc_id_t dest, uint8 * buff, size_t len);
extern int dme_recv_msg(uint8 ** out_buff, size_t * out_len);

extern int dme_broadcast_msg(uint8 * buff, size_t len);



/* The DME message format */
/*
 *   0                8                 16                24                32 
 *   +- - - - - - - - + - - - - - - - - + - - - - - - - - + - - - - - - - - +
 * 0 |                           Packet MAGIC                               |
 *   |----------------------------------------------------------------------|
 * 1 |                             Process ID                               |
 * 2 |                              (64 bits)                               |
 *   |----------------------------------------------------------------------|
 * 3 |         Message Type             |            Flags                  |
 *   |----------------------------------------------------------------------|
 * 4 |         Length                   | xxxxxxxxxxx Reserved xxxxxxxxxxx  |
 *   |----------------------------------------------------------------------|
 * 5 |                                                                      |
 * . |                                ....                                  |
 * . |                      DATA  (aligned to 4B)                           |
 * . |                                ....                                  |
 *   |                                                                      |
 * 
 */

#define DME_MSG_MAGIC (0xDAAEAA59)  /* DMEMSG in 31137 speech :) (AA -> M) */
struct dme_message_s {
    uint32      dme_magic;
    uint64      process_id;
    uint16      msg_type;
    uint16      flags;
    uint16      length;
    uint8       data[0];
} PACKED;
typedef struct dme_message_s dme_message_t;

/* The superisor messages format */
/*
 *   0                8                 16                24                32 
 *   |- - - - - - - - + - - - - - - - - + - - - - - - - - + - - - - - - - - |
 * 0 |                           Packet MAGIC                               |
 *   |----------------------------------------------------------------------|
 * 1 |                             Process ID                               |
 * 2 |                              (64 bits)                               |
 *   |----------------------------------------------------------------------|
 * 3 |      Event  (Message Type)       |            Flags                  |
 *   |----------------------------------------------------------------------|
 * 4 |                            Time delta secs.                          |
 *   |----------------------------------------------------------------------|
 * 5 |                         Time delta micorsecs.                        |
 *   +----------------------------------------------------------------------+
 * 
 */

#define SUP_MSG_MAGIC (0x500FAA59)  /* SUPMSG (SOOFMSg) in 31137 speech :) */
struct sup_message_s {
    uint32      sup_magic;
    uint64      process_id;
    uint16      msg_type;
    uint32      sec_tdelta;
    uint32      usec_tdelta;
} PACKED;
typedef struct sup_message_s sup_message_t;

#define SUPERVISOR_MESSAGE_LENGTH (sizeof(struct sup_message_s))


#endif /* NET_H_ */
