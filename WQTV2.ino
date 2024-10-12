#include <Arduino.h>
#include <Wire.h>
#include <DS1307RTC.h>

#include <WiFi.h>
#include <HTTPUpdate.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <cert.h>

#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <SPIFFS.h>

#include "stdint.h"
#include "stdbool.h"
#include "string.h"

#define resetWiFiPin 19
#define led1 5
#define led2 18

// UART Pin definitions
#define RXp2 16
#define TXp2 17

// SD Card Pin definitions
int sck = 14;
int miso = 2;
int mosi = 15;
int cs = 13;

// change interval time for data log in SD card
#define logInterval 30000  // 30s


#define LONG_PRESS_TIME 3000  // Time in milliseconds for a long press (3 seconds)

#define URL_FW_VERSION "https://raw.githubusercontent.com/vandychuob/WQTFrimware/master/bin_version.txt"
#define URL_FW_BIN "https://raw.githubusercontent.com/vandychuob/WQTFrimware/master/WQTV2.ino.bin"

// Define server details and file path
#define HOST "raw.githubusercontent.com"
#define PATH "/vandychuob/WQTFrimware/master/WQTV2.ino.bin"
#define PATHVERSION "vandychuob/WQTFrimware/master/bin_version.txt"
#define PORT 443

String FirmwareVer = {
  "1.0"
};

WiFiClient espClient;
PubSubClient client(espClient);

char* ssid_AP = "WaterQuality";
char* password_AP = "@@123456";

String SSID = "";
String password = "";
String server = "";
String port = "";
String topic = "";
String id = "";
String mqtt_user = "mqtt_iot";
String mqtt_password = "Aa04£8+XP]#q";

const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
const char* serverPath = "/server.txt";
const char* portPath = "/port.txt";
const char* topicPath = "/topic.txt";
const char* idPath = "/id.txt";
const char* usernamePath = "/username.txt";
const char* passwordPath = "/password.txt";
const char* intervalPath = "/interval.txt";
int interval = 30000;

DNSServer dnsServer;
WebServer webServer(80);
const byte DNS_PORT = 53;
char mqttID[20];
char mqttTopic[30];

unsigned long lastLogTime = 0;

volatile bool buttonPressed = false;
unsigned long buttonPressStartTime = 0;
bool longPressDetected = false;

TaskHandle_t Task1;
TaskHandle_t Task2;
TaskHandle_t task3;

// Mutex for SD Card access
SemaphoreHandle_t sdMutex;
bool firmwareUpdate();
bool firmwareVersionCheck();

unsigned long previousMillis = 0;  // will store last time LED was updated
const long fwUpdateInterval = 6000;

void repeatedFWUpdateCall() {
  static int num = 0;
  unsigned long currentMillis = millis();
  if ((currentMillis - previousMillis) >= fwUpdateInterval) {
    // save the last time you blinked the LED
    previousMillis = currentMillis;
    if (FirmwareVersionCheck()) {
      // firmwareUpdate();
    }
  }
}

bool firmwareUpdate(void) {
  WiFiClientSecure httpClient;
  httpClient.setInsecure();
  if (!httpClient.connect(HOST, PORT)) {
    Serial.println("Failed to connect to server");
    return false;
  }

  httpClient.printf("GET %s HTTP/1.1\r\nHost: %s\r\n%s\r\nConnection: close\r\n\r\n", PATH, HOST);

  // Wait for headers and extract content length
  int contentLength = 0;
  bool isValidContentType = false;
  String http_response_code = "error";


  while (httpClient.connected()) {
    String line = httpClient.readStringUntil('\n');
    if (line.startsWith("HTTP/1.1")) {
      http_response_code = line.substring(9, 12);  // Extract the response code (e.g., "200")
    }
    if (line.startsWith("Content-Length: ")) {
      contentLength = line.substring(16).toInt();
    }
    if (line.startsWith("Content-Type: ")) {
      isValidContentType = line.indexOf("application/octet-stream") >= 0;
    }
    if (line == "\r") {
      break;
    }
  }
  Serial.println("HTTP response code: " + http_response_code);  // Print the HTTP response code

  // Check if the response code is not 200 (OK)
  if (http_response_code != "200") {
    Serial.println("Failed to download firmware: HTTP response code " + http_response_code);
    httpClient.stop();
    return false;
  }
  // Check if the content length is available and content type is valid
  if (contentLength && isValidContentType) {
    Serial.printf("Content length: %d\n", contentLength);
    if (!Update.begin(contentLength)) {  // Start with max available size
      Serial.println("Not enough space to begin OTA");
      httpClient.stop();
      return false;
    }

    // Stream firmware directly to flash
    size_t written = Update.writeStream(httpClient);
    if (written == contentLength) {
      Serial.println("Written: " + String(written) + " successfully");
    } else {
      Serial.println("Written only: " + String(written) + "/" + String(contentLength) + ". Retry?");
      httpClient.stop();
      return false;
    }

    if (Update.end()) {
      Serial.println("OTA done!");
      if (Update.isFinished()) {
        Serial.println("Update successfully completed. Rebooting...");
        return true;
      } else {
        Serial.println("Update not finished? Something went wrong!");
      }
    } else {
      Serial.println("Error Occurred. Error #: " + String(Update.getError()));
    }
  } else {
    Serial.println("Invalid content length or content type");
  }
  httpClient.stop();
  return false;
}

