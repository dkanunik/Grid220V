#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

// ---------- SETTINGS ----------
const char* WIFI_SSID = "...";
const char* WIFI_PASS = "...";

const char* HC_URL = "...";

const char* TOKEN = "0123456789";

const uint16_t UDP_PORT = 4210;

const long GMT_OFFSET_SEC = 2 * 3600;  // Україна взимку (UTC+2)
const int DAYLIGHT_OFFSET_SEC = 0;     // взимку = 0
const char* NTP_SERVER = "pool.ntp.org";

const unsigned long LOST_AFTER_MS = 20000;      // no packets for 20 s -> grid lost
const unsigned long STARTUP_GRACE_MS = 5000;    // wait after boot before alarming
const unsigned long PING_INTERVAL_MS = 300000;  // heartbeat to healthchecks.io

const int PIN_GREEN = 18;
const int PIN_RED = 19;
const int PIN_BUZZER = 21;
const int PIN_BUTTON = 23;  // to GND, internal pull-up

const int TIME_OFF = 22;
const int TIME_ON = 8;

// A 50 ms HIGH pulse is barely noticeable on an active buzzer.  Keep the
// existing "short alarm / long pause" behaviour, but make the alarm audible.
const int ALARM_TIME_ON = 100;
const int ALARM_TIME_OFF = 4000;

// ---------- STATE ----------
enum State { ST_STARTUP,
             ST_OK,
             ST_LOST,
             ST_NO_WIFI };

WiFiUDP udp;
bool udpStarted = false;
bool haveHeard = false;
bool wifiWasOk = false;
bool muted = false;
bool isNight = false;

unsigned long lastPacket = 0;
unsigned long graceUntil = 0;
unsigned long lastPing = 0;
unsigned long lastBeepToggle = 0;

bool beepState = false;
bool welcomeBeepDone = true;
int welcomeBeepCount = 0;

State state = ST_STARTUP;
State prevState = ST_NO_WIFI;

const char* stateName(State s) {
  switch (s) {
    case ST_STARTUP: return "STARTUP";
    case ST_OK: return "GRID OK";
    case ST_LOST: return "GRID LOST";
    default: return "NO WI-FI";
  }
}

bool checkNight() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return false;  // якщо час невідомий — вважаємо день
  }
  int hour = timeinfo.tm_hour;
  return (hour >= TIME_OFF || hour < TIME_ON);
}

void sendPing(bool ok) {
  if (strstr(HC_URL, "YOUR-UUID")) {
    Serial.println("Healthchecks URL not set, skipping ping");
    return;
  }

  Serial.println();

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = String(HC_URL) + (ok ? "" : "/fail");
  if (http.begin(client, url)) {
    http.setTimeout(5000);
    int code = http.GET();
    Serial.printf("Ping %s -> HTTP %d\n", ok ? "OK" : "FAIL", code);
    http.end();
  } else {
    Serial.println("Ping: could not start HTTP request");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_GREEN, OUTPUT);
  pinMode(PIN_RED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  digitalWrite(PIN_GREEN, LOW);
  digitalWrite(PIN_RED, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(200);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connecting to Wi-Fi");
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 30000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected. IP address: ");
    Serial.println(WiFi.localIP());

    // Налаштування часу
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    Serial.println("Waiting for NTP time...");
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10000)) {
      Serial.println(&timeinfo, "Time set: %H:%M:%S");
    } else {
      Serial.println("Failed to get time from NTP");
    }
  } else {
    Serial.println("Not connected yet, will keep trying in background");
  }

  graceUntil = millis() + STARTUP_GRACE_MS;
}

