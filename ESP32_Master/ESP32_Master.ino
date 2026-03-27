/*
   DuinoCoin ESP-NOW Master Node
   Phân phối job cho slave nodes và tổng hợp kết quả
   Hỗ trợ 10 job khác nhau, phân phối không trùng lặp
   Tự động refresh job khi tất cả job đã được sử dụng
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
  bool completed;      // Đánh dấu job đã được mining xong
  uint32_t assignedTo;
  unsigned long assignedTime;
  unsigned long completedTime;
};

// Slave information
struct SlaveInfo {
  uint8_t mac[6];
  bool active;
  uint32_t lastSeen;
  uint32_t shares;
  uint32_t acceptedShares;
  float hashrate;
  float totalHashrate;
  uint32_t currentJobId;
  uint32_t samplesCount;
  unsigned long lastShareTime;
};

SlaveInfo slaves[MAX_SLAVES];
JobInfo jobs[MAX_JOBS];
uint8_t slaveCount = 0;
uint32_t nextJobId = 0;

// Mining job
MiningJob *masterJob;
struct_message myData;
struct_job currentJob;

// Statistics
unsigned long totalShares = 0;
unsigned long totalAccepted = 0;
float totalHashrate = 0;
unsigned long lastStatsReset = 0;

// Job management
uint32_t activeJobsCount = 0;
uint32_t completedJobsCount = 0;
unsigned long lastJobRefreshTime = 0;
bool refreshingJobs = false;

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
      slaves[i].lastShareTime = millis();
      
      // Update average hashrate
      slaves[i].totalHashrate += myData.hashrate;
      slaves[i].samplesCount++;
      slaves[i].hashrate = slaves[i].totalHashrate / slaves[i].samplesCount;
      
      if(myData.isAccepted) {
        slaves[i].acceptedShares++;
        totalAccepted++;
      }
      
      totalShares++;
      
      // Đánh dấu job đã được sử dụng
      for(int j = 0; j < MAX_JOBS; j++) {
        if(jobs[j].active && !jobs[j].completed && jobs[j].jobId == myData.jobId) {
          jobs[j].completed = true;
          jobs[j].completedTime = millis();
          completedJobsCount++;
          
          #if defined(SERIAL_PRINTING)
            Serial.printf("Job %lu completed by slave %d\n", myData.jobId, myData.slaveId);
          #endif
          break;
        }
      }
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
  slaves[slaveCount].currentJobId = 0;
  slaves[slaveCount].lastShareTime = millis();
  
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

// Fetch a single job from node
bool fetchSingleJob(uint32_t jobIndex) {
  Serial.printf("Fetching job %d/%d...\n", jobIndex + 1, MAX_JOBS);
  
  // Request new job
  masterJob->askForJob();
  
  // Store job
  if(masterJob->getCurrentJob(jobs[jobIndex].lastBlockHash, 
                               jobs[jobIndex].expectedHash, 
                               jobs[jobIndex].difficulty)) {
    jobs[jobIndex].jobId = nextJobId++;
    jobs[jobIndex].active = true;
    jobs[jobIndex].completed = false;
    jobs[jobIndex].assignedTo = 255;
    jobs[jobIndex].assignedTime = 0;
    jobs[jobIndex].completedTime = 0;
    
    Serial.printf("Job %lu fetched: diff=%lu, hash=%s\n", 
                  jobs[jobIndex].jobId, jobs[jobIndex].difficulty, 
                  jobs[jobIndex].lastBlockHash);
    return true;
  } else {
    jobs[jobIndex].active = false;
    Serial.println("Failed to fetch job");
    return false;
  }
}

// Fetch multiple jobs from node
void fetchJobs() {
  if(refreshingJobs) {
    Serial.println("Already refreshing jobs, skipping...");
    return;
  }
  
  refreshingJobs = true;
  Serial.println("\n=== Refreshing Jobs ===");
  
  // Reset job counters
  activeJobsCount = 0;
  completedJobsCount = 0;
  
  // Fetch 10 new jobs
  for(int i = 0; i < MAX_JOBS; i++) {
    if(fetchSingleJob(i)) {
      activeJobsCount++;
      delay(100); // Small delay between requests to avoid flooding
    }
  }
  
  lastJobRefreshTime = millis();
  refreshingJobs = false;
  
  Serial.printf("=== Fetched %d/%d jobs ===\n\n", activeJobsCount, MAX_JOBS);
}

// Check if all jobs are completed and need refresh
bool needJobRefresh() {
  // Nếu đang refresh thì không cần
  if(refreshingJobs) return false;
  
  // Nếu chưa có job nào active thì refresh ngay
  if(activeJobsCount == 0) return true;
  
  // Kiểm tra tất cả jobs đã completed chưa
  for(int i = 0; i < MAX_JOBS; i++) {
    if(jobs[i].active && !jobs[i].completed) {
      return false; // Còn job chưa hoàn thành
    }
  }
  
  // Tất cả jobs đã completed
  Serial.println("\n*** All jobs completed! Refreshing... ***");
  return true;
}

// Distribute jobs to slaves (round-robin, no duplicate)
void distributeJobs() {
  if(slaveCount == 0) {
    Serial.println("No slaves available to distribute jobs");
    return;
  }
  
  if(activeJobsCount == 0) {
    Serial.println("No active jobs to distribute");
    return;
  }
  
  Serial.println("\n=== Distributing Jobs ===");
  
  // Reset assignments
  for(int i = 0; i < MAX_JOBS; i++) {
    if(jobs[i].active && !jobs[i].completed) {
      jobs[i].assignedTo = 255;
    }
  }
  
  // Assign active jobs to slaves (round-robin)
  uint32_t slaveIndex = 0;
  uint32_t assignedCount = 0;
  
  for(int i = 0; i < MAX_JOBS; i++) {
    if(jobs[i].active && !jobs[i].completed && slaveIndex < slaveCount) {
      jobs[i].assignedTo = slaveIndex;
      jobs[i].assignedTime = millis();
      slaveIndex++;
      assignedCount++;
    }
  }
  
  // Send jobs to slaves
  for(int i = 0; i < slaveCount; i++) {
    // Find job assigned to this slave
    bool jobFound = false;
    for(int j = 0; j < MAX_JOBS; j++) {
      if(jobs[j].active && !jobs[j].completed && jobs[j].assignedTo == i) {
        struct_job jobToSend;
        jobToSend.msgType = 1;
        jobToSend.jobId = jobs[j].jobId;
        strcpy(jobToSend.lastBlockHash, jobs[j].lastBlockHash);
        strcpy(jobToSend.expectedHash, jobs[j].expectedHash);
        jobToSend.difficulty = jobs[j].difficulty;
        
        esp_err_t result = esp_now_send(slaves[i].mac, (uint8_t *)&jobToSend, sizeof(jobToSend));
        
        if(result == ESP_OK) {
          slaves[i].currentJobId = jobs[j].jobId;
          Serial.printf("Sent job %lu to slave %d (diff=%lu)\n", 
                        jobs[j].jobId, i, jobs[j].difficulty);
          jobFound = true;
        } else {
          Serial.printf("Failed to send job %lu to slave %d\n", jobs[j].jobId, i);
        }
        break;
      }
    }
    
    if(!jobFound) {
      Serial.printf("No job available for slave %d\n", i);
    }
  }
  
  Serial.printf("=== Distributed %d/%d jobs ===\n\n", assignedCount, activeJobsCount);
}

// Calculate total hashrate (average over all slaves)
void updateTotalHashrate() {
  totalHashrate = 0;
  for(int i = 0; i < slaveCount; i++) {
    totalHashrate += slaves[i].hashrate;
  }
}

// Print slave status
void printStatus() {
  updateTotalHashrate();
  
  Serial.println("\n╔═══════════════════════════════════════════════════════════╗");
  Serial.println("║                    Slave Status Report                     ║");
  Serial.println("╚═══════════════════════════════════════════════════════════╝");
  Serial.printf("Total slaves: %d\n", slaveCount);
  Serial.printf("Total shares: %lu\n", totalShares);
  Serial.printf("Total accepted: %lu\n", totalAccepted);
  Serial.printf("Accept rate: %.2f%%\n", 
                totalShares > 0 ? (totalAccepted * 100.0 / totalShares) : 0);
  Serial.printf("Total hashrate: %.2f kH/s\n\n", totalHashrate / 1000);
  
  Serial.println("┌─────┬──────────────┬──────────────┬──────────┬──────────┐");
  Serial.println("│ ID  │  Hashrate    │   Shares     │ Accepted │   Job    │");
  Serial.println("├─────┼──────────────┼──────────────┼──────────┼──────────┤");
  
  for(int i = 0; i < slaveCount; i++) {
    bool active = (millis() - slaves[i].lastSeen) < 10000;
    Serial.printf("│ %3d │ %12.2f kH/s │ %12lu │ %8lu │ %8lu │%s\n",
                  i+1, slaves[i].hashrate / 1000, slaves[i].shares, 
                  slaves[i].acceptedShares, slaves[i].currentJobId,
                  active ? "" : " (inactive)");
  }
  Serial.println("└─────┴──────────────┴──────────────┴──────────┴──────────┘");
  
  Serial.println("\n┌─────────────────────────────────────────────────────────┐");
  Serial.println("│                    Job Status Report                    │");
  Serial.println("├──────┬──────────────────┬─────────────┬────────────────┤");
  Serial.println("│ Job  │ Difficulty       │ Status      │ Assigned To    │");
  Serial.println("├──────┼──────────────────┼─────────────┼────────────────┤");
  
  for(int i = 0; i < MAX_JOBS; i++) {
    if(jobs[i].active) {
      String status = jobs[i].completed ? "COMPLETED" : "ACTIVE";
      String assigned = jobs[i].assignedTo < slaveCount ? 
                        String("Slave ") + String(jobs[i].assignedTo + 1) : "Not assigned";
      Serial.printf("│ %4lu │ %14lu │ %11s │ %14s │\n",
                    jobs[i].jobId, jobs[i].difficulty, status.c_str(), assigned.c_str());
    }
  }
  Serial.println("└──────┴──────────────────┴─────────────┴────────────────┘");
  
  Serial.printf("\nJobs: %d active, %d completed, %d total\n", 
                activeJobsCount, completedJobsCount, MAX_JOBS);
  Serial.println("═══════════════════════════════════════════════════════════\n");
}

// Check for inactive slaves
void checkInactiveSlaves() {
  unsigned long now = millis();
  for(int i = 0; i < slaveCount; i++) {
    if(now - slaves[i].lastSeen > 30000) { // 30 seconds timeout
      if(slaves[i].active) {
        slaves[i].active = false;
        Serial.printf("Slave %d marked as inactive (no response for 30s)\n", i+1);
      }
    } else {
      if(!slaves[i].active) {
        slaves[i].active = true;
        Serial.printf("Slave %d is back online\n", i+1);
      }
    }
  }
}

// Setup WiFi and connection to Duino-Coin node
void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  
  int attempts = 0;
  Serial.print("Connecting to WiFi");
  while(WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi connected");
    Serial.println("  IP address: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n✗ WiFi connection failed!");
  }
}

void setup() {
  Serial.begin(SERIAL_BAUDRATE);
  Serial.println("\n\n╔═══════════════════════════════════════════════════════════╗");
  Serial.println("║     DuinoCoin ESP-NOW Master Node v" + String(SOFTWARE_VERSION) + "          ║");
  Serial.println("╚═══════════════════════════════════════════════════════════╝");
  
  // Setup WiFi and node connection
  setupWiFi();
  
  // Initialize master miner (core 0)
  MiningConfig *config = new MiningConfig(DUCO_USER, "Master", MINER_KEY);
  masterJob = new MiningJob(0, config);
  masterJob->connectToNode();
  
  // Initialize ESP-NOW
  initESPNow();
  
  // Wait for slaves to register
  delay(2000);
  
  // Fetch initial 10 jobs
  fetchJobs();
  
  // Distribute jobs to slaves
  distributeJobs();
  
  pinMode(LED_BUILTIN, OUTPUT);
  blink(BLINK_SETUP_COMPLETE);
  
  Serial.println("\n✓ Master ready, managing slaves...");
  Serial.println("  - Will refresh jobs automatically when all jobs are completed\n");
}

unsigned long lastStatus = 0;
unsigned long lastInactiveCheck = 0;

void loop() {
  // Master also mines on core 1 (optional)
  masterJob->mine();
  
  // Update total hashrate periodically
  if(millis() - lastStatsReset > 10000) {
    updateTotalHashrate();
    lastStatsReset = millis();
  }
  
  // Check for inactive slaves
  if(millis() - lastInactiveCheck > 5000) {
    checkInactiveSlaves();
    lastInactiveCheck = millis();
  }
  
  // Kiểm tra và refresh job ngay khi tất cả jobs đã completed
  if(needJobRefresh()) {
    Serial.println("\n🔄 REFRESHING JOBS - All jobs completed!");
    fetchJobs();           // Lấy 10 job mới
    distributeJobs();      // Phân phối lại cho slaves
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
