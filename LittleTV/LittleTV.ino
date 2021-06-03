
/*
0.96 气象站 更新日志：

V4.3 
网页服务在天气更新时关闭


V4.2
添加文件上传界面

V4.1
OTA网页修改的好看了一点
V4：
实现在线OTA,但是有瑕疵，开机20分钟后关闭网页服务，电脑无法监控状态
增加备用设置网页set，为后续设置留接口

V3：
增加高考倒计时
修复动画与pc参数显示的冲突


By 南湘小隐  
*/
#define FS_CONFIG
//兼容1.1.x版本库
#include <wifi_link_tool.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <U8g2lib.h>
#include <EEPROM.h>
#include "zlt.h"
#include <FS.h>
#include <WiFiClientSecure.h>
#include <WiFiClientSecureBearSSL.h>
#include <Time.h>
#include <TimeLib.h>
#include <WiFiUdp.h>

#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

String keys = "***";      // 接口地址：https://console.heweather.com/app/index 自己申请一个
String dq = "101280604";  // 填入城市编号  获取编号 https://where.heweather.com/index.html
String UID = "177613639"; //  B站UID,用以显示粉丝数
String page = "3";        //设定显示界面1.B站粉丝界面/2.天气界面/ 3.天气第二界面/4.高考倒计时

#define sck D1 /* 屏幕 */
#define sda D2
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* clock=*/sck, /* data=*/sda, /* reset=*/U8X8_PIN_NONE);

const int CONFIG_HTTPPORT = 80;
const int CONFIG_HTTPSPORT = 443;

std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);

int col = 999;
//String          diqu  = "";
int tq = 0;            //天气代码
String pm2 = "";       //pm2.5
String aqi = "";       //空气质量参数
String hum = "";       //空气湿度
String bfs = "NO UID"; //bilbili 粉丝数
String wendu = "";     //温度
String tim = "00:00";
String dat = "2019/01/01";
int previousday = 0; //用于判断是否过了一天
//int           previousminutes  =  0;    //用于判断分钟改变后刷新屏幕
int lastday = 1; //倒计时天数
int pcboot = 0;  //pc请求识别flag
int webflag = 0; //网页服务开启标志

unsigned long previousMillis = 0;
const long interval = 30000; /* 30秒更新屏幕 */
unsigned long previousMillis2 = 0;
const long interval2 = 3600000; /* 更新天气频率 */
unsigned long previousMillis3 = 0;
const long interval3 = 70000; /* 动画更新时间 ，避免和屏幕刷新时间冲突*/

int flag1 = 0; //网页参数是否修改标记
int flag2 = 0;
int flag3 = 0;
int flag4 = 0;
int flag6 = 0;
int flag5 = 0; //所有网页函数是否都运行完成

IPAddress timeServer(120, 25, 115, 20); /* 阿里云ntp服务器 如果失效可以使用 120.25.115.19   120.25.115.20 */
#define STD_TIMEZONE_OFFSET +8          /* 设置中国 */
const int timeZone = 8;                 /* 修改北京时区 */
WiFiUDP Udp;

unsigned int localPort = 8266; /* 修改udp 有些路由器端口冲突时修改 */
int servoLift = 1500;

File fsUploadFile; // 建立文件对象用于闪存文件上传

