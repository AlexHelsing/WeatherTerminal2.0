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
#define SCALE_FACTOR 6
   
#include "SAMDTimerInterrupt.h"
#include "SAMD_ISR_Timer.h"
#include "rpcWiFi.h"
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#include <TFT_eSPI.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "config.h"
#include "qrcode.h"
#include <stdint.h>

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

// Wio Buttons

boolean leftButtonPressed = false;

// Pomodoro

#define POMODORO_WORK_DURATION 1500 // 25 minutes
#define POMODORO_BREAK_DURATION 300 // 5 minutes
#define POMODORO_CYCLES 4

volatile boolean pomodoroActive = false;
volatile boolean pomodoroWork = true;
volatile int pomodoroCycle = 0;
volatile int pomodoroTimeLeft = POMODORO_WORK_DURATION;
volatile boolean pomodoroSecondPassed = false;

// TFT Display

TFT_eSPI tft;

// QR Code

// Create the QR code
QRCode qrcode;
// Format: "WIFI:T:WPA;S:SSID;P:PASSWORD;;"
const char* text = QRCODEWIFI; // Replace this with your actual data

// Init selected SAMD timer
SAMDTimer ITimer(SELECTED_TIMER);

////////////////////////////////////////////////

// Init SAMD_ISR_Timer
// Each SAMD_ISR_Timer can service 16 different ISR-based timers
SAMD_ISR_Timer ISR_Timer;

#define TIMER_INTERVAL_1S 1000L // 1000 milliseconds = 1 second
#define TIMER_INTERVAL_2S 5000L
#define TIMER_INTERVAL_600S 600000L

// 
WiFiUDP ntpUDP;

// Time client used to get the correct swedish time
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


// Use volatile inside ISRs
volatile boolean secondPassed = false;
volatile boolean tenMinutesPassed = false;
volatile boolean fiveSecondPassed = false;

void TimerHandler(void) {
  ISR_Timer.run();
}

// In SAMD, avoid doing something fancy in ISR, for example complex Serial.print with String() argument
void secondCounter() {
  secondPassed = true;
  pomodoroSecondPassed = true; // Update the Pomodoro flag
}

//void doingSomething2(){
  //fiveSecondPassed = true;
//}

void tenMinuteCounter()
{
  tenMinutesPassed = true;
}

void setup()
{
  Serial.begin(115200);

  Serial.println(SAMD_TIMER_INTERRUPT_VERSION);
  Serial.print(F("CPU Frequency = ")); Serial.print(F_CPU / 1000000); Serial.println(F(" MHz"));

  // Instantiate HardwareTimer object. Thanks to 'new' instanciation, HardwareTimer is not destructed when setup() function is finished.
  //HardwareTimer *MyTim = new HardwareTimer(Instance);

  // configure pin in output mode
  pinMode(PIN_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_1, INPUT_PULLUP);
  pinMode(WIO_KEY_B, INPUT_PULLUP);
  pinMode(WIO_BUZZER, OUTPUT);

  // Interval in millisecs
  if (ITimer.attachInterruptInterval_MS(HW_TIMER_INTERVAL_MS, TimerHandler))
  {
    Serial.print(F("Starting ITimer OK, millis() = ")); Serial.println(millis());
  }
  else
    Serial.println(F("Can't set ITimer. Select another freq. or timer"));

  // Just to demonstrate, don't use too many ISR Timers if not absolutely necessary
  // You can use up to 16 timer for each ISR_Timer
  ISR_Timer.setInterval(TIMER_INTERVAL_1S, secondCounter);
  //ISR_Timer.setInterval(TIMER_INTERVAL_2S,  doingSomething2);
  ISR_Timer.setInterval(TIMER_INTERVAL_600S,  tenMinuteCounter);

  setupWifiConnection();
  setupStarterScreen();
}

void loop() {
  if (digitalRead(WIO_KEY_B) == LOW) {
    startPomodoro();
  }

  if (pomodoroActive) {
    updatePomodoro();
  }

  if(digitalRead(WIO_KEY_A) == LOW){
    displayQrCode();
  }

  if (secondPassed) {
    timeClient.update();
    timeClient.setTimeOffset(7200);
    currentTime = timeClient.getFormattedTime();
    if(!pomodoroActive){
        tft.setTextSize(3);
        tft.drawString("Time: " + currentTime, 10, 210);
        secondPassed = false; // Reset the flag after updating 
    }
    else{
      secondPassed = false; // Reset the flag after updating 
    }
  }

  if (tenMinutesPassed) {
    tenMinutesPassed = false; // Reset the flag after updating
    if(!pomodoroActive){
      showWeather(currentTimePtr);
    }
  }
}

