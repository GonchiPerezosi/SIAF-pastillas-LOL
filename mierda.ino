// ==================== FINAL COMBINED PILL DISPENSER (Telegram + Dispenser + Web Server) ====================

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <Adafruit_NeoPixel.h>
#include <Stepper.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// =============== PIN DEFINITIONS ===============
int ledPin = 2;
int buzzerPin = 38;
const int PIEZO_PIN = 15;
const int BUTTON_PIN = 4;
const int NEOPIXEL_PIN = 5;
const int TRIG_PIN = 46;
const int ECHO_PIN = 8;
const int NUM_LEDS = 18;
const int STEPS_PER_REV = 2048;

const int IN1 = 42; const int IN2 = 40; const int IN3 = 41; const int IN4 = 39;

// =============== VARIABLES ===============
// From Telegram + Dispenser
bool leds = true;
bool buzzer = true;
int hora = 25;           // default - change via Telegram
int minutos = 0;
bool alertaDisparada = false;
bool esperandoHora = false;
bool esperandoMinutos = false;
int editingPillIndex = -1;   // -1 = no pill being edited
bool esperandoAddHora = false;
bool esperandoAddMinutos = false;
bool esperandoDeletePill = false;
int newPillHour = -1;

String device_identity = "pill_dispenser_test_1";

// Telegram
const char* ssid = "SÓTANO";
const char* password = "Luis1234";
#define BOTtoken "8636360847:AAFava3aD9v_wU8OkOPOF0cOAPgV4ZRKzWs"
#define CHAT_ID "-5015722816"

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);
int botRequestDelay = 1000;
unsigned long lastTimeBotRan = 0;

// Dispenser mechanics
int VIBE_THRESHOLD = 900;
const int CALIBRATION_BUFFER = 300;
const int MIN_SAFE_FLOOR = 400;
const int MAX_SAFE_THRESHOLD = 4000;
const int REQUIRED_VIBES = 3;
const int DEBOUNCE_DELAY = 50;
const int VIBE_COOLDOWN = 500;
const int DIST_THRESHOLD = 6;
const long TIME_ALARM_1 = 6000;
const long TIME_ALARM_2 = 10000;
volatile bool vibeDetected = false;
volatile int lastPeakValue = 0;

