/*
  Diagnostic sketch: raw ADC reader for GPIO34.

  Purpose: isolate whether the ESP32<->AD8232 OUTPUT connection is solid,
  separately from WiFi/WebSocket/server, to explain the rail-to-rail
  (0 / 4095) glitching seen in the full ECG pipeline.

  How to use:
    1. Flash this sketch (Arduino IDE: select ESP32 Dev Module, correct port).
    2. Open Serial Monitor at 115200 baud.
    3. With NOTHING connected to GPIO34: values should look wildly random/jumpy
       if the pin is truly floating -- that's expected and fine, not a bug.
    4. Now connect AD8232 OUTPUT to GPIO34 as normal. Values should settle into
       a fairly stable range roughly in the middle third of 0-4095 (varies with
       your board/electrodes), not constantly slamming to 0 or 4095.
    5. While watching Serial Monitor, gently wiggle/press each connection point
       (the GPIO34 jumper, the AD8232 module in its breadboard slot, the
       electrode snap connectors). If the numbers jump to 0/4095 or go crazy
       exactly when you touch a specific point, that's your loose connection.
    6. Also touch the bare exposed metal of the wire going into GPIO34 with a
       finger -- if the ESP32 is otherwise idle/unconnected to WiFi, touching
       a floating analog pin usually causes visible noise, confirming the ADC
       itself is working; if it does nothing at all even floating, suspect a
       bad GPIO34 pin or board.

  This sketch does NOT connect to WiFi, so WiFi radio activity is ruled out
  as a noise source while you test.
*/

const int ECG_PIN = 34;

void setup() {
  Serial.begin(115200);
  analogReadResolution(12); // 0-4095, matches main firmware
  delay(500);
  Serial.println("ADC pin test starting on GPIO34...");
}

void loop() {
  int value = analogRead(ECG_PIN);
  Serial.println(value);
  delay(100);
}
