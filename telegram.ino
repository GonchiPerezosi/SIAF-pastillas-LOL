// Variables
int ledPin    = 2;
int buzzerPin = 4;

bool leds   = true;
bool buzzer = true;
int  hora    = 8;
int  minutos = 0;
bool alertaDisparada = false;

bool esperandoHora    = false;
bool esperandoMinutos = false;

// ############## LIBRARIES ##############
#ifdef ESP32
  #include <WiFi.h>
#else
  #include <ESP8266WiFi.h>
#endif
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
// ############## LIBRARIES END ##############

const char* ssid      = "Wifi";
const char* password  = "Contraseña";
const char* serverUrl = "REPLACE_WITH_SERVER_URL";

#define BOTtoken "8636360847:AAFava3aD9v_wU8OkOPOF0cOAPgV4ZRKzWs"  // <-- pon el token nuevo, este está expuesto
#define CHAT_ID  "-5015722816"

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

int           botRequestDelay = 1000;
unsigned long lastTimeBotRan;

WiFiUDP    ntpUDP;
NTPClient  timeClient(ntpUDP, "pool.ntp.org");

// ############## NTP HELPERS ##############
String getFormattedTime() { timeClient.update(); return timeClient.getFormattedTime(); }
int    getHours()         { timeClient.update(); return timeClient.getHours();         }
int    getMinutes()       { timeClient.update(); return timeClient.getMinutes();       }
int    getSeconds()       { timeClient.update(); return timeClient.getSeconds();       }
// ############## NTP HELPERS END ##############

// ---- Construye el teclado inline (evita duplicar el string) ----
String buildKeyboard() {
  String k = "[[";
  k += "{\"text\":\"-5h\",  \"callback_data\":\"h-5\"},";
  k += "{\"text\":\"-1h\",  \"callback_data\":\"h-1\"},";
  k += "{\"text\":\"+1h\",  \"callback_data\":\"h+1\"},";
  k += "{\"text\":\"+5h\",  \"callback_data\":\"h+5\"}";
  k += "],[";
  k += "{\"text\":\"-30min\",\"callback_data\":\"m-30\"},";
  k += "{\"text\":\"-15min\",\"callback_data\":\"m-15\"},";
  k += "{\"text\":\"+15min\",\"callback_data\":\"m+15\"},";
  k += "{\"text\":\"+30min\",\"callback_data\":\"m+30\"}";
  k += "],[";
  k += "{\"text\":\"✅ Confirmar\",\"callback_data\":\"confirmar\"}";
  k += "]]";
  return k;
}

// ---- Construye el texto del menú ----
String buildMenuMsg() {
  String msg = "Hora actual: " + String(hora) + ":" + (minutos < 10 ? "0" : "") + String(minutos) + "\n";
  msg += "Usa los botones para ajustar:";
  return msg;
}

// ---- Envía el menú con botones ----
void enviarMenuHora(String chat_id) {
  bot.sendMessageWithInlineKeyboard(chat_id, buildMenuMsg(), "", buildKeyboard());
}

// ---- Manejo de callbacks (botones pulsados) ----
void handleCallback(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {

    String chat_id = String(bot.messages[i].chat_id);
    String data    = bot.messages[i].text; // callback_data viene en text

    if (chat_id != CHAT_ID) continue;

    // Ajusta hora y minutos con wrap-around
    if (data == "h-5")  hora    = (hora    - 5  + 24) % 24;
    if (data == "h-1")  hora    = (hora    - 1  + 24) % 24;
    if (data == "h+1")  hora    = (hora    + 1)        % 24;
    if (data == "h+5")  hora    = (hora    + 5)        % 24;
    if (data == "m-30") minutos = (minutos - 30 + 60)  % 60;
    if (data == "m-15") minutos = (minutos - 15 + 60)  % 60;
    if (data == "m+15") minutos = (minutos + 15)        % 60;
    if (data == "m+30") minutos = (minutos + 30)        % 60;

    if (data == "confirmar") {
      alertaDisparada = false;
      String confirm = "✅ Hora guardada: " + String(hora) + ":" + (minutos < 10 ? "0" : "") + String(minutos);
      bot.sendMessage(chat_id, confirm, "");
      return;
    }

    // Reenvía el menú actualizado tras cada pulsación
    bot.sendMessageWithInlineKeyboard(chat_id, buildMenuMsg(), "", buildKeyboard());
  }
}

