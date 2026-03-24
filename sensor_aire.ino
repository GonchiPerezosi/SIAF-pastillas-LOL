#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <ScioSense_ENS160.h>
#include <Adafruit_NeoPixel.h>

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

const char* ssid = "ssid";
const char* password = "contraseña"; //como q no os la voy a dar la mia
String serverName = "http://192.168.0.168/post_air_data";

// --- Pins ---
#define BUZZER_PIN 14 
#define NEO_PIN    12 
#define BUTTON_PIN 13 
#define NUM_PIXELS 15

// --- AQI BASED THRESHOLDS ---
const int AQI_LEVEL_ORANGE = 2;
const int AQI_LEVEL_RED    = 4;

// --- Global Objects ---
Adafruit_AHTX0 aht;
ScioSense_ENS160 ens160(ENS160_I2CADDR_1); 
Adafruit_NeoPixel pixels(NUM_PIXELS, NEO_PIN, NEO_GRB + NEO_KHZ800);

// --- State Variables ---
bool showGreen = true;      
int lastButtonState = LOW;
unsigned long lastSendTime = 0; 

// --- Function Prototypes ---
void applyColor(int r, int g, int b);
void updateLedsBasedOnAir(int currentAqi);
void flashExposure(); 

void setup() {
  Serial.begin(115200); 

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
  Serial.println("\n--- CLEAN START: AQI & CO2 MONITOR ---");

  Wire.begin(); 
  aht.begin();
  if (ens160.begin()) {
    ens160.setMode(ENS160_OPMODE_STD);
  }

  pixels.begin();
  pixels.setBrightness(50);
  pixels.clear();
  pixels.show();
  
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void loop() {
  // 1. BUTTON CHECK (EXPOSURE)
  int currentButtonState = digitalRead(BUTTON_PIN);
  if (currentButtonState == LOW && lastButtonState == HIGH) {
    showGreen = !showGreen; 
    flashExposure();
  }

  lastButtonState = currentButtonState;

  // 2. SENSOR UPDATES
  sensors_event_t humidityEvent, tempEvent;
  aht.getEvent(&humidityEvent, &tempEvent);

  int tempC = tempEvent.temperature;
  int humidity = humidityEvent.relative_humidity;

  Serial.print("Temp: ");
  Serial.print(tempC);
  Serial.print(" C | ");

  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");

  if (ens160.available()) {
    ens160.set_envdata(tempC, humidity);
    ens160.measure(true);
    int currentAqi = ens160.getAQI();

    updateLedsBasedOnAir(currentAqi); 

    if (millis() - lastSendTime >= 10000) {

      WiFiClient client;
      HTTPClient http;

      http.begin(client, serverName);
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");

      String httpRequestData;
      httpRequestData = "identity=air_sensor";
      httpRequestData += "&hour=0";
      httpRequestData += "&minute=0";
      httpRequestData += "&quality=" + String(currentAqi);
      httpRequestData += "&temp=" + String(tempC);
      httpRequestData += "&hum=" + String(humidity);

      int httpResponseCode = http.POST(httpRequestData);

      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);

      http.end();
      lastSendTime = millis();
  }
    Serial.print("AQI: "); Serial.print(currentAqi);
    Serial.print(" | CO2: "); Serial.print(ens160.geteCO2());
    Serial.print(" ppm | TVOC: "); Serial.print(ens160.getTVOC());
    Serial.println(" ppb");
  }

  // 3. PULSING BUZZER LOGIC (Only in Red Alert)
  static unsigned long lastBeep = 0;
  if (ens160.getAQI() >= AQI_LEVEL_RED) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(1000);
    digitalWrite(BUZZER_PIN, LOW);
  }

  delay(1000); 
}

void flashExposure() {
  for (int i = 0; i < NUM_PIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(255, 255, 255));
  }
  pixels.show();
  digitalWrite(BUZZER_PIN, HIGH);
  delay(60); 
  digitalWrite(BUZZER_PIN, LOW);
  pixels.clear();
  pixels.show();
}

void updateLedsBasedOnAir(int currentAqi) {
    int aqi = currentAqi;

  if (aqi >= AQI_LEVEL_RED) {
    applyColor(255, 0, 0); // RED
  } 
  else if (aqi >= AQI_LEVEL_ORANGE) {
    applyColor(255, 100, 0); // ORANGE
  } 
  else {
    if (showGreen) applyColor(0, 255, 0); // GREEN
    else { pixels.clear(); pixels.show(); }
  }
}

void applyColor(int r, int g, int b) {
  for (int i = 0; i < NUM_PIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(r, g, b));
  }
  pixels.show();
}
