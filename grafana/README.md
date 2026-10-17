# Grafana-Dashboards GH / VH

Neue Dashboards für das einheitliche Datenschema (Messung `ePower`, Tag `eMeter`, Felder `actW`, `actWmin`, `actWmax`,
`sumWh`, `expWh`), gemeinsam für das Gartenhaus (GH) und das Vorderhaus (VH). Die alten Daten von vzlogger
(`ePowerWP`, Tags `meter`/`mptype`, Feld `value`) werden von den Abfragen automatisch auf das neue Schema abgebildet.

| Datei | Inhalt |
|---|---|
| `flux/*.flux` | Die Abfragen. Eine Datei pro Auswertung, mit Beschreibung und den benötigten Variablen. |
| `flux/_source.part`, `flux/_wp_state.part` | Gemeinsame Bausteine, werden mit `//@include` eingefügt. |
| `dashboards/*.json` | Die Dashboards (erzeugt, nicht von Hand ändern) |
| `original/*.json` | Sicherung der drei bisherigen Dashboards (Stand 2026-10-17) |
| `tools/build_dashboards.py` | erzeugt `dashboards/*.json` aus den Abfragen |
| `tools/runq.py` | führt eine Abfrage gegen die Pi- oder Proxmox-InfluxDB aus (nur lesend), ohne Grafana |
| `tools/gcheck.py` | führt alle Panel- und Variablenabfragen eines Dashboards über die Grafana-Schnittstelle aus |
| `tools/gpush.py` | lädt die Dashboards in den Grafana-Ordner "Neu (Entwurf)" (ändert nur diese drei uid) |

```
python3 grafana/tools/build_dashboards.py                       # Dashboards erzeugen
python3 grafana/tools/gcheck.py grafana/dashboards/strom-uebersicht.json --house GH --days 30   # prüfen
python3 grafana/tools/gpush.py                                   # hochladen
```
Der Grafana-Token (Service-Konto, Editor) liegt in `~/.config/grafana-token` (Rechte 600), nicht im Projekt.

## Dashboards
- **Strom – Übersicht GH / VH** (`strom-uebersicht`): aktuelle Leistung, Energie im Zeitraum, letzte Daten je Zähler, Leistung
  (Mittel mit min/max), Leistung aus dem Zählerstand, Energie je Tag/Monat, Zählerstand.
- **Wärmepumpe GH / VH** (`waermepumpe`): Laufzeit und Starts je Tag, Energie, berechnete Leistung, Außentemperatur.
- **Zähler & Geräte** (`zaehler-geraete`): Zählerstand am Tagesende (Ersatz für "Livius Numbers"), letzte Daten, Status des ESP32.

Variablen: Datenquelle `InfluxDB_proxmox` (versteckt), **Haus** (GH = Bucket `bktRP4GH`, VH = `cloudbktcp`), **Zähler**
(aus den Daten), Energie je Tag/Monat, Schwelle der Wärmepumpe (W).

## Zählerwechsel
Energie wird nie aus dem rohen Zählerstand (`increase()`) berechnet, sondern aus Tagesdifferenzen. Ein Schritt, der negativ
oder größer als ein Zähler in einem Tag liefern kann ist (`maxDayWh` = 480 000 Wh = 20 kW × 24 h), gilt als Zählerwechsel oder
Zählerreset und wird nicht mitgezählt. Beispiel GH-Wärmepumpe: Am 15.07.2026 springt der Zähler um -77 091 379 Wh (alter
Zähler ~77 MWh, neuer Landis+Gyr nahe 0), davor fehlen vom 3. bis 14. Juli alle Daten. Die Abfragen zählen das nicht.

## Wichtig beim Schreiben von Abfragen (InfluxDB 2.x)
- **Kein `option location`**: Mit einer Zeitzone macht InfluxDB `max`/`min`/`first`/`last` pro Fenster nicht mehr im Speicher
  (22 s statt 0,3 s für 30 Tage). Die Fenster sind UTC (stündlich), für Tage und Monate wird die Zeitzone nur der zweiten
  Aggregation mitgegeben: `aggregateWindow(..., location: tz)`.
- **Zählerstände mit `max` statt `last` je Fenster** aggregieren (sie wachsen nur), Mittelwerte mit `mean`.
- **Zwischen Filter und Aggregation nichts einfügen**, und Filter einfach halten (Vergleich von `r` mit Konstanten), sonst liest
  InfluxDB den ganzen Bucket.
- Zeitreihen über 30 Tage mit `v.windowPeriod`, nicht mit festem Fenster, sonst überschreitet Grafana 8001 Punkte.

## Stand und offen
- GH-Daten im neuen Format (`ePower` des Pi) kommen erst bei Proxmox an, wenn der Task `copyFromRP4GH` die Messung `ePower`
  mitkopiert (heute nur `ePowerWP` und `wpgh_runs_from_total`). Bis dahin zeigen die GH-Panels die alten Daten bis 17.10.2026 11:43.
- VH: `VHWP` fehlt seit dem Zählerwechsel (alter ESP32 kann den Zähler nicht lesen), bis die neue Firmware aufgespielt ist.
- Gas, "Deckungsbeitrag Umweltwärme", Histogramme der Laufzeiten und die Statistik-Tasks sind noch nicht übernommen.
