#include <Arduino.h>
#include <WiFi.h>
#include <esp32ModbusTCP.h>
#include <ArduinoFritzApi.h>
#include "FS.h"
#include "ESPAsyncWebServer.h"
#include "AsyncJson.h"
#include "SPIFFS.h"
#include <ArduinoJson.h>
#include <ESPmDNS.h>


bool SerialDebug = true;
void synchroniseWith_NTP_Time();
bool loadConfig();
bool saveConfig();
void syncConfig();
int switchoff(String ain);
int switchon(String ain);
bool check(int checkid);
void removeblock();
bool isBetween(
  String& now,
  String& lo,
  String& hi);
char now[5];

StaticJsonDocument<4096> config;

int hhh = 0;
int plugcount = 0;
int ordered[10];
bool blockeda[10];
unsigned long blockedtime[10];
unsigned long lastblockedtime[10];
unsigned long lastsetstatus[10];

void notFound(AsyncWebServerRequest* request)
{
  request->send(404, "application/json", "{\"message\":\"Not found\"}");
}

time_t nowsync; 
tm myTimeInfo; 

int device_listcount = 0;
void serverpathes();
unsigned long lastupdate = 0;
unsigned lastblock = 0;
bool WiFiConnected = false;
bool blocked = false;

String hostname = "switcher";
AsyncWebServer server(80);
// AP WLAN DATA
const char* ssidap = "Standard";
const char* passap = "123456789"; 

// IP und Benutzer Konfiguration
// Fritzbox
String ssid = "";
String pass = ""; 
String fritz_user = "";
String fritz_password = "";
String fritz_ip = ""; 
String fritz_ain = "";
String device_item[10] = { "", "", "", "", "", "", "", "", "", "" };
String switchoffact = "";
String switchonact = "";
unsigned long lastMillisTime = 0;
// CerbogX 116300189689
const char TIME_ZONE[] = "MEZ-1MESZ-2,M3.5.0/02:00:00,M10.5.0/03:00:00";
const char NTP_SERVER_POOL[] = "192.168.178.1";
esp32ModbusTCP cerbogx(100, { 192, 168, 178, 68 }, 502); 

unsigned long blocktime = 0;
// Variablen
double voltmin = 0.0;
double voltmax = 0.0;
double lastvoltage = 0;
double lastcurren = 0;
boolean lastalarm = 0;
int lastkwh = 0;
int kwhmin = 0;
int kwhmax = 0;
int lastaction = 0;

enum modbusType
{
  ENUM,  // enumeration
  UFIX0, // unsigned, no decimals
  SFIX0, // signed, no decimals
};
struct modbusData
{
  const char* name;
  uint16_t address;
  uint16_t length;
  modbusType type;
  uint16_t packetId;
  uint16_t serverid;
};
struct ains
{
  bool active = true;
  String id;
  String name;
  double voltmin;
  double voltmax;
  int delon;
  int deloff;
  bool invert;
  int order;
  int depon;
  int depoff;
  int hour;
  int minute;
  unsigned long lastblock = 0;
  int blocked = 0;
  bool firevent = false;
  String on;
  String off;
  int laststatus = 0;
  int status = 0;
  int itime;
  int dummy = 0;
};

struct ains device_list[10];

modbusData modbusRegisters[] = {
    "batteryvoltage", 840, 1, UFIX0, 0, 100 // Voltage
};
uint8_t numbermodbusRegisters = 1;//sizeof(modbusRegisters) / sizeof(modbusRegisters[0]);
uint8_t currentmodbusRegister = 0;
FritzApi fritz(fritz_user.c_str(), fritz_password.c_str(), fritz_ip.c_str());

