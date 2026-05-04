#include <ESP8266WiFi.h>
#include <espnow.h>

// All user-editable config (WiFi, pet ID, home node MAC) lives in secrets.h.
// Edit firmware/pet_node/secrets.h with your values.
// secrets.h is gitignored so your credentials never reach the repo.
#if !__has_include("secrets.h")
  #error "secrets.h is missing from firmware/pet_node/. See README for the required macros."
#endif
#include "secrets.h"

uint8_t HOME_NODE_MAC[] = HOME_NODE_MAC_BYTES;

const uint32_t PACKET_MAGIC = 0x50544731; // "PTG1"
const uint8_t PROTOCOL_VERSION = 1;

// Packet structure sent over ESP-NOW.
// Keep this identical on both nodes.
struct PetPacket {
  uint32_t magic;
  uint8_t protocolVersion;
  char petId[16];
  uint32_t packetCounter;
  float batteryVoltage;
  uint32_t uptimeSeconds;
};

PetPacket outgoingPacket;
uint32_t packetCounter = 0;
unsigned long lastSendAtMs = 0;
unsigned long lastWifiRetryAtMs = 0;
bool espNowReady = false;
bool wifiReady = false;
uint8_t lockedChannel = 0;
uint8_t consecutiveDeliveryFails = 0;

const char *wifiStatusName(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS:
      return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL:
      return "WL_NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED:
      return "WL_SCAN_COMPLETED";
    case WL_CONNECTED:
      return "WL_CONNECTED";
    case WL_CONNECT_FAILED:
      return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST:
      return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED:
      return "WL_DISCONNECTED";
    default:
      return "UNKNOWN";
  }
}

String formatMac(const uint8_t *mac) {
  char buffer[18];
  snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buffer);
}

void printMacAddress() {
  Serial.print("Pet Node MAC: ");
  Serial.println(WiFi.macAddress());
}

