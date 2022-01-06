#include <ArduinoOTA.h>
#include <HTTPUpdate.h>
#include "mycredentials.h"

#define Version "1.0.0.3"
#define MakeFirmwareInfo(k, v) "&_FirmwareInfo&k=" k "&v=" v "&FirmwareInfo_&"


String getChipId()
{
  String ChipIdHex = String((uint32_t)(ESP.getEfuseMac() >> 32), HEX);
  ChipIdHex += String((uint32_t)ESP.getEfuseMac(), HEX);
  return ChipIdHex;
}

long unsigned int lastcheck = 0;

void checkUpdate(long unsigned int checkperiod)
{
  if ((millis() - lastcheck) > checkperiod)
  {
    lastcheck = millis();
    String url = "http://otadrive.com/deviceapi/update?";
    url += MakeFirmwareInfo(OTAKEY, Version);
   
    url += "&s=" + getChipId();

    Serial.println(url);

    WiFiClient client;
    httpUpdate.update(client, url, Version);
  }
}