bool FirmwareVersionCheck(void) {
 String payload;
  int httpCode;
  String fwurl = "";
  fwurl += URL_FW_VERSION;
  fwurl += "?";
  fwurl += String(rand());
  Serial.println(fwurl);
  WiFiClientSecure* client = new WiFiClientSecure;

  if (client) 
  {
    // client->setCACert(rootCACertificate);

    // Add a scoping block for HTTPClient https to make sure it is destroyed before WiFiClientSecure *client is 
    HTTPClient https;

    if (https.begin(*client, fwurl)) 
    { // HTTPS      
      Serial.print("[HTTPS] GET...\n");
      // start connection and send HTTP header
      delay(1000);
      httpCode = https.GET();
      delay(1000);
      if (httpCode == HTTP_CODE_OK) // if version received
      {
        payload = https.getString(); // save received version
        Serial.println("payload :" + payload);
      } else {
        Serial.print("error in downloading version file:");
        Serial.println(httpCode);
      }
      https.end();
    }
    delete client;
  }
      
  if (httpCode == HTTP_CODE_OK) // if version received
  {
    payload.trim();
    if (payload.equals(FirmwareVer)) {
      Serial.printf("\nDevice already on latest firmware version:%s\n", FirmwareVer);
      return 0;
    } 
    else 
    {
      Serial.println(payload);
      Serial.println("New firmware detected");
      return 1;
    }
  } 
  return 0;
}

void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
  }
  Serial.println("SPIFFS mounted successfully");
}

String readFile(fs::FS& fs, const char* path, String old) {
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("- failed to open file for reading");
    return old;
  }

  String fileContent;
  while (file.available()) {
    fileContent = file.readStringUntil('\n');
    break;
  }
  return fileContent;
}

void writeFile(fs::FS& fs, const char* path, const char* message) {
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("- file written 200");
  } else {
    Serial.println("- write failed 404");
  }
}

