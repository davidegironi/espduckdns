/*
ESP8266 DuckDNS client v1.0

copyright (c) Davide Gironi, 2016

Released under GPLv3.
Please refer to LICENSE file for licensing information.
*/

//main ESP8266 Arduino library
#include <ESP8266WiFi.h>
//DNS server library
#include <DNSServer.h>
//WebServer library
#include <ESP8266WebServer.h>
//Multicast DNS library
#include <ESP8266mDNS.h>
//HTTP client library
#include <ESP8266HTTPClient.h>
//EEProm library
#include <EEPROM.h>
//WiFi connection manager library
#include <WiFiManager.h>
//Ticker library
#include <Ticker.h>
//interface SDK library
extern "C" {
  #include "user_interface.h"
}

//set the debug mode
#define DEBUGMODE_STATUSLED 1
#define DEBUGMODE_SERIAL 2
#define DEBUGMODE_BOTH 3
#define DEBUGMODE DEBUGMODE_STATUSLED
#if DEBUGMODE == DEBUGMODE_BOTH || DEBUGMODE == DEBUGMODE_SERIAL
#define SERIAL_ENABLED 1
#define WIFIMANAGERSETSTATUS_SERIALENABLED 1
#endif
#if DEBUGMODE == DEBUGMODE_BOTH || DEBUGMODE == DEBUGMODE_STATUSLED
#define WIFIMANAGERSETSTATUS_LEDENABLED 1
#endif

//define the ddns status led
#define DDNS_STATUSLED 2

//define the wifi status led port
#define WIFIMANAGERSETSTATUS_LED BUILTIN_LED

//define connection status
#define WIFIMANAGERSETSTATUS_STARTAP 0 
#define WIFIMANAGERSETSTATUS_CONNECTED 1
#define WIFIMANAGERSETSTATUS_TRYCONNECTION 2

//set to 1 if SDK is less than 1.5.1 or AP may not work properly
#define WIFIMANAGERSETSTATUS_SDKLESSTHAN151 1

//check connection timer
unsigned long checkconnection_timer = 0;

//check connection at selected interval (milliseconds)
#define CHECKCONNECTION_INTERVALL 1000

//retries on error
#define DDNS_ONERRORRETRIES 5

//ddns update internal on error
#define DDNS_ONERRORINTERVAL 1

//initialize wifi manager status led ticker
Ticker wifiManagerStatusLedTicker;

//initialize ddns status led ticker
Ticker ddnsStatusLedTicker;

//ddnsConfig
struct ddnsConfig
{
  char initialized;
  int deviceid;
  char domain[65];
  char token[37];
  int updateinterval;
} ddnsConfiguration;

//ddns timer
unsigned long ddns_updatetimer = 0;
unsigned long ddns_lastupdatetime = 0;
unsigned long ddns_lastupdatestatus = 0;

