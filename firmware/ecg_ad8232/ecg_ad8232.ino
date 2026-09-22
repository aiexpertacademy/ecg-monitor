/*
  ESP32 + AD8232 ECG front-end
  ----------------------------
  Reads the AD8232 analog output and streams samples over WebSocket to the
  local Node.js server (see /server), which broadcasts them to the browser
  dashboard for live graphing and BPM calculation.

  Wiring (typical AD8232 breakout):
    AD8232 OUTPUT -> ESP32 ADC pin (default: GPIO34, input-only ADC1 pin)
    AD8232 LO+    -> ESP32 GPIO32 (optional: leads-off detect)
    AD8232 LO-    -> ESP32 GPIO33 (optional: leads-off detect)
    AD8232 3.3V   -> ESP32 3V3
    AD8232 GND    -> ESP32 GND

  Required library (Arduino Library Manager):
    "WebSockets" by Markus Sattler (arduinoWebSockets)

  Configure WIFI_SSID / WIFI_PASSWORD / SERVER_HOST below, then flash.
*/

#include <WiFi.h>
#include <WebSocketsClient.h>
#include "secrets.h" // defines WIFI_SSID / WIFI_PASSWORD -- copy secrets.h.example, see that file

// ---- Configure these ----
// Set to true when pointing at a cloud host (e.g. Render), which serves
// HTTPS/WSS only. Set to false for local testing against `npm start` on
// your own PC (plain ws:// on your LAN).
const bool USE_SSL = false;

// Local testing: your PC's LAN IP, plain ws://, port 8080.
// Cloud (Render, etc.): the hostname only (no https://), wss://, port 443.
const char* SERVER_HOST   = "192.168.29.202"; // e.g. "ecg-monitor-xxxx.onrender.com"
const uint16_t SERVER_PORT = 8080;             // local: 8080, cloud: 443
const char* SERVER_PATH   = "/ws";

// ---- Pins ----
const int ECG_PIN   = 34; // ADC1 channel, input-only pin
const int LO_PLUS    = 32;
const int LO_MINUS   = 33;

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

void setup() {
  Serial.begin(115200);
  pinMode(LO_PLUS, INPUT);
  pinMode(LO_MINUS, INPUT);
  analogReadResolution(12); // 0-4095

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());

  if (USE_SSL) {
    webSocket.beginSSL(SERVER_HOST, SERVER_PORT, SERVER_PATH);
  } else {
    webSocket.begin(SERVER_HOST, SERVER_PORT, SERVER_PATH);
  }
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(2000);

  lastSampleMicros = micros();
}

void loop() {
  webSocket.loop();

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