void setup()
{
    WiFiServer HTTPserver(CONFIG_HTTPPORT);
    WiFiServerSecure HTTPSserver(CONFIG_HTTPSPORT);
    Serial.begin(115200); // 开启串口
    /* ///////////////////////////////////////////////////////基础设置////////////////////////////////////////////////////////*/
    rstb = D3;     /* 重置io */
    stateled = D4; /* 指示灯io */
                   // Hostname = "oled气象站";  /* 设备名称 允许中文名称 不建议太长 */
    wxscan = true; /* 是否被小程序发现设备 开启意味该设备具有后台 true开启 false关闭 */

    u8g2.begin();
    u8g2.enableUTF8Print();
    u8g2.setFont(u8g2_font_unifont_t_chinese2);
    u8g2.setFontDirection(0);
    u8g2.clearBuffer();
    u8g2.setCursor(0, 15);
    u8g2.print("开机");
    u8g2.setCursor(0, 30);
    u8g2.print("WiFi to Link");
    u8g2.sendBuffer();

    if (SPIFFS.begin())
    { // 启动闪存文件系统
        Serial.println("SPIFFS Started.");
    }
    else
    {
        Serial.println("SPIFFS Failed to Start.");
    }

    /* 获取网页信息服务开启 1/3 --开启网页服务*/
    webServer.on("/pc", pc);        //pc性能监控服务
    webServer.on("/zdws", zdws);    //和风keys
    webServer.on("/wsdw", wsdw);    //城市id
    webServer.on("/yggd", yggd);    //b站uid
    webServer.on("/txhm", txhm);    //界面选择：1.B站粉丝界面；2天气界面； 3天气第二界面
    webServer.on("/zgwd", zgwd);    //倒计时
    webServer.on("/ota", ota);      //固件升级网页
    webServer.on("/set", set);      //备用设置网页
    webServer.on("/seta", seta);    //备用设置01
    webServer.on("/up", up);        //   SPIFFS 文件上传页面
    webServer.on("/upload",         // SPIFFS 文件上传处理函数
                 HTTP_POST,         // 向服务器发送文件(请求方法POST)
                 respondOK,         // 则回复状态码 200 给客户端
                 handleFileUpload); // 并且运行处理文件上传函数
    webServer.on("/update", HTTP_POST, updateset1, updateset2);
    load();

    /* 初始化WiFi link tool */
    Serial.println("设置 UDP");
    Udp.begin(localPort);
    Serial.print("Local port: ");
    Serial.println(Udp.localPort());
    Serial.println("正在等待同步");

    if (link())
    {
        /*获取网页信息服务开启 2/3 --判断flash中是有数据*/
        EEPROM.begin(4096);
        if (get_String(EEPROM.read(3000), 3001).length() > 50)
        {
            set_String(3000, 3001, keys); //如果flash中的字符长度大于50，表明flash中没有数据，将keys的值写入flash
        }
        else
        {
            keys = get_String(EEPROM.read(3000), 3001); //和风keys  如果flash中有参数，读取读取参数
        }

        if (get_String(EEPROM.read(3100), 3101).length() > 50)
        {
            set_String(3100, 3101, dq);
        }
        else
        {
            dq = get_String(EEPROM.read(3100), 3101); //城市id
        }

        if (get_String(EEPROM.read(3200), 3201).length() > 50)
        {
            set_String(3200, 3201, UID);
        }
        else
        {
            UID = get_String(EEPROM.read(3200), 3201); //b站uid
        }

        if (get_String(EEPROM.read(3300), 3301).length() == 1)
        {
            page = get_String(EEPROM.read(3300), 3301); //界面选择：1.B站粉丝界面；2天气界面； 3天气第二界面
        }
        else
        {
            set_String(3300, 3301, page);
        }

        EEPROM.get(3400, lastday); // 倒计时参数，如果flash中没有参数，也没必要使用默认值

        Serial.println("01-和风keys：");
        Serial.println(keys);
        Serial.println("01-城市id：");
        Serial.println(dq);
        Serial.println("01-b站uid：");
        Serial.println(UID);
        Serial.println("01-界面选择：");
        Serial.println(page);
        Serial.println("01-倒计时天数：");
        Serial.println(lastday);

        u8g2.setFont(u8g2_font_unifont_t_chinese2);
        u8g2.setFontDirection(0);
        u8g2.clearBuffer();
        u8g2.setCursor(0, 15);
        u8g2.print("开机");
        u8g2.setCursor(0, 30);
        u8g2.print("WiFi to Link");
        u8g2.setCursor(0, 45);
        u8g2.print("OK V4.3"); //-----------------------------版本号
        u8g2.setCursor(0, 60);
        u8g2.print(WiFi.localIP());
        u8g2.sendBuffer();
        sjfx();
        delay(1000);
        donghua2();
        setSyncProvider(getNtpTime);
        previousday = day(); //初始化倒计时天数
        shuaxin();
    }
    else
    {
        u8g2.setFont(u8g2_font_unifont_t_chinese2);
        u8g2.setFontDirection(0);
        u8g2.clearBuffer();
        u8g2.setCursor(45, 15);
        u8g2.print("配网");
        u8g2.setCursor(0, 30);
        u8g2.print("Wifi Link To:");
        u8g2.setCursor(0, 45);
        u8g2.print("wifi_link_tool");
        u8g2.setCursor(0, 60);
        u8g2.print("IP:6.6.6.6");
        u8g2.sendBuffer();
        delay(5000);
        u8g2.clearBuffer();
        u8g2.drawXBMP(0, 0, 128, 64, wx);
        u8g2.sendBuffer();
    }
}

