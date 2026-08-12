/**
 * @file serial_protocol.h
 * @brief Bounded COBS-framed USB CDC protocol endpoint for the armor module.
 *
 * This module owns wire encoding, validation, request dispatch, and protocol
 * responses. Hardware sampling and LED control remain outside this boundary.
 */

#pragma once

#include <Arduino.h>

#include "led_controller.h"

namespace armor::serial_protocol {

constexpr uint8_t kProtocolVersion = 1;
constexpr size_t kMaxPayloadLength = 192;
constexpr size_t kMaxRawFrameLength = 200;
constexpr size_t kMaxEncodedFrameLength = 202;

class Endpoint {
 public:
  explicit Endpoint(led::Controller& led_controller);

  /** Starts the USB CDC endpoint. No human-readable text is emitted. */
  void begin(Stream& serial);

  /** Consumes all currently available serial bytes without blocking. */
  void poll();

 private:
  void handle_encoded_frame(const uint8_t* encoded, size_t encoded_length);
  void handle_request(const uint8_t* raw, size_t raw_length);
  void send_frame(uint8_t type, uint16_t sequence, const uint8_t* payload,
                  size_t payload_length);
  void send_error(uint16_t sequence, uint8_t command, uint8_t error_code);
  void send_device_info(uint16_t sequence, const uint8_t* payload,
                        size_t payload_length);
  void send_status(uint16_t sequence, size_t payload_length);
  void set_led_color(uint16_t sequence, const uint8_t* payload,
                     size_t payload_length);

  led::Controller& led_controller_;
  Stream* serial_ = nullptr;
  uint8_t encoded_frame_[kMaxEncodedFrameLength]{};
  size_t encoded_length_ = 0;
  bool frame_overflowed_ = false;
  bool handshake_complete_ = false;
};

}  // namespace armor::serial_protocol
