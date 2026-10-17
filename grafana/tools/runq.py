#!/usr/bin/env python3
"""Runs a Flux query of grafana/flux/ against a real InfluxDB (read only) and prints a short result.

The queries are written for Grafana: they use v.timeRangeStart / v.timeRangeStop / v.windowPeriod and Grafana
variables like ${bucket}. This script defines `v` and replaces the ${...} variables, so the query can be tested
without Grafana.

  runq.py --target pi|proxmox --start 2026-06-15T00:00:00Z --stop 2026-07-20T00:00:00Z \
          --var bucket=bktWP --var eMeter='GHWP|GHHS' [--window 5m] [--rows 40] [--cols _time,eMeter,_field,_value] query.flux

target pi:      the InfluxDB on the Raspberry Pi (through ssh, influx CLI of the user pi)
target proxmox: the InfluxDB on Proxmox (influx CLI config "proxmox" of this Mac)
"""
import argparse, csv, re, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("query")
ap.add_argument("--target", default="proxmox", choices=["pi", "proxmox"])
ap.add_argument("--start", required=True)
ap.add_argument("--stop", required=True)
ap.add_argument("--window", default="5m")
ap.add_argument("--var", action="append", default=[])
ap.add_argument("--rows", type=int, default=40)
ap.add_argument("--cols", default="_time,eMeter,_field,_value")
ap.add_argument("--show", action="store_true", help="print the final query")
a = ap.parse_args()

import os
def expand(path):
    out = []
    for line in open(path).read().split("\n"):
        m = re.match(r"\s*//@include\s+(\S+)", line)
        if m:
            out.append(open(os.path.join(os.path.dirname(path), m.group(1) + ".part")).read())
        else:
            out.append(line)
    return "\n".join(out)

flux = expand(a.query)
vals = dict(v.split("=", 1) for v in a.var)
for k, v in vals.items():
    flux = flux.replace("${%s}" % k, v).replace("${%s:regex}" % k, v)
left = re.findall(r"\$\{[^}]+\}", flux)
if left:
    sys.exit("variables without value: " + ", ".join(sorted(set(left))))
head = f'option v = {{timeRangeStart: {a.start}, timeRangeStop: {a.stop}, windowPeriod: {a.window}, defaultBucket: "{vals.get("bucket", "")}"}}\n'
imports = [l for l in flux.split("\n") if l.startswith("import ")]
body = [l for l in flux.split("\n") if not l.startswith("import ")]
flux = "\n".join(sorted(set(imports))) + "\n" + head + "\n".join(body)
if a.show:
    print(flux)

cmd = ["influx", "query", "--raw", "--file", "/dev/stdin"]
if a.target == "pi":
    cmd = ["ssh", "-o", "BatchMode=yes", "pi@100.98.118.40", "influx query --raw --file /dev/stdin"]
r = subprocess.run(cmd, input=flux, capture_output=True, text=True)
err = "\n".join(l for l in r.stderr.split("\n") if "post-quantum" not in l and "store now" not in l and "openssh.com/pq" not in l and l.strip())
if err:
    print("ERROR:", err[:800])
    if not r.stdout.strip():
        sys.exit(1)
tables = []
for blk in r.stdout.split("\n\n"):
    lines = [l for l in blk.split("\n") if l and not l.startswith("#")]
    if len(lines) > 1:
        rd = list(csv.reader(lines))
        tables.append((rd[0], rd[1:]))
cols = a.cols.split(",")
n = 0
for hdr, rows in tables:
    idx = [hdr.index(c) for c in cols if c in hdr]
    for row in rows:
        if n < a.rows:
            print("  ".join((row[i][:19] if hdr[i] == "_time" else row[i][:14]).ljust(20 if hdr[i] == "_time" else 12) for i in idx))
        n += 1
print(f"-- {n} rows in {len(tables)} table(s)")
