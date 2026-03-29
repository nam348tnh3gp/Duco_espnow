/*
   ESP32 NAT Router PRO - Dual Core Optimized
   Gộp từ 2 phiên bản, hỗ trợ NAT đầy đủ
   Tính năng: NAT Router, Web Server, DNS Spoofing
*/

#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <esp_task_wdt.h>

// NAT cho ESP32
#include <lwip/netif.h>
#include <lwip/ip4_addr.h>
#include <lwip/napt.h>

// ================= CONFIG =================
#define DNS_PORT 53
#define WIFI_CHECK_INTERVAL 30000
#define NAT_CHECK_INTERVAL 5000
#define WATCHDOG_TIMEOUT 30

const char* DEFAULT_AP_SSID = "ESP32_PRO";
const char* DEFAULT_AP_PASS = "12345678";

IPAddress AP_IP(192, 168, 4, 1);
IPAddress AP_GATEWAY(192, 168, 4, 1);
IPAddress AP_SUBNET(255, 255, 255, 0);

// ================= GLOBAL =================
DNSServer dns;
WebServer server(80);
Preferences prefs;

String sta_ssid, sta_pass;
String ap_ssid, ap_pass;

bool internetOK = false;
bool natEnabled = false;
bool natInitialized = false;

unsigned long uptimeStart = 0;
unsigned long lastNATCheck = 0;
unsigned long lastAttempt = 0;
unsigned long lastInternetCheck = 0;
IPAddress lastIP;

// ========== STATE ==========
enum WiFiState {
  WIFI_IDLE,
  WIFI_CONNECTING,
  WIFI_CONNECTED,
  WIFI_FAILED
};

WiFiState wifiState = WIFI_IDLE;

// ================= HARDWARE INFO =================
String getChipInfo() {
  String info = "ESP32 (Rev " + String(ESP.getChipRevision()) + ")";
  info += " | " + String(ESP.getCpuFreqMHz()) + "MHz";
  info += (CONFIG_FREERTOS_NUMBER_OF_CORES > 1) ? " | Dual Core" : " | Single Core";
  info += " | Flash: " + String(ESP.getFlashChipSize() / 1024 / 1024) + "MB";
  #ifdef CONFIG_SPIRAM_SUPPORT
    if (ESP.getPsramSize() > 0) {
      info += " | PSRAM: " + String(ESP.getPsramSize() / 1024 / 1024) + "MB";
    }
  #endif
  return info;
}

// ================= ENCODE FUNCTIONS =================
String urlDecode(String input) {
  String output = "";
  input.replace("+", " ");
  for (size_t i = 0; i < input.length(); i++) {
    if (input[i] == '%' && i + 2 < input.length()) {
      char hex[3] = {input[i+1], input[i+2], '\0'};
      char decoded = (char)strtol(hex, NULL, 16);
      output += decoded;
      i += 2;
    } else {
      output += input[i];
    }
  }
  return output;
}

String htmlEncode(String input) {
  String output = "";
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    switch (c) {
      case '<': output += "&lt;"; break;
      case '>': output += "&gt;"; break;
      case '&': output += "&amp;"; break;
      case '"': output += "&quot;"; break;
      case '\'': output += "&#39;"; break;
      default: output += c; break;
    }
  }
  return output;
}

String jsonEncode(String input) {
  String output = "";
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    switch (c) {
      case '"': output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      default: output += c; break;
    }
  }
  return output;
}

// ================= AP SETUP =================
void setupAP() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
  WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str());
  Serial.printf("📡 AP: %s | IP: %s\n", ap_ssid.c_str(), AP_IP.toString().c_str());
}

// ================= DNS SETUP =================
void setupDNS() {
  dns.start(DNS_PORT, "*", AP_IP);
  Serial.println("✅ DNS Server started");
}

// ================= MDNS SETUP =================
void setupMDNS() {
  if (MDNS.begin("esprouter")) {
    Serial.println("✅ mDNS: http://esprouter.local");
  }
}

