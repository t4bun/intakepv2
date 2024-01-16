#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include <ESP32Time.h>
#include "time.h"
#include "DFRobot_INA219.h"
DFRobot_INA219_IIC ina219(&Wire, INA219_I2C_ADDRESS4);

ESP32Time rtc(25200);  // offset in seconds GMT+1
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 0;

#define SD_SCK 14
#define SD_MISO 33
#define SD_MOSI 13
#define SD_CS 15
#define relay 5
const char* ssid = "AMOS";
const char* password =  "Huricane88";

SPIClass spi(HSPI);
AsyncWebServer server(80);
float ina219INReading_mA = 1000;
float extINMeterReading_mA = 1000;
void setup() {
  Serial.begin(500000);
  pinMode(relay, OUTPUT);
  spi.begin(SD_SCK, SD_MISO, SD_MOSI);  // SCK, MISO, MOSI, SS
  if(!SD.begin(SD_CS, spi)){
    Serial.println("Card Mount Failed");
    return;
  }
  while(ina219.begin() != true) {
        Serial.println("INA219 in begin failed");
        delay(2000);
    }
  ina219.setPGA(ina219.eIna219PGABits_8);
	// ina219.setBADC(ina219.eIna219AdcBits_12, ina219.eIna219AdcSample_8);
	// ina219.setSADC(ina219.eIna219AdcBits_12, ina219.eIna219AdcSample_8);
  ina219.setMode(ina219.eIna219SAndBVolCon);
  ina219.linearCalibrate(ina219INReading_mA, extINMeterReading_mA);
  uint8_t cardType = SD.cardType();

  if(cardType == CARD_NONE){
    Serial.println("No SD card attached");
    return;
  }

  Serial.print("SD Card Type: ");
  if(cardType == CARD_MMC){
    Serial.println("MMC");
  } else if(cardType == CARD_SD){
    Serial.println("SDSC");
  } else if(cardType == CARD_SDHC){
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println(WiFi.localIP());

  /*---------set with NTP---------------*/
 configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
 struct tm timeinfo;
 if (getLocalTime(&timeinfo)){
   rtc.setTimeStruct(timeinfo); 
 }

  server.on("/list", HTTP_GET, [](AsyncWebServerRequest *request){
    String output = listDir(SD, "/", 0);
    request->send(200, "text/plain", output);
  });

  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("path")) {
      String filename = request->getParam("path")->value();
      File file = SD.open(filename.c_str());
      if(!file) {
        request->send(404, "text/plain", "File not found");
      } else {
        request->send(SD, filename.c_str(), "text/plain", true);
      }
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });
  
  server.begin();
  // Start the task on core 1
  xTaskCreatePinnedToCore(
    sensorTask,   /* Function to implement the task */
    "sensorTask", /* Name of the task */
    10000,        /* Stack size in words */
    NULL,         /* Task input parameter */
    1,            /* Priority of the task */
    NULL,         /* Task handle. */
    1);           /* Core where the task should run */
}

void loop() {
  // The loop function is intentionally left empty.
  // All the work is done in the sensorTask function, running on core 1.
}

char joker[50];
char datejok[50];
char jamaja[50];
unsigned long previousMillis = 0;
const unsigned long interval = 3600000; // 1 hour in milliseconds
const int maxLoopsPerHour = 5000;
int loopCount = 0;
void sensorTask(void * pvParameters) {
  while(1) {
    unsigned long currentMillis = millis();
    struct tm timeinfo = rtc.getTimeStruct();
    strftime(joker, sizeof(joker), "%A, %d %B %Y", &timeinfo);
    strftime(datejok, sizeof(datejok), "%H:%M:%S", &timeinfo);
    float shuntvoltage = 0;
    float busvoltage = 0;
    float current_mA = 0;
    float loadvoltage = 0;
    float power_mW = 0;
    int hour = atoi(jamaja);
    static int previousHour = -1;
    if (hour != previousHour) {
      previousHour = hour;
      loopCount = 0;
    }
    if (loopCount < maxLoopsPerHour) {
      shuntvoltage = ina219.getShuntVoltage_mV();
      busvoltage = ina219.getBusVoltage_V();
      current_mA = ina219.getCurrent_mA();
      power_mW = ina219.getPower_mW();
      loadvoltage = busvoltage + (shuntvoltage / 1000);
      String dataString = String(datejok) + "," + String(busvoltage) + "," + String(shuntvoltage) + "," + String(loadvoltage)+ "," + String(current_mA)+ "," + String(power_mW);
      
      // Print the sensor data to the Serial monitor
      Serial.println(dataString);

      File dataFile = SD.open("/"+String(joker)+".csv", FILE_APPEND);

      if(dataFile) {
        dataFile.println(dataString);
        dataFile.close();
      } else {
        Serial.println("Failed to open data file for writing");
      }

      delay(1000);  // Delay for 5 seconds before next reading
    }else{
      digitalWrite(relay, HIGH);
      Serial.print("Loop Count: "); Serial.print(loopCount); Serial.print(" ");
      unsigned long timeToWait = interval - (currentMillis - previousMillis);
      Serial.print("Waiting for ");
      Serial.print(timeToWait / 1000);
      Serial.println(" seconds until the next hour.");
      delay(timeToWait);
    }
  }
}


String listDir(fs::FS &fs, const char * dirname, uint8_t levels){
  String output = "";
  File root = fs.open(dirname);
  if(!root){
    output = "Failed to open directory";
    return output;
  }
  if(!root.isDirectory()){
    output = "Not a directory";
    return output;
  }

  File file = root.openNextFile();
  while(file){
    if(file.isDirectory()){
      output += "  DIR : ";
      output += String(file.name());
      output += "\n";
      if(levels){
        listDir(fs, file.name(), levels -1);
      }
    } else {
      output += "  FILE: ";
      output += String(file.name());
      output += "\n";
    }
    file = root.openNextFile();
  }
  return output;
}
