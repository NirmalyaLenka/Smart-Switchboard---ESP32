/*
  Smart Switchboard - ESP32 Firmware
  Supports up to 4 channels (relays + physical switches)
  Control via:
    - Physical wall switches (manual override, always works)
    - Local Wi-Fi web interface (no internet required)
    - MQTT over internet (optional, when broker is configured)

  Wiring per channel:
    Relay IN  -> ESP32 GPIO (active LOW)
    Switch    -> ESP32 GPIO + GND (INPUT_PULLUP)
    Relay COM -> Live wire
    Relay NO  -> Load
*/

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>

#ifdef MQTT_ENABLED
#include <PubSubClient.h>
#endif

// ─── Pin definitions ──────────────────────────────────────────────────────────
// Relay pins (active LOW - relay energises when GPIO goes LOW)
const int RELAY_PIN[4] = {26, 27, 14, 12};

// Physical switch pins (INPUT_PULLUP - LOW when switch is pressed/closed)
const int SWITCH_PIN[4] = {34, 35, 32, 33};

const int NUM_CHANNELS = 4;

// ─── Runtime state ────────────────────────────────────────────────────────────
bool relayState[NUM_CHANNELS]  = {false, false, false, false};
bool lastSwitchState[NUM_CHANNELS] = {true, true, true, true}; // HIGH = open

// ─── Wi-Fi settings ───────────────────────────────────────────────────────────
// Access-point mode (no router needed - phone connects directly)
const char* AP_SSID     = "SmartBoard";
const char* AP_PASSWORD = "switch1234";

// Station mode (connect to your home router for internet control)
// Leave empty to stay in AP-only mode
const char* STA_SSID     = "";
const char* STA_PASSWORD = "";

// ─── MQTT settings (optional internet control) ────────────────────────────────
#ifdef MQTT_ENABLED
const char* MQTT_SERVER = "broker.hivemq.com"; // replace with your broker
const int   MQTT_PORT   = 1883;
const char* MQTT_CLIENT = "esp32-switchboard";
const char* MQTT_TOPIC_CMD    = "home/switchboard/cmd";
const char* MQTT_TOPIC_STATUS = "home/switchboard/status";
WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);
#endif

// ─── Web server ───────────────────────────────────────────────────────────────
AsyncWebServer server(80);
AsyncEventSource events("/events");

Preferences prefs;

// ─── Helpers ──────────────────────────────────────────────────────────────────

void applyRelay(int ch, bool on) {
  if (ch < 0 || ch >= NUM_CHANNELS) return;
  relayState[ch] = on;
  // Relay modules are usually active LOW
  digitalWrite(RELAY_PIN[ch], on ? LOW : HIGH);
}

void saveState() {
  prefs.begin("board", false);
  for (int i = 0; i < NUM_CHANNELS; i++) {
    prefs.putBool(("ch" + String(i)).c_str(), relayState[i]);
  }
  prefs.end();
}

void loadState() {
  prefs.begin("board", true);
  for (int i = 0; i < NUM_CHANNELS; i++) {
    bool saved = prefs.getBool(("ch" + String(i)).c_str(), false);
    applyRelay(i, saved);
  }
  prefs.end();
}

String buildStatusJson() {
  StaticJsonDocument<256> doc;
  JsonArray arr = doc.createNestedArray("channels");
  for (int i = 0; i < NUM_CHANNELS; i++) {
    JsonObject obj = arr.createNestedObject();
    obj["id"]  = i;
    obj["on"]  = relayState[i];
  }
  String out;
  serializeJson(doc, out);
  return out;
}

void broadcastStatus() {
  events.send(buildStatusJson().c_str(), "status", millis());
}

// ─── Physical switch handling ─────────────────────────────────────────────────
void handleSwitches() {
  for (int i = 0; i < NUM_CHANNELS; i++) {
    bool current = digitalRead(SWITCH_PIN[i]);
    if (current != lastSwitchState[i]) {
      delay(30); // debounce
      current = digitalRead(SWITCH_PIN[i]);
      if (current != lastSwitchState[i]) {
        lastSwitchState[i] = current;
        // Toggle relay on any edge (switch flipped either way)
        applyRelay(i, !relayState[i]);
        saveState();
        broadcastStatus();
      }
    }
  }
}