//html page constants
const char HTTP_PAGE[] PROGMEM = "<!DOCTYPE html>";
const char HTTP_HTMLSTART[] PROGMEM = "<html lang=\"en\">";
const char HTTP_HTMLEND[] PROGMEM = "</html>";
const char HTTP_HEADSTART[] PROGMEM = "<head>";
const char HTTP_HEADEND[] PROGMEM = "</head>";
const char HTTP_HEADMETA[] PROGMEM = "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
const char HTTP_HEADTITLE[] PROGMEM = "<title>ESP8266 Duck DNS Client</title>";
const char HTTP_HEADSTYLE[] PROGMEM = "<style>body{text-align:center;font-family:verdana;font-size:80%}div.b{text-align:left;display:inline-block;min-width:260px;min-width:260px;max-width:650px;width:100%;margin:20px 0 0}h1{text-align:center}div.m{background-color:#000;padding:10px;color:#fff}div.m a{color:#fff;text-decoration:none}p{margin:0 0 10px;padding:0}input,label{display:block}input[type=text]{width:100%;box-sizing:border-box;webkit-box-sizing:border-box;-moz-box-sizing:border-box}</style>";
const char HTTP_HEADSCRIPT[] PROGMEM = "<script></script>";
const char HTTP_BODYSTART[] PROGMEM = "<body>";
const char HTTP_BODYEND[] PROGMEM = "</body>";
const char HTTP_CONTENTSTART[] PROGMEM = "<div class='b'><h1>ESP8266<br/>DuckDNS client</h1><div class='m'><a href='/'>Home</a> | <a href='settings' class='s'>Settings</a></div><div class='p'>";
const char HTTP_CONTENTEND[] PROGMEM = "</div></div>";
const char HTTP_PAGENOTFOUND[] PROGMEM = "<h2>Page not found!</h2>";
const char HTTP_PAGEDUCKDNSSTATUS[] PROGMEM = "<h2>Status</h2>Last update was <b>{s}</b><br/>Last update was <b>{t}</b> ago<br/>Next update in <b>{u}</b><br/><br/>";
const char HTTP_PAGEDUCKDNSSETTINGS[] PROGMEM = "<h2>Settings</h2><script>function validate(e){var n='';return(isNaN(e.elements.n.value)||!(parseInt(e.elements.n.value)>=0&&parseInt(e.elements.n.value)<=999))&&(n+='Invalid Device ID, must be a number between 0 and 999. '),e.elements.d.value.length>=1&&e.elements.d.value.length<=64||(n+='Invalid Domain, max 64 characters. '),e.elements.t.value.length>=1&&e.elements.t.value.length<=36||(n+='Invalid Token, max 36 characters. '),(isNaN(e.elements.u.value)||!(parseInt(e.elements.u.value)>=1&&parseInt(e.elements.u.value)<=99))&&(n+='Invalid Update interval, must be a number between 0 and 99. '),''!=n?(alert(n),!1):void 0}</script><form method='get' onsubmit='return validate(this);' action='settings'><p><label>Device ID</label><input type='text' id='n' name='n' length=3 value='{n}'/></p><p><label>Domain</label><input type='text' id='d' name='d' length=64 value='{d}'/></p><p><label>Token</label><input type='text' id='t' name='t' length=36 value='{t}'/></p><p><label>Update interval</label><input type='text' id='u' name='u' length=3 value='{u}'/></p><p><label>Reset WiFi settings</label><input type='checkbox' id='r' name='r' value='1'/></p><br/><input type='hidden' id='b' name='b' value='1'/><button type='submit'>save</button></form>";
const char HTTP_PAGEDUCKDNSSETTINGSSAVE[] PROGMEM = "<h2>Settings</h2>Settings has been saved.<br/>Device will be rebooted in 10s.<br/><script>setTimeout(function(){ window.location.replace('http://{l}'); }, 10000);</script>";

//duck dns string
const char DUCKDNS_GETREQUEST[] PROGMEM = "http://www.duckdns.org/update?domains={d}&token={t}&ip=";

//initializer server
ESP8266WebServer server(80);

//emit home page
void pageHome() {
  int s = 0;
  int m = 0;
  int h = 0;

  //get last update status
  String lastupdatestatus = "Unknown";
  if(ddns_lastupdatestatus)
    lastupdatestatus = "<font style='color:green'>Success</font>";
  else
    lastupdatestatus = "<font style='color:red'>Fails</font>";
  
  //get last update time
  unsigned long lastupdatetime = millis() - 1000;
  if(millis() > lastupdatetime)
    lastupdatetime = millis() - ddns_lastupdatetime;
  s = lastupdatetime / 1000;
  m = (s / 60);
  h = (m / 60);
  s = s % 60;
  m = m % 60;
  char lastupdatetimec[13];
  snprintf(lastupdatetimec, sizeof(lastupdatetimec),"%02dh %02dm %02ds", h, m, s);

  //get next update time
  unsigned long nextupdatetime = ddns_updatetimer - millis();
  s = nextupdatetime / 1000;
  m = (s / 60);
  h = (m / 60);
  s = s % 60;
  m = m % 60;
  char nextupdatetimec[13];
  snprintf(nextupdatetimec, sizeof(nextupdatetimec),"%02dh %02dm %02ds", h, m, s);
  
  //set page content
  String contentpage = FPSTR(HTTP_PAGEDUCKDNSSTATUS);
  contentpage.replace("{s}", lastupdatestatus);
  contentpage.replace("{t}", lastupdatetimec);
  contentpage.replace("{u}", nextupdatetimec);
  
  //get main page content
  String content = page();
  content.replace("{c}", contentpage);

  //write page
  server.send(200, "text/html", content);
}

