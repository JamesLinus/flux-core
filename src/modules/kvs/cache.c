/*****************************************************************************\
 *  Copyright (c) 2015 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/time.h>
#include <czmq.h>
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libutil/blobref.h"
#include "src/common/libutil/tstat.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/iterators.h"

#include "waitqueue.h"
#include "cache.h"

struct cache_entry {
    waitqueue_t *waitlist_notdirty;
    waitqueue_t *waitlist_valid;
    void *data;             /* value object/data */
    int len;
    cache_data_type_t type; /* what does data point to */
    int lastuse_epoch;      /* time of last use for cache expiry */
    uint8_t dirty:1;
};

struct cache {
    zhash_t *zh;
};

struct cache_entry *cache_entry_create (void)
{
    struct cache_entry *hp = calloc (1, sizeof (*hp));
    if (!hp) {
        errno = ENOMEM;
        return NULL;
    }
    hp->type = CACHE_DATA_TYPE_NONE;
    return hp;
}

struct cache_entry *cache_entry_create_json (json_t *o)
{
    struct cache_entry *hp = cache_entry_create ();
    if (!hp)
        return NULL;
    if (o)
        hp->data = o;
    hp->type = CACHE_DATA_TYPE_JSON;
    return hp;
}

struct cache_entry *cache_entry_create_raw (void *data, int len)
{
    struct cache_entry *hp;

    if ((data && len <= 0) || (!data && len)) {
        errno = EINVAL;
        return NULL;
    }

    if (!(hp = cache_entry_create ()))
        return NULL;
    if (data) {
        hp->data = data;
        hp->len = len;
    }
    hp->type = CACHE_DATA_TYPE_RAW;
    return hp;
}

int cache_entry_type (struct cache_entry *hp, cache_data_type_t *t)
{
    if (hp) {
        if (t)
            (*t) = hp->type;
        return 0;
    }
    return -1;
}

bool cache_entry_is_type_json (struct cache_entry *hp)
{
    return (hp && hp->type == CACHE_DATA_TYPE_JSON);
}

bool cache_entry_is_type_raw (struct cache_entry *hp)
{
    return (hp && hp->type == CACHE_DATA_TYPE_RAW);
}

bool cache_entry_get_valid (struct cache_entry *hp)
{
    return (hp && hp->data != NULL);
}

bool cache_entry_get_dirty (struct cache_entry *hp)
{
    return (hp && hp->data && hp->dirty);
}

int cache_entry_set_dirty (struct cache_entry *hp, bool val)
{
    if (hp && hp->data) {
        if ((val && hp->dirty) || (!val && !hp->dirty))
            ; /* no-op */
        else if (val && !hp->dirty)
            hp->dirty = 1;
        else if (!val && hp->dirty) {
            hp->dirty = 0;
            if (hp->waitlist_notdirty) {
                if (wait_runqueue (hp->waitlist_notdirty) < 0) {
                    /* set back dirty bit to orig */
                    hp->dirty = 1;
                    return -1;
                }
            }
        }
        return 0;
    }
    return -1;
}

int cache_entry_clear_dirty (struct cache_entry *hp)
{
    if (hp && hp->data) {
        if (hp->dirty
            && (!hp->waitlist_notdirty
                || !wait_queue_length (hp->waitlist_notdirty)))
            hp->dirty = 0;
        return hp->dirty ? 1 : 0;
    }
    return -1;
}

int cache_entry_force_clear_dirty (struct cache_entry *hp)
{
    if (hp && hp->data) {
        if (hp->dirty) {
            if (hp->waitlist_notdirty) {
                wait_queue_destroy (hp->waitlist_notdirty);
                hp->waitlist_notdirty = NULL;
            }
            hp->dirty = 0;
        }
        return hp->dirty ? 1 : 0;
    }
    return -1;
}

json_t *cache_entry_get_json (struct cache_entry *hp)
{
    if (!hp || !hp->data || hp->type != CACHE_DATA_TYPE_JSON)
        return NULL;
    return hp->data;
}

