#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecure.h>
#include <espnow.h>

extern "C" {
  #include <user_interface.h>
}

// ESP8266 NONOS SDK promiscuous-callback metadata. Use a project-local type
// name so we do not collide with cores that already expose `RxControl`.
struct EspNowRxControlCompat {
  signed rssi : 8;
  unsigned rate : 4;
  unsigned is_group : 1;
  unsigned : 1;
  unsigned sig_mode : 2;
  unsigned legacy_length : 12;
  unsigned damatch0 : 1;
  unsigned damatch1 : 1;
  unsigned bssidmatch0 : 1;
  unsigned bssidmatch1 : 1;
  unsigned MCS : 7;
  unsigned CWB : 1;
  unsigned HT_length : 16;
  unsigned Smoothing : 1;
  unsigned Not_Sounding : 1;
  unsigned : 1;
  unsigned Aggregation : 1;
  unsigned STBC : 2;
  unsigned FEC_CODING : 1;
  unsigned SGI : 1;
  unsigned rxend_state : 8;
  unsigned ampdu_cnt : 8;
  unsigned channel : 4;
  unsigned : 12;
};

// All user-editable config (WiFi, SMTP, pet roster, dashboard token, GMT offset)
// lives in secrets.h. Edit firmware/home_node/secrets.h with your values.
// secrets.h is gitignored so your credentials never reach the repo.
#if !__has_include("secrets.h")
  #error "secrets.h is missing from firmware/home_node/. See README for the required macros."
#endif
#include "secrets.h"

const uint32_t PACKET_MAGIC = 0x50544731; // "PTG1"
const uint8_t PROTOCOL_VERSION = 1;
const uint8_t BUZZER_PIN = D5;

// Keep this struct identical to the pet node.
struct PetPacket {
  uint32_t magic;
  uint8_t protocolVersion;
  char petId[16];
  uint32_t packetCounter;
  float batteryVoltage;
  uint32_t uptimeSeconds;
};

struct PetConfig {
  uint8_t mac[6];
  const char *petId;
  const char *displayName;
};

const PetConfig PETS[] = { PET_NODE_LIST };
constexpr uint8_t PET_COUNT = sizeof(PETS) / sizeof(PETS[0]);

struct PetState {
  PetPacket lastPacket;
  bool packetReceived;
  unsigned long lastPacketAtMs;
  volatile int lastRssi;
  volatile unsigned long lastRssiAtMs;
  unsigned long lastEmailSentAtMs;
  bool alertConditionActive;
};

PetState petStates[PET_COUNT];

// Structure copied from ESP8266 promiscuous callback metadata.
struct PromiscuousPacket {
  EspNowRxControlCompat rx_ctrl;
  uint8_t payload[112];
};

ESP8266WebServer server(80);

bool espNowReady = false;
bool wifiReady = false;
bool buzzerOutputHigh = false;
bool emailConfigured = true;

unsigned long lastStatusLogAtMs = 0;
unsigned long lastBuzzerToggleAtMs = 0;
unsigned long buzzerSilencedUntilMs = 0;
unsigned long wifiDisconnectedSinceMs = 0;
unsigned long lastWifiRecoveryLogAtMs = 0;
unsigned long lastWifiIdleStatusLogAtMs = 0;
unsigned long lastEspNowHealthCheckAtMs = 0;
uint8_t wifiChannel = 0;
String homeIpString = "0.0.0.0";

String formatMac(const uint8_t *mac) {
  char buffer[18];
  snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buffer);
}

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

const char *encryptionName(uint8_t encryptionType) {
  switch (encryptionType) {
    case ENC_TYPE_NONE:
      return "open";
    case ENC_TYPE_WEP:
      return "WEP";
    case ENC_TYPE_TKIP:
      return "WPA/TKIP";
    case ENC_TYPE_CCMP:
      return "WPA2/CCMP";
    case ENC_TYPE_AUTO:
      return "auto";
    default:
      return "unknown";
  }
}

