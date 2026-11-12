/* Chess Online: HTTPS and WebSocket to the guest chess server with libcurl (TLS 1.2+, OpenSSL). Our own code; see net.h. */
#include "net.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <psp2/io/fcntl.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>

#include "json.h"
#include "platform.h"

#define BODY_LIMIT (1024 * 1024)
#define STACK_SIZE (256 * 1024)

typedef struct qnode {
    net_msg m;
    struct qnode *next;
} qnode;

static pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;
static qnode *qhead, *qtail;

static struct curl_blob ca_blob;
static char user_agent[128];

/* request worker */
typedef struct req {
    int tag, ctype;
    char method[8];
    char path[512];
    char *form;
    struct req *next;
} req;
static pthread_mutex_t rlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t rcond = PTHREAD_COND_INITIALIZER;
static req *rhead, *rtail;
static volatile int rbusy;
static volatile uint64_t rate_until;

static void push(int kind, int tag, int gen, int status, int rc, const char *data, size_t len)
{
    qnode *n = (qnode *)calloc(1, sizeof *n);
    if (!n) return;
    n->m.kind = kind;
    n->m.tag = tag;
    n->m.gen = gen;
    n->m.status = status;
    n->m.curl_rc = rc;
    if (data) {
        n->m.data = (char *)malloc(len + 1);
        if (n->m.data) {
            memcpy(n->m.data, data, len);
            n->m.data[len] = 0;
            n->m.len = len;
        }
    }
    pthread_mutex_lock(&qlock);
    if (qtail) qtail->next = n;
    else qhead = n;
    qtail = n;
    pthread_mutex_unlock(&qlock);
}

int net_poll(net_msg *m)
{
    qnode *n;
    pthread_mutex_lock(&qlock);
    n = qhead;
    if (n) {
        qhead = n->next;
        if (!qhead) qtail = NULL;
    }
    pthread_mutex_unlock(&qlock);
    if (!n) return 0;
    *m = n->m;
    free(n);
    return 1;
}

void net_msg_free(net_msg *m)
{
    free(m->data);
    m->data = NULL;
}

/* ctype: 0 none, 1 form, 2 JSON, 3 plain text */
static struct curl_slist *headers(int ctype)
{
    static const char *types[] = {NULL, "Content-Type: application/x-www-form-urlencoded",
                                  "Content-Type: application/json", "Content-Type: text/plain"};
    struct curl_slist *h = NULL;
    h = curl_slist_append(h, "Accept: application/json");
    if (ctype > 0 && ctype < 4) h = curl_slist_append(h, types[ctype]);
    return h;
}

static void common_opts(CURL *c, const char *url, struct curl_slist *h, const char *method, const char *form)
{
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_USERAGENT, user_agent);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_CAINFO, NULL);
    curl_easy_setopt(c, CURLOPT_CAPATH, NULL);
    curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, &ca_blob);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    if (!strcmp(method, "POST")) {
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_COPYPOSTFIELDS, form ? form : "");
    } else if (strcmp(method, "GET")) {
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method);
    }
}

/* ---- request worker ---- */
typedef struct {
    char *buf;
    size_t len;
} body_t;

static size_t body_write(char *data, size_t sz, size_t n, void *ud)
{
    body_t *b = (body_t *)ud;
    size_t add = sz * n;
    char *nb;
    if (b->len + add > BODY_LIMIT) return 0;
    nb = (char *)realloc(b->buf, b->len + add + 1);
    if (!nb) return 0;
    b->buf = nb;
    memcpy(b->buf + b->len, data, add);
    b->len += add;
    b->buf[b->len] = 0;
    return add;
}

