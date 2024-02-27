#if !( defined(ARDUINO_SAMD_ZERO) || defined(ARDUINO_SAMD_MKR1000) || defined(ARDUINO_SAMD_MKRWIFI1010) \
      || defined(ARDUINO_SAMD_NANO_33_IOT) || defined(ARDUINO_SAMD_MKRFox1200) || defined(ARDUINO_SAMD_MKRWAN1300) || defined(ARDUINO_SAMD_MKRWAN1310) \
      || defined(ARDUINO_SAMD_MKRGSM1400) || defined(ARDUINO_SAMD_MKRNB1500) || defined(ARDUINO_SAMD_MKRVIDOR4000) \
      || defined(ARDUINO_SAMD_CIRCUITPLAYGROUND_EXPRESS) || defined(__SAMD51__) || defined(__SAMD51J20A__) \
      || defined(__SAMD51J19A__) || defined(__SAMD51G19A__) || defined(__SAMD51P19A__)  \
      || defined(__SAMD21E15A__) || defined(__SAMD21E16A__) || defined(__SAMD21E17A__) || defined(__SAMD21E18A__) \
      || defined(__SAMD21G15A__) || defined(__SAMD21G16A__) || defined(__SAMD21G17A__) || defined(__SAMD21G18A__) \
      || defined(__SAMD21J15A__) || defined(__SAMD21J16A__) || defined(__SAMD21J17A__) || defined(__SAMD21J18A__) )
  #error This code is designed to run on SAMD21/SAMD51 platform! Please check your Tools->Board setting.
#endif

/////////////////////////////////////////////////////////////////

// These define's must be placed at the beginning before #include "SAMDTimerInterrupt.h"
// _TIMERINTERRUPT_LOGLEVEL_ from 0 to 4
// Don't define _TIMERINTERRUPT_LOGLEVEL_ > 0. Only for special ISR debugging only. Can hang the system.
// Don't define TIMER_INTERRUPT_DEBUG > 2. Only for special ISR debugging only. Can hang the system.
#define TIMER_INTERRUPT_DEBUG         0
#define _TIMERINTERRUPT_LOGLEVEL_     0

// Select only one to be true for SAMD21. Must must be placed at the beginning before #include "SAMDTimerInterrupt.h"
#define USING_TIMER_TC3         true      // Only TC3 can be used for SAMD51
#define USING_TIMER_TC4         false     // Not to use with Servo library
#define USING_TIMER_TC5         false
#define USING_TIMER_TCC         false
#define USING_TIMER_TCC1        false
#define USING_TIMER_TCC2        false     // Don't use this, can crash on some boards

// Uncomment To test if conflict with Servo library
//#include "Servo.h"

/////////////////////////////////////////////////////////////////

#define BUZZER_PIN WIO_BUZZER 
   
#include "SAMDTimerInterrupt.h"
#include "SAMD_ISR_Timer.h"
#include "rpcWiFi.h"
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#include <TFT_eSPI.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "config.h"

// TC3, TC4, TC5 max permissible HW_TIMER_INTERVAL_MS is 1398.101 ms, larger will overflow, therefore not permitted
// Use TCC, TCC1, TCC2 for longer HW_TIMER_INTERVAL_MS
#define HW_TIMER_INTERVAL_MS      10

///////////////////////////////////////////////

#if (TIMER_INTERRUPT_USING_SAMD21)

  #if USING_TIMER_TC3
    #define SELECTED_TIMER      TIMER_TC3
  #elif USING_TIMER_TC4
    #define SELECTED_TIMER      TIMER_TC4
  #elif USING_TIMER_TC5
    #define SELECTED_TIMER      TIMER_TC5
  #elif USING_TIMER_TCC
    #define SELECTED_TIMER      TIMER_TCC
  #elif USING_TIMER_TCC1
    #define SELECTED_TIMER      TIMER_TCC1
  #elif USING_TIMER_TCC2
    #define SELECTED_TIMER      TIMER_TCC
  #else
    #error You have to select 1 Timer  
  #endif