void printHomeNodeIdentity() {
  Serial.println();
  Serial.println("----- Home Node Identity -----");
  Serial.print("Home Node MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Home Node IP: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "not connected yet");
  Serial.print("Configured WiFi SSID: ");
  Serial.println(WIFI_SSID_VALUE);
  Serial.print("WiFi connect timeout: ");
  Serial.print(WIFI_CONNECT_TIMEOUT_MS_VALUE);
  Serial.println(" ms");
  Serial.println("------------------------------");
}

void printWifiScanResults(bool onlyConfiguredSsid) {
  if (onlyConfiguredSsid) {
    Serial.println("Scanning nearby 2.4 GHz WiFi networks for configured SSID...");
  } else {
    Serial.println("Initial WiFi scan: nearby 2.4 GHz networks");
  }

  int networkCount = WiFi.scanNetworks(false, true);
  if (networkCount < 0) {
    Serial.print("WiFi scan failed, result code: ");
    Serial.println(networkCount);
    return;
  }

  bool foundConfiguredSsid = false;
  Serial.print("Networks found: ");
  Serial.println(networkCount);

  for (int i = 0; i < networkCount; i++) {
    bool isConfiguredSsid = WiFi.SSID(i) == String(WIFI_SSID_VALUE);
    if (onlyConfiguredSsid && !isConfiguredSsid) {
      continue;
    }

    if (isConfiguredSsid) {
      foundConfiguredSsid = true;
    }

    Serial.print(isConfiguredSsid ? "* " : "  ");
    Serial.print("SSID=\"");
    Serial.print(WiFi.SSID(i));
    Serial.print("\", RSSI=");
    Serial.print(WiFi.RSSI(i));
    Serial.print(" dBm, channel=");
    Serial.print(WiFi.channel(i));
    Serial.print(", BSSID=");
    Serial.print(WiFi.BSSIDstr(i));
    Serial.print(", encryption=");
    Serial.println(encryptionName(WiFi.encryptionType(i)));
  }

  if (!foundConfiguredSsid) {
    Serial.println("Configured SSID was NOT found in scan results.");
    Serial.println("Check SSID spelling, 2.4 GHz availability, router range, and hidden-network settings.");
  }

  WiFi.scanDelete();
}

void printInitialWifiScan() {
  printWifiScanResults(false);
}

void scanForConfiguredWifi() {
  printWifiScanResults(true);
}

bool macEquals(const uint8_t *left, const uint8_t *right) {
  return memcmp(left, right, 6) == 0;
}

int findPetIndex(const uint8_t *mac) {
  for (uint8_t i = 0; i < PET_COUNT; i++) {
    if (macEquals(PETS[i].mac, mac)) {
      return i;
    }
  }
  return -1;
}

bool smtpConfigLooksFilled() {
  return String(SMTP_HOST_VALUE) != "smtp.example.com" &&
         String(SENDER_EMAIL_VALUE) != "sender@example.com" &&
         String(SENDER_APP_PASSWORD_VALUE) != "YOUR_APP_PASSWORD" &&
         String(RECIPIENT_EMAIL_VALUE) != "recipient@example.com";
}

bool hasNonZeroLocalIp() {
  IPAddress ip = WiFi.localIP();
  return ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0;
}

bool hasLastKnownIp() {
  return homeIpString.length() > 0 && homeIpString != "0.0.0.0";
}

bool connectToWifi(bool runInitialScan = true) {
  if (runInitialScan) {
    printInitialWifiScan();
  }

  Serial.println("Connecting Home Node to WiFi...");
  Serial.print("Target SSID: ");
  Serial.println(WIFI_SSID_VALUE);

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
    Serial.println("Home Node WiFi connect failed.");
    Serial.print("Final WiFi status: ");
    Serial.print(wifiStatusName(finalStatus));
    Serial.print(" (");
    Serial.print(static_cast<int>(finalStatus));
    Serial.println(")");
    Serial.print("Home Node MAC for router allow-list checks: ");
    Serial.println(WiFi.macAddress());
    Serial.print("Home Node IP: ");
    Serial.println(WiFi.localIP().toString());
    Serial.println("Common causes: wrong password, wrong SSID, 5 GHz-only SSID, weak signal, MAC filtering, or router channel changes.");
    scanForConfiguredWifi();
    return false;
  }

  wifiChannel = WiFi.channel();
  homeIpString = WiFi.localIP().toString();

  Serial.print("Home Node connected. IP: ");
  Serial.println(homeIpString);
  Serial.print("Home Node MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Home Node WiFi channel: ");
  Serial.println(wifiChannel);
  Serial.print("Tracking ");
  Serial.print(PET_COUNT);
  Serial.println(" pet(s):");
  for (uint8_t i = 0; i < PET_COUNT; i++) {
    Serial.print("  - ");
    Serial.print(PETS[i].displayName);
    Serial.print(" (");
    Serial.print(PETS[i].petId);
    Serial.print(") @ ");
    Serial.println(formatMac(PETS[i].mac));
  }
  return true;
}

