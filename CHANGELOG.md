# Changelog

## 1.0.1 (2026-11-12)

- The app updates itself. At start it checks this repo's latest GitHub release and offers **Install** or **Ask me
  later**. Install downloads the update and checks its size, SHA-256 and title ID. The built-in helper app then puts
  the files in place, keeps a backup and rolls back on failure, and the app reopens on the new version. Players still
  install only one VPK.
- The certificate bundle adds the USERTrust roots that GitHub uses.
- The updater's texts are in all 18 languages.
- Fixed: two connections starting at once (the chess server and the update check) could get each other's server
  address, because the system's name lookup is not safe to call from two threads at once. The handshake then failed
  until curl's 60-second DNS cache ran out. Name lookups now take turns. This could also have hit the chess
  connection.
- The release check retries up to 3 times. After a failed or cancelled update the app doesn't ask again until the
  next start.
- Tested on a PS Vita 2000:
  - 1.0.1 to a newer version, start to finish;
  - a helper failure halfway, with every file put back;
  - a tampered download, refused before anything changes;
  - Cancel during a real GitHub download.

## 1.0.0 (2026-11-12)

First release. It was played through on a PS Vita 2000 (firmware 3.65).

- Play vs computer: our own engine with eight levels; takeback, resign and new game.
- Random opponent: the guest lobby, with its list of waiting games and a form to create one. Your own waiting game
  is left out of the list.
- Play a friend: make a six-letter code or type one in. A refused code shows under Join game.
- Online games:
  - server clocks, which stop where they stood when a game ends between moves;
  - takeback and rematch offers;
  - resign and leave, each with a confirmation;
  - reconnect and resume after a drop.
- Chess tips: five lessons, 26 steps.
- Your statistics: an overview, vs computer, online and recent games. Clear keeps a copy.
- 18 languages, one for each system language the Vita offers. The app follows the Vita's setting until you pick a
  language in the app.
- Fixes found while playing it through:
  - cut texts no longer split a letter (a box showed before the "...");
  - the dim over the board follows the frame's round corners;
  - game buttons need a tap that starts and ends on them, so a drag can no longer press a result card's button;
  - Russian and Polish lines shortened to fit.
