# SML2InfluxDB
uses a ESP32
reads SML messages from a power meter via optical reader to Serial line
uses UART of ESP32

Decodes SML Messages and filters
actual power and overall power
and send it to a influxDB

WLAN init is done with IotWebConf


