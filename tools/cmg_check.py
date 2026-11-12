#!/usr/bin/env python3
"""Chess Online guest mode: PC check of Coolmath's chess server (wss://chess1.coolmathgames.com/ws).

Our own code (Python standard library only): a minimal RFC 6455 client, with no Origin header and an honest
User-Agent. Tests use only private games between our own connections; `lobby` only reads the public lobby list and
counters and creates nothing. Reconnect tokens are redacted from all output.

  py -3.13 -I tools/cmg_check.py lobby|private|leave|expired
"""
import base64
import json
import os
import socket
import ssl
import struct
import sys
import time

HOST = "chess1.coolmathgames.com"
UA = "ChessOnline-dev/0.1 (personal PS Vita client, PC check)"
CTX = ssl.create_default_context()
LOG = []


def log(msg):
    line = time.strftime("%H:%M:%S ") + msg
    print(line, flush=True)
    LOG.append(line)


def redact(obj):
    if isinstance(obj, dict):
        return {k: ("<redacted %d chars>" % len(v) if k in ("reconnectToken", "token") and isinstance(v, str) else redact(v))
                for k, v in obj.items()}
    if isinstance(obj, list):
        return [redact(x) for x in obj]
    return obj


def show(obj, limit=400):
    s = json.dumps(redact(obj), separators=(",", ":"))
    return s if len(s) <= limit else s[:limit] + "...(%d chars)" % len(s)


class WS:
    def __init__(self, name):
        self.name = name
        raw = socket.create_connection((HOST, 443), timeout=15)
        self.s = CTX.wrap_socket(raw, server_hostname=HOST)
        key = base64.b64encode(os.urandom(16)).decode()
        req = ("GET /ws HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
               "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nUser-Agent: %s\r\n\r\n") % (HOST, key, UA)
        self.s.sendall(req.encode())
        head = b""
        while b"\r\n\r\n" not in head:
            chunk = self.s.recv(4096)
            if not chunk:
                raise RuntimeError("%s: connection closed during the handshake" % name)
            head += chunk
        head, self.buf = head.split(b"\r\n\r\n", 1)
        status = head.split(b"\r\n")[0].decode(errors="replace")
        self.tls = self.s.version()
        if " 101 " not in status + " ":
            raise RuntimeError("%s: handshake refused: %s" % (name, status))
        log("%s: connected (%s, %s, no Origin header)" % (name, status, self.tls))
        self.s.settimeout(0.2)
        self.closed = False
        self.inbox = []

    def send(self, obj):
        data = json.dumps(obj).encode()
        hdr = bytearray([0x81])
        n = len(data)
        if n < 126:
            hdr.append(0x80 | n)
        elif n < 65536:
            hdr.append(0x80 | 126)
            hdr += struct.pack(">H", n)
        else:
            hdr.append(0x80 | 127)
            hdr += struct.pack(">Q", n)
        mask = os.urandom(4)
        hdr += mask
        self.s.sendall(bytes(hdr) + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))
        log("%s >> %s" % (self.name, show(obj)))

    def _frame(self):
        while True:
            if len(self.buf) >= 2:
                b0, b1 = self.buf[0], self.buf[1]
                n, off = b1 & 0x7F, 2
                if n == 126 and len(self.buf) >= 4:
                    n, off = struct.unpack(">H", self.buf[2:4])[0], 4
                elif n == 127 and len(self.buf) >= 10:
                    n, off = struct.unpack(">Q", self.buf[2:10])[0], 10
                elif n >= 126:
                    n = None
                if n is not None and not (b1 & 0x80) and len(self.buf) >= off + n:
                    payload, self.buf = self.buf[off:off + n], self.buf[off + n:]
                    return b0 & 0x0F, payload
            try:
                chunk = self.s.recv(65536)
            except (socket.timeout, ssl.SSLWantReadError):
                return None
            if not chunk:
                self.closed = True
                return None
            self.buf += chunk

    def recv_all(self, seconds):
        out, t0 = [], time.time()
        while time.time() - t0 < seconds and not self.closed:
            f = self._frame()
            if f is None:
                continue
            op, payload = f
            if op == 1:
                try:
                    msg = json.loads(payload.decode())
                except ValueError:
                    msg = {"_unparsed": payload[:200].decode(errors="replace")}
                log("%s << %s" % (self.name, show(msg)))
                out.append(msg)
            elif op == 9:  # ping -> pong
                self.s.sendall(bytes([0x8A, 0x80 | len(payload)]) + b"\0\0\0\0" + payload)
                log("%s: ping answered" % self.name)
            elif op == 8:
                code = struct.unpack(">H", payload[:2])[0] if len(payload) >= 2 else None
                log("%s: server closed the connection (code %s)" % (self.name, code))
                self.closed = True
        return out

    def wait(self, mtype, seconds=8):
        """Returns the first queued or new message of mtype; other messages stay queued in order."""
        t0 = time.time()
        while True:
            for i, m in enumerate(self.inbox):
                if m.get("type") == mtype:
                    del self.inbox[i]
                    return m, list(self.inbox)
            if time.time() - t0 >= seconds or self.closed:
                break
            self.inbox += [m for m in self.recv_all(0.3) if m.get("type") not in ("open_games", "stats")]
        raise RuntimeError("%s: no '%s' within %ss (queued: %s)" % (self.name, mtype, seconds,
                                                                  [m.get("type") for m in self.inbox]))

    def drain(self, seconds):
        self.inbox += [m for m in self.recv_all(seconds) if m.get("type") not in ("open_games", "stats")]
        got, self.inbox = self.inbox, []
        return got

    def close(self):
        try:
            self.s.sendall(bytes([0x88, 0x82]) + b"\0\0\0\0" + struct.pack(">H", 1000))
            self.s.close()
        except OSError:
            pass
        log("%s: closed by us" % self.name)