/*---------------------------------------------------------------网页服务处理函数----------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------------------------------*/
void seta() //--------------备用设置01
{
    String a = webServer.arg("seta");
    Serial.println("备用设置1");
    Serial.println(a);
    delay(5000);
}
void ota() //----------------固件升级函数
{
    File file = SPIFFS.open("/ota.html", "r");
    webServer.streamFile(file, "text/html");
    file.close();
}
void up() //------------------文件上传页面打开
{
    File file = SPIFFS.open("/upload.html", "r");
    webServer.streamFile(file, "text/html");
    file.close();
}
void set() //-----------------备用设置函数
{
    File file = SPIFFS.open("/set.html", "r");
    webServer.streamFile(file, "text/html");
    file.close();
}
// 获取网页信息服务开启 3/3 对应1的处理函数
void zdws()
{ // 和风keys
    String a = webServer.arg("zdws");
    if (a != "" && a != get_String(EEPROM.read(3000), 3001)) //如果网页获取的参数不是空值 且 与flash中的值不同
    {
        keys = a;
        set_String(3000, 3001, keys);
        flag1 = 1; // 标志着网页参数1改变
    }
    flag5 = flag5 + 1; //本函数运行之后 ，标志位加1
    Serial.println("和风keys");
    Serial.println(keys);
}
void wsdw()
{ //城市id
    String b = webServer.arg("wsdw");
    if (b != "" && b != get_String(EEPROM.read(3100), 3101))
    {
        dq = b;
        set_String(3100, 3101, dq);
        flag2 = 1; // 标志着网页参数2改变
    }
    flag5 = flag5 + 1; //本函数运行之后 ，标志位加1
    Serial.println("城市id");
    Serial.println(dq);
}
void yggd()
{ //b站uid
    String c = webServer.arg("yggd");
    if (c != "" && c != get_String(EEPROM.read(3200), 3201))
    {
        UID = c;
        set_String(3200, 3201, UID);
        flag3 = 1; // 标志着网页参数3改变
    }
    flag5 = flag5 + 1; //本函数运行之后 ，标志位加1
    Serial.println("b站uid");
    Serial.println(UID);
}
void txhm()
{ //界面选择：1.B站粉丝界面；2天气界面； 3天气第二界面
    String d = webServer.arg("txhm");
    if (d != "" && d != get_String(EEPROM.read(3300), 3301))
    {
        page = d;
        set_String(3300, 3301, page);
        flag4 = 1; // 标志着网页参数4改变
    }
    flag5 = flag5 + 1; //本函数运行之后 ，标志位加1
    Serial.println("界面选择");
    Serial.println(page);
}
void zgwd()
{ // 倒计时数据处理
    String e = webServer.arg("zgwd");
    EEPROM.get(3400, lastday);
    if (e != "" && e.toInt() != lastday)
    {
        lastday = e.toInt();
        EEPROM.put(3400, lastday);
        EEPROM.commit();
        flag6 = 1;
    }
    Serial.println("倒计时");
    Serial.println(lastday);
    flag5 = flag5 + 1; //本函数运行之后 ，标志位加1
    Serial.println("网页函数运行数量");
    Serial.println(flag5);
    if (flag5 > 4) //5个函数都运行了,执行重启的动作
    {
        if (flag1 == 1 || flag2 == 1 || flag3 == 1 || flag4 == 1 || flag6 == 1) //任一一个参数发生改变
        {
            setok();       //设定完成后屏幕提示完成
                           //        flag1 = 0;  //重启后，这些参数都恢复默认值
                           //        flag2 = 0;
                           //        flag3 = 0;
                           //        flag4 = 0;
                           //        flag5 = 0;
            ESP.restart(); //判断逻辑是，如果都执行了且有参数改变后才重启
        }
    }
}

//---------------------------------------------写入字符串函数
//a写入字符串长度位置，b是起始位，str为要保存的字符串
void set_String(int a, int b, String str)
{
    EEPROM.write(a, str.length()); //EEPROM第a位，写入str字符串的长度
    //把str所有数据逐个保存在EEPROM
    for (int i = 0; i < str.length(); i++)
    {
        EEPROM.write(b + i, str[i]);
    }
    EEPROM.commit();
}

//-------------------------------------读字符串函数
//a位是字符串长度，b是起始位
String get_String(int a, int b)
{
    String data = "";
    //从EEPROM中逐个取出每一位的值，并链接
    for (int i = 0; i < a; i++)
    {
        data += char(EEPROM.read(b + i));
    }
    return data;
}
//----------------------------------------------OTA处理
void updateset1()
{
    webServer.sendHeader("Connection", "close");
    webServer.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
}
void updateset2()
{
    HTTPUpload &upload = webServer.upload();
    if (upload.status == UPLOAD_FILE_START)
    {
        Serial.setDebugOutput(true);
        WiFiUDP::stopAll();
        Serial.printf("Update: %s\n", upload.filename.c_str());
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (!Update.begin(maxSketchSpace))
        { //start with max available size
            Update.printError(Serial);
        }
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
        {
            Update.printError(Serial);
        }
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
        if (Update.end(true))
        { //true to set the size to the current progress
            Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        }
        else
        {
            Update.printError(Serial);
        }
        Serial.setDebugOutput(false);
    }
    yield();
}
//-------------------------------------上传文件处理函数
void respondOK()
{
    webServer.send(200);
}