void setup()
{

  if (!SPIFFS.begin(true))
  {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  //bool formatted = SPIFFS.format();
  loadConfig();
  syncConfig();

  Serial.begin(115200);
  WiFi.disconnect(true);
  WiFi.setHostname(hostname.c_str());

  cerbogx.onData([](uint16_t packet, uint8_t slave, MBFunctionCode fc, uint8_t* data, uint16_t len)
    {
      for (uint8_t i = 0; i < numbermodbusRegisters; i++) {
        if (modbusRegisters[i].packetId == packet) {
          modbusRegisters[i].packetId = 0;
          uint32_t value = 0;
          switch (modbusRegisters[i].type) {

          case ENUM:
          case UFIX0:
          {
            value = (data[0] << 8) | (data[1]);
            Serial.printf("%s: %u\n", modbusRegisters[i].name, value);
            break;
          }
          case SFIX0:
          {
            value = (data[2] << 8) | (data[1]);
            Serial.printf("%s: %i\n", modbusRegisters[i].name, value);
            break;
          }
          }
          if (i == 0)
          {
            lastvoltage = (double)value / 10.0;
          }
          else  if (i == 1)
          {
            lastkwh = (int)value / 10.0;
          }
          lastupdate = millis();
          return;
        }
      } });
  cerbogx.onError([](uint16_t packet, MBError e)
    { Serial.printf("Error packet %u: %02x\n", packet, e); lastupdate = 0; });
  delay(1000);
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info)
    {
      Serial.print("WiFi connected. IP: ");
      Serial.println(IPAddress(info.got_ip.ip_info.ip.addr));
      WiFiConnected = true; },
    ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info)
    {
      Serial.print("WiFi lost connection. Reason: ");
      Serial.println(info.wifi_sta_disconnected.reason);
      WiFi.disconnect();
      WiFiConnected = false;
      WiFi.begin(ssid, pass);
      MDNS.begin("switcher.fritz.box"); },
    ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  if (config["ssid"].as<String>() != "")
  {
    WiFi.begin(ssid, pass);
    MDNS.begin("switcher.fritz.box");
  }
  else
    WiFi.softAP(ssidap, passap);
  delay(5000);
  try
  {
    fritz.init();
  }
  catch (int e)
  {
    Serial.println("Could not connect to fritzbox: " + String(e));
  }

  serverpathes();

  configTzTime(TIME_ZONE, NTP_SERVER_POOL);
  int cnt = 0;
  while (cnt < device_listcount)
  {
    switchoff(device_list[cnt].id);
    cnt++;
    delay(100);
  }
}
static uint32_t lastMillis = 0;

void loop()
{
//NTP Update
  if ((millis() - lastMillisTime > 60000UL) && WiFiConnected)
  {
    getLocalTime(&myTimeInfo);
    while (strftime(now, 6, "%H:%M", &myTimeInfo) == 0)
    {
      if (SerialDebug)
        Serial.print("sync");
    }
    lastMillisTime = millis();
  }
 //VoltageUpdate
  if ((millis() - lastMillis > 10000UL) && WiFiConnected)
  {
    getLocalTime(&myTimeInfo);
    int cntblocked = 0;
    while (strftime(now, 6, "%H:%M", &myTimeInfo) == 0)
    {
      if (SerialDebug)
        Serial.print("sync");
    }
    if (SerialDebug)
      Serial.println(now);
    removeblock(); // remove Blocktimes
    lastMillis = millis();

    if (SerialDebug)
      Serial.print("reading registers\n");
    for (uint8_t i = 0; i < numbermodbusRegisters; ++i)
    {
      uint16_t packetId = cerbogx.readHoldingRegisters( modbusRegisters[i].address, modbusRegisters[i].length); //modbusRegisters[i].serverid,
      if (packetId > 0)
      {
        modbusRegisters[i].packetId = packetId;
      }
      else
      {
        if (SerialDebug)
          Serial.print("reading error\n");

      }
    }
    if (SerialDebug)
      Serial.println(lastvoltage);
    int cnt = 0;
    if (lastupdate != 0) // VoltageUpdate?
      while (cnt < device_listcount)
      {
        if (device_list[cnt].active) //Check active before check Configuration Entrys
          if (check(cnt)) //All Checks Positive? (Depency, Voltage and condition)
          {
            if (device_list[cnt].status == false) //Check latest status
            {
              if (device_list[cnt].blocked == 0) //Check for cleared block 
              {
                device_list[cnt].blocked = 1; //Set only to one to activate blocking 
                device_list[cnt].lastblock = millis();
              }
              else if (device_list[cnt].status == false && device_list[cnt].blocked == 2) 
              {
                if (switchon(device_list[cnt].id) == 1)
                {
                  device_list[cnt].status = true;
                  device_list[cnt].blocked = 0;
                }
              }
            }
          }
          else
          {
            if (device_list[cnt].status == true)
            {
              if (device_list[cnt].blocked == 0)
              {
                device_list[cnt].blocked = 3;
                device_list[cnt].lastblock = millis();
              }
              else if (device_list[cnt].status == true && device_list[cnt].blocked == 4)
              {
                if (switchoff(device_list[cnt].id) == 0)
                {
                  device_list[cnt].status = false;
                  device_list[cnt].blocked = 0;
                }
              }
            }
          }
        cnt++;
      }
  }
}

