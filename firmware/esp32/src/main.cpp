/**
 * @file main.cpp
 * @brief Application entry point that exposes the armor USB CDC protocol.
 */

#include <Arduino.h>

#include "serial_protocol.h"

namespace {
armor::led::Controller led_controller;
armor::serial_protocol::Endpoint serial_endpoint(led_controller);
}

void setup() {
  Serial.begin(115200);
  led_controller.begin();
  serial_endpoint.begin(Serial);
}

void loop() { serial_endpoint.poll(); }
