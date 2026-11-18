#include <WiFi.h>
#include <WiFiUdp.h>

const char* WIFI_SSID = "iPhone2";
const char* WIFI_PASS = "111QQQaaazzz...";

const uint16_t UDP_PORT = 4210;
const char* TOKEN = "0123456789";          // must be the same on board A and board B
const unsigned long SEND_INTERVAL_MS = 2000;      // send a packet every 2 seconds
const unsigned long RESTART_AFTER_MS = 60000;     // restart if Wi-Fi is lost for 1 minute

const int LED_PIN = 2;   // built-in LED on most ESP32 DevKit boards

WiFiUDP udp;
unsigned long lastSend = 0;
unsigned long disconnectedSince = 0;

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected. IP address: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    if (disconnectedSince == 0) {
      disconnectedSince = millis();
      Serial.println("Wi-Fi connection lost, waiting for reconnect...");
    }
    if (millis() - disconnectedSince > RESTART_AFTER_MS) {
      Serial.println("Wi-Fi not restored, restarting.");
      ESP.restart();
    }
    delay(500);
    return;
  }
  disconnectedSince = 0;

  if (millis() - lastSend >= SEND_INTERVAL_MS) {
    lastSend = millis();

    IPAddress bcast((uint32_t)WiFi.localIP() | ~(uint32_t)WiFi.subnetMask());
    udp.beginPacket(bcast, UDP_PORT);
    udp.print(TOKEN);
    udp.endPacket();

    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    Serial.print("Packet sent. Wi-Fi signal (RSSI): ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  }
}