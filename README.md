# SML2InfluxDB

ESP32 firmware that reads SML power meters with an optical reader on the UART, decodes the SML frames and
writes actual power and energy to an InfluxDB. WLAN setup via IotWebConf (config portal), firmware updates via
OTAdrive.

Supported meters (OBIS variants in `src/MeterData.h`): Iskra and EMH eHZ (`129-129:199.130.3`, `1-0:0.0.9`),
Landis+Gyr LGZ (`1-0:96.50.1`, `1-0:96.1.0`). Note: the LGZ sends the actual power (`1-0:16.7.0`) only after the PIN
was entered on the meter; without it the firmware reports energy only.

## What is written to InfluxDB

The meters send a frame every 1 to 2 seconds. `src/SampleAggregator.h` reduces that (details in the file header):

| Field | Meaning |
|---|---|
| `actW`, `actWmin`, `actWmax` | power: mean, min, max of a 5 s window (only if the meter sends it) |
| `sumWh`, `expWh` | energy counters, the last value; written when changed (at most every 5 s), at least every 30 s |

Tag `eMeter` is the alias of the meter. The time stamp of a point comes from the clock of the meter plus a constant
offset, so the distances between points are exact even if the device was busy. Power for meters without it (LGZ
without PIN) is calculated later in the database from the energy, for any window:

```flux
from(bucket: "...") |> range(start: -6h)
  |> filter(fn: (r) => r._measurement == "ePower_test" and r._field == "sumWh")
  |> aggregateWindow(every: 1m, fn: last, createEmpty: false)
  |> derivative(unit: 1h, nonNegative: true)
```

## Build

```
cp src/mycredentials.h.example src/mycredentials.h    # and fill in
pio run -e esp32doit-devkit-v1
```

Settings (pins, intervals, names) are in `src/config.h`, most of them can be set with build flags:

| Flag | Default | Meaning |
|---|---|---|
| `INFLUX_DRY_RUN` | `1` | `1`: only print the line protocol, send nothing |
| `INFLUX_MEASUREMENT` | `ePower_test` | measurement name; use a separate one for tests |
| `OTA_CHECK_INTERVAL_MS` | 6 h | OTAdrive check interval, `0` = off |
| `OFFLINE_RESTART_MS` | 10 min | restart when offline this long, `0` = off |
| `SEND_INTERVAL_S` | `5` | window for the data reduction, `0` = every frame is a point |
| `SEND_HEARTBEAT_S` | `30` | an unchanged energy value is sent at the latest after this |
| `DEBUG_FEED_SAMPLE` | off | a made-up meter feeds the InfluxDB path, to test it without a meter |
| `OTA_PRODUCT_EMONVH` | off | use the OTAdrive key of the real device |
| `TEST_BOARD` | off | WLAN from `mycredentials.h` instead of the portal (test boards only) |
| `WIFI_SCAN_DEBUG` | off | list visible networks, print the disconnect reason |

For the real operation set `INFLUX_DRY_RUN=0` and the measurement of the real data in `platformio.ini`.

Other board: see [BOARDS.md](BOARDS.md) (environment, pins, partitions, checks).

## Tests

```
pio test -e native                       # decoder on the host, with captures and generated frames
pio run -e selftest -t upload && pio device monitor -e selftest   # UART receive path on one ESP32 (loopback)
```

See `test/README` for the test data and how to add a capture.

## On the console (115200 baud)

`alive: ...` once a minute (online, heap, offline restarts); `OTA check ...`; `Dry run, not writing: ...` /
`Writing: ...` with the line protocol; the status point `ESP32` contains heap, reset reason, write errors.
