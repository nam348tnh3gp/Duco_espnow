#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>

// Cấu hình mặc định
const char* default_ap_ssid = "ESP32_AP";
const char* default_ap_password = "12345678";

// Cấu hình IP cố định cho AP
IPAddress ap_local_ip(192, 168, 4, 1);
IPAddress ap_gateway(192, 168, 4, 1);
IPAddress ap_subnet(255, 255, 255, 0);

// DNS và Web Server
const byte DNS_PORT = 53;
DNSServer dnsServer;
WebServer webServer(80);

// Biến
bool internetAvailable = false;
unsigned long previousMillis = 0;
const long interval = 30000;

// Preferences để lưu cấu hình
Preferences preferences;
String saved_ssid = "";
String saved_password = "";
String saved_ap_ssid = "";
String saved_ap_password = "";
bool stationConnected = false;
unsigned long lastConnectionAttempt = 0;
bool isConfigMode = true;

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n╔═══════════════════════════════════╗");
    Serial.println("║   ESP32 WiFi Router v4.0        ║");
    Serial.println("║   Full Configuration Support    ║");
    Serial.println("╚═══════════════════════════════════╝\n");
    
    // Khởi tạo Preferences
    preferences.begin("wifi-config", false);
    saved_ssid = preferences.getString("sta_ssid", "");
    saved_password = preferences.getString("sta_pass", "");
    saved_ap_ssid = preferences.getString("ap_ssid", default_ap_ssid);
    saved_ap_password = preferences.getString("ap_pass", default_ap_password);
    
    // 1. Khởi tạo AP với cấu hình đã lưu
    setupAccessPoint();
    
    // 2. Cấu hình DNS và Web Server
    setupDNSAndWebServer();
    
    // 3. Thử kết nối Station nếu có cấu hình
    if (saved_ssid.length() > 0) {
        Serial.println("📡 Found saved WiFi configuration");
        connectToStation();
    } else {
        Serial.println("📡 No saved configuration - Starting in config mode");
        isConfigMode = true;
    }
    
    // 4. Cấu hình mDNS
    setupMDNS();
    
    // 5. Hiển thị thông tin
    displayConnectionInfo();
}

void loop() {
    dnsServer.processNextRequest();
    webServer.handleClient();
    handleStationReconnect();
    handlePeriodicInternetCheck();
    checkStationConnectionTimeout();
}

// ========== CÁC HÀM CHÍNH ==========

void setupAccessPoint() {
    Serial.println("\n📱 Setting up Access Point...");
    
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(ap_local_ip, ap_gateway, ap_subnet);
    
    String ap_ssid = saved_ap_ssid.length() > 0 ? saved_ap_ssid : String(default_ap_ssid);
    String ap_pass = saved_ap_password.length() > 0 ? saved_ap_password : String(default_ap_password);
    
    bool apStarted = WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str(), 6, 0, 4);
    
    if (apStarted) {
        Serial.println("✅ AP started successfully!");
        Serial.printf("   🏠 SSID: %s\n", ap_ssid.c_str());
        Serial.printf("   🔑 Password: %s\n", ap_pass.c_str());
        Serial.printf("   🌐 IP Address: %s\n", WiFi.softAPIP().toString().c_str());
        Serial.printf("   📻 Channel: 6\n");
        Serial.printf("   👥 Max clients: 4\n");
    } else {
        Serial.println("❌ AP failed to start!");
    }
}

