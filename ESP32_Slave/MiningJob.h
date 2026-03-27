#pragma GCC optimize("-Ofast")

#ifndef MINING_JOB_H
#define MINING_JOB_H

#include <Arduino.h>
#include <assert.h>
#include <string.h>
#include <Ticker.h>
#include <WiFiClient.h>
#include <WiFi.h>

#include "DSHA1.h"
#include "Counter.h"
#include "Settings.h"

// Base36 conversion table
const char base36Chars[36] PROGMEM = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'
};

const uint8_t base36CharValues[75] PROGMEM{
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0, 0,
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 
    26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 0, 0, 0, 0, 0, 0,
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 
    26, 27, 28, 29, 30, 31, 32, 33, 34, 35
};

#define SPC_TOKEN ' '
#define END_TOKEN '\n'
#define SEP_TOKEN ','
#define IOT_TOKEN '@'

struct MiningConfig {
    String host = "";
    int port = 0;
    String DUCO_USER = "";
    String RIG_IDENTIFIER = "";
    String MINER_KEY = "";
    String MINER_VER = SOFTWARE_VERSION;
    
    #if defined(ESP8266)
        String START_DIFF = "ESP8266H";
    #elif defined(CONFIG_FREERTOS_UNICORE)
        String START_DIFF = "ESP32S";
    #else
        String START_DIFF = "ESP32";
    #endif

    MiningConfig(String DUCO_USER, String RIG_IDENTIFIER, String MINER_KEY)
            : DUCO_USER(DUCO_USER), RIG_IDENTIFIER(RIG_IDENTIFIER), MINER_KEY(MINER_KEY) {}
};

class MiningJob {

public:
    MiningConfig *config;
    int core = 0;
    bool lastShareAccepted = false;

    MiningJob(int core, MiningConfig *config) {
        this->core = core;
        this->config = config;
        this->client_buffer = "";
        this->lastShareAccepted = false;
        dsha1 = new DSHA1();
        dsha1->warmup();
        generateRigIdentifier();
        initMining();
    }

    ~MiningJob() {
        if(dsha1) delete dsha1;
        if(client.connected()) client.stop();
    }

    void blink(uint8_t count, uint8_t pin = LED_BUILTIN) {
        #if defined(LED_BLINKING)
            for (int x = 0; x < count; ++x) {
                digitalWrite(pin, LOW);
                delay(100);
                digitalWrite(pin, HIGH);
                delay(100);
            }
        #else
            digitalWrite(LED_BUILTIN, HIGH);
        #endif
    }

    bool max_micros_elapsed(unsigned long current, unsigned long max_elapsed) {
        static unsigned long _start = 0;
        if ((current - _start) > max_elapsed) {
            _start = current;
            return true;
        }
        return false;
    }

    void handleSystemEvents(void) {
        #if defined(ESP32)
            esp_task_wdt_reset();
        #endif
        delay(1);
        yield();
        #if defined(ESP8266)
            ArduinoOTA.handle();
        #endif
    }

    void connectToNode() {
        if (client.connected()) return;

        unsigned int stopWatch = millis();
        #if defined(SERIAL_PRINTING)
            Serial.println("Core [" + String(core) + "] - Connecting to node " + 
                          config->host + ":" + String(config->port));
        #endif
        
        while (!client.connect(config->host.c_str(), config->port)) {
            if (max_micros_elapsed(micros(), 100000)) {
                handleSystemEvents();
            }
            if (millis() - stopWatch > 10000) {
                #if defined(SERIAL_PRINTING)
                    Serial.println("Connection timeout, retrying...");
                #endif
                return;
            }
        }
        
        waitForClientData();
        #if defined(SERIAL_PRINTING)
            Serial.println("Core [" + String(core) + "] - Connected. Server: " + client_buffer);
        #endif

        blink(BLINK_CLIENT_CONNECT);
    }