def mode_lobby():
    a = WS("lobby")
    a.send({"type": "lobby_hello"})
    msgs = a.recv_all(4)
    a.close()
    for m in msgs:
        if m.get("type") == "open_games":
            g = m.get("games") or []
            log("open_games: %d listed; fields of the first: %s" % (len(g), sorted(g[0].keys()) if g else "-"))
        if m.get("type") == "stats":
            log("stats fields: %s" % sorted(m.keys()))


def private_pair(minutes=5, increment=0):
    a = WS("A")
    b = WS("B")
    a.send({"type": "create_private", "minutes": minutes, "increment": increment, "color": "white"})
    created, _ = a.wait("private_created")
    b.send({"type": "join_private", "code": created["code"]})
    mb, _ = b.wait("matched")
    ma, _ = a.wait("matched")
    a.wait("state")
    b.wait("state")
    log("matched: game %s, A=%s, B=%s, opponent ids differ from game id: %s" % (
        ma["gameId"], ma["color"], mb["color"], ma.get("opponentId") != ma["gameId"]))
    return a, b, ma, mb


def mode_private():
    a, b, ma, mb = private_pair()
    gid, token = ma["gameId"], ma["reconnectToken"]
    # reconnect: drop A, open a fresh connection and resume with the token
    a.close()
    b.drain(1.5)
    a2 = WS("A2")
    a2.send({"type": "resume", "token": token})
    a2.wait("matched")
    st, _ = a2.wait("state")
    log("resume restored game %s, fen %s" % (st.get("gameId"), st.get("fen")))
    b.drain(1)
    # a rejected move
    a2.send({"type": "move", "gameId": gid, "from": "e2", "to": "e5"})
    print("rejected-move replies:", [m.get("type") for m in a2.drain(1.5)])
    # a quick line that ends in a promotion: 1.e4 d5 2.exd5 c6 3.dxc6 Nf6 4.cxb7 Nbd7 5.bxa8=Q
    line = [("e2", "e4"), ("d7", "d5"), ("e4", "d5"), ("c7", "c6"), ("d5", "c6"), ("g8", "f6"), ("c6", "b7"),
            ("b8", "d7"), ("b7", "a8", "q")]
    for i, mv in enumerate(line):
        who = a2 if i % 2 == 0 else b
        other = b if who is a2 else a2
        msg = {"type": "move", "gameId": gid, "from": mv[0], "to": mv[1]}
        if len(mv) == 3:
            msg["promotion"] = mv[2]
        who.send(msg)
        m1, _ = who.wait("move")
        other.wait("move")
        time.sleep(0.3)
    log("after the promotion: san %s, fen %s, moves %s" % (m1.get("san"), m1.get("fen"), m1.get("moves")))
    a2.send({"type": "resign", "gameId": gid})
    a2.wait("resigned")
    b.wait("resigned")
    a2.close()
    b.close()


