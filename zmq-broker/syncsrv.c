/* syncsrv.c - generate scheduling trigger */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <ctype.h>
#include <zmq.h>
#include <czmq.h>
#include <json/json.h>

#include "zmq.h"
#include "cmbd.h"
#include "log.h"
#include "plugin.h"

#include "syncsrv.h"

static void _timeout (plugin_ctx_t *p)
{
    cmb_msg_send (p->zs_out_event, NULL, "event.sched.trigger");
}

static void _init (plugin_ctx_t *p)
{
    p->timeout = p->conf->syncperiod_msec;
}

struct plugin_struct syncsrv = {
    .name      = "sync",
    .initFn    = _init,
    .timeoutFn = _timeout,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */