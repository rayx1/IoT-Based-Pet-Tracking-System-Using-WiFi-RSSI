#pragma once

// Copy this file to secrets.h and edit the values.
// secrets.h is ignored by Git.

#define WIFI_SSID_VALUE "YOUR_WIFI_SSID"
#define WIFI_PASSWORD_VALUE "YOUR_WIFI_PASSWORD"

// Each pet node MUST use a unique PET_ID_VALUE. The Home Node validates
// incoming packets against the petId you list for this pet's MAC in the
// home_node PET_NODE_LIST. Use the same string in both places.
//   Pet 1: "PET-001"
//   Pet 2: "PET-002"
//   ... etc
#define PET_ID_VALUE "PET-001"

// Replace with the MAC printed by the Home Node Serial Monitor.
#define HOME_NODE_MAC_BYTES {0x84, 0xF3, 0xEB, 0x12, 0x34, 0x56}