// ─── Web routes ───────────────────────────────────────────────────────────────
void setupRoutes() {
  // Serve dashboard (embedded HTML)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->redirect("/dashboard");
  });

  server.on("/dashboard", HTTP_GET, [](AsyncWebServerRequest* req) {
    // The full dashboard HTML is served from SPIFFS or embedded string.
    // See data/index.html - upload with: pio run --target uploadfs
    req->send(SPIFFS, "/index.html", "text/html");
  });

  // GET /api/status - returns JSON
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "application/json", buildStatusJson());
  });

  // POST /api/toggle?ch=0
  server.on("/api/toggle", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!req->hasParam("ch")) {
      req->send(400, "application/json", "{\"error\":\"missing ch\"}");
      return;
    }
    int ch = req->getParam("ch")->value().toInt();
    applyRelay(ch, !relayState[ch]);
    saveState();
    broadcastStatus();
    req->send(200, "application/json", buildStatusJson());
  });

  // POST /api/set?ch=0&on=1
  server.on("/api/set", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!req->hasParam("ch") || !req->hasParam("on")) {
      req->send(400, "application/json", "{\"error\":\"missing params\"}");
      return;
    }
    int  ch = req->getParam("ch")->value().toInt();
    bool on = req->getParam("on")->value() == "1";
    applyRelay(ch, on);
    saveState();
    broadcastStatus();
    req->send(200, "application/json", buildStatusJson());
  });

  // POST /api/alloff
  server.on("/api/alloff", HTTP_POST, [](AsyncWebServerRequest* req) {
    for (int i = 0; i < NUM_CHANNELS; i++) applyRelay(i, false);
    saveState();
    broadcastStatus();
    req->send(200, "application/json", buildStatusJson());
  });

  // Server-Sent Events for real-time push
  events.onConnect([](AsyncEventSourceClient* client) {
    client->send(buildStatusJson().c_str(), "status", millis(), 1000);
  });
  server.addHandler(&events);

  server.begin();
}

// ─── MQTT ─────────────────────────────────────────────────────────────────────
#ifdef MQTT_ENABLED
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, msg)) return;

  int  ch = doc["ch"]  | -1;
  int  on = doc["on"]  | -1;

  if (ch >= 0 && ch < NUM_CHANNELS) {
    if (on == -1) applyRelay(ch, !relayState[ch]);
    else          applyRelay(ch, on == 1);
    saveState();
    broadcastStatus();
    mqttClient.publish(MQTT_TOPIC_STATUS, buildStatusJson().c_str());
  }
}

void mqttReconnect() {
  if (!mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
    if (mqttClient.connect(MQTT_CLIENT)) {
      mqttClient.subscribe(MQTT_TOPIC_CMD);
    }
  }
}
#endif

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // Relay pins
  for (int i = 0; i < NUM_CHANNELS; i++) {
    pinMode(RELAY_PIN[i], OUTPUT);
    digitalWrite(RELAY_PIN[i], HIGH); // relays off at boot
  }

  // Switch pins with internal pull-up
  for (int i = 0; i < NUM_CHANNELS; i++) {
    pinMode(SWITCH_PIN[i], INPUT_PULLUP);
    lastSwitchState[i] = digitalRead(SWITCH_PIN[i]);
  }

  // SPIFFS for serving the web dashboard
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
  }

  // Restore last known relay states from flash
  loadState();

  // Start Wi-Fi in AP+STA mode
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  if (strlen(STA_SSID) > 0) {
    WiFi.begin(STA_SSID, STA_PASSWORD);
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
      delay(500);
      tries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("STA IP: ");
      Serial.println(WiFi.localIP());
    }
  }

  setupRoutes();

#ifdef MQTT_ENABLED
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
#endif

  Serial.println("Smart Switchboard ready");
}

// ─── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
  handleSwitches();

#ifdef MQTT_ENABLED
  mqttReconnect();
  mqttClient.loop();
#endif

  delay(10);
}
