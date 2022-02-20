#include <Arduino.h>
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

unsigned long lastsend;
void setup() {

  Serial.begin(115200);
  Serial.println("hallo sml reader here");
  Serial.println("trying to get OnLine");
 
 
  IoTconf_init(); // we hide some stuff in this include file
  //now we wait for the connection to avoid chaos for the send functions
  while (iotWebConf.getState() != iotwebconf::OnLine) {
    iotWebConf.doLoop(); }

  influxdb_init();
  
  pm1.dataset.alias = "VHWP";
  pm2.dataset.alias = "VHHS";

/* sml message debug
#include "testdata.h"
 printf("sizeof: %d\n", sizeof(isk1msgfull));
  pm1.filtertest(isk1msgfull, sizeof(isk1msgfull));

  Serial.println("hinterm filtertest kommt nichts mehr");
*/
}

void loop() {
  
// -- doLoop should be called as frequently as possible.
  iotWebConf.doLoop();
  checkUpdate (360000); // check periode in ms
  
  if (pm2.handle_event() ) {
    send2InfluxDb(pm2.dataset);
  };

  if (pm1.handle_event() ) {
    send2InfluxDb(pm1.dataset);
  };
  
  if ((millis()- lastsend) > 300000){ //every 5 min
    lastsend = millis();
    sendESP32_to_Influxdb();
  }
  
}