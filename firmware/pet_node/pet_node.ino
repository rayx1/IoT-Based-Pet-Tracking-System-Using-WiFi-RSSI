#include <ESP8266WiFi.h>
#include <espnow.h>

// Optional local credentials/config file.
// Copy secrets.example.h to secrets.h, edit it, and keep secrets.h out of Git.
#if __has_include("secrets.h")
  #include "secrets.h"
#endif

// =========================
// User configuration
// Edit these placeholders, or define the same names in secrets.h.
// =========================
#ifndef WIFI_SSID_VALUE
#define WIFI_SSID_VALUE "YOUR_WIFI_SSID"
#endif

#ifndef WIFI_PASSWORD_VALUE
#define WIFI_PASSWORD_VALUE "YOUR_WIFI_PASSWORD"
#endif

#ifndef PET_ID_VALUE
#define PET_ID_VALUE "PET-001"
#endif

#ifndef HOME_NODE_MAC_BYTES
// Replace this with the MAC printed by the Home Node sketch.
#define HOME_NODE_MAC_BYTES {0x84, 0xF3, 0xEB, 0x12, 0x34, 0x56}
#endif

const char *WIFI_SSID = WIFI_SSID_VALUE;
const char *WIFI_PASSWORD = WIFI_PASSWORD_VALUE;
const char *PET_ID = PET_ID_VALUE;
uint8_t HOME_NODE_MAC[] = HOME_NODE_MAC_BYTES;

const uint32_t PACKET_MAGIC = 0x50544731; // "PTG1"
const uint8_t PROTOCOL_VERSION = 1;

const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
const unsigned long SEND_INTERVAL_MS = 2000;

// Set to true after wiring A0 through a safe resistor divider.
const bool USE_ADC_BATTERY_READING = false;
const float ADC_REFERENCE_VOLTAGE = 3.30f;
const float BATTERY_DIVIDER_RATIO = 2.00f;

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

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connect failed. Pet node will retry later.");
    return false;
  }

  lockedChannel = WiFi.channel();
  Serial.print("Pet node connected. IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("Locked WiFi channel: ");
  Serial.println(lockedChannel);
  printMacAddress();
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
  if (!USE_ADC_BATTERY_READING) {
    return 4.00f;
  }

  int rawAdc = analogRead(A0);
  float pinVoltage = (rawAdc / 1023.0f) * ADC_REFERENCE_VOLTAGE;
  return pinVoltage * BATTERY_DIVIDER_RATIO;
}

void preparePacket() {
  memset(&outgoingPacket, 0, sizeof(outgoingPacket));
  outgoingPacket.magic = PACKET_MAGIC;
  outgoingPacket.protocolVersion = PROTOCOL_VERSION;
  strncpy(outgoingPacket.petId, PET_ID, sizeof(outgoingPacket.petId) - 1);
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
    wifiReady = false;
    espNowReady = false;
    ESP.restart();
  }

  if (millis() - lastSendAtMs >= SEND_INTERVAL_MS) {
    lastSendAtMs = millis();
    sendPacket();
  }
}
