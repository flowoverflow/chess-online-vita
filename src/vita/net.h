/* Chess Online: HTTPS and WebSocket to the guest chess server with libcurl (TLS 1.2+, OpenSSL). Our own code.
 * Plain requests run one at a time on a single worker; the guest transport runs on its own thread(s). Results come
 * back to the main thread through net_poll. */
#ifndef CO_NET_H
#define CO_NET_H

#include <stddef.h>
#include <stdint.h>

enum {
    NET_TAG_CMG = 1, /* the guest transport: Coolmath's chess server (WebSocket or long-poll) */
    NET_TAG_COUNT
};

enum { NM_LINE = 1, NM_END, NM_RESPONSE, NM_OPEN };

typedef struct {
    int kind;    /* NM_LINE: one JSON message; NM_END: the transport closed; NM_OPEN; NM_RESPONSE: a request finished */
    int tag;     /* NET_TAG_CMG, or the caller's request tag */
    int gen;     /* transport generation, to drop messages from a transport that was replaced */
    int status;  /* HTTP status, or 0 when no answer */
    int curl_rc; /* CURLcode, or -1 for a long-poll failure */
    char *data;  /* message or body (NUL-terminated), malloc'd */
    size_t len;
} net_msg;

int net_init(const char *ca_pem, size_t ca_len, const char *user_agent);

/* A request to a full https:// URL; the answer arrives as NM_RESPONSE with this tag. ctype: 1 form, 2 JSON, 3 text. */
void net_request_url(int tag, const char *method, const char *url, const char *body, int ctype);
int net_requests_pending(void);
/* Local monotonic ms until which the worker waits after an HTTP 429 (0 when not waiting). */
uint64_t net_rate_limit_until(void);

/* Guest transport to wss://chess1.coolmathgames.com/ws, or its HTTPS long-poll (/api/rt) when use_poll.
 * One at a time: opening replaces the previous one. Delivers NM_OPEN, NM_LINE and NM_END (unexpected close)
 * with tag NET_TAG_CMG and the transport's gen. net_cmg_close is an intentional close: nothing is sent to the
 * game (no leave). */
int net_cmg_open(int use_poll);
void net_cmg_close(void);
int net_cmg_send(const char *json); /* -1 when not open */
/* Waits up to max_ms for queued messages to go out (before an intentional close); 1 when all were sent. */
int net_cmg_flush(int max_ms);
int net_cmg_state(void);            /* 0 closed, 1 connecting, 2 open */
int net_cmg_is_poll(void);
int net_cmg_gen(void);

int net_poll(net_msg *m);
void net_msg_free(net_msg *m);

int net_wifi_connected(void);
const char *net_error_text(int curl_rc);

#endif