Adafruit_NeoPixel strip(NUM_LEDS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
Stepper myStepper(STEPS_PER_REV, IN1, IN2, IN3, IN4);

enum SystemState { IDLE, TRAINING, STEPPER_RUN, DISTANCE_MONITOR };
SystemState state = IDLE;

unsigned long lastDebounceTime = 0, stopTime = 0, lastFlashTime = 0, lastPrintTime = 0;
unsigned long lastVibeTime = 0;
bool lastButtonState = HIGH, flashState = false;
uint16_t pixelHue = 0;
int currentVibeCount = 0;
int currentStep = 0;
int direction = 1;

// =============== WEB SERVER (from your server file) ===============
WebServer server(80);

struct PillEntry {
  String identity;
  int hours;
  int minutes;
};
PillEntry pill_data[10];
int pill_count = 0;

struct TakenEntry {
  String identity;
  int hours;
  int minutes;
  int delay;
};
TakenEntry taken_data[100];
int taken_count = 0;

struct AirEntry {
  String identity;
  int hours;
  int minutes;
  int aqi;
  int eco2;
  int tvoc;
  int temp;
  int hum;
};
AirEntry air_data[100];
int air_count = 0;

struct Device {
  String identity;
  int device_type; // 0 = pill, 1 = air
};
Device devices[10];
int device_count = 0;

void addPillEntry(int h, int m) {
  if (pill_count > 9) {
    for (int i = 1; i < 10; i++) pill_data[i-1] = pill_data[i];
    pill_count--;
  }
  pill_data[pill_count].hours = h;
  pill_data[pill_count].minutes = m;
  pill_data[pill_count].identity = "none";
  pill_count++;
}
void deletePillEntry(int index) {
  if (index < 0 || index >= pill_count) return;

  for (int i = index; i < pill_count - 1; i++) {
    pill_data[i] = pill_data[i + 1];
  }

  pill_count--;
}

void addTakenEntry(String id, int h, int m, int d) {
  if (taken_count > 99) {
    for (int i = 1; i < 100; i++) taken_data[i-1] = taken_data[i];
    taken_count--;
  }
  taken_data[taken_count].identity = id;
  taken_data[taken_count].hours = h;
  taken_data[taken_count].minutes = m;
  taken_data[taken_count].delay = d;
  taken_count++;
}

void addAirEntry(String id, int h, int m, int aqi, int eco2, int tvoc, int t, int hum) {
  if (air_count > 99) {
    for (int i = 1; i < 100; i++) air_data[i-1] = air_data[i];
    air_count--;
  }
  air_data[air_count].identity = id;
  air_data[air_count].hours = h;
  air_data[air_count].minutes = m;
  air_data[air_count].aqi = aqi;
  air_data[air_count].eco2 = eco2;
  air_data[air_count].tvoc = tvoc;
  air_data[air_count].temp = t;
  air_data[air_count].hum = hum;
  air_count++;
}

void addDevice(String id, int type) {
  if (device_count > 9) return;
  devices[device_count].identity = id;
  devices[device_count].device_type = type;
  device_count++;
}

// =============== NTP HELPERS ===============
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

// =============== TELEGRAM FUNCTIONS ===============
String buildKeyboard() {
  String k = "[[{\"text\":\"-5h\",\"callback_data\":\"h-5\"},{\"text\":\"-1h\",\"callback_data\":\"h-1\"},"
             "{\"text\":\"+1h\",\"callback_data\":\"h+1\"},{\"text\":\"+5h\",\"callback_data\":\"h+5\"}],"
             "[{\"text\":\"-30min\",\"callback_data\":\"m-30\"},{\"text\":\"-15min\",\"callback_data\":\"m-15\"},"
             "{\"text\":\"+15min\",\"callback_data\":\"m+15\"},{\"text\":\"+30min\",\"callback_data\":\"m+30\"}],"
             "[{\"text\":\"✅ Confirmar\",\"callback_data\":\"confirmar\"}]]";
  return k;
}

String buildMenuMsg() {
  return "Hora programada: " + String(hora) + ":" + (minutos < 10 ? "0" : "") + String(minutos) + "\nUsa los botones:";
}

void enviarMenuHora(String chat_id) {
  bot.sendMessageWithInlineKeyboard(chat_id, buildMenuMsg(), "", buildKeyboard());
}

void sendPillSelectionMenu(String chat_id) {
  Serial.println("\n=== sendPillSelectionMenu() START ===");
  Serial.println("pill_count = " + String(pill_count));

  if (pill_count == 0) {
    Serial.println("No pills → sending simple message");
    bool sent = bot.sendMessage(chat_id, "No pills scheduled yet. Add some first.", "");
    Serial.println("sendMessage result: " + String(sent ? "SUCCESS" : "FAILED"));
    return;
  }

  // Build message
  String msg = "📋 **Select which pill to edit:**\n\n";
  for (int i = 0; i < pill_count; i++) {
    String dev = (pill_data[i].identity == "none" || pill_data[i].identity == "") ? 
                 "(unassigned)" : pill_data[i].identity;
    String timeStr = (pill_data[i].hours < 10 ? "0" : "") + String(pill_data[i].hours) + ":" +
                     (pill_data[i].minutes < 10 ? "0" : "") + String(pill_data[i].minutes);
    msg += String(i+1) + ". " + dev + " → " + timeStr + "\n";
  }

  // === Build keyboard more safely ===
  String keyboard = "[[";
  for (int i = 0; i < pill_count; i++) {
    if (i > 0) keyboard += ",";
    keyboard += "{\"text\":\"Pill " + String(i+1) + "\",\"callback_data\":\"select_pill_" + String(i) + "\"}";
  }
  keyboard += "]]";

  Serial.println("Message to send: " + msg);
  Serial.println("Keyboard JSON: " + keyboard);
  Serial.println("Keyboard length: " + String(keyboard.length()));

  // Send and check result
  bool result = bot.sendMessageWithInlineKeyboard(chat_id, msg, "Markdown", keyboard);

  Serial.println("=== sendMessageWithInlineKeyboard result: " + String(result ? "SUCCESS ✅" : "FAILED ❌") + " ===");

  if (!result) {
    Serial.println("→ Telegram rejected the message (bad JSON, network, rate limit, etc.)");
  }

  Serial.println("=== sendPillSelectionMenu() END ===\n");
}

void sendDeletePillMenu(String chat_id) {
  if (pill_count == 0) {
    bot.sendMessage(chat_id, "No pills scheduled to delete.", "");
    return;
  }

  String msg = "🗑️ Select which pill to delete:\n\n";
  for (int i = 0; i < pill_count; i++) {
    String dev = (pill_data[i].identity == "none" || pill_data[i].identity == "") ?
                 "(unassigned)" : pill_data[i].identity;
    String timeStr = (pill_data[i].hours < 10 ? "0" : "") + String(pill_data[i].hours) + ":" +
                     (pill_data[i].minutes < 10 ? "0" : "") + String(pill_data[i].minutes);

    msg += String(i + 1) + ". " + dev + " → " + timeStr + "\n";
  }

  String keyboard = "[[";
  for (int i = 0; i < pill_count; i++) {
    if (i > 0) keyboard += ",";
    keyboard += "{\"text\":\"Delete " + String(i + 1) + "\",\"callback_data\":\"delete_pill_" + String(i) + "\"}";
  }
  keyboard += "]]";

  bot.sendMessageWithInlineKeyboard(chat_id, msg, "", keyboard);
}


void handlePillSelectionCallback(String data, String chat_id) {
  if (data.startsWith("select_pill_")) {
    editingPillIndex = data.substring(12).toInt();  // extract index

    if (editingPillIndex < 0 || editingPillIndex >= pill_count) {
      bot.sendMessage(chat_id, "Invalid pill selected.", "");
      editingPillIndex = -1;
      return;
    }

    // Show current time and adjustment keyboard
    String currentTime = (pill_data[editingPillIndex].hours < 10 ? "0" : "") + String(pill_data[editingPillIndex].hours) + ":" +
                         (pill_data[editingPillIndex].minutes < 10 ? "0" : "") + String(pill_data[editingPillIndex].minutes);

    String msg = "✏️ Editing pill #" + String(editingPillIndex + 1) + "\n";
    msg += "Current time: " + currentTime + "\n";
    msg += "Dispenser: " + (pill_data[editingPillIndex].identity == "none" ? "(unassigned)" : pill_data[editingPillIndex].identity) + "\n\n";
    msg += "Use buttons to adjust:";

    bot.sendMessageWithInlineKeyboard(chat_id, msg, "", buildKeyboard());
  }
}

void handleTimeAdjustmentCallback(String data, String chat_id) {
  if (editingPillIndex < 0 || editingPillIndex >= pill_count) return;

  if (data == "h-5") pill_data[editingPillIndex].hours = (pill_data[editingPillIndex].hours - 5 + 24) % 24;
  else if (data == "h-1") pill_data[editingPillIndex].hours = (pill_data[editingPillIndex].hours - 1 + 24) % 24;
  else if (data == "h+1") pill_data[editingPillIndex].hours = (pill_data[editingPillIndex].hours + 1) % 24;
  else if (data == "h+5") pill_data[editingPillIndex].hours = (pill_data[editingPillIndex].hours + 5) % 24;
  else if (data == "m-30") pill_data[editingPillIndex].minutes = (pill_data[editingPillIndex].minutes - 30 + 60) % 60;
  else if (data == "m-15") pill_data[editingPillIndex].minutes = (pill_data[editingPillIndex].minutes - 15 + 60) % 60;
  else if (data == "m+15") pill_data[editingPillIndex].minutes = (pill_data[editingPillIndex].minutes + 15) % 60;
  else if (data == "m+30") pill_data[editingPillIndex].minutes = (pill_data[editingPillIndex].minutes + 30) % 60;
  else if (data == "confirmar") {
    String confirm = "✅ Pill #" + String(editingPillIndex + 1) + " updated to " +
                     (pill_data[editingPillIndex].hours < 10 ? "0" : "") + String(pill_data[editingPillIndex].hours) + ":" +
                     (pill_data[editingPillIndex].minutes < 10 ? "0" : "") + String(pill_data[editingPillIndex].minutes);
    bot.sendMessage(chat_id, confirm, "");
    editingPillIndex = -1;
    return;
  }

  // Refresh the adjustment menu
  String currentTime = (pill_data[editingPillIndex].hours < 10 ? "0" : "") + String(pill_data[editingPillIndex].hours) + ":" +
                       (pill_data[editingPillIndex].minutes < 10 ? "0" : "") + String(pill_data[editingPillIndex].minutes);

  String msg = "✏️ Editing pill #" + String(editingPillIndex + 1) + "\n";
  msg += "Current time: " + currentTime + "\n\nUse buttons to adjust:";

  bot.sendMessageWithInlineKeyboard(chat_id, msg, "", buildKeyboard());
}

void handleDeletePillCallback(String data, String chat_id) {
  if (!data.startsWith("delete_pill_")) return;

  int index = data.substring(12).toInt();

  if (index < 0 || index >= pill_count) {
    bot.sendMessage(chat_id, "Invalid pill selected.", "");
    return;
  }

  String dev = (pill_data[index].identity == "none" || pill_data[index].identity == "") ?
               "(unassigned)" : pill_data[index].identity;
  String timeStr = (pill_data[index].hours < 10 ? "0" : "") + String(pill_data[index].hours) + ":" +
                   (pill_data[index].minutes < 10 ? "0" : "") + String(pill_data[index].minutes);

  deletePillEntry(index);

  bot.sendMessage(chat_id, "✅ Deleted pill: " + dev + " → " + timeStr, "");
  esperandoDeletePill = false;
}

void handleCallback(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    if (bot.messages[i].type != F("callback_query")) continue;

    String chat_id = bot.messages[i].chat_id;
    String data = bot.messages[i].text;   // callback data

    if (chat_id != CHAT_ID) continue;

        // 1. Pill selection for editing
    if (data.startsWith("select_pill_")) {
      handlePillSelectionCallback(data, chat_id);
      return;
    }

    // 2. Pill selection for deleting
    if (data.startsWith("delete_pill_")) {
      handleDeletePillCallback(data, chat_id);
      return;
    }

    // 3. Time adjustment (when editing a pill)
    if (editingPillIndex >= 0 && editingPillIndex < pill_count) {
      handleTimeAdjustmentCallback(data, chat_id);
      return;
    }
  }
}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    if (chat_id != CHAT_ID) {
      bot.sendMessage(chat_id, "Unauthorized", "");
      continue;
    }

    // Skip callback queries here (they are handled in handleCallback)
    if (bot.messages[i].type == F("callback_query")) continue;

    String text = bot.messages[i].text;
		
		text.trim();
		Serial.print("Raw text: ");
		Serial.println(text);
		text.remove(0, 1);
		Serial.println(text);

    // === Manual hour/minute input mode ===
    if (esperandoHora) {
      int val = text.toInt();
      if (val < 0 || val > 23) {
        bot.sendMessage(chat_id, "Hora inválida (0-23)", "");
        continue;
      }
      hora = val;
      esperandoHora = false;
      esperandoMinutos = true;
      bot.sendMessage(chat_id, "Minutos (0-59):", "");
      continue;
    }

    if (esperandoMinutos) {
      int val = text.toInt();
      if (val < 0 || val > 59) {
        bot.sendMessage(chat_id, "Minutos inválidos (0-59)", "");
        continue;
      }
      minutos = val;
      esperandoMinutos = false;
      alertaDisparada = false;
      bot.sendMessage(chat_id, "Hora actualizada: " + String(hora) + ":" + 
                     (minutos < 10 ? "0" : "") + String(minutos), "");
      continue;
    }

		    if (esperandoAddHora) {
      int val = text.toInt();
      if (val < 0 || val > 23) {
        bot.sendMessage(chat_id, "Invalid hour (0-23).", "");
        continue;
      }

      newPillHour = val;
      esperandoAddHora = false;
      esperandoAddMinutos = true;
      bot.sendMessage(chat_id, "Enter minutes for new pill (0-59):", "");
      continue;
    }

    if (esperandoAddMinutos) {
      int val = text.toInt();
      if (val < 0 || val > 59) {
        bot.sendMessage(chat_id, "Invalid minutes (0-59).", "");
        continue;
      }

      addPillEntry(newPillHour, val);

      esperandoAddMinutos = false;
      newPillHour = -1;

      String timeStr = (pill_data[pill_count - 1].hours < 10 ? "0" : "") + String(pill_data[pill_count - 1].hours) + ":" +
                       (pill_data[pill_count - 1].minutes < 10 ? "0" : "") + String(pill_data[pill_count - 1].minutes);

      bot.sendMessage(chat_id, "✅ New pill added at " + timeStr, "");
      continue;
    }

    // === Commands ===
    if (text == "info") {
      // ... your existing /info code (unchanged) ...
      String msg = "📋 *Pill Dispenser Dashboard*\n\n";
      msg += "*Scheduled Pills:*\n\n";

      if (pill_count == 0) {
        msg += "No pills scheduled.\n";
      } else {
        msg += "Dispenser                  Time\n";
        msg += "─────────────────────────── ───────\n";

        int order[10];
        for (int i = 0; i < pill_count; i++) order[i] = i;
        for (int i = 0; i < pill_count-1; i++) {
          for (int j = i+1; j < pill_count; j++) {
            if ((pill_data[order[j]].hours*60 + pill_data[order[j]].minutes) < 
                (pill_data[order[i]].hours*60 + pill_data[order[i]].minutes)) {
              int temp = order[i]; order[i] = order[j]; order[j] = temp;
            }
          }
        }

        for (int k = 0; k < pill_count; k++) {
          int idx = order[k];
          String dev = (pill_data[idx].identity == "none" || pill_data[idx].identity == "") ? 
                       "(unassigned)" : pill_data[idx].identity;
          String timeStr = (pill_data[idx].hours < 10 ? "0" : "") + String(pill_data[idx].hours) + ":" +
                           (pill_data[idx].minutes < 10 ? "0" : "") + String(pill_data[idx].minutes);

          msg += dev.substring(0, 25);
          for (int pad = dev.length(); pad < 27; pad++) msg += " ";
          msg += timeStr + "\n";
        }
      }

      msg += "\nStats:\n";
      msg += "• Scheduled pills: " + String(pill_count) + "\n";
      msg += "• Taken records: " + String(taken_count) + "\n";
      msg += "• Registered devices: " + String(device_count);

      bot.sendMessage(chat_id, msg, "Markdown");
    }

    else if (text == "change_pill_hours") {
      Serial.println("→ MATCHED /change_pill_hours ! Calling sendPillSelectionMenu()");
      editingPillIndex = -1;
      sendPillSelectionMenu(chat_id);
      Serial.println("→ sendPillSelectionMenu() finished");
    }

        else if (text == "add_pill") {
      if (pill_count >= 10) {
        bot.sendMessage(chat_id, "Cannot add more pills. Max = 10.", "");
        continue;
      }

      esperandoAddHora = true;
      esperandoAddMinutos = false;
      newPillHour = -1;

      bot.sendMessage(chat_id, "Enter hour for new pill (0-23):", "");
    }

    else if (text == "delete_pill") {
      esperandoDeletePill = true;
      sendDeletePillMenu(chat_id);
    }
        else if (text == "help") {
      String help = "Available commands:\n";
      help += "/info - Show current schedule and stats\n";
      help += "/change_pill_hours - Edit existing pill times\n";
      help += "/add_pill - Add a new pill\n";
      help += "/delete_pill - Delete a pill\n";
      bot.sendMessage(chat_id, help, "");
    }
  }
}

