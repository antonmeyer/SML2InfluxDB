#ifndef _myinfluxdb
#define _myinfluxdb

#define INFLUXDB_CLIENT_DEBUG_ENABLE
#include "util/debug.h"

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

Point eMP1("ePower");
Point WLANP1("ESP32");

void influxdb_init()
{
  // /***** time sync already done by IOTAppStory *****
    timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");
 // */

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

    WLANP1.addTag("device", "eMonVH");
    WriteOptions wo;
    wo.batchSize(10);
    wo.bufferSize(20);
    wo.flushInterval(300);
    wo.writePrecision(WritePrecision::S);
    client.setWriteOptions(wo);
}

void send2InfluxDb(MeterDataSet dataset)
{
    //time_t tnow = time(nullptr);
    // Point eMP1("ePower");
    eMP1.setTime(WritePrecision::S); //set the current time
    eMP1.addTag("eMeter", dataset.getalias());
    eMP1.addField("actW", dataset.actW);
    eMP1.addField("sumWh", dataset.sumWh);

    // Print what are we exactly writing
    Serial.print("Writing: ");
    Serial.println(client.pointToLineProtocol(eMP1));

    // Write point into buffer - low priority measures
    if (!client.writePoint(eMP1))
    {
        Serial.print("InfluxDB write failed: ");
        Serial.println(client.getLastErrorMessage());
    };

    eMP1.clearFields(); //will change for sure
    eMP1.clearTags();   //might change
};

void sendESP32_to_Influxdb()
{
    WLANP1.setTime(WritePrecision::S);
    WLANP1.addField("rssi", WiFi.RSSI());
    WLANP1.addField("heap", ESP.getMinFreeHeap());

    // Print what are we exactly writing
    Serial.print("Writing: ");
    Serial.println(client.pointToLineProtocol(WLANP1));

    if (!client.writePoint(WLANP1))
    {
        Serial.print("InfluxDB write failed: ");
        Serial.println(client.getLastErrorMessage());
    };

    WLANP1.clearFields(); //will change for sure
}
#endif