void connectToStation() {
    if (saved_ssid.length() == 0) {
        Serial.println("⚠️ No SSID configured");
        return;
    }
    
    Serial.println("\n📡 Connecting to Station...");
    Serial.printf("   SSID: %s\n", saved_ssid.c_str());
    
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(saved_ssid.c_str(), saved_password.c_str());
    
    lastConnectionAttempt = millis();
    int attempt = 0;
    
    while (WiFi.status() != WL_CONNECTED && attempt < 20) {
        delay(500);
        Serial.print(".");
        attempt++;
        
        if (attempt == 10) {
            Serial.println("\n   ⏳ Taking longer than expected...");
        }
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        stationConnected = true;
        isConfigMode = false;
        Serial.println("\n✅ Station connected successfully!");
        Serial.printf("   📍 IP Address: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("   🚪 Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
        Serial.printf("   📶 Signal: %d dBm\n", WiFi.RSSI());
        
        delay(1000);
        checkInternetConnection();
    } else {
        stationConnected = false;
        Serial.println("\n❌ Station connection failed!");
        Serial.println("   🔄 Keeping AP mode active for reconfiguration");
        
        WiFi.mode(WIFI_AP);
        String ap_ssid = saved_ap_ssid.length() > 0 ? saved_ap_ssid : String(default_ap_ssid);
        String ap_pass = saved_ap_password.length() > 0 ? saved_ap_password : String(default_ap_password);
        WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str(), 6, 0, 4);
    }
}

void checkStationConnectionTimeout() {
    if (!stationConnected && saved_ssid.length() > 0 && 
        millis() - lastConnectionAttempt > 30000) {
        
        Serial.println("\n⏰ Station connection timeout!");
        Serial.println("   🔄 Switching back to AP-only mode");
        
        WiFi.mode(WIFI_AP);
        String ap_ssid = saved_ap_ssid.length() > 0 ? saved_ap_ssid : String(default_ap_ssid);
        String ap_pass = saved_ap_password.length() > 0 ? saved_ap_password : String(default_ap_password);
        WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str(), 6, 0, 4);
        stationConnected = false;
        isConfigMode = true;
        
        displayConnectionInfo();
    }
}

void setupDNSAndWebServer() {
    Serial.println("\n🌐 Setting up DNS & Web Server...");
    
    dnsServer.start(DNS_PORT, "*", ap_local_ip);
    
    webServer.on("/", handleRoot);
    webServer.on("/status", handleStatusJSON);
    webServer.on("/scan", handleWiFiScan);
    webServer.on("/save-station", HTTP_POST, handleSaveStation);
    webServer.on("/save-ap", HTTP_POST, handleSaveAP);
    webServer.on("/reboot", handleReboot);
    webServer.on("/reset", handleResetConfig);
    webServer.onNotFound(handleNotFound);
    
    webServer.begin();
    Serial.println("✅ Web server started on port 80");
    Serial.printf("   📱 Captive portal: http://%s\n", ap_local_ip.toString().c_str());
}

void setupMDNS() {
    if (MDNS.begin("esp32")) {
        Serial.println("✅ mDNS responder started");
        Serial.println("   🌐 Access via: http://esp32.local");
    }
}

void displayConnectionInfo() {
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║        ESP32 IS READY!                ║");
    Serial.println("╠════════════════════════════════════════╣");
    
    String current_ap = saved_ap_ssid.length() > 0 ? saved_ap_ssid : String(default_ap_ssid);
    String current_ap_pass = saved_ap_password.length() > 0 ? saved_ap_password : String(default_ap_password);
    
    Serial.printf("║ AP:      %-30s║\n", current_ap.c_str());
    Serial.printf("║ Password:%-30s║\n", current_ap_pass.c_str());
    Serial.printf("║ IP:      %-30s║\n", ap_local_ip.toString().c_str());
    
    if (stationConnected && WiFi.status() == WL_CONNECTED) {
        Serial.printf("║ Station: %-30s║\n", saved_ssid.c_str());
        Serial.printf("║ IP:      %-30s║\n", WiFi.localIP().toString().c_str());
        Serial.printf("║ Internet: %-31s║\n", internetAvailable ? "✅ AVAILABLE" : "❌ UNAVAILABLE");
    } else {
        Serial.printf("║ Mode:    %-30s║\n", "CONFIGURATION MODE");
        Serial.printf("║ Status:  %-30s║\n", "Waiting for config");
    }
    
    Serial.printf("║ Clients: %d connected%-24s║\n", WiFi.softAPgetStationNum(), "");
    Serial.println("╚════════════════════════════════════════╝\n");
}

// ========== HÀM KIỂM TRA ==========

void checkInternetConnection() {
    Serial.println("\n🌍 Testing Internet Connection...");
    delay(2000);
    
    internetAvailable = checkInternet();
    
    if (internetAvailable) {
        Serial.println("✅ Internet connection: AVAILABLE");
    } else {
        Serial.println("❌ Internet connection: NOT AVAILABLE");
    }
}

bool checkInternet() {
    WiFiClient client;
    if (client.connect("8.8.8.8", 53)) {
        client.stop();
        return true;
    }
    return false;
}

void handleStationReconnect() {
    static unsigned long lastReconnect = 0;
    
    if (saved_ssid.length() > 0 && WiFi.status() != WL_CONNECTED && stationConnected) {
        if (millis() - lastReconnect > 10000) {
            Serial.println("\n⚠️ Station disconnected! Reconnecting...");
            WiFi.reconnect();
            lastReconnect = millis();
            stationConnected = false;
        }
    } else if (WiFi.status() == WL_CONNECTED && !stationConnected) {
        stationConnected = true;
        Serial.println("\n✅ Station reconnected!");
    }
}

void handlePeriodicInternetCheck() {
    unsigned long currentMillis = millis();
    
    if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;
        
        if (WiFi.status() == WL_CONNECTED) {
            bool currentInternet = checkInternet();
            if (currentInternet != internetAvailable) {
                internetAvailable = currentInternet;
                Serial.printf("\n🔄 Internet status: %s\n", 
                             internetAvailable ? "CONNECTED" : "DISCONNECTED");
            }
            
            Serial.printf("[%lus] 📊 Internet: %s | Signal: %d dBm | Clients: %d\n", 
                         millis() / 1000,
                         internetAvailable ? "✅ OK" : "❌ LOST",
                         WiFi.RSSI(),
                         WiFi.softAPgetStationNum());
        }
    }
}

// ========== WEB HANDLERS ==========

