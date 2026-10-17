#!/usr/bin/env python3
"""Builds the Grafana dashboards (JSON) in grafana/dashboards/ from the Flux files in grafana/flux/.

  python3 grafana/tools/build_dashboards.py

The JSON files are what is imported into Grafana (tools/gpush.py) and what is kept in git.
Everything is defined here, so a change of a query or of a variable is one change in one place.
"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fluxlib import expand

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
DS = {"type": "influxdb", "uid": "${ds}"}
UID_PROXMOX = "5YkhJsqMz"

# ---- variables -------------------------------------------------------------------------------------------------
def v_ds():
    return {"name": "ds", "label": "Datenquelle", "type": "datasource", "query": "influxdb", "regex": "/InfluxDB_proxmox/",
            "current": {"selected": True, "text": "InfluxDB_proxmox", "value": UID_PROXMOX}, "hide": 2, "options": []}

def v_house(default="GH"):
    opts = [{"text": "GH", "value": "bktRP4GH", "selected": default == "GH"}, {"text": "VH", "value": "cloudbktcp", "selected": default == "VH"}]
    cur = opts[0] if default == "GH" else opts[1]
    return {"name": "bucket", "label": "Haus", "type": "custom", "query": "GH : bktRP4GH, VH : cloudbktcp",
            "current": {"selected": True, "text": cur["text"], "value": cur["value"]}, "options": opts, "multi": False, "includeAll": False}

def v_meter(regex="", label="Zähler"):
    q = """import "influxdata/influxdb/schema"
new = schema.tagValues(bucket: "${bucket}", tag: "eMeter", start: -400d)
old = schema.tagValues(bucket: "${bucket}", tag: "meter", start: -400d)
    |> map(fn: (r) => ({r with _value: if r._value == "WPGH" then "GHWP" else if r._value == "HSGH" then "GHHS" else r._value}))
union(tables: [new, old])
    |> group()
    |> distinct()
