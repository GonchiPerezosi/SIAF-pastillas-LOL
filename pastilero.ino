//Variables temporales (quitar codigo final es para compilar)
int ledPin = 2;
int ledState = LOW;

// #########################-TELEGRAM LIBRARIES-############################# //
#ifdef ESP32
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h> //esta es para el server pero me la pela
#endif
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <NTPClient.h>//para el ntp
#include <WiFiUdp.h>// para el ntp
// #########################-TELEGRAM LIBRARIES END-############################# //
//Las chupi variables de Gonzalo el hacker//

bool leds=true;
int hora;
int minutos;
//Fin :(//

// WIFI CREDENTIALS
const char* ssid = "REPLACE_WITH_YOUR_SSID";
const char* password = "REPLACE_WITH_YOUR_PASSWORD";
const char* serverUrl = "REPLACE_WITH_SERVER_URL"; // URL for the GET request

// WIFI CREDENTIALS END

// TELEGRAM CREDENTIALS
#define BOTtoken "8636360847:AAFWqMSwHP1N18Pls4wOBv9HgON-c3pOlPo"
#define CHAT_ID "-5015722816"
// TELEGRAM CREDENTIALS END

WiFiClientSecure client; // WIFI CLIENT

UniversalTelegramBot bot(BOTtoken, client);
int botRequestDelay = 1000;
unsigned long lastTimeBotRan;

void handleNewMessages(int numNewMessages) {
  Serial.println("handleNewMessages");
  Serial.println(String(numNewMessages));

  for (int i=0; i<numNewMessages; i++) {

    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != CHAT_ID) {
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }

    String text = bot.messages[i].text;
    Serial.println(text);

    String from_name = bot.messages[i].from_name;

    if (text == "/info") { //GON
 String msg = "Dispensador de pastillas activo\n";
msg += "Hora programada: ";
msg += hora;
msg += ":";
msg += minutos;
msg += "\nAviso visual LEDs: ";
msg += leds ? "ON" : "OFF";
msg += buzzer ? "ON" : "OFF";

bot.sendMessage(chat_id, msg, ""); 
   }

    if (text == "/change_pill_hours") { //GON
      bot.sendMessage(chat_id, "Por favor, ingresa la hora (solo la hora, no minutos):", "");
      hora = text.toInt();

      bot.sendMessage(chat_id, "Por favor ingrese los minutos de la hora a la que quieres que la pastilla caiga:", "");
      minutos = text.toInt();
    }

    if (text == "/toggle_leds") { //GON

      if (leds == true) {
        leds = false;
        bot.sendMessage(chat_id, "Aviso visual con LEDs desactivado", "");
      } 
      else {
        leds = true;
        bot.sendMessage(chat_id, "Aviso visual con LEDs activado", "");
      }

    }
    }

    if (text == "/???") { //GON
      bot.sendMessage(chat_id, "Comando no implementado", "");
    }

  }
}

//################# NTP CREDENTIALS ###########
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
//################# NTP CREDENTIALS END #######
//######################### NTP GET TIME ##########################
void getTime(String format) {
	timeClient.update();
	switch (format) {
	case "time":
		return timeClient.getFormattedTime();
	case "hours":
		return timeClient.getHours();
	case "minutes":
		return timeClient.getMinutes();
	case "seconds":
		return timeClient.getSeconds();
	}
}
//######################### NTP GET TIME END ######################

void setup() {
	//############# NTP INITIALIZE ########
	timeClient.begin();
	timeClient.setTimeOffset(7200); //Time offset to GMT+2 (summer in spain) for April.
	//############# NTP INITIALIZE END ####

	// PILL LOGIC
	//LIGHT LOGIC

	// ################ TELEGRAM LOGIC ################
	Serial.begin(115200);

#ifdef ESP8266
	configTime(0, 0, "pool.ntp.org");      // get UTC time via NTP
	client.setTrustAnchors(&cert); // Add root certificate for api.telegram.org
#endif

	pinMode(ledPin, OUTPUT);
	digitalWrite(ledPin, ledState);

	// Connect to Wi-Fi
	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, password);
#ifdef ESP32
	client.setCACert(TELEGRAM_CERTIFICATE_ROOT); // Add root certificate for api.telegram.org
#endif
	while (WiFi.status() != WL_CONNECTED) {
		delay(1000);
		Serial.println("Connecting to WiFi..");
	}
	// Print ESP32 Local IP Address
	Serial.println(WiFi.localIP());
	//################################ TELEGRAM LOGIC END #########################

	//################################ SERVER LOGIC -- GET DATA ######################################
	WiFiClient client;
	HTTPClient http;  // Declare an object of class HTTPClient

	http.begin(client, serverUrl); // Specify the destination URL

	int httpCode = http.GET();      // Send the GET request and store the result (HTTP status code)

	if (httpCode > 0) { // Check for a successful response code (e.g., HTTP_CODE_OK == 200)
		Serial.printf("[HTTP] GET... code: %d\n", httpCode);

		// Get the response payload
		String payload = http.getString();
		Serial.println(payload);            // Print the response payload
	} else {
		Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
	}

	http.end(); // Close the connection and free resources

	//################################ SERVER LOGIC -- GET DATA END #################################

}


void loop() {
	//pill logic
	//light and stuff logic

	//############################ TELEGRAM LOGIC ########################
	if (millis() > lastTimeBotRan + botRequestDelay)  {
		int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

		while(numNewMessages) {
			Serial.println("got response");
			handleNewMessages(numNewMessages);
			numNewMessages = bot.getUpdates(bot.last_message_received + 1);
		}
		lastTimeBotRan = millis();
	}
	//############################ TELEGRAM LOGIC END ####################

	//############################ SERVER LOGIC -- POST DATA #############
	client = ""
	         http.begin(client, serverUrl);
	// Content-Type: text/plain is used here; use application/json for JSON data
	http.addHeader("Content-Type", "text/plain");
	String postData = "Message from ESP8266";
	int statusCode = http.POST(postData);
	String payload = http.getString();

	Serial.print("HTTP status code: ");
	Serial.println(statusCode);
	Serial.print("Server response: ");
	Serial.println(payload);

	http.end();
	//######################## SERVER LOGIC -- POST DATA END #################

	//slavery logic
}
