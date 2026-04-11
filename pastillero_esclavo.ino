#include <Adafruit_NeoPixel.h>

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

#include <NTPClient.h>//para el ntp
#include <WiFiUdp.h>// para el ntp

const char* ssid = "Iphone de Borja";
const char* password = "B0rjo1977";
String serverName = "http://siafteleco/post_device_data";
String device_identity = "pill_dispenser_test_1";

// --- Pins ---
const int PIEZO_PIN = A0;
const int BUTTON_PIN = 17;     
const int BUZZER_PIN = 18;     
const int NEOPIXEL_PIN = 2;    
const int TRIG_PIN = 4;        
const int ECHO_PIN = 5;        

// --- Motor Pins ---
const int IN1 = 14; const int IN2 = 12; const int IN3 = 13; const int IN4 = 15;

// --- Settings ---
const int NUM_LEDS = 18;
const int STEPS_PER_REV = 2048;
int VIBE_THRESHOLD = 900;        
const int CALIBRATION_BUFFER = 150;
const int MIN_SAFE_FLOOR = 300;      
const int MAX_SAFE_THRESHOLD = 1020; 
const int REQUIRED_VIBES = 3;           
const int DEBOUNCE_DELAY = 500;
const int VIBE_COOLDOWN = 250;    
const int DIST_THRESHOLD = 6;    
const long TIME_ALARM_1 = 6000;   
const long TIME_ALARM_2 = 18000; 

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
String getTime(int format) {
  timeClient.update();

  switch (format) {

    case 1: {
      String form = timeClient.getFormattedTime();
      return form;
    }

    case 2: {
      return String(timeClient.getHours());
    }

    case 3: {
      return String(timeClient.getMinutes());
    }

    case 4: {
      return String(timeClient.getSeconds());
    }

    default:
      return "";
  }
}

enum SystemState { IDLE, START_TRAINING, STEPPER_RUN, DISTANCE_MONITOR };
SystemState state = IDLE;

Adafruit_NeoPixel strip(NUM_LEDS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// --- Variables ---
unsigned long lastDebounceTime = 0, stopTime = 0, lastFlashTime = 0, lastPrintTime = 0;
unsigned long lastVibeTime = 0;   
bool lastButtonState = HIGH, flashState = false;
uint16_t pixelHue = 0;
int currentVibeCount = 0;
unsigned long lastReqTime = 0; 

// --- Manual Stepper Logic Variables ---
int motorStepIndex = 0; 
unsigned long lastStepTime = 0;
const int stepDelay = 3; 
int currentStep = 0;
int direction = 1; 

struct AssignedPill {
  int hour = -1;
  int minute = -1;
};

AssignedPill assignedPill;
int extractJsonInt(const String& json, const String& key) {
  String pattern = "\"" + key + "\":\"";
  int start = json.indexOf(pattern);
  if (start == -1) return -1;

  start += pattern.length();
  int end = json.indexOf("\"", start);
  if (end == -1) return -1;

  String value = json.substring(start, end);
  value.trim();

  for (int i = 0; i < value.length(); i++) {
    if (!isDigit(value[i])) return -1;
  }

  return value.toInt();
}

bool requestPillSchedule(serverUrl, myIdentity) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return false;
  }

  HTTPClient http;

  String url = serverUrl + "?identity=" + myIdentity;
  http.begin(url);

  int httpCode = http.GET();

  if (httpCode <= 0) {
    Serial.print("HTTP request failed: ");
    Serial.println(http.errorToString(httpCode));
    http.end();
    return false;
  }

  if (httpCode != 200) {
    Serial.print("Unexpected HTTP code: ");
    Serial.println(httpCode);
    String payload = http.getString();
    Serial.println(payload);
    http.end();
    return false;
  }

  String payload = http.getString();
  Serial.println("Server response:");
  Serial.println(payload);

  int h = extractJsonInt(payload, "drop-hour");
  int m = extractJsonInt(payload, "drop-minute");

  if (h < 0 || h > 23 || m < 0 || m > 59) {
    Serial.println("Invalid schedule received");
    http.end();
    return false;
  }

  assignedPill.hour = h;
  assignedPill.minute = m;

  http.end();
  return true;
}

