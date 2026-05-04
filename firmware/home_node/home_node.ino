#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <espnow.h>
#include <ESP_Mail_Client.h>

extern "C" {
  #include <user_interface.h>
}

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

#ifndef SMTP_HOST_VALUE
#define SMTP_HOST_VALUE "smtp.example.com"
#endif

#ifndef SMTP_PORT_VALUE
#define SMTP_PORT_VALUE 465
#endif

#ifndef SENDER_EMAIL_VALUE
#define SENDER_EMAIL_VALUE "sender@example.com"
#endif

#ifndef SENDER_APP_PASSWORD_VALUE
#define SENDER_APP_PASSWORD_VALUE "YOUR_APP_PASSWORD"
#endif

#ifndef RECIPIENT_EMAIL_VALUE
#define RECIPIENT_EMAIL_VALUE "recipient@example.com"
#endif

// SMTP DATE-header offset. Units depend on the ESP-Mail-Client version.
// Older builds expect seconds (e.g. 19800 = UTC+5:30 IST), newer builds expect hours.
// Set whatever your installed version expects; default 0 = UTC.
#ifndef SMTP_TIME_GMT_OFFSET_VALUE
#define SMTP_TIME_GMT_OFFSET_VALUE 0
#endif

// Pet roster. Add one entry per pet node. Each entry is:
//   { {mac bytes}, "PROTOCOL_PET_ID", "Display Name" }
// PROTOCOL_PET_ID must match the pet node's PET_ID_VALUE exactly (used for packet validation).
// Display Name is shown on the dashboard. Use the same string as PROTOCOL_PET_ID if you do not care.
//
// Example with two pets:
//   #define PET_NODE_LIST \
//     { {0x84, 0xF3, 0xEB, 0xAA, 0xBB, 0xCC}, "PET-001", "Bella" }, \
//     { {0x84, 0xF3, 0xEB, 0xAA, 0xBB, 0xCD}, "PET-002", "Max"   }
#ifndef PET_NODE_LIST
#define PET_NODE_LIST \
  { {0x84, 0xF3, 0xEB, 0xAA, 0xBB, 0xCC}, "PET-001", "Pet 1" }
#endif

// Optional dashboard token. If defined and non-empty, /silence requires
// the matching value as a "token" form field. Leave undefined to disable.
#ifndef DASHBOARD_TOKEN_VALUE
#define DASHBOARD_TOKEN_VALUE ""
#endif

const char *WIFI_SSID = WIFI_SSID_VALUE;
const char *WIFI_PASSWORD = WIFI_PASSWORD_VALUE;
const char *SMTP_HOST = SMTP_HOST_VALUE;
const int SMTP_PORT = SMTP_PORT_VALUE;
const char *SENDER_EMAIL = SENDER_EMAIL_VALUE;
const char *SENDER_APP_PASSWORD = SENDER_APP_PASSWORD_VALUE;
const char *RECIPIENT_EMAIL = RECIPIENT_EMAIL_VALUE;
const float SMTP_TIME_GMT_OFFSET = SMTP_TIME_GMT_OFFSET_VALUE;
const char *DASHBOARD_TOKEN = DASHBOARD_TOKEN_VALUE;

const uint32_t PACKET_MAGIC = 0x50544731; // "PTG1"
const uint8_t PROTOCOL_VERSION = 1;

const int RSSI_THRESHOLD_DBM = -75;
const unsigned long PACKET_TIMEOUT_MS = 10000;
const unsigned long RSSI_FRESH_MS = 3000;
const unsigned long EMAIL_COOLDOWN_MS = 300000;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
const unsigned long BUZZER_SILENCE_MS = 120000;

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
  RxControl rx_ctrl;
  uint8_t payload[112];
};

ESP8266WebServer server(80);
SMTPSession smtp;

bool espNowReady = false;
bool wifiReady = false;
bool buzzerOutputHigh = false;
bool emailConfigured = true;

unsigned long lastStatusLogAtMs = 0;
unsigned long lastBuzzerToggleAtMs = 0;
unsigned long buzzerSilencedUntilMs = 0;
uint8_t wifiChannel = 0;
String homeIpString = "0.0.0.0";

String formatMac(const uint8_t *mac) {
  char buffer[18];
  snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buffer);
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

void smtpCallback(SMTP_Status status) {
  Serial.println(status.info());
  if (status.success()) {
    Serial.println("Email alert sent successfully.");
  } else {
    Serial.println("Email alert failed.");
  }
}

bool smtpConfigLooksFilled() {
  return String(SMTP_HOST) != "smtp.example.com" &&
         String(SENDER_EMAIL) != "sender@example.com" &&
         String(SENDER_APP_PASSWORD) != "YOUR_APP_PASSWORD" &&
         String(RECIPIENT_EMAIL) != "recipient@example.com";
}

