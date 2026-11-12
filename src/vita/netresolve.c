/* Chess Online: name lookups one at a time (1.0.1). Our own code.
 *
 * Observed 2026-11-12: when the update check (api.github.com) and the chess server's connection
 * (chess1.coolmathgames.com) looked up their names at the same moment on two threads, the GitHub request sometimes got
 * the chess server's address (104.18.36.160). The TLS handshake there failed ("sslv3 alert handshake failure"), and
 * curl kept the wrong address for its 60 s DNS cache. The system's getaddrinfo is not safe to call from two threads at
 * once. Every getaddrinfo in the program (curl's included) is linked to __wrap_getaddrinfo (-Wl,--wrap, CMakeLists.txt).
 * It calls the real one under a lock and copies the answer into memory of its own before the lock is released, and
 * __wrap_freeaddrinfo frees that copy. */
#include <netdb.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

int __real_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);
void __real_freeaddrinfo(struct addrinfo *ai);
int __wrap_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);
void __wrap_freeaddrinfo(struct addrinfo *ai);

static pthread_mutex_t lookup_lock = PTHREAD_MUTEX_INITIALIZER;

void __wrap_freeaddrinfo(struct addrinfo *ai)
{
    while (ai) {
        struct addrinfo *next = ai->ai_next;
        free(ai->ai_canonname);
        free(ai); /* ai_addr lives in the same block */
        ai = next;
    }
}

int __wrap_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
    struct addrinfo *found = NULL, *p, *head = NULL, **tail = &head;
    int rc;
    pthread_mutex_lock(&lookup_lock);
    rc = __real_getaddrinfo(node, service, hints, &found);
    if (rc == 0) {
        for (p = found; p; p = p->ai_next) {
            struct addrinfo *c = (struct addrinfo *)calloc(1, sizeof *c + p->ai_addrlen);
            if (!c) {
                rc = EAI_MEMORY;
                break;
            }
            *c = *p;
            c->ai_next = NULL;
            c->ai_canonname = NULL;
            if (p->ai_addr && p->ai_addrlen) {
                c->ai_addr = (struct sockaddr *)(void *)(c + 1);
                memcpy(c->ai_addr, p->ai_addr, p->ai_addrlen);
            } else {
                c->ai_addr = NULL;
            }
            if (p->ai_canonname) c->ai_canonname = strdup(p->ai_canonname);
            *tail = c;
            tail = &c->ai_next;
        }
        __real_freeaddrinfo(found);
    }
    pthread_mutex_unlock(&lookup_lock);
    if (rc != 0) {
        __wrap_freeaddrinfo(head);
        head = NULL;
    }
    *res = head;
    return rc;
}