// =============== DISPENSER HELPERS ===============
void releaseStepper() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW); digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
}

long getDistance() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 25000);
  return (duration <= 0) ? 999 : (duration * 0.0343) / 2;
}

void piezoSamplerTask(void* pvParameters) {
  while (true) {
    int sample = analogRead(PIEZO_PIN);

    if (sample > lastPeakValue) lastPeakValue = sample;

    if (sample > VIBE_THRESHOLD && !vibeDetected) {
      vibeDetected = true;
      lastPeakValue = sample;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
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

void handleVibrationEvent() {
  state = DISTANCE_MONITOR;
  stopTime = millis();
  strip.clear(); strip.show();
  digitalWrite(buzzerPin, HIGH); delay(200); digitalWrite(buzzerPin, LOW);
}

// =============== WEB SERVER HANDLERS (from your server file) ===============

void handleEmergencyBroadcast () {
	String msg = "🚨 *BOTÓN DE EMERGENCIA PULSADO* 🚨\n";
  msg += "Tal vez deberías llamar...";

  bot.sendMessage(CHAT_ID, msg, "Markdown");
	server.send(200, "text/plain", "OK");

}

void handlePillSchedule() {

	if (!server.hasArg("identity")) {
		server.send(400, "text/plain", "Missing parameters");
		return;
	}

	String device_identity = server.arg("identity");

	bool device_exists = false;
	for(int i = 0; i<10; i++) {
		if (devices[i].identity == device_identity) {
			device_exists = true;
			Serial.println("ScheduleHandlerCheck: device exists");
			break;
		}
	}
	if (!device_exists){
		addDevice(device_identity, 0);
		Serial.println("ScheduleHandlerCheck: Device does not exist. Writing device");
	}

	for (int i=0; i<10; i++) {

		if (pill_data[i].identity == "none") {

			pill_data[i].identity = device_identity;

			String json = "{";
			json += "\"drop-hour\":\"" + String(pill_data[i].hours) + "\",";
			json += "\"drop-minute\":\"" + String(pill_data[i].minutes) + "\"";
			json += "}";

			server.send(200, "application/json", json);
			return;
		}
	}

	server.send(404, "text/plain", "no available pills found");
}

void handlePillData() {

	if (!server.hasArg("hour") || !server.hasArg("minute") ||
		!server.hasArg("identity") || !server.hasArg("delay")) {

		server.send(400, "text/plain", "Missing parameters");
		return;
	}

	int hour = server.arg("hour").toInt();
	int minute = server.arg("minute").toInt();
	int delay = server.arg("delay").toInt();
	String identity = server.arg("identity");

	bool device_exists = false;
	for(int i = 0; i<10; i++) {
		if (devices[i].identity == identity) {
			device_exists = true;
			Serial.println("TakenHandlerCheck: device exists");
			break;
		}
	}
	if (!device_exists){
		addDevice(identity, 0);
		Serial.println("TakenHandlerCheck: Device does not exist. Writing device");
	}

	addTakenEntry(identity, hour, minute, delay);

	server.send(200, "text/plain", "OK");
}

void handleNewIdentity() {

	if (!server.hasArg("identity") || !server.hasArg("type")) {
		server.send(400, "text/plain", "Missing parameters");
		return;
	}

	String identity = server.arg("identity");
	int type = server.arg("type").toInt();

	addDevice(identity, type);

	server.send(200, "text/plain", "OK");
}
void handleAirData() {

	if (!server.hasArg("identity") || 
	    !server.hasArg("hour") || 
	    !server.hasArg("minute") || 
	    !server.hasArg("quality") || 
	    !server.hasArg("temp") || 
	    !server.hasArg("hum") || 
	    !server.hasArg("eco2") || 
	    !server.hasArg("tvoc")) {

		server.send(400, "text/plain", "Missing parameters");
		return;
	}

	if (air_count >= 100) {
		server.send(500, "text/plain", "Air data storage full");
		return;
	}

	String identity = server.arg("identity");
	int hour = server.arg("hour").toInt();
	int minute = server.arg("minute").toInt();
	int quality = server.arg("quality").toInt();
	int temp = server.arg("temp").toInt();
	int hum = server.arg("hum").toInt();
	int eco2 = server.arg("eco2").toInt();
	int tvoc = server.arg("tvoc").toInt();

	bool device_exists = false;
	for(int i = 0; i<10; i++) {
		if (devices[i].identity == identity) {
			device_exists = true;
			Serial.println("AirHandlerCheck: device exists");
			break;
		}
	}
	if (!device_exists){
		addDevice(identity, 1);
		Serial.println("AirHandlerCheck: Device does not exist. Writing device");
	}

	addAirEntry(identity, hour, minute, quality, eco2, tvoc, temp, hum);

	server.send(200, "text/plain", "OK");
}

void charts() {
    String html;
    html.reserve(42000);

    html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Device Dashboard</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        body {
            font-family: Arial, sans-serif;
            background: #f5f7fb;
            color: #222;
            margin: 0;
            padding: 20px;
        }
        .container { max-width: 1400px; margin: 0 auto; }
        h1, h2, h3 { margin-bottom: 10px; }
        .card {
            background: white;
            border-radius: 12px;
            padding: 20px;
            margin-bottom: 20px;
            box-shadow: 0 2px 8px rgba(0, 0, 0, 0.08);
        }
        .top-grid {
            display: grid;
            grid-template-columns: 1fr;
            gap: 20px;
            margin-bottom: 20px;
        }
        @media (min-width: 950px) {
            .top-grid { grid-template-columns: 1fr 1fr; }
        }
        .device-block {
            background: #ffffff;
            border-radius: 14px;
            padding: 20px;
            margin-bottom: 28px;
            box-shadow: 0 2px 10px rgba(0, 0, 0, 0.08);
            border-left: 6px solid #d9e2f2;
        }
        .device-title {
            margin-top: 0;
            margin-bottom: 16px;
            color: #1f3b63;
        }
        ul { padding-left: 20px; }
        li { margin-bottom: 8px; }
        .chart-wrapper {
            position: relative;
            height: 360px;
            width: 100%;
            margin-top: 10px;
        }
        .small-note {
            font-size: 0.9em;
            color: #666;
            margin-top: 0;
        }
        .subsection {
            margin-top: 20px;
            padding-top: 10px;
            border-top: 1px solid #e7ebf3;
        }
        table {
            width: 100%;
            border-collapse: collapse;
            margin-top: 10px;
        }
        th, td {
            padding: 10px 12px;
            border-bottom: 1px solid #e7ebf3;
            text-align: left;
            font-size: 0.95rem;
        }
        th { background: #f0f4fb; color: #1f3b63; }
        tr:hover td { background: #fafcff; }
        .muted { color: #666; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Device Dashboard</h1>

        <div class="top-grid">
            <div class="card">
                <h2>Registered Devices</h2>
                <ul>
)rawliteral";

    for (int i = 0; i < device_count; i++) {
        String typeName = (devices[i].device_type == 0) ? "Pill dispenser" : "Air sensor";
        html += "<li><strong>" + devices[i].identity + "</strong> - " + typeName + "</li>\n";
    }

    html += R"rawliteral(
                </ul>
            </div>

            <div class="card">
                <h2>Planned Pill Dispensing</h2>
                <p class="small-note">Scheduled pill drops ordered by time.</p>
                <table>
                    <thead><tr><th>Pill dispenser ID</th><th>Drop time</th></tr></thead>
                    <tbody>
)rawliteral";

    if (pill_count > 0) {
        int pillOrder[10];
        for (int i = 0; i < pill_count; i++) pillOrder[i] = i;
        for (int i = 0; i < pill_count-1; i++) {
            for (int j = i+1; j < pill_count; j++) {
                int ti = pill_data[pillOrder[i]].hours*60 + pill_data[pillOrder[i]].minutes;
                int tj = pill_data[pillOrder[j]].hours*60 + pill_data[pillOrder[j]].minutes;
                if (tj < ti) { int tmp = pillOrder[i]; pillOrder[i] = pillOrder[j]; pillOrder[j] = tmp; }
            }
        }
        for (int k = 0; k < pill_count; k++) {
            int i = pillOrder[k];
            String hh = (pill_data[i].hours < 10) ? "0" + String(pill_data[i].hours) : String(pill_data[i].hours);
            String mm = (pill_data[i].minutes < 10) ? "0" + String(pill_data[i].minutes) : String(pill_data[i].minutes);
            String dev = pill_data[i].identity;
            if (dev == "" || dev == "none") dev = "<span class=\"muted\">unassigned</span>";
            html += "<tr><td>" + dev + "</td><td>" + hh + ":" + mm + "</td></tr>\n";
        }
    } else {
        html += "<tr><td colspan=\"2\" class=\"muted\">No scheduled pills</td></tr>\n";
    }

    html += R"rawliteral(
                    </tbody>
                </table>
            </div>
        </div>

        <div class="card"><h2>Pill Dispensers</h2></div>
)rawliteral";

    // ==================== Pill Dispensers ====================
    for (int d = 0; d < device_count; d++) {
        if (devices[d].device_type != 0) continue;
        String devId = devices[d].identity;
        String canvasId = "takenChart" + String(d);

        html += "<div class=\"device-block\">";
        html += "<h2 class=\"device-title\">" + devId + " - Pill dispenser</h2>";

        html += "<div class=\"subsection\"><h3>Pill Schedule</h3><ul>";
        int schedIdx[10]; int schedN = 0;
        for (int i = 0; i < pill_count; i++) if (pill_data[i].identity == devId) schedIdx[schedN++] = i;
        for (int i = 0; i < schedN-1; i++) for (int j = i+1; j < schedN; j++) {
            int ti = pill_data[schedIdx[i]].hours*60 + pill_data[schedIdx[i]].minutes;
            int tj = pill_data[schedIdx[j]].hours*60 + pill_data[schedIdx[j]].minutes;
            if (tj < ti) { int tmp = schedIdx[i]; schedIdx[i] = schedIdx[j]; schedIdx[j] = tmp; }
        }
        if (schedN == 0) html += "<li>No assigned schedule</li>";
        else for (int s = 0; s < schedN; s++) {
            int i = schedIdx[s];
            String hh = (pill_data[i].hours < 10) ? "0" + String(pill_data[i].hours) : String(pill_data[i].hours);
            String mm = (pill_data[i].minutes < 10) ? "0" + String(pill_data[i].minutes) : String(pill_data[i].minutes);
            html += "<li>" + hh + ":" + mm + "</li>";
        }
        html += "</ul></div>";

        html += "<div class=\"subsection\"><h3>Taken Pills Delay</h3>";
        html += "<div class=\"chart-wrapper\"><canvas id=\"" + canvasId + "\"></canvas></div></div>";
        html += "</div>";
    }

    html += R"rawliteral(
        <div class="card"><h2>Air Sensors</h2></div>
)rawliteral";

    // ==================== Air Sensors ====================
    for (int d = 0; d < device_count; d++) {
        if (devices[d].device_type != 1) continue;
        String devId = devices[d].identity;
        String tempHumId = "tempHumChart" + String(d);
        String airId     = "airQualityChart" + String(d);

        html += "<div class=\"device-block\">";
        html += "<h2 class=\"device-title\">" + devId + " - Air sensor</h2>";

        html += "<div class=\"subsection\"><h3>Temperature and Humidity</h3>";
        html += "<div class=\"chart-wrapper\"><canvas id=\"" + tempHumId + "\"></canvas></div></div>";

        html += "<div class=\"subsection\"><h3>AQI, eCO2 and TVOC</h3>";
        html += "<div class=\"chart-wrapper\"><canvas id=\"" + airId + "\"></canvas></div></div>";
        html += "</div>";
    }

    html += R"rawliteral(
    </div>

    <script>
)rawliteral";

    // ==================== JS - Pill Dispensers ====================
    for (int d = 0; d < device_count; d++) {
        if (devices[d].device_type != 0) continue;
        String devId = devices[d].identity;
        String labelsVar = "takenLabels" + String(d);
        String dataVar   = "takenDelayData" + String(d);
        String canvasId  = "takenChart" + String(d);

        int idx[100]; int n = 0;
        for (int i = 0; i < taken_count; i++) if (taken_data[i].identity == devId) idx[n++] = i;
        for (int i = 0; i < n-1; i++) for (int j = i+1; j < n; j++) {
            int ti = taken_data[idx[i]].hours*60 + taken_data[idx[i]].minutes;
            int tj = taken_data[idx[j]].hours*60 + taken_data[idx[j]].minutes;
            if (tj < ti) { int tmp = idx[i]; idx[i] = idx[j]; idx[j] = tmp; }
        }

        html += "const " + labelsVar + " = [";
        for (int k = 0; k < n; k++) {
            int i = idx[k];
            String hh = (taken_data[i].hours < 10) ? "0" + String(taken_data[i].hours) : String(taken_data[i].hours);
            String mm = (taken_data[i].minutes < 10) ? "0" + String(taken_data[i].minutes) : String(taken_data[i].minutes);
            html += "\"" + hh + ":" + mm + "\"" + (k < n-1 ? "," : "");
        }
        html += "];\n";

        html += "const " + dataVar + " = [";
        for (int k = 0; k < n; k++) html += String(taken_data[idx[k]].delay) + (k < n-1 ? "," : "");
        html += "];\n";

        html += "if (" + labelsVar + ".length > 0) { new Chart(document.getElementById('" + canvasId + "'), {\n";
        html += "type: 'line',\n";
        html += "data: { labels: " + labelsVar + ", datasets: [{ label: 'Delay (min)', data: " + dataVar + ", borderColor: '#3366cc', backgroundColor: 'rgba(51,102,204,0.15)', tension: 0.35, fill: false, pointRadius: 4 }] },\n";
        html += "options: { responsive: true, maintainAspectRatio: false, interaction: {mode:'index', intersect:false}, plugins:{legend:{display:true}},\n";
        html += "scales: { x: {title:{display:true, text:'Time'}}, y: {title:{display:true, text:'Delay (min)'}, beginAtZero:true} } }\n";
        html += "}); }\n";
    }

    // ==================== JS - Air Sensors ====================
    for (int d = 0; d < device_count; d++) {
        if (devices[d].device_type != 1) continue;
        String devId = devices[d].identity;
        String labelsVar = "airLabels" + String(d);
        String tempVar = "tempData" + String(d);
        String humVar  = "humData" + String(d);
        String aqiVar  = "aqiData" + String(d);
        String eco2Var = "eco2Data" + String(d);
        String tvocVar = "tvocData" + String(d);

        String tempHumId = "tempHumChart" + String(d);
        String airId     = "airQualityChart" + String(d);

        int idx[100]; int n = 0;
        for (int i = 0; i < air_count; i++) if (air_data[i].identity == devId) idx[n++] = i;
        for (int i = 0; i < n-1; i++) for (int j = i+1; j < n; j++) {
            int ti = air_data[idx[i]].hours*60 + air_data[idx[i]].minutes;
            int tj = air_data[idx[j]].hours*60 + air_data[idx[j]].minutes;
            if (tj < ti) { int tmp = idx[i]; idx[i] = idx[j]; idx[j] = tmp; }
        }

        // Labels
        html += "const " + labelsVar + " = [";
        for (int k = 0; k < n; k++) {
            int i = idx[k];
            String hh = (air_data[i].hours < 10) ? "0" + String(air_data[i].hours) : String(air_data[i].hours);
            String mm = (air_data[i].minutes < 10) ? "0" + String(air_data[i].minutes) : String(air_data[i].minutes);
            html += "\"" + hh + ":" + mm + "\"" + (k < n-1 ? "," : "");
        }
        html += "];\n";

        // Data arrays
        html += "const " + tempVar + " = ["; for (int k=0; k<n; k++) html += String(air_data[idx[k]].temp)  + (k<n-1?",":""); html += "];\n";
        html += "const " + humVar  + " = ["; for (int k=0; k<n; k++) html += String(air_data[idx[k]].hum)   + (k<n-1?",":""); html += "];\n";
        html += "const " + aqiVar  + " = ["; for (int k=0; k<n; k++) html += String(air_data[idx[k]].aqi)   + (k<n-1?",":""); html += "];\n";
        html += "const " + eco2Var + " = ["; for (int k=0; k<n; k++) html += String(air_data[idx[k]].eco2)  + (k<n-1?",":""); html += "];\n";
        html += "const " + tvocVar + " = ["; for (int k=0; k<n; k++) html += String(air_data[idx[k]].tvoc)  + (k<n-1?",":""); html += "];\n";

        // Temperature + Humidity Chart (multiple Y axes)
        html += "if (" + labelsVar + ".length > 0) { new Chart(document.getElementById('" + tempHumId + "'), {\n";
        html += "type: 'line',\n";
        html += "data: { labels: " + labelsVar + ", datasets: [\n";
        html += "{ label: 'Temperature (°C)', data: " + tempVar + ", borderColor: '#e67e22', tension: 0.35, yAxisID: 'yTemp' },\n";
        html += "{ label: 'Humidity (%)', data: " + humVar  + ", borderColor: '#16a085', tension: 0.35, yAxisID: 'yHum' }\n";
        html += "]},\n";
        html += "options: { responsive: true, maintainAspectRatio: false, interaction: {mode:'index', intersect:false},\n";
        html += "scales: {\n";
        html += "x: { title: { display: true, text: 'Time' } },\n";
        html += "yTemp: { type: 'linear', position: 'left', title: { display: true, text: 'Temperature (°C)' }, suggestedMin: 15, suggestedMax: 35 },\n";
        html += "yHum:  { type: 'linear', position: 'right', title: { display: true, text: 'Humidity (%)' }, suggestedMin: 20, suggestedMax: 80, grid: { drawOnChartArea: false } }\n";
        html += "} } });\n";

        // Air Quality Chart (multiple Y axes)
        html += "new Chart(document.getElementById('" + airId + "'), {\n";
        html += "type: 'line',\n";
        html += "data: { labels: " + labelsVar + ", datasets: [\n";
        html += "{ label: 'AQI', data: " + aqiVar  + ", borderColor: '#c0392b', tension: 0.35, yAxisID: 'yAQI' },\n";
        html += "{ label: 'eCO2 (ppm)', data: " + eco2Var + ", borderColor: '#8e44ad', tension: 0.35, yAxisID: 'yECO2' },\n";
        html += "{ label: 'TVOC', data: " + tvocVar + ", borderColor: '#27ae60', tension: 0.35, yAxisID: 'yTVOC' }\n";
        html += "]},\n";
        html += "options: { responsive: true, maintainAspectRatio: false, interaction: {mode:'index', intersect:false},\n";
        html += "scales: {\n";
        html += "x: { title: { display: true, text: 'Time' } },\n";
        html += "yAQI:  { type: 'linear', position: 'left',  title: { display: true, text: 'AQI' }, min: 1, max: 5 },\n";
        html += "yECO2: { type: 'linear', position: 'right', title: { display: true, text: 'eCO2 (ppm)' }, suggestedMin: 400, suggestedMax: 1200, grid: { drawOnChartArea: false } },\n";
        html += "yTVOC: { type: 'linear', position: 'right', offset: true, title: { display: true, text: 'TVOC' }, suggestedMin: 0, suggestedMax: 500, grid: { drawOnChartArea: false } }\n";
        html += "} } });\n}\n";
    }

    html += R"rawliteral(
    </script>
</body>
</html>
)rawliteral";

    server.send(200, "text/html", html);
}

