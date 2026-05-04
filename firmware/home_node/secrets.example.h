#pragma once

// Copy this file to secrets.h and edit the values.
// secrets.h is ignored by Git.

#define WIFI_SSID_VALUE "YOUR_WIFI_SSID"
#define WIFI_PASSWORD_VALUE "YOUR_WIFI_PASSWORD"

#define SMTP_HOST_VALUE "smtp.gmail.com"
#define SMTP_PORT_VALUE 465
#define SENDER_EMAIL_VALUE "sender@example.com"
#define SENDER_APP_PASSWORD_VALUE "YOUR_APP_PASSWORD"
#define RECIPIENT_EMAIL_VALUE "recipient@example.com"

// Optional dashboard token for POST /silence.
// Leave empty to disable token checking.
#define DASHBOARD_TOKEN_VALUE ""

// Main behavior tuning.
#define RSSI_THRESHOLD_DBM_VALUE -75
#define PACKET_TIMEOUT_MS_VALUE 10000UL
#define RSSI_FRESH_MS_VALUE 3000UL
#define EMAIL_COOLDOWN_MS_VALUE 300000UL
#define WIFI_CONNECT_TIMEOUT_MS_VALUE 15000UL
#define BUZZER_SILENCE_MS_VALUE 120000UL

// Replace each MAC with the value printed by that Pet Node Serial Monitor.
// Format: { {mac bytes}, "PROTOCOL_PET_ID", "Display Name" }
#define PET_NODE_LIST \
  { {0x84, 0xF3, 0xEB, 0xAA, 0xBB, 0xCC}, "PET-001", "Bella" }
