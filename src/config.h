#ifndef CONFIG_H
#define CONFIG_H

// All settings of the firmware in one place. Most of them can be overridden with a build flag (-D...) in
// platformio.ini, so the same source builds the real device, the test board and the self test.
// Credentials (WLAN, InfluxDB, OTAdrive) are not here: src/mycredentials.h, see mycredentials.h.example.

// ---- meters -------------------------------------------------------------------------------------------------
// UART and RX pin of the optical reader per meter (GPIO16 is the default RX of UART2).
// The alias is the name of the meter in the database if its serial number is not in the lookup table of
// MeterData.h.
#define METER1_UART UART_NUM_1
#define METER1_RX_PIN GPIO_NUM_32
#define METER1_ALIAS "VHWP" // Vorderhaus Waermepumpe
#define METER2_UART UART_NUM_2
#define METER2_RX_PIN GPIO_NUM_16
#define METER2_ALIAS "VHHS" // Vorderhaus Haushalt

// ---- InfluxDB -----------------------------------------------------------------------------------------------
#define INFLUXDB_BUCKET "testbkt1221"
#define DEVICE_NAME "eMonVH" // tag "device" of the status point

// Central Europe; other zones: https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html
#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"

// Measurement names. For tests use a separate measurement, so that the data can be deleted afterwards without
// touching the real data.
#ifndef INFLUX_MEASUREMENT
#define INFLUX_MEASUREMENT "ePower_test"
#endif
#ifndef INFLUX_MEASUREMENT_ESP
#define INFLUX_MEASUREMENT_ESP "ESP32_test"
#endif

// 1: build and print the line protocol, but do not send anything to the database.
#ifndef INFLUX_DRY_RUN
#define INFLUX_DRY_RUN 1
#endif

// Write buffer of the InfluxDB library: points are sent in batches, or at the latest after the flush interval.
#define INFLUX_BATCH_SIZE 10
#define INFLUX_BUFFER_SIZE 20 // batches, the oldest points are dropped when it is full
#define INFLUX_FLUSH_INTERVAL_S 300

// ---- timing -------------------------------------------------------------------------------------------------
// loop() must run at least this often, else the task watchdog resets the device (reset reason 6 in the status
// point). The longest things in loop() are an HTTPS write and the OTA check, the OTA download feeds the watchdog.
#define WDT_TIMEOUT_S 60

#define STATUS_INTERVAL_MS 300000UL   // ESP32 status point every 5 min
#define INFLUX_INIT_RETRY_MS 60000UL  // time sync is retried after this
#define HEARTBEAT_INTERVAL_MS 60000UL // "alive" line on the console

// Restart if the device was not online for this long. An ESP32 does not always find its way back into the WLAN
// by itself (e.g. after a router restart). 0 switches it off. Not counted while the config portal is in use.
#ifndef OFFLINE_RESTART_MS
#define OFFLINE_RESTART_MS 600000UL // 10 min
#endif

// ---- OTAdrive -----------------------------------------------------------------------------------------------
// Firmware version, as uploaded to OTAdrive. OTAdrive installs whatever version is set on the server when it is
// different from the one the device reports (also an older one).
#ifndef Version // can be overridden at build time: -DVersion=\"1.2.1.0\"
#define Version "1.2.0.0"
#endif

// How often to ask OTAdrive. The server blocks devices that ask too often (quota), so keep this low.
// 0 switches OTA off. The first check is done OTA_FIRST_CHECK_MS after boot (WLAN and NTP time are needed for the
// certificate check), so every reboot also gives a chance to get an update.
#ifndef OTA_CHECK_INTERVAL_MS
#define OTA_CHECK_INTERVAL_MS (6UL * 60UL * 60UL * 1000UL) // 6 h
#endif
#define OTA_FIRST_CHECK_MS 60000UL

#endif