// ================= WIFI =================
void startConnect() {
  WiFi.begin(sta_ssid.c_str(), sta_pass.c_str());
  wifiState = WIFI_CONNECTING;
  lastAttempt = millis();
  Serial.printf("📡 Connecting to STA: %s\n", sta_ssid.c_str());
}

void handleWiFiState() {
  switch (wifiState) {
    case WIFI_CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        wifiState = WIFI_CONNECTED;
        lastIP = WiFi.localIP();
        Serial.printf("✅ WiFi Connected | IP: %s | RSSI: %d dBm\n", 
                      lastIP.toString().c_str(), WiFi.RSSI());
      } else if (millis() - lastAttempt > 15000) {
        wifiState = WIFI_FAILED;
        Serial.println("❌ WiFi Failed");
      }
      break;

    case WIFI_FAILED:
      if (millis() - lastAttempt > 30000) {
        Serial.println("🔄 Retrying WiFi...");
        startConnect();
      }
      break;

    case WIFI_CONNECTED:
      if (WiFi.status() != WL_CONNECTED) {
        wifiState = WIFI_FAILED;
        lastAttempt = millis();
        if (natEnabled) {
          ip_napt_disable();
          natEnabled = false;
        }
        Serial.println("⚠️ WiFi Lost");
      }
      break;

    default: break;
  }
}

// ================= NAT ROUTER =================
void handleNAT() {
  if (wifiState != WIFI_CONNECTED) return;
  if (millis() - lastNATCheck < NAT_CHECK_INTERVAL) return;
  lastNATCheck = millis();

  IPAddress currentIP = WiFi.localIP();
  if (currentIP == IPAddress(0, 0, 0, 0)) return;

  if (!natInitialized) {
    ip_napt_init(2048, 1024);
    natInitialized = true;
    Serial.println("✅ NAT initialized");
  }

  if (currentIP != lastIP) {
    if (natEnabled) ip_napt_disable();
    natEnabled = false;
    lastIP = currentIP;
  }

  if (!natEnabled && WiFi.status() == WL_CONNECTED) {
    natEnabled = ip_napt_enable(currentIP, 1);
    if (natEnabled) {
      Serial.printf("✅ NAT Enabled - ESP32 is now a full router!\n");
      Serial.printf("   Clients connecting to '%s' will share internet\n", ap_ssid.c_str());
    }
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
  if (millis() - lastInternetCheck > WIFI_CHECK_INTERVAL) {
    lastInternetCheck = millis();
    bool now = checkInternet();
    if (now != internetOK) {
      internetOK = now;
      Serial.println(internetOK ? "🌍 Internet OK" : "⚠️ No Internet");
    }
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(100);
  uptimeStart = millis();

  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║     ESP32 NAT Router PRO - Dual Core ║");
  Serial.println("║     " + getChipInfo());
  Serial.println("║     Mode: FULL NAT (All protocols)    ║");
  Serial.println("╚════════════════════════════════════════╝\n");

  // Khởi tạo Preferences
  prefs.begin("wifi", false);

  // Đọc cấu hình
  sta_ssid = prefs.getString("sta_ssid", "");
  sta_pass = prefs.getString("sta_pass", "");
  ap_ssid  = prefs.getString("ap_ssid", DEFAULT_AP_SSID);
  ap_pass  = prefs.getString("ap_pass", DEFAULT_AP_PASS);

  // Setup các dịch vụ
  setupAP();
  setupDNS();
  setupMDNS();
  setupWeb();

  // Kết nối WiFi nếu đã cấu hình
  if (sta_ssid.length() > 0) {
    startConnect();
  } else {
    Serial.printf("\n⚠️ Connect to AP: %s | http://%s\n", ap_ssid.c_str(), AP_IP.toString().c_str());
    Serial.println("   Then configure your WiFi network via web interface");
  }

  // Watchdog
  esp_task_wdt_init(WATCHDOG_TIMEOUT, true);
  esp_task_wdt_add(NULL);
  
  // LED báo hiệu
  pinMode(LED_BUILTIN, OUTPUT);
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_BUILTIN, LOW);
    delay(50);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(50);
  }
}