static void *worker_main(void *arg)
{
    CURL *c = curl_easy_init();
    (void)arg;
    for (;;) {
        req *r;
        body_t body = {0, 0};
        struct curl_slist *h;
        long status = 0;
        CURLcode rc;
        pthread_mutex_lock(&rlock);
        while (!rhead) pthread_cond_wait(&rcond, &rlock);
        r = rhead;
        rhead = r->next;
        if (!rhead) rtail = NULL;
        rbusy = 1;
        pthread_mutex_unlock(&rlock);

        while (rate_until && plat_ms() < rate_until) {
            struct timespec ts = {0, 200 * 1000 * 1000};
            nanosleep(&ts, NULL);
        }
        rate_until = 0;
        if (!c) c = curl_easy_init();
        if (c) {
            curl_easy_reset(c);
            h = headers(r->form ? r->ctype : 0);
            common_opts(c, r->path, h, r->method, r->form);
            curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
            curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, body_write);
            curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
            rc = curl_easy_perform(c);
            curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
            curl_slist_free_all(h);
        } else {
            rc = CURLE_FAILED_INIT;
        }
        plat_log("net: %s %s -> HTTP %ld, %s", r->method, r->path, status, curl_easy_strerror(rc));
        if (status == 429) {
            rate_until = plat_ms() + 61000;
            plat_log("net: rate limited; waiting a minute before the next request");
        }
        push(NM_RESPONSE, r->tag, 0, (int)status, (int)rc, body.buf ? body.buf : "", body.len);
        free(body.buf);
        free(r->form);
        free(r);
        pthread_mutex_lock(&rlock);
        rbusy = 0;
        pthread_mutex_unlock(&rlock);
    }
    return NULL;
}

int net_requests_pending(void)
{
    int n;
    pthread_mutex_lock(&rlock);
    n = rhead != NULL || rbusy;
    pthread_mutex_unlock(&rlock);
    return n;
}

uint64_t net_rate_limit_until(void)
{
    return rate_until;
}

int net_wifi_connected(void)
{
    int state = 0;
    if (sceNetCtlInetGetState(&state) < 0) return 0;
    return state == SCE_NETCTL_STATE_CONNECTED;
}

const char *net_error_text(int rc)
{
    return curl_easy_strerror((CURLcode)rc);
}

int net_init(const char *ca_pem, size_t ca_len, const char *ua)
{
    static char netmem[2 * 1024 * 1024] __attribute__((aligned(64)));
    SceNetInitParam p;
    pthread_attr_t at;
    pthread_t th;
    int r;
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    p.memory = netmem;
    p.size = sizeof netmem;
    p.flags = 0;
    r = sceNetInit(&p);
    if (r < 0 && r != (int)0x80410201 /* already initialised */) plat_log("net: sceNetInit 0x%08x", (unsigned)r);
    r = sceNetCtlInit();
    if (r < 0) plat_log("net: sceNetCtlInit 0x%08x", (unsigned)r);
    if (curl_global_init(CURL_GLOBAL_ALL)) {
        plat_log("net: curl_global_init failed");
        return -1;
    }
    ca_blob.data = (void *)ca_pem;
    ca_blob.len = ca_len;
    ca_blob.flags = CURL_BLOB_NOCOPY;
    snprintf(user_agent, sizeof user_agent, "%s", ua);
    plat_log("net: %s", curl_version());
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, STACK_SIZE);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    r = pthread_create(&th, &at, worker_main, NULL);
    pthread_attr_destroy(&at);
    return r ? -1 : 0;
}

/* ---- plain request to a full URL ---- */
void net_request_url(int tag, const char *method, const char *url, const char *body, int ctype)
{
    req *r = (req *)calloc(1, sizeof *r);
    if (!r) return;
    r->tag = tag;
    r->ctype = ctype;
    snprintf(r->method, sizeof r->method, "%s", method);
    snprintf(r->path, sizeof r->path, "%s", url);
    r->form = body ? strdup(body) : NULL;
    pthread_mutex_lock(&rlock);
    if (rtail) rtail->next = r;
    else rhead = r;
    rtail = r;
    pthread_cond_signal(&rcond);
    pthread_mutex_unlock(&rlock);
}

/* ---- a file download (1.0.1, the updater) ---- */
typedef struct {
    SceUID fd;
    int err;
    volatile unsigned long long *done, *total;
    volatile int *cancel;
} dl_t;