// --- Vibration Start Variable ---
int START_VIBE_THRESHOLD = 500; // Variable for the trigger vibration

void setup() {
  Serial.begin(19200); 

  WiFi.begin(ssid, password);
  Serial.println("Connecting");
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to WiFi network with IP Address: ");
  Serial.println(WiFi.localIP());

  delay(1000); 
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT); pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);

  digitalWrite(TRIG_PIN, LOW); 
  strip.begin();
  strip.show();
  
  releaseStepper(); 
  Serial.println("SYSTEM BOOTED: MAIN BRANCH READY");
}

void loop() {
  int currentReading = analogRead(PIEZO_PIN);
  int buttonReading = digitalRead(BUTTON_PIN);
  if (millis() - lastSendTime >= 60000) {
    bool ok = requestPillSchedule("http://siafteleco/pill_schedule", device_identity);

if (ok) {
  Serial.println("Schedule assigned correctly");
  Serial.print("Hour: ");
  Serial.println(assignedPill.hour);
  Serial.print("Minute: ");
  Serial.println(assignedPill.minute);
} else {
  Serial.println("Failed to get schedule");
}
  }

  // Button still works as an override/reset to IDLE
  if (buttonReading == LOW && lastButtonState == HIGH && (millis() - lastDebounceTime > DEBOUNCE_DELAY)) {
    state = IDLE;
    currentVibeCount = 0;
    lastDebounceTime = millis();
    Serial.println("SYSTEM RESET TO IDLE");
  }
  lastButtonState = buttonReading;

  switch (state) {
    case IDLE: {
      strip.clear(); strip.show();
      digitalWrite(BUZZER_PIN, LOW);
      releaseStepper();
      
      // --- Start when vibration is detected ---
      if (currentReading > START_VIBE_THRESHOLD && (millis() - lastVibeTime > VIBE_COOLDOWN)) {
        Serial.print("Initial Vibe Detected: "); Serial.println(currentReading);
        state = START_TRAINING;
        lastVibeTime = millis();
      }
      break;
    }

    case START_TRAINING: {
      Serial.print("\n>>> TRAINING PHASE (1s) - HIT #"); Serial.print(currentVibeCount + 1); Serial.println(" <<<");
      int peakFound = 0;
      unsigned long trainingStart = millis();
      
      while (millis() - trainingStart < 1000) {
        int sample = analogRead(PIEZO_PIN);
        if (millis() - trainingStart > 200) {
            if (sample > peakFound && sample < MAX_SAFE_THRESHOLD) {
                peakFound = sample;
            }
        }
        cyclePixels(); 
        if (millis() - lastPrintTime > 200) {
            Serial.print("Sampling: "); Serial.println(sample);
            lastPrintTime = millis();
        }
      }

      VIBE_THRESHOLD = peakFound + CALIBRATION_BUFFER;
      if (VIBE_THRESHOLD < MIN_SAFE_FLOOR) VIBE_THRESHOLD = MIN_SAFE_FLOOR;

      Serial.print("TRAINING DONE. Peak: "); Serial.print(peakFound);
      Serial.print(" | Final Threshold: "); Serial.println(VIBE_THRESHOLD);
      
      
      currentStep = 0;
      direction = 1;
      state = STEPPER_RUN;
      break;
    }

    case STEPPER_RUN: {
      if (millis() - lastStepTime > stepDelay) {
        motorStepIndex += direction;
        if (motorStepIndex > 3) motorStepIndex = 0;
        if (motorStepIndex < 0) motorStepIndex = 3;

        switch (motorStepIndex) {
          case 0: setStepper(1, 0, 0, 0); break;
          case 1: setStepper(0, 1, 0, 0); break;
          case 2: setStepper(0, 0, 1, 0); break;
          case 3: setStepper(0, 0, 0, 1); break;
        }

        currentStep++;
        if (currentStep >= STEPS_PER_REV) {
          direction *= -1;
          currentStep = 0;
          Serial.println(direction == 1 ? ">>> Swap: CW" : ">>> Swap: CCW");
        }
        lastStepTime = millis();
      }

      cyclePixels();
      
      if (currentReading > VIBE_THRESHOLD && (millis() - lastVibeTime > VIBE_COOLDOWN)) {
        currentVibeCount++;
        lastVibeTime = millis(); 
        Serial.print("Vibe Detected! Strength: "); Serial.print(currentReading);
        Serial.print(" | Hit #"); Serial.println(currentVibeCount);
        
        if (currentVibeCount >= REQUIRED_VIBES) {
          handleVibrationEvent();
          currentVibeCount = 0; 
        } else {
          digitalWrite(BUZZER_PIN, HIGH); delay(50); digitalWrite(BUZZER_PIN, LOW);
          state = START_TRAINING; 
        }
      }

      if (millis() - lastPrintTime > 200) {
        Serial.print("Vibe: "); Serial.print(currentReading);
        Serial.print(" | Hits: "); Serial.print(currentVibeCount);
        Serial.print("/"); Serial.println(REQUIRED_VIBES);
        lastPrintTime = millis();
      }
      break;
    }

    case DISTANCE_MONITOR: {
      releaseStepper();
      long d = getDistance();
      unsigned long elapsed = millis() - stopTime;

      if (millis() - lastPrintTime > 500) {
        Serial.print("MONITORING | Dist: "); Serial.print(d); Serial.println("cm");
        lastPrintTime = millis();
      }
      if (d > 3 && d < DIST_THRESHOLD) {
        Serial.println("cazamos una mano");
          delay(1000);
      
      if (d > 3 && d < DIST_THRESHOLD) {
        Serial.print(">>> RECOVERY OK! Final Dist: "); Serial.print(d); Serial.println("cm");
        WiFiClient client;
        HTTPClient http;

        http.begin(client, serverName);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
        String httpRequestData;
        httpRequestData = "identity=" + device_identity;
        httpRequestData += "&hour=" + getTime(2); 
        httpRequestData += "&minute=" + getTime(3);
        httpRequestData += "&delay=" + String(int(round(elapsed / 60000.0)));
        int httpResponseCode = http.POST(httpRequestData);
        Serial.print("HTTP Response code: ");
        Serial.println(httpResponseCode);
        http.end();
        state = IDLE;                
      }} else {
        if (elapsed > TIME_ALARM_1) runLedAlarm();
      }
      break;
    }
  }
}

void setStepper(int a, int b, int c, int d) {
  digitalWrite(IN1, a); digitalWrite(IN2, b); digitalWrite(IN3, c); digitalWrite(IN4, d);
}

void releaseStepper() {
  setStepper(0, 0, 0, 0);
}

void handleVibrationEvent() {
  state = DISTANCE_MONITOR;
  stopTime = millis();
  strip.clear(); strip.show();
  digitalWrite(BUZZER_PIN, HIGH); delay(200); digitalWrite(BUZZER_PIN, LOW);
}

long getDistance() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 20000);
  return (duration <= 0) ? 999 : (duration * 0.0343) / 2;
}

void runLedAlarm() {
  if (millis() - lastFlashTime > 250) {
    flashState = !flashState;
    if (flashState) strip.fill(strip.Color(255, 0, 0));
    else strip.clear();
    strip.show();
    lastFlashTime = millis();
  }
}

void cyclePixels() {
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 20) {
    strip.fill(strip.gamma32(strip.ColorHSV(pixelHue)));
    strip.show();
    pixelHue += 256;
    lastUpdate = millis();
  }
}