#else

  #if !(USING_TIMER_TC3)
    #error You must select TC3 for SAMD51
  #endif
  
  #define SELECTED_TIMER      TIMER_TC3

#endif  

// Init selected SAMD timer
SAMDTimer ITimer(SELECTED_TIMER);

////////////////////////////////////////////////

// Init SAMD_ISR_Timer
// Each SAMD_ISR_Timer can service 16 different ISR-based timers
SAMD_ISR_Timer ISR_Timer;

#define TIMER_INTERVAL_1S             1000L
#define TIMER_INTERVAL_2S             2000L
#define TIMER_INTERVAL_60S            60000L

// 
WiFiUDP ntpUDP;

NTPClient timeClient(ntpUDP, "0.se.pool.ntp.org");

// Replace with your country code and city
String city = "Boras";
String countryCode = "SE";

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


// Use volatile inside ISRs
volatile boolean secondPassed = false;
volatile boolean minutePassed = false;

void TimerHandler(void)
{
  ISR_Timer.run();
}

// In SAMD, avoid doing something fancy in ISR, for example complex Serial.print with String() argument
void secondCounter()
{
  secondPassed = true;
}

//void doingSomething2()
//{}

void minuteCounter()
{
  minutePassed = true;
}

void setup()
{
  Serial.begin(115200);
  
  delay(100);

  Serial.print(F("\nStarting TimerInterruptLEDDemo on ")); Serial.println(BOARD_NAME);
  Serial.println(SAMD_TIMER_INTERRUPT_VERSION);
  Serial.print(F("CPU Frequency = ")); Serial.print(F_CPU / 1000000); Serial.println(F(" MHz"));

  // Instantiate HardwareTimer object. Thanks to 'new' instanciation, HardwareTimer is not destructed when setup() function is finished.
  //HardwareTimer *MyTim = new HardwareTimer(Instance);

  // configure pin in output mode
  pinMode(PIN_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Interval in millisecs
  if (ITimer.attachInterruptInterval_MS(HW_TIMER_INTERVAL_MS, TimerHandler))
  {
    Serial.print(F("Starting ITimer OK, millis() = ")); Serial.println(millis());
  }
  else
    Serial.println(F("Can't set ITimer. Select another freq. or timer"));

  // Just to demonstrate, don't use too many ISR Timers if not absolutely necessary
  // You can use up to 16 timer for each ISR_Timer
  ISR_Timer.setInterval(TIMER_INTERVAL_1S,  secondCounter);
  //ISR_Timer.setInterval(TIMER_INTERVAL_2S,  doingSomething2);
  ISR_Timer.setInterval(TIMER_INTERVAL_60S,  minuteCounter);

  setupWifiConnection();
  setupStarterScreen();
}

void loop()
{
  if(secondPassed){
    timeClient.update();
    timeClient.setTimeOffset(3600);
    currentTime = timeClient.getFormattedTime();
    tft.setTextSize(3);
    tft.drawString("Time: " + currentTime, 10, 210);
  }
  if(minutePassed){
    showWeather(currentTimePtr);
  }

}

void updateTimeScreen(){
    timeClient.update();
    timeClient.setTimeOffset(3600);
    currentTime = timeClient.getFormattedTime();
    tft.setTextSize(3);
    tft.drawString("Time: " + currentTime, 10, 210);
    secondPassed = false;
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

void setupWifiConnection(){
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  WiFi.begin(SSID, WIFI_PASSWORD);
  Serial.println("Connecting");
  tft.drawString("Connecting to wifi", 0, 0);
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    tft.drawString("Connecting to wifi", 0, 0);
  }
  Serial.print("Connected to WiFi network with IP Address: ");
  tft.drawString("Connected to wifi ", 0, 0);
  Serial.println(WiFi.localIP());
}

void setupStarterScreen(){
  timeClient.begin();
  tft.fillScreen(TFT_BLACK);
  timeClient.update();
  timeClient.setTimeOffset(3600);
  currentTime = timeClient.getFormattedTime();
  showWeather(currentTimePtr);
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
      tft.drawString("Wifi disconnected, trying to connect..", 0, 0);
      setupWifiConnection();
    }
    minutePassed = false;
  }