static size_t dl_write(char *data, size_t sz, size_t n, void *ud)
{
    dl_t *d = (dl_t *)ud;
    size_t len = sz * n;
    if (sceIoWrite(d->fd, data, (SceSize)len) != (int)len) {
        d->err = 1;
        return 0;
    }
    return len;
}

static int dl_progress(void *ud, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    dl_t *d = (dl_t *)ud;
    (void)ultotal;
    (void)ulnow;
    if (dltotal > 0) *d->total = (unsigned long long)dltotal;
    *d->done = (unsigned long long)dlnow;
    return *d->cancel ? 1 : 0;
}

int net_download(const char *url, const char *path, volatile unsigned long long *done,
                 volatile unsigned long long *total, volatile int *cancel, int *http, int *curl_rc)
{
    CURL *c = curl_easy_init();
    dl_t d;
    long status = 0;
    CURLcode rc = CURLE_FAILED_INIT;
    *http = 0;
    *curl_rc = (int)rc;
    if (!c) return -1;
    memset(&d, 0, sizeof d);
    d.done = done;
    d.total = total;
    d.cancel = cancel;
    d.fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (d.fd < 0) {
        plat_log("net: can't write %s (0x%08x)", path, (unsigned)d.fd);
        curl_easy_cleanup(c);
        return -1;
    }
    common_opts(c, url, NULL, "GET", NULL);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L); /* github.com answers 302 to its release-asset server */
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 256L); /* give up after 30 s below 256 bytes a second */
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, dl_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &d);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, dl_progress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &d);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    sceIoClose(d.fd);
    curl_easy_cleanup(c);
    *http = (int)status;
    *curl_rc = (int)rc;
    plat_log("net: download %s -> HTTP %ld, %s, %llu bytes%s", url, status, curl_easy_strerror(rc), *done,
             d.err ? " (write failed)" : "");
    return rc == CURLE_OK && status == 200 && !d.err ? 0 : -1;
}

/* ---- guest mode transport: Coolmath's chess server ---- */
#define CMG_WS_URL "wss://chess1.coolmathgames.com/ws"
#define CMG_RT_URL "https://chess1.coolmathgames.com/api/rt"
#define CMG_MSG_LIMIT (256 * 1024)

typedef struct outmsg {
    char *data;
    struct outmsg *next;
} outmsg;

static pthread_mutex_t cmg_lock = PTHREAD_MUTEX_INITIALIZER;
static outmsg *cmg_out_head, *cmg_out_tail;
static volatile int cmg_gen_v, cmg_state_v, cmg_poll_v;
static volatile int cmg_rx_closed; /* the long-poll receiver gave up */
static volatile unsigned cmg_queued, cmg_sent; /* messages queued / fully sent on the current transport */

static void cmg_out_clear(void)
{
    outmsg *o;
    pthread_mutex_lock(&cmg_lock);
    while ((o = cmg_out_head)) {
        cmg_out_head = o->next;
        free(o->data);
        free(o);
    }
    cmg_out_tail = NULL;
    pthread_mutex_unlock(&cmg_lock);
}

static outmsg *cmg_out_pop(void)
{
    outmsg *o;
    pthread_mutex_lock(&cmg_lock);
    o = cmg_out_head;
    if (o) {
        cmg_out_head = o->next;
        if (!cmg_out_head) cmg_out_tail = NULL;
    }
    pthread_mutex_unlock(&cmg_lock);
    return o;
}

int net_cmg_send(const char *json)
{
    outmsg *o;
    if (cmg_state_v != 2) return -1;
    o = (outmsg *)calloc(1, sizeof *o);
    if (!o || !(o->data = strdup(json))) {
        free(o);
        return -1;
    }
    pthread_mutex_lock(&cmg_lock);
    if (cmg_out_tail) cmg_out_tail->next = o;
    else cmg_out_head = o;
    cmg_out_tail = o;
    cmg_queued++;
    pthread_mutex_unlock(&cmg_lock);
    return 0;
}

