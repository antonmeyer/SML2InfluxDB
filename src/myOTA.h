#ifndef MYOTA_H
#define MYOTA_H

#include <HTTPUpdate.h>
#include <esp_task_wdt.h>
#include <WiFiClientSecure.h>
#include "mycredentials.h" // OTAKEY

// Firmware version, as uploaded to OTAdrive. OTAdrive compares it with the version of the newest upload.
#ifndef Version // can be overridden at build time: -DVersion=\"1.1.0.1\"
#define Version "1.2.0.0"
#endif
#define MakeFirmwareInfo(k, v) "&_FirmwareInfo&k=" k "&v=" v "&FirmwareInfo_&"

// How often to ask OTAdrive. The server blocks devices that ask too often (quota), so keep this low.
// 0 switches OTA off. The first check is done OTA_FIRST_CHECK_MS after boot (WLAN and NTP time are needed
// for the certificate check), so every reboot also gives a chance to get an update.
#ifndef OTA_CHECK_INTERVAL_MS
#define OTA_CHECK_INTERVAL_MS (6UL * 60UL * 60UL * 1000UL) // 6 h
#endif
#define OTA_FIRST_CHECK_MS 60000UL

// Root certificate of otadrive.com: "Certum Trusted Network CA" (self signed, valid until 2029-12-31).
// The chain sent by the server is leaf -> Certum DV TLS G2 R39 CA -> Certum Trusted Root CA -> this one.
// The former intermediate "Certum Domain Validation CA SHA2" (valid until 2027-06-09) is not in the chain
// anymore, with only that one the TLS handshake fails.
const char otadrv_ca[] =
    "-----BEGIN CERTIFICATE-----\n\
MIIDuzCCAqOgAwIBAgIDBETAMA0GCSqGSIb3DQEBBQUAMH4xCzAJBgNVBAYTAlBM\n\
MSIwIAYDVQQKExlVbml6ZXRvIFRlY2hub2xvZ2llcyBTLkEuMScwJQYDVQQLEx5D\n\
ZXJ0dW0gQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkxIjAgBgNVBAMTGUNlcnR1bSBU\n\
cnVzdGVkIE5ldHdvcmsgQ0EwHhcNMDgxMDIyMTIwNzM3WhcNMjkxMjMxMTIwNzM3\n\
WjB+MQswCQYDVQQGEwJQTDEiMCAGA1UEChMZVW5pemV0byBUZWNobm9sb2dpZXMg\n\
Uy5BLjEnMCUGA1UECxMeQ2VydHVtIENlcnRpZmljYXRpb24gQXV0aG9yaXR5MSIw\n\
IAYDVQQDExlDZXJ0dW0gVHJ1c3RlZCBOZXR3b3JrIENBMIIBIjANBgkqhkiG9w0B\n\
AQEFAAOCAQ8AMIIBCgKCAQEA4/t9o3K6wvDJFIf1awFO4W5AB7ptJ11/91sts1rH\n\
UV+rpDKmYYe2bg+G0jACl/jXaVehGDldamR5xgFZrDwxSjh80gTSSyjoIF87B6LM\n\
TXPb865Px1bVWqeWifrzq2jUI4ZZJ88JJ7ysbnKDHDBy3+Ci6dLhdHUZvSqeexVU\n\
BBvXQzmtVSjF4hq79MDkrjhJM8x2hZ85RdKknvISjFH4fOQtf/WsX+sWn7Et0brM\n\
kUJ3TCXJkDhv2/DM+44el1k+1WBO5gUo7Ul5E0u6SNsv+XLTOcr+H9g0cvW0QM8x\n\
AcPs3hEtF10fuFDRXhmnad4HMyjKUJX5p1TLVIZQRan5SQIDAQABo0IwQDAPBgNV\n\
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBQIds3LB/8k9sXN7buQvOKEN0Z19zAOBgNV\n\
HQ8BAf8EBAMCAQYwDQYJKoZIhvcNAQEFBQADggEBAKaorSLOAT2mo/9i0Eidi15y\n\
sHhE49wcrwn9I0j6vSrEuVUEtRCjjSfeC4Jj0O7eDDd5QVsisrCaQVymcODU0HfL\n\
I9MA4GxWL+FpDQ3Zqr8hgVDZBqWo/5U30Kr+4rP1mS1FhIrlQgnXdAIv94nYmem8\n\
J9RHjboNRhx3zxSkHLmkMcScKHQDNP8zGSal6Q10tz6XxnboJ5ajZt3hrvJBW8qY\n\
VoNzcOSGGtIxQbovvi0TWnZvTuhOgQ4/WwMioBK+ZlgRSssDxLQqKi2WF+A5VLxI\n\
03YnnZotBqbJ7DnSq9ufmgsnAjUpsUCV5/nonFWIGUbWtzT1fs45mtk48VH3Tyw=\n\
-----END CERTIFICATE-----";

String getChipId()
{
  String ChipIdHex = String((uint32_t)(ESP.getEfuseMac() >> 32), HEX);
  ChipIdHex += String((uint32_t)ESP.getEfuseMac(), HEX);
  return ChipIdHex;
}

// Call from loop(), returns immediately if it is not time yet. A successful update reboots the device.
void checkUpdate()
{
#if OTA_CHECK_INTERVAL_MS > 0
  static uint32_t nextCheck = OTA_FIRST_CHECK_MS; // millis() value, compared wrap safe
  if ((int32_t)(millis() - nextCheck) < 0)
    return;
  // needs WLAN and a valid time (certificate check), else try again at the next call
  if (WiFi.status() != WL_CONNECTED || time(nullptr) < 1000000000L)
    return;
  nextCheck = millis() + OTA_CHECK_INTERVAL_MS;

  String url = "https://otadrive.com/deviceapi/update?";
  url += MakeFirmwareInfo(OTAKEY, Version);
  url += "&s=" + getChipId();

  // a TLS handshake needs ~40 KB and two contiguous 16 KB blocks, so the largest free block is the number to watch
  Serial.printf("OTA check, free heap %u, largest block %u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  t_httpUpdate_return result;
  {
    WiFiClientSecure client; // lives only for this check, the TLS memory is released at the end of the scope
    client.setCACert(otadrv_ca);
    httpUpdate.onProgress([](int, int) { esp_task_wdt_reset(); }); // the download can take longer than the watchdog
    result = httpUpdate.update(client, url, Version);
  }

  switch (result)
  {
  case HTTP_UPDATE_OK: // not reached, device reboots
    break;
  case HTTP_UPDATE_NO_UPDATES:
    Serial.println("OTA: no update");
    break;
  default:
    Serial.printf("OTA failed: %d %s\n",httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
  }
  Serial.printf("OTA check done, free heap %u, largest block %u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
}

#endif
