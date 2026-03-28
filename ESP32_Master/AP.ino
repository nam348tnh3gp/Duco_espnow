#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// Cấu hình WiFi
const char* sta_ssid = "Galaxy A05s 495e";
const char* sta_password = "12341234";
const char* ap_ssid = "ESP32_AP";
const char* ap_password = "12345678";

// Cấu hình IP cố định cho AP
IPAddress ap_local_ip(192, 168, 4, 1);
IPAddress ap_gateway(192, 168, 4, 1);
IPAddress ap_subnet(255, 255, 255, 0);

// DNS và Web Server
const byte DNS_PORT = 53;
DNSServer dnsServer;
WebServer webServer(80);

// Biến kiểm tra kết nối
unsigned long previousMillis = 0;
const long interval = 30000; // 30 giây

// Biến lưu trạng thái
bool internetAvailable = false;
unsigned long lastInternetCheck = 0;

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n╔═══════════════════════════════════╗");
    Serial.println("║     ESP32 WiFi Router v2.0       ║");
    Serial.println("╚═══════════════════════════════════╝\n");
    
    // Bắt đầu ở chế độ Station + AP
    WiFi.mode(WIFI_AP_STA);
    
    // 1. Kết nối WiFi Station
    connectToStation();
    
    // 2. Khởi tạo AP với IP cố định 192.168.4.1
    setupAccessPoint();
    
    // 3. Cấu hình DNS và Web Server
    setupDNSAndWebServer();
    
    // 4. Cấu hình mDNS để dễ dàng truy cập
    setupMDNS();
    
    // 5. Kiểm tra internet lần đầu
    checkInternetConnection();
    
    // 6. Hiển thị thông tin kết nối
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
}

// ========== CÁC HÀM CHÍNH ==========

