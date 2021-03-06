#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <jansson.h>
#include <string.h>
#include <errno.h>

#include "kvs.h"
#include "kvs_txn_private.h"
#include "treeobj.h"

#include "src/common/libtap/tap.h"

void jdiag (json_t *o)
{
    char *tmp = json_dumps (o, JSON_COMPACT);
    diag ("%s", tmp);
    free (tmp);
}

/* Decode a 'val' containing base64 encoded JSON.
 * Extract a number, and compare it to 'expected'.
 */
int check_int_value (json_t *dirent, int expected)
{
    char *data;
    int rc, len, i;
    json_t *val;

    if (treeobj_decode_val (dirent, (void **)&data, &len) < 0) {
        diag ("%s: initial base64 decode failed", __FUNCTION__);
        return -1;
    }
    if (len == 0 || data[len - 1] != '\0') {
        diag ("%s: data not null terminated", __FUNCTION__);
        free (data);
        return -1;
    }
    if (!(val = json_loads (data, JSON_DECODE_ANY, NULL))) {
        diag ("%s: couldn't decode JSON", __FUNCTION__);
        free (data);
        return -1;
    }
    rc = json_unpack (val, "i", &i);
    free (data);
    if (rc < 0) {
        diag ("%s: couldn't find requested JSON value", __FUNCTION__);
        json_decref (val);
        return -1;
    }
    json_decref (val);
    if (i != expected) {
        diag ("%s: expected %d received %d", __FUNCTION__, expected, i);
        return -1;
    }
    return 0;
}

/* Decode a 'val' containing base64 encoded JSON.
 * Extract a string, and compare it to 'expected'.
 */
int check_string_value (json_t *dirent, const char *expected)
{
    char *data;
    int rc, len;
    const char *s;
    json_t *val;

    if (treeobj_decode_val (dirent, (void **)&data, &len) < 0) {
        diag ("%s: initial base64 decode failed", __FUNCTION__);
        return -1;
    }
    if (len == 0 || data[len - 1] != '\0') {
        diag ("%s: data not null terminated", __FUNCTION__);
        free (data);
        return -1;
    }
    if (!(val = json_loads (data, JSON_DECODE_ANY, NULL))) {
        diag ("%s: couldn't decode JSON", __FUNCTION__);
        free (data);
        return -1;
    }
    rc = json_unpack (val, "s", &s);
    free (data);
    if (rc < 0) {
        diag ("%s: couldn't find requested JSON value", __FUNCTION__);
        json_decref (val);
        return -1;
    }
    if (strcmp (expected, s) != 0) {
        diag ("%s: expected %s received %s", __FUNCTION__, expected, s);
        json_decref (val);
        return -1;
    }
    json_decref (val);
    return 0;
}

void basic (void)
{
    flux_kvs_txn_t *txn;
    int rc;
    const char *key;
    json_t *entry, *dirent;

    /* Create a transaction
     */
    txn = flux_kvs_txn_create ();
    ok (txn != NULL,
        "flux_kvs_txn_create works");
    rc = flux_kvs_txn_pack (txn, 0, "foo.bar.baz",  "i", 42);
    ok (rc == 0,
        "1: flux_kvs_txn_pack(i) works");
    rc = flux_kvs_txn_pack (txn, 0, "foo.bar.bleep",  "s", "foo");
    ok (rc == 0,
        "2: flux_kvs_txn_pack(s) works");
    rc = flux_kvs_txn_unlink (txn, 0, "a");
    ok (rc == 0,
        "3: flux_kvs_txn_unlink works");
    rc = flux_kvs_txn_mkdir (txn, 0, "b.b.b");
    ok (rc == 0,
        "4: flux_kvs_txn_mkdir works");
    rc = flux_kvs_txn_symlink (txn, 0, "c.c.c", "b.b.b");
    ok (rc == 0,
        "5: flux_kvs_txn_symlink works");
    rc = flux_kvs_txn_put (txn, 0, "d.d.d", "43");
    ok (rc == 0,
        "6: flux_kvs_txn_put(i) works");
    rc = flux_kvs_txn_put (txn, 0, "e", NULL);
    ok (rc == 0,
        "7: flux_kvs_txn_put(NULL) works");
    errno = 0;
    rc = flux_kvs_txn_put (txn, 0, "f", "");
    ok (rc < 0 && errno == EINVAL,
        "error: flux_kvs_txn_put(empty string) fails with EINVAL");
    errno = 0;
    rc = flux_kvs_txn_pack (txn, 0xFFFF, "foo.bar.blorp",  "s", "foo");
    ok (rc < 0 && errno == EINVAL,
        "error: flux_kvs_txn_pack(bad flags) fails with EINVAL");
    errno = 0;
    rc = flux_kvs_txn_mkdir (txn, 0xFFFF, "b.b.b");
    ok (rc < 0 && errno == EINVAL,
        "error: flux_kvs_txn_mkdir works");
    errno = 0;
    rc = flux_kvs_txn_put (txn, 0xFFFF, "f", "42");
    ok (rc < 0 && errno == EINVAL,
        "error: flux_kvs_txn_put(bad flags) fails with EINVAL");

    /* Verify transaction contents
     */
    ok (txn_get (txn, TXN_GET_FIRST, &entry) == 0
        && entry != NULL,
        "1: retrieved");
    ok (json_unpack (entry, "{s:s s:o}", "key", &key,
                                         "dirent", &dirent) == 0,
        "1: unpacked operation");
    ok (!strcmp (key, "foo.bar.baz")
        && check_int_value (dirent, 42) == 0,
        "1: put foo.bar.baz = 42");

    ok (txn_get (txn, TXN_GET_NEXT, &entry) == 0,
        "2: retrieved");
    jdiag (entry);
    ok (json_unpack (entry, "{s:s s:o}", "key", &key,
                                         "dirent", &dirent) == 0,
        "2: unpacked operation");
    ok (!strcmp (key, "foo.bar.bleep")
        && check_string_value (dirent, "foo") == 0,
        "2: put foo.bar.baz = \"foo\"");

    ok (txn_get (txn, TXN_GET_NEXT, &entry) == 0 && entry != NULL,
        "3: retrieved");
    jdiag (entry);
    rc = json_unpack (entry, "{s:s s:n}", "key", &key,
                                          "dirent");
    ok (rc == 0 && !strcmp (key, "a"),
        "3: unlink a");

    ok (txn_get (txn, TXN_GET_NEXT, &entry) == 0 && entry != NULL,
        "4: retrieved");
    jdiag (entry);
    rc = json_unpack (entry, "{s:s s:o}", "key", &key,
                                          "dirent", &dirent);
    ok (rc == 0 && !strcmp (key, "b.b.b")
        && treeobj_is_dir (dirent) && treeobj_get_count (dirent) == 0,
        "4: mkdir b.b.b");

    ok (txn_get (txn, TXN_GET_NEXT, &entry) == 0 && entry != NULL,
        "5: retrieved");
    jdiag (entry);
    rc = json_unpack (entry, "{s:s s:o}", "key", &key,
                                          "dirent", &dirent);
    ok (rc == 0 && !strcmp (key, "c.c.c") && treeobj_is_symlink (dirent)
        && !strcmp (json_string_value (treeobj_get_data (dirent)), "b.b.b"),
        "5: symlink c.c.c b.b.b");

    ok (txn_get (txn, TXN_GET_NEXT, &entry) == 0 && entry != NULL,
        "6: retrieved");
    ok (json_unpack (entry, "{s:s s:o}", "key", &key,
                                         "dirent", &dirent) == 0,
        "6: unpacked operation");
    ok (!strcmp (key, "d.d.d")
        && check_int_value (dirent, 43) == 0,
        "6: put foo.bar.baz = 43");

    ok (txn_get (txn, TXN_GET_NEXT, &entry) == 0 && entry != NULL,
        "7: retrieved");
    jdiag (entry);
    rc = json_unpack (entry, "{s:s s:n}", "key", &key,
                                          "dirent");
    ok (rc == 0 && !strcmp (key, "e"),
        "7: unlink e");

    ok (txn_get (txn, TXN_GET_NEXT, &entry) == 0 && entry == NULL,
        "8: NULL - end of transaction");

    flux_kvs_txn_destroy (txn);
}

