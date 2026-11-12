/* Chess Online online play: anonymous guest games on Coolmath's chess server, behind the app's own interface.
 * Our own code. Protocol notes: docs/chessonline.md ("Online play"). */
#ifndef CO_GUEST_H
#define CO_GUEST_H

#include <stdint.h>

#include "net.h"
#include "platform.h"

enum { GUEST_VIEW_RANDOM = 1, GUEST_VIEW_FRIEND = 2 };

void guest_start(void);                         /* at app start: join the guest lobby */
void guest_open(int view);                      /* show "Random opponent" or "Play a friend" */
int guest_on_net(const net_msg *m, uint64_t now);
void guest_on_response(const net_msg *m);       /* responses with tag GUEST_REQ_LEAVE */
void guest_timers(uint64_t now);
int guest_input(const plat_input *in, uint64_t now); /* 1: back to the home screen */
void guest_draw(uint64_t now);
int guest_playing(void);                        /* an online game is running (keep the console awake) */
int guest_showing(void);                        /* the online module has a screen to show */
int guest_lobby_view(void);                     /* that screen is the random or friend dialog (drawn over home) */
void guest_ime_result(const char *text);        /* the on-screen keyboard's text (a friend's code) */

/* for the home screen */
typedef struct {
    int state;        /* 0 offline, 1 connecting, 2 online */
    int retry_in_s;   /* seconds to the next attempt while offline, else 0 */
    int have_stats, players, active_games;
    int open_games;   /* games waiting in the lobby list */
    int poll;         /* using the HTTPS fallback */
} guest_status;
void guest_get_status(guest_status *st);

#define GUEST_REQ_LEAVE 300

#endif
