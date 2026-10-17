#!/usr/bin/env python3
"""Uploads the dashboards of grafana/dashboards/ into one folder of Grafana (default: "Neu (Entwurf)").

Only the dashboards of this directory are touched (their uid is fixed in the JSON). If a dashboard with the same uid
lives in another folder, it is not overwritten. Existing dashboards of other uids are never changed.

  gpush.py [--folder "Neu (Entwurf)"] [--url http://192.168.1.41:3000] [--dry-run]
The token (editor) is read from ~/.config/grafana-token.
"""
import argparse, glob, json, os, urllib.request, urllib.error

ap = argparse.ArgumentParser()
ap.add_argument("--folder", default="Neu (Entwurf)")
ap.add_argument("--url", default="http://192.168.1.41:3000")
ap.add_argument("--dry-run", action="store_true")
a = ap.parse_args()
tok = open(os.path.expanduser("~/.config/grafana-token")).read().strip()

def api(method, path, body=None):
    req = urllib.request.Request(a.url + path, data=json.dumps(body).encode() if body is not None else None, method=method,
                                 headers={"Authorization": "Bearer " + tok, "Content-Type": "application/json"})
    try:
        return json.load(urllib.request.urlopen(req, timeout=60))
    except urllib.error.HTTPError as e:
        return {"_error": e.code, "message": e.read().decode()[:300]}

folders = api("GET", "/api/folders")
fuid = next((f["uid"] for f in folders if f["title"] == a.folder), None)
if not fuid:
    print("folder '%s' does not exist yet%s" % (a.folder, " (dry run)" if a.dry_run else ", creating it"))
    if not a.dry_run:
        r = api("POST", "/api/folders", {"title": a.folder})
        fuid = r["uid"]
        print("  created folder", fuid)

here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "dashboards")
for f in sorted(glob.glob(os.path.join(here, "*.json"))):
    d = json.load(open(f))
    found = api("GET", "/api/dashboards/uid/" + d["uid"])
    if "_error" not in found:
        where = found["meta"].get("folderUid", "")
        if where != fuid:
            print("SKIP %-22s uid exists in another folder (%s), not touched" % (d["uid"], found["meta"].get("folderTitle")))
            continue
        d["version"] = found["dashboard"].get("version", 1)
    if a.dry_run:
        print("would upload", d["uid"], "-", d["title"])
        continue
    r = api("POST", "/api/dashboards/db", {"dashboard": d, "folderUid": fuid, "overwrite": True, "message": "grafana/dashboards from git"})
    print("%-22s %s  %s" % (d["uid"], r.get("status", r), a.url + r.get("url", "")))
