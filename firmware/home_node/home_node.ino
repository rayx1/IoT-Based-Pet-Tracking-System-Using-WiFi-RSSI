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

#ifndef PET_NODE_MAC_BYTES
// Replace this with the MAC printed by the Pet Node sketch.
#define PET_NODE_MAC_BYTES {0x84, 0xF3, 0xEB, 0xAA, 0xBB, 0xCC}
#endif

const char *WIFI_SSID = WIFI_SSID_VALUE;
const char *WIFI_PASSWORD = WIFI_PASSWORD_VALUE;
const char *SMTP_HOST = SMTP_HOST_VALUE;
const int SMTP_PORT = SMTP_PORT_VALUE;
const char *SENDER_EMAIL = SENDER_EMAIL_VALUE;
const char *SENDER_APP_PASSWORD = SENDER_APP_PASSWORD_VALUE;
const char *RECIPIENT_EMAIL = RECIPIENT_EMAIL_VALUE;
uint8_t PET_NODE_MAC[] = PET_NODE_MAC_BYTES;

const uint32_t PACKET_MAGIC = 0x50544731; // "PTG1"
const uint8_t PROTOCOL_VERSION = 1;

const int RSSI_THRESHOLD_DBM = -75;
const unsigned long PACKET_TIMEOUT_MS = 10000;
const unsigned long EMAIL_COOLDOWN_MS = 300000;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
const unsigned long BUZZER_SILENCE_MS = 120000;

const uint8_t BUZZER_PIN = D5;
const char *EXPECTED_PET_ID = "PET-001";

// Keep this struct identical to the pet node.
struct PetPacket {
  uint32_t magic;
  uint8_t protocolVersion;
  char petId[16];
  uint32_t packetCounter;
  float batteryVoltage;
  uint32_t uptimeSeconds;
};

// Structure copied from ESP8266 promiscuous callback metadata.
struct PromiscuousPacket {
  RxControl rx_ctrl;
  uint8_t payload[112];
};

ESP8266WebServer server(80);
SMTPSession smtp;

volatile int lastCapturedRssi = -127;
volatile bool rssiUpdated = false;
volatile unsigned long lastPromiscPacketAtMs = 0;

PetPacket lastPacket;
bool packetReceived = false;
bool espNowReady = false;
bool wifiReady = false;
bool buzzerOutputHigh = false;
bool alertConditionActive = false;
bool emailConfigured = true;

unsigned long lastPacketAtMs = 0;
unsigned long lastStatusLogAtMs = 0;
unsigned long lastEmailSentAtMs = 0;
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
  Serial.print("Expected Pet Node MAC: ");
  Serial.println(formatMac(PET_NODE_MAC));
  Serial.print("Home Node WiFi channel: ");
  Serial.println(wifiChannel);
  return true;
}

