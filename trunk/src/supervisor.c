/*
 * /socd/src/supervisor.c/supervisor.c
 * 
 *  Created on: Dec 3, 2009 
 *      Author: alex
 * -------------------------------------------------------------------------
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>


#include <common/defs.h>
#include <common/init.h>
#include <common/util.h>
#include <common/net.h>
#include <common/fsm.h>

/* 
 * global vars, defined in each app
 * Don't forget to declare them in each ".c" file
 * The supervisor always has proc_id = 0
 */
proc_id_t proc_id = 0;                  /* this process id */
link_info_t * nodes = NULL;             /* peer matrix */
size_t nodes_count = 0;

int err_code = 0;
bool_t exit_request = FALSE;

static char * fname = NULL;

static unsigned int election_interval = 10;     /* time in seconds to rerun election */
static unsigned int concurency_ratio = 50;      /* value in percent of total processes */
static unsigned int max_concurrent_proc = 0;    /* This will be computed in main() */

/* 
 * trigger_critical_region()
 * 
 * Trigger request of the critical region for a certain process.
 * Once entered the critical region, that process will stay there
 * for the specified ammount of time as sec and nanosec.
 */
static int trigger_critical_region (proc_id_t dest_pid,
                                    uint32 sec_delta, uint32 nsec_delta) {
    sup_message_t msg = {};
    uint8 * buff = (uint8 *) &msg;
    
    sup_msg_set(&msg, DME_EV_WANT_CRITICAL_REG, sec_delta, nsec_delta, 0);
    
    return dme_send_msg(dest_pid, buff, SUPERVISOR_MESSAGE_LENGTH);
}

/*
 * Returns a random pid.
 */
proc_id_t get_random_pid() {
    int ix = 0;
    
    /* get a value in 1 .. nodes_count */
    ix = 1 + random() % nodes_count;
    return nodes[ix].proc_id;
    
}

/* 
 * Event handler functions.
 * These functions must properly free the cookie revieved.
 */

int do_work(void * cookie) {
    int concurrent_count = random() % (max_concurrent_proc + 1);
    proc_id_t pid_arr[concurrent_count];
    proc_id_t tpid;
    bool_t found;
    int ix;
    int jx;
    
    /* If the critical region is free, elect processes to compete for it */
    if (critical_region_is_idlle() && concurrent_count > 0) {
        /* build the list of competing processes */
        ix = 0;
        while (ix < concurrent_count) {
            tpid = get_random_pid();
            
            /* Avoid duplicates */
            found = FALSE;
            for(jx = 0; jx < ix && !found; jx++) {
                if (pid_arr[jx] == tpid) {
                    found = TRUE;
                }
            }
            
            /* 
             * This process id was not elected before. Add it to te list.
             * Else replay the loop.
             */
            if (!found) {
                pid_arr[ix] = tpid;
                ix++;
            }
        }
        
        /* Trigger the elected processes to compete for the critical region */
        for (ix = 0; ix < concurrent_count; ix++) {
            dbg_msg("elected pid[%d] = %llu ", ix, pid_arr[ix]);
        }

        for (ix = 0; ix < concurrent_count; ix++) {
            trigger_critical_region(pid_arr[ix],5,0);
        }
    }
    
    
    /* reschedule this process */
    schedule_event(DME_SEV_PERIODIC_WORK, election_interval, 0, NULL);
}

/* Process incomming messages */
int process_messages(void * cookie)
{
    buff_t buff = {NULL, 0};
    /* TODO: */
    
    return 0;
}

int main(int argc, char *argv[])
{
    FILE *fh;
    int res = 0;
    
    if (0 != (res = parse_sup_params(argc, argv, &fname,
                                     &concurency_ratio, &election_interval))) {
        dbg_err("parse_args() returned nonzero status:%d", res);
        goto end;
    }

    /*
     * Parse the file fname
     */
    if (0 != (res = parse_file(fname, proc_id, &nodes, &nodes_count))) {
        dbg_err("parse_file() returned nonzero status:%d", res);
        goto end;
    }
    dbg_msg("nodes has %d elements", nodes_count);

    /* compute the number of maximum concurrent processes (nearest integer) */
    max_concurrent_proc = (nodes_count * concurency_ratio + 50) / 100;
    if (max_concurrent_proc < 2) {
        dbg_err("concurency ratio set too low. At least 2 processes must be concurent.");
        goto end;
    } else {
        dbg_msg("max_concurrent_proc=%d", max_concurrent_proc);
    }
    
    /*
     * Init connections (open listenning socket)
     */
    if (0 != (res = open_listen_socket(proc_id, nodes, nodes_count))) {
        dbg_err("open_listen_socket() returned nonzero status:%d", res);
        goto end;
    }

    /*
     * Register signals (for I/O, alarms, etc.)
     */
    if (0 != (res = init_handlers(nodes[proc_id].sock_fd))) {
        dbg_err("init_handlers() returned nonzero status");
        goto end;
    }
    
    register_event_handler(DME_SEV_PERIODIC_WORK, do_work);
    register_event_handler(DME_SEV_MSG_IN, process_messages);

    /* wait for peers processes to init, then kick start the supervisor */
    sleep(5);
    
    deliver_event(DME_SEV_PERIODIC_WORK, NULL);

    /*
     * Main loop: just sit here and wait for interrups.
     * All work is done in interrupt handlers mapped to registered functions.
     */
    wait_events();
    
end:
    /*
     * Do cleanup (dealocating dynamic strucutres)
     */
    deinit_handlers();

    /* Close our listening socket */
    if (nodes && nodes[proc_id].sock_fd > 0) {
        close(nodes[proc_id].sock_fd);
    }
    
    safe_free(nodes);
    
    return res;
}