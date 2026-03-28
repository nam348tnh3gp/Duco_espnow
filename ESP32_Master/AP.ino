#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>

// Cấu hình WiFi
const char* ap_ssid = "ESP32_Config";
const char* ap_password = "12345678";

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
bool stationConnected = false;
unsigned long lastConnectionAttempt = 0;
bool isConfigMode = true;  // Bắt đầu ở chế độ cấu hình

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n╔═══════════════════════════════════╗");
    Serial.println("║   ESP32 WiFi Router v3.0        ║");
    Serial.println("║      with Auto Fallback         ║");
    Serial.println("╚═══════════════════════════════════╝\n");
    
    // Khởi tạo Preferences
    preferences.begin("wifi-config", false);
    saved_ssid = preferences.getString("ssid", "");
    saved_password = preferences.getString("password", "");
    
    // 1. Luôn khởi tạo AP trước
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
    // Xử lý DNS và Web server
    dnsServer.processNextRequest();
    webServer.handleClient();
    
    // Kiểm tra và tự động reconnect station
    handleStationReconnect();
    
    // Kiểm tra internet định kỳ
    handlePeriodicInternetCheck();
    
    // Kiểm tra timeout kết nối Station
    checkStationConnectionTimeout();
}

// ========== CÁC HÀM CHÍNH ==========

