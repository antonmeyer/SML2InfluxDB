#ifndef _myinfluxdb
#define _myinfluxdb

// Writes the meter values and the status of the ESP32 to InfluxDB (line protocol over HTTPS), see config.h for
// the measurement names and INFLUX_DRY_RUN. Header only, included once from main.cpp.

#include "config.h"
#include "mycredentials.h" // INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_TOKEN

#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include "SampleAggregator.h" // the points to send

// InfluxDB client instance with preconfigured InfluxCloud certificate
static InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

static Point eMP1(INFLUX_MEASUREMENT);        // one point per meter frame
static Point WLANP1(INFLUX_MEASUREMENT_ESP);  // status of the ESP32

// Write problems are counted and reported with the ESP32 status point, so they are visible in the database
// without a serial console. A rejected batch (e.g. HTTP 400, invalid line protocol) is dropped by the library.
static uint32_t influxWriteErrors = 0; // failed writePoint() calls since boot
static int influxLastStatus = 0;       // HTTP status of the last request to the server (0 = none yet)

// write a point, or just log it in dry-run mode
static void influxWrite(Point &p)
{
    // Print what are we exactly writing
    Serial.print(INFLUX_DRY_RUN ? "Dry run, not writing: " : "Writing: ");
    Serial.println(client.pointToLineProtocol(p));
#if !INFLUX_DRY_RUN
    // Write point into buffer - low priority measures
    if (!client.writePoint(p))
    {
        influxWriteErrors++;
        Serial.print("InfluxDB write failed: ");
        Serial.println(client.getLastErrorMessage());
    }
    influxLastStatus = client.getLastStatusCode();
#endif
}

// Time sync and database connection, call when WLAN is up. Returns false if the clock could not be set
// (retry later), the points need valid time stamps and TLS needs a valid time for the certificates.
bool influxdb_init()
{
    // time sync, needed for the timestamps of the points (waits max. 20 s)
    timeSync(TZ_INFO, NTP_SERVER1, NTP_SERVER2);
    if (time(nullptr) < 1000000000L)
    {
        Serial.println("time not set (NTP blocked? see NTP_SERVER1 in config.h), will retry");
        return false;
    }

#if !INFLUX_DRY_RUN
    // Check server connection
    if (client.validateConnection())
    {
        Serial.print("Connected to InfluxDB: ");
        Serial.println(client.getServerUrl());
    }
    else
    {
        Serial.print("InfluxDB connection failed: ");
        Serial.println(client.getLastErrorMessage());
    }
#else
    Serial.println("InfluxDB dry run: nothing is sent");
#endif

    WLANP1.addTag("device", DEVICE_NAME);
    WriteOptions wo;
    wo.batchSize(INFLUX_BATCH_SIZE);
    wo.bufferSize(INFLUX_BUFFER_SIZE);
    wo.flushInterval(INFLUX_FLUSH_INTERVAL_S);
    wo.writePrecision(WritePrecision::S);
    client.setWriteOptions(wo);
    return true;
}

// One point of a meter: alias is the tag "eMeter". The time stamp comes from the aggregator (clock of the meter), not
// from the moment of sending. A field that is not in the point is not written, a missing value must not show up as 0.
void send2InfluxDb(const char *alias, const SamplePoint &p)
{
    eMP1.setTime((unsigned long long)p.epoch); // seconds, as the write precision
    eMP1.addTag("eMeter", alias);
    if (p.hasW)
    {
        eMP1.addField("actW", p.wMean, 1); // mean of the window, so old dashboards go on
        eMP1.addField("actWmin", p.wMin, 1);
        eMP1.addField("actWmax", p.wMax, 1);
    }
    if (p.hasSumWh)
        eMP1.addField("sumWh", p.sumWh, 1);
    if (p.hasExpWh)
        eMP1.addField("expWh", p.expWh, 1);

    influxWrite(eMP1);

    eMP1.clearFields(); // also deletes the time stamp
    eMP1.clearTags();
}

void sendESP32_to_Influxdb(uint32_t offlineRestarts)
{
    WLANP1.setTime(WritePrecision::S);
    WLANP1.addField("rssi", WiFi.RSSI());
    WLANP1.addField("heap", ESP.getMinFreeHeap());
    WLANP1.addField("heapFree", ESP.getFreeHeap());
    WLANP1.addField("heapMaxBlock", ESP.getMaxAllocHeap()); // largest free block, falls with fragmentation
    // reason of the last reset: 1 power on, 3 software, 4 panic (crash), 5/6/7 watchdog, 9 brownout
    WLANP1.addField("resetReason", (int)esp_reset_reason());
    WLANP1.addField("offlineRestarts", (long)offlineRestarts); // restarts because WLAN was away, since power up
    WLANP1.addField("writeErrors", (long)influxWriteErrors);
    WLANP1.addField("lastStatus", influxLastStatus);

    influxWrite(WLANP1);

    WLANP1.clearFields(); //will change for sure
}
#endif