// switchon
int switchon(String ain)
{
  int checkon = 2;
  try
  {
    boolean b = fritz.setSwitchOn(ain);
    if (b)
    {
      checkon = 1;
      if (SerialDebug)
        Serial.println("Switch is on");
    }
    else
    {
      checkon = 0;
      if (SerialDebug)
        Serial.println("Switch is off");
    }
  }
  catch (int e)
  {
    if (SerialDebug)
      Serial.println("Got errorCode during execution " + String(e));
  }
  return checkon;
}
// switchoff
int switchoff(String ain)
{
  int checkoff = 2;
  try
  {
    boolean b = fritz.setSwitchOff(ain);
    if (b)
    {
      checkoff = 1;
      if (SerialDebug)
        Serial.println("Switch is on");
    }
    else
    {
      checkoff = 0;
      if (SerialDebug)
        Serial.println("Switch is off");
    }
  }
  catch (int e)
  {
    if (SerialDebug)
      Serial.println("Got errorCode during execution " + String(e));
  }
  return checkoff;
}
// server init
void serverpathes()
{

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request)
    {
      AsyncWebServerResponse* response = request->beginResponse(200, "text/html", "Ok");
      request->send(SPIFFS, "/www/index.html", "text/html"); });

  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* request)
    { request->send(SPIFFS, "/www/favicon.png", "image/png"); });

  server.on("/css/bootstrap.min.css", HTTP_GET, [](AsyncWebServerRequest* request)
    {
      AsyncWebServerResponse* response = request->beginResponse(200, "text/css", "Ok");
      request->send(SPIFFS, "/www/css/bootstrap.min.css", "text/css"); });

  server.on("/js/jsoneditor.js", HTTP_GET, [](AsyncWebServerRequest* request)
    {
      AsyncWebServerResponse* response = request->beginResponse(200, "text/javascript", "Ok");
      request->send(SPIFFS, "/www/js/jsoneditor.js"); });

  AsyncCallbackJsonWebHandler* handler =
    new AsyncCallbackJsonWebHandler("/postconfig",
      [](AsyncWebServerRequest* request, JsonVariant& json)
      {
        if (json.is<JsonArray>())
        {
          config = json.as<JsonArray>();
        }
        else if (json.is<JsonObject>())
        {
          config = json.as<JsonObject>();
        }
        String response = "";
        // serializeJson(config, response);
        saveConfig();
        request->send(200, "application/json");
        // Serial.println(response);
      });

  loadConfig();

  server.addHandler(handler);

  server.on("/getconfig", HTTP_GET, [](AsyncWebServerRequest* request)
    {
      String response = "";
      serializeJson(config, response);
      request->send(200, "application/json", response); });
  server.begin();
}

bool loadConfig()
{

  File configFile = SPIFFS.open("/www/config.json", "r");
  if (!configFile)
  {
    Serial.println("- failed to open config file for writing");
    return false;
  }

  deserializeJson(config, configFile);
  configFile.close();
  return true;
}

bool saveConfig()
{

  Serial.print("ConfigFile_Save_Variable: ");

  File configFile = SPIFFS.open("/www/config.json", "w");
  if (!configFile)
  {
    Serial.println("- failed to open config file for writing");
    return false;
  }

  serializeJson(config, configFile);
  configFile.close();
  syncConfig();
  return true;
}