void ICACHE_FLASH_ATTR promiscuousCallback(uint8_t *buffer, uint16_t length) {
  if (length < 36) {
    return;
  }

  PromiscuousPacket *packet = reinterpret_cast<PromiscuousPacket *>(buffer);
  // ESP8266 ESP-NOW receive callbacks do not expose RSSI. The promiscuous
  // callback exposes the WiFi frame metadata (rx_ctrl) plus the raw 802.11
  // header. Bytes payload[10..15] are addr2 (the transmitter address) for
  // the data and management frame layouts ESP-NOW uses, so we filter on it
  // before trusting the RSSI value.
  const uint8_t *sourceMac = packet->payload + 10;
  int idx = findPetIndex(sourceMac);
  if (idx < 0) {
    return;
  }

  petStates[idx].lastRssi = packet->rx_ctrl.rssi;
  petStates[idx].lastRssiAtMs = millis();
}

void startPromiscuousSniffer() {
  if (!ENABLE_RSSI_SNIFFER_VALUE) {
    Serial.println("Promiscuous RSSI sniffer disabled by ENABLE_RSSI_SNIFFER_VALUE.");
    return;
  }

  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(promiscuousCallback);
  wifi_promiscuous_enable(1);
  Serial.println("Promiscuous RSSI sniffer enabled with Pet Node MAC filter.");
}

void stopPromiscuousSniffer() {
  if (!ENABLE_RSSI_SNIFFER_VALUE) {
    return;
  }

  wifi_promiscuous_enable(0);
}

bool isValidPacket(const PetPacket &packet, int idx) {
  return packet.magic == PACKET_MAGIC &&
         packet.protocolVersion == PROTOCOL_VERSION &&
         strncmp(packet.petId, PETS[idx].petId, sizeof(packet.petId)) == 0;
}

void onDataReceived(uint8_t *senderMac, uint8_t *incomingData, uint8_t len) {
  int idx = findPetIndex(senderMac);
  if (idx < 0) {
    Serial.print("Ignoring packet from unknown MAC: ");
    Serial.println(formatMac(senderMac));
    return;
  }

  if (len != sizeof(PetPacket)) {
    Serial.print("Unexpected packet size from ");
    Serial.print(formatMac(senderMac));
    Serial.print(": ");
    Serial.println(len);
    return;
  }

  PetPacket incomingPacket;
  memcpy(&incomingPacket, incomingData, sizeof(incomingPacket));

  if (!isValidPacket(incomingPacket, idx)) {
    Serial.print("Rejected packet for ");
    Serial.print(PETS[idx].displayName);
    Serial.println(" (bad magic, version, or pet ID mismatch).");
    return;
  }

  petStates[idx].lastPacket = incomingPacket;
  petStates[idx].packetReceived = true;
  petStates[idx].lastPacketAtMs = millis();

  Serial.print("Packet received from ");
  Serial.print(PETS[idx].displayName);
  Serial.print(" (");
  Serial.print(formatMac(senderMac));
  Serial.print(") counter=");
  Serial.print(incomingPacket.packetCounter);
  Serial.print(" battery=");
  Serial.print(incomingPacket.batteryVoltage, 2);
  Serial.println(" V");
}

bool initEspNow() {
  esp_now_deinit();
  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW init failed on Home Node.");
    return false;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(onDataReceived);
  uint8_t peerChannel = ESPNOW_CHANNEL_VALUE == 0 ? wifiChannel : ESPNOW_CHANNEL_VALUE;

  for (uint8_t i = 0; i < PET_COUNT; i++) {
    if (esp_now_add_peer(const_cast<uint8_t *>(PETS[i].mac), ESP_NOW_ROLE_COMBO, peerChannel, NULL, 0) != 0) {
      Serial.print("Failed to add ESP-NOW peer for ");
      Serial.print(PETS[i].displayName);
      Serial.print(" @ ");
      Serial.println(formatMac(PETS[i].mac));
      return false;
    }
  }

  Serial.println("ESP-NOW initialized on Home Node.");
  Serial.print("ESP-NOW self role: COMBO, peer channel: ");
  Serial.println(peerChannel);
  Serial.print("Home Node ESP-NOW MAC: ");
  Serial.println(WiFi.macAddress());
  return true;
}

