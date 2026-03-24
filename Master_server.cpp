#include <WiFi.h>
#include <WebServer.h>

// WiFi credentials
const char* ssid = "ssid";
const char* password = "contraseña";

// Create server on port 80
WebServer server(80);

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

// Devices data structure
struct Device {
	String identity;
	int device_type; //0 pill dispenser, 1 air sensor
};

Device devices[10];
int device_count = 0;

void addDevice(String id, int type) {
	devices[device_count].identity = id;
	devices[device_count].device_type = type;
	device_count++;
}

void setup() {

	Serial.begin(115200);

	Serial.print("Connecting to ");
	Serial.println(ssid);

	WiFi.begin(ssid, password);

	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}

	Serial.println("");
	Serial.println("WiFi connected");
	Serial.println(WiFi.localIP());

	server.on("/get_pill_schedule", HTTP_GET, handlePillSchedule);
	server.on("/charts", HTTP_GET, charts);
	server.on("/post_device_data", HTTP_POST, handlePillData);
  server.on("/post_air_data", HTTP_POST, handleAirData);
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
	    !server.hasArg("hum")) {

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

	addAirEntry(identity, hour, minute, quality, temp, hum);

	server.send(200, "text/plain", "OK");
}

void charts() {

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<title>Air Sensor Data</title>";
  html += "<style>body{font-family:Arial;text-align:center;background:#f4f4f4;}canvas{background:white;border:1px solid #ccc;}</style>";
  html += "</head><body>";
  html += "<h2>Air Sensor Data</h2>";
  html += "<canvas id='chart' width='700' height='350'></canvas>";

  html += "<script>";

  // Air quality data
  html += "var quality=[";
  for(int i=0;i<air_count;i++){
      html += String(air_data[i].quality);
      if(i < air_count-1) html += ",";
  }
  html += "];";

  // Temperature data
  html += "var temp=[";
  for(int i=0;i<air_count;i++){
      html += String(air_data[i].temp);
      if(i < air_count-1) html += ",";
  }
  html += "];";

  // Humidity data
  html += "var hum=[";
  for(int i=0;i<air_count;i++){
      html += String(air_data[i].hum);
      if(i < air_count-1) html += ",";
  }
  html += "];";

  // Labels (time)
  html += "var labels=[";
  for(int i=0;i<air_count;i++){
      html += "'" + String(air_data[i].hours) + ":" + String(air_data[i].minutes) + "'";
      if(i < air_count-1) html += ",";
  }
  html += "];";

  html += R"rawliteral(

var canvas=document.getElementById('chart');
var ctx=canvas.getContext('2d');

var width=canvas.width;
var height=canvas.height;
var padding=50;

var max=Math.max(...quality,...temp,...hum,10);

ctx.beginPath();
ctx.moveTo(padding,padding);
ctx.lineTo(padding,height-padding);
ctx.lineTo(width-padding,height-padding);
ctx.stroke();

var barWidth=(width-padding*2)/labels.length/3;

for(var i=0;i<labels.length;i++){

    var qh=(quality[i]/max)*(height-padding*2);
    var th=(temp[i]/max)*(height-padding*2);
    var hh=(hum[i]/max)*(height-padding*2);

    // air quality (red)
    ctx.fillStyle="#c0392b";
    ctx.fillRect(padding+i*barWidth*3,height-padding-qh,barWidth,qh);

    // temperature (orange)
    ctx.fillStyle="#e67e22";
    ctx.fillRect(padding+i*barWidth*3+barWidth,height-padding-th,barWidth,th);

    // humidity (blue)
    ctx.fillStyle="#3498db";
    ctx.fillRect(padding+i*barWidth*3+barWidth*2,height-padding-hh,barWidth,hh);

    ctx.fillStyle="black";
    ctx.fillText(labels[i],padding+i*barWidth*3,height-padding+15);
}

)rawliteral";

  html += "</script></body></html>";

  server.send(200,"text/html",html);
}