bool connectToWifi() {
  Serial.println("Connecting Home Node to WiFi...");

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
    Serial.println("Home Node WiFi connect failed.");
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
  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(promiscuousCallback);
  wifi_promiscuous_enable(1);
  Serial.println("Promiscuous RSSI sniffer enabled with Pet Node MAC filter.");
}

void stopPromiscuousSniffer() {
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
  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW init failed on Home Node.");
    return false;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  esp_now_register_recv_cb(onDataReceived);
  Serial.println("ESP-NOW initialized on Home Node.");
  return true;
}

bool hasRecentRssiSample(int idx) {
  unsigned long stamp = petStates[idx].lastRssiAtMs;
  return stamp != 0 && (millis() - stamp) <= RSSI_FRESH_MS;
}

String petStatus(int idx) {
  const PetState &s = petStates[idx];
  if (!s.packetReceived) {
    return "Waiting";
  }
  if (millis() - s.lastPacketAtMs > PACKET_TIMEOUT_MS) {
    return "Lost";
  }
  if (hasRecentRssiSample(idx) && s.lastRssi <= RSSI_THRESHOLD_DBM) {
    return "Far";
  }
  return "Nearby";
}

// Returns the most urgent status across all pets: "Lost" > "Far" > anything else.
String overallUrgency() {
  bool anyFar = false;
  for (uint8_t i = 0; i < PET_COUNT; i++) {
    String s = petStatus(i);
    if (s == "Lost") return "Lost";
    if (s == "Far") anyFar = true;
  }
  return anyFar ? "Far" : "Nearby";
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
  if (buzzerSilenced() || urgency == "Nearby") {
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
  return stamp != 0 && (millis() - stamp) < EMAIL_COOLDOWN_MS;
}

String emailCooldownState(int idx) {
  unsigned long stamp = petStates[idx].lastEmailSentAtMs;
  if (stamp == 0) return "Ready";
  unsigned long elapsed = millis() - stamp;
  if (elapsed >= EMAIL_COOLDOWN_MS) return "Ready";
  unsigned long remainingSeconds = (EMAIL_COOLDOWN_MS - elapsed) / 1000UL;
  return "Cooldown (" + String(remainingSeconds) + "s left)";
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

  smtp.callback(smtpCallback);

  ESP_Mail_Session session;
  session.server.host_name = SMTP_HOST;
  session.server.port = SMTP_PORT;
  session.login.email = SENDER_EMAIL;
  session.login.password = SENDER_APP_PASSWORD;
  session.login.user_domain = "";
  session.time.ntp_server = "pool.ntp.org,time.nist.gov";
  session.time.gmt_offset = SMTP_TIME_GMT_OFFSET;
  session.time.day_light_offset = 0;

  const PetState &s = petStates[idx];

  SMTP_Message message;
  message.sender.name = "ESP8266 Pet Tracker";
  message.sender.email = SENDER_EMAIL;
  String subject = "Pet Tracker Alert: ";
  subject += PETS[idx].displayName;
  subject += " is ";
  subject += statusText;
  message.subject = subject.c_str();
  message.addRecipient("Owner", RECIPIENT_EMAIL);

  String body;
  body += "Pet tracker alert generated by Home Node.\r\n\r\n";
  body += "Pet: " + String(PETS[idx].displayName) +
          " (" + String(PETS[idx].petId) + ")\r\n";
  body += "Status: " + statusText + "\r\n";
  body += "MAC: " + formatMac(PETS[idx].mac) + "\r\n";
  body += "RSSI: " + String(s.lastRssi) + " dBm\r\n";
  body += "Last packet age: " + String(s.packetReceived ? millis() - s.lastPacketAtMs : 0) + " ms\r\n";
  body += "Packet counter: " + String(s.lastPacket.packetCounter) + "\r\n";
  body += "Battery: " + String(s.lastPacket.batteryVoltage, 2) + " V\r\n";
  body += "Home Node IP: " + homeIpString + "\r\n";
  body += "\r\n";
  body += "This is an approximate RSSI-based alert.\r\n";
  message.text.content = body.c_str();
  message.text.charSet = "us-ascii";
  message.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;

  Serial.print("Opening SMTP session for ");
  Serial.println(PETS[idx].displayName);
  if (!smtp.connect(&session)) {
    Serial.println("SMTP connection failed.");
    return false;
  }

  bool sent = MailClient.sendMail(&smtp, &message, true);
  smtp.closeSession();
  return sent;
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
  if (strlen(DASHBOARD_TOKEN) > 0) {
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
  if (strlen(DASHBOARD_TOKEN) > 0) {
    if (!server.hasArg("token") || server.arg("token") != String(DASHBOARD_TOKEN)) {
      server.send(403, "text/plain", "Invalid token");
      return;
    }
  }
  buzzerSilencedUntilMs = millis() + BUZZER_SILENCE_MS;
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

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi dropped. Restarting Home Node for recovery.");
    stopPromiscuousSniffer();
    ESP.restart();
  }

  server.handleClient();

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
