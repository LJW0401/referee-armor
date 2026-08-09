/**
 * @file main.cpp
 * @brief Minimal hardware smoke test for the ESP32-C3 SuperMini.
 *
 * Board mapping: the onboard blue LED is connected to GPIO8.
 */

#include <Arduino.h>

namespace {
constexpr uint8_t kOnboardLedPin = 8;
constexpr uint32_t kBlinkIntervalMs = 500;
}

void setup() {
  Serial.begin(115200);
  pinMode(kOnboardLedPin, OUTPUT);
  Serial.println("ESP32-C3 SuperMini started");
}

void loop() {
  digitalWrite(kOnboardLedPin, !digitalRead(kOnboardLedPin));
  delay(kBlinkIntervalMs);
}