String responseHTMLHead = R"(<!DOCTYPE html><html>
<head>
  <title>Water Quality Monitoring</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta charset="utf-8" />
  <link rel="icon" href="data:,">
  <style>
    html {
      font-family: Arial, Helvetica, sans-serif; 
      display: inline-block; 
      text-align: center;
    }

    h1 {
      font-size: 1.8rem; 
      color: white;
    }

    p { 
      font-size: 1.4rem;
    }

    .topnav { 
      overflow: hidden; 
      background-color: #0A1128;
    }

    body {  
      margin: 0;
    }

    .content { 
      padding: 3%;
    }

    .card-grid { 
      max-width: 800px; 
      margin: 0 auto; 
      display: grid; 
      grid-gap: 2rem; 
      grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
    }

    .card { 
      background-color: white; 
      box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5);
    }

    .card-title { 
      font-size: 1.2rem;
      font-weight: bold;
      color: #034078
    }

    input[type=submit] {
      border: none;
      color: #FEFCFB;
      background-color: #034078;
      padding: 15px 15px;
      text-align: center;
      text-decoration: none;
      display: inline-block;
      font-size: 16px;
      width: 100px;
      margin-right: 10px;
      border-radius: 4px;
      transition-duration: 0.4s;
    }

    input[type=submit]:hover {
      background-color: #1282A2;
    }

    input[type=text], input[type=number], select {
      width: 54%;
      padding: 12px 20px;
      margin: 18px;
      display: inline-block;
      border: 1px solid #ccc;
      border-radius: 4px;
      box-sizing: border-box;
    }

    label {
      font-size: 1.2rem;
      display: inline-block;
      width: 32%; /* Fixed width for labels */
      text-align: left; /* Align text to the right of the label */
      margin-left: 10px; /* Adjusted margin */
    }

    .value{
      font-size: 0.9rem;
      color: #1282A2;  
    }

    .state {
      font-size: 1.2rem;
      color: #1282A2;
    }

    button {
      border: none;
      color: #FEFCFB;
      padding: 15px 32px;
      text-align: center;
      font-size: 16px;
      width: 100px;
      border-radius: 4px;
      transition-duration: 0.4s;
    }

    .button-on {
      background-color: #034078;
    }

    .button-on:hover {
      background-color: #1282A2;
    }

    .button-off {
      background-color: #858585;
    }

    .button-off:hover {
      background-color: #252524;
    }
  </style>
</head>
<body>
  <div class="topnav">
    <h1>Water Quality Config File</h1>
  </div>
  <div class="content">
    <div class="card-grid">
      <div class="card">)";

String responseHTMLEnd = R"(</p>
        </form>
      </div>
    </div>
  </div>
</body>
</html>)";

void handleWelcome() {
  webServer.send(200, "text/plain", "WPMS Device is running...");
}

void handlePortal() {
  String html = responseHTMLHead;
  html += "<form action='\config' method='POST'><p>";
  html += "<input type='submit' value='Config'>";
  html += responseHTMLEnd;
  webServer.send(200, "text/html", html);
}

void handleConfig() {
  if (!webServer.authenticate("username", "password"))
    return webServer.requestAuthentication();
  String html = responseHTMLHead;
  html += "<form action='\submit' method='POST'><p>";
  html += "<label for='ssid'> SSID :</label>";
  html += "<input type='text' id='ssid' name='ssid' value =''><br>";
  html += "<label for='pass'>Password:</label>";
  html += "<input type='text' id='pass' name='pass' value = ''><br>";
  html += "<label for='ip'>server:</label>";
  html += "<input type='text' id='server' name='server' value=''><br>";
  html += "<label for='port'>Mqtt port:</label>";
  html += "<input type='text' id='port' name='port' value=''><br>";
  html += "<label for='port'>mqtt_user:</label>";
  html += "<input type='text' id='username' name='username' value=''><br>";
  html += "<label for='port'>mqtt_password:</label>";
  html += "<input type='text' id='password' name='password' value=''><br>";
  html += "<label for='topic'>topic:</label>";
  html += "<input type='text' id='topic' name='topic' value=''><br>";
  html += "<label for='id'>ID:</label>";
  html += "<input type='text' id='id' name='id' value='" + id + "'><br>";
  html += "<label for='interval'>interval:</label>";
  html += "<input type='text' id='interval' name='interval' value='" + String(interval) + "'><br>";
  html += "<input type='submit' value='Submit'>";
  html += responseHTMLEnd;
  webServer.send(200, "text/html", html);
}