void checkEspNowReceiverHealth() {
  if (!ENABLE_RSSI_SNIFFER_VALUE) {
    return;
  }

  if (millis() - lastEspNowHealthCheckAtMs < ESPNOW_RX_HEALTH_MS_VALUE) {
    return;
  }
  lastEspNowHealthCheckAtMs = millis();

  for (uint8_t i = 0; i < PET_COUNT; i++) {
    bool hasFreshSignal = hasRecentRssiSample(i);
    bool hasNoDecodedPacket = !petStates[i].packetReceived;
    if (!hasFreshSignal || !hasNoDecodedPacket) {
      continue;
    }

    Serial.print("ESP-NOW health: signal frames visible for ");
    Serial.print(PETS[i].displayName);
    Serial.println(", but no valid payload packet decoded yet.");
    Serial.println("Reinitializing ESP-NOW receive callback.");
    espNowReady = initEspNow();
    return;
  }
}

bool handleWifiRecovery() {
  wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    wifiDisconnectedSinceMs = 0;
    lastWifiRecoveryLogAtMs = 0;
    return true;
  }

  unsigned long nowMs = millis();
  if (status == WL_IDLE_STATUS && hasLastKnownIp()) {
    wifiDisconnectedSinceMs = 0;
    lastWifiRecoveryLogAtMs = 0;
    if (nowMs - lastWifiIdleStatusLogAtMs >= 10000) {
      lastWifiIdleStatusLogAtMs = nowMs;
      Serial.println("WiFi reports WL_IDLE_STATUS after successful connection. Treating connection as usable.");
      Serial.print("Home Node last known IP: ");
      Serial.println(homeIpString);
      Serial.print("Current local IP readback: ");
      Serial.println(WiFi.localIP().toString());
      Serial.print("Home Node MAC: ");
      Serial.println(WiFi.macAddress());
    }
    return true;
  }

  if (wifiDisconnectedSinceMs == 0) {
    wifiDisconnectedSinceMs = nowMs;
    lastWifiRecoveryLogAtMs = 0;
    Serial.println("WiFi not connected. Starting recovery grace period.");
    Serial.print("Current WiFi status: ");
    Serial.print(wifiStatusName(status));
    Serial.print(" (");
    Serial.print(static_cast<int>(status));
    Serial.println(")");
    Serial.print("Home Node MAC: ");
    Serial.println(WiFi.macAddress());
    Serial.print("Home Node last known IP: ");
    Serial.println(homeIpString);
  }

  if (nowMs - lastWifiRecoveryLogAtMs >= 2000) {
    lastWifiRecoveryLogAtMs = nowMs;
    Serial.print("WiFi recovery waiting. Status=");
    Serial.print(wifiStatusName(status));
    Serial.print(" (");
    Serial.print(static_cast<int>(status));
    Serial.print("), elapsed=");
    Serial.print(nowMs - wifiDisconnectedSinceMs);
    Serial.print("/");
    Serial.print(WIFI_RECOVERY_GRACE_MS_VALUE);
    Serial.println(" ms");
  }

  if (nowMs - wifiDisconnectedSinceMs < WIFI_RECOVERY_GRACE_MS_VALUE) {
    return false;
  }

  Serial.println("WiFi recovery grace expired. Reconnecting without immediate reboot...");
  stopPromiscuousSniffer();
  wifiReady = connectToWifi(false);
  if (wifiReady) {
    wifiDisconnectedSinceMs = 0;
    lastWifiRecoveryLogAtMs = 0;
    startPromiscuousSniffer();
    if (!espNowReady) {
      espNowReady = initEspNow();
    }
    Serial.println("WiFi recovered successfully.");
    return true;
  }

  Serial.println("WiFi reconnect failed after grace period. Restarting Home Node for clean recovery.");
  Serial.print("Home Node MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Home Node last known IP: ");
  Serial.println(homeIpString);
  ESP.restart();
  return false;
}

bool hasRecentRssiSample(int idx) {
  unsigned long stamp = petStates[idx].lastRssiAtMs;
  return stamp != 0 && (millis() - stamp) <= RSSI_FRESH_MS_VALUE;
}

String petStatus(int idx) {
  const PetState &s = petStates[idx];
  if (!s.packetReceived) {
    return "Waiting";
  }
  if (millis() - s.lastPacketAtMs > PACKET_TIMEOUT_MS_VALUE) {
    return "Lost";
  }
  if (hasRecentRssiSample(idx) && s.lastRssi <= RSSI_THRESHOLD_DBM_VALUE) {
    return "Far";
  }
  return "Nearby";
}