int net_cmg_flush(int max_ms)
{
    uint64_t until = plat_ms() + (uint64_t)max_ms;
    while (cmg_state_v == 2 && cmg_sent != cmg_queued && plat_ms() < until) {
        struct timespec ts = {0, 10 * 1000 * 1000};
        nanosleep(&ts, NULL);
    }
    return cmg_sent == cmg_queued;
}

int net_cmg_state(void) { return cmg_state_v; }
int net_cmg_is_poll(void) { return cmg_poll_v; }
int net_cmg_gen(void) { return cmg_gen_v; }

static void tiny_sleep(int ms)
{
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000L * 1000L};
    nanosleep(&ts, NULL);
}

static void cmg_tls_opts(CURL *c)
{
    curl_easy_setopt(c, CURLOPT_USERAGENT, user_agent);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_CAINFO, NULL);
    curl_easy_setopt(c, CURLOPT_CAPATH, NULL);
    curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, &ca_blob);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
}

static void *cmg_ws_main(void *arg)
{
    int gen = (int)(intptr_t)arg;
    CURL *c = curl_easy_init();
    CURLcode rc = CURLE_FAILED_INIT;
    long status = 0;
    char *msg = NULL;
    size_t mlen = 0;
    char buf[4096];
    outmsg *cur = NULL;
    size_t cur_off = 0;
    if (!c) goto out;
    curl_easy_setopt(c, CURLOPT_URL, CMG_WS_URL);
    cmg_tls_opts(c);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 8L); /* the browser client also gives up after 8 s */
    curl_easy_setopt(c, CURLOPT_CONNECT_ONLY, 2L);   /* WebSocket: handshake, then curl_ws_send/recv */
    plat_log("cmg: ws connecting (%s)", CMG_WS_URL);
    rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    if (rc != CURLE_OK) {
        plat_log("cmg: ws handshake failed: %s (curl %d), HTTP %ld", curl_easy_strerror(rc), (int)rc, status);
        goto out;
    }
    plat_log("cmg: ws open (HTTP %ld)", status);
    if (cmg_gen_v == gen) {
        cmg_state_v = 2;
        push(NM_OPEN, NET_TAG_CMG, gen, (int)status, 0, NULL, 0);
    }
    while (cmg_gen_v == gen) {
        int busy = 0;
        size_t n = 0;
        const struct curl_ws_frame *meta = NULL;
        if (!cur) {
            cur = cmg_out_pop();
            cur_off = 0;
        }
        if (cur) {
            size_t len = strlen(cur->data), sent = 0;
            rc = curl_ws_send(c, cur->data + cur_off, len - cur_off, &sent, 0, CURLWS_TEXT);
            if (rc == CURLE_OK || rc == CURLE_AGAIN) {
                cur_off += sent;
                if (cur_off >= len) {
                    free(cur->data);
                    free(cur);
                    cur = NULL;
                    cmg_sent++;
                }
                busy = 1;
            } else {
                plat_log("cmg: ws send failed: %s (curl %d)", curl_easy_strerror(rc), (int)rc);
                break;
            }
        }
        rc = curl_ws_recv(c, buf, sizeof buf, &n, &meta);
        if (rc == CURLE_OK && meta) {
            busy = 1;
            if (meta->flags & CURLWS_CLOSE) {
                plat_log("cmg: ws closed by the server");
                rc = CURLE_RECV_ERROR;
                break;
            }
            if (meta->flags & (CURLWS_TEXT | CURLWS_BINARY | CURLWS_CONT)) {
                char *nm;
                if (mlen + n > CMG_MSG_LIMIT) {
                    plat_log("cmg: message over %d bytes dropped", CMG_MSG_LIMIT);
                    mlen = 0;
                } else if ((nm = (char *)realloc(msg, mlen + n + 1))) {
                    msg = nm;
                    memcpy(msg + mlen, buf, n);
                    mlen += n;
                    msg[mlen] = 0;
                }
                if (meta->bytesleft == 0 && !(meta->flags & CURLWS_CONT)) {
                    if (mlen) push(NM_LINE, NET_TAG_CMG, gen, 200, 0, msg, mlen);
                    mlen = 0;
                }
            }
        } else if (rc != CURLE_AGAIN && rc != CURLE_OK) {
            plat_log("cmg: ws receive failed: %s (curl %d)", curl_easy_strerror(rc), (int)rc);
            break;
        }
        if (!busy) tiny_sleep(10);
    }
    if (cmg_gen_v != gen) { /* closed on purpose: send a close frame */
        size_t sent = 0;
        curl_ws_send(c, "", 0, &sent, 0, CURLWS_CLOSE);
        plat_log("cmg: ws closed by us");
    }
