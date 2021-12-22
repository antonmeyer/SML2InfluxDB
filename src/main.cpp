#include <Arduino.h>
#include <list>
//#include "config.h"
//#include "debug.h"
//#include <sml/sml_file.h>
#include "PowerMeter.h"
//#include "EEPROM.h"
#include <WiFi.h>
#include "myInfluxdb.h"


PowerMeter pm2(UART_NUM_2, GPIO_NUM_16);  // UART_PIN_NO_CHANGE keep the defaults does not work
PowerMeter pm1(UART_NUM_1, GPIO_NUM_36);    //default UART2 = GPIO_NUM_16

unsigned long lastsend;
void setup() {

  Serial.begin(115200);
  Serial.println("hallo sml reader here");
  pm1.dataset.alias = "VHWP";
  pm2.dataset.alias = "VHHS";

  Serial.print("Connecting to WiFi: ");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500); Serial.print(":-(");
  }

  Serial.println("WiFi connected with IP: ");
  Serial.println(WiFi.localIP());

  influxdb_init();
}

void loop() {
  // put your main code here, to run repeatedly:

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