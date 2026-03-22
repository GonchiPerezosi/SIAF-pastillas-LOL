#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// WiFi credentials
const char* ssid = "REPLACE_WITH_YOUR_SSID";
const char* password = "REPLACE_WITH_YOUR_PASSWORD";

// Create server on port 80
ESP8266WebServer server(80);

// Pill schedule data structure
struct PillEntry {
	String identity;
	int hours;
	int minutes;
};

PillEntry pill_data[10];
int pill_count = 0;

void addPillEntry(int h, int m) {
	pill_data[pill_count].hours = h;
	pill_data[pill_count].minutes = m;
	pill_data[pill_count].identity = "none";

	                                 pill_count++;
}
//Pill schedule data structure END

// Taken pills data structure
struct TakenEntry {
	String identity;
	int hours;
	int minutes;
	int delay;
};

TakenEntry taken_data[100];
int taken_count = 0;

void addTakenEntry(String id, int h, int m, int d) {
	taken_data[taken_count].identity = id;
	taken_data[taken_count].hours = h;
	taken_data[taken_count].minutes = m;
	taken_data[taken_count].delay = d;
	taken_count++;
}
//taken pills data structure END
// Air data structure
struct AirEntry {
	String identity;
	int hours;
	int minutes;
	int quality;
	int temp;
	int hum;
};

AirEntry air_data[100];
int air_count = 0;

void addAirEntry(String id, int h, int m, int q, int t, int hum) {
	air_data[air_count].hours = h;
	air_data[air_count].minutes = m;
	air_data[air_count].quality = q;
	air_data[air_count].temp = t;
	air_data[air_count].hum = hum;
	air_data[air_count].identity = id;
	air_count++;
}
//Air data structure END

//Devices in the ecosystem data structure
struct Device {
	String identity;
	int device_type; //0 for pill dispenser and 1 for air sensor
};

Device devices[10];
int device_count = 0;

void addDevice(String id, int type) {
	devices[device_count].identity = id;
	devices[device_count].device_type = type;
	device_count++;
}
//Devices in the ecosystem data structure END


void setup() {

	Serial.begin(115200);

	// Connect WiFi
	Serial.print("Connecting to ");
	Serial.println(ssid);

	WiFi.begin(ssid, password);

	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}

	Serial.println("");
	Serial.println("WiFi connected");
	Serial.println("IP address:");
	Serial.println(WiFi.localIP());

	// Routes

	server.on("/get_pill_schedule", HTTP_GET, handlePillSchedule);
	server.on("/post_device_data", HTTP_POST, handlePillData);
	server.on("/post_identity", HTTP_POST, handleNewIdentity);

	server.begin();

	Serial.println("HTTP server started");
}

void loop() {
	server.handleClient();
}

void handlePillSchedule() {
	if (!server.hasArg("identity")) {
		server.send(400, "text/plain", "Missing parameters");
		return;
	}
	String device_identity = server.arg("identity");

	for (int i=0; i<10; i++) {
		if (pill_data[i].identity)=="none") {
			pill_data[i].identity = device_identity;
			String json = "{";
			json += "\"drop-hour:\"" + pill_data[i].hours + "\",";
			json += "\"drop-minute\":\"" + pill_data[i].minutes + "\"";
			json += "}";
			server.send(200, "application/json", json);
		}
	}
	server.send(404, "text/plain", "no available pills found")

}
void handlePillData() {

	if (!server.hasArg("hour") || !server.hasArg("minute") || !server.hasArg("identity") || !server.hasArg("delay")) {
		server.send(400, "text/plain", "Missing parameters");
		return;
	}

	int hour = server.arg("gpio");
	int minute = server.arg("state");
	String identity = server.arg("identity");
	int delay = server.arg("delay");
	addTakenEntry(id=identity, h=hour, m=minute, d=delay);
	server.send(200, "text/plain", "OK");
}
void handleNewIdentity() {
	if (!server.hasArg("identity") || !server.hasArg("type")) {
		server.send(400, "text/plain", "Missing parameters");
		return;
	}

	String identity = server.arg("identity");
	int type = server.arg("type");
	addDevice(id=identity, type=type);
	server.send(200, "text/plain", "OK");
}





