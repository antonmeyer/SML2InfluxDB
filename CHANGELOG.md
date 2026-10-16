# Changelog

## 1.3.0.0 (2026-10) - ACHTUNG: Messwerte in der Datenbank haben sich wesentlich geändert

Wer Dashboards, Abfragen oder Alarme auf die Daten gebaut hat, muss sie prüfen.

### Was sich bei den Messwerten geändert hat

| Thema | Vorher | Jetzt |
|---|---|---|
| Menge | ein Punkt pro Frame (jede 1 bis 2 s) | ein Punkt pro 5-s-Fenster; Energie nur bei Änderung (höchstens alle 5 s), spätestens alle 30 s (`SEND_INTERVAL_S`, `SEND_HEARTBEAT_S`) |
| `actW` | Momentanwert des letzten Frames | **Mittelwert** über das 5-s-Fenster |
| `actWmin`, `actWmax` | gab es nicht | **neu**: Minimum und Maximum im Fenster |
| `sumWh` | in jedem Punkt | nur wenn geändert oder nach 30 s; letzter Wert |
| `expWh` (Einspeisung, `1-0:2.8.0`) | gab es nicht | **neu**, wenn der Zähler es liefert |
| fehlende Werte | wurden als **0** geschrieben (LGZ ohne PIN: `actW=0`) | das Feld **fehlt**, nichts wird erfunden |
| Zeitstempel | Uhr des ESP32 beim Senden | **Uhr des Zählers** plus festem Offset: die Abstände sind exakt, auch wenn das Gerät kurz blockiert war |
| Zähler-Tag `eMeter` | Alias aus Tabelle, bei unbekannter ID leer/falsch | Alias aus Tabelle, sonst Alias des Anschlusses, sonst die ID; neue LGZ-Zähler (GHWP, VHWP) sind eingetragen |
| Messung (Standard) | `ePower`, `ESP32` | `ePower_test`, `ESP32_test` (Dry-Run ist Standard), siehe `config.h` |
| Status-Punkt `ESP32` | `rssi`, `heap` | zusätzlich `heapFree`, `heapMaxBlock`, `resetReason`, `offlineRestarts`, `writeErrors`, `lastStatus`; Tag `device` aus `DEVICE_NAME` |

Die Zählerzeit wird **nicht** als eigenes Feld geschrieben, sie steckt nur im Zeitstempel.

### Leistung aus der Energie (z. B. LGZ ohne PIN)
Der Zähler liefert ohne PIN keine Wirkleistung. Sie lässt sich in der Datenbank für jedes Fenster aus `sumWh`
berechnen, die Genauigkeit begrenzt die Auflösung von 0,1 Wh (bei 1 min ca. ±6 W):

```flux
from(bucket: "...") |> range(start: -6h)
  |> filter(fn: (r) => r._measurement == "ePower_test" and r._field == "sumWh")
  |> aggregateWindow(every: 1m, fn: last, createEmpty: false)
  |> derivative(unit: 1h, nonNegative: true)
```

### Weitere Änderungen
- Decoder für Landis+Gyr (LGZ) mit anderen OBIS-Codes, CRC-Prüfung der Frames, Tabelle statt Kette.
- UART-Empfang robust gegen Rauschen, abgeschnittene Frames und blockierte `loop()`; größere Puffer.
- Watchdog, Neustart nach 10 min ohne WLAN, kein Warten auf WLAN in `setup()`.
- OTAdrive wieder aktiv: 6 h statt 6 min, aktuelle Root-CA, installiert nur eine **neuere** Version.
- ESP32-S3 N16R8 als Testboard, `TEST_BOARD`/`DEBUG_FEED_SAMPLE` für Tests ohne Zähler.
- Tests: `pio test -e native` (Decoder, Aggregation), `pio run -e selftest` (UART-Pfad auf dem ESP32).

Vorgängerversion: 1.0.0.8.
