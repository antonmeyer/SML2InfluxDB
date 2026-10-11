#include <Arduino.h>
#include <esp_task_wdt.h>
#include <list>
//#include "config.h"
//#include "debug.h"
//#include <sml/sml_file.h>
#include "PowerMeter.h"
//#include "EEPROM.h"
//#include <WiFi.h>
#include "myInfluxdb.h"

#include <IotWebConf.h>
#include "myWebConf.h"

#include "myOTA.h"

PowerMeter pm2(UART_NUM_2, GPIO_NUM_16);  // UART_PIN_NO_CHANGE keep the defaults does not work
PowerMeter pm1(UART_NUM_1, GPIO_NUM_32);    //default UART2 = GPIO_NUM_16

// loop() must run at least this often, else the task watchdog resets the device (reset reason 6 in the status
// point). The longest things in loop() are an HTTPS write and the OTA check, the OTA download feeds the watchdog.
#define WDT_TIMEOUT_S 60

#define STATUS_INTERVAL_MS 300000UL // ESP32 status point every 5 min
#define INFLUX_INIT_RETRY_MS 60000UL
#define HEARTBEAT_INTERVAL_MS 60000UL

// Restart if the device was not online for this long. An ESP32 does not always find its way back into the WLAN
// by itself (e.g. after a router restart). 0 switches it off. Not counted while the config portal is in use.
#ifndef OFFLINE_RESTART_MS
#define OFFLINE_RESTART_MS 600000UL // 10 min
#endif

// number of offline restarts since power up: RTC memory survives a software reset, the magic marks it valid
#define RTC_MAGIC 0xC0FFEE01u
RTC_NOINIT_ATTR static uint32_t rtcMagic;
RTC_NOINIT_ATTR static uint32_t offlineRestarts;

unsigned long lastsend;
static bool influxReady = false; // WLAN is up and the clock is set, points can be written

void setup() {

  Serial.begin(115200);
  Serial.println("hallo sml reader here");
  Serial.println("trying to get OnLine");

#if WIFI_SCAN_DEBUG
  // diagnosis: list the visible networks (SSID exactly as the ESP sees it) and print why a connect fails
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
    Serial.printf("WiFi disconnected, reason %d\n", info.wifi_sta_disconnected.reason); // 202 = auth fail (password),
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);                                              // 15 = 4-way handshake timeout, 201 = SSID not found
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

  IoTconf_init(); // we hide some stuff in this include file
  // We do not wait for WLAN here: the meters are read from the beginning, the part that needs the network
  // (time, InfluxDB, OTA) is started in loop() as soon as WLAN is up. A WLAN outage at boot (or later) does not
  // block anything, IotWebConf keeps on trying in doLoop().

  pm1.dataset.alias = "VHWP"; // used if the meter is not in the lookup table (MeterData.h)
  pm2.dataset.alias = "VHHS";

  // the decoder is tested on the host with real captures: pio test -e native
}

void loop() {
  esp_task_wdt_reset(); // we are alive

// -- doLoop should be called as frequently as possible.
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

  if (pm2.handle_event() ) {
    pm2.dataset.print();
    if (influxReady) send2InfluxDb(pm2.dataset);
  };

  if (pm1.handle_event() ) {
    pm1.dataset.print();
    if (influxReady) send2InfluxDb(pm1.dataset);
  };

  if (influxReady && (millis() - lastsend) > STATUS_INTERVAL_MS) {
    lastsend = millis();
    sendESP32_to_Influxdb(offlineRestarts);
  }
}