#include <ArduinoOTA.h>
#include <HTTPUpdate.h>
#include "mycredentials.h"

#define Version "1.0.0.7"
#define MakeFirmwareInfo(k, v) "&_FirmwareInfo&k=" k "&v=" v "&FirmwareInfo_&"

const char otadrv_ca[] =
    "-----BEGIN CERTIFICATE-----\n\
MIIEzjCCA7agAwIBAgIQJt3SK0bJxE1aaU05gH5yrTANBgkqhkiG9w0BAQsFADB+\n\
MQswCQYDVQQGEwJQTDEiMCAGA1UEChMZVW5pemV0byBUZWNobm9sb2dpZXMgUy5B\n\
LjEnMCUGA1UECxMeQ2VydHVtIENlcnRpZmljYXRpb24gQXV0aG9yaXR5MSIwIAYD\n\
VQQDExlDZXJ0dW0gVHJ1c3RlZCBOZXR3b3JrIENBMB4XDTE0MDkxMTEyMDAwMFoX\n\
DTI3MDYwOTEwNDYzOVowgYUxCzAJBgNVBAYTAlBMMSIwIAYDVQQKExlVbml6ZXRv\n\
IFRlY2hub2xvZ2llcyBTLkEuMScwJQYDVQQLEx5DZXJ0dW0gQ2VydGlmaWNhdGlv\n\
biBBdXRob3JpdHkxKTAnBgNVBAMTIENlcnR1bSBEb21haW4gVmFsaWRhdGlvbiBD\n\
QSBTSEEyMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAoSVj343kIAfZ\n\
VNHRBPYX4j5H+8N0JbjEvxISvOBw0TkFwhez94JwoE4H/hAq/9sNRl4klKOLRZ8Y\n\
m85CxK7bgzO8wru0MLanN4d4e0jLJSyCuwpIEmB2ieyOzI8eUkjphgJawrCKfIU9\n\
2f9gTzNspqGgheHXU/LqJz1lqXLBCIPMsCWcEUYk4D70p+/tUbFlk0K09uaGChB5\n\
MjZYsmuo3NV6Hp0U7kDnskZMvZopwuz4MMFiAiriHINi0IU2GoPeEoQpZe/SMr4x\n\
YEKoz/jd6tBWRx29dpYkE+e+2Zkr+jBk8Yo4eqbhKpYCsJ262I9tTnqUaX2wk6p0\n\
5ZOQE/qimQIDAQABo4IBPjCCATowDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQU\n\
5TGtvzoRlvSDvFA81LeQm5Du3iUwHwYDVR0jBBgwFoAUCHbNywf/JPbFze27kLzi\n\
hDdGdfcwDgYDVR0PAQH/BAQDAgEGMC8GA1UdHwQoMCYwJKAioCCGHmh0dHA6Ly9j\n\
cmwuY2VydHVtLnBsL2N0bmNhLmNybDBrBggrBgEFBQcBAQRfMF0wKAYIKwYBBQUH\n\
MAGGHGh0dHA6Ly9zdWJjYS5vY3NwLWNlcnR1bS5jb20wMQYIKwYBBQUHMAKGJWh0\n\
dHA6Ly9yZXBvc2l0b3J5LmNlcnR1bS5wbC9jdG5jYS5jZXIwOQYDVR0gBDIwMDAu\n\
BgRVHSAAMCYwJAYIKwYBBQUHAgEWGGh0dHA6Ly93d3cuY2VydHVtLnBsL0NQUzAN\n\
BgkqhkiG9w0BAQsFAAOCAQEAur/w4d1NK0JDZFjfZPP/gBpfVr47qbJ291R6TDDB\n\
mSRLctLK1PoIxpDeiBLt+JD5/KmE/ZLyeOXbySJXq0EwQmsLn9dzM/sBZxxCXI8n\n\
Z8duBwONDpbLCgPMPviHPDUwzRiM1XHdzd1hsBOjZEZO/nFOa2XpFATyP6i9DDY9\n\
Kl2eB/LCT5DFXk0YN9EnKICkNuXKk2plDviTua9SWEt6cdi68+/S8/ail+RdFAKa\n\
y+WutpPhI5+bP0b37o6hAFtmwx5oI4YPXXe6U635UvtwFcV16895rUl88nZirkQv\n\
xV9RNCVBahIKX46uEMRDiTX97P8x5uweh+k6fClQRUGjFA==\n\
-----END CERTIFICATE-----";

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
    String url = "https://otadrive.com/deviceapi/update?";
    url += MakeFirmwareInfo(OTAKEY, Version);
    url += "&s=" + getChipId();

    Serial.println(url);

    //WiFiClient client;
    WiFiClientSecure client;
    client.setCACert(otadrv_ca);

    // set desired timeouts
    //client.setTimeout(5);
    
    httpUpdate.update(client, url, Version);
  }
}