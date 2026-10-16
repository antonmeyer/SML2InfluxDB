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

## Beispiel: ESP32-S3 N16R8 (16 MB Flash, 8 MB PSRAM)

Getestet mit dem Entwicklungsboard mit zwei USB-Anschlüssen, Konsole über den **nativen USB-Port**.

- **Umgebungen:** `esp32-s3-n16r8` (Firmware) und `selftest-s3` (UART-Selbsttest) in `platformio.ini`.
- **Boardeinstellungen:** `board = esp32-s3-devkitc-1`, `board_upload.flash_size = 16MB`,
  `board_build.arduino.memory_type = qio_opi` (Quad-Flash, Octal-PSRAM), `board_build.partitions = default_16MB.csv`,
  `-DARDUINO_USB_CDC_ON_BOOT=1` für die Konsole über USB.
- **Pins:** GPIO 26 bis 37 gehören bei der N16R8-Variante dem Flash und dem Octal-PSRAM, 19/20 dem USB. Verwendet werden
  RX Zähler 1 = GPIO4, RX Zähler 2 = GPIO5, Konfig-Pin = GPIO7 (per Build-Flag `-DMETER1_RX_PIN=GPIO_NUM_4` usw., siehe
  `config.h`).
- **UART-Pattern:** Die Funktion `uart_enable_pattern_det_intr` gibt es nur beim klassischen ESP32. `PowerMeter.h`
  nimmt bei anderen Chips `uart_enable_pattern_det_baud_intr` (Zeiten in Bit statt in APB-Takten). Der Selbsttest
  besteht auf dem S3 (142 von 142 Frames, alle Fehlerfälle erholt).
- **OTA:** Ein S3 darf kein Image aus dem OTAdrive-Produkt des ESP32 holen. In der Umgebung ist OTA deshalb abgeschaltet
  (`-DOTA_CHECK_INTERVAL_MS=0`), bis ein eigenes Produkt existiert.
- **Flashen:** `pio run -e esp32-s3-n16r8 -t upload`. Der Port ist ein `usbmodem`-Gerät. Verbindet esptool nicht, BOOT
  halten und EN drücken.

## WLAN ohne Konfigurationsportal (nur Testboards)

Mit `-DTEST_BOARD=1` nimmt die Firmware WLAN-Name und Passwort aus `src/mycredentials.h` (`ssid`, `password`) und
überspringt das Portal. Beispiel:

```
PLATFORMIO_BUILD_FLAGS='-DTEST_BOARD=1 -DDEBUG_FEED_SAMPLE=1' pio run -e esp32-s3-n16r8 -t upload
```

`DEBUG_FEED_SAMPLE=1` speist einen erfundenen Zähler ein, so sieht man den Influx-Pfad ohne angeschlossenen Zähler
(Zeilen `Dry run, not writing: ePower_test,eMeter=FAKE ...`). Beide Flags nie für das echte Gerät verwenden.

## Netzwerkzeit (NTP)

Die Uhr wird über NTP gesetzt (UDP-Port 123, Server `NTP_SERVER1`/`NTP_SERVER2` in `config.h`). Ohne gültige Zeit
laufen weder Datenbank noch OTA, auch nicht im Dry-Run. Sperrt das WLAN NTP, meldet die Konsole
`time not set (NTP blocked? ...)`. Dann einen lokalen Server eintragen, z. B. den Router (`-DNTP_SERVER1=\"fritz.box\"`).

