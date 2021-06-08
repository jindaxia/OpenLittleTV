

////////////////////////////////

// Select the FileSystem by uncommenting one of the lines below

//#define USE_SPIFFS
#define USE_LITTLEFS
//#define USE_SDFS

////////////////////////////////
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <ArduinoJson.h>

#if defined USE_SPIFFS
#include <FS.h>
FS *fileSystem = &SPIFFS;
SPIFFSConfig fileSystemConfig = SPIFFSConfig();
#elif defined USE_LITTLEFS
#include <LittleFS.h>
FS *fileSystem = &LittleFS;
LittleFSConfig fileSystemConfig = LittleFSConfig();
#elif defined USE_SDFS
#include <SDFS.h>
FS *fileSystem = &SDFS;
SDFSConfig fileSystemConfig = SDFSConfig();
// fileSystemConfig.setCSPin(chipSelectPin);
#else
#error Please select a filesystem first by uncommenting one of the "#define USE_xxx" lines at the beginning of the sketch.
#endif

#define sclPin D1
#define sdaPin D2

U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ sclPin, /* data=*/ sdaPin, /* reset=*/ U8X8_PIN_NONE);   // ESP32 Thing, pure SW emulated I2C

const int led = LED_BUILTIN;

// touch sensor PIN
#define ctsPin 10

#define DBG_OUTPUT_PORT Serial

#ifndef STASSID
#define STASSID "your-ssid"
#define STAPSK "your-password"
#endif

const String setting_file = "settings.json";

String api_key = "";
String city_code = "";
String bilibili_uid = "";

int maxDisplayMode = 4;                                // 目前支持4种显示模式
int displayMode = 0;                                   // 当前显示模式 0-时间  1-天气  2-倒计时 3-B站
esp8266::polledTimeout::periodicMs timeToChange(1000); // 刷新最小间隔时间 1s
bool modeChangeRequested = false;

const char *ssid = STASSID;
const char *password = STAPSK;
const char *host = "weatherstation";

ESP8266WebServer server(80);

static const char TEXT_PLAIN[] PROGMEM = "text/plain";
static const char FS_INIT_ERROR[] PROGMEM = "FS INIT ERROR";
static const char FILE_NOT_FOUND[] PROGMEM = "FileNotFound";

////////////////////////////////
// Utils to return HTTP codes

void replyOK()
{
  server.send(200, FPSTR(TEXT_PLAIN), "");
}

void replyOKWithMsg(String msg)
{
  server.send(200, FPSTR(TEXT_PLAIN), msg);
}

void replyNotFound(String msg)
{
  server.send(404, FPSTR(TEXT_PLAIN), msg);
}

void replyBadRequest(String msg)
{
  DBG_OUTPUT_PORT.println(msg);
  server.send(400, FPSTR(TEXT_PLAIN), msg + "\r\n");
}

void replyServerError(String msg)
{
  DBG_OUTPUT_PORT.println(msg);
  server.send(500, FPSTR(TEXT_PLAIN), msg + "\r\n");
}

////////////////////////////////
// Request handlers

/*
   Read the given file from the filesystem and stream it back to the client
*/
bool handleFileRead(String path)
{
  DBG_OUTPUT_PORT.println(String("handleFileRead: ") + path);

  // 不允许请求设置文件
  if (path == setting_file)
  {
    return false;
  }
  if (path.endsWith("/"))
  {
    path += "index.htm";
  }

  String contentType = mime::getContentType(path);

  if (!fileSystem->exists(path))
  {
    // File not found, try gzip version
    path = path + ".gz";
  }
  if (fileSystem->exists(path))
  {
    File file = fileSystem->open(path, "r");
    if (server.streamFile(file, contentType) != file.size())
    {
      DBG_OUTPUT_PORT.println("Sent less data than expected!");
    }
    file.close();
    return true;
  }

  return false;
}

/*
   The "Not Found" handler catches all URI not explicitely declared in code
   First try to find and return the requested file from the filesystem,
   and if it fails, return a 404 page with debug information
*/
void handleNotFound()
{
  String uri = ESP8266WebServer::urlDecode(server.uri()); // required to read paths with blanks

  if (handleFileRead(uri))
  {
    return;
  }

  // Dump debug data
  String message;
  message.reserve(100);
  message = F("Error: File not found\n\nURI: ");
  message += uri;
  message += F("\nMethod: ");
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += F("\nArguments: ");
  message += server.args();
  message += '\n';
  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += F(" NAME:");
    message += server.argName(i);
    message += F("\n VALUE:");
    message += server.arg(i);
    message += '\n';
  }
  message += "path=";
  message += server.arg("path");
  message += '\n';
  DBG_OUTPUT_PORT.print(message);

  return replyNotFound(message);
}

