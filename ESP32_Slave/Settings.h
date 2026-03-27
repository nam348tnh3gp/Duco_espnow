// Settings.h - Slave
#ifndef SETTINGS_H
#define SETTINGS_H

// ---------------------- General settings ---------------------- //
#define MASTER_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}  // Thay bằng MAC của master
#define SLAVE_ID 1  // ID từ 1-10
// -------------------------------------------------------------- //

// -------------------- Advanced options ------------------------ //
#define LED_BLINKING
#define SERIAL_PRINTING
#define SERIAL_BAUDRATE 115200
#define ESPNOW_CHANNEL 1
#define WATCHDOG_TIMEOUT 30000

// -------------------------------------------------------------- //

#define SOFTWARE_VERSION "4.3_ESPNOW_SLAVE"
#define BLINK_SETUP_COMPLETE 2

extern unsigned int hashrate = 0;
extern unsigned int difficulty = 0;
extern unsigned long share_count = 0;
extern unsigned long accepted_share_count = 0;
extern unsigned int ping = 0;

#ifndef LED_BUILTIN
  #define LED_BUILTIN 2
#endif

#endif
