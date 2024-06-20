// C99 libraries
#include <cstdlib>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>


////// Defines for remote debug!
// Host name (please change it)
#define HOST_NAME "HANANTON"

// Board especific libraries
#if defined ESP8266 || defined ESP32
// Use mDNS ? (comment this do disable it)
// #define USE_MDNS 1
// Arduino OTA (uncomment this to enable)
//#define USE_ARDUINO_OTA true
#else
#error "The board must be ESP8266 or ESP32"
#endif // ESP
//////// Libraries
#if defined ESP8266
// Includes of ESP8266
#include <ESP8266WiFi.h>

#ifdef USE_MDNS
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#endif
#elif defined ESP32
// Includes of ESP32
#include <WiFi.h>
#ifdef USE_MDNS
#include <DNSServer.h>
#include "ESPmDNS.h"
#endif
#endif // ESP
#include "antonp1.h"
#include "wifisecrets.h"
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>


// Predefined static config
#define MAX_MISSED_DATA 2000          // MAX data missed from Client/Web HTTP reply before time-out (accept short messages only)
#define MAXBUFFER   1500              // MAX buffer size of P1 Telegram
const size_t JSON_BUFFER_SIZE = 1024; // ToDO: ADJUST based on JSON payload size to be created
char jsonPayload[JSON_BUFFER_SIZE];

#define NTP_SERVERS "pool.ntp.org", "time.nist.gov"

static const char* ssid = WIFI_SSID_NAME;
static const char* password = WIFI_PASSWORD;

AsyncWebServer server(80);
long lastMillis = 0;
long lastPostMillis = 0;

// Memory allocated for the sample's variables and structures.
static WiFiClientSecure wifi_client;

// Auxiliary functions
static void connectToWiFi()
{
  WiFi.mode(WIFI_AP_STA);
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
  }

  WiFi.softAP("HANANTON", "0123456789");
}

static void initializeTime()
{


  configTime(-5 * 3600, 0, NTP_SERVERS);
  time_t now = time(NULL);
  while (now < 1510592825)
  {
    delay(500);
    now = time(NULL);
  }

}

const char* proxyUrl = "http://ns.ant.is/p1";

bool postData(const char* data) {
    WiFiClient client;
    HTTPClient http;

    // Get WiFi details
    String macAddress = WiFi.macAddress();
    String localIP = WiFi.localIP().toString();
    int32_t rssi = WiFi.RSSI();
    char* isvalid = "Yes";
    if (P1valid == false)
      isvalid = "No";

    http.begin(client, proxyUrl);

    // Add headers
    http.addHeader("Content-Type", "text/plain");  // Or any other content type you need
    http.addHeader("p1control-wifimac", macAddress);
    http.addHeader("p1control-isvalid", isvalid);
    http.addHeader("p1control-wifiip", localIP);
    http.addHeader("p1control-wifisignal", String(rssi));

    int httpResponseCode = http.POST(data);

    http.end();
    return httpResponseCode > 0;
}

static char* getCurrentLocalTimeString()
{
  time_t now = time(NULL);
  return ctime(&now);
}

static uint32_t getSecondsSinceEpoch() { return (uint32_t)time(NULL); }

#include <ESP8266WiFi.h>

char* printNetworkInfo() {
    static char info[512];
    
    // Get the connected network's SSID
    String ssid = WiFi.SSID();
    
    // Get the RSSI (signal strength)
    int32_t rssi = WiFi.RSSI();
    
    // Get the STA (Station) IP address
    IPAddress staIP = WiFi.localIP();
    
    // Get the AP (Access Point) IP address
    IPAddress apIP = WiFi.softAPIP();
    
    // Get the MAC addresses
    String staMAC = WiFi.macAddress();
    String apMAC = WiFi.softAPmacAddress();
    
    // Get the subnet mask
    IPAddress subnetMask = WiFi.subnetMask();
    
    // Get the gateway IP
    IPAddress gatewayIP = WiFi.gatewayIP();
    
    // Format all information into the info char array
    snprintf(info, sizeof(info), 
        "SSID: %s\n"
        "Signal strength (RSSI): %d dBm\n"
        "STA IP Address: %s\n"
        "AP IP Address: %s\n"
        "STA MAC Address: %s\n"
        "AP MAC Address: %s\n"
        "Subnet Mask: %s\n"
        "Gateway IP: %s\n",
        ssid.c_str(),
        rssi,
        staIP.toString().c_str(),
        apIP.toString().c_str(),
        staMAC.c_str(),
        apMAC.c_str(),
        subnetMask.toString().c_str(),
        gatewayIP.toString().c_str()
    );
    
    return info;
}



char* buildJSONPayload() {
  StaticJsonDocument<JSON_BUFFER_SIZE> doc;

  // Populate the Device section
  JsonObject device = doc.createNestedObject("Device");
  device["Uptime"] = millis();
  device["HFB"] = ESP.getFreeHeap();
  device["HFPct"] = ESP.getHeapFragmentation();
  device["Version"] = "1.0.0";
  device["Name"] = HOST_NAME;

  // Populate the OBIS array
  JsonArray obisArray = doc.createNestedArray("OBIS");

  OBISItem* item = p1parsed->items;

  //TODO: Probably we should have some logic to allow filters and grouping by devices (in case of subdevices)
  while (item != nullptr)
  {
    JsonObject obj = obisArray.createNestedObject(); 
    
    obj["Code"] = item->getObisCode();
    if (item->type == OBISItem::DOUBLE) 
    {
      obj["DValue"] = item->value.dValue;
    } 
    else if (item->type == OBISItem::INT32) 
    {
      obj["IValue"] = item->value.i32Value;
    }
    else if (item->type == OBISItem::CHARARR) 
    {
      obj["SValue"] = item->value.stringValue;
    }    
    if (item->unit != nullptr) 
    {
      obj["Unit"] = item->unit->unitstr;
    }
    item = item->next;
  }
  // Serialize the JSON document into the buffer
  serializeJson(doc, jsonPayload, sizeof(jsonPayload));

  return jsonPayload;
}

void webserverSetup()
{
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(200, "text/html", "Hello, welcome to P1 Module.<br /><a href='/api'>API Payload</a>");
  });

    //Send OBIS payload as JSON
  server.on("/network", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(200, "application/json", printNetworkInfo());
  });

  //Send OBIS payload as JSON
  server.on("/api", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(200, "application/json", buildJSONPayload());
  });

  AsyncElegantOTA.begin(&server,"admin","p1anton"); //access to update / change firmware on ESP
  server.begin();
}

void setup()
{
  connectToWiFi();
  initializeTime();

   // Register host name in WiFi and mDNS
    String hostNameWifi = HOST_NAME;
#ifdef ESP8266 // Only for it
    WiFi.hostname(hostNameWifi);
#endif
#ifdef USE_MDNS  // Use the MDNS ?
    if (MDNS.begin(HOST_NAME)) {
    }
    MDNS.addService("telnet", "tcp", 23);
#endif


  webserverSetup();

  p1setup(); //Setup P1 DMRS reader
}


void loop()
{
  if (millis() > lastMillis + 5000) //Update parsing every 5s
  {
    p1ReadAndParseNow();
    lastMillis = millis();
  }

  if (millis() > lastPostMillis + 120000)
  {  
    postData(P1buffer);
    lastPostMillis = millis();
  }
  delay(10);
}
