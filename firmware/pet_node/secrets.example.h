#pragma once

// Copy this file to secrets.h and edit the values.
// secrets.h is ignored by Git.

#define WIFI_SSID_VALUE "YOUR_WIFI_SSID"
#define WIFI_PASSWORD_VALUE "YOUR_WIFI_PASSWORD"
#define PET_ID_VALUE "PET-001"

// Replace with the MAC printed by the Home Node Serial Monitor.
#define HOME_NODE_MAC_BYTES {0x84, 0xF3, 0xEB, 0x12, 0x34, 0x56}

// Main behavior tuning.
#define WIFI_CONNECT_TIMEOUT_MS_VALUE 15000UL
#define SEND_INTERVAL_MS_VALUE 2000UL
#define ESPNOW_CHANNEL_VALUE 0
#define ESPNOW_REINIT_AFTER_FAILS_VALUE 5

// Battery reading is disabled until A0 is wired through a safe divider.
#define USE_ADC_BATTERY_READING_VALUE false
#define ADC_REFERENCE_VOLTAGE_VALUE 3.30f
#define BATTERY_DIVIDER_RATIO_VALUE 2.00f
