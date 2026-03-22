#include <Adafruit_NeoPixel.h>
#include <Stepper.h>

// --- Pins ---
const int PIEZO_PIN = 10;
const int BUTTON_PIN = 4;
const int BUZZER_PIN = 38;
const int NEOPIXEL_PIN = 5;
const int TRIG_PIN = 46;         
const int ECHO_PIN = 8;          
const int NUM_LEDS = 18;
const int STEPS_PER_REV = 2048;

// --- Motor Pins ---
const int IN1 = 42; const int IN2 = 40; const int IN3 = 41; const int IN4 = 39;

// --- Dynamic Settings (Trained) ---
int VIBE_THRESHOLD = 500;        // Will be: Max + 200
int DIST_THRESHOLD = 20;         // Will be: Min - 1
int REQUIRED_VIBES = 3;           
const int DEBOUNCE_DELAY = 50;
const long TIME_ALARM_1 = 3000;   
const long TIME_ALARM_2 = 6000;   

enum SystemState { CALIBRATING, IDLE, STEPPER_RUN, DISTANCE_MONITOR };
SystemState state = CALIBRATING;

Adafruit_NeoPixel strip(NUM_LEDS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
Stepper myStepper(STEPS_PER_REV, IN1, IN2, IN3, IN4);

// --- Variables ---
unsigned long lastDebounceTime = 0, stopTime = 0, lastFlashTime = 0, lastPrintTime = 0;
unsigned long trainingStartTime = 0;
bool lastButtonState = HIGH, flashState = false;
uint16_t pixelHue = 0;
int currentVibeCount = 0;
int currentStep = 0;
int direction = 1; 

// --- Calibration Holders ---
int maxVibeObserved = 0;
long minPulseObserved = 99999;

void setup() {
  Serial.begin(115200);
  analogSetAttenuation(ADC_11db); 
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT); pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);

  digitalWrite(TRIG_PIN, LOW); 
  strip.begin();
  strip.show();
  myStepper.setSpeed(15); 
  releaseStepper(); 
  
  trainingStartTime = millis();
  Serial.println("--- CALIBRATING: DO NOT TOUCH ---");
}

void loop() {
  // --- 1. BUTTON LOGIC ---
  int buttonReading = digitalRead(BUTTON_PIN);
  bool buttonTriggered = false;
  if (buttonReading == LOW && lastButtonState == HIGH && (millis() - lastDebounceTime > DEBOUNCE_DELAY)) {
    buttonTriggered = true;
    lastDebounceTime = millis();
  }
  lastButtonState = buttonReading;

  // --- 2. STATE MACHINE ---
  switch (state) {
    
    case CALIBRATING: {
      unsigned long elapsed = millis() - trainingStartTime;
      strip.fill(strip.Color(0, 0, 255)); // Blue for calibration
      strip.show();

      if (elapsed < 2000) {
        // Phase 1: Vibe Training (0-2s)
        int currentV = analogRead(PIEZO_PIN);
        if (currentV > maxVibeObserved) maxVibeObserved = currentV;
      } 
      else if (elapsed < 4000) {
        // Phase 2: Distance Training (2-4s)
        long d = getDistancePulse(); // Raw pulse for accuracy
        if (d > 0 && d < minPulseObserved) minPulseObserved = d;
      } 
      else {
        // Training Complete
        VIBE_THRESHOLD = maxVibeObserved + 200;
        DIST_THRESHOLD = ((minPulseObserved * 0.0343) / 2) - 1;
        if (DIST_THRESHOLD < 2) DIST_THRESHOLD = 2; // Safety floor

        Serial.println("--- TRAINING COMPLETE ---");
        Serial.print("New Vibe Threshold: "); Serial.println(VIBE_THRESHOLD);
        Serial.print("New Dist Threshold: "); Serial.print(DIST_THRESHOLD); Serial.println("cm");
        
        state = IDLE;
      }
      break;
    }

    case IDLE: {
      if (buttonTriggered) {
        Serial.println("\n>>> STARTING <<<");
        currentVibeCount = 0;
        currentStep = 0;
        direction = 1;
        state = STEPPER_RUN;
      }
      strip.clear(); strip.show();
      digitalWrite(BUZZER_PIN, LOW);
      digitalWrite(TRIG_PIN, LOW); 
      releaseStepper();
      break;
    }

    case STEPPER_RUN: {
      myStepper.step(direction); 
      currentStep++;
      if (currentStep >= STEPS_PER_REV) { direction *= -1; currentStep = 0; }

      cyclePixels();
      int vibeValue = analogRead(PIEZO_PIN);
      
      if (vibeValue > VIBE_THRESHOLD || buttonTriggered) {
        currentVibeCount++;
        Serial.print(buttonTriggered ? "MANUAL HIT #" : "VIBE HIT #");
        Serial.print(currentVibeCount); Serial.print(" | Val: "); Serial.println(vibeValue);
        
        if (currentVibeCount >= REQUIRED_VIBES) handleVibrationEvent();
        else {
          digitalWrite(BUZZER_PIN, HIGH); delay(50); digitalWrite(BUZZER_PIN, LOW);
          delay(250); 
        }
      }
      if (millis() - lastPrintTime > 200) {
        Serial.print("Vibe: "); Serial.print(vibeValue); Serial.print(" | Hits: "); Serial.println(currentVibeCount);
        lastPrintTime = millis();
      }
      break;
    }

    case DISTANCE_MONITOR: {
      releaseStepper();
      long d = getDistance();
      unsigned long elapsed = millis() - stopTime;

      if (buttonTriggered) {
        Serial.println(">>> MANUAL SKIP <<<");
        state = IDLE;
      }

      if (millis() - lastPrintTime > 500) {
        Serial.print("DIST: "); Serial.print(d); Serial.println("cm");
        lastPrintTime = millis();
      }

      if (d > 0 && d < DIST_THRESHOLD) {
        Serial.println(">>> RECOVERED <<<");
        state = IDLE;                
      } 
      else {
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
  delay(1000); 
}

// Separate helper for raw pulse during training
long getDistancePulse() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  return pulseIn(ECHO_PIN, HIGH, 25000);
}

long getDistance() {
  long duration = getDistancePulse();
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