//emit page settings
void pageSettings() {
  String contentpage = "";
  int deviceid = 0;
  String deviceids = "";
  String domain = "";
  String token = "";
  int updateinterval = 0;
  String updateintervals = "";
  
  //get vars
  if (server.arg("n") != "") {
    ddnsConfiguration.deviceid = atoi(server.arg("n").c_str());
  }
  if (server.arg("d") != "") {
    domain = server.arg("d");
    if(domain.length() < sizeof(ddnsConfiguration.domain)/sizeof(ddnsConfiguration.domain[0]))
      strcpy(ddnsConfiguration.domain, domain.c_str());
  }
  if (server.arg("t") != "") {
    token = server.arg("t");
    if(token.length() < sizeof(ddnsConfiguration.token)/sizeof(ddnsConfiguration.token[0]))
      strcpy(ddnsConfiguration.token, token.c_str());
  }
  if (server.arg("u") != "") {
    ddnsConfiguration.updateinterval = atoi(server.arg("u").c_str());
  }
  if (server.arg("r") == "1") {
    WiFiManager wifiManager;
    wifiManager.resetSettings();
  }
  
  //eeprom update
  ddnsEEPROMwrite();

  //get vars
  ddnsEEPROMread();

  deviceid = ddnsConfiguration.deviceid;
  char deviceidc[4];
  snprintf(deviceidc, sizeof(deviceidc), "%03d", deviceid);
  domain = String(ddnsConfiguration.domain);
  token = String(ddnsConfiguration.token);
  updateinterval = ddnsConfiguration.updateinterval;
  char updateintervalc[7];
  snprintf(updateintervalc, sizeof(updateintervalc), "%d", updateinterval);
    
  if (server.arg("b") != "") {
    //save and reboot
    contentpage = FPSTR(HTTP_PAGEDUCKDNSSETTINGSSAVE);
    char hostname [10+4];
    strcpy(hostname, "espduckdns");
    strcat(hostname, deviceidc);
    contentpage.replace("{l}", hostname);
  } else {
    //set page content
    contentpage = FPSTR(HTTP_PAGEDUCKDNSSETTINGS);
    contentpage.replace("{n}", deviceidc);
    contentpage.replace("{d}", domain);
    contentpage.replace("{t}", token);
    contentpage.replace("{u}", updateintervalc);
  }
  
  //get main page content
  String content = page();
  content.replace("{c}", contentpage);
  
  //write page
  server.send(200, "text/html", content);
  
  if (server.arg("b") != "") {
    delay(1000);
    ESP.restart();
    delay(1000);
  }
}

//emit page not found
void pageNotFound() {
  //set page content
  String contentpage = FPSTR(HTTP_PAGENOTFOUND);
  
  //get main page content
  String content = page();
  content.replace("{c}", contentpage);

  //write page
  server.send(200, "text/html", content);
}

//populate main page conent
String page() {
  String content = "";
  content += FPSTR(HTTP_PAGE);
  content += FPSTR(HTTP_HTMLSTART);
  content += FPSTR(HTTP_HEADSTART);
  content += FPSTR(HTTP_HEADMETA);
  content += FPSTR(HTTP_HEADTITLE);
  content += FPSTR(HTTP_HEADSTYLE);
  content += FPSTR(HTTP_HEADSCRIPT);
  content += FPSTR(HTTP_HEADEND);
  content += FPSTR(HTTP_BODYSTART);
  content += FPSTR(HTTP_CONTENTSTART);
  content += "{c}";
  content += FPSTR(HTTP_CONTENTEND);
  content += FPSTR(HTTP_BODYEND);
  content += FPSTR(HTTP_HTMLEND);
  return content;
}