void ICACHE_FLASH_ATTR promiscuousCallback(uint8_t *buffer, uint16_t length) {
  if (length < 36) {
    return;
  }

  PromiscuousPacket *packet = reinterpret_cast<PromiscuousPacket *>(buffer);
  const uint8_t *sourceMac = packet->payload + 10;

  // ESP8266 ESP-NOW receive callbacks do not expose RSSI. The promiscuous
  // callback sees the WiFi frame metadata, so filter it by the expected
  // transmitter MAC before using the RSSI value.
  if (!macEquals(sourceMac, PET_NODE_MAC)) {
    return;
  }

  lastCapturedRssi = packet->rx_ctrl.rssi;
  lastPromiscPacketAtMs = millis();
  rssiUpdated = true;
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

bool isValidPacket(const PetPacket &packet) {
  return packet.magic == PACKET_MAGIC &&
         packet.protocolVersion == PROTOCOL_VERSION &&
         strncmp(packet.petId, EXPECTED_PET_ID, sizeof(packet.petId)) == 0;
}

void onDataReceived(uint8_t *senderMac, uint8_t *incomingData, uint8_t len) {
  if (!macEquals(senderMac, PET_NODE_MAC)) {
    Serial.print("Ignoring packet from unexpected MAC: ");
    Serial.println(formatMac(senderMac));
    return;
  }

  if (len != sizeof(PetPacket)) {
    Serial.print("Unexpected packet size: ");
    Serial.println(len);
    return;
  }

  PetPacket incomingPacket;
  memcpy(&incomingPacket, incomingData, sizeof(incomingPacket));

  if (!isValidPacket(incomingPacket)) {
    Serial.println("Rejected packet with invalid magic, protocol version, or pet ID.");
    return;
  }

  memcpy(&lastPacket, &incomingPacket, sizeof(lastPacket));
  lastPacketAtMs = millis();
  packetReceived = true;

  Serial.print("Packet received from ");
  Serial.println(formatMac(senderMac));
  Serial.print("petId: ");
  Serial.println(lastPacket.petId);
  Serial.print("counter: ");
  Serial.println(lastPacket.packetCounter);
  Serial.print("batteryVoltage: ");
  Serial.println(lastPacket.batteryVoltage, 2);
  Serial.print("uptimeSeconds: ");
  Serial.println(lastPacket.uptimeSeconds);

  if (rssiUpdated) {
    Serial.print("Filtered RSSI: ");
    Serial.println(lastCapturedRssi);
  } else {
    Serial.println("Filtered RSSI not updated yet.");
  }
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

String emailCooldownState() {
  unsigned long elapsed = millis() - lastEmailSentAtMs;
  bool cooldownActive = lastEmailSentAtMs != 0 && elapsed < EMAIL_COOLDOWN_MS;
  if (!cooldownActive) {
    return "Ready";
  }

  unsigned long remainingSeconds = (EMAIL_COOLDOWN_MS - elapsed) / 1000UL;
  return "Cooldown (" + String(remainingSeconds) + "s left)";
}

bool hasRecentRssiSample() {
  return rssiUpdated && (millis() - lastPromiscPacketAtMs) <= 3000;
}

String currentPetStatus() {
  if (!packetReceived) {
    return "Waiting";
  }

  unsigned long ageMs = millis() - lastPacketAtMs;
  if (ageMs > PACKET_TIMEOUT_MS) {
    return "Lost";
  }

  if (hasRecentRssiSample() && lastCapturedRssi <= RSSI_THRESHOLD_DBM) {
    return "Far";
  }

  return "Nearby";
}

bool buzzerSilenced() {
  return millis() < buzzerSilencedUntilMs;
}

void writeBuzzer(bool enabled) {
  buzzerOutputHigh = enabled;
  digitalWrite(BUZZER_PIN, enabled ? HIGH : LOW);
}

void updateBuzzerPattern(const String &statusText) {
  if (buzzerSilenced() || statusText == "Nearby" || statusText == "Waiting") {
    writeBuzzer(false);
    return;
  }

  unsigned long nowMs = millis();
  unsigned long intervalMs = statusText == "Lost" ? 200 : 700;

  if (nowMs - lastBuzzerToggleAtMs >= intervalMs) {
    lastBuzzerToggleAtMs = nowMs;
    writeBuzzer(!buzzerOutputHigh);
  }
}

bool sendEmailAlert(const String &statusText) {
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
  session.time.gmt_offset = 19800;
  session.time.day_light_offset = 0;

  SMTP_Message message;
  message.sender.name = "ESP8266 Pet Tracker";
  message.sender.email = SENDER_EMAIL;
  String subject = "Pet Tracker Alert: " + statusText;
  message.subject = subject.c_str();
  message.addRecipient("Owner", RECIPIENT_EMAIL);

  String body;
  body += "Pet tracker alert generated by Home Node.\r\n\r\n";
  body += "Status: " + statusText + "\r\n";
  body += "Pet ID: " + String(lastPacket.petId) + "\r\n";
  body += "RSSI: " + String(lastCapturedRssi) + " dBm\r\n";
  body += "Last packet age: " + String(millis() - lastPacketAtMs) + " ms\r\n";
  body += "Packet counter: " + String(lastPacket.packetCounter) + "\r\n";
  body += "Battery: " + String(lastPacket.batteryVoltage, 2) + " V\r\n";
  body += "Home Node IP: " + homeIpString + "\r\n";
  body += "\r\n";
  body += "This is an approximate RSSI-based alert.\r\n";
  message.text.content = body.c_str();
  message.text.charSet = "us-ascii";
  message.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;

  Serial.println("Opening SMTP session...");
  if (!smtp.connect(&session)) {
    Serial.println("SMTP connection failed.");
    return false;
  }

  bool sent = MailClient.sendMail(&smtp, &message, true);
  smtp.closeSession();
  return sent;
}

void maybeSendAlertEmail(const String &statusText) {
  if (!(statusText == "Far" || statusText == "Lost")) {
    return;
  }

  unsigned long elapsed = millis() - lastEmailSentAtMs;
  bool cooldownActive = lastEmailSentAtMs != 0 && elapsed < EMAIL_COOLDOWN_MS;
  if (cooldownActive) {
    return;
  }

  if (sendEmailAlert(statusText)) {
    lastEmailSentAtMs = millis();
  }
}

String jsonBool(bool value) {
  return value ? "true" : "false";
}

void handleRoot() {
  String statusText = currentPetStatus();
  unsigned long packetAge = packetReceived ? millis() - lastPacketAtMs : 0;

  String html;
  html += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='3'>";
  html += "<title>ESP8266 Pet Tracker</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;background:#f4f7fb;color:#1f2937;margin:0;padding:24px;}";
  html += ".card{max-width:760px;margin:0 auto;background:#fff;border-radius:8px;padding:24px;box-shadow:0 10px 30px rgba(0,0,0,0.08);}";
  html += "h1{margin-top:0;}table{width:100%;border-collapse:collapse;}td{padding:10px;border-bottom:1px solid #e5e7eb;}";
  html += ".status{font-weight:bold;font-size:1.2rem;}a.button{display:inline-block;margin-top:16px;background:#1f2937;color:#fff;padding:10px 14px;border-radius:6px;text-decoration:none;}";
  html += "code{background:#eef2ff;padding:2px 6px;border-radius:6px;}";
  html += "</style></head><body><div class='card'>";
  html += "<h1>IoT Pet Tracker Dashboard</h1>";
  html += "<p>JSON API: <code>/json</code></p>";
  html += "<table>";
  html += "<tr><td>Pet status</td><td class='status'>" + statusText + "</td></tr>";
  html += "<tr><td>Pet ID</td><td>" + String(packetReceived ? lastPacket.petId : EXPECTED_PET_ID) + "</td></tr>";
  html += "<tr><td>Pet Node MAC</td><td>" + formatMac(PET_NODE_MAC) + "</td></tr>";
  html += "<tr><td>RSSI</td><td>" + String(lastCapturedRssi) + " dBm</td></tr>";
  html += "<tr><td>RSSI sample</td><td>" + String(hasRecentRssiSample() ? "Recent" : "Waiting") + "</td></tr>";
  html += "<tr><td>Last packet age</td><td>" + String(packetAge) + " ms</td></tr>";
  html += "<tr><td>Packet counter</td><td>" + String(packetReceived ? lastPacket.packetCounter : 0) + "</td></tr>";
  html += "<tr><td>Battery</td><td>" + String(packetReceived ? lastPacket.batteryVoltage : 0, 2) + " V</td></tr>";
  html += "<tr><td>Buzzer state</td><td>" + String(buzzerOutputHigh ? "ON" : "OFF") + "</td></tr>";
  html += "<tr><td>Buzzer silence</td><td>" + String(buzzerSilenced() ? "Active" : "Inactive") + "</td></tr>";
  html += "<tr><td>Email cooldown</td><td>" + emailCooldownState() + "</td></tr>";
  html += "<tr><td>Home Node IP</td><td>" + homeIpString + "</td></tr>";
  html += "<tr><td>WiFi channel</td><td>" + String(wifiChannel) + "</td></tr>";
  html += "</table>";
  html += "<a class='button' href='/silence'>Silence buzzer for 2 minutes</a>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handleJson() {
  String statusText = currentPetStatus();
  unsigned long packetAge = packetReceived ? millis() - lastPacketAtMs : 0;
  bool cooldownActive = lastEmailSentAtMs != 0 && (millis() - lastEmailSentAtMs) < EMAIL_COOLDOWN_MS;

  String json = "{";
  json += "\"petId\":\"" + String(packetReceived ? lastPacket.petId : EXPECTED_PET_ID) + "\",";
  json += "\"status\":\"" + statusText + "\",";
  json += "\"rssi\":" + String(lastCapturedRssi) + ",";
  json += "\"rssiRecent\":" + jsonBool(hasRecentRssiSample()) + ",";
  json += "\"lastPacketAgeMs\":" + String(packetAge) + ",";
  json += "\"packetCounter\":" + String(packetReceived ? lastPacket.packetCounter : 0) + ",";
  json += "\"batteryVoltage\":" + String(packetReceived ? lastPacket.batteryVoltage : 0, 2) + ",";
  json += "\"buzzerOn\":" + jsonBool(buzzerOutputHigh) + ",";
  json += "\"buzzerSilenced\":" + jsonBool(buzzerSilenced()) + ",";
  json += "\"emailCooldownActive\":" + jsonBool(cooldownActive) + ",";
  json += "\"petNodeMac\":\"" + formatMac(PET_NODE_MAC) + "\",";
  json += "\"homeNodeIp\":\"" + homeIpString + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

void handleSilence() {
  buzzerSilencedUntilMs = millis() + BUZZER_SILENCE_MS;
  writeBuzzer(false);
  Serial.println("Buzzer silenced from dashboard.");
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

void startWebServer() {
  server.on("/", handleRoot);
  server.on("/json", handleJson);
  server.on("/silence", handleSilence);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Web server started on port 80.");
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== Home Node Boot ===");

  pinMode(BUZZER_PIN, OUTPUT);
  writeBuzzer(false);

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

  String statusText = currentPetStatus();
  updateBuzzerPattern(statusText);

  bool newAlertState = statusText == "Far" || statusText == "Lost";
  if (newAlertState && !alertConditionActive) {
    Serial.print("Alert condition entered: ");
    Serial.println(statusText);
  }
  if (newAlertState) {
    maybeSendAlertEmail(statusText);
  }
  alertConditionActive = newAlertState;

  if (millis() - lastStatusLogAtMs >= 2000) {
    lastStatusLogAtMs = millis();
    Serial.print("Status=");
    Serial.print(statusText);
    Serial.print(", RSSI=");
    Serial.print(lastCapturedRssi);
    Serial.print(", RSSIRecent=");
    Serial.print(hasRecentRssiSample() ? "yes" : "no");
    Serial.print(", PacketAgeMs=");
    Serial.print(packetReceived ? millis() - lastPacketAtMs : 0);
    Serial.print(", Buzzer=");
    Serial.println(buzzerOutputHigh ? "ON" : "OFF");
  }

  if (!espNowReady) {
    Serial.println("ESP-NOW not ready. Attempting re-init...");
    espNowReady = initEspNow();
  }
}
