/*
   DuinoCoin ESP-NOW Slave Node
   Nhận job từ master và mining
*/

#pragma GCC optimize("-Ofast")

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
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

// Master MAC address
uint8_t masterMac[] = MASTER_MAC;

struct_message sendData;
struct_job currentJob;
bool hasJob = false;
uint32_t currentJobId = 0;

// Mining variables
DSHA1 *dsha1;
uint8_t hashArray[20];
String lastBlockHash;
String expectedHashStr;
uint8_t expectedHash[20];

// Statistics
unsigned long shares = 0;
unsigned long acceptedShares = 0;

// Callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  #if defined(SERIAL_PRINTING)
    if(status == ESP_NOW_SEND_SUCCESS) {
      // Optional: log success
    }
  #endif
}

// Callback when data is received
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  struct_job *job = (struct_job*)incomingData;
  
  if(job->msgType == 1) {
    // New job received
    currentJobId = job->jobId;
    strcpy(currentJob.lastBlockHash, job->lastBlockHash);
    strcpy(currentJob.expectedHash, job->expectedHash);
    currentJob.difficulty = job->difficulty;
    hasJob = true;
    
    // Parse job
    lastBlockHash = String(currentJob.lastBlockHash);
    expectedHashStr = String(currentJob.expectedHash);
    
    // Convert expected hash string to byte array
    for(int i = 0; i < 20; i++) {
      char hex[3] = {expectedHashStr[i*2], expectedHashStr[i*2+1], 0};
      expectedHash[i] = strtol(hex, NULL, 16);
    }
    
    difficulty = currentJob.difficulty;
    
    #if defined(SERIAL_PRINTING)
      Serial.printf("Slave %d received job #%lu, diff=%lu\n", 
                    SLAVE_ID, currentJobId, currentJob.difficulty);
    #endif
    
    // Reset SHA1 context
    dsha1->reset();
    dsha1->write((const unsigned char*)lastBlockHash.c_str(), lastBlockHash.length());
  }
}

void initESPNow() {
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  
  if(esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  
  // Add master peer
  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, masterMac, 6);
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.encrypt = false;
  
  if(esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add master peer");
  }
  
  Serial.println("ESP-NOW initialized");
}

void sendResult(uint32_t counter, float hashrate, float elapsedTime, bool accepted) {
  sendData.msgType = 0;
  sendData.slaveId = SLAVE_ID;
  sendData.counter = counter;
  sendData.hashrate = hashrate;
  sendData.elapsedTime = elapsedTime;
  sendData.isAccepted = accepted;
  sendData.jobId = currentJobId;
  
  esp_now_send(masterMac, (uint8_t*)&sendData, sizeof(sendData));
}

void mine() {
  if(!hasJob) return;
  
  int start_time = micros();
  Counter<10> counter;
  
  for(; counter < difficulty; ++counter) {
    DSHA1 ctx = *dsha1;
    ctx.write((const unsigned char*)counter.c_str(), counter.strlen()).finalize(hashArray);
    
    if(memcmp(expectedHash, hashArray, 20) == 0) {
      unsigned long elapsed_time = micros() - start_time;
      float elapsed_time_s = elapsed_time * 0.000001f;
      shares++;
      
      float currentHashrate = counter / elapsed_time_s;
      bool accepted = false;
      
      // Check if share is accepted (always true for slave, master will validate)
      accepted = true;
      
      if(accepted) {
        acceptedShares++;
      }
      
      hashrate = currentHashrate;
      
      #if defined(SERIAL_PRINTING)
        Serial.printf("Slave %d found share: counter=%lu, hr=%.2f kH/s, time=%.2fs\n",
                      SLAVE_ID, counter.getVal(), currentHashrate/1000, elapsed_time_s);
      #endif
      
      // Send result to master
      sendResult(counter.getVal(), currentHashrate, elapsed_time_s, accepted);
      
      // Blink LED
      #if defined(LED_BLINKING)
        digitalWrite(LED_BUILTIN, HIGH);
        delay(50);
        digitalWrite(LED_BUILTIN, LOW);
      #endif
      
      break;
    }
  }
}

void setup() {
  Serial.begin(SERIAL_BAUDRATE);
  Serial.printf("\n\nDuinoCoin ESP-NOW Slave Node %d v%s\n", SLAVE_ID, SOFTWARE_VERSION);
  
  // Initialize mining
  dsha1 = new DSHA1();
  dsha1->warmup();
  
  // Setup ESP-NOW
  initESPNow();
  
  pinMode(LED_BUILTIN, OUTPUT);
  blink(BLINK_SETUP_COMPLETE);
  
  Serial.println("Slave ready, waiting for jobs...");
}

void loop() {
  if(hasJob) {
    mine();
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