void test_raw_values (void)
{
    flux_kvs_txn_t *txn;
    char buf[13], *nbuf;
    json_t *entry, *dirent;
    const char *key;
    int nlen;

    memset (buf, 'c', sizeof (buf));

    txn = flux_kvs_txn_create ();
    ok (txn != NULL,
        "flux_kvs_txn_create works");

    /* Try some bad params
     */
    errno = 0;
    ok (flux_kvs_txn_put_raw (txn, FLUX_KVS_TREEOBJ, "a.b.c",
                              buf, sizeof (buf)) < 0
        && errno == EINVAL,
        "flux_kvs_txn_put_raw fails with EINVAL when fed TREEOBJ flag");
    errno = 0;
    ok (flux_kvs_txn_put_raw (txn, 0, NULL, buf, sizeof (buf)) < 0
        && errno == EINVAL,
        "flux_kvs_txn_put_raw fails with EINVAL when fed NULL key");

    /* Put an empty buffer.
     */
    ok (flux_kvs_txn_put_raw (txn, 0, "a.a.a", NULL, 0) == 0,
        "flux_kvs_txn_put_raw works on empty buffer");
    /* Put some data
     */
    ok (flux_kvs_txn_put_raw (txn, 0, "a.b.c", buf, sizeof (buf)) == 0,
        "flux_kvs_txn_put_raw works with data");
    /* Get first.
     */
    ok (txn_get (txn, TXN_GET_FIRST, &entry) == 0 && entry != NULL,
        "retreived 1st op from txn");
    jdiag (entry);
    ok (json_unpack (entry, "{s:s s:o}", "key", &key,
                                         "dirent", &dirent) == 0,
        "decoded op to get dirent");
    nbuf = buf;
    nlen = sizeof (buf);
    ok (treeobj_decode_val (dirent, (void **)&nbuf, &nlen) == 0,
        "retrieved buffer from dirent");
    ok (nlen == 0,
        "and it is size of zero");
    ok (nbuf == NULL,
        "and buffer is NULL");

    /* Get 2nd
     */
    ok (txn_get (txn, TXN_GET_NEXT, &entry) == 0 && entry != NULL,
        "retreived 2nd op from txn");
    jdiag (entry);
    ok (json_unpack (entry, "{s:s s:o}", "key", &key,
                                         "dirent", &dirent) == 0,
        "decoded op to get dirent");
    ok (treeobj_decode_val (dirent, (void **)&nbuf, &nlen) == 0,
        "retrieved buffer from dirent");
    ok (nlen == sizeof (buf),
        "and it is the correct size");
    ok (memcmp (nbuf, buf, nlen) == 0,
        "and it is the correct content");
    free (nbuf);
    flux_kvs_txn_destroy (txn);
}

int main (int argc, char *argv[])
{

    plan (NO_PLAN);

    basic ();
    test_raw_values ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