void handleRoot() {
    String current_ap = saved_ap_ssid.length() > 0 ? saved_ap_ssid : String(default_ap_ssid);
    String current_ap_pass = saved_ap_password.length() > 0 ? saved_ap_password : String(default_ap_password);
    
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>ESP32 WiFi Router</title>";
    html += "<style>";
    html += "*{margin:0;padding:0;box-sizing:border-box;}";
    html += "body{font-family:'Segoe UI',Arial;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;padding:20px;}";
    html += ".container{max-width:900px;margin:0 auto;}";
    html += ".card{background:white;border-radius:15px;padding:20px;margin-bottom:20px;box-shadow:0 10px 30px rgba(0,0,0,0.2);}";
    html += ".header{text-align:center;color:white;margin-bottom:20px;}";
    html += "h1{font-size:2em;margin-bottom:10px;}";
    html += "h2{color:#333;margin-bottom:15px;border-bottom:2px solid #667eea;padding-bottom:10px;}";
    html += ".status-badge{display:inline-block;padding:5px 15px;border-radius:20px;font-weight:bold;}";
    html += ".online{background:#d4edda;color:#155724;}";
    html += ".offline{background:#f8d7da;color:#721c24;}";
    html += ".config-mode{background:#fff3cd;color:#856404;}";
    html += "table{width:100%;border-collapse:collapse;}";
    html += "td,th{padding:12px;text-align:left;border-bottom:1px solid #ddd;}";
    html += "input, select{width:100%;padding:10px;margin:10px 0;border:1px solid #ddd;border-radius:5px;font-size:16px;}";
    html += ".button{display:inline-block;padding:10px 20px;margin:5px;background:#667eea;color:white;text-decoration:none;border-radius:5px;border:none;cursor:pointer;}";
    html += ".button-red{background:#dc3545;}";
    html += ".button-green{background:#28a745;}";
    html += ".footer{text-align:center;color:white;margin-top:20px;}";
    html += ".warning{background:#fff3cd;border-left:4px solid #ffc107;padding:10px;margin:10px 0;border-radius:5px;}";
    html += "small{color:#666;}";
    html += ".grid-2{display:grid;grid-template-columns:1fr 1fr;gap:20px;}";
    html += "@media (max-width:600px){.grid-2{grid-template-columns:1fr;}}";
    html += "</style></head><body>";
    
    html += "<div class='container'>";
    html += "<div class='header'>";
    html += "<h1>📡 ESP32 WiFi Router</h1>";
    html += "<p>Advanced Configuration Panel</p>";
    html += "</div>";
    
    // Status Card
    html += "<div class='card'>";
    html += "<h2>📊 System Status</h2>";
    
    if (!stationConnected || WiFi.status() != WL_CONNECTED) {
        html += "<div class='status-badge config-mode'>⚠️ CONFIGURATION MODE</div>";
        html += "<p>No WiFi connection configured or connection failed.</p>";
    } else {
        String statusClass = internetAvailable ? "online" : "offline";
        html += "<div class='status-badge " + statusClass + "'>";
        html += internetAvailable ? "✅ Internet Connected" : "❌ No Internet";
        html += "</div>";
        html += "<p>Connected to: <strong>" + saved_ssid + "</strong></p>";
    }
    html += "<p><strong>Connected Clients:</strong> " + String(WiFi.softAPgetStationNum()) + "</p>";
    html += "<p><strong>ESP32 IP:</strong> 192.168.4.1 | <strong>Uptime:</strong> " + String(millis() / 1000) + "s</p>";
    html += "</div>";
    
    html += "<div class='grid-2'>";
    
    // Station Configuration Card
    html += "<div class='card'>";
    html += "<h2>📶 Station WiFi</h2>";
    html += "<div class='warning'>";
    html += "💡 Connect ESP32 to your home/work WiFi network";
    html += "</div>";
    html += "<form action='/save-station' method='POST' accept-charset='UTF-8'>";
    html += "<p><strong>WiFi Name (SSID):</strong></p>";
    html += "<input type='text' name='ssid' placeholder='Enter WiFi name' value='" + saved_ssid + "' required>";
    html += "<p><strong>Password:</strong></p>";
    html += "<input type='password' name='password' placeholder='Enter password'>";
    html += "<button type='submit' class='button'>💾 Save Station Config</button>";
    html += "</form>";
    if (stationConnected) {
        html += "<p style='color:#28a745;margin-top:10px;'>✅ Currently connected to: " + saved_ssid + "</p>";
    }
    html += "</div>";
    
    // AP Configuration Card
    html += "<div class='card'>";
    html += "<h2>📱 Access Point</h2>";
    html += "<div class='warning'>";
    html += "💡 Configure the WiFi network that devices connect to ESP32";
    html += "</div>";
    html += "<form action='/save-ap' method='POST' accept-charset='UTF-8'>";
    html += "<p><strong>AP SSID (Network Name):</strong></p>";
    html += "<input type='text' name='ap_ssid' placeholder='AP SSID' value='" + current_ap + "' required>";
    html += "<p><strong>AP Password:</strong></p>";
    html += "<input type='text' name='ap_password' placeholder='AP Password' value='" + current_ap_pass + "'>";
    html += "<p><small>Leave password empty for open network (not recommended)</small></p>";
    html += "<button type='submit' class='button'>💾 Save AP Config</button>";
    html += "</form>";
    html += "<p><strong>Current AP:</strong> " + current_ap + "</p>";
    html += "</div>";
    
    html += "</div>";
    
    // Action Buttons Card
    html += "<div class='card'>";
    html += "<h2>⚙️ System Actions</h2>";
    html += "<div style='display:flex;gap:10px;flex-wrap:wrap;'>";
    html += "<a href='/scan' class='button'>🔍 Scan Networks</a>";
    html += "<a href='/reboot' class='button'>🔄 Reboot ESP32</a>";
    html += "<a href='/reset' class='button button-red' onclick='return confirm(\"Reset all settings?\")'>🗑️ Factory Reset</a>";
    html += "</div>";
    html += "</div>";
    
    html += "<div class='footer'>";
    html += "<p>ESP32 WiFi Router v4.0 | Supports Unicode & Spaces | " + String(__DATE__) + "</p>";
    html += "</div></div></body></html>";
    
    webServer.send(200, "text/html", html);
}

