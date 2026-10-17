#!/usr/bin/env python3
"""Runs every panel query (and the variable queries) of a dashboard JSON through the Grafana API (/api/ds/query),
the way Grafana would run them, and prints rows or the error. Read only.

  gcheck.py dashboards/strom-uebersicht.json [--house GH|VH] [--from now-30d] [--url http://192.168.1.41:3000]
The token is read from ~/.config/grafana-token.
"""
import argparse, json, os, re, sys, time, urllib.request

ap = argparse.ArgumentParser()
ap.add_argument("dashboard")
ap.add_argument("--house", default="GH")
ap.add_argument("--days", type=float, default=30)
ap.add_argument("--url", default="http://192.168.1.41:3000")
ap.add_argument("--meters", default="", help="regex for eMeter, default: all")
a = ap.parse_args()

tok = open(os.path.expanduser("~/.config/grafana-token")).read().strip()
def api(path, body=None):
    req = urllib.request.Request(a.url + path, data=json.dumps(body).encode() if body else None,
                                 headers={"Authorization": "Bearer " + tok, "Content-Type": "application/json"})
    try:
        return json.load(urllib.request.urlopen(req, timeout=120))
    except urllib.error.HTTPError as e:
        return {"error": e.read().decode()[:600]}

d = json.load(open(a.dashboard))
bucket = {"GH": "bktRP4GH", "VH": "cloudbktcp"}[a.house]
vals = {"bucket": bucket, "eMeter": a.meters or ".*"}
dsuid = "5YkhJsqMz"
for v in d["templating"]["list"]:
    if v["name"] in ("ds", "bucket", "eMeter"):
        continue
    cur = v.get("current", {}).get("value", v.get("query"))
    vals[v["name"]] = cur[0] if isinstance(cur, list) else cur

def subst(s):
    for k, v in vals.items():
        s = s.replace("${%s:regex}" % k, v).replace("${%s}" % k, v)
    return s

now = int(time.time() * 1000)
frm, to = now - int(a.days * 86400000), now
win = max(60000, int((to - frm) / 800))

def run(flux):
    r = api("/api/ds/query", {"queries": [{"refId": "A", "datasource": {"uid": dsuid, "type": "influxdb"}, "query": subst(flux),
                                           "intervalMs": win, "maxDataPoints": 800}], "from": str(frm), "to": str(to)})
    if "error" in r:
        return None, r["error"]
    res = r["results"]["A"]
    if res.get("error"):
        return None, res["error"]
    frames = res.get("frames", [])
    rows = sum(len(f["data"]["values"][0]) if f["data"]["values"] else 0 for f in frames)
    names = []
    for f in frames[:6]:
        fl = f["schema"]["fields"]
        names.append(",".join((x.get("name", "") + ("[" + ",".join(f"{k}={v}" for k, v in (x.get("labels") or {}).items()) + "]" if x.get("labels") else "")) for x in fl[1:3]))
    return (len(frames), rows, names), None

ok = True
print(f"== {d['title']}  house={a.house} bucket={bucket}  last {a.days:g} days")
for v in d["templating"]["list"]:
    if v["type"] == "query":
        res, err = run(v["query"])
        print(f"  variable {v['name']:8s}: " + (f"{res[1]} values" if res else "ERROR " + str(err)[:300]))
        ok = ok and res is not None
for p in d["panels"]:
    for t in p.get("targets", []):
        res, err = run(t["query"])
        if res:
            fr, rows, names = res
            print(f"  [{p['type']:10s}] {p['title'][:46]:46s} {fr:2d} frames {rows:6d} rows   {'; '.join(names)[:90]}")
        else:
            ok = False
            print(f"  [{p['type']:10s}] {p['title'][:46]:46s} ERROR {str(err)[:400]}")
sys.exit(0 if ok else 1)