// =============== SETUP ===============
void setup() {
  Serial.begin(9600);

  pinMode(ledPin, OUTPUT); digitalWrite(ledPin, LOW);
  pinMode(buzzerPin, OUTPUT); digitalWrite(buzzerPin, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(PIEZO_PIN, INPUT_PULLUP);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT); pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);

  analogSetAttenuation(ADC_11db);

  strip.begin(); strip.show();
  myStepper.setSpeed(15);
  releaseStepper();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());

  client.setInsecure();
  timeClient.begin();
  timeClient.setTimeOffset(7200);

  // Web server routes
	if (!MDNS.begin("SIAFteleco")) {
        Serial.println("Error setting up mDNS responder!");
        while(1) {
            delay(1000);
        }
    }
    Serial.println("mDNS responder started");

  server.on("/charts", HTTP_GET, charts);
  server.on("/post_device_data", HTTP_POST, handlePillData);
  server.on("/post_air_data", HTTP_POST, handleAirData);
  server.on("/post_identity", HTTP_POST, handleNewIdentity);
	server.on("/pill_schedule", handlePillSchedule);
	server.on("/emergency_broadcast", handleEmergencyBroadcast);
  server.begin();
  Serial.println("HTTP server started");

  addPillEntry(13, 15);
  addPillEntry(16, 30);

  Serial.println("Full system ready (Telegram + Dispenser + Web Server)");
  xTaskCreatePinnedToCore(
  piezoSamplerTask,     // function
  "PiezoSampler",       // name
  2048,                 // stack
  NULL,                 // parameter
  4,                    // priority (higher than loop)
  NULL,                 // task handle
  0                     // core 0 (loop runs on core 1 by default)
);
}