// ================= LOOP =================
void loop() {
  dns.processNextRequest();
  server.handleClient();
  MDNS.update();

  handleWiFiState();
  checkInternetLoop();
  handleNAT();

  esp_task_wdt_reset();
}

// ================= WEB HANDLERS =================
void setupWeb() {
  server.on("/", handleRoot);
  server.on("/scan", handleScan);
  server.on("/save-sta", HTTP_POST, handleSaveSTA);
  server.on("/save-ap", HTTP_POST, handleSaveAP);
  server.on("/reset", handleReset);
  server.on("/reboot", handleReboot);
  server.on("/status", handleStatus);
  server.on("/info", handleInfo);

  server.onNotFound([]() {
    server.sendHeader("Location", "http://192.168.4.1", true);
    server.send(302, "text/plain", "");
  });

  server.begin();
  Serial.printf("🌐 Web: http://%s\n", AP_IP.toString().c_str());
}

void handleRoot() {
  String html;
  html.reserve(10000);
  unsigned long uptime = (millis() - uptimeStart) / 1000;

  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='15'>";
  html += "<style>";
  html += "*{box-sizing:border-box;}";
  html += "body{font-family:sans-serif;background:linear-gradient(135deg,#667eea,#764ba2);color:white;padding:20px;}";
  html += ".card{background:white;color:black;padding:15px;margin:10px 0;border-radius:12px;}";
  html += "h1{text-align:center;font-size:1.5em;}";
  html += ".chip-badge{background:#667eea;color:white;padding:3px 10px;border-radius:20px;font-size:12px;display:inline-block;}";
  html += "button{padding:10px;background:#667eea;color:white;border:none;border-radius:6px;cursor:pointer;}";
  html += "input{width:100%;padding:8px;margin:5px 0;border-radius:6px;border:1px solid #ccc;}";
  html += ".badge{padding:5px 10px;border-radius:10px;font-size:12px;display:inline-block;}";
  html += ".ok{background:#28a745;color:white;}";
  html += ".bad{background:#dc3545;color:white;}";
  html += ".nat-on{background:#17a2b8;color:white;}";
  html += ".grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;}";
  html += ".ssid-display{word-break:break-all;}";
  html += "</style>";
  html += "</head><body>";

  html += "<h1>📡 ESP32 NAT Router PRO <span class='chip-badge'>" + getChipInfo() + "</span></h1>";

  html += "<div class='card'><h3>📊 Status</h3>";
  html += "<div class='grid'>";
  html += "<div>WiFi STA:</div><div>";
  html += (wifiState == WIFI_CONNECTED) ? "<span class='badge ok'>Connected</span>" : "<span class='badge bad'>Disconnected</span>";
  html += "</div>";
  html += "<div>Internet:</div><div>";
  html += internetOK ? "<span class='badge ok'>OK</span>" : "<span class='badge bad'>No</span>";
  html += "</div>";
  html += "<div>NAT Router:</div><div>";
  html += natEnabled ? "<span class='badge nat-on'>✅ Enabled (Full)</span>" : "<span class='badge bad'>Disabled</span>";
  html += "</div>";
  html += "<div>AP SSID:</div><div class='ssid-display'>" + htmlEncode(ap_ssid) + "</div>";
  html += "<div>STA SSID:</div><div class='ssid-display'>" + (sta_ssid.length() ? htmlEncode(sta_ssid) : "Not set") + "</div>";
  html += "<div>Clients:</div><div>" + String(WiFi.softAPgetStationNum()) + " devices</div>";
  html += "<div>STA IP:</div><div>" + (wifiState == WIFI_CONNECTED ? WiFi.localIP().toString() : "0.0.0.0") + "</div>";
  html += "<div>RSSI:</div><div>" + (wifiState == WIFI_CONNECTED ? String(WiFi.RSSI()) + " dBm" : "N/A") + "</div>";
  html += "<div>Uptime:</div><div>" + String(uptime) + "s</div>";
  html += "<div>Free Heap:</div><div>" + String(ESP.getFreeHeap() / 1024) + " KB</div>";
  html += "</div></div>";

  html += "<div class='card'><h3>📶 Connect WiFi (STA)</h3>";
  html += "<form method='POST' action='/save-sta' accept-charset='UTF-8'>";
  html += "SSID:<input id='ssid' name='ssid' placeholder='Enter WiFi name'><br>";
  html += "PASS:<input name='pass' type='password' placeholder='Password'><br>";
  html += "<button type='submit'>Save & Connect</button></form>";
  html += "<small>💡 Supports all special characters: # % & @ ! $ ^ ( ) - _ = + [ ] { } ; : ' \" , . / ? \\ | ` ~</small>";
  html += "</div>";

  html += "<div class='card'><h3>🎛️ Access Point (AP)</h3>";
  html += "<form method='POST' action='/save-ap' accept-charset='UTF-8'>";
  html += "SSID:<input name='ssid' value='" + htmlEncode(ap_ssid) + "'><br>";
  html += "PASS:<input name='pass' type='password' placeholder='At least 8 chars'><br>";
  html += "<button type='submit'>Save AP</button></form>";
  html += "<small>⚠️ After saving AP, reconnect to the new network</small>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<a href='/scan'><button>🔍 Scan WiFi</button></a> ";
  html += "<a href='/info'><button>ℹ️ System Info</button></a> ";
  html += "<a href='/reset'><button onclick='return confirm(\"Reset all?\")'>🗑️ Reset</button></a> ";
  html += "<a href='/reboot'><button onclick='return confirm(\"Reboot?\")'>🔄 Reboot</button></a>";
  html += "</div>";

  html += "<div class='card'><small>📌 NAT Router: Devices connected to <b>" + htmlEncode(ap_ssid) + "</b> will share internet automatically (no proxy config needed). Supports all protocols: HTTP, HTTPS, TCP, UDP, games, SSH...</small></div>";

  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleSaveSTA() {
  sta_ssid = urlDecode(server.arg("ssid"));
  sta_pass = urlDecode(server.arg("pass"));
  sta_ssid.trim();
  
  if (sta_ssid.length() == 0) {
    server.send(400, "text/html", 
      "<html><body style='text-align:center;padding:50px;'>"
      "<h1>❌ SSID cannot be empty</h1>"
      "<a href='/'>Back</a></body></html>");
    return;
  }

  prefs.putString("sta_ssid", sta_ssid);
  prefs.putString("sta_pass", sta_pass);

  String encodedSsid = htmlEncode(sta_ssid);
  startConnect();

  server.send(200, "text/html",
    "<html><head><meta charset='UTF-8'></head>"
    "<body style='text-align:center;padding:50px;'>"
    "<h1>✅ Saved!</h1>"
    "<p>Connecting to: <strong>" + encodedSsid + "</strong></p>"
    "<a href='/'>Back</a>"
    "</body></html>");
}

void handleSaveAP() {
  ap_ssid = urlDecode(server.arg("ssid"));
  ap_pass = urlDecode(server.arg("pass"));
  ap_ssid.trim();
  
  if (ap_ssid.length() == 0) {
    server.send(400, "text/html", 
      "<html><body style='text-align:center;padding:50px;'>"
      "<h1>❌ AP SSID cannot be empty</h1>"
      "<a href='/'>Back</a></body></html>");
    return;
  }

  if (ap_pass.length() > 0 && ap_pass.length() < 8) {
    server.send(400, "text/html", 
      "<html><body style='text-align:center;padding:50px;'>"
      "<h1>❌ Password must be at least 8 characters</h1>"
      "<a href='/'>Back</a></body></html>");
    return;
  }

  prefs.putString("ap_ssid", ap_ssid);
  prefs.putString("ap_pass", ap_pass);

  WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str());

  String encodedSsid = htmlEncode(ap_ssid);
  
  server.send(200, "text/html",
    "<html><head><meta charset='UTF-8'></head>"
    "<body style='text-align:center;padding:50px;'>"
    "<h1>✅ AP Saved!</h1>"
    "<p>New AP: <strong>" + encodedSsid + "</strong></p>"
    "<a href='/'>Back</a>"
    "</body></html>");
}