void handleSaveStation() {
    if (webServer.method() == HTTP_POST) {
        String new_ssid = webServer.arg("ssid");
        String new_password = webServer.arg("password");
        
        new_ssid.replace("+", " ");
        
        if (new_ssid.length() > 0) {
            Serial.println("\n📝 Saving Station Configuration:");
            Serial.printf("   SSID: %s\n", new_ssid.c_str());
            Serial.printf("   Password length: %d\n", new_password.length());
            
            preferences.putString("sta_ssid", new_ssid);
            preferences.putString("sta_pass", new_password);
            saved_ssid = new_ssid;
            saved_password = new_password;
            
            String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
            html += "<meta http-equiv='refresh' content='5;url=/'>";
            html += "<title>Saved</title>";
            html += "<style>body{text-align:center;margin-top:50px;background:#667eea;color:white;font-family:Arial;}</style>";
            html += "</head><body>";
            html += "<h1>✅ Station Configuration Saved!</h1>";
            html += "<p>Network: <strong>" + new_ssid + "</strong></p>";
            html += "<p>ESP32 is now connecting...</p>";
            html += "<p>Redirecting...</p>";
            html += "</body></html>";
            webServer.send(200, "text/html", html);
            
            delay(500);
            connectToStation();
        } else {
            webServer.send(400, "text/plain", "SSID is required!");
        }
    }
}

void handleSaveAP() {
    if (webServer.method() == HTTP_POST) {
        String new_ap_ssid = webServer.arg("ap_ssid");
        String new_ap_password = webServer.arg("ap_password");
        
        new_ap_ssid.replace("+", " ");
        
        if (new_ap_ssid.length() > 0) {
            Serial.println("\n📝 Saving AP Configuration:");
            Serial.printf("   AP SSID: %s\n", new_ap_ssid.c_str());
            Serial.printf("   AP Password: %s\n", new_ap_password.c_str());
            
            preferences.putString("ap_ssid", new_ap_ssid);
            preferences.putString("ap_pass", new_ap_password);
            saved_ap_ssid = new_ap_ssid;
            saved_ap_password = new_ap_password;
            
            // Restart AP with new config
            WiFi.mode(WIFI_AP);
            if (new_ap_password.length() > 0) {
                WiFi.softAP(new_ap_ssid.c_str(), new_ap_password.c_str(), 6, 0, 4);
            } else {
                WiFi.softAP(new_ap_ssid.c_str(), NULL, 6, 0, 4);
            }
            
            String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
            html += "<meta http-equiv='refresh' content='3;url=/'>";
            html += "<title>Saved</title>";
            html += "<style>body{text-align:center;margin-top:50px;background:#667eea;color:white;font-family:Arial;}</style>";
            html += "</head><body>";
            html += "<h1>✅ AP Configuration Saved!</h1>";
            html += "<p>New AP SSID: <strong>" + new_ap_ssid + "</strong></p>";
            html += "<p>You may need to reconnect to the new WiFi network.</p>";
            html += "<p>Redirecting...</p>";
            html += "</body></html>";
            webServer.send(200, "text/html", html);
        } else {
            webServer.send(400, "text/plain", "AP SSID is required!");
        }
    }
}