//handle for what we summit in web server
void handleSubmit() {
  if (!webServer.authenticate("username", "password"))
    return webServer.requestAuthentication();
  SSID = webServer.arg("ssid");
  password = webServer.arg("pass");
  server = webServer.arg("server");
  port = webServer.arg("port");
  topic = webServer.arg("topic");
  id = webServer.arg("id");
  mqtt_user = webServer.arg("username");
  mqtt_password = webServer.arg("password");
  interval = webServer.arg("interval").toInt();
  if (SSID == "") {
    SSID = " ";
  }
  if (password == "") {
    password = " ";
  }
  Serial.println("SSID :" + SSID);
  Serial.println("Password :" + password);
  String submit = "<h1>SSID :" + SSID + "<br>interval:" + String(interval) + "<br>Mac : " + mqttID + "<br></h1>";
  webServer.send(200, "text/html", submit);
  writeFile(SPIFFS, ssidPath, SSID.c_str());
  writeFile(SPIFFS, passPath, password.c_str());
  writeFile(SPIFFS, serverPath, server.c_str());
  writeFile(SPIFFS, portPath, port.c_str());
  writeFile(SPIFFS, topicPath, topic.c_str());
  writeFile(SPIFFS, idPath, id.c_str());
  writeFile(SPIFFS, usernamePath, mqtt_user.c_str());
  writeFile(SPIFFS, passwordPath, mqtt_password.c_str());
  writeFile(SPIFFS, intervalPath, String(interval).c_str());
  WiFi.begin(SSID, password);
  delay(2000);
  Serial.println("connect to " + SSID);
  Serial.println("Connecting");
  ESP.restart();
}
// Function to Set AP mode
void setupWifiAP() {

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid_AP, password_AP);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  webServer.on("/", handleWelcome);
  webServer.on("/submit", handleSubmit);
  webServer.on("/config", handleConfig);
  webServer.onNotFound(handlePortal);
  webServer.begin();
  Serial.println("WiFi Manger Ready");
}

void resetWiFi() {
  SPIFFS.remove(ssidPath);
  SPIFFS.remove(passPath);
}
void checkResetESP() {
  if (buttonPressed) {
    // Check if the button has been pressed long enough for a long press
    if (millis() - buttonPressStartTime > LONG_PRESS_TIME && !longPressDetected) {
      longPressDetected = true;
      Serial.println("Resetting ESP32...");
      delay(100);  // Small delay to ensure serial message is sent
      resetWiFi();
      ESP.restart();
    }
  } else {
    // Reset the long press detection when the button is released
    if (longPressDetected) {
      longPressDetected = false;
      Serial.println("Button released.");
    }
  }
}
// Function to connect to Wi-Fi
void setupWifi() {

  SSID = readFile(SPIFFS, ssidPath, SSID);
  password = readFile(SPIFFS, passPath, password);
  server = readFile(SPIFFS, serverPath, server);
  port = readFile(SPIFFS, portPath, port);
  topic = readFile(SPIFFS, topicPath, topic);
  id = readFile(SPIFFS, idPath, id);
  mqtt_user = readFile(SPIFFS, usernamePath, mqtt_user);
  mqtt_password = readFile(SPIFFS, passwordPath, mqtt_password);
  interval = readFile(SPIFFS, intervalPath, String(interval)).toInt();

  if (SSID == "") {
    setupWifiAP();
    while (SSID == "") {
      dnsServer.processNextRequest();
      webServer.handleClient();
    }
  }
  if (SSID != "") {
    if (password != "") {
      WiFi.begin(SSID, password);
    } else {
      WiFi.begin(SSID);
    }
    delay(1500);
    int wifiState = WiFi.status();
    while (wifiState != WL_CONNECTED) {
      wifiState = WiFi.status();
      Serial.println("wifi not connected.");
      delay(1500);
      if (wifiState == 3) {
        WiFi.mode(WIFI_STA);
        break;
      }
      if (wifiState != 1 && wifiState != 6) {
        WiFi.reconnect();
        delay(800);
      }
      // if still can't connect to wifi, reset wifi and restart ESP by press button for 2s
      checkResetESP();
    }
  }
  webServer.close();
  WiFi.mode(WIFI_STA);
  WiFi.reconnect();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void setupMqtt() {
  // espClient.setInsecure();
  // espClient.stop();
  client.setServer(server.c_str(), port.toInt());
  client.setKeepAlive(60);
  // client.setCallback(callback);
  strcpy(mqttTopic, topic.c_str());
  // Attempt to connect
  Serial.println("Attempting MQTT connection...");
  if (client.connect(mqttID, mqtt_user.c_str(), mqtt_password.c_str())) {
    Serial.println("MQTT connected");
  } else {
    Serial.print("MQTT connection failed, rc=");
    Serial.print(client.state());
    Serial.println(" retrying in 5 seconds");
    delay(3000);  // Wait before retrying
  }
}

// Function to sync RTC with NTP time
void syncTimeWithNTP() {
  Wire.begin(33, 32);
  configTime(7 * 3600, 0, "pool.ntp.org");
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }

  // Convert struct tm to time_t
  time_t now = mktime(&timeinfo);

  // Set the RTC time using time_t
  if (RTC.set(now)) {
    Serial.println("RTC time set successfully.");
  } else {
    // Serial.println("Failed to set RTC time.");
  }
}

