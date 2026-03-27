/*
   DuinoCoin ESP-NOW Master Node
   Phân phối job cho 9 slave nodes và tổng hợp kết quả
*/

#pragma GCC optimize("-Ofast")

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>
#include "Settings.h"
#include "MiningJob.h"
#include "DSHA1.h"
#include "Counter.h"

// Structure for ESP-NOW data
typedef struct struct_message {
  uint8_t msgType;
  uint8_t slaveId;
  uint32_t counter;
  float hashrate;
  float elapsedTime;
  bool isAccepted;
  uint32_t jobId;
} struct_message;

typedef struct struct_job {
  uint8_t msgType;
  uint32_t jobId;
  char lastBlockHash[65];
  char expectedHash[41];
  uint32_t difficulty;
} struct_job;

// Slave information
struct SlaveInfo {
  uint8_t mac[6];
  bool active;
  uint32_t lastSeen;
  uint32_t shares;
  uint32_t acceptedShares;
  float hashrate;
  uint32_t jobId;
};

SlaveInfo slaves[MAX_SLAVES];
uint8_t slaveCount = 0;
uint32_t currentJobId = 0;

// Mining job
MiningJob *masterJob;
struct_message myData;
struct_job currentJob;

// Statistics
unsigned long totalShares = 0;
unsigned long totalAccepted = 0;
float totalHashrate = 0;

// Callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  #if defined(SERIAL_PRINTING)
    Serial.print("Send status to ");
    for(int i = 0; i < 6; i++) {
      Serial.printf("%02X", mac_addr[i]);
      if(i < 5) Serial.print(":");
    }
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? " Success" : " Fail");
  #endif
}

// Callback when data is received
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  
  #if defined(SERIAL_PRINTING)
    Serial.printf("Received from slave %d: job=%d, counter=%lu, hr=%.2f, accepted=%d\n",
                  myData.slaveId, myData.jobId, myData.counter, 
                  myData.hashrate, myData.isAccepted);
  #endif
  
  // Find slave
  for(int i = 0; i < slaveCount; i++) {
    if(memcmp(slaves[i].mac, mac, 6) == 0) {
      slaves[i].lastSeen = millis();
      slaves[i].shares++;
      slaves[i].hashrate = myData.hashrate;
      slaves[i].jobId = myData.jobId;
      
      if(myData.isAccepted) {
        slaves[i].acceptedShares++;
        totalAccepted++;
      }
      
      totalShares++;
      totalHashrate += myData.hashrate;
      break;
    }
  }
}

// Add slave to network
bool addSlave(uint8_t *mac) {
  if(slaveCount >= MAX_SLAVES) return false;
  
  // Check if already exists
  for(int i = 0; i < slaveCount; i++) {
    if(memcmp(slaves[i].mac, mac, 6) == 0) return true;
  }
  
  memcpy(slaves[slaveCount].mac, mac, 6);
  slaves[slaveCount].active = true;
  slaves[slaveCount].lastSeen = millis();
  slaves[slaveCount].shares = 0;
  slaves[slaveCount].acceptedShares = 0;
  slaves[slaveCount].hashrate = 0;
  slaves[slaveCount].jobId = 0;
  
  // Add peer
  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, mac, 6);
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.encrypt = false;
  
  if(esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return false;
  }
  
  slaveCount++;
  Serial.printf("Added slave %d, MAC: ", slaveCount);
  for(int i = 0; i < 6; i++) {
    Serial.printf("%02X", mac[i]);
    if(i < 5) Serial.print(":");
  }
  Serial.println();
  
  return true;
}

// Broadcast job to all slaves
void broadcastJob() {
  if(slaveCount == 0) return;
  
  currentJob.msgType = 1;
  currentJob.jobId = currentJobId++;
  
  // Get job from master miner
  if(masterJob->getCurrentJob(currentJob.lastBlockHash, 
                               currentJob.expectedHash, 
                               currentJob.difficulty)) {
    
    for(int i = 0; i < slaveCount; i++) {
      esp_now_send(slaves[i].mac, (uint8_t *)&currentJob, sizeof(currentJob));
      slaves[i].jobId = currentJob.jobId;
    }
    
    #if defined(SERIAL_PRINTING)
      Serial.printf("Broadcast job #%lu: diff=%lu\n", 
                    currentJob.jobId, currentJob.difficulty);
    #endif
  }
}

// Initialize ESP-NOW
void initESPNow() {
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  
  if(esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  
  Serial.println("ESP-NOW initialized");
}

// Print slave status
void printStatus() {
  Serial.println("\n=== Slave Status ===");
  Serial.printf("Total slaves: %d\n", slaveCount);
  Serial.printf("Total shares: %lu\n", totalShares);
  Serial.printf("Total accepted: %lu\n", totalAccepted);
  Serial.printf("Accept rate: %.2f%%\n", 
                totalShares > 0 ? (totalAccepted * 100.0 / totalShares) : 0);
  Serial.printf("Total hashrate: %.2f kH/s\n", totalHashrate / 1000);
  Serial.println("\nSlaves:");
  
  for(int i = 0; i < slaveCount; i++) {
    Serial.printf("  Slave %d: HR=%.2f kH/s, Shares=%lu, Accepted=%lu, Active=%d\n",
                  i+1, slaves[i].hashrate / 1000, slaves[i].shares, 
                  slaves[i].acceptedShares, 
                  (millis() - slaves[i].lastSeen) < 10000);
  }
  Serial.println("===================\n");
}

// Setup WiFi and connection to Duino-Coin node
void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected");
  Serial.println("IP address: " + WiFi.localIP().toString());
}

void setup() {
  Serial.begin(SERIAL_BAUDRATE);
  Serial.println("\n\nDuinoCoin ESP-NOW Master Node v" + String(SOFTWARE_VERSION));
  
  // Setup WiFi and node connection
  setupWiFi();
  
  // Initialize master miner (core 0)
  MiningConfig *config = new MiningConfig(DUCO_USER, "Master", MINER_KEY);
  masterJob = new MiningJob(0, config);
  masterJob->connectToNode();
  
  // Initialize ESP-NOW
  initESPNow();
  
  // Wait for slaves to register (broadcast discovery)
  delay(2000);
  
  pinMode(LED_BUILTIN, OUTPUT);
  blink(BLINK_SETUP_COMPLETE);
  
  Serial.println("Master ready, waiting for slaves...");
}

unsigned long lastSync = 0;
unsigned long lastStatus = 0;

void loop() {
  // Master also mines on core 1
  masterJob->mine();
  
  // Update statistics from master mining
  totalHashrate += masterJob->getHashrate() / 1000;
  totalShares++;
  if(masterJob->isLastShareAccepted()) totalAccepted++;
  
  // Broadcast new job periodically
  if(millis() - lastSync > SYNC_INTERVAL) {
    broadcastJob();
    lastSync = millis();
  }
  
  // Print status every 30 seconds
  if(millis() - lastStatus > 30000) {
    printStatus();
    lastStatus = millis();
  }
  
  delay(10);
}

void blink(uint8_t count) {
  #if defined(LED_BLINKING)
    for(int x = 0; x < count; x++) {
      digitalWrite(LED_BUILTIN, LOW);
      delay(100);
      digitalWrite(LED_BUILTIN, HIGH);
      delay(100);
    }
  #endif
}