def mode_leave():
    a, b, ma, mb = private_pair()
    a.send({"type": "leave_game", "gameId": ma["gameId"]})
    b.wait("opponent_left")
    a.drain(1)
    a.close()
    b.close()


def mode_expired():
    # a private code that nobody joins, then a resume with a token from a finished game
    a, b, ma, mb = private_pair()
    a.send({"type": "resign", "gameId": ma["gameId"]})
    a.wait("resigned")
    b.wait("resigned")
    a.close()
    b.close()
    c = WS("C")
    c.send({"type": "resume", "token": ma["reconnectToken"]})
    c.drain(3)
    c.send({"type": "join_private", "code": "ZZZZZZ"})
    c.drain(3)
    c.close()


class Poll:
    """The server's HTTPS long-poll transport (/api/rt/*), as a stand-in for a WebSocket."""

    def __init__(self, name):
        import urllib.request
        self.name, self.ur, self.ack, self.seq, self.inbox, self.closed = name, urllib.request, 0, 0, [], False
        body = self._post("/api/rt/open", b"")
        self.sid = body["sid"]
        log("%s: poll transport open (sid %d chars)" % (name, len(self.sid)))

    def _req(self, method, path, data=None, timeout=45):
        req = self.ur.Request("https://%s%s" % (HOST, path), data=data, method=method)
        req.add_header("User-Agent", UA)
        if data is not None:
            req.add_header("Content-Type", "text/plain")
        with self.ur.urlopen(req, timeout=timeout, context=CTX) as r:
            return json.loads(r.read().decode() or "{}")

    def _post(self, path, data):
        return self._req("POST", path, data)

    def send(self, obj):
        self.seq += 1
        body = self._post("/api/rt/send?sid=%s&n=%d" % (self.sid, self.seq), json.dumps(obj).encode())
        log("%s >> %s (seq %d, reply %s)" % (self.name, show(obj), self.seq, sorted(body.keys())))

    def poll(self, timeout=30):
        body = self._req("GET", "/api/rt/poll?sid=%s&ack=%d" % (self.sid, self.ack), timeout=timeout)
        for m in body.get("m") or []:
            if m["s"] > self.ack:
                self.ack = m["s"]
                msg = json.loads(m["d"])
                if msg.get("type") not in ("open_games", "stats"):
                    log("%s << %s (seq %d)" % (self.name, show(msg), m["s"]))
                    self.inbox.append(msg)
        if body.get("c"):
            self.closed = True

    def wait(self, mtype, seconds=20):
        t0 = time.time()
        while time.time() - t0 < seconds and not self.closed:
            for i, m in enumerate(self.inbox):
                if m.get("type") == mtype:
                    del self.inbox[i]
                    return m, list(self.inbox)
            self.poll()
        raise RuntimeError("%s: no '%s' (queued %s)" % (self.name, mtype, [m.get("type") for m in self.inbox]))

    def close(self):
        self._post("/api/rt/close?sid=%s" % self.sid, b"")
        log("%s: poll transport closed by us" % self.name)