void connectToStation() {
    Serial.println("📡 Connecting to Station...");
    Serial.printf("   SSID: %s\n", sta_ssid);
    
    WiFi.begin(sta_ssid, sta_password);
    
    int attempt = 0;
    while (WiFi.status() != WL_CONNECTED && attempt < 20) {
        delay(500);
        Serial.print(".");
        attempt++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ Station connected successfully!");
        Serial.printf("   📍 IP Address: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("   🚪 Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
        Serial.printf("   🔍 DNS: %s\n", WiFi.dnsIP().toString().c_str());
        Serial.printf("   📻 Channel: %d\n", WiFi.channel());
        Serial.printf("   📶 Signal: %d dBm\n", WiFi.RSSI());
    } else {
        Serial.println("\n❌ Station connection failed!");
        Serial.println("   ⚠️  Please check:");
        Serial.println("   - Phone hotspot is active");
        Serial.println("   - SSID and password are correct");
        Serial.println("   - Phone is within range");
    }
}

void setupAccessPoint() {
    Serial.println("\n📱 Setting up Access Point...");
    
    // Lấy channel từ station (nếu có)
    int canal = WiFi.channel();
    if (canal == 0) canal = 6;
    
    // Cấu hình IP cố định cho AP
    WiFi.softAPConfig(ap_local_ip, ap_gateway, ap_subnet);
    
    // Khởi tạo AP
    bool apStarted = WiFi.softAP(ap_ssid, ap_password, canal, 0, 4);
    
    if (apStarted) {
        Serial.println("✅ AP started successfully!");
        Serial.printf("   🏠 SSID: %s\n", ap_ssid);
        Serial.printf("   🔑 Password: %s\n", ap_password);
        Serial.printf("   🌐 IP Address: %s (FIXED)\n", WiFi.softAPIP().toString().c_str());
        Serial.printf("   📻 Channel: %d (synced with station)\n", canal);
        Serial.printf("   👥 Max clients: 4\n");
    } else {
        Serial.println("❌ AP failed to start!");
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
    webServer.on("/reboot", handleReboot);
    webServer.onNotFound(handleNotFound);
    
    webServer.begin();
    Serial.println("✅ Web server started on port 80");
    Serial.printf("   📱 Captive portal: http://%s\n", ap_local_ip.toString().c_str());
}

void setupMDNS() {
    // mDNS để dễ dàng truy cập bằng tên
    if (MDNS.begin("esp32")) {
        Serial.println("✅ mDNS responder started");
        Serial.println("   🌐 Access via: http://esp32.local");
    }
}

void checkInternetConnection() {
    Serial.println("\n🌍 Testing Internet Connection...");
    delay(2000);
    
    internetAvailable = checkInternet();
    
    if (internetAvailable) {
        Serial.println("✅ Internet connection: AVAILABLE");
        Serial.println("   🎉 You can now browse the web through ESP32 AP!");
    } else {
        Serial.println("❌ Internet connection: NOT AVAILABLE");
        Serial.println("\n   🔍 Possible causes:");
        Serial.println("   1. Phone hotspot doesn't have mobile data enabled");
        Serial.println("   2. Phone data sharing is disabled");
        Serial.println("   3. Phone has reached connection limit");
        Serial.println("   4. Gateway can't be reached");
        
        if (WiFi.gatewayIP().isSet()) {
            Serial.printf("   5. Gateway IP: %s (check if accessible)\n", 
                         WiFi.gatewayIP().toString().c_str());
        }
    }
}

void displayConnectionInfo() {
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║        ESP32 IS READY!                ║");
    Serial.println("╠════════════════════════════════════════╣");
    Serial.printf("║ AP:      %-30s║\n", ap_ssid);
    Serial.printf("║ Password:%-30s║\n", ap_password);
    Serial.printf("║ IP:      %-30s║\n", ap_local_ip.toString().c_str());
    Serial.printf("║ mDNS:    http://esp32.local%-16s║\n", "");
    Serial.printf("║ Portal:  http://%s%-19s║\n", ap_local_ip.toString().c_str(), "");
    Serial.printf("║ Clients: %d connected%-24s║\n", WiFi.softAPgetStationNum(), "");
    Serial.printf("║ Internet: %-31s║\n", internetAvailable ? "✅ AVAILABLE" : "❌ UNAVAILABLE");
    Serial.println("╚════════════════════════════════════════╝\n");
}

// ========== HÀM KIỂM TRA VÀ XỬ LÝ ==========

bool checkInternet() {
    WiFiClient client;
    
    // Thử kết nối đến Google DNS
    if (client.connect("8.8.8.8", 53)) {
        client.stop();
        return true;
    }
    
    // Thử Cloudflare DNS
    if (client.connect("1.1.1.1", 53)) {
        client.stop();
        return true;
    }
    
    // Thử ping gateway nếu có
    if (WiFi.gatewayIP().isSet() && WiFi.gatewayIP() != INADDR_NONE) {
        if (client.connect(WiFi.gatewayIP(), 80)) {
            client.stop();
            return true;
        }
    }
    
    return false;
}

void handleStationReconnect() {
    static unsigned long lastReconnect = 0;
    
    if (WiFi.status() != WL_CONNECTED) {
        if (millis() - lastReconnect > 10000) {
            Serial.println("\n⚠️ Station disconnected! Reconnecting...");
            WiFi.reconnect();
            lastReconnect = millis();
        }
    }
}

void handlePeriodicInternetCheck() {
    unsigned long currentMillis = millis();
    
    if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;
        
        bool currentInternet = checkInternet();
        
        if (currentInternet != internetAvailable) {
            internetAvailable = currentInternet;
            Serial.printf("\n🔄 Internet status changed: %s\n", 
                         internetAvailable ? "CONNECTED" : "DISCONNECTED");
        }
        
        if (WiFi.status() == WL_CONNECTED) {
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
    String html = "<!DOCTYPE html><html>";
    html += "<head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<meta http-equiv='refresh' content='30'>";
    html += "<title>ESP32 WiFi Router</title>";
    html += "<style>";
    html += "*{margin:0;padding:0;box-sizing:border-box;}";
    html += "body{font-family:'Segoe UI',Arial;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;padding:20px;}";
    html += ".container{max-width:800px;margin:0 auto;}";
    html += ".card{background:white;border-radius:15px;padding:20px;margin-bottom:20px;box-shadow:0 10px 30px rgba(0,0,0,0.2);}";
    html += ".header{text-align:center;color:white;margin-bottom:20px;}";
    html += ".header h1{font-size:2em;margin-bottom:10px;}";
    html += ".status-badge{display:inline-block;padding:5px 15px;border-radius:20px;font-size:0.9em;font-weight:bold;}";
    html += ".online{background:#d4edda;color:#155724;border-left:4px solid #28a745;}";
    html += ".offline{background:#f8d7da;color:#721c24;border-left:4px solid #dc3545;}";
    html += ".info{background:#d1ecf1;color:#0c5460;border-left:4px solid #17a2b8;}";
    html += "h2{color:#333;margin-bottom:15px;font-size:1.3em;}";
    html += "table{width:100%;border-collapse:collapse;}";
    html += "td,th{padding:12px;text-align:left;border-bottom:1px solid #ddd;}";
    html += "th{background:#f8f9fa;font-weight:600;}";
    html += ".signal{font-weight:bold;}";
    html += ".good{color:#28a745;}";
    html += ".fair{color:#ffc107;}";
    html += ".poor{color:#dc3545;}";
    html += ".button{display:inline-block;padding:10px 20px;margin:5px;background:#667eea;color:white;text-decoration:none;border-radius:5px;transition:0.3s;}";
    html += ".button:hover{background:#5a67d8;}";
    html += ".footer{text-align:center;color:white;margin-top:20px;font-size:0.8em;}";
    html += "</style></head><body>";
    
    html += "<div class='container'>";
    html += "<div class='header'>";
    html += "<h1>📡 ESP32 WiFi Router</h1>";
    html += "<p>Access Point: <strong>" + String(ap_ssid) + "</strong></p>";
    html += "</div>";
    
    // Status Card
    String statusClass = internetAvailable ? "online" : "offline";
    html += "<div class='card " + statusClass + "'>";
    html += "<h2>🌐 Internet Status</h2>";
    html += "<p style='font-size:1.2em;margin:10px 0;'>";
    html += internetAvailable ? "✅ Connected to Internet" : "❌ No Internet Connection";
    html += "</p>";
    if (!internetAvailable && WiFi.status() == WL_CONNECTED) {
        html += "<p>⚠️ Check phone mobile data and hotspot sharing settings</p>";
    }
    html += "</div>";
    
    // Station Info
    html += "<div class='card'>";
    html += "<h2>📶 Station Connection</h2>";
    if (WiFi.status() == WL_CONNECTED) {
        html += "<table>";
        html += "<tr><th>SSID:</th><td>" + String(sta_ssid) + "</td></tr>";
        html += "<tr><th>IP Address:</th><td>" + WiFi.localIP().toString() + "</td></tr>";
        html += "<tr><th>Gateway:</th><td>" + WiFi.gatewayIP().toString() + "</td></tr>";
        html += "<tr><th>DNS:</th><td>" + WiFi.dnsIP().toString() + "</td></tr>";
        int rssi = WiFi.RSSI();
        String signalClass = (rssi > -60) ? "good" : (rssi > -70) ? "fair" : "poor";
        html += "<tr><th>Signal:</th><td class='signal " + signalClass + "'>" + String(rssi) + " dBm</td></tr>";
        html += "<tr><th>Channel:</th><td>" + String(WiFi.channel()) + "</td></tr>";
        html += "</table>";
    } else {
        html += "<p>❌ Not connected to any WiFi network</p>";
        html += "<a href='/scan' class='button'>🔍 Scan Networks</a>";
    }
    html += "</div>";
    
    // AP Info
    html += "<div class='card'>";
    html += "<h2>📱 Access Point</h2>";
    html += "<table>";
    html += "<tr><th>SSID:</th><td>" + String(ap_ssid) + "</td></tr>";
    html += "<tr><th>IP Address:</th><td><strong>192.168.4.1</strong> (Fixed)</td></tr>";
    html += "<tr><th>Connected Clients:</th><td>" + String(WiFi.softAPgetStationNum()) + "</td></tr>";
    html += "<tr><th>Channel:</th><td>" + String(WiFi.channel()) + "</td></tr>";
    html += "</table>";
    html += "</div>";
    
    // System Info
    html += "<div class='card'>";
    html += "<h2>⚙️ System</h2>";
    html += "<table>";
    html += "<tr><th>Uptime:</th><td>" + String(millis() / 1000) + " seconds</td></tr>";
    html += "<tr><th>Free Heap:</th><td>" + String(ESP.getFreeHeap() / 1024) + " KB</td></tr>";
    html += "<tr><th>Chip:</th><td>" + String(ESP.getChipModel()) + "</td></tr>";
    html += "<tr><th>Flash Size:</th><td>" + String(ESP.getFlashChipSize() / 1024 / 1024) + " MB</td></tr>";
    html += "</table>";
    html += "<div style='margin-top:15px;'>";
    html += "<a href='/reboot' class='button' onclick='return confirm(\"Reboot ESP32?\")'>🔄 Reboot</a>";
    html += "<a href='/configure' class='button'>⚙️ Configure</a>";
    html += "</div>";
    html += "</div>";
    
    html += "<div class='footer'>";
    html += "<p>ESP32 WiFi Router v2.0 | " + String(__DATE__) + " " + String(__TIME__) + "</p>";
    html += "<p>📍 Fixed IP: 192.168.4.1 | Access via: http://esp32.local</p>";
    html += "</div></div></body></html>";
    
    webServer.send(200, "text/html", html);
}

void handleStatusJSON() {
    String json = "{";
    json += "\"station\":{";
    json += "\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
    json += "\"ssid\":\"" + String(sta_ssid) + "\",";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI());
    json += "},";
    json += "\"ap\":{";
    json += "\"ssid\":\"" + String(ap_ssid) + "\",";
    json += "\"ip\":\"192.168.4.1\",";
    json += "\"clients\":" + String(WiFi.softAPgetStationNum());
    json += "},";
    json += "\"internet\":" + String(internetAvailable ? "true" : "false") + ",";
    json += "\"uptime\":" + String(millis() / 1000);
    json += "}";
    
    webServer.send(200, "application/json", json);
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
    html += "<table>";
    html += "<tr><th>#</th><th>SSID</th><th>Signal</th><th>Encryption</th></tr>";
    
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n && i < 20; i++) {
        html += "<tr>";
        html += "<td>" + String(i + 1) + "</td>";
        html += "<td>" + WiFi.SSID(i) + "</td>";
        html += "<td>" + String(WiFi.RSSI(i)) + " dBm</td>";
        html += "<td>" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "Open" : "Secured") + "</td>";
        html += "</tr>";
    }
    
    html += "</table>";
    html += "<p><a href='/' class='button'>Back to Status</a></p>";
    html += "</div></body></html>";
    
    webServer.send(200, "text/html", html);
}