void setupAccessPoint() {
    Serial.println("\n📱 Setting up Access Point...");
    
    // Luôn set AP mode với channel cố định
    WiFi.mode(WIFI_AP);
    
    // Cấu hình IP cố định cho AP
    WiFi.softAPConfig(ap_local_ip, ap_gateway, ap_subnet);
    
    // Khởi tạo AP với channel cố định (không phụ thuộc station)
    bool apStarted = WiFi.softAP(ap_ssid, ap_password, 6, 0, 4);
    
    if (apStarted) {
        Serial.println("✅ AP started successfully!");
        Serial.printf("   🏠 SSID: %s\n", ap_ssid);
        Serial.printf("   🔑 Password: %s\n", ap_password);
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
    
    // Chuyển sang chế độ AP+STA
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(saved_ssid.c_str(), saved_password.c_str());
    
    lastConnectionAttempt = millis();
    int attempt = 0;
    
    while (WiFi.status() != WL_CONNECTED && attempt < 20) {
        delay(500);
        Serial.print(".");
        attempt++;
        
        // Nếu quá 5 giây chưa kết nối, hiển thị cảnh báo
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
        
        // Kiểm tra internet
        delay(1000);
        checkInternetConnection();
    } else {
        stationConnected = false;
        Serial.println("\n❌ Station connection failed!");
        Serial.println("   🔄 Keeping AP mode active for reconfiguration");
        Serial.printf("   📱 Connect to '%s' to fix settings\n", ap_ssid);
        
        // Quay lại chế độ AP-only
        WiFi.mode(WIFI_AP);
        WiFi.softAP(ap_ssid, ap_password, 6, 0, 4);
    }
}

void checkStationConnectionTimeout() {
    // Nếu đang cố kết nối nhưng quá 30 giây chưa thành công
    if (!stationConnected && saved_ssid.length() > 0 && 
        millis() - lastConnectionAttempt > 30000) {
        
        Serial.println("\n⏰ Station connection timeout!");
        Serial.println("   🔄 Switching back to AP-only mode");
        
        // Reset và quay lại AP mode
        WiFi.mode(WIFI_AP);
        WiFi.softAP(ap_ssid, ap_password, 6, 0, 4);
        stationConnected = false;
        isConfigMode = true;
        
        // Hiển thị lại thông tin
        displayConnectionInfo();
    }
}

void setupDNSAndWebServer() {
    Serial.println("\n🌐 Setting up DNS & Web Server...");
    
    // DNS server cho captive portal
    dnsServer.start(DNS_PORT, "*", ap_local_ip);
    
    // Web server routes
    webServer.on("/", handleRoot);
    webServer.on("/status", handleStatusJSON);
    webServer.on("/scan", handleWiFiScan);
    webServer.on("/configure", handleConfigure);
    webServer.on("/save", HTTP_POST, handleSave);
    webServer.on("/reboot", handleReboot);
    webServer.on("/reset", handleResetConfig);
    webServer.on("/switchmode", handleSwitchMode);
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
    Serial.printf("║ AP:      %-30s║\n", ap_ssid);
    Serial.printf("║ Password:%-30s║\n", ap_password);
    Serial.printf("║ IP:      %-30s║\n", ap_local_ip.toString().c_str());
    
    if (stationConnected && WiFi.status() == WL_CONNECTED) {
        Serial.printf("║ Station: %-30s║\n", saved_ssid.c_str());
        Serial.printf("║ IP:      %-30s║\n", WiFi.localIP().toString().c_str());
        Serial.printf("║ Internet: %-31s║\n", internetAvailable ? "✅ AVAILABLE" : "❌ UNAVAILABLE");
    } else {
        Serial.printf("║ Mode:    %-30s║\n", "CONFIGURATION MODE");
        Serial.printf("║ Status:  %-30s║\n", "Waiting for WiFi config");
    }
    
    Serial.printf("║ Clients: %d connected%-24s║\n", WiFi.softAPgetStationNum(), "");
    Serial.println("╚════════════════════════════════════════╝\n");
    
    if (!stationConnected) {
        Serial.println("📱 IMPORTANT:");
        Serial.printf("   1. Connect to WiFi: %s\n", ap_ssid);
        Serial.printf("   2. Open browser: http://%s\n", ap_local_ip.toString().c_str());
        Serial.println("   3. Configure your WiFi network\n");
    }
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
        Serial.println("   💡 You can still access http://192.168.4.1 to reconfigure");
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
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>ESP32 WiFi Router</title>";
    html += "<style>";
    html += "*{margin:0;padding:0;box-sizing:border-box;}";
    html += "body{font-family:'Segoe UI',Arial;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;padding:20px;}";
    html += ".container{max-width:800px;margin:0 auto;}";
    html += ".card{background:white;border-radius:15px;padding:20px;margin-bottom:20px;box-shadow:0 10px 30px rgba(0,0,0,0.2);}";
    html += ".header{text-align:center;color:white;margin-bottom:20px;}";
    html += "h1{font-size:2em;margin-bottom:10px;}";
    html += "h2{color:#333;margin-bottom:15px;}";
    html += ".status-badge{display:inline-block;padding:5px 15px;border-radius:20px;font-weight:bold;}";
    html += ".online{background:#d4edda;color:#155724;}";
    html += ".offline{background:#f8d7da;color:#721c24;}";
    html += ".config-mode{background:#fff3cd;color:#856404;}";
    html += "table{width:100%;border-collapse:collapse;}";
    html += "td,th{padding:12px;text-align:left;border-bottom:1px solid #ddd;}";
    html += ".button{display:inline-block;padding:10px 20px;margin:5px;background:#667eea;color:white;text-decoration:none;border-radius:5px;}";
    html += ".button-red{background:#dc3545;}";
    html += ".footer{text-align:center;color:white;margin-top:20px;}";
    html += "</style></head><body>";
    
    html += "<div class='container'>";
    html += "<div class='header'>";
    html += "<h1>📡 ESP32 WiFi Router</h1>";
    html += "</div>";
    
    // Status Card
    html += "<div class='card'>";
    html += "<h2>📊 System Status</h2>";
    
    if (!stationConnected || WiFi.status() != WL_CONNECTED) {
        html += "<div class='status-badge config-mode'>⚠️ CONFIGURATION MODE</div>";
        html += "<p>No WiFi connection configured or connection failed.</p>";
        html += "<p>Please configure your WiFi network below.</p>";
    } else {
        String statusClass = internetAvailable ? "online" : "offline";
        html += "<div class='status-badge " + statusClass + "'>";
        html += internetAvailable ? "✅ Internet Connected" : "❌ No Internet";
        html += "</div>";
        html += "<p>Connected to: <strong>" + saved_ssid + "</strong></p>";
    }
    html += "</div>";
    
    // Configuration Card
    html += "<div class='card'>";
    html += "<h2>⚙️ WiFi Configuration</h2>";
    html += "<form action='/save' method='POST'>";
    html += "<p><strong>WiFi Network:</strong></p>";
    html += "<input type='text' name='ssid' placeholder='Enter WiFi name' style='width:100%;padding:10px;margin:10px 0;' required>";
    html += "<p><strong>Password:</strong></p>";
    html += "<input type='password' name='password' placeholder='Enter password' style='width:100%;padding:10px;margin:10px 0;'>";
    html += "<button type='submit' class='button'>Save & Connect</button>";
    html += "</form>";
    html += "<p><small>After saving, ESP32 will try to connect. If successful, you'll get internet access.</small></p>";
    html += "<p><small>If connection fails, AP will remain active for reconfiguration.</small></p>";
    html += "</div>";
    
    // Info Card
    html += "<div class='card'>";
    html += "<h2>ℹ️ Information</h2>";
    html += "<p><strong>AP SSID:</strong> " + String(ap_ssid) + "</p>";
    html += "<p><strong>AP Password:</strong> " + String(ap_password) + "</p>";
    html += "<p><strong>Connected Clients:</strong> " + String(WiFi.softAPgetStationNum()) + "</p>";
    html += "<p><strong>ESP32 IP:</strong> 192.168.4.1</p>";
    html += "<div style='margin-top:15px;'>";
    html += "<a href='/scan' class='button'>🔍 Scan Networks</a>";
    html += "<a href='/reboot' class='button'>🔄 Reboot</a>";
    html += "<a href='/reset' class='button button-red'>🗑️ Reset All</a>";
    html += "</div>";
    html += "</div>";
    
    html += "<div class='footer'>";
    html += "<p>ESP32 WiFi Router v3.0</p>";
    html += "</div></div></body></html>";
    
    webServer.send(200, "text/html", html);
}

void handleSave() {
    if (webServer.method() == HTTP_POST) {
        String new_ssid = webServer.arg("ssid");
        String new_password = webServer.arg("password");
        
        if (new_ssid.length() > 0) {
            Serial.println("\n📝 Saving new WiFi configuration:");
            Serial.printf("   SSID: %s\n", new_ssid.c_str());
            
            // Lưu vào Preferences
            preferences.putString("ssid", new_ssid);
            preferences.putString("password", new_password);
            saved_ssid = new_ssid;
            saved_password = new_password;
            
            String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
            html += "<meta http-equiv='refresh' content='10;url=/'>";
            html += "<title>Connecting...</title>";
            html += "<style>body{font-family:Arial;text-align:center;margin-top:50px;background:#667eea;color:white;}</style>";
            html += "</head><body>";
            html += "<h1>📡 Connecting to WiFi...</h1>";
            html += "<p>Network: <strong>" + new_ssid + "</strong></p>";
            html += "<p>Please wait 10 seconds...</p>";
            html += "<p>If connection fails, AP will remain active.</p>";
            html += "</body></html>";
            webServer.send(200, "text/html", html);
            
            // Thử kết nối
            delay(1000);
            connectToStation();
        } else {
            webServer.send(400, "text/plain", "SSID is required!");
        }
    }
}

void handleWiFiScan() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta http-equiv='refresh' content='10'>";
    html += "<title>WiFi Scan</title>";
    html += "<style>body{font-family:Arial;margin:20px;background:#667eea;}";
    html += ".container{max-width:600px;margin:0 auto;background:white;padding:20px;border-radius:10px;}";
    html += "table{width:100%;border-collapse:collapse;}";
    html += "td,th{padding:8px;border-bottom:1px solid #ddd;}";
    html += ".button{display:inline-block;padding:10px 20px;background:#667eea;color:white;text-decoration:none;border-radius:5px;}";
    html += "</style></head><body>";
    html += "<div class='container'>";
    html += "<h1>🔍 WiFi Networks</h1>";
    html += "|h2";
    html += "|<th>#</th><th>SSID</th><th>Signal</th><th>Security</th>|</tr>";
    
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n && i < 20; i++) {
        html += "|<tr>";
        html += "|<td>" + String(i + 1) + "|</td>";
        html += "|<td>" + WiFi.SSID(i) + "|</td>";
        html += "|<td>" + String(WiFi.RSSI(i)) + " dBm|</td>";
        html += "|<td>" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "Open" : "Secured") + "|</td>";
        html += "|</tr>";
    }
    
    html += "|</table>";
    html += "<p><a href='/' class='button'>Back</a></p>";
    html += "</div></body></html>";
    
    webServer.send(200, "text/html", html);
}

