/* Chess Online: the app's own updater, Vita side (1.0.1). Our own code.
 * At start it asks GitHub for the public repo's latest release (update.h). When that is newer than this version, Home
 * shows "Update available" with Install / Ask me later. Install downloads the release's ChessOnline-*.vpk into
 * ux0:data/ChessOnline/update/ and checks its size and the sha256 digest GitHub gives. It checks that the package's
 * param.sfo is CHESSONL1's, unpacks it into update/new/ (every name checked, every file's CRC-32) and writes
 * update/job.txt. Then it starts the helper app, Chess Online Updater (CHESSUPD1, updater/), and closes. The helper
 * copies the files into ux0:app/CHESSONL1/ (this app's own folder is locked while it runs), keeping a backup, and
 * reopens Chess Online, which shows the result the helper wrote in update/result.txt. When the helper can't be
 * started, the dialog says where the VPK is, for VitaShell. */
#ifndef CO_UPDATEUI_H
#define CO_UPDATEUI_H

#include <stdint.h>

#include "net.h"
#include "platform.h"

/* after net_init: reads the helper's last result and starts the release check; version is this app's (APP_VERSION) */
void update_init(const char *version, uint64_t now);
void update_on_response(const net_msg *m, uint64_t now); /* an NM_RESPONSE with NET_TAG_UPDATE */
void update_timers(uint64_t now);
int update_showing(void);                             /* a dialog or the progress card is up (drawn over Home) */
int update_input(const plat_input *in, uint64_t now); /* 1 when it took the input */
void update_draw(uint64_t now);

#endif
