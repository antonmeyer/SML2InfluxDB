# Anderes Board bauen

Der Code ist nicht boardspezifisch. An der Hardware hängen nur die Boarddefinition in `platformio.ini`, die Pins
und die Partitionstabelle. Getestet sind das DoIT ESP32 DevKit V1 (`esp32doit-devkit-v1`) und ein Wemos-32-Board
(ESP32-D0WDQ6, 4 MB Flash). Andere Chips (S2, S3, C3) sind nicht getestet.

## 1. Neue Umgebung in `platformio.ini`

Den Block `[env:esp32doit-devkit-v1]` kopieren, einen neuen Namen vergeben und ändern:

```ini
[env:mein-board]
platform = espressif32@7.1.3
board = <PlatformIO-Boardname>        ; Liste: pio boards esp32
framework = arduino
; lib_deps, lib_ldf_mode und build_flags wie in der Vorlage übernehmen
monitor_speed = 115200
monitor_port = /dev/cu.usbserial-XXXX
upload_port = /dev/cu.usbserial-XXXX
board_build.partitions = min_spiffs.csv   ; nur bei 4 MB Flash, siehe Abschnitt 3
```

Bauen und flashen:

```
pio run -e mein-board
pio run -e mein-board -t upload
```

Den Port kann man auch beim Aufruf angeben: `--upload-port /dev/cu.usbserial-XXXX`.

## 2. Pins

| Was | Wo | Aktuell | Worauf achten |
|---|---|---|---|
| RX Zähler 1 | `METER1_UART`, `METER1_RX_PIN` in `src/config.h` | UART1, GPIO32 | freier, eingangsfähiger Pin |
| RX Zähler 2 | `METER2_UART`, `METER2_RX_PIN` in `src/config.h` | UART2, GPIO16 | **nicht** bei Modulen mit PSRAM (WROVER), dort belegen GPIO16/17 den PSRAM |
| Konfig-Pin | `CONFIG_PIN` in `src/myWebConf.h` | GPIO33 | gegen GND beim Start: AP mit Initialpasswort |
| Status-LED | `STATUS_PIN` (`LED_BUILTIN`) in `src/myWebConf.h` | boardabhängig | Polarität in `setStatusPin(STATUS_PIN, HIGH)`: manche Boards leuchten bei `LOW` |

Zu vermeiden: GPIO 6 bis 11 (Flash), die Strapping-Pins 0, 2, 12 und 15, und GPIO1/3 (UART0, die Konsole).
Die GPIO 34 bis 39 sind nur Eingänge, für den RX-Pin aber geeignet.

## 3. Weitere Punkte

- **Flash-Größe:** `min_spiffs.csv` setzt 4 MB Flash mit zwei App-Slots für OTA voraus. Die Firmware ist rund
  1,07 MB groß und muss in einen Slot passen. Bei 2 MB Flash wird eine andere Partitionstabelle gebraucht.
- **Anderer Chip (S2, S3, C3):** Anzahl der UARTs und Pins unterscheiden sich. Beim S3 mit nativem USB braucht die
  Konsole die Flags für USB-CDC. Das muss am Selbsttest geprüft werden (Abschnitt 4).
- **Selbsttest:** In `[env:selftest]` ebenfalls `board` anpassen, sonst wird das falsche Board getestet.
  `[env:native]` (Host-Tests) ist vom Board unabhängig.
- **OTAdrive:** Ein Image gilt nur für den Chip, für den es gebaut wurde. Für ein anderes Board ein eigenes
  Produkt oder zumindest eine eigene Gruppe anlegen, damit ein Gerät nie die falsche `.bin` bekommt.
- **Bibliotheken:** Plattform und Bibliotheken sind in `platformio.ini` auf getestete Versionen festgelegt. Bei einem
  anderen Board nur dann ändern, wenn es sein muss.

## 4. So prüft man es

1. `pio run -e mein-board` muss ohne Fehler bauen.
2. Selbsttest auf diesem Board, noch ohne Zähler:
   ```
   pio run -e selftest -t upload --upload-port <Port>
   pio device monitor -e selftest
   ```
   Am Ende muss `PASS: no loss, no wrong data` stehen. Das testet den UART-Empfang per internem Loopback.
3. Mit dem Zähler verdrahten, in der Dry-Run-Einstellung (`INFLUX_DRY_RUN=1`, Standard) starten und auf der Konsole
   kontrollieren: `meterid: ...` mit dem erwarteten Alias und Werten, `alive: ...` jede Minute.
4. Erst danach `INFLUX_DRY_RUN=0` setzen.

Beschreibung der Build-Flags und Tests: `README.md` und `test/README`.