void handleFileUpload()
{

    HTTPUpload &upload = webServer.upload();

    if (upload.status == UPLOAD_FILE_START)
    { // 如果上传状态为UPLOAD_FILE_START

        String filename = upload.filename; // 建立字符串变量用于存放上传文件名
        Serial.printf("Update: %s\n", upload.filename.c_str());
        //  (1) 如果旧文件中存在同名文件,删除旧文件
        if (SPIFFS.exists(upload.filename.c_str()))
        {
            Serial.print(upload.filename.c_str());
            Serial.println(" FOUND.");
            //SPIFFS.rename(upload.filename.c_str(),(upload.filename+".BAK").c_str());
            SPIFFS.remove(upload.filename.c_str());
        }

        if (!filename.startsWith("/"))
            filename = "/" + filename;            // 为上传文件名前加上"/"
        Serial.println("File Name: " + filename); // 通过串口监视器输出上传文件的名称

        fsUploadFile = SPIFFS.open(filename, "w"); // 在SPIFFS中建立文件用于写入用户上传的文件数据
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    { // 如果上传状态为UPLOAD_FILE_WRITE

        if (fsUploadFile)
            fsUploadFile.write(upload.buf, upload.currentSize); // 向SPIFFS文件写入浏览器发来的文件数据
    }
    else if (upload.status == UPLOAD_FILE_END)
    { // 如果上传状态为UPLOAD_FILE_END
        if (fsUploadFile)
        {                                                 // 如果文件成功建立
            fsUploadFile.close();                         // 将文件关闭
            Serial.println(" Size: " + upload.totalSize); // 通过串口监视器输出文件大小
                                                          //webServer.sendHeader("Location","/success.html");  // 将浏览器跳转到/success.html（成功上传页面）
            File file = SPIFFS.open("/success.html", "r");
            webServer.streamFile(file, "text/html");
            file.close();
            webServer.send(303); // 发送相应代码303（重定向到新页面）
        }
        else
        {                                                                   // 如果文件未能成功建立
            Serial.println("File upload failed");                           // 通过串口监视器输出报错信息
            webServer.send(500, "text/plain", "500: couldn't create file"); // 向浏览器发送相应代码500（服务器错误）
        }
    }
}

//----------------------------------电脑性能监控处理
void pc()
{
    pcboot = 1;
    String clk = webServer.arg("clk");
    String cpu = webServer.arg("cpu");
    String ram = webServer.arg("ram");
    String cput = webServer.arg("cput");
    webServer.arg("cpuv");
    u8g2.clearBuffer();
    u8g2.drawXBMP(0, 0, 128, 64, pctp);
    u8g2.setFont(u8g2_font_crox5hb_tf);
    u8g2.setFontDirection(0);
    u8g2.setCursor(33, 25);
    u8g2.print(cpu); //cpu
    u8g2.setCursor(33, 57);
    u8g2.print(cput); //cput
    u8g2.setCursor(95, 57);
    u8g2.print(ram); //ram
    u8g2.setFont(u8g2_font_ncenR10_tf);
    u8g2.setFontDirection(0);
    u8g2.setCursor(96, 16);
    u8g2.print(clk); //mhz
    u8g2.setCursor(94, 30);
    u8g2.print("MHz");
    u8g2.sendBuffer();
    webServer.send(200, "text/plain", "ojbk");
}

/*-----------------------------------------------------处理函数---------------------------------------------------------------------------------
----------------------------------------------------------------------------------------------------------------------------------------*/

//----------------------------高考倒计时函数
void djs()
{
    int currentday = day();
    if (currentday != previousday)
    {
        previousday = currentday;
        if (lastday != 0)
        {
            lastday = lastday - 1;
            EEPROM.put(3400, lastday); //将此值写入flash
            EEPROM.commit();
        }
        //Serial.println("previousday");
        //Serial.println(previousday);
        //Serial.println("lastday");
        //Serial.println(lastday);
    }
}
/*
//时间刷新
void timerefrash(){
  int currentminutes = minute();
  if(currentminutes != previousminutes )
   {
    previousminutes = currentminutes;
    shuaxin();  //刷新屏幕
    //Serial.println("previousminutes"); 
    //Serial.println(previousminutes);   
   }
}
*/

//-------------------------------参数设定确认界面
void setok()
{
    u8g2.clearBuffer();
    u8g2.drawXBMP(0, 5, 20, 20, ok1); //参数设置完成
    u8g2.drawXBMP(20, 5, 20, 20, ok2);
    u8g2.drawXBMP(40, 5, 20, 20, ok3);
    u8g2.drawXBMP(60, 5, 20, 20, ok4);
    u8g2.drawXBMP(80, 5, 20, 20, ok5);
    u8g2.drawXBMP(100, 5, 20, 20, ok6);
    u8g2.drawXBMP(30, 30, 20, 20, ok7); //重启中
    u8g2.drawXBMP(50, 30, 20, 20, ok8);
    u8g2.drawXBMP(70, 30, 20, 20, ok9);
    u8g2.sendBuffer();
    delay(2000);
}

//-------------------------------------天气刷新函数
void sjfx()
{
    delay(1000);
    xx();
    delay(1000);
    pm();
    delay(1000);
    col = tq;

    //Serial.println("天气刷新中...");
    //Serial.println( "当前地区： " + diqu );
    //Serial.println( "天气代码： " );
    //Serial.println( tq );
    //Serial.println( "体感温度： " + wendu );
    //Serial.println( "PM2.5： " + pm2 );
}

//------------------------------------屏幕刷新函数
void shuaxin()
{

    if (pcboot == 0)
    {

        /* setSyncProvider(getNtpTime); */
        String zov = "";
        String zod = "";
        if (hour() < 10)
        {
            zov = "0";
        }
        if (minute() < 10)
        {
            tim = zov + String(hour()) + ":0" + String(minute());
        }
        else
        {
            tim = zov + String(hour()) + ":" + String(minute());
        }
        if (month() < 10)
        {
            zod = "0";
        }
        if (day() < 10)
        {
            dat = String(year()) + "/" + zod + String(month()) + "/0" + String(day());
        }
        else
        {
            dat = String(year()) + "/" + zod + String(month()) + "/" + String(day());
        }

        //Serial.print( tim );    /* 输出当前网络分钟 */
        //Serial.print( dat );    /* 输出当前日期 */
        u8g2.clearBuffer();
        tubiao(); //天气图标

        if (page == "1") //1.B站粉丝界面
        {
            blbluid(); //刷新b站粉丝
            u8g2.setFont(u8g2_font_ncenB18_tf);
            u8g2.setFontDirection(0);
            u8g2.setCursor(65, 53);
            u8g2.print(wendu); //温度
            u8g2.setFont(u8g2_font_wqy12_t_chinese2);
            u8g2.setFontDirection(0);
            u8g2.setCursor(63, 64);
            u8g2.print(dat); //日期
            u8g2.drawXBMP(103, 37, 16, 16, col_ssd1);

            u8g2.setFont(u8g2_font_ncenR12_tf);
            u8g2.setFontDirection(0);
            u8g2.setCursor(0, 50);
            u8g2.print("bilibili");

            //     u8g2.drawXBMP( 0, 39, 12, 12, id6 );  //名字定制
            //     u8g2.drawXBMP( 12, 39, 12, 12, zhan );
            //     u8g2.drawXBMP( 24, 39, 12, 12, fen );
            //     u8g2.drawXBMP( 36, 39, 12, 12, si );
            //     u8g2.drawXBMP( 48, 39, 12, 12, id5 );

            u8g2.setFont(u8g2_font_bauhaus2015_tn);
            u8g2.setCursor(0, 64);
            u8g2.print(bfs); // 刷新粉丝
            u8g2.setFont(u8g2_font_fub25_tf);
            u8g2.setFontDirection(0);
            u8g2.setCursor(40, 32);
            u8g2.print(tim);
            u8g2.sendBuffer();
        }
        else if (page == "2") // 2.默认天气界面  pm2.5
        {
            u8g2.setFont(u8g2_font_ncenB18_tf);
            u8g2.setFontDirection(0);
            u8g2.setCursor(65, 53);
            u8g2.print(wendu); //温度
            u8g2.setFont(u8g2_font_wqy12_t_chinese2);
            u8g2.setFontDirection(0);
            u8g2.setCursor(63, 64);
            u8g2.print(dat); //日期
            u8g2.drawXBMP(103, 37, 16, 16, col_ssd1);
            u8g2.setFont(u8g2_font_ncenR12_tf);
            u8g2.setFontDirection(0);
            u8g2.setCursor(0, 48);
            u8g2.print("pm2.5");
            u8g2.setCursor(10, 62);
            u8g2.print(pm2); //pm2.5 浓度
            u8g2.setFont(u8g2_font_fub25_tf);
            u8g2.setFontDirection(0);
            u8g2.setCursor(40, 32);
            u8g2.print(tim);
            u8g2.sendBuffer();
        }
        else if (page == "3") //3.天气界面  温湿度  aqi
        {
            if (wendu.length() < 3)
            {
                u8g2.drawXBMP(30, 38, 16, 16, col_ssd1);
                u8g2.setFont(u8g2_font_VCR_OSD_tf);
                u8g2.setFontDirection(0);
                u8g2.setCursor(5, 53);
                u8g2.print(wendu); //体感温度
            }
            else
            {
                u8g2.setFont(u8g2_font_VCR_OSD_tf);
                u8g2.setFontDirection(0);
                u8g2.setCursor(5, 53);
                u8g2.print(wendu); //体感温度
            }
            u8g2.setFont(u8g2_font_8x13_t_symbols);
            u8g2.setFontDirection(0);
            u8g2.setCursor(3, 64);
            u8g2.print(dat); //日期
            u8g2.setFont(u8g2_font_8x13_t_symbols);
            u8g2.setFontDirection(0);
            u8g2.setCursor(102, 64);
            u8g2.print("AQI"); //空气质量符号

            if (aqi.length() < 3)
            {
                u8g2.setFont(u8g2_font_VCR_OSD_tf);
                u8g2.setFontDirection(0);
                u8g2.setCursor(52, 53);
                u8g2.print(hum); //实际湿度
                u8g2.setFont(u8g2_font_luBS12_tr);
                u8g2.setFontDirection(0);
                u8g2.setCursor(77, 53);
                u8g2.print("%");
                u8g2.setFont(u8g2_font_VCR_OSD_tf);
                u8g2.setFontDirection(0);
                u8g2.setCursor(100, 53);
                u8g2.print(aqi); //空气质量数值
            }
            else
            {
                u8g2.setFont(u8g2_font_VCR_OSD_tf);
                u8g2.setFontDirection(0);
                u8g2.setCursor(50, 53);
                u8g2.print(hum); //实际湿度
                u8g2.setFont(u8g2_font_luBS12_tr);
                u8g2.setFontDirection(0);
                u8g2.setCursor(75, 53);
                u8g2.print("%");
                u8g2.setFont(u8g2_font_VCR_OSD_tf);
                u8g2.setFontDirection(0);
                u8g2.setCursor(92, 53);
                u8g2.print(aqi); //空气质量数值
            }
            u8g2.setFont(u8g2_font_fub25_tf); //u8g2_font_fub25_tf
            u8g2.setFontDirection(0);
            u8g2.setCursor(40, 30);
            u8g2.print(tim); //时间
            u8g2.sendBuffer();
        }
        else if (page == "4") //4.倒计时界面
        {
            u8g2.setFont(u8g2_font_ncenB18_tf);
            u8g2.setFontDirection(0);
            u8g2.setCursor(65, 53);
            u8g2.print(wendu);                        //温度
            u8g2.setFont(u8g2_font_wqy12_t_chinese2); //u8g2_font_6x10_tf
            u8g2.setFontDirection(0);
            u8g2.setCursor(63, 64);
            u8g2.print(dat); //日期
            u8g2.drawXBMP(103, 37, 16, 16, col_ssd1);

            u8g2.drawXBMP(0, 39, 12, 12, ju); //距高考
            u8g2.drawXBMP(17, 39, 12, 12, gao);
            u8g2.drawXBMP(34, 39, 12, 12, kao);
            u8g2.drawXBMP(36, 52, 12, 12, tian); //天

            u8g2.setFont(u8g2_font_bauhaus2015_tn);
            u8g2.setCursor(6, 64);
            u8g2.print(lastday); // 刷新倒计时天数
            u8g2.setFont(u8g2_font_fub25_tf);
            u8g2.setFontDirection(0);
            u8g2.setCursor(40, 32);
            u8g2.print(tim);
            u8g2.sendBuffer();
        }
    }
    else
    {
        pcboot = 0;
    }
}

//-----------------------------------天气图标
void tubiao()
{

    if (col == 100 || col == 150)
    { //晴
        u8g2.drawXBMP(0, 0, 40, 40, col_100);
    }
    else if (col == 102 || col == 101)
    {
        u8g2.drawXBMP(0, 0, 40, 40, col_102); //云
    }
    else if (col == 103 || col == 153)
    {
        u8g2.drawXBMP(0, 0, 40, 40, col_103); //晴间多云
    }
    else if (col == 104 || col == 154)
    {
        u8g2.drawXBMP(0, 0, 40, 40, col_104); //阴
    }
    else if (col >= 300 && col <= 301)
    {
        u8g2.drawXBMP(0, 0, 40, 40, col_301); //阵雨
    }
    else if (col >= 302 && col <= 303)
    {
        u8g2.drawXBMP(0, 0, 40, 40, col_302); //雷阵雨
    }
    else if (col == 304)
    {
        u8g2.drawXBMP(0, 0, 40, 40, col_304); //冰雹
    }
    else if (col == 399 || col == 314 || col == 305 || col == 306 || col == 307 || col == 315 || col == 350 || col == 351)
    {
        u8g2.drawXBMP(0, 0, 40, 40, col_307); //雨
    }
    else if ((col >= 308 && col <= 313) || (col >= 316 && col <= 318))
    {
        u8g2.drawXBMP(0, 0, 40, 40, col_310); //暴雨
    }
    else if ((col >= 402 && col <= 406) || col == 409 || col == 410 || col == 400 || col == 401 || col == 408 || col == 499 || col == 456)
    {
        u8g2.drawXBMP(0, 0, 40, 40, col_401); //雪
    }
    else if (col == 407 || col == 457)
    {
        u8g2.drawXBMP(0, 0, 40, 40, col_407); //阵雪
    }
    else if (col >= 500 && col <= 502)
    {
        u8g2.drawXBMP(0, 0, 40, 40, col_500); //雾霾
    }
    else if (col >= 503 && col <= 508)
    {
        u8g2.drawXBMP(0, 0, 40, 40, col_503); //沙尘暴
    }
    else if (col >= 509 && col <= 515)
    {
        u8g2.drawXBMP(0, 0, 40, 40, col_509); //不适宜生存
    }
    else
    {
        u8g2.drawXBMP(0, 0, 40, 40, col_999); //未知
    }
}

//-----------------------------天气数据获取
void xx()
{
    String line;
    std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
    client->setInsecure();
    HTTPClient https;
    if (https.begin(*client, "https://devapi.heweather.net/v7/weather/now?gzip=n&location=" + dq + "&key=" + keys))
    {
        int httpCode = https.GET();
        if (httpCode > 0)
        {
            if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
            {
                line = https.getString();
            }
        }
        https.end();
    }
    else
    {
        Serial.printf("[HTTPS]请求链接失败\n");
    }
    Serial.println("接口返回" + line);
    DynamicJsonBuffer jsonBuffer(1024);
    JsonObject &res_json = jsonBuffer.parseObject(line);
    //String r1=res_json["basic"]["location"];//地区
    int r2 = res_json["now"]["icon"];        //天气
    String r3 = res_json["now"]["temp"];     //温度
    String r4 = res_json["now"]["humidity"]; //humidity湿度
    jsonBuffer.clear();
    //diqu  = r1; /* 地区 */
    if (r2 != 0)
    {
        tq = r2;
    }
    if (r3 != "")
    {
        wendu = r3; //体感温度
        if (r4 == "100")
        {
            r4 = "99";
        }
        hum = r4;
    }
}

//--------------------------------空气质量获取
void pm()
{

    String line;
    std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
    client->setInsecure();
    HTTPClient https;
    if (https.begin(*client, "https://devapi.heweather.net/v7/air/now?gzip=n&location=" + dq + "&key=" + keys))
    {
        int httpCode = https.GET();
        if (httpCode > 0)
        {
            if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
            {
                line = https.getString();
            }
        }
        https.end();
    }
    else
    {
        Serial.printf("[HTTPS]请求链接失败\n");
    }

    Serial.println("接口返回" + line);
    DynamicJsonBuffer jsonBuffer(1024);
    JsonObject &res_json = jsonBuffer.parseObject(line);
    String r1 = res_json["now"]["pm2p5"]; //pm25
    String r2 = res_json["now"]["aqi"];   //空气质量
    jsonBuffer.clear();
    if (r1 != "")
    {
        pm2 = r1; //pm2.5
        aqi = r2; //空气质量
    }
}

//--------------------------------b站粉丝采集函数
void blbluid()
{
    WiFiClient client;
    HTTPClient http;
    if (http.begin(client, "http://api.bilibili.com/x/relation/stat?vmid=" + UID))
    {
        int httpCode = http.GET();
        String payload = http.getString();
        http.end();
        DynamicJsonBuffer jsonBuffer;
        JsonObject &res_json = jsonBuffer.parseObject(payload);
        String follower = res_json["data"]["follower"];
        if (follower != "")
        {
            bfs = follower;
        }
        jsonBuffer.clear();
    }
}

//-------------------------------第一个动画效果 bilibili

void donghua2()
{
    int i = 0;
    u8g2.clearBuffer();
    for (i = 0; i <= 4; i++)
    {
        u8g2.drawXBMP(0, 0, 128, 64, bili_Logo_1);
        u8g2.sendBuffer();
        delay(66);

        u8g2.drawXBMP(0, 0, 128, 64, bili_Logo_2);
        u8g2.sendBuffer();
        delay(66);

        u8g2.drawXBMP(0, 0, 128, 64, bili_Logo_3);
        u8g2.sendBuffer();
        delay(66);

        u8g2.drawXBMP(0, 0, 128, 64, bili_Logo_2);
        u8g2.sendBuffer();
        delay(66);
    }
}

/*----------------------------------------------------解包设置---------------------------------------------------------------------------//////////////////////////////////// */
void digitalClockDisplay()
{
    /*  */
    Serial.print(hour());
    printDigits(minute());
    Serial.println();
}

void printDigits(int digits)
{
    Serial.print(":");
    if (digits < 10)
        Serial.print('0');
    Serial.print(digits);
}

const int NTP_PACKET_SIZE = 48;
byte packetBuffer[NTP_PACKET_SIZE];

time_t getNtpTime()
{
    while (Udp.parsePacket() > 0)
        ;
    Serial.println("连接时间服务器");
    sendNTPpacket(timeServer);
    uint32_t beginWait = millis();
    while (millis() - beginWait < 1500)
    {
        int size = Udp.parsePacket();
        if (size >= NTP_PACKET_SIZE)
        {
            Serial.println("时间服务器应答");
            Udp.read(packetBuffer, NTP_PACKET_SIZE);
            unsigned long secsSince1900;
            /* convert four bytes starting at location 40 to a long integer */
            secsSince1900 = (unsigned long)packetBuffer[40] << 24;
            secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
            secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
            secsSince1900 |= (unsigned long)packetBuffer[43];
            return (secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR);
        }
    }
    Serial.println("No NTP Response :-(");
    return (0);
}

void sendNTPpacket(IPAddress &address)
{
    memset(packetBuffer, 0, NTP_PACKET_SIZE);

    packetBuffer[0] = 0b11100011;
    packetBuffer[1] = 0;
    packetBuffer[3] = 0xEC;

    packetBuffer[12] = 49;
    packetBuffer[13] = 0x4E;
    packetBuffer[14] = 49;
    packetBuffer[15] = 52;
    Udp.beginPacket(address, 123);
    Udp.write(packetBuffer, NTP_PACKET_SIZE);
    Udp.endPacket();
}

/**********--------------------------------------**主函数**------------------------------------------------------------------------------------*********/
void loop()
{
    if (webflag == 0)
    {
        // pant(); /* WiFi link tool 服务维持函数  请勿修改位置 */
        if (millis() / 1000 < 60)
        {
            dnsServer.processNextRequest();
        }
        else
        {
            dnsServer.stop();
        }
        webServer.handleClient();
    }
    else
    {
        webServer.stop(); //关闭服务，减少内存占用
    }

    if (link())
    {
        djs();                                  // 高考倒计时函数
        unsigned long currentMillis = millis(); //旧的刷新屏幕的方法
        if (currentMillis - previousMillis >= interval)
        {
            previousMillis = currentMillis;
            shuaxin();
        }

        unsigned long currentMillis2 = millis(); //天气刷新
        if (currentMillis2 - previousMillis2 >= interval2)
        {
            webflag = webflag + 1;
            //     Serial.println("webflag");
            //     Serial.println(webflag);
            if (webflag == 2)
            {
                previousMillis2 = currentMillis2;
                sjfx();
                Serial.println("webon");
                Serial.println(millis() / 1000);
                webflag = 0;
                webServer.begin();
                //dnsServer.start(DNS_PORT, "*", apIP);
            }
        }

        unsigned long currentMillis3 = millis();
        if (currentMillis3 - previousMillis3 >= interval3) //动画刷新
        {
            previousMillis3 = currentMillis3;
            donghua2();
            if (pcboot == 1)
            {
                pc();
            }
            else
            {
                shuaxin();
            }
        }
    }
}