"""
    v = {"name": "eMeter", "label": label, "type": "query", "datasource": DS, "query": q, "refresh": 1, "multi": True, "includeAll": True,
         "allValue": ".*", "current": {"selected": True, "text": ["All"], "value": ["$__all"]}, "options": [], "sort": 1}
    if regex:
        v["regex"] = regex
        v.pop("allValue", None)  # All = all options that pass the filter
    return v

def v_const(name, value):
    return {"name": name, "type": "constant", "query": value, "hide": 2, "current": {"text": value, "value": value}, "options": [{"text": value, "value": value, "selected": True}]}

def v_custom(name, label, options, default):
    opts = [{"text": t, "value": val, "selected": val == default} for t, val in options]
    cur = [o for o in opts if o["selected"]][0]
    return {"name": name, "label": label, "type": "custom", "query": ", ".join(f"{t} : {val}" for t, val in options),
            "current": {"selected": True, "text": cur["text"], "value": cur["value"]}, "options": opts}

def v_text(name, label, value):
    return {"name": name, "label": label, "type": "textbox", "query": value, "current": {"text": value, "value": value}, "options": [{"text": value, "value": value, "selected": True}]}

COMMON_CONSTS = [v_const("maxDayWh", "480000"), v_const("maxPowerW", "25000"), v_const("nowWindow", "10m"),
                 v_const("runWindow", "1m"), v_const("runMinutes", "1")]

# ---- panels ---------------------------------------------------------------------------------------------------
class Board:
    def __init__(self, uid, title, description, tags, time_from="now-30d"):
        self.uid, self.title, self.description, self.tags, self.time_from = uid, title, description, tags, time_from
        self.panels, self.y, self.x, self.rowh, self.nid = [], 0, 0, 0, 1

    def _place(self, w, h):
        if self.x + w > 24:
            self.y += self.rowh
            self.x, self.rowh = 0, 0
        pos = {"x": self.x, "y": self.y, "w": w, "h": h}
        self.x += w
        self.rowh = max(self.rowh, h)
        return pos

    def row(self, title):
        if self.x:
            self.y += self.rowh
            self.x, self.rowh = 0, 0
        self.panels.append({"id": self.nid, "type": "row", "title": title, "collapsed": False, "gridPos": {"x": 0, "y": self.y, "w": 24, "h": 1}, "panels": []})
        self.nid += 1
        self.y += 1

    def panel(self, kind, title, queries, w, h, unit=None, description="", options=None, overrides=None, custom=None, thresholds=None, decimals=None, extra=None, min_interval=None):
        targets = []
        for i, q in enumerate(queries):
            flux = expand(os.path.join(ROOT, "flux", q if q.endswith(".flux") else q + ".flux"))
            targets.append({"refId": chr(65 + i), "datasource": DS, "query": flux})
        defaults = {}
        if unit:
            defaults["unit"] = unit
        if decimals is not None:
            defaults["decimals"] = decimals
        if custom:
            defaults["custom"] = custom
        if thresholds:
            defaults["thresholds"] = {"mode": "absolute", "steps": thresholds}
        p = {"id": self.nid, "type": kind, "title": title, "description": description, "datasource": DS, "targets": targets,
             "gridPos": self._place(w, h), "fieldConfig": {"defaults": defaults, "overrides": overrides or []}, "options": options or {}}
        if min_interval:
            p["interval"] = min_interval
        if extra:
            p.update(extra)
        self.nid += 1
        self.panels.append(p)

    def build(self, variables):
        return {"uid": self.uid, "title": self.title, "description": self.description, "tags": self.tags, "timezone": "browser",
                "schemaVersion": 39, "version": 1, "editable": True, "graphTooltip": 1, "refresh": "", "time": {"from": self.time_from, "to": "now"},
                "templating": {"list": variables}, "annotations": {"list": []}, "panels": self.panels, "links": []}

TS = {"drawStyle": "line", "lineWidth": 1, "fillOpacity": 8, "showPoints": "never", "spanNulls": True}
BARS = {"drawStyle": "bars", "lineWidth": 1, "fillOpacity": 80, "showPoints": "never"}
LEGEND = {"legend": {"displayMode": "table", "placement": "bottom", "calcs": ["mean", "max"]}, "tooltip": {"mode": "multi", "sort": "desc"}}
LEGEND_SMALL = {"legend": {"displayMode": "list", "placement": "bottom"}, "tooltip": {"mode": "multi", "sort": "desc"}}
STAT = {"reduceOptions": {"values": False, "calcs": ["lastNotNull"], "fields": ""}, "textMode": "value_and_name", "colorMode": "none", "graphMode": "none", "orientation": "auto"}
BAND = [{"matcher": {"id": "byRegexp", "options": ".*actWm(in|ax).*"}, "properties": [{"id": "custom.lineWidth", "value": 1}, {"id": "custom.lineStyle", "value": {"fill": "dash", "dash": [8, 6]}}, {"id": "custom.fillOpacity", "value": 0}]}]

# ---- dashboard 1: electricity overview ---------------------------------------------------------------------------------
def strom():
    b = Board("strom-uebersicht", "Strom – Übersicht GH / VH",
              "Leistung, Zählerstände und Energie aller Zähler eines Hauses. Neues Schema (ePower, eMeter), die alten Daten von vzlogger (ePowerWP) werden automatisch einbezogen. Zählerwechsel werden bei der Energie nicht mitgezählt.",
              ["neu", "strom"])
    b.row("Jetzt")
    b.panel("stat", "Aktuelle Leistung", ["power_now"], 8, 5, unit="watt", decimals=0, options=STAT,
            description="Mittel der letzten 10 Minuten. actW: vom Zähler gemessen. actW_calc: aus dem Zählerstand berechnet (Zähler ohne Leistungswert, z. B. Landis+Gyr ohne PIN).")
    b.panel("stat", "Energie im gewählten Zeitraum", ["energy_total"], 8, 5, unit="watth", decimals=0, options=STAT,
            description="Summe der Tagesenergien im Zeitraum. Zählerwechsel und Zählerrücksprünge (negativ oder über 20 kW × 24 h) werden nicht mitgezählt.")
    b.panel("table", "Letzte Daten je Zähler", ["last_seen"], 8, 5, description="Wann hat jeder Zähler zuletzt etwas gesendet (age_min = Minuten). Die Daten auf Proxmox sind bis zu einer Stunde alt (Kopier-Task).",
            overrides=[{"matcher": {"id": "byName", "options": "age_min"}, "properties": [{"id": "unit", "value": "m"}, {"id": "decimals", "value": 0},
                       {"id": "thresholds", "value": {"mode": "absolute", "steps": [{"color": "green", "value": None}, {"color": "orange", "value": 90}, {"color": "red", "value": 240}]}},
                       {"id": "custom.cellOptions", "value": {"type": "color-text"}}]}])
    b.row("Leistung")
    b.panel("timeseries", "Leistung (Mittel, min/max)", ["power_timeseries"], 24, 9, unit="watt", options=LEGEND, custom=TS, overrides=BAND,
            description="actW = Mittel je Fenster. actWmin/actWmax (gestrichelt) gibt es nur für Daten ab dem neuen Format (v1.3).")
    b.panel("timeseries", "Leistung aus dem Zählerstand (berechnet)", ["power_from_energy"], 24, 8, unit="watt", options=LEGEND, custom=TS, min_interval="5m",
            description="Aus dem Zählerstand berechnet. Fenster = Grafana-Intervall, mindestens 5 Minuten (≈ ±1,2 W Auflösung bei 0,1 Wh Zählerauflösung, 1 Minute wären ≈ ±6 W).")
    b.row("Energie")
    b.panel("timeseries", "Energie je Tag / Monat", ["energy_per_period"], 24, 8, unit="watth", options=LEGEND_SMALL, custom=BARS,
            description="Tag oder Monat in lokaler Zeit (Europe/Berlin). Differenz des letzten Zählerstands von Tag zu Tag, Zählerwechsel werden nicht mitgezählt.")
    b.panel("timeseries", "Zählerstand", ["energy_counter"], 24, 7, unit="watth", options=LEGEND_SMALL, custom=TS, description="Rohe Zählerstände (sumWh). Der Sprung beim Zählerwechsel ist hier sichtbar.")
    vs = [v_ds(), v_house("GH"), v_meter(), v_custom("period", "Energie je", [("Tag", "1d"), ("Monat", "1mo")], "1d")] + COMMON_CONSTS
    return b.build(vs)

# ---- dashboard 2: heat pump --------------------------------------------------------------------------------------------
def wp():
    b = Board("waermepumpe", "Wärmepumpe GH / VH",
              "Läufe, Laufzeit und Energie der Wärmepumpen. Ein Lauf ist ein Zeitfenster, in dem die mittlere Leistung über der Schwelle liegt. Die Leistung wird aus dem Zählerstand berechnet, so funktioniert es auch ohne Leistungswert vom Zähler.",
              ["neu", "waermepumpe"])
    b.row("Jetzt")
    b.panel("stat", "Aktuelle Leistung", ["power_now"], 6, 5, unit="watt", decimals=0, options=STAT)
    b.panel("stat", "Energie im Zeitraum", ["energy_total"], 6, 5, unit="watth", decimals=0, options=STAT)
    b.panel("timeseries", "Außentemperatur (Tagesmittel, DWD)", ["temperature_daily"], 12, 5, unit="celsius", options=LEGEND_SMALL, custom=TS)
    b.row("Läufe")
    b.panel("timeseries", "Laufzeit je Tag", ["wp_runtime_per_day"], 12, 8, unit="h", options=LEGEND_SMALL, custom=BARS,
            description="Stunden pro Tag, in denen die mittlere Leistung (1-Minuten-Fenster) über der Schwelle (Variable 'Schwelle') liegt.")
    b.panel("timeseries", "Starts je Tag", ["wp_starts_per_day"], 12, 8, options=LEGEND_SMALL, custom=BARS, decimals=0,
            description="Anzahl der Wechsel von aus nach ein (1-Minuten-Fenster).")
    b.row("Leistung und Energie")
    b.panel("timeseries", "Leistung (berechnet aus dem Zählerstand)", ["power_from_energy"], 24, 8, unit="watt", options=LEGEND, custom=TS, min_interval="5m")
    b.panel("timeseries", "Energie je Tag / Monat", ["energy_per_period"], 24, 8, unit="watth", options=LEGEND_SMALL, custom=BARS)
    vs = [v_ds(), v_house("GH"), v_meter(regex="/WP$/", label="Wärmepumpe"), v_custom("period", "Energie je", [("Tag", "1d"), ("Monat", "1mo")], "1d"), v_text("onW", "Schwelle (W)", "1000")] + COMMON_CONSTS
    return b.build(vs)

# ---- dashboard 3: counters and devices --------------------------------------------------------------------------------
def geraete():
    b = Board("zaehler-geraete", "Zähler & Geräte", "Zählerstände am Tagesende (Ersatz für 'Livius Numbers') und Zustand der ESP32-Geräte.", ["neu", "zaehler"], time_from="now-14d")
    b.row("Zähler")
    b.panel("table", "Zählerstand am Tagesende", ["counter_daily"], 24, 10, unit="watth", decimals=1, description="Letzter Zählerstand je Tag in lokaler Zeit, ein Zähler je Spalte.")
    b.panel("table", "Letzte Daten je Zähler", ["last_seen"], 24, 6, description="Wann hat jeder Zähler zuletzt etwas gesendet.")
    b.row("ESP32 eMonVH (nur VH)")
    b.panel("timeseries", "ESP32: Heap, WLAN, Neustarts, Fehler", ["esp_status"], 24, 9, options=LEGEND, custom=TS,
            description="Fester Bucket cloudbktcp. Die Felder heapFree, heapMaxBlock, resetReason, offlineRestarts, writeErrors, lastStatus gibt es erst ab der Firmware 1.3.")
    vs = [v_ds(), v_house("GH"), v_meter()] + COMMON_CONSTS
    return b.build(vs)

if __name__ == "__main__":
    os.makedirs(os.path.join(ROOT, "dashboards"), exist_ok=True)
    for name, fn in [("strom-uebersicht", strom), ("waermepumpe", wp), ("zaehler-geraete", geraete)]:
        d = fn()
        with open(os.path.join(ROOT, "dashboards", name + ".json"), "w") as f:
            json.dump(d, f, indent=2, ensure_ascii=False)
            f.write("\n")
        print("wrote dashboards/%s.json: %d panels" % (name, len([p for p in d["panels"] if p["type"] != "row"])))
