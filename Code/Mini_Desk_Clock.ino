#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <WiFi.h>
#include "time.h"
#include <math.h>

// --- WI-FI & NTP CONFIGURATION ---
// Don't Forget to add your wifi's SSID and Password in the below columns before uploading this code in ESP32
const char* ssid     = "Your WIFI SSID";
const char* password = "YOUR WIFI Password";

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 19800;      // IST (UTC +5:30)
const int   daylightOffset_sec = 0;

// --- OLED CONFIGURATION ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- SENSOR PINS ---
#define DHTPIN 4          // DHT11 data pin
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

#define MQ2_PIN 34        // MQ-2 Analog Out pin
#define IR_PIN 15         // IR Proximity Sensor Digital Out pin (Wave hand to trigger)

// --- RGB LED PINS ---
const int redPin = 25;
const int greenPin = 26;
const int bluePin = 27;

// PWM Settings for ESP32
const int freq = 5000;
const int resolution = 8;

// --- SYSTEM STATES & TIMING ---
enum ClockState { IDLE, WAKEUP_ANIMATION, EYE_ANIMATION, SCREEN_TIME, SCREEN_TEMP, SCREEN_AQI, SCREEN_CALENDAR, SLEEP_ANIMATION };
ClockState currentState = IDLE;

unsigned long stateTimer = 0;
unsigned long timeSyncTimer = 0;

// Time & Date Variables (Updated automatically via NTP)
int hours = 0;       
int minutes = 0;
int seconds = 0;
int day = 1;
int month = 1;
int year = 2026;
const char* dayOfWeek = "Sunday";

// AQI Simulation Helper Variables
int baseAqi = 48;
unsigned long aqiTimer = 0;

// Autonomous Idle Twitch Variables
unsigned long idleTwitchTimer = 0;
bool isTwitching = false;
unsigned long twitchStart = 0;

void setup() {
  Serial.begin(115200);

  // Initialize Pins
  pinMode(IR_PIN, INPUT);

  // Initialize ESP32 PWM pins
  ledcAttach(redPin, freq, resolution);
  ledcAttach(greenPin, freq, resolution);
  ledcAttach(bluePin, freq, resolution);

  // Initialize Sensors & Display
  dht.begin();
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 20);
  display.println("Connecting Wi-Fi...");
  display.display();

  // Connect to Wi-Fi using your hotspot credentials
  WiFi.begin(ssid, password);
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 12000) {
    delay(500);
  }

  display.clearDisplay();
  display.setCursor(10, 20);
  if (WiFi.status() == WL_CONNECTED) {
    display.println("Wi-Fi Connected!");
    display.display();
    
    // Sync time from NTP server
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    delay(2000); 
  } else {
    display.println("Wi-Fi Failed (Offline)");
    display.display();
  }

  delay(1500);

  timeSyncTimer = millis();
  aqiTimer = millis();
  idleTwitchTimer = millis();
}