void setup(void)
{
  ////////////////////////////////
  // SERIAL INIT
  DBG_OUTPUT_PORT.begin(115200);
  DBG_OUTPUT_PORT.setDebugOutput(true);
  DBG_OUTPUT_PORT.print('\n');

  pinMode(ctsPin, INPUT);

  u8g2.begin();
  ////////////////////////////////
  // FILESYSTEM INIT

  fileSystemConfig.setAutoFormat(false);
  fileSystem->setConfig(fileSystemConfig);
  bool fsOK = fileSystem->begin();
  DBG_OUTPUT_PORT.println(fsOK ? F("Filesystem initialized.") : F("Filesystem init failed!"));

  ////////////////////////////////
  // WI-FI INIT
  DBG_OUTPUT_PORT.printf("Connecting to %s\n", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    DBG_OUTPUT_PORT.print(".");
  }
  DBG_OUTPUT_PORT.println("");
  DBG_OUTPUT_PORT.print(F("Connected! IP address: "));
  DBG_OUTPUT_PORT.println(WiFi.localIP());

  ////////////////////////////////
  // MDNS INIT
  if (MDNS.begin(host))
  {
    MDNS.addService("http", "tcp", 80);
    DBG_OUTPUT_PORT.print(F("Open http://"));
    DBG_OUTPUT_PORT.print(host);
    DBG_OUTPUT_PORT.println(F(".local to open the page"));
  }

  load_settings();

  ////////////////////////////////
  // WEB SERVER INIT

  // 获取所有的设置信息，返回json
  server.on("/api/settings", handleSetting);

  // Default handler for all URIs not defined above
  // Use it to read files from filesystem
  server.onNotFound(handleNotFound);

  // Start server
  server.begin();
  DBG_OUTPUT_PORT.println("HTTP server started");
}

int day_diff(int year_start, int month_start, int day_start, int year_end, int month_end, int day_end)
{
  int y2, m2, d2;
  int y1, m1, d1;

  m1 = (month_start + 9) % 12;
  y1 = year_start - m1 / 10;
  d1 = 365 * y1 + y1 / 4 - y1 / 100 + y1 / 400 + (m1 * 306 + 5) / 10 + (day_start - 1);

  m2 = (month_end + 9) % 12;
  y2 = year_end - m2 / 10;
  d2 = 365 * y2 + y2 / 4 - y2 / 100 + y2 / 400 + (m2 * 306 + 5) / 10 + (day_end - 1);

  return (d2 - d1);
}

void load_settings()
{
  if (!fileSystem->exists(setting_file))
  {
    DBG_OUTPUT_PORT.println("Setting file not found, create one");
    File file = fileSystem->open(setting_file, "w+");
    file.print(setting_serialize());
    return;
  }
  else
  {
    DBG_OUTPUT_PORT.println("Read setting file:");
    File file = fileSystem->open(setting_file, "r");
    String setting = "";
    while (file.available()) {
      setting += (char)file.read();
    }
    file.close();

    DBG_OUTPUT_PORT.println(setting);
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, setting);
    if (error) {
      DBG_OUTPUT_PORT.print(F("deserializeJson() failed: "));
      DBG_OUTPUT_PORT.println(error.f_str());
      return;
    }
    const char* key = doc["api_key"];
    api_key = String(key);
    const char* city = doc["city_code"];
    city_code = String(city);
    const char* bili = doc["bili_id"];
    bilibili_uid = String(bili);
  }
}

String setting_serialize()
{
  String json;
  StaticJsonDocument<128> doc;

  doc["api_key"] = api_key;
  doc["city_code"] = city_code;
  doc["bili_id"] = bilibili_uid;
  doc["displaymode"] = displayMode;
  doc["event"] = 1;
  // 日期格式必须为  "yyyy-MM-dd"
  doc["date"] = "2021-08-08";
  serializeJson(doc, json);
  return json;
}

void handleSetting()
{
  if (server.method() == HTTP_GET)
  {
    DBG_OUTPUT_PORT.println("Get Settings:");
    digitalWrite(led, 1);

    String setting = setting_serialize();
    server.send(200, "text/json", setting);

    digitalWrite(led, 0);
    DBG_OUTPUT_PORT.println(setting);
  }
  else if (server.method() == HTTP_POST)
  {
    digitalWrite(led, 1);
    String message = "POST form was:\n";
    for (uint8_t i = 0; i < server.args(); i++)
    {
      String setted_value = server.arg(i);
      if (server.argName(i) == "api_key")
      {
        DBG_OUTPUT_PORT.println("SET api_key: " + setted_value);
        api_key = setted_value;
      }
      else if (server.argName(i) == "city_code")
      {
        DBG_OUTPUT_PORT.println("SET city_code: " + setted_value);
        city_code = setted_value;
      }
      else if (server.argName(i) == "bilibili_uid")
      {
        DBG_OUTPUT_PORT.println("SET bilibili_uid: " + setted_value);
        bilibili_uid = setted_value;
      }
      else
      {
        DBG_OUTPUT_PORT.println("Unsupported set Key :: " + server.argName(i));
      }
    }

    // 写入设置文件
    if (fileSystem->exists(setting_file))
    {
      File file = fileSystem->open(setting_file, "w");
      file.print(setting_serialize());
      file.close();
    }

    // 回OK代表保存成功
    server.send(200, "text/json", "{\"message\": \"OK\"}");
    digitalWrite(led, 0);
  }
  else
  {
    digitalWrite(led, 1);
    server.send(405, "text/plain", "Method Not Allowed");
    digitalWrite(led, 0);
  }
}

