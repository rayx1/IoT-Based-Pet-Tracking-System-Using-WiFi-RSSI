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

// SMTP DATE-header offset. Units depend on the ESP-Mail-Client version.
// Older builds expect seconds (19800 = UTC+5:30 IST).
// Newer builds expect hours (5.5    = UTC+5:30 IST).
// Set 0 for UTC if unsure.
#define SMTP_TIME_GMT_OFFSET_VALUE 0

// Pet roster. One entry per pet node:
//   { {mac bytes}, "PROTOCOL_PET_ID", "Display Name" }
// PROTOCOL_PET_ID must match the pet node's PET_ID_VALUE exactly.
//
// Single pet:
#define PET_NODE_LIST \
  { {0x84, 0xF3, 0xEB, 0xAA, 0xBB, 0xCC}, "PET-001", "Bella" }

// Multiple pets (uncomment and adjust):
// #define PET_NODE_LIST \
//   { {0x84, 0xF3, 0xEB, 0xAA, 0xBB, 0xCC}, "PET-001", "Bella" }, \
//   { {0x84, 0xF3, 0xEB, 0xAA, 0xBB, 0xCD}, "PET-002", "Max"   }, \
//   { {0x84, 0xF3, 0xEB, 0xAA, 0xBB, 0xCE}, "PET-003", "Coco"  }

// Optional shared token for the /silence endpoint. Leave empty to disable.
#define DASHBOARD_TOKEN_VALUE ""
