#include "rpcWiFi.h"
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#include <TFT_eSPI.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "config.h"

// Time

WiFiUDP ntpUDP;

// By default 'pool.ntp.org' is used with 60 seconds update interval and
// no offset
NTPClient timeClient(ntpUDP, "0.se.pool.ntp.org");


// Replace with your country code and city
String city = "Boras";
String countryCode = "SE";

unsigned long lastTime = 0;
// Timer set to 10 minutes (600000)
//unsigned long timerDelay = 600000;
unsigned long timerDelay = 60000;

String currentTime;
String *currentTimePtr = &currentTime;

String humidity;
String windSpeed;
String description;
String temperature;
int integerTemp;
int *integerTempPtr;

String serverPath;

  
String payload = "{}"; 

String jsonBuffer;

TFT_eSPI tft;

void setup() {
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);

  Serial.begin(115200);

  timeClient.begin();

  WiFi.begin(SSID, WIFI_PASSWORD);
  Serial.println("Connecting");
  tft.drawString("Connecting to wifi", 0, 0);
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    tft.drawString("Connecting to wifi", 0, 0);
  }
  Serial.println("");
  Serial.print("Connected to WiFi network with IP Address: ");
  tft.drawString("Connected to wifi ", 0, 0);
  Serial.println(WiFi.localIP());
 
  Serial.println("Timer set to 10 seconds (timerDelay variable), it will take 10 seconds before publishing the first reading.");
  tft.fillScreen(TFT_BLACK);
  timeClient.update();
  timeClient.setTimeOffset(3600);
  currentTime = timeClient.getFormattedTime();
  showWeather(currentTimePtr);
  }

void loop() {

  timeClient.update();
  timeClient.setTimeOffset(3600);

  currentTime = timeClient.getFormattedTime();

  tft.setTextSize(3);
  tft.drawString("Time: " + currentTime, 10, 210);
  delay(1000);
  // Send an HTTP GET request
  if ((millis() - lastTime) > timerDelay) {
    showWeather(currentTimePtr);
}
}

String httpGETRequest(const char* serverName) {
  WiFiClient client;
  HTTPClient http;
    
  // Your Domain name with URL path or IP address with path
  http.begin(client, serverName);
  
  // Send HTTP POST request
  int httpResponseCode = http.GET();
  
  payload = "{}"; 
  
  if (httpResponseCode>0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    payload = http.getString();
  }
  else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  // Free resources
  http.end();

  return payload;
}

void showWeather(String* currentTime){
  // Check WiFi connection status
    if(WiFi.status()== WL_CONNECTED){
      serverPath = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "," + countryCode + "&APPID=" + openWeatherMapApiKey + "&units=metric";
      
      jsonBuffer = httpGETRequest(serverPath.c_str());
      Serial.println(jsonBuffer);
      JSONVar myObject = JSON.parse(jsonBuffer);
      
  
      // JSON.typeof(jsonVar) can be used to get the type of the var
      if (JSON.typeof(myObject) == "undefined") {
        Serial.println("Parsing input failed!");
        return;
      }
    
      temperature =  JSON.stringify(myObject["main"]["temp"]);
      integerTemp = temperature.toInt();
      temperature = String(integerTemp);
      humidity =  JSON.stringify(myObject["main"]["humidity"]);
      windSpeed = JSON.stringify(myObject["wind"]["speed"]);
      description = JSON.stringify(myObject["weather"][0]["main"]);
    
      tft.fillScreen(TFT_BLACK);

      // Weather Updates on screen
      tft.setTextSize(4);
      tft.drawString("Boras", 100, 5);
      tft.setTextSize(3);
      tft.drawString("Temperature: " + temperature + "c", 5, 50);
      tft.drawString("Humidity: " + humidity + "%", 5, 90);
      tft.drawString("Wind Speed: " + windSpeed, 5, 130);
      tft.drawString("Status: " + description, 5, 170);
      tft.setTextSize(3);

    }
    else {
      Serial.println("WiFi Disconnected");
      tft.drawString("Wifi disconnected", 0, 0);
    }
    lastTime = millis();
  }