// 以下是显示的代码

void showDisplay0()
{
  u8g2.clearBuffer();					// clear the internal memory
  u8g2.setFont(u8g2_font_ncenB08_tr);	// choose a suitable font
  u8g2.drawStr(0,10,"Screen 0!");	// write something to the internal memory
  u8g2.sendBuffer();					// transfer internal memory to the display
  
  // Create a sceenshot. The picture (XBM or PBM format) is sent to the serial output. 
  // Copy and paste the output from the Arduino serial monitor into a file.
  // Depending on the display controller use u8g2.writeBufferXBM() or u8g2.writeBufferXBM2()
  // For SH1122, LD7032, ST7920, ST7986, LC7981, T6963, SED1330, RA8835, MAX7219, LS0?
  // use u8g2.writeBufferXBM2(), for all others use u8g2.writeBufferXBM()
  u8g2.writeBufferXBM(DBG_OUTPUT_PORT);			// Write XBM image to serial out
  delay(500);  
}

void showDisplay1()
{
  u8g2.clearBuffer();					// clear the internal memory
  u8g2.setFont(u8g2_font_ncenB08_tr);	// choose a suitable font
  u8g2.drawStr(0,10,"Screen 1!");	// write something to the internal memory
  u8g2.sendBuffer();					// transfer internal memory to the display
  
  // Create a sceenshot. The picture (XBM or PBM format) is sent to the serial output. 
  // Copy and paste the output from the Arduino serial monitor into a file.
  // Depending on the display controller use u8g2.writeBufferXBM() or u8g2.writeBufferXBM2()
  // For SH1122, LD7032, ST7920, ST7986, LC7981, T6963, SED1330, RA8835, MAX7219, LS0?
  // use u8g2.writeBufferXBM2(), for all others use u8g2.writeBufferXBM()
  u8g2.writeBufferXBM(DBG_OUTPUT_PORT);			// Write XBM image to serial out
  delay(500);  
}

void showDisplay2()
{
  u8g2.clearBuffer();					// clear the internal memory
  u8g2.setFont(u8g2_font_ncenB08_tr);	// choose a suitable font
  u8g2.drawStr(0,10,"Screen 2!");	// write something to the internal memory
  u8g2.sendBuffer();					// transfer internal memory to the display
  
  // Create a sceenshot. The picture (XBM or PBM format) is sent to the serial output. 
  // Copy and paste the output from the Arduino serial monitor into a file.
  // Depending on the display controller use u8g2.writeBufferXBM() or u8g2.writeBufferXBM2()
  // For SH1122, LD7032, ST7920, ST7986, LC7981, T6963, SED1330, RA8835, MAX7219, LS0?
  // use u8g2.writeBufferXBM2(), for all others use u8g2.writeBufferXBM()
  u8g2.writeBufferXBM(DBG_OUTPUT_PORT);			// Write XBM image to serial out
  delay(500);
}

void showDisplay3()
{
  u8g2.clearBuffer();					// clear the internal memory
  u8g2.setFont(u8g2_font_ncenB08_tr);	// choose a suitable font
  u8g2.drawStr(0,10,"Screen 3!");	// write something to the internal memory
  u8g2.sendBuffer();					// transfer internal memory to the display
  
  // Create a sceenshot. The picture (XBM or PBM format) is sent to the serial output. 
  // Copy and paste the output from the Arduino serial monitor into a file.
  // Depending on the display controller use u8g2.writeBufferXBM() or u8g2.writeBufferXBM2()
  // For SH1122, LD7032, ST7920, ST7986, LC7981, T6963, SED1330, RA8835, MAX7219, LS0?
  // use u8g2.writeBufferXBM2(), for all others use u8g2.writeBufferXBM()
  u8g2.writeBufferXBM(DBG_OUTPUT_PORT);			// Write XBM image to serial out
  delay(500);  
}

void loop(void)
{
  server.handleClient();
  MDNS.update();

  // 检测是否按了按钮
  if (digitalRead(ctsPin) == HIGH)
  {
    // button pressed
    modeChangeRequested = true;
  }

  // 间隔时间还没到，不刷新屏幕
  if (!timeToChange)
  {
    return;
  }

  // 是否切换显示模式
  if (modeChangeRequested)
  {
    // increment mode (reset after 2)
    displayMode++;
    if (displayMode >= maxDisplayMode)
    {
      displayMode = 0;
    }

    modeChangeRequested = false;
  }

  // act according to mode
  switch (displayMode)
  {
  case 0: // Mode0
    DBG_OUTPUT_PORT.println("show display mode 0 started");
    showDisplay0();
    break;

  case 1: // Mode1
    DBG_OUTPUT_PORT.println("show display mode 1 started");
    showDisplay1();
    break;

  case 2: // Mode2
    DBG_OUTPUT_PORT.println("show display mode 2 started");
    showDisplay2();
    break;

  case 3: // Mode3
    DBG_OUTPUT_PORT.println("show display mode 3 started");
    showDisplay3();
    break;
  }
}
