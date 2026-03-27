#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>

// Cấu hình WiFi
const char* sta_ssid = "Galaxy A05s 495e";
const char* sta_password = "12341234";
const char* ap_ssid = "ESP32_AP";
const char* ap_password = "12345678";

// DNS và Web Server cho captive portal (tùy chọn)
const byte DNS_PORT = 53;
DNSServer dnsServer;
WebServer webServer(80);

// Biến kiểm tra kết nối
unsigned long previousMillis = 0;
const long interval = 30000; // 30 giây kiểm tra 1 lần

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== ESP32 WiFi Setup ===");
    
    // Bắt đầu ở chế độ Station + AP
    WiFi.mode(WIFI_AP_STA);
    
    // 1. Kết nối WiFi Station (đến điện thoại)
    Serial.printf("Connecting to: %s\n", sta_ssid);
    WiFi.begin(sta_ssid, sta_password);
    
    int attempt = 0;
    while (WiFi.status() != WL_CONNECTED && attempt < 20) {
        delay(500);
        Serial.print(".");
        attempt++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✓ Station connected!");
        Serial.printf("  IP Address: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("  Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
        Serial.printf("  DNS: %s\n", WiFi.dnsIP().toString().c_str());
        Serial.printf("  Channel: %d\n", WiFi.channel());
    } else {
        Serial.println("\n✗ Station connection failed!");
        Serial.println("  Check SSID/password or phone hotspot");
    }
    
    // 2. Khởi tạo AP (Access Point)
    int canal = WiFi.channel();
    if (canal == 0) canal = 6; // Mặc định channel 6 nếu chưa có
    
    bool apStarted = WiFi.softAP(ap_ssid, ap_password, canal, 0, 4);
    
    if (apStarted) {
        Serial.println("\n✓ AP started successfully!");
        Serial.printf("  AP SSID: %s\n", ap_ssid);
        Serial.printf("  AP IP: %s\n", WiFi.softAPIP().toString().c_str());
        Serial.printf("  Channel: %d\n", canal);
        Serial.printf("  Max connections: 4\n");
    } else {
        Serial.println("\n✗ AP failed to start!");
    }
    
    // 3. Cấu hình DNS server (cho phép captive portal)
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    webServer.on("/", handleRoot);
    webServer.onNotFound(handleNotFound);
    webServer.begin();
    
    // 4. Kiểm tra internet
    Serial.println("\n=== Testing Internet Connection ===");
    delay(2000); // Đợi kết nối ổn định
    
    if (checkInternet()) {
        Serial.println("✓ Internet connection: AVAILABLE");
        Serial.println("  You can now browse the web through ESP32 AP!");
    } else {
        Serial.println("✗ Internet connection: NOT AVAILABLE");
        Serial.println("\nPossible causes:");
        Serial.println("  1. Phone hotspot doesn't have mobile data enabled");
        Serial.println("  2. Phone data sharing is disabled");
        Serial.println("  3. Phone has reached connection limit");
        Serial.println("  4. Gateway can't be reached: " + WiFi.gatewayIP().toString());
    }
    
    Serial.println("\n=== ESP32 Ready ===");
    Serial.printf("Connect to AP: %s (Password: %s)\n", ap_ssid, ap_password);
    Serial.printf("Then open browser to: http://%s\n", WiFi.softAPIP().toString());
}

void loop() {
    // Xử lý DNS và Web server
    dnsServer.processNextRequest();
    webServer.handleClient();
    
    // Kiểm tra và tự động reconnect station
    if (WiFi.status() != WL_CONNECTED) {
        static unsigned long lastReconnect = 0;
        if (millis() - lastReconnect > 10000) {
            Serial.println("\n⚠️ Station disconnected! Reconnecting...");
            WiFi.reconnect();
            lastReconnect = millis();
        }
    }
    
    // Kiểm tra internet định kỳ
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;
        
        if (WiFi.status() == WL_CONNECTED) {
            bool hasInternet = checkInternet();
            Serial.printf("[%lu] Internet: %s\n", millis() / 1000, 
                         hasInternet ? "✓ OK" : "✗ LOST");
            
            if (!hasInternet) {
                Serial.println("  Tip: Check phone mobile data and sharing settings");
            }
        } else {
            Serial.printf("[%lu] WiFi: DISCONNECTED\n", millis() / 1000);
        }
    }
}

