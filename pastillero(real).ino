#include <Adafruit_NeoPixel.h>
#include <Stepper.h>

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

#include <NTPClient.h>//para el ntp
#include <WiFiUdp.h>// para el ntp

// --- Pins ---
const int PIEZO_PIN = 15;
const int BUTTON_PIN = 4;
const int BUZZER_PIN = 38;
const int NEOPIXEL_PIN = 5;
const int TRIG_PIN = 46;         
const int ECHO_PIN = 8;          
const int NUM_LEDS = 18;
const int STEPS_PER_REV = 2048;

// --- Motor Pins ---
const int IN1 = 42; const int IN2 = 40; const int IN3 = 41; const int IN4 = 39;

// --- Settings ---
int VIBE_THRESHOLD = 900;        
const int CALIBRATION_BUFFER = 200;
const int MIN_SAFE_FLOOR = 400;      // Safety floor for the threshold
const int MAX_SAFE_THRESHOLD = 4000; 
const int REQUIRED_VIBES = 3;           
const int DEBOUNCE_DELAY = 50;
const int VIBE_COOLDOWN = 250;    
const int DIST_THRESHOLD = 6;    
const long TIME_ALARM_1 = 6000;   
const long TIME_ALARM_2 = 1800;   

const char* ssid = "ssid";
const char* password = "psswd";
String serverName = "http://192.168.50.53:80/post_device_data";
String device_identity = "pill_dispenser_test_1";

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

enum SystemState { IDLE, TRAINING, STEPPER_RUN, DISTANCE_MONITOR };
SystemState state = IDLE;

Adafruit_NeoPixel strip(NUM_LEDS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
Stepper myStepper(STEPS_PER_REV, IN1, IN2, IN3, IN4);

// --- Variables ---
unsigned long lastDebounceTime = 0, stopTime = 0, lastFlashTime = 0, lastPrintTime = 0;
unsigned long lastVibeTime = 0;   
bool lastButtonState = HIGH, flashState = false;
uint16_t pixelHue = 0;
int currentVibeCount = 0;
int currentStep = 0;
int direction = 1; 

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());
  WiFiClient client;

  if (client.connect("192.168.50.53", 80)) {
    Serial.println("TCP connection SUCCESS");
  } else {
  Serial.println("TCP connection FAILED");
  }
  analogSetAttenuation(ADC_11db); 
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(PIEZO_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT); pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);

  digitalWrite(TRIG_PIN, LOW); 
  strip.begin();
  strip.show();
  myStepper.setSpeed(15); 
  
  releaseStepper(); 
  Serial.println("SYSTEM BOOTED: MAIN BRANCH READY");
  timeClient.begin();
}

void loop() {
  int currentReading = analogRead(PIEZO_PIN);
  int buttonReading = digitalRead(BUTTON_PIN);

  // --- Safety Guard: Max Threshold Pause ---
  if (currentReading > MAX_SAFE_THRESHOLD) {
    releaseStepper();
    if (millis() - lastPrintTime > 500) {
      Serial.print("!!! OVERLOAD DETECTED: "); Serial.println(currentReading);
      lastPrintTime = millis();
    }
    return; 
  }

  // --- Button Debounce Logic ---
  if (buttonReading == LOW && lastButtonState == HIGH && (millis() - lastDebounceTime > DEBOUNCE_DELAY)) {
    if (state == IDLE) {
      state = TRAINING;
    }
    lastDebounceTime = millis();
  }
  lastButtonState = buttonReading;

  switch (state) {
    case IDLE: {
      strip.clear(); strip.show();
      digitalWrite(BUZZER_PIN, LOW);
      releaseStepper();
      break;
    }

    case TRAINING: {
      Serial.print("\n>>> TRAINING PHASE (1s) - HIT #"); Serial.print(currentVibeCount + 1); Serial.println(" <<<");
      int peakFound = 0;
      unsigned long trainingStart = millis();
      
      while (millis() - trainingStart < 1000) {
        int sample = analogRead(PIEZO_PIN);
        
        // Ignore first 200ms to allow noise to settle
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
      
      digitalWrite(BUZZER_PIN, HIGH); delay(50); digitalWrite(BUZZER_PIN, LOW);
      
      currentStep = 0;
      direction = 1;
      state = STEPPER_RUN;
      break;
    }

    case STEPPER_RUN: {
      myStepper.step(direction); 
      currentStep++;

      if (currentStep >= STEPS_PER_REV) {
        direction *= -1;
        currentStep = 0;
        Serial.println(direction == 1 ? ">>> Swap: CW" : ">>> Swap: CCW");
      }

      cyclePixels();
      
      // Vibe Detection
      if (currentReading > VIBE_THRESHOLD && (millis() - lastVibeTime > VIBE_COOLDOWN)) {
        currentVibeCount++;
        lastVibeTime = millis(); 
        
        Serial.print("Vibe Detected! Strength: "); Serial.print(currentReading);
        Serial.print(" | Hit #"); Serial.println(currentVibeCount);
        
        if (currentVibeCount >= REQUIRED_VIBES) {
          handleVibrationEvent();
          currentVibeCount = 0; // Reset for next button press
        } else {
          digitalWrite(BUZZER_PIN, HIGH); delay(50); digitalWrite(BUZZER_PIN, LOW);
          state = TRAINING; // Re-train between hits
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

      if (d > 0 && d < DIST_THRESHOLD) {
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
      } else {
        if (elapsed > TIME_ALARM_1) runLedAlarm();
        if (elapsed > TIME_ALARM_2) digitalWrite(BUZZER_PIN, flashState);
      }
      break;
    }
  }
}

void releaseStepper() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
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
  long duration = pulseIn(ECHO_PIN, HIGH, 25000);
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
