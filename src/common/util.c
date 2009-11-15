/*
 * /socd/src/common/util.c/util.c
 * 
 *  Created on: Oct 23, 2009 
 *      Author: alex
 * 
 */

#include <stdio.h>
#include <arpa/inet.h>
#include <common/util.h>
#include <unistd.h>

/*
 * getopt() defines and lib variables
 */
#define OPT_STRING "i:f:"

extern char *optarg;
extern int optind, opterr, optopt;


#define USAGE_MESSAGE \
"Usage:\n"\
"       <algorithm-name>  -i <process-id> -f <config-file>\n"

int parse_params(int argc, char ** argv,
                 proc_id_t *out_proc_id, char ** out_fname)
{
    char optchar = '\0';
    bool_t file_provided = FALSE;
    bool_t procid_provided = FALSE;
    
    if (!out_proc_id || !out_fname) {
        return 1;
    }

    while ((optchar = getopt(argc, argv, OPT_STRING)) != -1) {
        switch(optchar) {
        case 'i':
            *out_proc_id = strtoull(optarg, NULL, BASE_10);
            procid_provided = TRUE;
            break;

        case 'f':
            *out_fname = optarg;
            file_provided = TRUE;
            break;

        default:
            /* Print usage */
            fprintf(stdout, USAGE_MESSAGE);
            exit(ERR_BADARGS);
            break;
        }
    }
    
    if (!(procid_provided && file_provided)) {
            /* Print usage */
            fprintf(stdout, USAGE_MESSAGE);
            exit(ERR_BADARGS);
    }
    
    dbg_msg("proc_id=%llu file=%s", *out_proc_id, *out_fname);
    
    return 0;
}



#define TOK_DELIM " ;:\t\r\n"
static inline uint64 speed_mult(char prefix)
{
    switch(prefix) {
    case 'k':
    case 'K':
        return KBITPS;
    case 'm':
    case 'M':
        return MBITPS;
    case 'g':
    case 'G':
        return GBITPS;
    default:
        /* This ins not valid so we don't want to interpret it */
        return 1;
    }
    return 1;
}

int parse_file(const char * fname, proc_id_t p_id, link_info_t ** out_nodes)
{
    FILE *fh = NULL;
    int prc_count = 0;
    int ix = 0;
    int jx = 0;
    char linebuf[256];
    char *tok;
    char *mult;
    
    uint64 lnk_speed;
        
    link_info_t * cnode = NULL;
    
    
    if (NULL == (fh = fopen(fname, "r"))) {
        dbg_err("Could not open file %s", fname);
        return ERR_BADFILE;
    }
    
    fscanf(fh, "%d", &prc_count);
    dbg_msg("prc_count = %d", prc_count);
    fgets(linebuf, sizeof(linebuf), fh);
    
    dbg_msg("Trying to allocate nodes[%d] of size %d * %d",
            prc_count, prc_count, sizeof(link_info_t));
    if (!(*out_nodes = (link_info_t *)calloc(prc_count, sizeof(link_info_t)))) {
        dbg_err("Could not allocate the nodes matrix");
        fclose(fh);
        return ERR_MALLOC;
    }
    dbg_msg("Allocated nodes array nodes[%d]", sizeof(*out_nodes));
    
    ix = 0;
    while (ix < prc_count && fgets(linebuf, sizeof(linebuf), fh) != NULL) {
        dbg_msg("readbuf[%d] = %s", strlen(linebuf), linebuf);
        
        /*
         * The format of a line is:
         * <ip>:<port> linkspeed_1 linkspeed_2 ... linkspeed_prc_count
         * 
         * Line number determines the proc_id
         */
        
        cnode = (*out_nodes) + ix;
        dbg_msg("cnode: offset from start of array is %4d bytes", (void*)cnode - (void*)(*out_nodes));
        
        jx = 0;
        tok = strtok(linebuf, TOK_DELIM);
        
        /* Parse the IP */
        cnode->listen_addr.sin_family = AF_INET;
        inet_pton(AF_INET, tok, &cnode->listen_addr.sin_addr.s_addr);
        
        /* Parse the port */
        tok = strtok(NULL, TOK_DELIM);
        cnode->listen_addr.sin_port = htons(strtoul(tok, NULL, BASE_10));
        
        /* 
         * Only for this proc_id parse link speeds,
         * wich can have sufixes of K,M,G case insensitive
         */
        if (ix == p_id) {
            while (jx < prc_count && NULL != (tok = strtok(NULL, TOK_DELIM))) {
                /* No error checking done here */
                lnk_speed = strtoull(tok, &mult, BASE_10) * speed_mult(*mult);
                dbg_msg("\t\t found link speed to node %2d: %10s = %llu", jx, tok, lnk_speed);
                
                cnode->link_speeds[jx++] = lnk_speed;
            }
            
            if (jx < prc_count) {
                dbg_err("There were only %d/%d links specified", jx, prc_count);
                fclose(fh);
                return ERR_BADFILE;
            }
            
            dbg_msg("Found %d/%d links for process %d\n", jx, prc_count, ix);
        }
        
        ix++;
    }
    fclose(fh);
    
    if (ix < prc_count) {
        /*
         * The file terminated unexpectedly.
         */
        dbg_err("File %s has only %d of %d records", fname, ix, prc_count);
        return ERR_BADFILE;
    }
    
    return 0;
}



int open_listen_socket (proc_id_t p_id, link_info_t * const nodes)
{
    int res = 0;
    int max_nodes = sizeof(nodes) - 1;
    
    if (p_id < 0 || p_id > max_nodes) {
        dbg_err("process id out of bounds: %llu not int [0..%d]", p_id, max_nodes);
        res = -1;
        goto end;
    }
    
    if (1 > (nodes[p_id].sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))) {
        dbg_err("Could not alocate socket");
        res = -1;
        goto end;
    }
    
    dbg_msg("The socket is open on fd %d", nodes[p_id].sock_fd);
    
    if (res = bind(nodes[p_id].sock_fd,
                   &nodes[p_id].listen_addr, sizeof(nodes[p_id].listen_addr))) {
        dbg_err("Could not bind socket.");
        goto end;
    }
    
end:
    /* There was an error so close the socket if created */
    if (res && nodes[p_id].sock_fd > 0) {
        close(nodes[p_id].sock_fd);
    }
    
    return res;
}

uint64 get_msg_delay_usec(uint64 link_speed, size_t msg_length)
{
    return (msg_length * 1000000 / link_speed);
}