out:
    if (cur) {
        free(cur->data);
        free(cur);
    }
    free(msg);
    if (c) curl_easy_cleanup(c);
    if (cmg_gen_v == gen) {
        cmg_state_v = 0;
        push(NM_END, NET_TAG_CMG, gen, (int)status, (int)rc, NULL, 0);
    }
    return NULL;
}

/* long-poll fallback: one thread receives, the transport thread sends in order */
typedef struct {
    int gen;
    char sid[96];
} poll_ctx;

static int rt_call(CURL *c, const char *method, const char *url, const char *body, long timeout, body_t *out,
                   long *status)
{
    struct curl_slist *h = NULL;
    CURLcode rc;
    curl_easy_reset(c);
    curl_easy_setopt(c, CURLOPT_URL, url);
    cmg_tls_opts(c);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, body_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, out);
    if (!strcmp(method, "POST")) {
        h = curl_slist_append(h, "Content-Type: text/plain");
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_COPYPOSTFIELDS, body ? body : "");
    }
    rc = curl_easy_perform(c);
    *status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, status);
    curl_slist_free_all(h);
    return rc == CURLE_OK && *status == 200 ? 0 : (rc ? (int)rc : -1);
}

static void *cmg_poll_rx(void *arg)
{
    poll_ctx *p = (poll_ctx *)arg;
    CURL *c = curl_easy_init();
    long ack = 0;
    int errors = 0;
    char url[256];
    while (c && cmg_gen_v == p->gen) {
        body_t b = {0, 0};
        long status = 0;
        jval *j = NULL;
        snprintf(url, sizeof url, CMG_RT_URL "/poll?sid=%s&ack=%ld", p->sid, ack);
        if (rt_call(c, "GET", url, NULL, 40, &b, &status) == 0 && (j = json_parse(b.buf ? b.buf : "", b.len))) {
            const jval *arr = json_get(j, "m"), *it;
            int closed = json_bool(j, "c", 0);
            errors = 0;
            for (it = arr && arr->t == J_ARR ? arr->child : NULL; it; it = it->next) {
                long s = (long)json_num(it, "s", 0);
                const char *d = json_str(it, "d", NULL);
                if (s > ack && d) { /* re-delivered messages carry old numbers */
                    ack = s;
                    if (cmg_gen_v == p->gen) push(NM_LINE, NET_TAG_CMG, p->gen, 200, 0, d, strlen(d));
                }
            }
            json_free(j);
            free(b.buf);
            if (closed) {
                plat_log("cmg: poll transport closed by the server");
                break;
            }
            continue;
        }
        free(b.buf);
        if (cmg_gen_v != p->gen) break;
        errors++;
        plat_log("cmg: poll failed (HTTP %ld), %d in a row", status, errors);
        if (errors >= 4) break;
        tiny_sleep((500 << errors) > 5000 ? 5000 : (500 << errors));
    }
    if (c) curl_easy_cleanup(c);
    if (cmg_gen_v == p->gen) cmg_rx_closed = 1;
    free(p);
    return NULL;
}