void loop() {
  unsigned long now = millis();
  bool wifiOk = (WiFi.status() == WL_CONNECTED);

  isNight = checkNight();

  // debug
  // static unsigned long lastDiag = 0;
  // if (now - lastDiag > 3000) {
  //   lastDiag = now;
  //   Serial.printf(
  //     "DEBUG: State=%s muted=%d button=%d isNight=%d beepState=%d\n",
  //     stateName(state), muted, digitalRead(PIN_BUTTON), isNight, beepState
  //   );
  // }

  if (wifiOk && !udpStarted) {
    udp.begin(UDP_PORT);
    udpStarted = true;
    Serial.println("Listening for UDP packets");
  }
  if (wifiOk && !wifiWasOk) {
    Serial.println("Wi-Fi connected");
    if (haveHeard) graceUntil = now + LOST_AFTER_MS;
  }
  if (!wifiOk && wifiWasOk) Serial.println("Wi-Fi lost");
  wifiWasOk = wifiOk;

  // receive packets from board A
  if (udpStarted && wifiOk) {
    while (udp.parsePacket() > 0) {
      char buf[64];
      int n = udp.read(buf, sizeof(buf) - 1);
      if (n < 0) n = 0;
      buf[n] = 0;
      if (strcmp(buf, TOKEN) == 0) {
        lastPacket = now;
        haveHeard = true;
      }
    }
  }

  // decide state
  bool heardRecently = haveHeard && (now - lastPacket < LOST_AFTER_MS);
  bool inGrace = (long)(graceUntil - now) > 0;
  if (!wifiOk) state = ST_NO_WIFI;
  else if (heardRecently) state = ST_OK;
  else if (inGrace) state = ST_STARTUP;
  else state = ST_LOST;

  bool pingNow = false;
  if (state != prevState) {
    Serial.printf("State: %s\n", stateName(state));
    if (state == ST_OK) {
      muted = false;
      welcomeBeepDone = false;
      welcomeBeepCount = 0;
      lastBeepToggle = now;
      beepState = false;
    }
    if (state == ST_LOST) {
      lastBeepToggle = now;
      beepState = true;
      // Start the first alarm pulse immediately.  Previously beepState was
      // set to true but the output was left LOW; the next scheduled change
      // turned it LOW again, so the first audible pulse was delayed 4 seconds.
      if (!muted && !isNight) digitalWrite(PIN_BUZZER, HIGH);
    }
    if (state == ST_OK || state == ST_LOST) pingNow = true;
    prevState = state;
  }

  // mute button
  if (state == ST_LOST && digitalRead(PIN_BUTTON) == LOW) muted = true;

  // outputs
  bool blink = ((now / 1000) % 2) == 0;

  switch (state) {
    case ST_OK:
      digitalWrite(PIN_GREEN, HIGH);
      digitalWrite(PIN_RED, LOW);

      // Короткі писки при появі мережі (тільки один раз)
      if (!welcomeBeepDone) {
        if (welcomeBeepCount < 6) {
          if (now - lastBeepToggle >= 100) {
            lastBeepToggle = now;
            beepState = !beepState;
            digitalWrite(PIN_BUZZER, beepState ? HIGH : LOW);
            if (!beepState) welcomeBeepCount++;
          }
        } else {
          digitalWrite(PIN_BUZZER, LOW);
          welcomeBeepDone = true;
        }
      } else {
        digitalWrite(PIN_BUZZER, LOW);
      }
      break;

    case ST_LOST:
      digitalWrite(PIN_GREEN, LOW);
      digitalWrite(PIN_RED, HIGH);

      // Довгі сигнали тільки вдень і якщо не натиснута кнопка
      if (!muted && !isNight) {
        if (now - lastBeepToggle >= (beepState ? ALARM_TIME_ON : ALARM_TIME_OFF)) {
          lastBeepToggle = now;
          beepState = !beepState;
          digitalWrite(PIN_BUZZER, beepState ? HIGH : LOW);
        }
      } else {
        digitalWrite(PIN_BUZZER, LOW);
      }
      break;

    case ST_STARTUP:
      digitalWrite(PIN_GREEN, blink ? HIGH : LOW);
      digitalWrite(PIN_RED, LOW);
      digitalWrite(PIN_BUZZER, LOW);
      welcomeBeepDone = false;
      welcomeBeepCount = 0;
      break;

    default:  // NO_WIFI
      digitalWrite(PIN_GREEN, blink ? HIGH : LOW);
      digitalWrite(PIN_RED, blink ? LOW : HIGH);
      digitalWrite(PIN_BUZZER, LOW);
      break;
  }

  // healthchecks.io
  if (wifiOk && (state == ST_OK || state == ST_LOST) && (pingNow || now - lastPing >= PING_INTERVAL_MS)) {
    lastPing = now;
    sendPing(state == ST_OK);
  }

  delay(20);
}