// Function to get current time from RTC
String getDateTime() {
  tmElements_t tm;
  RTC.read(tm);  // Read the time from the RTC
  char datetime[20];
  sprintf(datetime, "%04d-%02d-%02d %02d:%02d:%02d", tm.Year + 1970, tm.Month, tm.Day, tm.Hour + 7, tm.Minute, tm.Second);
  return String(datetime);
}

// Function to generate random sensor data with names
String getSensorData() {

  uint8_t buf[sizeof(float) * 6];
  Serial1.readBytes(buf, sizeof(float) * 6);

  float pH, temp, waterFlow, ss, cod, toc;
  memcpy(&pH, buf, sizeof(float));
  memcpy(&temp, buf + sizeof(float), sizeof(float));
  memcpy(&waterFlow, buf + sizeof(float) * 2, sizeof(float));
  memcpy(&ss, buf + sizeof(float) * 3, sizeof(float));
  memcpy(&cod, buf + sizeof(float) * 4, sizeof(float));
  memcpy(&toc, buf + sizeof(float) * 5, sizeof(float));

  // String data = "pH:" + String(pH) + "pH, " + "WaterFlow:" + String(waterFlow) + "m3/h, " + "SS:" + String(ss) + "mg/L, " + "COD:" + String(cod) + "mg/L, " + "TOC:" + String(toc) + "mg/L, " + "Temp:" + String(temp) + "°C";
  String data = "{\"payload\":{\"id\": \"" + String(mqttID) + "\",\"name\": \"" + id + "\",\"fields\": [ { \"ph\":";
  if (!isnan(pH)) {
    data += String(pH);
  } else {
    data += "0.00";
  }
  data += ",\"temp\":";
  if (!isnan(temp)) {
    data += String(temp);
  } else {
    data += "0.00";
  }
  data += ",\"cod\": ";
  if (!isnan(cod)) {
    data += String(cod);
  } else {
    data += "0.00";
  }
  // data +=",\"TOC\": ";
  // if(TOCGet != -1){
  //   data += String(TOCGet);
  // }
  // else{
  //   data += "null";
  // }
  data += ",\"ss\": ";
  if (!isnan(ss)) {
    data += String(ss);
  } else {
    data += "0.00";
  }
  data += ",\"waterflow\": ";
  if (waterFlow != 0) {
    data += String(waterFlow);
  } else {
    data += "0.00";
  }
  data += ",\"timestamp\":" + getDateTime() + "}] }}";

  return data;
}

// Function to log data with timestamp to SD card
void logDataToSD() {
  xSemaphoreTake(sdMutex, portMAX_DELAY);
  if (Serial1.available() >= sizeof(float) * 6) {
    File file = SD.open("/data.txt", FILE_APPEND);
    if (file) {
      String data = getSensorData();
      file.println(data);
      file.close();
      Serial.println("Logged: " + data);
    } else {
      // Serial.println("Failed to open file for writing.");
    }
  }
  xSemaphoreGive(sdMutex);
}

// Task 1: Data logging every 10ms
void TaskLogData(void* pvParameters) {
  while (1) {
    checkResetESP();
    repeatedFWUpdateCall();
    if (millis() - lastLogTime >= logInterval) {
      lastLogTime = millis();
      logDataToSD();
    }
    vTaskDelay(10);  // Yield control for minimal time
  }
}

