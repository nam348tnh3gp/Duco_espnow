#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <HTTPClient.h>

// ================= CONFIG =================
#define DNS_PORT 53

const char* DEFAULT_AP_SSID = "ESP32_PRO";
const char* DEFAULT_AP_PASS = "12345678";

IPAddress AP_IP(192,168,4,1);
IPAddress AP_GATEWAY(192,168,4,1);
IPAddress AP_SUBNET(255,255,255,0);

// ================= GLOBAL =================
DNSServer dns;
WebServer server(80);
Preferences prefs;

String sta_ssid, sta_pass;
String ap_ssid, ap_pass;

bool internetOK = false;

unsigned long uptimeStart = 0;

// ========== STATE ==========
enum WiFiState {
  WIFI_IDLE,
  WIFI_CONNECTING,
  WIFI_CONNECTED,
  WIFI_FAILED
};

WiFiState wifiState = WIFI_IDLE;

unsigned long lastAttempt = 0;
unsigned long lastInternetCheck = 0;

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  uptimeStart = millis();

  prefs.begin("wifi", false);

  sta_ssid = prefs.getString("sta_ssid", "");
  sta_pass = prefs.getString("sta_pass", "");
  ap_ssid  = prefs.getString("ap_ssid", DEFAULT_AP_SSID);
  ap_pass  = prefs.getString("ap_pass", DEFAULT_AP_PASS);

  setupAP();
  setupDNS();
  setupWeb();
  setupMDNS();

  if (sta_ssid.length()) startConnect();
}

// ================= LOOP =================
void loop() {
  dns.processNextRequest();
  server.handleClient();

  handleWiFiState();
  checkInternetLoop();
}

// ================= AP =================
void setupAP() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
  WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str());
}

// ================= DNS =================
void setupDNS() {
  dns.start(DNS_PORT, "*", AP_IP);
}

// ================= MDNS =================
void setupMDNS() {
  if (MDNS.begin("esp32")) {
    Serial.println("mDNS: http://esp32.local");
  }
}

// ================= WIFI =================
void startConnect() {
  WiFi.begin(sta_ssid.c_str(), sta_pass.c_str());
  wifiState = WIFI_CONNECTING;
  lastAttempt = millis();
}

void handleWiFiState() {

  switch (wifiState) {

    case WIFI_CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        wifiState = WIFI_CONNECTED;
      }
      else if (millis() - lastAttempt > 15000) {
        wifiState = WIFI_FAILED;
      }
      break;

    case WIFI_FAILED:
      if (millis() - lastAttempt > 10000) {
        startConnect();
      }
      break;

    case WIFI_CONNECTED:
      if (WiFi.status() != WL_CONNECTED) {
        wifiState = WIFI_FAILED;
        lastAttempt = millis();
      }
      break;

    default: break;
  }
}

// ================= INTERNET =================
bool checkInternet() {
  HTTPClient http;
  http.setTimeout(3000);
  http.begin("http://clients3.google.com/generate_204");

  int code = http.GET();
  http.end();

  return (code == 204);
}

void checkInternetLoop() {
  if (wifiState != WIFI_CONNECTED) return;

  if (millis() - lastInternetCheck > 30000) {
    lastInternetCheck = millis();

    bool now = checkInternet();
    if (now != internetOK) {
      internetOK = now;
    }
  }
}

// ================= WEB =================
void setupWeb() {

  server.on("/", handleRoot);
  server.on("/scan", handleScan);
  server.on("/save-sta", HTTP_POST, handleSaveSTA);
  server.on("/save-ap", HTTP_POST, handleSaveAP);
  server.on("/reset", handleReset);
  server.on("/reboot", handleReboot);
  server.on("/status", handleStatus);

  server.onNotFound([](){
    server.sendHeader("Location", "http://192.168.4.1", true);
    server.send(302, "text/plain", "");
  });

  server.begin();
}