// Returns the most urgent status across all pets: "Lost" > "Far" > anything else.
String overallUrgency() {
  bool anyFar = false;
  bool anyNearby = false;
  bool anyWaiting = false;
  for (uint8_t i = 0; i < PET_COUNT; i++) {
    String s = petStatus(i);
    if (s == "Lost") return "Lost";
    if (s == "Far") anyFar = true;
    if (s == "Nearby") anyNearby = true;
    if (s == "Waiting") anyWaiting = true;
  }
  if (anyFar) return "Far";
  if (anyNearby) return "Nearby";
  if (anyWaiting) return "Waiting";
  return "Waiting";
}

bool buzzerSilenced() {
  return millis() < buzzerSilencedUntilMs;
}

void writeBuzzer(bool enabled) {
  buzzerOutputHigh = enabled;
  digitalWrite(BUZZER_PIN, enabled ? HIGH : LOW);
}

void updateBuzzerPattern() {
  String urgency = overallUrgency();
  if (buzzerSilenced() || urgency == "Nearby" || urgency == "Waiting") {
    writeBuzzer(false);
    return;
  }

  unsigned long nowMs = millis();
  unsigned long intervalMs = urgency == "Lost" ? 200 : 700;

  if (nowMs - lastBuzzerToggleAtMs >= intervalMs) {
    lastBuzzerToggleAtMs = nowMs;
    writeBuzzer(!buzzerOutputHigh);
  }
}

bool emailCooldownActive(int idx) {
  unsigned long stamp = petStates[idx].lastEmailSentAtMs;
  return stamp != 0 && (millis() - stamp) < EMAIL_COOLDOWN_MS_VALUE;
}

String emailCooldownState(int idx) {
  unsigned long stamp = petStates[idx].lastEmailSentAtMs;
  if (stamp == 0) return "Ready";
  unsigned long elapsed = millis() - stamp;
  if (elapsed >= EMAIL_COOLDOWN_MS_VALUE) return "Ready";
  unsigned long remainingSeconds = (EMAIL_COOLDOWN_MS_VALUE - elapsed) / 1000UL;
  return "Cooldown (" + String(remainingSeconds) + "s left)";
}

String base64Encode(const String &input) {
  const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String output;
  int value = 0;
  int bits = -6;

  for (size_t i = 0; i < input.length(); i++) {
    value = (value << 8) + static_cast<uint8_t>(input[i]);
    bits += 8;
    while (bits >= 0) {
      output += table[(value >> bits) & 0x3F];
      bits -= 6;
    }
  }

  if (bits > -6) {
    output += table[((value << 8) >> (bits + 8)) & 0x3F];
  }
  while (output.length() % 4) {
    output += '=';
  }
  return output;
}

int readSmtpCode(BearSSL::WiFiClientSecure &client) {
  int code = -1;
  unsigned long startMs = millis();

  while (client.connected() && millis() - startMs < 15000) {
    if (!client.available()) {
      delay(10);
      continue;
    }

    String line = client.readStringUntil('\n');
    line.trim();
    Serial.print("SMTP < ");
    Serial.println(line);

    if (line.length() >= 3) {
      code = line.substring(0, 3).toInt();
      if (line.length() < 4 || line.charAt(3) != '-') {
        return code;
      }
    }
  }

  return code;
}

bool smtpCommand(BearSSL::WiFiClientSecure &client, const String &command, int expectedCode) {
  if (command.length() > 0) {
    Serial.print("SMTP > ");
    if (command.startsWith("AUTH") || command.length() > 48) {
      Serial.println("[redacted]");
    } else {
      Serial.println(command);
    }
    client.print(command);
    client.print("\r\n");
  }

  int code = readSmtpCode(client);
  return code == expectedCode;
}

