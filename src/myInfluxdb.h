#ifndef _myinfluxdb
#define _myinfluxdb

#define INFLUXDB_CLIENT_DEBUG_ENABLE
#include "util/debug.h"

#include "credentials.h"

#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include "PowerMeter.h" //we need the dataset structure

#define INFLUXDB_BUCKET "testbkt1221"
//#define INFLUXDB_BUCKET "bucket1"

// Set timezone string according to https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html
// Examples:
//  Pacific Time: "PST8PDT"
//  Eastern: "EST5EDT"
//  Japanesse: "JST-9"
//  Central Europe: "CET-1CEST,M3.5.0,M10.5.0/3"
 #define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"
 //#define TZ_INFO "PST8PDT"

// InfluxDB client instance with preconfigured InfluxCloud certificate
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

//InfluxDBClient client2(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

//InfluxDBClient client3(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

void influxdb_init()
{

    timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

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
    client.setWriteOptions(WriteOptions().batchSize(10));
    client.setWriteOptions(WriteOptions().bufferSize(20));
    client.setWriteOptions(WriteOptions().writePrecision(WritePrecision::S));
    client.setWriteOptions(WriteOptions().flushInterval(300));
}

void send2InfluxDb(MeterDataSet dataset)
{
    time_t tnow = time(nullptr);
    
        Point MP1("ePower");
        MP1.addTag("eMeter", dataset.alias);
        MP1.addField("actW", dataset.actW);
        MP1.addField("sumWh", dataset.actW);
    
       // MP1.setTime(tnow); //set the time

        // Print what are we exactly writing
        Serial.print("Writing: ");
        Serial.println(client.pointToLineProtocol(MP1));

        // Write point into buffer - low priority measures


        if (!client.writePoint(MP1))
        {
            Serial.print("InfluxDB write failed: ");
            Serial.println(client.getLastErrorMessage());
        };

};

#endif