// Function to send data from SD card to MQTT and delete after sending
void sendDataToMQTT() {
  xSemaphoreTake(sdMutex, portMAX_DELAY);

  // Open the data file for reading
  File file = SD.open("/data.txt", FILE_READ);
  if (file) {
    // Open a temporary file to store unsent data
    File tempFile = SD.open("/temp.txt", FILE_WRITE);
    if (!tempFile) {
      Serial.println("Failed to open temp file.");
      file.close();
      xSemaphoreGive(sdMutex);
      return;
    }

    bool allSent = true;

    // Read and send each line of the data
    while (file.available()) {
      String line = file.readStringUntil('\n');

      // Try sending each line via MQTT
      if (client.publish(mqttTopic, line.c_str())) {
        Serial.println("Sent: " + line);  // Debugging info
      } else {
        // If sending fails, write the unsent line to the temporary file
        tempFile.println(line);
        allSent = false;
        // Serial.println("Failed to send: " + line);  // Debugging info
      }
    }

    file.close();
    tempFile.close();

    // If all data was sent, delete the original file
    if (allSent) {
      SD.remove("/data.txt");
      Serial.println("All data sent, original file deleted.");
    } else {
      // If there is unsent data, rename the temporary file to be the new data file
      SD.remove("/data.txt");               // Remove original file
      SD.rename("/temp.txt", "/data.txt");  // Rename temp file to original file
                                            // Serial.println("Unsent data saved to new data.txt.");
    }
  } else {
    // Serial.println("No data to send, file not found.");
  }

  xSemaphoreGive(sdMutex);
}

// Task 2: Send data to MQTT if connected
void TaskSendData(void* pvParameters) {
  for (;;) {

    if (WiFi.status() == WL_CONNECTED && client.connected()) {
      sendDataToMQTT();

    } else {
      vTaskDelay(500);
      Serial.println("MQTT disconnected, retrying...");
      if (!client.connected()) {
        for (int retry = 0; retry < 5 && !client.connected(); retry++) {  // Limit retries
          if (client.connect(mqttID, mqtt_user.c_str(), mqtt_password.c_str())) {
            Serial.println("MQTT reconnected.");
          } else {
            Serial.println("MQTT connection failed, retrying...");
            vTaskDelay(2000 / portTICK_PERIOD_MS);  // Wait before retrying
          }
        }
      }
      client.loop();
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);  // Check every 10ms
  }
}

// Task to update time every 3 hours
void TaskUpdateTime(void* pvParameters) {
  const TickType_t xDelay = 3 * 60 * 60 * 1000 / portTICK_PERIOD_MS;  // 3 hours in ticks

  // Run indefinitely
  for (;;) {
    syncTimeWithNTP();
    // Delay for 3 hours before next update
    vTaskDelay(xDelay);
  }
}
void IRAM_ATTR handleButtonPress() {
  // Interrupt is triggered when the button is pressed (active low)
  if (digitalRead(resetWiFiPin) == LOW) {
    buttonPressed = true;
    buttonPressStartTime = millis();  // Record the time when the button was pressed
  } else {
    buttonPressed = false;
  }
}
void setup() {
  delay(500);
  Serial.begin(115200);
  Serial1.setRxBufferSize(4096);
  Serial1.begin(9600, SERIAL_8N1, RXp2, TXp2);

  // Setup LED pin
  pinMode(led1, OUTPUT);
  pinMode(led2, OUTPUT);
  digitalWrite(led1, LOW);
  digitalWrite(led2, LOW);

  // Setup Reset button pin
  delay(100);
  pinMode(resetWiFiPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(resetWiFiPin), handleButtonPress, CHANGE);  // Trigger on both press and release

  // Initialize SPIFFS for data saving
  delay(100);
  initSPIFFS();

  // Initialize SD card
  delay(100);
  SPI.begin(sck, miso, mosi, cs);
  if (!SD.begin(cs)) {
    Serial.println("SD Card mount failed.");
    return;
  }
  delay(100);
  WiFi.mode(WIFI_STA);
  delay(50);
  strcpy(mqttID, WiFi.macAddress().c_str());
  Serial.println(mqttID);
  setupWifi();
  setupMqtt();
  delay(100);
  syncTimeWithNTP();

  // Create a mutex to protect SD card access
  sdMutex = xSemaphoreCreateMutex();

  // Create tasks
  xTaskCreatePinnedToCore(TaskLogData, "TaskLogData", 4096, NULL, 1, &Task1, 1);
  xTaskCreatePinnedToCore(TaskSendData, "TaskSendData", 4096, NULL, 1, &Task2, 0);
  xTaskCreatePinnedToCore(TaskUpdateTime, "TaskUpdateTime", 4096, NULL, 1, &task3, 0);
}

void loop() {
  // Nothing to do here, tasks are running in the background
}
