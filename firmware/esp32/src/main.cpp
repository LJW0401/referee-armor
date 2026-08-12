/**
 * @file main.cpp
 * @brief Application entry point that exposes the armor USB CDC protocol.
 */

#include <Arduino.h>

#include "serial_protocol.h"

namespace {
armor::serial_protocol::Endpoint serial_endpoint;
}

void setup() {
  Serial.begin(115200);
  serial_endpoint.begin(Serial);
}

void loop() { serial_endpoint.poll(); }