void syncConfig()
{
  ssid = config["ssid"].as<String>();
  pass = config["password"].as<String>();
  fritz_user = config["fritzuser"].as<String>();
  fritz_password = config["fritzpass"].as<String>();
  fritz_ip = config["fritzbox"].as<String>();
  blocktime = config["blocktime"].as<int>();
  int z = 0;

  while (!config["device_item"].as<JsonArray>().operator[](z).isNull())
  {
    device_list[z].active = config["device_item"].operator[](z)["active"].as<bool>();
    device_list[z].name = config["device_item"].operator[](z)["name"].as<String>();
    device_list[z].id = config["device_item"].operator[](z)["urlid"].as<String>();
    device_list[z].voltmin = config["device_item"].operator[](z)["voltmin"].as<double>();
    device_list[z].voltmax = config["device_item"].operator[](z)["voltmax"].as<double>();
    if (config["device_item"].operator[](z)["invert"].as<String>() == "OffOn")
      device_list[z].invert = true;
    else
      device_list[z].invert = false;
    device_list[z].delon = config["device_item"].operator[](z)["delon"].as<int>();
    device_list[z].deloff = config["device_item"].operator[](z)["deloff"].as<int>();
    if (config["device_item"].operator[](z)["itime"].as<String>() == "OnOff")
      device_list[z].itime = true;
    else
      device_list[z].itime = false;
    device_list[z].depon = config["device_item"].operator[](z)["depon"].as<int>();
    device_list[z].depoff = config["device_item"].operator[](z)["depoff"].as<int>();
    device_list[z].on = config["device_item"].operator[](z)["on"].as<String>();
    device_list[z].off = config["device_item"].operator[](z)["off"].as<String>();
    device_list[z].dummy = 1;

    z++;
  }
  device_listcount = z;
}
bool check(int checkid)
{
  int timecheck = 0;
  int ordercheck = 0;
  int voltagecheck = 0;
  int depencycheck = 0;
  char temp[] = "00:00";
  
  String mynow = String(now);
  String zero = "";
  if (device_list[checkid].itime)
  {
    if (!(device_list[checkid].on == "00:00" && device_list[checkid].off == "00:00"))
    {
      if (isBetween(mynow, device_list[checkid].on, device_list[checkid].off))
      {
        timecheck = 2;
      }
    }
    else
      timecheck = 1;
  }
  else
  {
    if (!(device_list[checkid].on == "00:00" && device_list[checkid].off == "00:00"))
    {
      if (!isBetween(mynow, device_list[checkid].on, device_list[checkid].off))
      {
        timecheck = 2;
      }
      else
        timecheck = 1;
    }
  }
  if ((device_list[checkid].on == "00:00" && device_list[checkid].off == "00:00"))
    timecheck = 3;

  // voltage check
  if (device_list[checkid].voltmin == 0 && device_list[checkid].voltmax == 0)
    voltagecheck = 3;
  else if (!device_list[checkid].invert)
  {

    if (lastvoltage <= device_list[checkid].voltmin && device_list[checkid].firevent == false)
    {
      voltagecheck = 1;
      device_list[checkid].firevent = true;
    }
    else if (lastvoltage <= device_list[checkid].voltmax && device_list[checkid].firevent == true)
      voltagecheck = 1;

    else
      device_list[checkid].firevent = false;
  }
  else
  {
    if (lastvoltage >= device_list[checkid].voltmin && device_list[checkid].firevent == false)
    {
      voltagecheck = 1;
    }
    else
    {
      if (!device_list[checkid].firevent)
        device_list[checkid].firevent = true;
      if (lastvoltage >= device_list[checkid].voltmax)
        device_list[checkid].firevent = false;
    }
  }

  if (device_list[checkid].voltmin == 0 && device_list[checkid].voltmax == 0)
    voltagecheck = 3;

  // Depencycheck
  if (device_list[checkid].depoff != 0 || device_list[checkid].depon != 0)
  {
    if (device_list[checkid].depon != 0)
      if (device_list[device_list[checkid].depon - 1].status == 1)
        depencycheck = 1;
    if (device_list[checkid].depoff != 0)
      if (device_list[device_list[checkid].depoff - 1].status == 0)
        depencycheck = 1;
  }
  else
    depencycheck = 3;

  // summarize checks
  if (SerialDebug)
  {
    Serial.print(checkid);
    Serial.print("-");
    Serial.print(timecheck);
    Serial.print("-");
    Serial.print(voltagecheck);
    Serial.print("-");
    Serial.print(depencycheck);
    Serial.println("-");
  }
  if (timecheck == 2 || timecheck == 3)
    if (voltagecheck == 1 || voltagecheck == 3)
      if (depencycheck == 1 || depencycheck == 3)
        return true;

  return false;
}

// Simple Function check Time in Range
bool isBetween(
  String& now,
  String& lo,
  String& hi)
{
  return (now >= lo) && (now <= hi);
}

// Reset Timers
void removeblock()
{
  int cntblocked = 0;
  while (cntblocked < device_listcount)
  {
    if (device_list[cntblocked].blocked == 1 || device_list[cntblocked].blocked == 3)
    {
      if (millis() - device_list[cntblocked].lastblock > (60000UL * device_list[cntblocked].delon) && device_list[cntblocked].blocked == 1)
      {
        device_list[cntblocked].blocked = 2;
      }

      if (millis() - device_list[cntblocked].lastblock > (60000UL * device_list[cntblocked].deloff) && device_list[cntblocked].blocked == 3)
      {
        device_list[cntblocked].blocked = 4;
      }
    }
    cntblocked++;
  }
}
