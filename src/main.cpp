// Reads SML power meters on two UARTs and writes actual power / energy to InfluxDB.
//
//   PowerMeter.h   UART receive, frame check (MeterData.h: decoder, OBIS table, aliases)
//   myInfluxdb.h   line protocol to InfluxDB (dry run by default, see config.h)
//   myWebConf.h    WLAN and config portal (IotWebConf)
//   myOTA.h        firmware update from OTAdrive
//   config.h       pins, intervals, names - everything you may want to change
//
// Tests: pio test -e native (decoder on the host), pio run -e selftest (UART path on the ESP32).

#include <Arduino.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "PowerMeter.h"
#include "myInfluxdb.h"

#include <IotWebConf.h>
#include "myWebConf.h"

#include "myOTA.h"

static PowerMeter pm1(METER1_UART, METER1_RX_PIN);
static PowerMeter pm2(METER2_UART, METER2_RX_PIN);

// one aggregator per meter: reduces the frames to the points we write, see SampleAggregator.h
static SampleAggregator agg1(SEND_INTERVAL_S, SEND_HEARTBEAT_S);
static SampleAggregator agg2(SEND_INTERVAL_S, SEND_HEARTBEAT_S);

// A decoded frame of a meter goes to the aggregator, and a point that comes out of it to the database.
static void handleFrame(const MeterDataSet &ds, SampleAggregator &agg) {
  SamplePoint p;
  if (agg.add(ds, (uint32_t)time(nullptr), p)) send2InfluxDb(ds.getalias(), p);
}

// number of offline restarts since power up: RTC memory survives a software reset, the magic marks it valid
#define RTC_MAGIC 0xC0FFEE01u
RTC_NOINIT_ATTR static uint32_t rtcMagic;
RTC_NOINIT_ATTR static uint32_t offlineRestarts;

static bool influxReady = false; // WLAN is up and the clock is set, points can be written

void setup() {
  Serial.begin(115200);
  Serial.println("hallo sml reader here");

#if WIFI_SCAN_DEBUG
  // diagnosis: list the visible networks (SSID exactly as the ESP sees it) and print why a connect fails
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
    // 202 = auth fail (password), 15 = 4-way handshake timeout, 201 = SSID not found
    Serial.printf("WiFi disconnected, reason %d\n", info.wifi_sta_disconnected.reason);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  Serial.printf("%d networks:\n", n);
  for (int i = 0; i < n; i++)
    Serial.printf("  [%s] ch %d, %d dBm, %s\n", WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i),
                  WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "encrypted");
#endif

  if (rtcMagic != RTC_MAGIC) { // power up: RTC memory is random
    rtcMagic = RTC_MAGIC;
    offlineRestarts = 0;
  }

  // Watchdog for the loop task. Without it a hanging network call would stop the meter readout for good.
  esp_task_wdt_init(WDT_TIMEOUT_S, true); // true: reset on timeout
  esp_task_wdt_add(NULL);

  IoTconf_init();
  // We do not wait for WLAN here: the meters are read from the beginning, the part that needs the network
  // (time, InfluxDB, OTA) is started in loop() as soon as WLAN is up. A WLAN outage at boot (or later) does not
  // block anything, IotWebConf keeps on trying in doLoop().

  pm1.dataset.alias = METER1_ALIAS;
  pm2.dataset.alias = METER2_ALIAS;
}

void loop() {
  esp_task_wdt_reset(); // we are alive

  // IotWebConf state machine: WLAN connect, AP and config portal. Call as often as possible.
  iotWebConf.doLoop();

  // time sync and database connection as soon as WLAN is up, retried if the time could not be set
  static unsigned long lastInfluxTry = 0;
  if (!influxReady && iotWebConf.getState() == iotwebconf::OnLine &&
      (lastInfluxTry == 0 || millis() - lastInfluxTry > INFLUX_INIT_RETRY_MS)) {
    lastInfluxTry = millis();
    esp_task_wdt_reset(); // time sync waits up to 20 s
    influxReady = influxdb_init();
  }

  checkUpdate(); // OTAdrive, rate is set by OTA_CHECK_INTERVAL_MS

#if OFFLINE_RESTART_MS > 0
  static unsigned long lastOnline = 0; // last time we were online or somebody used the config portal
  bool portalInUse = iotWebConf.getState() == iotwebconf::NotConfigured || WiFi.softAPgetStationNum() > 0;
  if (iotWebConf.getState() == iotwebconf::OnLine || portalInUse) {
    lastOnline = millis();
  } else if (millis() - lastOnline > OFFLINE_RESTART_MS) {
    offlineRestarts++;
    Serial.printf("offline for %lu s, restarting (restart no. %u)\n", (millis() - lastOnline) / 1000, offlineRestarts);
    delay(200);
    ESP.restart();
  }
#endif

  static unsigned long lastBeat = 0;
  if (millis() - lastBeat > HEARTBEAT_INTERVAL_MS) { // shows on the console that loop() is running
    lastBeat = millis();
    Serial.printf("alive: up %lus, online %d, influx ready %d, free heap %u, largest block %u, offline restarts %u\n",
                  millis() / 1000, iotWebConf.getState() == iotwebconf::OnLine, influxReady, ESP.getFreeHeap(),
                  ESP.getMaxAllocHeap(), offlineRestarts);
  }

  // The meters: a frame is a few hundred bytes every second. Without network (no time stamps) the values are dropped.
  if (pm2.handle_event()) {
    pm2.dataset.print();
    if (influxReady) handleFrame(pm2.dataset, agg2);
  }
  if (pm1.handle_event()) {
    pm1.dataset.print();
    if (influxReady) handleFrame(pm1.dataset, agg1);
  }

#if DEBUG_FEED_SAMPLE
  // Test of the InfluxDB path without a meter: a made-up meter delivers a frame per second, like the real ones.
  // Power swings between 100 and 500 W, the counter counts up with it (0.1 Wh steps).
  static unsigned long lastFake = 0;
  static MeterDataSet fake;
  static uint32_t fakeT = 1000;
  static double fakeWh = 1000.0;
  if (influxReady && millis() - lastFake >= 1000) {
    lastFake = millis();
    double w = 300 + 200 * sin(fakeT / 7.0);
    fakeWh += w / 3600.0;
    fake.clearValues();
    strcpy(fake.meterid, "FAKE0000");
    fake.alias = "FAKE";
    fake.actW = w;
    fake.hasActW = true;
    fake.sumWh = floor(fakeWh * 10) / 10; // the counter has 0.1 Wh
    fake.hasSumWh = true;
    fake.sensorTime = fakeT++;
    fake.hasSensorTime = true;
    handleFrame(fake, agg1);
  }
#endif

  static unsigned long lastStatus = 0;
  if (influxReady && (millis() - lastStatus) > STATUS_INTERVAL_MS) {
    lastStatus = millis();
    sendESP32_to_Influxdb(offlineRestarts);
  }
}