void handleConfigure() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<title>Configure WiFi</title>";
    html += "<style>body{font-family:Arial;margin:20px;background:#667eea;}";
    html += ".container{max-width:500px;margin:0 auto;background:white;padding:20px;border-radius:10px;}";
    html += "input{width:100%;padding:10px;margin:10px 0;border:1px solid #ddd;border-radius:5px;}";
    html += "button{padding:10px 20px;background:#667eea;color:white;border:none;border-radius:5px;}";
    html += "</style></head><body>";
    html += "<div class='container'>";
    html += "<h1>⚙️ Configure Station WiFi</h1>";
    html += "<form action='/save' method='POST'>";
    html += "<label>SSID:</label><input type='text' name='ssid' placeholder='WiFi Name'>";
    html += "<label>Password:</label><input type='password' name='password' placeholder='Password'>";
    html += "<button type='submit'>Save & Connect</button>";
    html += "</form>";
    html += "<p><a href='/'>Back</a></p>";
    html += "</div></body></html>";
    
    webServer.send(200, "text/html", html);
}

void handleReboot() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta http-equiv='refresh' content='10;url=/'>";
    html += "<title>Rebooting...</title>";
    html += "<style>body{font-family:Arial;text-align:center;margin-top:50px;background:#667eea;color:white;}</style>";
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