//initialize web server
void pageInit() {
#if defined SERIAL_ENABLED
  Serial.println("Starting webserver");
#endif
  //initialize server
  server.on("/", pageHome);
  server.on("/settings", pageSettings);
  server.onNotFound(pageNotFound);
  server.begin();
}

//wifi status led tick
void wifiManagerStatusLedTick() {
  //toggle the led state
  int ledstate = digitalRead(WIFIMANAGERSETSTATUS_LED);
  digitalWrite(WIFIMANAGERSETSTATUS_LED, !ledstate);
}

//set the wifi connection status
void wifiManagerSetStatus(int status) {
  if(status == WIFIMANAGERSETSTATUS_STARTAP) {
#if defined WIFIMANAGERSETSTATUS_SERIALENABLED
    Serial.println("Configuration mode");
    Serial.println(WiFi.softAPIP());
#endif
#if defined WIFIMANAGERSETSTATUS_LEDENABLED
    //set led to config mode
    wifiManagerStatusLedTicker.detach();
    wifiManagerStatusLedTicker.attach(0.2, wifiManagerStatusLedTick);
#endif
  } else if(status == WIFIMANAGERSETSTATUS_CONNECTED) {
#if defined WIFIMANAGERSETSTATUS_SERIALENABLED
    Serial.println("Connected");
    Serial.println(WiFi.localIP());
#endif
#if defined WIFIMANAGERSETSTATUS_LEDENABLED
    //set led to connected mode
    wifiManagerStatusLedTicker.detach();
    digitalWrite(WIFIMANAGERSETSTATUS_LED, LOW);
#endif
  } else if(status == WIFIMANAGERSETSTATUS_TRYCONNECTION) {
#if defined WIFIMANAGERSETSTATUS_SERIALENABLED
    Serial.println("Trying to connect");
#endif
#if defined WIFIMANAGERSETSTATUS_LEDENABLED
    //set let to connection mode
    wifiManagerStatusLedTicker.detach();
    wifiManagerStatusLedTicker.attach(0.5, wifiManagerStatusLedTick);
#endif    
  }
}

//config mode callback
void wifiManagerConfigModeCallback(WiFiManager *myWiFiManager) {
  //set wifi status
  wifiManagerSetStatus(WIFIMANAGERSETSTATUS_STARTAP);
}

//connection to wifi
void wifiManagerConnecting() {
  //initialize wifi manager
  WiFiManager wifiManager;
  
  //connecting...
  wifiManagerSetStatus(WIFIMANAGERSETSTATUS_TRYCONNECTION);

#if defined WIFIMANAGERSETSTATUS_SERIALENABLED
  wifiManager.setDebugOutput(true);
#endif
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.setAPCallback(wifiManagerConfigModeCallback);
  if(!wifiManager.autoConnect("ESPDuckDNS-AP")) {
#if defined WIFIMANAGERSETSTATUS_SERIALENABLED
    Serial.println("Failed to connect and hit timeout, restarting");
#endif
    ESP.reset();
    delay(1000);
  }
  
  //we are connected
  wifiManagerSetStatus(WIFIMANAGERSETSTATUS_CONNECTED);
}

//ddns eeprom read
void ddnsEEPROMinit() {
  EEPROM.begin(512);
  delay(10);
  ddnsEEPROMread();
  if(ddnsConfiguration.initialized != 0x10) {
#if defined SERIAL_ENABLED
    Serial.println("Initialize eeprom");
#endif
    ddnsConfiguration.initialized = 0x10;
    ddnsConfiguration.deviceid = 1;
    strcpy(ddnsConfiguration.domain, "domain");
    strcpy(ddnsConfiguration.token, "token");
    ddnsConfiguration.updateinterval = 10;
    ddnsEEPROMwrite();
  }
}

//ddns eeprom read
void ddnsEEPROMread() {
  EEPROM.get(0, ddnsConfiguration);
}

//ddns eeprom write
void ddnsEEPROMwrite() {
  EEPROM.put(0, ddnsConfiguration);
  EEPROM.commit();
}

//status led tick
void ddnsStatusLedTick() {
  //toggle the led state
  int ledstate = digitalRead(DDNS_STATUSLED);
  digitalWrite(DDNS_STATUSLED, !ledstate);
}