void loop() {
  // 1. Live NTP Time Sync (Updates every second from system time)
  if (millis() - timeSyncTimer >= 1000) {
    timeSyncTimer = millis();
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      hours = timeinfo.tm_hour;
      minutes = timeinfo.tm_min;
      seconds = timeinfo.tm_sec;
      day = timeinfo.tm_mday;
      month = timeinfo.tm_mon + 1;
      year = timeinfo.tm_year + 1900;
      
      const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
      dayOfWeek = days[timeinfo.tm_wday];
    }
  }

  // 2. Dynamic AQI Fluctuation (every 2.5 seconds)
  if (millis() - aqiTimer >= 2500) {
    aqiTimer = millis();
    int fluctuation = (rand() % 7) - 3;
    baseAqi += fluctuation;
    if (baseAqi < 25) baseAqi = 25;
    if (baseAqi > 180) baseAqi = 180;
  }

  // 3. Smooth, Relaxing RGB Color Cycling in Idle Mode
  if (currentState == IDLE) {
    float t = millis() * 0.002; 
    int r = (sin(t) + 1.0) * 127;
    int g = (sin(t + 2.094) + 1.0) * 127; 
    int b = (sin(t + 4.188) + 1.0) * 127; 
    
    ledcWrite(redPin, r);
    ledcWrite(greenPin, g);
    ledcWrite(bluePin, b);
  }

  // 4. State Machine Logic
  switch (currentState) {
    
    // --- MODE 1: IDLE AMBIENT LIGHTING & IR HAND-WAVE LISTENING ---
    case IDLE: {
      if (!isTwitching) {
        display.clearDisplay();
        display.display();
      }

      if (!isTwitching && (millis() - idleTwitchTimer > 8000)) {
        idleTwitchTimer = millis();
        isTwitching = true;
        twitchStart = millis();
      }

      if (isTwitching) {
        display.clearDisplay();
        drawEyes(1); 
        display.display();
        if (millis() - twitchStart > 400) {
          isTwitching = false;
        }
      }

      // Check for IR Sensor trigger (Hand wave detected)
      // Note: Most IR obstacle modules output LOW when hand is present. 
      // If your module outputs HIGH on detection, change LOW to HIGH.
      if (digitalRead(IR_PIN) == LOW) {
        delay(50); // Debounce
        if (digitalRead(IR_PIN) == LOW) {
          isTwitching = false;
          currentState = WAKEUP_ANIMATION;
          stateTimer = millis();
        }
      }
      break;
    }

    // --- MODE 2: WAKE-UP SEQUENCE ---
    case WAKEUP_ANIMATION: {
      ledcWrite(redPin, 255);
      ledcWrite(greenPin, 255);
      ledcWrite(bluePin, 255);

      display.clearDisplay();
      unsigned long wakeElapsed = millis() - stateTimer;

      if (wakeElapsed < 700) {
        drawSleepingEyes();
        drawMouth(2);
        int zDrift = (wakeElapsed / 150) % 4;
        display.setCursor(55, 10 - zDrift);
        display.print("Z z");
      } else if (wakeElapsed < 1200) {
        drawWideEyes();
        drawMouth(1);
        display.setCursor(52, 2);
        display.print("! ! !");
      } else if (wakeElapsed < 1700) {
        drawHalfClosedEyes();
        drawMouth(0);
        display.setCursor(50, 2);
        display.print("Oh!");
      } else {
        drawEyes(0);
        drawMouth(0);
      }

      display.display();

      if (wakeElapsed >= 2200) {
        currentState = EYE_ANIMATION;
        stateTimer = millis();
      }
      break;
    }

    // --- MODE 3: PIXEL ART EYES ---
    case EYE_ANIMATION: {
      display.clearDisplay();
      drawSparkles(millis());

      unsigned long animElapsed = millis() - stateTimer;
      
      if (animElapsed > 1300 && animElapsed < 1550) {
        drawHalfClosedEyes();
        drawMouth(0);
      } else if (animElapsed < 900) {
        drawEyes(0);  
        drawMouth(0); 
      } else if (animElapsed < 1800) {
        drawEyes(1);  
        drawMouth(0);
      } else if (animElapsed < 2700) {
        drawEyes(-1); 
        drawMouth(0);
      } else {
        drawEyes(0);  
        drawMouth(0);
      }
      
      display.display();

      if (animElapsed >= 3000) {
        currentState = SCREEN_TIME;
        stateTimer = millis();
      }
      break;
    }

    // --- MODE 4: SCREEN 1 — TIME ---
    case SCREEN_TIME: {
      display.clearDisplay();
      drawClockIcon(8, 2, millis());

      display.setTextSize(1);
      display.setCursor(26, 2);
      display.print("MINI CLOCK");

      int displayHour = hours % 12;
      if (displayHour == 0) displayHour = 12;
      const char* ampm = (hours >= 12) ? "PM" : "AM";

      display.setTextSize(2);
      display.setCursor(5, 26);
      char timeBuf[12];
      char colon = (seconds % 2 == 0) ? ':' : ' ';
      sprintf(timeBuf, "%02d%c%02d%c%02d", displayHour, colon, minutes, colon, seconds);
      display.print(timeBuf);

      display.setTextSize(1);
      display.setCursor(110, 32);
      display.print(ampm);

      int bounceX = (millis() / 50) % 110;
      display.drawFastHLine(bounceX, 58, 18, SSD1306_WHITE);

      display.display();

      if (millis() - stateTimer >= 3000) {
        currentState = SCREEN_TEMP;
        stateTimer = millis();
      }
      break;
    }

    // --- MODE 5: SCREEN 2 — TEMPERATURE ---
    case SCREEN_TEMP: {
      float tempC = dht.readTemperature();
      if (isnan(tempC)) tempC = 24.5;

      display.clearDisplay();
      drawSunIcon(10, 4, millis());

      display.setTextSize(1);
      display.setCursor(32, 2);
      display.print("TEMPERATURE");

      display.setTextSize(2);
      display.setCursor(18, 28);
      char tempBuf[8];
      sprintf(tempBuf, "%.1f", tempC);
      display.print(tempBuf);

      int curX = display.getCursorX();
      int curY = display.getCursorY();
      display.drawCircle(curX + 3, curY + 4, 2, SSD1306_WHITE); 
      display.setCursor(curX + 9, curY);
      display.print("C");

      if (tempC > 30.0) {
        display.drawPixel(115, 18, SSD1306_WHITE);
        display.drawPixel(115, 19, SSD1306_WHITE);
        display.drawPixel(114, 20, SSD1306_WHITE);
      } else if (tempC < 20.0) {
        display.drawLine(112, 16, 115, 19, SSD1306_WHITE);
        display.drawLine(115, 19, 112, 22, SSD1306_WHITE);
        display.drawLine(112, 22, 115, 25, SSD1306_WHITE);
      }

      display.setTextSize(1);
      display.setCursor(105, 22);
      display.print(tempC > 28.0 ? "^v^" : ":)");

      int waveShift = sin(millis() * 0.01) * 2;
      display.drawPixel(40 + waveShift, 55, SSD1306_WHITE);
      display.drawPixel(50 - waveShift, 55, SSD1306_WHITE);
      display.drawPixel(60 + waveShift, 55, SSD1306_WHITE);
      display.drawPixel(70 - waveShift, 55, SSD1306_WHITE);

      display.display();

      if (millis() - stateTimer >= 3000) {
        currentState = SCREEN_AQI;
        stateTimer = millis();
      }
      break;
    }

    // --- MODE 6: SCREEN 3 — AIR QUALITY ---
    case SCREEN_AQI: {
      display.clearDisplay();
      drawPlantIcon(12, 15, millis());

      int p1 = (millis() / 100) % 50;
      int p2 = (millis() / 150) % 50;
      display.drawPixel(105, 55 - p1, SSD1306_WHITE);
      display.drawPixel(118, 60 - p2, SSD1306_WHITE);

      display.setTextSize(1);
      display.setCursor(35, 2);
      display.print("AIR QUALITY");

      display.setTextSize(2);
      display.setCursor(45, 22);
      display.print("AQI:");
      display.setCursor(45, 42);
      display.print(baseAqi);

      display.setTextSize(1);
      display.setCursor(85, 48);
      if (baseAqi <= 50) {
        display.print("Good");
      } else if (baseAqi <= 100) {
        display.print("Mod");
      } else {
        display.print("Bad!");
      }
      display.display();

      if (millis() - stateTimer >= 3000) {
        currentState = SCREEN_CALENDAR;
        stateTimer = millis();
      }
      break;
    }

    // --- MODE 7: SCREEN 4 — CALENDAR ---
    case SCREEN_CALENDAR: {
      display.clearDisplay();
      drawCalendarIcon(8, 2, millis());

      display.setTextSize(1);
      display.setCursor(28, 2);
      display.print("CALENDAR");

      display.setTextSize(2);
      display.setCursor(5, 22);
      char dateBuf[12];
      sprintf(dateBuf, "%02d/%02d/%d", day, month, year);
      display.println(dateBuf);

      display.setTextSize(1);
      display.setCursor(35, 48);
      display.print(dayOfWeek);

      display.display();

      if (millis() - stateTimer >= 3000) {
        currentState = SLEEP_ANIMATION;
        stateTimer = millis();
      }
      break;
    }

    // --- MODE 8: SLEEP ANIMATION ---
    case SLEEP_ANIMATION: {
      display.clearDisplay();
      unsigned long sleepElapsed = millis() - stateTimer;

      if (sleepElapsed < 800) {
        drawHalfClosedEyes();
        drawMouth(2);
      } else {
        drawSleepingEyes();
        drawMouth(2); 
        int drift = (sleepElapsed / 300) % 8;
        display.setTextSize(1);
        display.setCursor(55, 12 - drift);
        display.print("Z z z");
      }

      display.display();

      if (sleepElapsed >= 2500) {
        currentState = IDLE;
        idleTwitchTimer = millis(); 
      }
      break;
    }
  }
}