int cache_entry_set_json (struct cache_entry *hp, json_t *o)
{
    if (hp
        && (hp->type == CACHE_DATA_TYPE_NONE
            || hp->type == CACHE_DATA_TYPE_JSON)) {
        if ((o && hp->data) || (!o && !hp->data)) {
            json_decref (o); /* no-op, 'o' is assumed identical to hp->data */
        } else if (o && !hp->data) {
            hp->data = o;
            if (hp->waitlist_valid) {
                if (wait_runqueue (hp->waitlist_valid) < 0) {
                    /* set back to orig */
                    hp->data = NULL;
                    return -1;
                }
            }
        } else if (!o && hp->data) {
            json_decref (hp->data);
            hp->data = NULL;
        }
        hp->type = CACHE_DATA_TYPE_JSON;
        return 0;
    }
    return -1;
}

void *cache_entry_get_raw (struct cache_entry *hp, int *len)
{
    if (!hp || !hp->data || hp->type != CACHE_DATA_TYPE_RAW)
        return NULL;
    if (len)
        (*len) = hp->len;
    return hp->data;
}

int cache_entry_set_raw (struct cache_entry *hp, void *data, int len)
{
    if ((data && len <= 0) || (!data && len)) {
        errno = EINVAL;
        return -1;
    }

    if (hp
        && (hp->type == CACHE_DATA_TYPE_NONE
            || hp->type == CACHE_DATA_TYPE_RAW)) {
        if ((data && hp->data) || (!data && !hp->data)) {
            free (data); /* no-op, 'data' is assumed identical to hp->data */
        } else if (data && !hp->data) {
            hp->data = data;
            hp->len = len;
            if (hp->waitlist_valid) {
                if (wait_runqueue (hp->waitlist_valid) < 0) {
                    /* set back to orig */
                    hp->data = NULL;
                    hp->len = 0;
                    return -1;
                }
            }
        } else if (!data && hp->data) {
            free (hp->data);
            hp->data = NULL;
            hp->len = 0;
        }
        hp->type = CACHE_DATA_TYPE_RAW;
        return 0;
    }
    return -1;
}

void cache_entry_destroy (void *arg)
{
    struct cache_entry *hp = arg;
    if (hp) {
        if (hp->data) {
            if (hp->type == CACHE_DATA_TYPE_JSON)
                json_decref (hp->data);
            else if (hp->type == CACHE_DATA_TYPE_RAW)
                free (hp->data);
        }
        if (hp->waitlist_notdirty)
            wait_queue_destroy (hp->waitlist_notdirty);
        if (hp->waitlist_valid)
            wait_queue_destroy (hp->waitlist_valid);
        free (hp);
    }
}

int cache_entry_wait_notdirty (struct cache_entry *hp, wait_t *wait)
{
    if (wait) {
        if (!hp->waitlist_notdirty) {
            if (!(hp->waitlist_notdirty = wait_queue_create ()))
                return -1;
        }
        if (wait_addqueue (hp->waitlist_notdirty, wait) < 0)
            return -1;
    }
    return 0;
}

int cache_entry_wait_valid (struct cache_entry *hp, wait_t *wait)
{
    if (wait) {
        if (!hp->waitlist_valid) {
            if (!(hp->waitlist_valid = wait_queue_create ()))
                return -1;
        }
        if (wait_addqueue (hp->waitlist_valid, wait) < 0)
            return -1;
    }
    return 0;
}

struct cache_entry *cache_lookup (struct cache *cache, const char *ref,
                                  int current_epoch)
{
    struct cache_entry *hp = zhash_lookup (cache->zh, ref);
    if (hp && current_epoch > hp->lastuse_epoch)
        hp->lastuse_epoch = current_epoch;
    return hp;
}

json_t *cache_lookup_and_get_json (struct cache *cache,
                                   const char *ref,
                                   int current_epoch)
{
    struct cache_entry *hp = cache_lookup (cache, ref, current_epoch);
    return cache_entry_get_valid (hp) ? cache_entry_get_json (hp) : NULL;
}

void cache_insert (struct cache *cache, const char *ref, struct cache_entry *hp)
{
    int rc = zhash_insert (cache->zh, ref, hp);
    assert (rc == 0);
    zhash_freefn (cache->zh, ref, cache_entry_destroy);
}