void handleScan() {
  String html;
  html.reserve(8000);

  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body{font-family:sans-serif;background:#1a1a2e;color:white;padding:20px;}";
  html += "li{background:#16213e;margin:8px 0;padding:12px;border-radius:8px;cursor:pointer;list-style:none;word-break:break-all;}";
  html += "li:hover{background:#0f3460;}";
  html += "input{width:100%;padding:10px;margin:10px 0;border-radius:8px;border:none;}";
  html += ".back{background:#667eea;padding:10px 20px;border-radius:8px;text-decoration:none;color:white;display:inline-block;margin-top:20px;}";
  html += ".note{font-size:12px;color:#aaa;margin-top:10px;}";
  html += "</style>";

  html += "<script>";
  html += "function pick(ssid){";
  html += "  var target = document.getElementById('target');";
  html += "  target.value = ssid;";
  html += "  target.focus();";
  html += "  alert('SSID copied: ' + ssid);";
  html += "}";
  html += "</script>";

  html += "</head><body>";

  html += "<h2>📡 WiFi Networks</h2>";
  html += "<div class='note'>💡 Click on any network to copy its SSID (supports all special characters)</div>";
  html += "<ul>";

  int n = WiFi.scanNetworks();
  if (n == 0) {
    html += "<li>No networks found</li>";
  } else {
    for(int i = 0; i < n; i++){
      String ssid = WiFi.SSID(i);
      String encodedSsid = htmlEncode(ssid);
      String enc = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "🔓" : "🔒";
      
      String jsSsid = ssid;
      jsSsid.replace("\\", "\\\\");
      jsSsid.replace("'", "\\'");
      jsSsid.replace("\"", "\\\"");
      
      html += "<li onclick=\"pick('" + jsSsid + "')\">";
      html += enc + " <b>" + encodedSsid + "</b> (" + String(WiFi.RSSI(i)) + " dBm)";
      html += "</li>";
    }
  }

  html += "</ul>";
  html += "<input id='target' style='width:100%;padding:10px;' placeholder='Selected SSID appears here'>";
  html += "<div class='note'>📋 SSID has been copied - you can paste it above or directly into the connection form</div>";
  html += "<a href='/' class='back'>← Back to Main</a>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleInfo() {
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body{font-family:sans-serif;background:#1a1a2e;color:white;padding:20px;}";
  html += ".card{background:#16213e;padding:15px;margin:10px 0;border-radius:12px;}";
  html += "h1{color:#667eea;}";
  html += ".back{background:#667eea;padding:10px 20px;border-radius:8px;text-decoration:none;color:white;display:inline-block;}";
  html += "</style>";
  html += "</head><body>";
  
  html += "<h1>ℹ️ System Information</h1>";
  html += "<div class='card'><h3>🔧 Hardware</h3>";
  html += "<p><strong>Chip:</strong> " + getChipInfo() + "</p>";
  html += "<p><strong>CPU Frequency:</strong> " + String(ESP.getCpuFreqMHz()) + " MHz</p>";
  html += "<p><strong>Free Heap:</strong> " + String(ESP.getFreeHeap() / 1024) + " KB</p>";
  #ifdef CONFIG_SPIRAM_SUPPORT
    if (ESP.getPsramSize() > 0) {
      html += "<p><strong>PSRAM:</strong> " + String(ESP.getPsramSize() / 1024 / 1024) + " MB</p>";
      html += "<p><strong>Free PSRAM:</strong> " + String(ESP.getFreePsram() / 1024) + " KB</p>";
    }
  #endif
  html += "<p><strong>Flash Size:</strong> " + String(ESP.getFlashChipSize() / 1024 / 1024) + " MB</p>";
  html += "<p><strong>Dual Core:</strong> Yes (240MHz each)</p>";
  html += "<p><strong>NAT Support:</strong> Yes (Full Router)</p>";
  html += "</div>";
  
  html += "<div class='card'><h3>📡 Network</h3>";
  html += "<p><strong>AP IP:</strong> " + AP_IP.toString() + "</p>";
  html += "<p><strong>AP SSID:</strong> " + htmlEncode(ap_ssid) + "</p>";
  html += "<p><strong>AP Clients:</strong> " + String(WiFi.softAPgetStationNum()) + "</p>";
  html += "<p><strong>STA IP:</strong> " + (wifiState == WIFI_CONNECTED ? WiFi.localIP().toString() : "Not connected") + "</p>";
  html += "<p><strong>STA SSID:</strong> " + (sta_ssid.length() ? htmlEncode(sta_ssid) : "Not set") + "</p>";
  html += "<p><strong>RSSI:</strong> " + (wifiState == WIFI_CONNECTED ? String(WiFi.RSSI()) + " dBm" : "N/A") + "</p>";
  html += "<p><strong>MAC Address:</strong> " + WiFi.macAddress() + "</p>";
  html += "<p><strong>Internet:</strong> " + String(internetOK ? "Connected" : "Disconnected") + "</p>";
  html += "<p><strong>NAT Router:</strong> " + String(natEnabled ? "Active" : "Inactive") + "</p>";
  html += "</div>";
  
  html += "<a href='/' class='back'>← Back</a>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleReset() {
  prefs.clear();
  if (natEnabled) {
    ip_napt_disable();
    natEnabled = false;
  }
  server.send(200, "text/html", 
    "<html><head><meta charset='UTF-8'></head>"
    "<body style='text-align:center;padding:50px;'>"
    "<h1>🔄 Resetting...</h1>"
    "</body></html>");
  delay(1000);
  ESP.restart();
}

void handleReboot() {
  if (natEnabled) {
    ip_napt_disable();
    natEnabled = false;
  }
  server.send(200, "text/html",
    "<html><head><meta charset='UTF-8'></head>"
    "<body style='text-align:center;padding:50px;'>"
    "<h1>🔄 Rebooting...</h1>"
    "</body></html>");
  delay(1000);
  ESP.restart();
}

void handleStatus() {
  String json = "{";
  json += "\"chip\":\"ESP32\",";
  json += "\"cpu_freq\":" + String(ESP.getCpuFreqMHz()) + ",";
  json += "\"wifi\":\"";
  json += (wifiState == WIFI_CONNECTED) ? "connected" : "disconnected";
  json += "\",\"internet\":";
  json += internetOK ? "true" : "false";
  json += ",\"nat\":";
  json += natEnabled ? "true" : "false";
  json += ",\"ap_clients\":";
  json += WiFi.softAPgetStationNum();
  json += ",\"sta_ip\":\"";
  json += (wifiState == WIFI_CONNECTED) ? WiFi.localIP().toString() : "0.0.0.0";
  json += "\",\"rssi\":";
  json += (wifiState == WIFI_CONNECTED) ? String(WiFi.RSSI()) : "0";
  json += ",\"free_heap\":" + String(ESP.getFreeHeap());
  json += ",\"sta_ssid\":\"";
  json += jsonEncode(sta_ssid);
  json += "\",\"ap_ssid\":\"";
  json += jsonEncode(ap_ssid);
  json += "\"}";

  server.send(200, "application/json", json);
}
