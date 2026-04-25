#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <espnow.h>
#include <ESP_Mail_Client.h>

extern "C" {
  #include <user_interface.h>
}

// =========================
// User configuration
// Edit these placeholders before uploading.
// =========================
const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char *SMTP_HOST = "smtp.example.com";
const int SMTP_PORT = 465;
const char *SENDER_EMAIL = "sender@example.com";
const char *SENDER_APP_PASSWORD = "YOUR_APP_PASSWORD";
const char *RECIPIENT_EMAIL = "recipient@example.com";

const int RSSI_THRESHOLD_DBM = -75;
const unsigned long PACKET_TIMEOUT_MS = 10000;
const unsigned long EMAIL_COOLDOWN_MS = 300000;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;

const uint8_t BUZZER_PIN = D5;
const char *EXPECTED_PET_ID = "PET-001";

// Keep this struct identical to the pet node.
struct PetPacket {
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
bool buzzerOn = false;
bool alertConditionActive = false;
bool emailConfigured = true;

unsigned long lastPacketAtMs = 0;
unsigned long lastStatusLogAtMs = 0;
unsigned long lastEmailSentAtMs = 0;
uint8_t wifiChannel = 0;
String homeIpString = "0.0.0.0";
String lastStatus = "Waiting";

String formatMac(const uint8_t *mac) {
  char buffer[18];
  snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buffer);
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
  return true;
}

void ICACHE_FLASH_ATTR promiscuousCallback(uint8_t *buffer, uint16_t length) {
  if (length < 24) {
    return;
  }

  PromiscuousPacket *packet = reinterpret_cast<PromiscuousPacket *>(buffer);

  // ESP8266 ESP-NOW receive callbacks do not directly provide packet RSSI.
  // This promiscuous callback captures radio metadata on the same channel and
  // stores the most recent RSSI as an approximate signal-strength indicator.
  lastCapturedRssi = packet->rx_ctrl.rssi;
  lastPromiscPacketAtMs = millis();
  rssiUpdated = true;
}

void startPromiscuousSniffer() {
  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(promiscuousCallback);
  wifi_promiscuous_enable(1);
  Serial.println("Promiscuous RSSI sniffer enabled.");
}

void stopPromiscuousSniffer() {
  wifi_promiscuous_enable(0);
}

void onDataReceived(uint8_t *senderMac, uint8_t *incomingData, uint8_t len) {
  if (len != sizeof(PetPacket)) {
    Serial.print("Unexpected packet size: ");
    Serial.println(len);
    return;
  }

  memcpy(&lastPacket, incomingData, sizeof(lastPacket));
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
    Serial.print("Approx RSSI: ");
    Serial.println(lastCapturedRssi);
  } else {
    Serial.println("Approx RSSI not updated yet.");
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

bool shouldTurnBuzzerOn() {
  if (!packetReceived) {
    return false;
  }

  unsigned long ageMs = millis() - lastPacketAtMs;
  return ageMs > PACKET_TIMEOUT_MS || (hasRecentRssiSample() && lastCapturedRssi <= RSSI_THRESHOLD_DBM);
}

void setBuzzer(bool enabled) {
  buzzerOn = enabled;
  digitalWrite(BUZZER_PIN, enabled ? HIGH : LOW);
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
  html += ".card{max-width:720px;margin:0 auto;background:#fff;border-radius:16px;padding:24px;box-shadow:0 10px 30px rgba(0,0,0,0.08);}";
  html += "h1{margin-top:0;}table{width:100%;border-collapse:collapse;}td{padding:10px;border-bottom:1px solid #e5e7eb;}";
  html += ".status{font-weight:bold;font-size:1.2rem;}.ok{color:#0f766e;}.warn{color:#b45309;}.bad{color:#b91c1c;}";
  html += "code{background:#eef2ff;padding:2px 6px;border-radius:6px;}";
  html += "</style></head><body><div class='card'>";
  html += "<h1>IoT Pet Tracker Dashboard</h1>";
  html += "<p>JSON API: <code>/json</code></p>";
  html += "<table>";
  html += "<tr><td>Pet status</td><td class='status'>" + statusText + "</td></tr>";
  html += "<tr><td>Pet ID</td><td>" + String(packetReceived ? lastPacket.petId : EXPECTED_PET_ID) + "</td></tr>";
  html += "<tr><td>RSSI</td><td>" + String(lastCapturedRssi) + " dBm</td></tr>";
  html += "<tr><td>Last packet age</td><td>" + String(packetAge) + " ms</td></tr>";
  html += "<tr><td>Packet counter</td><td>" + String(packetReceived ? lastPacket.packetCounter : 0) + "</td></tr>";
  html += "<tr><td>Buzzer state</td><td>" + String(buzzerOn ? "ON" : "OFF") + "</td></tr>";
  html += "<tr><td>Email cooldown</td><td>" + emailCooldownState() + "</td></tr>";
  html += "<tr><td>Home Node IP</td><td>" + homeIpString + "</td></tr>";
  html += "<tr><td>WiFi channel</td><td>" + String(wifiChannel) + "</td></tr>";
  html += "</table>";
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
  json += "\"lastPacketAgeMs\":" + String(packetAge) + ",";
  json += "\"packetCounter\":" + String(packetReceived ? lastPacket.packetCounter : 0) + ",";
  json += "\"buzzerOn\":" + jsonBool(buzzerOn) + ",";
  json += "\"emailCooldownActive\":" + jsonBool(cooldownActive) + ",";
  json += "\"homeNodeIp\":\"" + homeIpString + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

void startWebServer() {
  server.on("/", handleRoot);
  server.on("/json", handleJson);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Web server started on port 80.");
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== Home Node Boot ===");

  pinMode(BUZZER_PIN, OUTPUT);
  setBuzzer(false);

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
  setBuzzer(shouldTurnBuzzerOn());

  bool newAlertState = statusText == "Far" || statusText == "Lost";
  if (newAlertState && !alertConditionActive) {
    Serial.print("Alert condition entered: ");
    Serial.println(statusText);
  }
  if (newAlertState) {
    maybeSendAlertEmail(statusText);
  }
  alertConditionActive = newAlertState;
  lastStatus = statusText;

  if (millis() - lastStatusLogAtMs >= 2000) {
    lastStatusLogAtMs = millis();
    Serial.print("Status=");
    Serial.print(statusText);
    Serial.print(", RSSI=");
    Serial.print(lastCapturedRssi);
    Serial.print(", PacketAgeMs=");
    Serial.print(packetReceived ? millis() - lastPacketAtMs : 0);
    Serial.print(", Buzzer=");
    Serial.println(buzzerOn ? "ON" : "OFF");
  }

  if (!espNowReady) {
    Serial.println("ESP-NOW not ready. Attempting re-init...");
    espNowReady = initEspNow();
  }
}