// --- GRAPHICS LIBRARY ---

void drawEyes(int direction) {
  display.fillRect(25, 18, 24, 26, SSD1306_WHITE);
  display.fillRect(79, 18, 24, 26, SSD1306_WHITE);

  int pupilOffset = direction * 6;
  display.fillRect(33 + pupilOffset, 24, 8, 12, SSD1306_BLACK);
  display.fillRect(87 + pupilOffset, 24, 8, 12, SSD1306_BLACK);
}

void drawWideEyes() {
  display.drawRect(23, 14, 28, 30, SSD1306_WHITE);
  display.drawRect(77, 14, 28, 30, SSD1306_WHITE);
  display.fillRect(33, 22, 8, 14, SSD1306_WHITE);
  display.fillRect(87, 22, 8, 14, SSD1306_WHITE);
}

void drawHalfClosedEyes() {
  display.fillRect(25, 26, 24, 8, SSD1306_WHITE);
  display.fillRect(79, 26, 24, 8, SSD1306_WHITE);
}

void drawSleepingEyes() {
  display.fillRect(25, 30, 24, 4, SSD1306_WHITE);
  display.fillRect(79, 30, 24, 4, SSD1306_WHITE);
}

void drawMouth(int type) {
  if (type == 0) {
    display.drawPixel(61, 50, SSD1306_WHITE);
    display.drawPixel(62, 51, SSD1306_WHITE);
    display.drawPixel(65, 51, SSD1306_WHITE);
    display.drawPixel(66, 50, SSD1306_WHITE);
    display.drawFastHLine(63, 52, 2, SSD1306_WHITE);
  } else if (type == 1) {
    display.drawCircle(64, 50, 3, SSD1306_WHITE);
  } else if (type == 2) {
    display.drawPixel(61, 50, SSD1306_WHITE);
    display.drawPixel(62, 49, SSD1306_WHITE);
    display.drawPixel(63, 50, SSD1306_WHITE);
    display.drawPixel(64, 51, SSD1306_WHITE);
    display.drawPixel(65, 50, SSD1306_WHITE);
    display.drawPixel(66, 49, SSD1306_WHITE);
  }
}

