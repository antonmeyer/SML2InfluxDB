#!/usr/bin/env python3
"""End to end test of sml2influx without meter and without InfluxDB (runs on macOS and Linux).

A pseudo terminal plays the serial port: the frames of a capture are written at one frame per second, with some
junk between them. A small HTTP server plays InfluxDB: it answers the first write with 500 (the daemon must retry
and not lose the lines), the second batch with 400 once (the daemon must drop it and go on), the rest with 204.

usage: python3 test_e2e.py ./sml2influx ../../test/smlGHHS_261011.hex [seconds]
"""
import binascii, http.server, os, pty, re, signal, subprocess, sys, tempfile, threading, time, tty

exe, capture = sys.argv[1], sys.argv[2]
seconds = int(sys.argv[3]) if len(sys.argv) > 3 else 100

d = open(capture, "rb").read()
d = binascii.unhexlify(re.sub(rb"\s", b"", d)[: len(re.sub(rb"\s", b"", d)) & ~1]) if re.fullmatch(rb"[0-9a-fA-F\s]+", d) else d
S, E = b"\x1b\x1b\x1b\x1b\x01\x01\x01\x01", b"\x1b\x1b\x1b\x1b\x1a"
frames, i = [], 0
while True:
    s = d.find(S, i)
    e = d.find(E, s + 8) if s >= 0 else -1
    if s < 0 or e < 0 or e + 8 > len(d):
        break
    frames.append(d[s : e + 8])
    i = e + 8

received, answers = [], {"n": 0}


class H(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        body = self.rfile.read(int(self.headers["Content-Length"])).decode()
        answers["n"] += 1
        auth_ok = self.headers.get("Authorization") == "Token testtoken"
        path_ok = self.path.startswith("/api/v2/write?org=testorg&bucket=testbucket&precision=s")
        if not (auth_ok and path_ok):
            code = 401
        elif answers["n"] == 1:
            code = 500
        elif answers["n"] == 2:
            code = 400  # one batch is refused: must be dropped, not retried for ever
        else:
            code = 204
        if code == 204:
            received.extend([l for l in body.split("\n") if l])
        answers.setdefault("codes", []).append(code)
        self.send_response(code)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def log_message(self, *a):
        pass


srv = http.server.HTTPServer(("127.0.0.1", 0), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()

master, slave = pty.openpty()
tty.setraw(master)
conf = tempfile.NamedTemporaryFile("w", suffix=".conf", delete=False)
conf.write(f"""device = {os.ttyname(slave)}
influx_url = http://127.0.0.1:{srv.server_port}
org = testorg
bucket = testbucket
token = testtoken
measurement = ePower_test
alias = GHHS
batch = 5
flush_seconds = 5
""")
conf.close()

proc = subprocess.Popen([exe, "-c", conf.name], stderr=subprocess.PIPE, text=True)
t0 = time.time()
sent = 0
while time.time() - t0 < seconds:
    fr = frames[sent % len(frames)]
    if sent % 17 == 5:
        os.write(master, b"\x00\x55junk")  # noise between two frames
    if sent % 29 == 11:
        os.write(master, fr[:100])  # a frame that is cut off, the next one follows
    os.write(master, fr)
    sent += 1
    time.sleep(1)
proc.send_signal(signal.SIGTERM)
err = proc.communicate(timeout=20)[1]
os.unlink(conf.name)

print("frames written:", sent, " requests:", len(answers.get("codes", [])), "answers:", answers.get("codes"))
print("lines received:", len(received))
for l in received[:3]:
    print("  ", l)
print("daemon log (last lines):")
print("\n".join(err.strip().split("\n")[-6:]))

ok = True
def check(c, msg):
    global ok
    print(("ok   " if c else "FAIL ") + msg)
    ok = ok and c
check(len(received) >= int(seconds / 5) - 4, "about one line per 5 s arrived (%d)" % len(received))
check(all(re.fullmatch(r"ePower_test,eMeter=GHHS (actW=[\d.]+,actWmin=[\d.]+,actWmax=[\d.]+)?,?(sumWh=[\d.]+)?,?(expWh=[\d.]+)? \d{10}", l) or
         re.fullmatch(r"ePower_test,eMeter=GHHS .+ \d{10}", l) for l in received), "line format")
check(answers.get("codes", [0])[0] == 500 and 400 in answers.get("codes", []), "the 500 and the 400 were provoked")
check(not any(c == 401 for c in answers.get("codes", [])), "token and path are right")
stamps = [int(l.split()[-1]) for l in received]
check(stamps == sorted(stamps), "time stamps do not go back")
sys.exit(0 if ok else 1)
