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
  Serial.println("-----------------------------");
}

void onDataSent(uint8_t *macAddr, uint8_t sendStatus) {
  Serial.print("ESP-NOW send status: ");
  Serial.println(sendStatus == 0 ? "Delivery success" : "Delivery fail");
}

bool connectToWifiAndLockChannel() {
  Serial.println("Connecting to WiFi to lock router channel...");

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.disconnect();
  delay(50);

  WiFi.begin(WIFI_SSID_VALUE, WIFI_PASSWORD_VALUE);

  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < WIFI_CONNECT_TIMEOUT_MS_VALUE) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connect failed. Pet node will retry later.");
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
  return true;
}

bool initEspNow() {
  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW init failed on pet node.");
    return false;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_register_send_cb(onDataSent);

  if (esp_now_add_peer(HOME_NODE_MAC, ESP_NOW_ROLE_SLAVE, lockedChannel, NULL, 0) != 0) {
    Serial.println("Failed to add Home Node as ESP-NOW peer.");
    return false;
  }

  Serial.println("ESP-NOW initialized on pet node.");
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
    Serial.println("WiFi connection lost. Restarting for clean channel lock...");
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
}