void drawSparkles(unsigned long t) {
  int s1 = (t / 200) % 4;
  if (s1 == 0) display.drawPixel(15, 12, SSD1306_WHITE);
  if (s1 == 2) display.drawPixel(112, 10, SSD1306_WHITE);
  if (s1 == 1) display.drawPixel(20, 52, SSD1306_WHITE);
  if (s1 == 3) display.drawPixel(108, 50, SSD1306_WHITE);
}

void drawClockIcon(int x, int y, unsigned long t) {
  display.drawCircle(x + 6, y + 6, 6, SSD1306_WHITE);
  float angle = (t * 0.003);
  int hx = (x + 6) + sin(angle) * 4;
  int hy = (y + 6) - cos(angle) * 4;
  display.drawLine(x + 6, y + 6, hx, hy, SSD1306_WHITE);
}

void drawSunIcon(int x, int y, unsigned long t) {
  display.fillCircle(x + 8, y + 8, 4, SSD1306_WHITE);
  int offset = (t / 300) % 2;
  display.drawLine(x + 8, y, x + 8, y + 2 + offset, SSD1306_WHITE);
  display.drawLine(x + 8, y + 14 - offset, x + 8, y + 16, SSD1306_WHITE);
  display.drawLine(x, y + 8, x + 2 + offset, y + 8, SSD1306_WHITE);
  display.drawLine(x + 14 - offset, y + 8, x + 16, y + 8, SSD1306_WHITE);
}

void drawPlantIcon(int x, int y, unsigned long t) {
  display.fillRect(x + 2, y + 12, 10, 6, SSD1306_WHITE);
  int sway = sin(t * 0.006) * 3;
  display.drawLine(x + 7, y + 12, x + 7 + sway, y + 5, SSD1306_WHITE);
  display.fillCircle(x + 4 + sway, y + 4, 3, SSD1306_WHITE);
  display.fillCircle(x + 10 + sway, y + 3, 2, SSD1306_WHITE);
}

void drawCalendarIcon(int x, int y, unsigned long t) {
  display.drawRect(x, y + 2, 14, 12, SSD1306_WHITE);
  display.fillRect(x, y, 14, 3, SSD1306_WHITE);
  display.drawLine(x + 3, y - 1, x + 3, y + 1, SSD1306_WHITE);
  display.drawLine(x + 10, y - 1, x + 10, y + 1, SSD1306_WHITE);
  if ((t / 800) % 2 == 0) {
    display.drawPixel(x + 12, y + 12, SSD1306_BLACK);
  }
}
