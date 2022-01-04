#include <Arduino.h>
#include <list>

#include "PowerMeter.h"

#include "myInfluxdb.h"
#include "myIoTAppStory.h"

PowerMeter pm2(UART_NUM_2, GPIO_NUM_16);  // UART_PIN_NO_CHANGE keep the defaults does not work
PowerMeter pm1(UART_NUM_1, GPIO_NUM_36);    //default UART2 = GPIO_NUM_16

unsigned long lastsend;
void setup() {

  myIoT_init();
  Serial.begin(115200);
  Serial.println("hallo sml reader here");
  //Serial.println("trying to get OnLine");
   
  influxdb_init();
  pm1.dataset.alias = "VHWP";
  pm2.dataset.alias = "VHHS";
  
}

void loop() {
  // put your main code here, to run repeatedly:

// -- doLoop should be called as frequently as possible.
  IAS.loop();

  if (pm2.handle_event() ) {
    send2InfluxDb(pm2.dataset);
  };

  if (pm1.handle_event() ) {
    send2InfluxDb(pm1.dataset);
  };
  
  if ((millis()- lastsend) > 30000){ //every 5 min
    lastsend = millis();
    sendESP32_to_Influxdb();
    //printf("heap: %d\n", ESP.getMinFreeHeap());
  }
  
}