// =============== LOOP ===============
void loop() {
  server.handleClient();   // Web server

  timeClient.update();
  int horaActual = getTime(2).toInt();
  int minutosActual = getTime(3).toInt();

  // Scheduled alarm triggers dispenser
  if (horaActual == hora && minutosActual == minutos && !alertaDisparada) {
    alertaDisparada = true;
    if (leds) digitalWrite(ledPin, HIGH);
    if (buzzer) digitalWrite(buzzerPin, HIGH);
    state = TRAINING;   // start dispensing process
  } else if (horaActual != hora || minutosActual != minutos) {
    digitalWrite(ledPin, LOW);
    digitalWrite(buzzerPin, LOW);
    alertaDisparada = false;
  }

  // Dispenser mechanics (your original logic)
  int currentReading = analogRead(PIEZO_PIN);
  int buttonReading = digitalRead(BUTTON_PIN);

  if (buttonReading == LOW && lastButtonState == HIGH && (millis() - lastDebounceTime > DEBOUNCE_DELAY)) {
    if (state == IDLE) state = TRAINING;
    lastDebounceTime = millis();
  }
  lastButtonState = buttonReading;

  switch (state) {
    case IDLE:
      strip.clear(); strip.show();
      digitalWrite(buzzerPin, LOW);
      releaseStepper();
      break;

    case TRAINING:
      // full training block from your file (kept as-is)
      {
        Serial.print("\n>>> TRAINING PHASE (1s) - HIT #"); Serial.print(currentVibeCount + 1); Serial.println(" <<<");
        int peakFound = 0;
        unsigned long trainingStart = millis();
        while (millis() - trainingStart < 1000) {
          int sample = analogRead(PIEZO_PIN);
          if (millis() - trainingStart > 200 && sample < MAX_SAFE_THRESHOLD && sample > peakFound) peakFound = sample;
          cyclePixels();
          if (millis() - lastPrintTime > 200) { Serial.print("Sampling: "); Serial.println(sample); lastPrintTime = millis(); }
        }
        VIBE_THRESHOLD = peakFound + CALIBRATION_BUFFER;
        if (VIBE_THRESHOLD < MIN_SAFE_FLOOR) VIBE_THRESHOLD = MIN_SAFE_FLOOR;
        Serial.print("TRAINING DONE. Threshold: "); Serial.println(VIBE_THRESHOLD);
        digitalWrite(buzzerPin, HIGH); delay(50); digitalWrite(buzzerPin, LOW);
        currentStep = 0; direction = 1; state = STEPPER_RUN;
      }
      break;

    case STEPPER_RUN:

      myStepper.step(direction);
      currentStep++;
      if (currentStep >= STEPS_PER_REV) { direction *= -1; currentStep = 0; }
      cyclePixels();
      if (vibeDetected && (millis() - lastVibeTime > VIBE_COOLDOWN)) {
        vibeDetected = false;
        currentVibeCount++;
        lastVibeTime = millis();
        if (currentVibeCount >= REQUIRED_VIBES+1) { 
          handleVibrationEvent();
          currentVibeCount = 0;
        } else {
          digitalWrite(buzzerPin, HIGH); delay(50); digitalWrite(buzzerPin, LOW); Serial.println(currentVibeCount);
          state = TRAINING;
        }
      }
      break;

    case DISTANCE_MONITOR:
          releaseStepper();
          long d = getDistance();
          unsigned long elapsed = millis() - stopTime;

          if (d > 0 && d < DIST_THRESHOLD) {
            // === DIRECTLY UPDATE THE TAKEN PILLS STRUCT (no HTTP) ===
        int currentHour = getTime(2).toInt();
        int currentMinute = getTime(3).toInt();
        int delayMinutes = int(round(elapsed / 60000.0));

        bool device_exists = false;
	for(int i = 0; i<10; i++) {
		if (devices[i].identity == device_identity) {
			device_exists = true;
			Serial.println("TakenHandlerCheck: device exists");
			break;
		}
	}
	     if (!device_exists){
		     addDevice(device_identity, 0);
		     Serial.println("TakenHandlerCheck: Device does not exist. Writing device");
	     }

	     addTakenEntry(device_identity, getTime(2).toInt(), getTime(3).toInt(), delayMinutes);

        Serial.print(">>> RECOVERY OK! Pill taken at ");
        Serial.print(currentHour);
        Serial.print(":");
        Serial.print(currentMinute < 10 ? "0" : "");
        Serial.print(currentMinute);
        Serial.print(" | Delay: ");
        Serial.print(delayMinutes);
        Serial.println(" min");

        state = IDLE;
      } 
      else {
        if (elapsed > TIME_ALARM_1) runLedAlarm();
        if (elapsed > TIME_ALARM_2) digitalWrite(buzzerPin, flashState);
      }
      break;
  }

  // Telegram bot
  if (millis() > lastTimeBotRan + botRequestDelay) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages) {
      for (int i = 0; i < numNewMessages; i++) {
        if (bot.messages[i].type == F("callback_query")) {
          handleCallback(numNewMessages);
          break;
        }
      }
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    lastTimeBotRan = millis();
  }
}