int cache_remove_entry (struct cache *cache, const char *ref)
{
    struct cache_entry *hp = zhash_lookup (cache->zh, ref);

    if (hp
        && !hp->dirty
        && (!hp->waitlist_notdirty
            || !wait_queue_length (hp->waitlist_notdirty))
        && (!hp->waitlist_valid
            || !wait_queue_length (hp->waitlist_valid))) {
        zhash_delete (cache->zh, ref);
        return 1;
    }
    return 0;
}

int cache_count_entries (struct cache *cache)
{
    return zhash_size (cache->zh);
}

static int cache_entry_age (struct cache_entry *hp, int current_epoch)
{
    if (!hp)
        return -1;
    if (hp->lastuse_epoch == 0)
        hp->lastuse_epoch = current_epoch;
    return current_epoch - hp->lastuse_epoch;
}

int cache_expire_entries (struct cache *cache, int current_epoch, int thresh)
{
    zlist_t *keys;
    char *ref;
    struct cache_entry *hp;
    int count = 0;

    if (!(keys = zhash_keys (cache->zh))) {
        errno = ENOMEM;
        return -1;
    }
    while ((ref = zlist_pop (keys))) {
        if ((hp = zhash_lookup (cache->zh, ref))
            && !cache_entry_get_dirty (hp)
            && cache_entry_get_valid (hp)
            && (thresh == 0 || cache_entry_age (hp, current_epoch) > thresh)) {
                zhash_delete (cache->zh, ref);
                count++;
        }
        free (ref);
    }
    zlist_destroy (&keys);
    return count;
}

int cache_get_stats (struct cache *cache, tstat_t *ts, int *sizep,
                     int *incompletep, int *dirtyp)
{
    zlist_t *keys = NULL;
    struct cache_entry *hp;
    char *ref;
    int size = 0;
    int incomplete = 0;
    int dirty = 0;
    int saved_errno;
    int rc = -1;

    if (!(keys = zhash_keys (cache->zh))) {
        saved_errno = ENOMEM;
        goto cleanup;
    }
    while ((ref = zlist_pop (keys))) {
        hp = zhash_lookup (cache->zh, ref);
        if (cache_entry_get_valid (hp)) {
            int obj_size = 0;

            if (hp->type == CACHE_DATA_TYPE_JSON) {
                /* must pass JSON_ENCODE_ANY, object could be anything */
                char *s = json_dumps (hp->data, JSON_ENCODE_ANY);
                if (!s) {
                    saved_errno = ENOMEM;
                    goto cleanup;
                }
                obj_size = strlen (s);
                free (s);
            }
            else if (hp->type == CACHE_DATA_TYPE_RAW)
                obj_size = hp->len;
            size += obj_size;
            tstat_push (ts, obj_size);
        } else
            incomplete++;
        if (cache_entry_get_dirty (hp))
            dirty++;
        free (ref);
    }
    if (sizep)
        *sizep = size;
    if (incompletep)
        *incompletep = incomplete;
    if (dirtyp)
        *dirtyp = dirty;
    rc = 0;
cleanup:
    zlist_destroy (&keys);
    if (rc < 0)
        errno = saved_errno;
    return rc;
}

int cache_wait_destroy_msg (struct cache *cache, wait_test_msg_f cb, void *arg)
{
    const char *key;
    struct cache_entry *hp;
    int n, count = 0;
    int rc = -1;

    FOREACH_ZHASH (cache->zh, key, hp) {
        if (hp->waitlist_valid) {
            if ((n = wait_destroy_msg (hp->waitlist_valid, cb, arg)) < 0)
                goto done;
            count += n;
        }
        if (hp->waitlist_notdirty) {
            if ((n = wait_destroy_msg (hp->waitlist_notdirty, cb, arg)) < 0)
                goto done;
            count += n;
        }
    }
    rc = count;
done:
    return rc;
}

struct cache *cache_create (void)
{
    struct cache *cache = calloc (1, sizeof (*cache));
    if (!cache) {
        errno = ENOMEM;
        return NULL;
    }
    if (!(cache->zh = zhash_new ())) {
        free (cache);
        errno = ENOMEM;
        return NULL;
    }
    return cache;
}

void cache_destroy (struct cache *cache)
{
    if (cache) {
        zhash_destroy (&cache->zh);
        free (cache);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