// ############## TELEGRAM MESSAGES ##############
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {

    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != CHAT_ID) {
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }

    // Los callbacks se procesan en handleCallback, aquí los ignoramos
    if (bot.messages[i].type == F("callback_query")) continue;

    String text = bot.messages[i].text;

    // ---------- ESTADOS ----------
    if (esperandoHora) {
      int val = text.toInt();
      if (val < 0 || val > 23) {
        bot.sendMessage(chat_id, "Hora inválida. Introduce un número entre 0 y 23:", "");
        continue;
      }
      hora             = val;
      esperandoHora    = false;
      esperandoMinutos = true;
      bot.sendMessage(chat_id, "Ahora introduce los minutos (0-59):", "");
      continue;
    }

    if (esperandoMinutos) {
      int val = text.toInt();
      if (val < 0 || val > 59) {
        bot.sendMessage(chat_id, "Minutos inválidos. Introduce un número entre 0 y 59:", "");
        continue;
      }
      minutos          = val;
      esperandoMinutos = false;
      alertaDisparada  = false;
      String confirm   = "Hora actualizada: " + String(hora) + ":" + (minutos < 10 ? "0" : "") + String(minutos);
      bot.sendMessage(chat_id, confirm, "");
      continue;
    }

    // ---------- COMANDOS ----------
    if (text == "/info") {
      String msg = "Dispensador de pastillas activo\n";
      msg += "Hora programada: " + String(hora) + ":" + (minutos < 10 ? "0" : "") + String(minutos) + "\n";
      msg += "LEDs: ";   msg += leds   ? "ON" : "OFF"; msg += "\n";
      msg += "Buzzer: "; msg += buzzer ? "ON" : "OFF";
      bot.sendMessage(chat_id, msg, "");
    }

    if (text == "/change_pill_hours") {
      esperandoHora    = false;
      esperandoMinutos = false;
      enviarMenuHora(chat_id);
    }

    if (text == "/toggle_leds") {
      leds = !leds;
      bot.sendMessage(chat_id, leds ? "LEDs activados" : "LEDs desactivados", "");
    }

    if (text == "/toggle_buzzer") {
      buzzer = !buzzer;
      bot.sendMessage(chat_id, buzzer ? "Buzzer activado" : "Buzzer desactivado", "");
    }
  }
}
// ############## TELEGRAM MESSAGES END ##############

void setup() {
  Serial.begin(115200);

  pinMode(ledPin,    OUTPUT); digitalWrite(ledPin,    LOW);
  pinMode(buzzerPin, OUTPUT); digitalWrite(buzzerPin, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  #ifdef ESP32
    client.setInsecure();
  #else
    configTime(0, 0, "pool.ntp.org");
  #endif

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Conectando...");
  }
  Serial.println(WiFi.localIP());

  timeClient.begin();
  timeClient.setTimeOffset(7200); // GMT+2
}

void loop() {
  timeClient.update();

  int horaActual    = getHours();
  int minutosActual = getMinutes();

  // ---- Lógica pastilla ----
  if (horaActual == hora && minutosActual == minutos) {
    if (!alertaDisparada) {
      if (leds)   digitalWrite(ledPin,    HIGH);
      if (buzzer) digitalWrite(buzzerPin, HIGH);
      alertaDisparada = true;
    }
  } else {
    digitalWrite(ledPin,    LOW);
    digitalWrite(buzzerPin, LOW);
    alertaDisparada = false;
  }

  // ---- Telegram ----
  if (millis() > lastTimeBotRan + botRequestDelay) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    while (numNewMessages) {
      // Primero callbacks, luego mensajes normales
      for (int i = 0; i < numNewMessages; i++) {
        if (bot.messages[i].type == F("callback_query")) {
          handleCallback(numNewMessages);
          break; // handleCallback itera todos, no hace falta seguir
        }
      }
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }

    lastTimeBotRan = millis();
  }
}