    void askForJob() {
        #if defined(SERIAL_PRINTING)
            Serial.println("Core [" + String(core) + "] - Requesting job for user: " + config->DUCO_USER);
        #endif

        #if defined(USE_DS18B20)
            sensors.requestTemperatures(); 
            float temp = sensors.getTempCByIndex(0);
            client.print("JOB," + config->DUCO_USER + SEP_TOKEN + config->START_DIFF + 
                        SEP_TOKEN + config->MINER_KEY + SEP_TOKEN + "Temp:" + String(temp) + "*C" + END_TOKEN);
        #elif defined(USE_DHT)
            float temp = dht.readTemperature();
            float hum = dht.readHumidity();
            client.print("JOB," + config->DUCO_USER + SEP_TOKEN + config->START_DIFF + 
                        SEP_TOKEN + config->MINER_KEY + SEP_TOKEN + "Temp:" + String(temp) + "*C" +
                        IOT_TOKEN + "Hum:" + String(hum) + "%" + END_TOKEN);
        #elif defined(USE_HSU07M)
            float temp = read_hsu07m();
            client.print("JOB," + config->DUCO_USER + SEP_TOKEN + config->START_DIFF + 
                        SEP_TOKEN + config->MINER_KEY + SEP_TOKEN + "Temp:" + String(temp) + "*C" + END_TOKEN);
        #elif defined(USE_INTERNAL_SENSOR)
            float temp = 0;
            temp_sensor_read_celsius(&temp);
            client.print("JOB," + config->DUCO_USER + SEP_TOKEN + config->START_DIFF + 
                        SEP_TOKEN + config->MINER_KEY + SEP_TOKEN + "CPU Temp:" + String(temp) + "*C" + END_TOKEN);
        #else
            client.print("JOB," + config->DUCO_USER + SEP_TOKEN + config->START_DIFF + 
                        SEP_TOKEN + config->MINER_KEY + END_TOKEN);
        #endif

        waitForClientData();
        
        #if defined(SERIAL_PRINTING)
            Serial.println("Core [" + String(core) + "] - Job received: " + client_buffer);
        #endif

        parse();
        
        #if defined(SERIAL_PRINTING)
            Serial.println("Core [" + String(core) + "] - Job parsed: hash=" + last_block_hash + 
                          ", expected=" + expected_hash_str + ", diff=" + String(difficulty));
        #endif
    }

    void submit(unsigned long counter, float hashrate, float elapsed_time_s) {
        client.print(String(counter) + SEP_TOKEN + String(hashrate) + SEP_TOKEN + 
                    MINER_BANNER + SPC_TOKEN + config->MINER_VER + SEP_TOKEN + 
                    config->RIG_IDENTIFIER + SEP_TOKEN + "DUCOID" + String(chipID) + 
                    SEP_TOKEN + String(WALLET_ID) + END_TOKEN);

        unsigned long ping_start = millis();
        waitForClientData();
        ping = millis() - ping_start;

        lastShareAccepted = (client_buffer == "GOOD");
        if (lastShareAccepted) {
            accepted_share_count++;
        }

        #if defined(SERIAL_PRINTING)
            Serial.println("Core [" + String(core) + "] - " + client_buffer +
                          " share #" + String(share_count) + " (" + String(counter) + ")" +
                          " hashrate: " + String(hashrate / 1000, 2) + " kH/s (" +
                          String(elapsed_time_s) + "s) Ping: " + String(ping) + "ms " +
                          "(" + node_id + ")\n");
        #endif
    }

    void mine() {
        if(!client.connected()) {
            connectToNode();
            if(!client.connected()) return;
            askForJob();
        }
        
        dsha1->reset();
        dsha1->write((const unsigned char *)last_block_hash.c_str(), last_block_hash.length());

        int start_time = micros();
        max_micros_elapsed(start_time, 0);
        
        #if defined(LED_BLINKING)
            digitalWrite(LED_BUILTIN, LOW);
        #endif
        
        Counter<10> counter;
        for (; counter < difficulty; ++counter) {
            DSHA1 ctx = *dsha1;
            ctx.write((const unsigned char *)counter.c_str(), counter.strlen()).finalize(hashArray);
            
            #ifndef CONFIG_FREERTOS_UNICORE
                #if defined(ESP32)
                    #define SYSTEM_TIMEOUT 100000
                #else 
                    #define SYSTEM_TIMEOUT 500000
                #endif
                if (max_micros_elapsed(micros(), SYSTEM_TIMEOUT)) {
                    handleSystemEvents();
                } 
            #endif

            if (memcmp(expected_hash, hashArray, 20) == 0) {
                unsigned long elapsed_time = micros() - start_time;
                float elapsed_time_s = elapsed_time * .000001f;
                share_count++;

                #if defined(LED_BLINKING)
                    digitalWrite(LED_BUILTIN, HIGH);
                #endif

                float currentHashrate = counter / elapsed_time_s;
                if (core == 0) {
                    hashrate = currentHashrate;
                } else {
                    hashrate_core_two = currentHashrate;
                }
                
                submit(counter, currentHashrate, elapsed_time_s);
                
                #if defined(BLUSHYBOX)
                    gauge_set(hashrate + hashrate_core_two);
                #endif
                
                break;
            }
        }
        
        // Request new job after mining
        askForJob();
    }

    // Methods for Master node
    bool getCurrentJob(char* lastBlockHashOut, char* expectedHashOut, uint32_t& difficultyOut) {
        if(last_block_hash.length() > 0 && expected_hash_str.length() > 0) {
            strcpy(lastBlockHashOut, last_block_hash.c_str());
            strcpy(expectedHashOut, expected_hash_str.c_str());
            difficultyOut = difficulty;
            return true;
        }
        return false;
    }
    
    float getHashrate() {
        return hashrate;
    }
    
    bool isLastShareAccepted() {
        return lastShareAccepted;
    }
    
