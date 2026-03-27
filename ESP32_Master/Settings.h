// Settings.h - Master
#ifndef SETTINGS_H
#define SETTINGS_H

// ---------------------- General settings ---------------------- //
extern char *DUCO_USER = "your_username";  // Thay bằng username của bạn
extern char *MINER_KEY = "None";           // Mining key (nếu có)
extern const char SSID[] = "your_wifi";    // WiFi name
extern const char PASSWORD[] = "your_pass"; // WiFi password
// -------------------------------------------------------------- //

// -------------------- Advanced options ------------------------ //
#define LED_BLINKING
#define SERIAL_PRINTING
#define SERIAL_BAUDRATE 115200

// ESP-NOW Configuration
#define ESPNOW_CHANNEL 1
#define MAX_SLAVES 9
#define SYNC_INTERVAL 30000  // Sync interval (ms)

// -------------------------------------------------------------- //

#define SOFTWARE_VERSION "4.3_ESPNOW"
#define BLINK_SETUP_COMPLETE 2
#define BLINK_CLIENT_CONNECT 5

extern unsigned int hashrate = 0;
extern unsigned int difficulty = 0;
extern unsigned long share_count = 0;
extern unsigned long accepted_share_count = 0;
extern String node_id = "";
extern String WALLET_ID = "";
extern unsigned int ping = 0;

#ifndef LED_BUILTIN
  #define LED_BUILTIN 2
#endif

#endif