bool sendEmailAlert(int idx, const String &statusText) {
  if (!wifiReady) {
    Serial.println("Skip email: WiFi unavailable.");
    return false;
  }

  if (!emailConfigured) {
    Serial.println("Skip email: SMTP placeholders still present.");
    return false;
  }

  const PetState &s = petStates[idx];

  String subject = "Pet Tracker Alert: ";
  subject += PETS[idx].displayName;
  subject += " is ";
  subject += statusText;

  String body;
  body += "Pet tracker alert generated by Home Node.\r\n\r\n";
  body += "Pet: ";
  body += PETS[idx].displayName;
  body += " (";
  body += PETS[idx].petId;
  body += ")\r\n";
  body += "Status: " + statusText + "\r\n";
  body += "MAC: " + formatMac(PETS[idx].mac) + "\r\n";
  body += "RSSI: " + String(s.lastRssi) + " dBm\r\n";
  body += "Last packet age: " + String(s.packetReceived ? millis() - s.lastPacketAtMs : 0) + " ms\r\n";
  body += "Packet counter: " + String(s.lastPacket.packetCounter) + "\r\n";
  body += "Battery: " + String(s.lastPacket.batteryVoltage, 2) + " V\r\n";
  body += "Home Node IP: " + homeIpString + "\r\n";
  body += "\r\n";
  body += "This is an approximate RSSI-based alert.\r\n";

  Serial.print("Opening SMTP SSL connection for ");
  Serial.println(PETS[idx].displayName);

  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);

  if (!client.connect(SMTP_HOST_VALUE, SMTP_PORT_VALUE)) {
    Serial.println("SMTP connection failed.");
    return false;
  }

  bool ok = true;
  ok = ok && smtpCommand(client, "", 220);
  ok = ok && smtpCommand(client, "EHLO esp8266-pet-tracker", 250);
  ok = ok && smtpCommand(client, "AUTH LOGIN", 334);
  ok = ok && smtpCommand(client, base64Encode(SENDER_EMAIL_VALUE), 334);
  ok = ok && smtpCommand(client, base64Encode(SENDER_APP_PASSWORD_VALUE), 235);
  ok = ok && smtpCommand(client, "MAIL FROM:<" + String(SENDER_EMAIL_VALUE) + ">", 250);

  if (ok) {
    client.print("RCPT TO:<");
    client.print(RECIPIENT_EMAIL_VALUE);
    client.print(">\r\n");
    int rcptCode = readSmtpCode(client);
    ok = rcptCode == 250 || rcptCode == 251;
  }

  ok = ok && smtpCommand(client, "DATA", 354);
  if (ok) {
    client.print("From: ESP8266 Pet Tracker <");
    client.print(SENDER_EMAIL_VALUE);
    client.print(">\r\n");
    client.print("To: Owner <");
    client.print(RECIPIENT_EMAIL_VALUE);
    client.print(">\r\n");
    client.print("Subject: ");
    client.print(subject);
    client.print("\r\n");
    client.print("MIME-Version: 1.0\r\n");
    client.print("Content-Type: text/plain; charset=us-ascii\r\n");
    client.print("Content-Transfer-Encoding: 7bit\r\n");
    client.print("\r\n");
    client.print(body);
    client.print("\r\n.\r\n");
    ok = readSmtpCode(client) == 250;
  }

  smtpCommand(client, "QUIT", 221);
  client.stop();

  Serial.println(ok ? "Email alert sent successfully." : "Email alert failed.");
  return ok;
}

void maybeSendAlertEmail(int idx, const String &statusText) {
  if (statusText != "Far" && statusText != "Lost") {
    return;
  }
  if (emailCooldownActive(idx)) {
    return;
  }
  if (sendEmailAlert(idx, statusText)) {
    petStates[idx].lastEmailSentAtMs = millis();
  }
}

String jsonBool(bool value) {
  return value ? "true" : "false";
}

String jsonEscape(const char *value) {
  String out;
  out.reserve(strlen(value) + 2);
  for (const char *p = value; *p; p++) {
    char c = *p;
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if ((uint8_t)c < 0x20) {
      char buf[8];
      snprintf(buf, sizeof(buf), "\\u%04x", c);
      out += buf;
    } else {
      out += c;
    }
  }
  return out;
}