void printPetNodeIdentity() {
  Serial.println();
  Serial.println("----- Pet Node Identity -----");
  Serial.print("Pet ID: ");
  Serial.println(PET_ID_VALUE);
  Serial.print("Pet Node MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Pet Node IP: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "not connected yet");
  Serial.print("WiFi status: ");
  wl_status_t status = WiFi.status();
  Serial.print(wifiStatusName(status));
  Serial.print(" (");
  Serial.print(static_cast<int>(status));
  Serial.println(")");
  Serial.print("Target SSID: ");
  Serial.println(WIFI_SSID_VALUE);
  Serial.print("Home Node peer MAC: ");
  Serial.println(formatMac(HOME_NODE_MAC));
  Serial.println("-----------------------------");
}

void onDataSent(uint8_t *macAddr, uint8_t sendStatus) {
  Serial.print("ESP-NOW send status: ");
  if (sendStatus == 0) {
    consecutiveDeliveryFails = 0;
    Serial.println("Delivery success");
    return;
  }

  consecutiveDeliveryFails++;
  Serial.println("Delivery fail");
  Serial.print("Failed peer MAC: ");
  Serial.println(formatMac(macAddr));
  Serial.print("Configured Home Node MAC: ");
  Serial.println(formatMac(HOME_NODE_MAC));
  Serial.print("WiFi channel: ");
  Serial.print(WiFi.channel());
  Serial.print(", ESP-NOW peer channel: ");
  Serial.println(ESPNOW_CHANNEL_VALUE == 0 ? lockedChannel : ESPNOW_CHANNEL_VALUE);
  Serial.print("Consecutive delivery fails: ");
  Serial.println(consecutiveDeliveryFails);
}

bool connectToWifiAndLockChannel() {
  Serial.println("Connecting to WiFi to lock router channel...");
  Serial.print("Target SSID: ");
  Serial.println(WIFI_SSID_VALUE);
  Serial.print("Pet Node MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Pet Node IP before connect: ");
  Serial.println(WiFi.localIP().toString());

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.disconnect();
  delay(50);

  WiFi.begin(WIFI_SSID_VALUE, WIFI_PASSWORD_VALUE);

  unsigned long startMs = millis();
  wl_status_t lastStatus = WiFi.status();
  Serial.print("Initial WiFi status: ");
  Serial.print(wifiStatusName(lastStatus));
  Serial.print(" (");
  Serial.print(static_cast<int>(lastStatus));
  Serial.println(")");

  while (WiFi.status() != WL_CONNECTED && millis() - startMs < WIFI_CONNECT_TIMEOUT_MS_VALUE) {
    delay(250);
    wl_status_t currentStatus = WiFi.status();
    if (currentStatus != lastStatus) {
      Serial.println();
      Serial.print("WiFi status changed: ");
      Serial.print(wifiStatusName(currentStatus));
      Serial.print(" (");
      Serial.print(static_cast<int>(currentStatus));
      Serial.println(")");
      lastStatus = currentStatus;
    }
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    wl_status_t finalStatus = WiFi.status();
    Serial.println("WiFi connect failed. Pet node will retry later.");
    Serial.print("Final WiFi status: ");
    Serial.print(wifiStatusName(finalStatus));
    Serial.print(" (");
    Serial.print(static_cast<int>(finalStatus));
    Serial.println(")");
    Serial.print("Pet Node MAC: ");
    Serial.println(WiFi.macAddress());
    Serial.print("Pet Node IP: ");
    Serial.println(WiFi.localIP().toString());
    return false;
  }

  lockedChannel = WiFi.channel();
  Serial.print("Pet Node connected. IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("Pet Node MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Locked WiFi channel: ");
  Serial.println(lockedChannel);
  Serial.print("Final WiFi status: ");
  Serial.print(wifiStatusName(WiFi.status()));
  Serial.print(" (");
  Serial.print(static_cast<int>(WiFi.status()));
  Serial.println(")");
  return true;
}

bool initEspNow() {
  esp_now_deinit();
  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW init failed on pet node.");
    return false;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(onDataSent);
  uint8_t peerChannel = ESPNOW_CHANNEL_VALUE == 0 ? lockedChannel : ESPNOW_CHANNEL_VALUE;

  if (esp_now_add_peer(HOME_NODE_MAC, ESP_NOW_ROLE_COMBO, peerChannel, NULL, 0) != 0) {
    Serial.println("Failed to add Home Node as ESP-NOW peer.");
    return false;
  }

  Serial.println("ESP-NOW initialized on pet node.");
  Serial.print("ESP-NOW self role: COMBO, peer channel: ");
  Serial.println(peerChannel);
  Serial.print("Home Node peer MAC: ");
  Serial.println(formatMac(HOME_NODE_MAC));
  return true;
}

float readBatteryVoltage() {
  if (!USE_ADC_BATTERY_READING_VALUE) {
    return 4.00f;
  }

  int rawAdc = analogRead(A0);
  float pinVoltage = (rawAdc / 1023.0f) * ADC_REFERENCE_VOLTAGE_VALUE;
  return pinVoltage * BATTERY_DIVIDER_RATIO_VALUE;
}

void preparePacket() {
  memset(&outgoingPacket, 0, sizeof(outgoingPacket));
  outgoingPacket.magic = PACKET_MAGIC;
  outgoingPacket.protocolVersion = PROTOCOL_VERSION;
  strncpy(outgoingPacket.petId, PET_ID_VALUE, sizeof(outgoingPacket.petId) - 1);
  outgoingPacket.packetCounter = packetCounter++;
  outgoingPacket.batteryVoltage = readBatteryVoltage();
  outgoingPacket.uptimeSeconds = millis() / 1000UL;
}

void sendPacket() {
  if (!espNowReady) {
    Serial.println("Skip send: ESP-NOW not ready.");
    return;
  }

  preparePacket();

  uint8_t result = esp_now_send(HOME_NODE_MAC, reinterpret_cast<uint8_t *>(&outgoingPacket), sizeof(outgoingPacket));

  Serial.print("Packet #");
  Serial.print(outgoingPacket.packetCounter);
  Serial.print(" sent, result code: ");
  Serial.print(result);
  Serial.print(", battery=");
  Serial.print(outgoingPacket.batteryVoltage, 2);
  Serial.println(" V");
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== Pet Node Boot ===");
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  printPetNodeIdentity();

  wifiReady = connectToWifiAndLockChannel();
  if (wifiReady) {
    espNowReady = initEspNow();
  }
}

void loop() {
  if (!wifiReady && millis() - lastWifiRetryAtMs >= 5000) {
    lastWifiRetryAtMs = millis();
    wifiReady = connectToWifiAndLockChannel();
    if (wifiReady && !espNowReady) {
      espNowReady = initEspNow();
    }
  }

  if (wifiReady && WiFi.status() != WL_CONNECTED) {
    wl_status_t droppedStatus = WiFi.status();
    Serial.println("WiFi connection lost. Restarting for clean channel lock...");
    Serial.print("Dropped WiFi status: ");
    Serial.print(wifiStatusName(droppedStatus));
    Serial.print(" (");
    Serial.print(static_cast<int>(droppedStatus));
    Serial.println(")");
    Serial.print("Pet Node MAC: ");
    Serial.println(WiFi.macAddress());
    Serial.print("Pet Node last known IP: ");
    Serial.println(WiFi.localIP().toString());
    wifiReady = false;
    espNowReady = false;
    ESP.restart();
  }

  if (millis() - lastSendAtMs >= SEND_INTERVAL_MS_VALUE) {
    lastSendAtMs = millis();
    sendPacket();
  }

  if (espNowReady && consecutiveDeliveryFails >= ESPNOW_REINIT_AFTER_FAILS_VALUE) {
    Serial.println("Too many ESP-NOW delivery failures. Reinitializing ESP-NOW peer.");
    consecutiveDeliveryFails = 0;
    espNowReady = initEspNow();
  }
}