void updateTimeScreen(){
    timeClient.update();
    timeClient.setTimeOffset(7200);
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
  
  if(httpResponseCode != 200){
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    Serial.println("Data fetch failed.. trying again in one minute");
    http.end();
    return payload;
  }
  else{
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    payload = http.getString();
    http.end();
    return payload;
  }
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
    WiFi.begin(SSID, WIFI_PASSWORD);
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
      if(jsonBuffer == "{}"){
        tenMinutesPassed = false;
        return;
      }
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

      // Weather Updates on screen
      tft.setTextSize(4);
      tft.drawString("Boras", 100, 5);
      tft.setTextSize(3);
      tft.drawString("Temperature: " + temperature + "c", 5, 50);
      tft.drawString("Humidity: " + humidity + "%", 5, 90);
      tft.drawString("Wind Speed: " + windSpeed, 5, 130);
      tft.drawString("Status: " + description, 5, 170);
      tft.setTextSize(3);

      tenMinutesPassed = false;
    }
    else {
      Serial.println("WiFi Disconnected");
      tft.drawString("Wifi disconnected, trying to connect..", 0, 0);
      setupWifiConnection();
    }
  }

  void displayQrCode(){
    pomodoroActive = false;
    tft.setRotation(3); // Adjust rotation if needed
    tft.fillScreen(TFT_BLACK); // Fill the screen with black color
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("Scan the QR code for Wifi", 10, 210);

    uint8_t qrcodeData[qrcode_getBufferSize(3)];
    qrcode_initText(&qrcode, qrcodeData, 3, 0, text);

    for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
        if (qrcode_getModule(&qrcode, x, y)) {
            // Draw a block of pixels based on the scaling factor
            for (uint8_t sy = 0; sy < SCALE_FACTOR; sy++) {
                for (uint8_t sx = 0; sx < SCALE_FACTOR; sx++) {
                    tft.drawPixel(x * SCALE_FACTOR + sx + 10, y * SCALE_FACTOR + sy + 10, TFT_WHITE);
                }
            }
        }
    }
}
  // Adjust for how long the QR code shall be shown
  delay(7000);
  // Reset screen
  tft.fillScreen(TFT_BLACK);
  // Just in case, reset the tenMinutesPassed. Otherwise it might double fetch while it goes into the main loop.
  tenMinutesPassed = false;
  // Update the screen with weather again.
  showWeather(currentTimePtr);
}

void startPomodoro() {
  pomodoroActive = true;
  pomodoroWork = true;
  pomodoroCycle = 0;
  pomodoroTimeLeft = POMODORO_WORK_DURATION;
  tft.fillScreen(TFT_RED);
  tft.setTextSize(4);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.drawString("Pomodoro Work", 10, 20);
  analogWrite(WIO_BUZZER, 128);
  delay(250);
  analogWrite(WIO_BUZZER, 0);
  delay(250);
  updatePomodoroDisplay();
}

void updatePomodoroDisplay() {
  int minutes = pomodoroTimeLeft / 60;
  int seconds = pomodoroTimeLeft % 60;
  char timeString[6];
  sprintf(timeString, "%02d:%02d", minutes, seconds);
  //tft.fillRect(10, 50, 200, 50, TFT_BLACK); // Clear previous time
  tft.setTextColor(pomodoroWork ? TFT_WHITE : TFT_BLACK , pomodoroWork ? TFT_RED : TFT_GREEN);
  tft.setTextSize(5);
  tft.drawString(timeString, 90, 100);
}

void updatePomodoro() {
  if (pomodoroSecondPassed) {
    pomodoroTimeLeft--;
    updatePomodoroDisplay();
    if (pomodoroTimeLeft <= 0) {
      if (pomodoroWork) {
        pomodoroCycle++;
        if (pomodoroCycle < POMODORO_CYCLES) {
          pomodoroWork = false;
          pomodoroTimeLeft = POMODORO_BREAK_DURATION;
          tft.fillScreen(TFT_GREEN);
          tft.setTextColor(TFT_BLACK, TFT_GREEN);
          tft.setTextSize(3);
          tft.drawString("Pomodoro Break", 30, 20);
          analogWrite(WIO_BUZZER, 128);
          delay(1000);
          analogWrite(WIO_BUZZER, 0);
          delay(1000);
        } else {
          pomodoroActive = false;
          tft.fillScreen(TFT_GREEN);
          tft.setTextColor(TFT_BLACK, TFT_GREEN);
          tft.setTextSize(3);
          tft.drawString("Pomodoro Done!", 5, 20);
          analogWrite(WIO_BUZZER, 128);
          delay(1000);
          analogWrite(WIO_BUZZER, 0);
          delay(1000);
        }
      } else {
        pomodoroWork = true;
        pomodoroTimeLeft = POMODORO_WORK_DURATION;
        tft.fillScreen(TFT_RED);
        tft.setTextColor(TFT_WHITE, TFT_RED);
        tft.setTextSize(4);
        tft.drawString("Pomodoro Work", 20, 20);
        analogWrite(WIO_BUZZER, 128);
        delay(1000);
        analogWrite(WIO_BUZZER, 0);
        delay(1000);
      }
    }
    pomodoroSecondPassed = false;
  }
}


