#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <Ticker.h>
#include <LiquidCrystal_I2C.h>

/* Variables */
// config Motor
#define sendChartTime 2   // n Seconds each time update data testSpeed
#define pinControl 0      // PWM
#define pinEncoder 13     // ENCODER
#define SPEEDMAX 300      // 300RPM - 6V
float ratio = 45.0;       // 45 vong quay dong co chinh => 1 vong quay hop so
float pulseEncoder = 3.0; // 3 xung 1 vong quay dong co chinh
float noiseRPM = 10.0;    // RPM la vong/phut
// configMCU
LiquidCrystal_I2C lcd(0x27, 16, 2);
Ticker timer;
Ticker timerMotor;
Ticker timerJobDone;
ESP8266WebServer server;
WebSocketsServer webSocket = WebSocketsServer(81);
char *ssid = "ShakeMachine";
char *password = "lexuankhanh";
void ICACHE_RAM_ATTR handleInterrupt();
// Calculator
volatile int checkSpeed_ = 0;
volatile int pulse = 1;
volatile int rpm = 0;
volatile int id, state = 2, speedSet, timeSet, speedTest;
volatile int flag = 0;
volatile int flagTick = 0;
volatile int flagSend = 0;
volatile int tick = 0;

/* Api */
// LCD
void initLCD()
{
  pinMode(14, OUTPUT);
  pinMode(12, OUTPUT);
  digitalWrite(14, HIGH);
  digitalWrite(12, LOW);
  lcd.init();
  lcd.init();
  lcd.backlight();
}
// WIFI
void initWifi()
{
  WiFi.softAP(ssid, password);
  delay(500);
  lcd.setCursor(0, 0);
  lcd.print("ShakeMachine");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.softAPIP());
}
// Server
static void handleRoot()
{
  File file = SPIFFS.open("/index.html", "r");
  if (!file)
  {
    server.send(500, "text/plain", "Problem with filesystem!\n");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}
static void handleNotFound()
{
  String path = server.uri();
  if (!SPIFFS.exists(path))
  {
    server.send(404, "text/plain", "Path " + path + " not found. Please double-check the URL");
    return;
  }
  String contentType = "text/plain";
  if (path.endsWith(".css"))
  {
    contentType = "text/css";
  }
  else if (path.endsWith(".html"))
  {
    contentType = "text/html";
  }
  else if (path.endsWith(".js"))
  {
    contentType = "application/javascript";
  }
  File file = SPIFFS.open(path, "r");
  server.streamFile(file, contentType);
  file.close();
}
// Websocket
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
  if (type == WStype_TEXT)
  {
    // STATE
    if (payload[0] == 'I')
    {
      int a = 0;
      for (int i = 1; i < length; i++)
      {
        payload[i] = payload[i] - 48;
        a = a * 10 + payload[i];
      }
      state = a;
    }
    if (state == 2)
    {
      // SpeedSet
      if (payload[0] == 'S')
      {
        int a = 0;
        for (int i = 1; i < length; i++)
        {
          payload[i] = payload[i] - 48;
          a = a * 10 + payload[i];
        }
        speedSet = a;
        if (speedSet >= 200)
          speedSet = 200;
      }
      // Time
      if (payload[0] == 'T')
      {
        int b = 0;
        for (int i = 1; i < length; i++)
        {
          payload[i] = payload[i] - 48;
          b = b * 10 + payload[i];
        }
        timeSet = b;
      }
    }
  }
}
// MOTOR
void delayServer()
{
  for (int i = 0; i < 2; i++)
  {
    webSocket.loop();
    server.handleClient();
    delay(50);
  }
}
void ICACHE_RAM_ATTR handleInterrupt()
{
  pulse++;
}
void setSpeedMotor()
{
  if (rpm > SPEEDMAX)
    rpm = SPEEDMAX;
  int duty = (1023 * rpm) / SPEEDMAX;
  analogWrite(pinControl, duty);
  delayServer();
  int duty_ = (1023 * checkSpeed_) / SPEEDMAX;
  duty = duty - (duty_ - duty);
  if (rpm < 100)
    duty += 15;
  if (rpm >= 100)
    duty -= 200;
  analogWrite(pinControl, duty);
}
void checkSpeed()
{
  checkSpeed_ = (pulse / (pulseEncoder * ratio) * 60) / 0.1 - noiseRPM;
  if (pulse == 1)
    checkSpeed_ = 0;
  pulse = 1;
  flag = 1;
}
// WORK
void initData()
{
  // current
  String json = "{\"id\":";
  json += 1;
  json += ",";
  json += "\"state\":";
  json += state;
  json += ",";
  json += "\"speed\":";
  json += speedSet;
  json += ",";
  json += "\"time\":";
  json += timeSet;
  json += "}";
  webSocket.broadcastTXT(json.c_str(), json.length()); // Server TX
}
void jobDone()
{
  tick++;
  flagTick = 1;
  flagSend++;
}
void work()
{
  // run
  if (state == 1)
  {
    timerJobDone.attach(1, jobDone);
    while (true)
    {
      // code
      rpm = speedSet;
      if (flag)
      {
        setSpeedMotor();
        flag = 0;
      }
      if (flagTick == 1)
      {
        if (flagSend == sendChartTime)
        {
          String json = "{\"id\":";
          json += 2;
          json += ",";
          json += "\"value\":";
          json += checkSpeed_;
          json += "}";
          webSocket.broadcastTXT(json.c_str(), json.length()); // Server TX
          flagSend = 0;
        }
        String jsonTick = "{\"id\":";
        jsonTick += 3;
        jsonTick += ",";
        jsonTick += "\"timer\":";
        jsonTick += tick;
        jsonTick += "}";
        webSocket.broadcastTXT(jsonTick.c_str(), jsonTick.length()); // Server TX
        flagTick = 0;
      }
      if (tick == timeSet)
      {
        timerJobDone.detach();
        tick = 0;
        state = 3;
        rpm = 0;
        setSpeedMotor();
        // wait press reset
        while (true)
        {
          if (state == 2)
            break;
          webSocket.loop();
          server.handleClient();
          delay(50);
        }
      }
      if (state == 0)
      {
        timerJobDone.detach();
        while (state == 0)
        {
          rpm = 0;
          setSpeedMotor();
          delayServer();
        }
        timerJobDone.attach(1, jobDone);
      }
      if (state == 2)
      {
        timerJobDone.detach();
        tick = 0;
        speedSet = 0;
        timeSet = 0;
        rpm = 0;
        setSpeedMotor();
        String json = "{\"id\":";
        json += 3;
        json += ",";
        json += "\"timer\":";
        json += tick;
        json += "}";
        webSocket.broadcastTXT(json.c_str(), json.length()); // Server TX
        break;
      }
      webSocket.loop();
      server.handleClient();
      delay(50);
    }
  }
}

/* Main function */
void setup()
{
  analogWrite(pinControl, 0);
  // MCU
  initLCD();
  // WIFI
  initWifi();
  // SERVER
  if (!SPIFFS.begin())
  {
    // Serial.println("Failed to mount filesystem!\n");
  }
  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();
  // WEBSOCKET
  webSocket.begin();
  webSocket.onEvent(webSocketEvent); // Server RX
  // MOTOR
  attachInterrupt(digitalPinToInterrupt(pinEncoder), handleInterrupt, FALLING); // ChannelB,A:Rising
  timerMotor.attach(0.1, checkSpeed);                                           // 0.1s timer
  // WORK
  timer.attach(1, initData);
}
void loop()
{
  work();
  webSocket.loop();
  server.handleClient();
}