def mode_poll():
    a = Poll("Apoll")
    b = WS("B")
    a.send({"type": "create_private", "minutes": 5, "increment": 0, "color": "white"})
    created, _ = a.wait("private_created")
    b.send({"type": "join_private", "code": created["code"]})
    mb, _ = b.wait("matched")
    ma, _ = a.wait("matched")
    a.wait("state")
    gid = ma["gameId"]
    a.send({"type": "move", "gameId": gid, "from": "e2", "to": "e4"})
    a.wait("move")
    b.wait("move")
    a.send({"type": "resign", "gameId": gid})
    a.wait("resigned")
    b.wait("resigned")
    a.close()
    b.close()


def mode_partner():
    """Joins a private code shown on the Vita and answers with fixed replies, for a hardware test of our app.
    Usage: cmg_check.py partner CODE [seconds] [offer|leaveN]
    offer: after its first reply the partner asks for a takeback once (to test the app's Accept / Decline).
    leaveN: after N replies, on its next turn, the partner waits 15 s with its clock running and then leaves
    (leave_game), to test the app's clocks when an opponent leaves between moves (1.0.0)."""
    code = sys.argv[2]
    seconds = int(sys.argv[3]) if len(sys.argv) > 3 else 240
    offer = len(sys.argv) > 4 and sys.argv[4] == "offer"
    leave_after = int(sys.argv[4][5:]) if len(sys.argv) > 4 and sys.argv[4].startswith("leave") else -1
    offered = False
    replies = [("e7", "e5"), ("b8", "c6"), ("g8", "f6"), ("f8", "c5"), ("d7", "d6"), ("c8", "g4"), ("a7", "a6"), ("h7", "h6")]
    b = WS("partner")
    b.send({"type": "join_private", "code": code})
    m, _ = b.wait("matched", 15)
    gid, color = m["gameId"], m["color"]
    log("partner plays %s in game %s" % (color, gid))
    t0, turn, k = time.time(), None, 0
    while time.time() - t0 < seconds and not b.closed:
        for msg in b.drain(0.5):
            t = msg.get("type")
            if t in ("state", "move", "takeback_applied"):
                turn = (msg.get("clock") or {}).get("turn")
            if t in ("resigned", "timeout", "opponent_left") or (t == "move" and msg.get("gameOver")):
                log("game over: %s" % t)
                b.close()
                return
            if t == "takeback_offer_received":
                b.send({"type": "takeback_response", "gameId": gid, "accept": True})
            if t not in ("state", "move", "clock", "ping", "pong"):
                log("partner got %s" % show(msg, 200))
        if turn == color and k == leave_after:
            log("partner's turn after %d replies: waiting 15 s, then leave_game" % k)
            time.sleep(15.0)
            b.send({"type": "leave_game", "gameId": gid})
            log("partner sent leave_game")
            time.sleep(1.0)
            b.close()
            return
        if turn == color and k < len(replies):
            time.sleep(1.0)
            b.send({"type": "move", "gameId": gid, "from": replies[k][0], "to": replies[k][1]})
            k += 1
            turn = None
            if offer and not offered and k == 1:
                time.sleep(1.5)
                b.send({"type": "takeback_offer", "gameId": gid})
                offered = True
                log("partner asked for a takeback")
    b.close()


def main():
    modes = {"lobby": mode_lobby, "private": mode_private, "leave": mode_leave, "expired": mode_expired,
             "poll": mode_poll, "partner": mode_partner}
    if len(sys.argv) < 2 or sys.argv[1] not in modes or (sys.argv[1] != "partner" and len(sys.argv) != 2):
        print(__doc__)
        return 2
    rc = 0
    try:
        modes[sys.argv[1]]()
        log("done")
    except Exception as e:  # report the exact stage
        log("STOP: %s: %s" % (type(e).__name__, e))
        rc = 1
    out = os.path.join(os.path.dirname(__file__), "..", "build", "cmg-check")
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "cmg-check-%s-%s.log" % (sys.argv[1], time.strftime("%Y%m%dT%H%M%S"))), "w",
              encoding="utf-8", newline="\n") as f:
        f.write("\n".join(LOG) + "\n")
    return rc


if __name__ == "__main__":
    sys.exit(main())
