/*
   DuinoCoin ESP-NOW Master Node
   Phân phối job cho slave nodes và tổng hợp kết quả
   Hỗ trợ 10 job khác nhau, phân phối không trùng lặp
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

// Job structure
struct JobInfo {
  uint32_t jobId;
  char lastBlockHash[65];
  char expectedHash[41];
  uint32_t difficulty;
  bool active;
  uint32_t assignedTo;
};

// Slave information
struct SlaveInfo {
  uint8_t mac[6];
  bool active;
  uint32_t lastSeen;
  uint32_t shares;
  uint32_t acceptedShares;
  float hashrate;
  float totalHashrate;  // For averaging
  uint32_t jobId;
  uint32_t samplesCount;  // For averaging
};

SlaveInfo slaves[MAX_SLAVES];
JobInfo jobs[MAX_JOBS];
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
unsigned long lastStatsReset = 0;

// Job distribution
uint32_t nextJobIndex = 0;

// Callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  #if defined(SERIAL_PRINTING)
    if(status != ESP_NOW_SEND_SUCCESS) {
      Serial.print("Send failed to ");
      for(int i = 0; i < 6; i++) {
        Serial.printf("%02X", mac_addr[i]);
        if(i < 5) Serial.print(":");
      }
      Serial.println();
    }
  #endif
}

// Callback when data is received
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  
  #if defined(SERIAL_PRINTING)
    Serial.printf("Received from slave %d: job=%lu, counter=%lu, hr=%.2f, accepted=%d\n",
                  myData.slaveId, myData.jobId, myData.counter, 
                  myData.hashrate, myData.isAccepted);
  #endif
  
  // Find slave
  for(int i = 0; i < slaveCount; i++) {
    if(memcmp(slaves[i].mac, mac, 6) == 0) {
      slaves[i].lastSeen = millis();
      slaves[i].shares++;
      // Update average hashrate
      slaves[i].totalHashrate += myData.hashrate;
      slaves[i].samplesCount++;
      slaves[i].hashrate = slaves[i].totalHashrate / slaves[i].samplesCount;
      slaves[i].jobId = myData.jobId;
      
      if(myData.isAccepted) {
        slaves[i].acceptedShares++;
        totalAccepted++;
      }
      
      totalShares++;
      // Don't add to totalHashrate here to avoid infinite accumulation
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
  slaves[slaveCount].totalHashrate = 0;
  slaves[slaveCount].samplesCount = 0;
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

// Fetch multiple jobs from node
void fetchJobs() {
  Serial.println("Fetching 10 jobs from node...");
  
  for(int i = 0; i < MAX_JOBS; i++) {
    // Request new job
    masterJob->askForJob();
    
    // Store job
    if(masterJob->getCurrentJob(jobs[i].lastBlockHash, jobs[i].expectedHash, jobs[i].difficulty)) {
      jobs[i].jobId = currentJobId++;
      jobs[i].active = true;
      jobs[i].assignedTo = 255; // Not assigned
      
      Serial.printf("Job %d: diff=%lu, hash=%s\n", 
                    jobs[i].jobId, jobs[i].difficulty, jobs[i].lastBlockHash);
    } else {
      jobs[i].active = false;
      Serial.println("Failed to fetch job");
    }
    
    delay(100); // Small delay between requests
  }
}

// Distribute jobs to slaves (round-robin, no duplicate)
void distributeJobs() {
  if(slaveCount == 0) return;
  
  // Reset assignments
  for(int i = 0; i < MAX_JOBS; i++) {
    if(jobs[i].active) {
      jobs[i].assignedTo = 255;
    }
  }
  
  // Assign jobs to slaves (round-robin)
  uint32_t slaveIndex = 0;
  for(int i = 0; i < MAX_JOBS; i++) {
    if(jobs[i].active && slaveIndex < slaveCount) {
      jobs[i].assignedTo = slaveIndex;
      slaveIndex++;
    }
  }
  
  // Send jobs to slaves
  for(int i = 0; i < slaveCount; i++) {
    // Find job assigned to this slave
    for(int j = 0; j < MAX_JOBS; j++) {
      if(jobs[j].active && jobs[j].assignedTo == i) {
        struct_job jobToSend;
        jobToSend.msgType = 1;
        jobToSend.jobId = jobs[j].jobId;
        strcpy(jobToSend.lastBlockHash, jobs[j].lastBlockHash);
        strcpy(jobToSend.expectedHash, jobs[j].expectedHash);
        jobToSend.difficulty = jobs[j].difficulty;
        
        esp_err_t result = esp_now_send(slaves[i].mac, (uint8_t *)&jobToSend, sizeof(jobToSend));
        
        if(result == ESP_OK) {
          slaves[i].jobId = jobs[j].jobId;
          #if defined(SERIAL_PRINTING)
            Serial.printf("Sent job %lu to slave %d\n", jobs[j].jobId, i);
          #endif
        }
        break;
      }
    }
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

// Calculate total hashrate (average over last minute)
void updateTotalHashrate() {
  totalHashrate = 0;
  for(int i = 0; i < slaveCount; i++) {
    totalHashrate += slaves[i].hashrate;
  }
}

// Print slave status
void printStatus() {
  updateTotalHashrate();
  
  Serial.println("\n=== Slave Status ===");
  Serial.printf("Total slaves: %d\n", slaveCount);
  Serial.printf("Total shares: %lu\n", totalShares);
  Serial.printf("Total accepted: %lu\n", totalAccepted);
  Serial.printf("Accept rate: %.2f%%\n", 
                totalShares > 0 ? (totalAccepted * 100.0 / totalShares) : 0);
  Serial.printf("Total hashrate: %.2f kH/s\n", totalHashrate / 1000);
  Serial.println("\nSlaves:");
  
  for(int i = 0; i < slaveCount; i++) {
    Serial.printf("  Slave %d: HR=%.2f kH/s, Shares=%lu, Accepted=%lu, Job=%lu, Active=%d\n",
                  i+1, slaves[i].hashrate / 1000, slaves[i].shares, 
                  slaves[i].acceptedShares, slaves[i].jobId,
                  (millis() - slaves[i].lastSeen) < 10000);
  }
  
  Serial.println("\nJobs:");
  for(int i = 0; i < MAX_JOBS; i++) {
    if(jobs[i].active) {
      Serial.printf("  Job %lu: diff=%lu, assigned to slave %d\n",
                    jobs[i].jobId, jobs[i].difficulty, jobs[i].assignedTo);
    }
  }
  Serial.println("===================\n");
}

// Setup WiFi and connection to Duino-Coin node
void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  
  int attempts = 0;
  while(WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
    Serial.println("IP address: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi connection failed!");
  }
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
  
  // Fetch 10 jobs from node
  fetchJobs();
  
  // Distribute jobs to slaves
  distributeJobs();
  
  pinMode(LED_BUILTIN, OUTPUT);
  blink(BLINK_SETUP_COMPLETE);
  
  Serial.println("Master ready, managing slaves...");
}

unsigned long lastSync = 0;
unsigned long lastStatus = 0;
unsigned long lastJobRefresh = 0;

void loop() {
  // Master also mines on core 1 (optional)
  masterJob->mine();
  
  // Update total hashrate periodically (without accumulation)
  if(millis() - lastStatsReset > 10000) {
    updateTotalHashrate();
    lastStatsReset = millis();
  }
  
  // Broadcast new jobs periodically
  if(millis() - lastSync > SYNC_INTERVAL) {
    // Refresh jobs from node
    fetchJobs();
    distributeJobs();
    lastSync = millis();
  }
  
  // Print status every 30 seconds
  if(millis() - lastStatus > 30000) {
    printStatus();
    lastStatus = millis();
  }
  
  // Refresh jobs every 5 minutes
  if(millis() - lastJobRefresh > 300000) {
    fetchJobs();
    distributeJobs();
    lastJobRefresh = millis();
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