static void *cmg_poll_main(void *arg)
{
    int gen = (int)(intptr_t)arg;
    CURL *c = curl_easy_init();
    body_t b = {0, 0};
    long status = 0, seq = 0;
    int rc = -1;
    char sid[96] = "", url[256];
    jval *j;
    if (!c) goto out;
    plat_log("cmg: poll transport opening");
    rc = rt_call(c, "POST", CMG_RT_URL "/open", "", 20, &b, &status);
    if (rc == 0 && (j = json_parse(b.buf ? b.buf : "", b.len))) {
        snprintf(sid, sizeof sid, "%s", json_str(j, "sid", ""));
        json_free(j);
    }
    free(b.buf);
    b.buf = NULL;
    b.len = 0;
    if (!sid[0]) {
        plat_log("cmg: poll open failed (HTTP %ld)", status);
        rc = -1;
        goto out;
    }
    {
        pthread_attr_t at;
        pthread_t th;
        poll_ctx *p = (poll_ctx *)calloc(1, sizeof *p);
        if (!p) goto out;
        p->gen = gen;
        snprintf(p->sid, sizeof p->sid, "%s", sid);
        cmg_rx_closed = 0;
        pthread_attr_init(&at);
        pthread_attr_setstacksize(&at, STACK_SIZE);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&th, &at, cmg_poll_rx, p)) {
            free(p);
            pthread_attr_destroy(&at);
            goto out;
        }
        pthread_attr_destroy(&at);
    }
    plat_log("cmg: poll transport open");
    if (cmg_gen_v == gen) {
        cmg_state_v = 2;
        push(NM_OPEN, NET_TAG_CMG, gen, 200, 0, NULL, 0);
    }
    rc = 0;
    while (cmg_gen_v == gen && !cmg_rx_closed) {
        outmsg *o = cmg_out_pop();
        int attempt, closed = 0;
        if (!o) {
            tiny_sleep(15);
            continue;
        }
        seq++; /* numbered once; a retry reuses the number so the server can drop a duplicate */
        snprintf(url, sizeof url, CMG_RT_URL "/send?sid=%s&n=%ld", sid, seq);
        for (attempt = 0; attempt <= 3; attempt++) {
            b.buf = NULL;
            b.len = 0;
            if (rt_call(c, "POST", url, o->data, 20, &b, &status) == 0) break;
            free(b.buf);
            b.buf = NULL;
            tiny_sleep(400 * (attempt + 1));
        }
        free(o->data);
        free(o);
        if (attempt > 3) {
            plat_log("cmg: poll send %ld failed (HTTP %ld)", seq, status);
            rc = -1;
            break;
        }
        cmg_sent++;
        if (b.buf && (j = json_parse(b.buf, b.len))) {
            closed = json_bool(j, "c", 0);
            json_free(j);
        }
        free(b.buf);
        b.buf = NULL;
        if (closed) {
            rc = -1;
            break;
        }
    }
    if (cmg_gen_v != gen && sid[0]) {
        snprintf(url, sizeof url, CMG_RT_URL "/close?sid=%s", sid);
        b.buf = NULL;
        b.len = 0;
        rt_call(c, "POST", url, "", 10, &b, &status);
        free(b.buf);
        plat_log("cmg: poll transport closed by us");
    }
out:
    if (c) curl_easy_cleanup(c);
    if (cmg_gen_v == gen) {
        cmg_state_v = 0;
        push(NM_END, NET_TAG_CMG, gen, (int)status, rc ? -1 : 0, NULL, 0);
    }
    return NULL;
}

int net_cmg_open(int use_poll)
{
    pthread_attr_t at;
    pthread_t th;
    int gen = cmg_gen_v + 1;
    cmg_gen_v = gen; /* any older transport stops on its next turn */
    cmg_out_clear();
    cmg_queued = cmg_sent = 0;
    cmg_state_v = 1;
    cmg_poll_v = use_poll;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, STACK_SIZE);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &at, use_poll ? cmg_poll_main : cmg_ws_main, (void *)(intptr_t)gen)) {
        cmg_state_v = 0;
        push(NM_END, NET_TAG_CMG, gen, 0, (int)CURLE_FAILED_INIT, NULL, 0);
    }
    pthread_attr_destroy(&at);
    return gen;
}

void net_cmg_close(void)
{
    if (cmg_state_v) plat_log("cmg: closing the transport on purpose");
    cmg_gen_v++;
    cmg_state_v = 0;
    cmg_out_clear();
}