void handleConfigure() {
    handleRoot();  // Redirect to root which has config form
}

void handleSwitchMode() {
    // Force AP mode
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_password, 6, 0, 4);
    stationConnected = false;
    
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta http-equiv='refresh' content='3;url=/'>";
    html += "<title>Switching Mode</title>";
    html += "<body style='text-align:center;margin-top:50px;'>";
    html += "<h1>Switched to Configuration Mode</h1>";
    html += "<p>ESP32 is now in AP-only mode</p>";
    html += "</body></html>";
    webServer.send(200, "text/html", html);
}

void handleResetConfig() {
    preferences.clear();
    saved_ssid = "";
    saved_password = "";
    stationConnected = false;
    
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_password, 6, 0, 4);
    
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta http-equiv='refresh' content='5;url=/'>";
    html += "<title>Reset</title>";
    html += "<body style='text-align:center;margin-top:50px;background:#667eea;color:white;'>";
    html += "<h1>✅ Configuration Reset</h1>";
    html += "<p>All settings cleared. ESP32 is now in configuration mode.</p>";
    html += "</body></html>";
    webServer.send(200, "text/html", html);
}

void handleReboot() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta http-equiv='refresh' content='10;url=/'>";
    html += "<title>Rebooting...</title>";
    html += "<body style='text-align:center;margin-top:50px;background:#667eea;color:white;'>";
    html += "<h1>🔄 Rebooting...</h1>";
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
    json += "\"station\":" + String(stationConnected ? "true" : "false") + ",";
    json += "\"ssid\":\"" + saved_ssid + "\",";
    json += "\"internet\":" + String(internetAvailable ? "true" : "false") + ",";
    json += "\"clients\":" + String(WiFi.softAPgetStationNum());
    json += "}";
    webServer.send(200, "application/json", json);
}
