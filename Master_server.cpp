#include <WiFi.h>
#include <WebServer.h>

// WiFi credentials
const char* ssid = "ssid";
const char* password = "passwd";

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
	if (pill_count > 9) {
    for (int i = 1; i < 10; i++) {
        pill_data[i - 1] = pill_data[i];
    }
    pill_count--;
	}
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
	if (taken_count > 99) {
    for (int i = 1; i < 100; i++) {
        taken_data[i - 1] = taken_data[i];
    }
    taken_count--;
	}
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
	int aqi;
	int eco2;
	int tvoc;
	int temp;
	int hum;
};

AirEntry air_data[100];
int air_count = 0;

void addAirEntry(String id, int h, int m, int aqi, int eco2, int tvoc, int t, int hum) {
	if (air_count > 99) {
    for (int i = 1; i < 100; i++) {
        air_data[i - 1] = air_data[i];
    }
    air_count--;
	}
	air_data[air_count].hours = h;
	air_data[air_count].minutes = m;
	air_data[air_count].aqi = aqi;
	air_data[air_count].eco2 = eco2;
	air_data[air_count].tvoc = tvoc;
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
	if (device_count > 9){
		Serial.println("DeviceHandler: Maximum device limit reached");
		return;
	}
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
	addPillEntry(13, 15);
	addPillEntry(16, 30);
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

		.container {
			max-width: 1400px;
			margin: 0 auto;
		}

		h1, h2, h3 {
			margin-bottom: 10px;
		}

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
			.top-grid {
				grid-template-columns: 1fr 1fr;
			}
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

		ul {
			padding-left: 20px;
		}

		li {
			margin-bottom: 8px;
		}

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

		th {
			background: #f0f4fb;
			color: #1f3b63;
		}

		tr:hover td {
			background: #fafcff;
		}

		.muted {
			color: #666;
		}
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
				<p class="small-note">Scheduled pill drops by pill dispenser, ordered by time.</p>
				<table>
					<thead>
						<tr>
							<th>Pill dispenser ID</th>
							<th>Drop time</th>
						</tr>
					</thead>
					<tbody>
)rawliteral";

	// Global pill schedule sorted by time
	if (pill_count > 0) {
		int pillOrder[10];
		for (int i = 0; i < pill_count; i++) {
			pillOrder[i] = i;
		}

		for (int i = 0; i < pill_count - 1; i++) {
			for (int j = i + 1; j < pill_count; j++) {
				int ti = pill_data[pillOrder[i]].hours * 60 + pill_data[pillOrder[i]].minutes;
				int tj = pill_data[pillOrder[j]].hours * 60 + pill_data[pillOrder[j]].minutes;
				if (tj < ti) {
					int tmp = pillOrder[i];
					pillOrder[i] = pillOrder[j];
					pillOrder[j] = tmp;
				}
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
)rawliteral";

	// ----------------------------
	// PILL DISPENSERS BY DEVICE
	// ----------------------------
	html += R"rawliteral(
		<div class="card">
			<h2>Pill Dispensers</h2>
			<p class="small-note">Each pill dispenser is shown separately with its own assigned schedule and taken-pill delay chart. Chart points are ordered by time.</p>
		</div>
)rawliteral";

	for (int d = 0; d < device_count; d++) {
		if (devices[d].device_type != 0) continue;

		String devId = devices[d].identity;
		String takenCanvasId = "takenChart" + String(d);

		html += "<div class=\"device-block\">";
		html += "<h2 class=\"device-title\">" + devId + " - Pill dispenser</h2>";

		// Schedule subsection
		html += "<div class=\"subsection\">";
		html += "<h3>Pill Schedule</h3>";
		html += "<ul>";

		int scheduleIdx[10];
		int scheduleN = 0;

		for (int i = 0; i < pill_count; i++) {
			if (pill_data[i].identity == devId) {
				scheduleIdx[scheduleN++] = i;
			}
		}

		for (int i = 0; i < scheduleN - 1; i++) {
			for (int j = i + 1; j < scheduleN; j++) {
				int ti = pill_data[scheduleIdx[i]].hours * 60 + pill_data[scheduleIdx[i]].minutes;
				int tj = pill_data[scheduleIdx[j]].hours * 60 + pill_data[scheduleIdx[j]].minutes;
				if (tj < ti) {
					int tmp = scheduleIdx[i];
					scheduleIdx[i] = scheduleIdx[j];
					scheduleIdx[j] = tmp;
				}
			}
		}

		if (scheduleN == 0) {
			html += "<li>No assigned schedule</li>";
		} else {
			for (int s = 0; s < scheduleN; s++) {
				int i = scheduleIdx[s];
				String hh = (pill_data[i].hours < 10) ? "0" + String(pill_data[i].hours) : String(pill_data[i].hours);
				String mm = (pill_data[i].minutes < 10) ? "0" + String(pill_data[i].minutes) : String(pill_data[i].minutes);
				html += "<li>" + hh + ":" + mm + "</li>\n";
			}
		}

		html += "</ul>";
		html += "</div>";

		// Taken pills chart subsection
		html += "<div class=\"subsection\">";
		html += "<h3>Taken Pills Delay</h3>";
		html += "<p class=\"small-note\">X axis = time, Y axis = delay in minutes</p>";
		html += "<div class=\"chart-wrapper\"><canvas id=\"" + takenCanvasId + "\"></canvas></div>";
		html += "</div>";

		html += "</div>";
	}

	// ----------------------------
	// AIR SENSORS BY DEVICE
	// ----------------------------
	html += R"rawliteral(
		<div class="card">
			<h2>Air Sensors</h2>
			<p class="small-note">Each air sensor is shown separately with its own environmental charts. Chart points are ordered by time.</p>
		</div>
)rawliteral";

	for (int d = 0; d < device_count; d++) {
		if (devices[d].device_type != 1) continue;

		String devId = devices[d].identity;
		String tempHumCanvasId = "tempHumChart" + String(d);
		String airQualityCanvasId = "airQualityChart" + String(d);

		html += "<div class=\"device-block\">";
		html += "<h2 class=\"device-title\">" + devId + " - Air sensor</h2>";

		html += "<div class=\"subsection\">";
		html += "<h3>Temperature and Humidity</h3>";
		html += "<p class=\"small-note\">Two Y axes are used so both variables remain readable without changing their real values.</p>";
		html += "<div class=\"chart-wrapper\"><canvas id=\"" + tempHumCanvasId + "\"></canvas></div>";
		html += "</div>";

		html += "<div class=\"subsection\">";
		html += "<h3>AQI, eCO2 and TVOC</h3>";
		html += "<p class=\"small-note\">Each metric keeps its real values and uses its own scale.</p>";
		html += "<div class=\"chart-wrapper\"><canvas id=\"" + airQualityCanvasId + "\"></canvas></div>";
		html += "</div>";

		html += "</div>";
	}

	html += R"rawliteral(
	</div>

	<script>
)rawliteral";

	// ----------------------------
	// JS FOR PILL DISPENSERS
	// ----------------------------
	for (int d = 0; d < device_count; d++) {
		if (devices[d].device_type != 0) continue;

		String devId = devices[d].identity;
		String takenLabelsVar = "takenLabels" + String(d);
		String takenDataVar = "takenDelayData" + String(d);
		String takenCanvasId = "takenChart" + String(d);

		int takenIdx[100];
		int takenN = 0;

		for (int i = 0; i < taken_count; i++) {
			if (taken_data[i].identity == devId) {
				takenIdx[takenN++] = i;
			}
		}

		for (int i = 0; i < takenN - 1; i++) {
			for (int j = i + 1; j < takenN; j++) {
				int ti = taken_data[takenIdx[i]].hours * 60 + taken_data[takenIdx[i]].minutes;
				int tj = taken_data[takenIdx[j]].hours * 60 + taken_data[takenIdx[j]].minutes;
				if (tj < ti) {
					int tmp = takenIdx[i];
					takenIdx[i] = takenIdx[j];
					takenIdx[j] = tmp;
				}
			}
		}

		html += "const " + takenLabelsVar + " = [\n";
		for (int k = 0; k < takenN; k++) {
			int i = takenIdx[k];
			String hh = (taken_data[i].hours < 10) ? "0" + String(taken_data[i].hours) : String(taken_data[i].hours);
			String mm = (taken_data[i].minutes < 10) ? "0" + String(taken_data[i].minutes) : String(taken_data[i].minutes);
			html += "\"" + hh + ":" + mm + "\"";
			if (k < takenN - 1) html += ",\n";
		}
		html += "\n];\n";

		html += "const " + takenDataVar + " = [\n";
		for (int k = 0; k < takenN; k++) {
			int i = takenIdx[k];
			html += String(taken_data[i].delay);
			if (k < takenN - 1) html += ",\n";
		}
		html += "\n];\n";

		html += "if (" + takenLabelsVar + ".length > 0) {\n";
		html += "new Chart(document.getElementById('" + takenCanvasId + "'), {\n";
		html += "type: 'line',\n";
		html += "data: {\n";
		html += "labels: " + takenLabelsVar + ",\n";
		html += "datasets: [{\n";
		html += "label: 'Delay (min)',\n";
		html += "data: " + takenDataVar + ",\n";
		html += "borderColor: '#3366cc',\n";
		html += "backgroundColor: 'rgba(51, 102, 204, 0.15)',\n";
		html += "tension: 0.35,\n";
		html += "fill: false,\n";
		html += "pointRadius: 4,\n";
		html += "pointHoverRadius: 6\n";
		html += "}]\n";
		html += "},\n";
		html += "options: {\n";
		html += "responsive: true,\n";
		html += "maintainAspectRatio: false,\n";
		html += "interaction: { mode: 'index', intersect: false },\n";
		html += "plugins: { legend: { display: true } },\n";
		html += "scales: {\n";
		html += "x: { title: { display: true, text: 'Time' } },\n";
		html += "y: { title: { display: true, text: 'Delay (min)' }, beginAtZero: true }\n";
		html += "}\n";
		html += "}\n";
		html += "});\n";
		html += "}\n";
	}

	// ----------------------------
	// JS FOR AIR SENSORS
	// ----------------------------
	for (int d = 0; d < device_count; d++) {
		if (devices[d].device_type != 1) continue;

		String devId = devices[d].identity;

		String labelsVar = "airLabels" + String(d);
		String tempVar = "tempData" + String(d);
		String humVar = "humData" + String(d);
		String aqiVar = "aqiData" + String(d);
		String eco2Var = "eco2Data" + String(d);
		String tvocVar = "tvocData" + String(d);

		String tempHumCanvasId = "tempHumChart" + String(d);
		String airQualityCanvasId = "airQualityChart" + String(d);

		int airIdx[100];
		int airN = 0;

		for (int i = 0; i < air_count; i++) {
			if (air_data[i].identity == devId) {
				airIdx[airN++] = i;
			}
		}

		for (int i = 0; i < airN - 1; i++) {
			for (int j = i + 1; j < airN; j++) {
				int ti = air_data[airIdx[i]].hours * 60 + air_data[airIdx[i]].minutes;
				int tj = air_data[airIdx[j]].hours * 60 + air_data[airIdx[j]].minutes;
				if (tj < ti) {
					int tmp = airIdx[i];
					airIdx[i] = airIdx[j];
					airIdx[j] = tmp;
				}
			}
		}

		// labels
		html += "const " + labelsVar + " = [\n";
		for (int k = 0; k < airN; k++) {
			int i = airIdx[k];
			String hh = (air_data[i].hours < 10) ? "0" + String(air_data[i].hours) : String(air_data[i].hours);
			String mm = (air_data[i].minutes < 10) ? "0" + String(air_data[i].minutes) : String(air_data[i].minutes);
			html += "\"" + hh + ":" + mm + "\"";
			if (k < airN - 1) html += ",\n";
		}
		html += "\n];\n";

		// temp
		html += "const " + tempVar + " = [\n";
		for (int k = 0; k < airN; k++) {
			int i = airIdx[k];
			html += String(air_data[i].temp);
			if (k < airN - 1) html += ",\n";
		}
		html += "\n];\n";

		// hum
		html += "const " + humVar + " = [\n";
		for (int k = 0; k < airN; k++) {
			int i = airIdx[k];
			html += String(air_data[i].hum);
			if (k < airN - 1) html += ",\n";
		}
		html += "\n];\n";

		// aqi
		html += "const " + aqiVar + " = [\n";
		for (int k = 0; k < airN; k++) {
			int i = airIdx[k];
			html += String(air_data[i].aqi);
			if (k < airN - 1) html += ",\n";
		}
		html += "\n];\n";

		// eco2
		html += "const " + eco2Var + " = [\n";
		for (int k = 0; k < airN; k++) {
			int i = airIdx[k];
			html += String(air_data[i].eco2);
			if (k < airN - 1) html += ",\n";
		}
		html += "\n];\n";

		// tvoc
		html += "const " + tvocVar + " = [\n";
		for (int k = 0; k < airN; k++) {
			int i = airIdx[k];
			html += String(air_data[i].tvoc);
			if (k < airN - 1) html += ",\n";
		}
		html += "\n];\n";

		// temp/hum chart
		html += "if (" + labelsVar + ".length > 0) {\n";
		html += "new Chart(document.getElementById('" + tempHumCanvasId + "'), {\n";
		html += "type: 'line',\n";
		html += "data: {\n";
		html += "labels: " + labelsVar + ",\n";
		html += "datasets: [\n";
		html += "{ label: 'Temperature (°C)', data: " + tempVar + ", borderColor: '#e67e22', backgroundColor: 'rgba(230, 126, 34, 0.15)', tension: 0.35, fill: false, pointRadius: 4, pointHoverRadius: 6, yAxisID: 'yTemp' },\n";
		html += "{ label: 'Humidity (%)', data: " + humVar + ", borderColor: '#16a085', backgroundColor: 'rgba(22, 160, 133, 0.15)', tension: 0.35, fill: false, pointRadius: 4, pointHoverRadius: 6, yAxisID: 'yHum' }\n";
		html += "]\n";
		html += "},\n";
		html += "options: {\n";
		html += "responsive: true,\n";
		html += "maintainAspectRatio: false,\n";
		html += "interaction: { mode: 'index', intersect: false },\n";
		html += "plugins: { legend: { display: true } },\n";
		html += "scales: {\n";
		html += "x: { title: { display: true, text: 'Time' } },\n";
		html += "yTemp: { type: 'linear', position: 'left', title: { display: true, text: 'Temperature (°C)' }, suggestedMin: 15, suggestedMax: 35 },\n";
		html += "yHum: { type: 'linear', position: 'right', title: { display: true, text: 'Humidity (%)' }, suggestedMin: 20, suggestedMax: 80, grid: { drawOnChartArea: false } }\n";
		html += "}\n";
		html += "}\n";
		html += "});\n";

		// air quality chart
		html += "new Chart(document.getElementById('" + airQualityCanvasId + "'), {\n";
		html += "type: 'line',\n";
		html += "data: {\n";
		html += "labels: " + labelsVar + ",\n";
		html += "datasets: [\n";
		html += "{ label: 'AQI', data: " + aqiVar + ", borderColor: '#c0392b', backgroundColor: 'rgba(192, 57, 43, 0.15)', tension: 0.35, fill: false, pointRadius: 4, pointHoverRadius: 6, yAxisID: 'yAQI' },\n";
		html += "{ label: 'eCO2 (ppm)', data: " + eco2Var + ", borderColor: '#8e44ad', backgroundColor: 'rgba(142, 68, 173, 0.15)', tension: 0.35, fill: false, pointRadius: 4, pointHoverRadius: 6, yAxisID: 'yECO2' },\n";
		html += "{ label: 'TVOC', data: " + tvocVar + ", borderColor: '#27ae60', backgroundColor: 'rgba(39, 174, 96, 0.15)', tension: 0.35, fill: false, pointRadius: 4, pointHoverRadius: 6, yAxisID: 'yTVOC' }\n";
		html += "]\n";
		html += "},\n";
		html += "options: {\n";
		html += "responsive: true,\n";
		html += "maintainAspectRatio: false,\n";
		html += "interaction: { mode: 'index', intersect: false },\n";
		html += "plugins: { legend: { display: true } },\n";
		html += "scales: {\n";
		html += "x: { title: { display: true, text: 'Time' } },\n";
		html += "yAQI: { type: 'linear', position: 'left', title: { display: true, text: 'AQI' }, min: 1, max: 5 },\n";
		html += "yECO2: { type: 'linear', position: 'right', title: { display: true, text: 'eCO2 (ppm)' }, suggestedMin: 400, suggestedMax: 1200, grid: { drawOnChartArea: false } },\n";
		html += "yTVOC: { type: 'linear', position: 'right', offset: true, title: { display: true, text: 'TVOC' }, suggestedMin: 0, suggestedMax: 500, grid: { drawOnChartArea: false } }\n";
		html += "}\n";
		html += "}\n";
		html += "});\n";
		html += "}\n";
	}

	html += R"rawliteral(
	</script>
</body>
</html>
)rawliteral";

	server.send(200, "text/html", html);
}