//ddns updater
int ddnsUpdate() {
  int updated = 0;
  
  //parse the get request
  String domain = String(ddnsConfiguration.domain);
  String token = String(ddnsConfiguration.token);
  String getrequest = FPSTR(DUCKDNS_GETREQUEST);
  getrequest.replace("{d}", domain);
  getrequest.replace("{t}", token);

#if defined SERIAL_ENABLED
  Serial.print("Sending request ");
  Serial.println(getrequest);
#endif

  //send the request, retry on error
  int updateretries = 0;
  do {
    HTTPClient http;
    http.begin(getrequest);
    int httpCode = http.GET();
    if(httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      if(payload == "OK")
        updated = 1;
    }
    http.end();
    updateretries++;
    if(updateretries < DDNS_ONERRORRETRIES && updated == 0) 
      delay(500);
  } while(updated == 0 && updateretries < DDNS_ONERRORRETRIES);
  
  //check the status update
  if(updated) {
    ddnsStatusLedTicker.detach();
    digitalWrite(DDNS_STATUSLED, 1);
#if defined SERIAL_ENABLED
    Serial.println("DDNS update success");
#endif
  } else {
    ddnsStatusLedTicker.detach();
    ddnsStatusLedTicker.attach(0.5, ddnsStatusLedTick);
#if defined SERIAL_ENABLED
    Serial.println("DDNS update error");
#endif  
  }

  //set last update status
  ddns_lastupdatestatus = updated;
  
  return updated;
}

//main setup
void setup() {  
#if defined SERIAL_ENABLED
  //initialize Serial
  Serial.begin(115200);
  delay(500);
  Serial.println("Starting...");
#endif

#if defined WIFIMANAGERSETSTATUS_LEDENABLED
  //set wifi status led as output
  pinMode(WIFIMANAGERSETSTATUS_LED, OUTPUT);
#endif

  //set ddns status led
  pinMode(DDNS_STATUSLED, OUTPUT); 
  digitalWrite(DDNS_STATUSLED, 0);

  //set ddns timers
  ddns_lastupdatetime = millis();
  ddns_updatetimer = millis();
  
  //initialize eeprom
  ddnsEEPROMinit();
  ddnsEEPROMread();

#if WIFIMANAGERSETSTATUS_SDKLESSTHAN151 == 1
  WiFi.mode(WIFI_STA);
#endif
  
  //set hostname
  char deviceidc[4];
  snprintf(deviceidc, sizeof(deviceidc), "%03d", ddnsConfiguration.deviceid);
  char hostname [10+4];
  strcpy(hostname, "espduckdns");
  strcat(hostname, deviceidc);
#if defined SERIAL_ENABLED
  Serial.print("Setting hostname as ");
  Serial.println(hostname);
#endif
  wifi_station_set_hostname(hostname);
  
  //try to connect
  wifiManagerConnecting();
    
  //multicast dns
  if(MDNS.begin(hostname)) {
#if defined SERIAL_ENABLED
    Serial.println("MDNS responder started");
#endif
  }

  //check connection init
  checkconnection_timer = millis();
  
  //initialize webserver
  pageInit();
}

//main loop
void loop() {
  //check connection
  if (millis() > checkconnection_timer) {
    checkconnection_timer = millis() + CHECKCONNECTION_INTERVALL;
    if(WiFi.status() != WL_CONNECTED) {
        wifiManagerSetStatus(WIFIMANAGERSETSTATUS_TRYCONNECTION);
    } else {
      wifiManagerSetStatus(WIFIMANAGERSETSTATUS_CONNECTED);
    }
  }
  
  //server handler
  server.handleClient();
  
  //check ddns update timer
  if (millis() > ddns_updatetimer) {
    ddns_updatetimer = millis() + ddnsConfiguration.updateinterval * 60 * 1000;
    if(ddnsUpdate()) {
      ddns_lastupdatetime = millis();
    } else {
      ddns_updatetimer = millis() + DDNS_ONERRORINTERVAL * 60 * 1000;
    }
  }
}