    void setJob(String lastHash, String expected, uint32_t diff) {
        last_block_hash = lastHash;
        expected_hash_str = expected;
        difficulty = diff;
        
        // Convert expected hash string to byte array
        for(int i = 0; i < 20; i++) {
            char hex[3] = {expected_hash_str[i*2], expected_hash_str[i*2+1], 0};
            expected_hash[i] = strtol(hex, NULL, 16);
        }
        
        dsha1->reset();
        dsha1->write((const unsigned char*)last_block_hash.c_str(), last_block_hash.length());
    }
    
    void initMining() {
        // Initialize mining variables
        hashrate = 0;
        difficulty = 0;
        share_count = 0;
        accepted_share_count = 0;
        ping = 0;
        lastShareAccepted = false;
    }

protected:
    String client_buffer;
    uint8_t hashArray[20];
    String last_block_hash;
    String expected_hash_str;
    uint8_t expected_hash[20];
    DSHA1 *dsha1;
    WiFiClient client;
    String chipID = "";
    float hashrate = 0;
    uint32_t difficulty = 0;
    uint32_t share_count_local = 0;
    uint32_t accepted_share_count_local = 0;

    #if defined(ESP8266)
        #if defined(BLUSHYBOX)
            String MINER_BANNER = "Official BlushyBox Miner (ESP8266)";
        #else
            String MINER_BANNER = "Official ESP8266 Miner";
        #endif
    #elif defined(CONFIG_FREERTOS_UNICORE)
        String MINER_BANNER = "Official ESP32-S2 Miner";
    #else
        #if defined(BLUSHYBOX)
            String MINER_BANNER = "Official BlushyBox Miner (ESP32)";
        #else
            String MINER_BANNER = "Official ESP32 Miner";
        #endif
    #endif

    uint8_t *hexStringToUint8Array(const String &hexString, uint8_t *uint8Array, const uint32_t arrayLength) {
        assert(hexString.length() >= arrayLength * 2);
        const char *hexChars = hexString.c_str();
        for (uint32_t i = 0; i < arrayLength; ++i) {
            uint8Array[i] = (pgm_read_byte(base36CharValues + hexChars[i * 2] - '0') << 4) + 
                            pgm_read_byte(base36CharValues + hexChars[i * 2 + 1] - '0');
        }
        return uint8Array;
    }

    void generateRigIdentifier() {
        String AutoRigName = "";

        #if defined(ESP8266)
            chipID = String(ESP.getChipId(), HEX);
            if (strcmp(config->RIG_IDENTIFIER.c_str(), "Auto") != 0)
                return;
            AutoRigName = "ESP8266-" + chipID;
            AutoRigName.toUpperCase();
            config->RIG_IDENTIFIER = AutoRigName.c_str();
        #else
            uint64_t chip_id = ESP.getEfuseMac();
            uint16_t chip = (uint16_t)(chip_id >> 32);
            char fullChip[23];
            snprintf(fullChip, 23, "%04X%08X", chip, (uint32_t)chip_id);
            chipID = String(fullChip);

            if (strcmp(config->RIG_IDENTIFIER.c_str(), "Auto") != 0)
                return;
            AutoRigName = "ESP32-" + String(fullChip);
            AutoRigName.toUpperCase();
            config->RIG_IDENTIFIER = AutoRigName.c_str();
        #endif 
        
        #if defined(SERIAL_PRINTING)
            Serial.println("Core [" + String(core) + "] - Rig identifier: " + config->RIG_IDENTIFIER);
        #endif
    }

    void waitForClientData() {
        client_buffer = "";
        unsigned int stopWatch = millis();
        while (client.connected()) {
            if (client.available()) {
                client_buffer = client.readStringUntil(END_TOKEN);
                if (client_buffer.length() == 1 && client_buffer[0] == END_TOKEN)
                    client_buffer = "???\n";
                break;
            }
            if (max_micros_elapsed(micros(), 100000)) {
                handleSystemEvents();
            }
            if (millis() - stopWatch > 5000) {
                #if defined(SERIAL_PRINTING)
                    Serial.println("Timeout waiting for server data");
                #endif
                break;
            }
        }
    }

    bool parse() {
        char *job_str_copy = strdup(client_buffer.c_str());

        if (job_str_copy) {
            String tokens[3];
            char *token = strtok(job_str_copy, ",");
            for (int i = 0; token != NULL && i < 3; i++) {
                tokens[i] = token;
                token = strtok(NULL, ",");
            }

            if(tokens[0].length() > 0 && tokens[1].length() > 0) {
                last_block_hash = tokens[0];
                expected_hash_str = tokens[1];
                hexStringToUint8Array(expected_hash_str, expected_hash, 20);
                difficulty = tokens[2].toInt() * 100 + 1;
                
                free(job_str_copy);
                return true;
            }
        }
        
        free(job_str_copy);
        return false;
    }
};

#endif
