#ifndef _myinfluxdb
#define _myinfluxdb

//#define INFLUXDB_CLIENT_DEBUG_ENABLE
//#include "util/debug.h"

#include "mycredentials.h"

#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include "PowerMeter.h" //we need the dataset structure

#define INFLUXDB_BUCKET "testbkt1221"
//#define INFLUXDB_BUCKET "bucket1"

// Set timezone string according to https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html

//  Central Europe: "CET-1CEST,M3.5.0,M10.5.0/3"
#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"
//#define TZ_INFO "PST8PDT"

// InfluxDB client instance with preconfigured InfluxCloud certificate
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

//InfluxDBClient client2(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

//InfluxDBClient client3(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

// Measurement names. For tests use a separate measurement (default "ePower_test"), so that the data can be
// deleted afterwards without touching the real data. Set INFLUX_MEASUREMENT / INFLUX_MEASUREMENT_ESP in platformio.ini.
#ifndef INFLUX_MEASUREMENT
#define INFLUX_MEASUREMENT "ePower_test"
#endif
#ifndef INFLUX_MEASUREMENT_ESP
#define INFLUX_MEASUREMENT_ESP "ESP32_test"
#endif

// INFLUX_DRY_RUN=1: build and print the line protocol, but do not send anything to the database.
#ifndef INFLUX_DRY_RUN
#define INFLUX_DRY_RUN 1
#endif

Point eMP1(INFLUX_MEASUREMENT);
Point WLANP1(INFLUX_MEASUREMENT_ESP);

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
    timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");
    if (time(nullptr) < 1000000000L)
    {
        Serial.println("time not set, will retry");
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

    WLANP1.addTag("device", "eMonVH");
    WriteOptions wo;
    wo.batchSize(10);
    wo.bufferSize(20);
    wo.flushInterval(300);
    wo.writePrecision(WritePrecision::S);
    client.setWriteOptions(wo);
    return true;
}

void send2InfluxDb(const MeterDataSet &dataset)
{
    // only send what the meter delivered, a missing value must not show up as 0
    if (!dataset.hasData())
        return;

    eMP1.setTime(WritePrecision::S); //set the current time
    eMP1.addTag("eMeter", dataset.getalias());
    if (dataset.hasActW)
        eMP1.addField("actW", dataset.actW);
    if (dataset.hasSumWh)
        eMP1.addField("sumWh", dataset.sumWh);
    if (dataset.hasExpWh)
        eMP1.addField("expWh", dataset.expWh);
    if (dataset.hasSensorTime)
        eMP1.addField("sensorTime", (long)dataset.sensorTime);

    influxWrite(eMP1);

    eMP1.clearFields(); //will change for sure
    eMP1.clearTags();   //might change
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