// Hàm kiểm tra kết nối internet bằng cách ping Google DNS
bool checkInternet() {
    WiFiClient client;
    
    // Thử kết nối đến Google DNS (8.8.8.8) port 53
    if (client.connect("8.8.8.8", 53)) {
        client.stop();
        return true;
    }
    
    // Thử phương án dự phòng: kết nối đến Cloudflare DNS
    if (client.connect("1.1.1.1", 53)) {
        client.stop();
        return true;
    }
    
    return false;
}

// Hàm kiểm tra gateway có hoạt động không
bool checkGateway() {
    if (WiFi.gatewayIP().isSet() && WiFi.gatewayIP() != INADDR_NONE) {
        WiFiClient client;
        if (client.connect(WiFi.gatewayIP(), 80)) {
            client.stop();
            return true;
        }
    }
    return false;
}

// Web server handlers
void handleRoot() {
    String html = "<!DOCTYPE html><html>";
    html += "<head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>ESP32 Status</title>";
    html += "<style>body{font-family:Arial;margin:20px;background:#f0f0f0;}";
    html += ".status{padding:15px;margin:10px 0;border-radius:5px;}";
    html += ".online{background:#d4edda;color:#155724;border:1px solid #c3e6cb;}";
    html += ".offline{background:#f8d7da;color:#721c24;border:1px solid #f5c6cb;}";
    html += ".info{background:#d1ecf1;color:#0c5460;border:1px solid #bee5eb;}";
    html += "h1{color:#333;}table{width:100%;border-collapse:collapse;}";
    html += "td,th{padding:8px;text-align:left;border-bottom:1px solid #ddd;}";
    html += "</style></head><body>";
    
    html += "<h1>📡 ESP32 WiFi Status</h1>";
    
    // Station status
    html += "<div class='status " + String(WiFi.status() == WL_CONNECTED ? "online" : "offline") + "'>";
    html += "<h3>📶 Station (Internet Source)</h3>";
    if (WiFi.status() == WL_CONNECTED) {
        html += "<table>";
        html += "<tr><th>SSID:</th><td>" + String(sta_ssid) + "</td></tr>";
        html += "<tr><th>IP Address:</th><td>" + WiFi.localIP().toString() + "</td></tr>";
        html += "<tr><th>Gateway:</th><td>" + WiFi.gatewayIP().toString() + "</td></tr>";
        html += "<tr><th>DNS:</th><td>" + WiFi.dnsIP().toString() + "</td></tr>";
        html += "<tr><th>Channel:</th><td>" + String(WiFi.channel()) + "</td></tr>";
        html += "<tr><th>RSSI:</th><td>" + String(WiFi.RSSI()) + " dBm</td></tr>";
        html += "</table>";
        
        // Internet check
        bool internet = checkInternet();
        html += "<p>🌐 Internet: <strong>" + String(internet ? "✓ Connected" : "✗ Disconnected") + "</strong></p>";
    } else {
        html += "<p>❌ Not connected to any WiFi network</p>";
    }
    html += "</div>";
    
    // AP status
    html += "<div class='status info'>";
    html += "<h3>📱 Access Point (ESP32 Hotspot)</h3>";
    html += "<table>";
    html += "<tr><th>SSID:</th><td>" + String(ap_ssid) + "</td></tr>";
    html += "<tr><th>Password:</th><td>" + String(ap_password) + "</td></tr>";
    html += "<tr><th>IP Address:</th><td>" + WiFi.softAPIP().toString() + "</td></tr>";
    html += "<tr><th>Connected Clients:</th><td>" + String(WiFi.softAPgetStationNum()) + "</td></tr>";
    html += "</table>";
    html += "</div>";
    
    // System info
    html += "<div class='status info'>";
    html += "<h3>⚙️ System Information</h3>";
    html += "<table>";
    html += "<tr><th>Uptime:</th><td>" + String(millis() / 1000) + " seconds</td></tr>";
    html += "<tr><th>Free Heap:</th><td>" + String(ESP.getFreeHeap() / 1024) + " KB</td></tr>";
    html += "<tr><th>Chip Model:</th><td>" + String(ESP.getChipModel()) + "</td></tr>";
    html += "</table>";
    html += "</div>";
    
    html += "<p><small>ESP32 WiFi Router - " + String(__DATE__) + " " + String(__TIME__) + "</small></p>";
    html += "</body></html>";
    
    webServer.send(200, "text/html", html);
}

void handleNotFound() {
    // Redirect all requests to root (captive portal)
    webServer.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    webServer.send(302, "text/plain", "");
}