// ================= ROOT UI =================
void handleRoot() {

  String html;
  html.reserve(5000);

  unsigned long uptime = (millis() - uptimeStart) / 1000;

  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";

  html += "<style>";
  html += "body{font-family:sans-serif;background:linear-gradient(135deg,#667eea,#764ba2);color:white;padding:20px;}";
  html += ".card{background:white;color:black;padding:15px;margin:10px 0;border-radius:12px;}";
  html += "h1{text-align:center;}";
  html += "button{padding:10px;background:#667eea;color:white;border:none;border-radius:6px;}";
  html += "input{width:100%;padding:8px;margin:5px 0;border-radius:6px;border:1px solid #ccc;}";
  html += ".badge{padding:5px 10px;border-radius:10px;font-size:12px;}";
  html += ".ok{background:#28a745;color:white;}";
  html += ".bad{background:#dc3545;color:white;}";
  html += "</style>";

  html += "</head><body>";

  html += "<h1>📡 ESP32 Router PRO</h1>";

  html += "<div class='card'><h3>Status</h3>";
  html += "WiFi: ";
  html += (wifiState == WIFI_CONNECTED) ? "<span class='badge ok'>Connected</span>" : "<span class='badge bad'>Disconnected</span>";
  html += "<br>Internet: ";
  html += internetOK ? "<span class='badge ok'>OK</span>" : "<span class='badge bad'>No</span>";
  html += "<br>AP: " + ap_ssid;
  html += "<br>STA: " + sta_ssid;
  html += "<br>Clients: " + String(WiFi.softAPgetStationNum());
  html += "<br>Uptime: " + String(uptime) + "s";
  html += "</div>";

  html += "<div class='card'><h3>Connect WiFi</h3>";
  html += "<form method='POST' action='/save-sta'>";
  html += "SSID:<input id='ssid' name='ssid'><br>";
  html += "PASS:<input name='pass'><br>";
  html += "<button>Save</button></form></div>";

  html += "<div class='card'><h3>Access Point</h3>";
  html += "<form method='POST' action='/save-ap'>";
  html += "SSID:<input name='ssid' value='" + ap_ssid + "'><br>";
  html += "PASS:<input name='pass'><br>";
  html += "<button>Save</button></form></div>";

  html += "<div class='card'>";
  html += "<a href='/scan'><button>Scan WiFi</button></a> ";
  html += "<a href='/reset'><button>Reset</button></a> ";
  html += "<a href='/reboot'><button>Reboot</button></a>";
  html += "</div>";

  html += "</body></html>";

  server.send(200,"text/html",html);
}

// ================= SAVE STA =================
void handleSaveSTA() {

  sta_ssid = server.arg("ssid");
  sta_pass = server.arg("pass");

  sta_ssid.replace("+"," ");

  prefs.putString("sta_ssid", sta_ssid);
  prefs.putString("sta_pass", sta_pass);

  startConnect();

  server.send(200,"text/html",
    "<h1>✅ Saved!</h1><p>Connecting...</p><a href='/'>Back</a>");
}

// ================= SAVE AP =================
void handleSaveAP() {

  ap_ssid = server.arg("ssid");
  ap_pass = server.arg("pass");

  ap_ssid.replace("+"," ");

  prefs.putString("ap_ssid", ap_ssid);
  prefs.putString("ap_pass", ap_pass);

  WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str());

  server.send(200,"text/html",
    "<h1>✅ AP Saved!</h1><a href='/'>Back</a>");
}

// ================= SCAN =================
void handleScan() {

  String html;
  html.reserve(4000);

  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<style>body{font-family:sans-serif;padding:20px;} li{cursor:pointer;margin:5px 0;}</style>";

  html += "<script>";
  html += "function pick(ssid){navigator.clipboard.writeText(ssid);alert('Copied: '+ssid);}";
  html += "</script>";

  html += "</head><body>";

  html += "<h2>📡 WiFi Networks</h2><ul>";

  int n = WiFi.scanNetworks();

  for(int i=0;i<n;i++){
    html += "<li onclick=\"pick('" + WiFi.SSID(i) + "')\">";
    html += WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + " dBm)";
    html += "</li>";
  }

  html += "</ul><a href='/'>← Back</a></body></html>";

  server.send(200,"text/html",html);
}

// ================= RESET =================
void handleReset() {
  prefs.clear();
  server.send(200,"text/html","Reset done");
  delay(1000);
  ESP.restart();
}

// ================= REBOOT =================
void handleReboot() {
  server.send(200,"text/html","Rebooting...");
  delay(1000);
  ESP.restart();
}

// ================= STATUS API =================
void handleStatus() {
  String json = "{";
  json += "\"wifi\":\"";
  json += (wifiState == WIFI_CONNECTED) ? "connected" : "disconnected";
  json += "\",\"internet\":";
  json += internetOK ? "true":"false";
  json += "}";

  server.send(200,"application/json",json);
}
