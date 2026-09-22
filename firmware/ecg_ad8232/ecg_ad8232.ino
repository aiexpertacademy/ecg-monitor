/*
  ESP32 + AD8232 ECG front-end
  ----------------------------
  Reads the AD8232 analog output and streams samples over WebSocket to the
  Node.js server (see /server), which broadcasts them to the browser
  dashboard for live graphing and BPM calculation.

  WiFi setup (no reflashing needed per device/network):
    On first boot -- or if it can't reconnect to a previously saved network --
    the ESP32 opens its own WiFi hotspot named "ECG-Setup". Connect to that
    hotspot with a phone or laptop, a setup page should open automatically
    (or open a browser and go to 192.168.4.1); pick your WiFi network, enter
    its password, and save. The ESP32 remembers it in flash and auto-connects
    on every future boot -- even after power loss.

    To make it forget a saved network (e.g. moving the device to a new
    WiFi), hold the BOOT button for 5 seconds WHILE THE DEVICE IS ALREADY
    RUNNING NORMALLY (fully booted, not during power-on/reset). GPIO0/BOOT
    is also the chip's hardware boot-mode-select pin, so holding it during
    an actual power-on or reset risks dropping the chip into its flashing
    bootloader instead of running this sketch at all -- checking for a long
    press only after normal runtime has started avoids that entirely.

  Wiring (typical AD8232 breakout):
    AD8232 OUTPUT -> ESP32 ADC pin (default: GPIO34, input-only ADC1 pin)
    AD8232 LO+    -> ESP32 GPIO32 (optional: leads-off detect)
    AD8232 LO-    -> ESP32 GPIO33 (optional: leads-off detect)
    AD8232 3.3V   -> ESP32 3V3
    AD8232 GND    -> ESP32 GND

  Required libraries (Arduino Library Manager):
    "WebSockets" by Markus Sattler (arduinoWebSockets)
    "WiFiManager" by tzapu

  Configure SERVER_HOST / USE_SSL below for your deployment, then flash.
*/

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <WiFiManager.h>

// ---- Configure these ----
// Set to true when pointing at a cloud host (e.g. Render), which serves
// HTTPS/WSS only. Set to false for local testing against `npm start` on
// your own PC (plain ws:// on your LAN).
const bool USE_SSL = true;

// Local testing: your PC's LAN IP, plain ws://, port 8080.
// Cloud (Render, etc.): the hostname only (no https://), wss://, port 443.
const char* SERVER_HOST   = "ecg-monitor-15ti.onrender.com";
const uint16_t SERVER_PORT = 443;
const char* SERVER_PATH   = "/ws";

// ---- Pins ----
const int ECG_PIN   = 34; // ADC1 channel, input-only pin
const int LO_PLUS    = 32;
const int LO_MINUS   = 33;
const int WIFI_RESET_PIN = 0; // BOOT button on most ESP32 DevKit boards
const uint32_t WIFI_RESET_HOLD_MS = 5000; // long-press duration to trigger a WiFi reset

// ---- Sampling ----
const uint32_t SAMPLE_RATE_HZ = 250;
const uint32_t SAMPLE_INTERVAL_US = 1000000UL / SAMPLE_RATE_HZ;

WebSocketsClient webSocket;
uint32_t lastSampleMicros = 0;

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.println("[ws] connected to server");
      break;
    case WStype_DISCONNECTED:
      Serial.println("[ws] disconnected");
      break;
    default:
      break;
  }
}

void setupWiFi() {
  pinMode(WIFI_RESET_PIN, INPUT_PULLUP);
  WiFiManager wm;
  wm.setConfigPortalTimeout(180); // give up and retry after 3 minutes if no one configures it

  Serial.println("Connecting to saved WiFi (or opening ECG-Setup portal)...");
  bool connected = wm.autoConnect("ECG-Setup");

  if (!connected) {
    Serial.println("Failed to connect / portal timed out -- restarting...");
    delay(1000);
    ESP.restart();
  }

  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);
  pinMode(LO_PLUS, INPUT);
  pinMode(LO_MINUS, INPUT);
  analogReadResolution(12); // 0-4095

  setupWiFi();

  if (USE_SSL) {
    webSocket.beginSSL(SERVER_HOST, SERVER_PORT, SERVER_PATH);
  } else {
    webSocket.begin(SERVER_HOST, SERVER_PORT, SERVER_PATH);
  }
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(2000);

  lastSampleMicros = micros();
}

void checkWifiResetButton() {
  static uint32_t pressStart = 0;

  if (digitalRead(WIFI_RESET_PIN) == LOW) {
    if (pressStart == 0) {
      pressStart = millis();
    } else if (millis() - pressStart >= WIFI_RESET_HOLD_MS) {
      Serial.println("BOOT held 5s -- clearing saved WiFi credentials and restarting...");
      WiFiManager wm;
      wm.resetSettings();
      delay(500);
      ESP.restart();
    }
  } else {
    pressStart = 0;
  }
}

void loop() {
  webSocket.loop();
  checkWifiResetButton();

  uint32_t now = micros();
  if (now - lastSampleMicros >= SAMPLE_INTERVAL_US) {
    lastSampleMicros += SAMPLE_INTERVAL_US;

    bool leadsOff = (digitalRead(LO_PLUS) == HIGH) || (digitalRead(LO_MINUS) == HIGH);
    int value = leadsOff ? 2048 : analogRead(ECG_PIN); // flatline midline if leads off

    // Lightweight CSV payload: "<adc_value>,<millis_timestamp>"
    // The server also accepts JSON {"v":..,"t":..} if you prefer ArduinoJson.
    char msg[32];
    snprintf(msg, sizeof(msg), "%d,%lu", value, millis());
    webSocket.sendTXT(msg);
  }
}