void handleRoot() {
  String html;
  html.reserve(2048 + 512 * PET_COUNT);
  html += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='3'>";
  html += "<title>ESP8266 Pet Tracker</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;background:#f4f7fb;color:#1f2937;margin:0;padding:24px;}";
  html += ".card{max-width:760px;margin:0 auto 16px auto;background:#fff;border-radius:8px;padding:24px;box-shadow:0 10px 30px rgba(0,0,0,0.08);}";
  html += "h1,h2{margin-top:0;}table{width:100%;border-collapse:collapse;}td{padding:10px;border-bottom:1px solid #e5e7eb;vertical-align:top;}";
  html += ".status{font-weight:bold;font-size:1.2rem;}";
  html += ".s-Nearby{color:#047857;}.s-Far{color:#b45309;}.s-Lost{color:#b91c1c;}.s-Waiting{color:#6b7280;}";
  html += "form.silence{margin-top:16px;}button.silence{background:#1f2937;color:#fff;padding:10px 14px;border:0;border-radius:6px;font-size:1rem;cursor:pointer;}";
  html += "code{background:#eef2ff;padding:2px 6px;border-radius:6px;}";
  html += "</style></head><body>";

  html += "<div class='card'>";
  html += "<h1>IoT Pet Tracker Dashboard</h1>";
  html += "<p>Tracking " + String(PET_COUNT) + " pet(s). JSON API: <code>/json</code></p>";
  html += "<table>";
  html += "<tr><td>Overall urgency</td><td class='status s-" + overallUrgency() + "'>" + overallUrgency() + "</td></tr>";
  html += "<tr><td>Buzzer state</td><td>" + String(buzzerOutputHigh ? "ON" : "OFF") + "</td></tr>";
  html += "<tr><td>Buzzer silence</td><td>" + String(buzzerSilenced() ? "Active" : "Inactive") + "</td></tr>";
  html += "<tr><td>Home Node IP</td><td>" + homeIpString + "</td></tr>";
  html += "<tr><td>WiFi channel</td><td>" + String(wifiChannel) + "</td></tr>";
  html += "</table>";
  html += "<form class='silence' method='POST' action='/silence'>";
  if (strlen(DASHBOARD_TOKEN_VALUE) > 0) {
    html += "<input type='password' name='token' placeholder='token' required> ";
  }
  html += "<button class='silence' type='submit'>Silence buzzer for 2 minutes</button>";
  html += "</form>";
  html += "</div>";

  for (uint8_t i = 0; i < PET_COUNT; i++) {
    const PetState &s = petStates[i];
    String statusText = petStatus(i);
    unsigned long packetAge = s.packetReceived ? millis() - s.lastPacketAtMs : 0;

    html += "<div class='card'>";
    html += "<h2>" + String(PETS[i].displayName) + "</h2>";
    html += "<table>";
    html += "<tr><td>Status</td><td class='status s-" + statusText + "'>" + statusText + "</td></tr>";
    html += "<tr><td>Pet ID</td><td>" + String(PETS[i].petId) + "</td></tr>";
    html += "<tr><td>MAC</td><td>" + formatMac(PETS[i].mac) + "</td></tr>";
    html += "<tr><td>RSSI</td><td>" + String(s.lastRssi) + " dBm</td></tr>";
    html += "<tr><td>RSSI sample</td><td>" + String(hasRecentRssiSample(i) ? "Recent" : "Stale") + "</td></tr>";
    html += "<tr><td>Last packet age</td><td>" + String(packetAge) + " ms</td></tr>";
    html += "<tr><td>Packet counter</td><td>" + String(s.packetReceived ? s.lastPacket.packetCounter : 0) + "</td></tr>";
    html += "<tr><td>Battery</td><td>" + String(s.packetReceived ? s.lastPacket.batteryVoltage : 0, 2) + " V</td></tr>";
    html += "<tr><td>Email cooldown</td><td>" + emailCooldownState(i) + "</td></tr>";
    html += "</table>";
    html += "</div>";
  }

  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleJson() {
  String json;
  json.reserve(512 + 256 * PET_COUNT);
  json += "{";
  json += "\"homeNodeIp\":\"" + homeIpString + "\",";
  json += "\"wifiChannel\":" + String(wifiChannel) + ",";
  json += "\"overallUrgency\":\"" + overallUrgency() + "\",";
  json += "\"buzzerOn\":" + jsonBool(buzzerOutputHigh) + ",";
  json += "\"buzzerSilenced\":" + jsonBool(buzzerSilenced()) + ",";
  json += "\"pets\":[";
  for (uint8_t i = 0; i < PET_COUNT; i++) {
    const PetState &s = petStates[i];
    String statusText = petStatus(i);
    unsigned long packetAge = s.packetReceived ? millis() - s.lastPacketAtMs : 0;

    if (i > 0) json += ",";
    json += "{";
    json += "\"petId\":\"" + jsonEscape(PETS[i].petId) + "\",";
    json += "\"displayName\":\"" + jsonEscape(PETS[i].displayName) + "\",";
    json += "\"mac\":\"" + formatMac(PETS[i].mac) + "\",";
    json += "\"status\":\"" + statusText + "\",";
    json += "\"rssi\":" + String(s.lastRssi) + ",";
    json += "\"rssiRecent\":" + jsonBool(hasRecentRssiSample(i)) + ",";
    json += "\"lastPacketAgeMs\":" + String(packetAge) + ",";
    json += "\"packetCounter\":" + String(s.packetReceived ? s.lastPacket.packetCounter : 0) + ",";
    json += "\"batteryVoltage\":" + String(s.packetReceived ? s.lastPacket.batteryVoltage : 0, 2) + ",";
    json += "\"emailCooldownActive\":" + jsonBool(emailCooldownActive(i));
    json += "}";
  }
  json += "]}";

  server.send(200, "application/json", json);
}

void handleSilence() {
  if (strlen(DASHBOARD_TOKEN_VALUE) > 0) {
    if (!server.hasArg("token") || server.arg("token") != String(DASHBOARD_TOKEN_VALUE)) {
      server.send(403, "text/plain", "Invalid token");
      return;
    }
  }
  buzzerSilencedUntilMs = millis() + BUZZER_SILENCE_MS_VALUE;
  writeBuzzer(false);
  Serial.println("Buzzer silenced from dashboard.");
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
}

void handleSilenceWrongMethod() {
  server.send(405, "text/plain", "Use POST");
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

void startWebServer() {
  server.on("/", handleRoot);
  server.on("/json", handleJson);
  server.on("/silence", HTTP_POST, handleSilence);
  server.on("/silence", HTTP_GET, handleSilenceWrongMethod);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Web server started on port 80.");
}

void initPetStates() {
  for (uint8_t i = 0; i < PET_COUNT; i++) {
    memset(&petStates[i].lastPacket, 0, sizeof(PetPacket));
    petStates[i].packetReceived = false;
    petStates[i].lastPacketAtMs = 0;
    petStates[i].lastRssi = -127;
    petStates[i].lastRssiAtMs = 0;
    petStates[i].lastEmailSentAtMs = 0;
    petStates[i].alertConditionActive = false;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== Home Node Boot ===");

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  printHomeNodeIdentity();

  pinMode(BUZZER_PIN, OUTPUT);
  writeBuzzer(false);

  initPetStates();

  emailConfigured = smtpConfigLooksFilled();
  if (!emailConfigured) {
    Serial.println("SMTP placeholders detected. Email alerts are disabled until edited.");
  }

  wifiReady = connectToWifi();
  if (wifiReady) {
    startWebServer();
    startPromiscuousSniffer();
    espNowReady = initEspNow();
  }
}

void loop() {
  if (!wifiReady) {
    static unsigned long lastRetryAtMs = 0;
    if (millis() - lastRetryAtMs > 5000) {
      lastRetryAtMs = millis();
      wifiReady = connectToWifi();
      if (wifiReady) {
        startWebServer();
        startPromiscuousSniffer();
        espNowReady = initEspNow();
      }
    }
    return;
  }

  if (!handleWifiRecovery()) {
    updateBuzzerPattern();
    delay(10);
    return;
  }

  server.handleClient();

  checkEspNowReceiverHealth();
  updateBuzzerPattern();

  for (uint8_t i = 0; i < PET_COUNT; i++) {
    String statusText = petStatus(i);
    bool inAlert = statusText == "Far" || statusText == "Lost";
    if (inAlert && !petStates[i].alertConditionActive) {
      Serial.print("Alert condition entered for ");
      Serial.print(PETS[i].displayName);
      Serial.print(": ");
      Serial.println(statusText);
    }
    if (inAlert) {
      maybeSendAlertEmail(i, statusText);
    }
    petStates[i].alertConditionActive = inAlert;
  }

  if (millis() - lastStatusLogAtMs >= 2000) {
    lastStatusLogAtMs = millis();
    Serial.print("Overall=");
    Serial.print(overallUrgency());
    Serial.print(", Buzzer=");
    Serial.println(buzzerOutputHigh ? "ON" : "OFF");
    for (uint8_t i = 0; i < PET_COUNT; i++) {
      const PetState &s = petStates[i];
      Serial.print("  ");
      Serial.print(PETS[i].displayName);
      Serial.print(": status=");
      Serial.print(petStatus(i));
      Serial.print(", rssi=");
      Serial.print(s.lastRssi);
      Serial.print(" (");
      Serial.print(hasRecentRssiSample(i) ? "fresh" : "stale");
      Serial.print("), packetAge=");
      Serial.print(s.packetReceived ? millis() - s.lastPacketAtMs : 0);
      Serial.println(" ms");
    }
  }

  if (!espNowReady) {
    Serial.println("ESP-NOW not ready. Attempting re-init...");
    espNowReady = initEspNow();
  }
}