void handleWiFiScan() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta http-equiv='refresh' content='15'>";
    html += "<title>WiFi Scan</title>";
    html += "<style>";
    html += "body{font-family:Arial;margin:20px;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);}";
    html += ".container{max-width:600px;margin:0 auto;background:white;padding:20px;border-radius:10px;}";
    html += "table{width:100%;border-collapse:collapse;}";
    html += "td,th{padding:8px;border-bottom:1px solid #ddd;}";
    html += "tr:hover{background:#f5f5f5;cursor:pointer;}";
    html += ".button{display:inline-block;padding:10px 20px;background:#667eea;color:white;text-decoration:none;border-radius:5px;}";
    html += ".ssid{font-weight:bold;}";
    html += "</style>";
    html += "<script>";
    html += "function selectSSID(ssid) {";
    html += "  document.getElementById('ssid_input').value = ssid;";
    html += "  alert('SSID \\'' + ssid + '\\' copied! You can now go back and save.');";
    html += "}";
    html += "</script>";
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<h1>🔍 WiFi Networks</h1>";
    html += "<input type='text' id='ssid_input' placeholder='Click on any network to copy' style='width:100%;padding:10px;margin:10px 0;border:1px solid #ddd;border-radius:5px;'>";
    html += "|h2";
    html += "|<th>#</th><th>SSID</th><th>Signal</th><th>Security</th>|</tr>";
    
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n && i < 20; i++) {
        String ssid = WiFi.SSID(i);
        html += "|<tr onclick='selectSSID(\"" + ssid + "\")' style='cursor:pointer;'>";
        html += "|<td>" + String(i + 1) + "|</td>";
        html += "|<td class='ssid'>" + ssid + "|</td>";
        html += "|<td>" + String(WiFi.RSSI(i)) + " dBm|</td>";
        html += "|<td>" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "Open" : "🔒 Secured") + "|</td>";
        html += "|</tr>";
    }
    
    if (n == 0) {
        html += "|<tr><td colspan='4'>No networks found|</tr>";
    }
    
    html += "|</table>";
    html += "<p><a href='/' class='button'>← Back to Configuration</a></p>";
    html += "<p><small>Click on any network name to copy it to the input field above.</small></p>";
    html += "</div></body></html>";
    
    webServer.send(200, "text/html", html);
}

void handleResetConfig() {
    preferences.clear();
    saved_ssid = "";
    saved_password = "";
    saved_ap_ssid = default_ap_ssid;
    saved_ap_password = default_ap_password;
    stationConnected = false;
    
    WiFi.mode(WIFI_AP);
    WiFi.softAP(default_ap_ssid, default_ap_password, 6, 0, 4);
    
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta http-equiv='refresh' content='5;url=/'>";
    html += "<title>Reset</title>";
    html += "<style>body{text-align:center;margin-top:50px;background:#667eea;color:white;font-family:Arial;}</style>";
    html += "</head><body>";
    html += "<h1>🗑️ Factory Reset Complete!</h1>";
    html += "<p>All settings have been cleared.</p>";
    html += "<p>AP SSID: <strong>" + String(default_ap_ssid) + "</strong></p>";
    html += "<p>AP Password: <strong>" + String(default_ap_password) + "</strong></p>";
    html += "<p>Redirecting...</p>";
    html += "</body></html>";
    webServer.send(200, "text/html", html);
}

void handleReboot() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta http-equiv='refresh' content='10;url=/'>";
    html += "<title>Rebooting...</title>";
    html += "<style>body{text-align:center;margin-top:50px;background:#667eea;color:white;font-family:Arial;}</style>";
    html += "</head><body>";
    html += "<h1>🔄 Rebooting ESP32...</h1>";
    html += "<p>Please wait 10 seconds</p>";
    html += "</body></html>";
    webServer.send(200, "text/html", html);
    delay(1000);
    ESP.restart();
}

void handleNotFound() {
    webServer.sendHeader("Location", String("http://") + ap_local_ip.toString(), true);
    webServer.send(302, "text/plain", "");
}

void handleStatusJSON() {
    String json = "{";
    json += "\"mode\":\"" + String(stationConnected ? "STA+AP" : "AP-ONLY") + "\",";
    json += "\"station_connected\":" + String(stationConnected ? "true" : "false") + ",";
    json += "\"station_ssid\":\"" + saved_ssid + "\",";
    json += "\"ap_ssid\":\"" + (saved_ap_ssid.length() > 0 ? saved_ap_ssid : default_ap_ssid) + "\",";
    json += "\"internet\":" + String(internetAvailable ? "true" : "false") + ",";
    json += "\"clients\":" + String(WiFi.softAPgetStationNum());
    json += "}";
    webServer.send(200, "application/json", json);
}
