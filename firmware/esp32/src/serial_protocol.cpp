/**
 * @file serial_protocol.cpp
 * @brief Implementation of the bounded COBS and CRC serial protocol endpoint.
 */

#include "serial_protocol.h"

#include <esp_system.h>

namespace armor::serial_protocol {
namespace {

constexpr uint8_t kGetDeviceInfo = 0x01;
constexpr uint8_t kGetStatus = 0x02;
constexpr uint8_t kSetLedColor = 0x10;
constexpr uint8_t kSetLedEffect = 0x11;
constexpr uint8_t kResponseMask = 0x80;
constexpr uint8_t kErrorResponse = 0xFF;

constexpr uint8_t kErrorUnsupportedVersion = 0x01;
constexpr uint8_t kErrorUnsupportedCommand = 0x02;
constexpr uint8_t kErrorInvalidPayload = 0x03;
constexpr uint8_t kErrorNotConnected = 0x04;
constexpr uint8_t kErrorInternal = 0x06;

constexpr size_t kHeaderLength = 6;
constexpr size_t kCrcLength = 2;
constexpr size_t kDeviceInfoRequestLength = 4;
constexpr size_t kDeviceInfoResponseLength = 20;
constexpr size_t kStatusResponseLength = 24;
constexpr size_t kSetLedColorRequestLength = 4;
constexpr size_t kSetLedEffectRequestLength = 1;

constexpr uint8_t kFirmwareMajor = 0;
constexpr uint8_t kFirmwareMinor = 2;
constexpr uint8_t kFirmwarePatch = 2;

uint16_t crc16_ccitt_false(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t index = 0; index < length; ++index) {
    crc ^= static_cast<uint16_t>(data[index]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) != 0 ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

void write_u16_le(uint8_t* destination, uint16_t value) {
  destination[0] = static_cast<uint8_t>(value);
  destination[1] = static_cast<uint8_t>(value >> 8);
}

void write_u32_le(uint8_t* destination, uint32_t value) {
  for (uint8_t index = 0; index < 4; ++index) {
    destination[index] = static_cast<uint8_t>(value >> (index * 8));
  }
}

void write_u64_le(uint8_t* destination, uint64_t value) {
  for (uint8_t index = 0; index < 8; ++index) {
    destination[index] = static_cast<uint8_t>(value >> (index * 8));
  }
}

uint16_t read_u16_le(const uint8_t* source) {
  return static_cast<uint16_t>(source[0]) |
         (static_cast<uint16_t>(source[1]) << 8);
}

bool cobs_decode(const uint8_t* input, size_t input_length, uint8_t* output,
                 size_t* output_length) {
  if (input_length == 0 || input_length > kMaxEncodedFrameLength) {
    return false;
  }

  size_t input_index = 0;
  size_t output_index = 0;
  while (input_index < input_length) {
    const uint8_t code = input[input_index++];
    if (code == 0 || input_index + code - 1 > input_length ||
        output_index + code - 1 > kMaxRawFrameLength) {
      return false;
    }
    for (uint8_t index = 1; index < code; ++index) {
      output[output_index++] = input[input_index++];
    }
    if (code != 0xFF && input_index < input_length) {
      if (output_index == kMaxRawFrameLength) {
        return false;
      }
      output[output_index++] = 0;
    }
  }
  *output_length = output_index;
  return true;
}

size_t cobs_encode(const uint8_t* input, size_t input_length, uint8_t* output) {
  size_t read_index = 0;
  size_t write_index = 1;
  size_t code_index = 0;
  uint8_t code = 1;
  while (read_index < input_length) {
    if (input[read_index] == 0) {
      output[code_index] = code;
      code = 1;
      code_index = write_index++;
      ++read_index;
    } else {
      output[write_index++] = input[read_index++];
      ++code;
      if (code == 0xFF) {
        output[code_index] = code;
        code = 1;
        code_index = write_index++;
      }
    }
  }
  output[code_index] = code;
  return write_index;
}

}  // namespace

Endpoint::Endpoint(led::Controller& led_controller)
    : led_controller_(led_controller) {}

void Endpoint::begin(Stream& serial) {
  serial_ = &serial;
}

void Endpoint::poll() {
  if (serial_ == nullptr) {
    return;
  }

  while (serial_->available() > 0) {
    const uint8_t byte = static_cast<uint8_t>(serial_->read());
    if (byte == 0) {
      if (!frame_overflowed_ && encoded_length_ > 0) {
        handle_encoded_frame(encoded_frame_, encoded_length_);
      }
      encoded_length_ = 0;
      frame_overflowed_ = false;
      continue;
    }
    if (encoded_length_ == kMaxEncodedFrameLength) {
      frame_overflowed_ = true;
      continue;
    }
    if (!frame_overflowed_) {
      encoded_frame_[encoded_length_++] = byte;
    }
  }
}

void Endpoint::handle_encoded_frame(const uint8_t* encoded, size_t encoded_length) {
  uint8_t raw[kMaxRawFrameLength]{};
  size_t raw_length = 0;
  if (!cobs_decode(encoded, encoded_length, raw, &raw_length)) {
    return;
  }
  handle_request(raw, raw_length);
}

void Endpoint::handle_request(const uint8_t* raw, size_t raw_length) {
  if (raw_length < kHeaderLength + kCrcLength) {
    return;
  }
  const uint16_t payload_length = read_u16_le(raw + 4);
  if (payload_length > kMaxPayloadLength ||
      raw_length != kHeaderLength + payload_length + kCrcLength) {
    return;
  }
  const uint16_t received_crc = read_u16_le(raw + raw_length - kCrcLength);
  if (crc16_ccitt_false(raw, raw_length - kCrcLength) != received_crc) {
    return;
  }

  const uint8_t version = raw[0];
  const uint8_t command = raw[1];
  const uint16_t sequence = read_u16_le(raw + 2);
  const uint8_t* payload = raw + kHeaderLength;
  if (sequence == 0) {
    return;
  }
  if (version != kProtocolVersion) {
    send_error(sequence, command, kErrorUnsupportedVersion);
    return;
  }

  if (command == kGetDeviceInfo) {
    send_device_info(sequence, payload, payload_length);
    return;
  }
  if (command == kGetStatus) {
    send_status(sequence, payload_length);
    return;
  }
  if (command == kSetLedColor) {
    set_led_color(sequence, payload, payload_length);
    return;
  }
  if (command == kSetLedEffect) {
    set_led_effect(sequence, payload, payload_length);
    return;
  }
  send_error(sequence, command, kErrorUnsupportedCommand);
}

void Endpoint::send_device_info(uint16_t sequence, const uint8_t* payload,
                                size_t payload_length) {
  if (payload_length != kDeviceInfoRequestLength) {
    send_error(sequence, kGetDeviceInfo, kErrorInvalidPayload);
    return;
  }

  uint8_t response[kDeviceInfoResponseLength]{};
  memcpy(response, payload, kDeviceInfoRequestLength);
  write_u64_le(response + 4, ESP.getEfuseMac());
  response[12] = kProtocolVersion;
  response[13] = kFirmwareMajor;
  response[14] = kFirmwareMinor;
  response[15] = kFirmwarePatch;
  write_u32_le(response + 16, 1U << 0);
  handshake_complete_ = true;
  send_frame(kGetDeviceInfo | kResponseMask, sequence, response,
             sizeof(response));
}

void Endpoint::send_status(uint16_t sequence, size_t payload_length) {
  if (payload_length != 0) {
    send_error(sequence, kGetStatus, kErrorInvalidPayload);
    return;
  }
  if (!handshake_complete_) {
    send_error(sequence, kGetStatus, kErrorNotConnected);
    return;
  }

  uint8_t response[kStatusResponseLength]{};
  response[0] = 2;
  const uint16_t health_flags =
      (led_controller_.is_initialized() ? 1U << 4 : 0) |
      (led_controller_.is_persistence_healthy() ? 1U << 6 : 0);
  write_u16_le(response + 1, health_flags);
  write_u32_le(response + 3, millis());
  write_u32_le(response + 7, static_cast<uint32_t>(INT32_MIN));
  write_u32_le(response + 11, UINT32_MAX);
  response[15] = led_controller_.is_initialized() ? 16 : 0;
  response[16] = static_cast<uint8_t>(led_controller_.effect());
  const led::RgbColor color = led_controller_.color();
  response[17] = color.red;
  response[18] = color.green;
  response[19] = color.blue;
  response[20] = led_controller_.brightness_percent();
  send_frame(kGetStatus | kResponseMask, sequence, response, sizeof(response));
}

void Endpoint::set_led_effect(uint16_t sequence, const uint8_t* payload,
                              size_t payload_length) {
  if (payload_length != kSetLedEffectRequestLength ||
      payload[0] > static_cast<uint8_t>(led::Effect::kRandomBreathing)) {
    send_error(sequence, kSetLedEffect, kErrorInvalidPayload);
    return;
  }
  if (!handshake_complete_) {
    send_error(sequence, kSetLedEffect, kErrorNotConnected);
    return;
  }
  if (!led_controller_.set_effect(static_cast<led::Effect>(payload[0]))) {
    send_error(sequence, kSetLedEffect, kErrorInternal);
    return;
  }
  send_frame(kSetLedEffect | kResponseMask, sequence, nullptr, 0);
}

void Endpoint::set_led_color(uint16_t sequence, const uint8_t* payload,
                             size_t payload_length) {
  if (payload_length != kSetLedColorRequestLength) {
    send_error(sequence, kSetLedColor, kErrorInvalidPayload);
    return;
  }
  if (!handshake_complete_) {
    send_error(sequence, kSetLedColor, kErrorNotConnected);
    return;
  }
  if (!led_controller_.set_color({payload[0], payload[1], payload[2]},
                                 payload[3])) {
    send_error(sequence, kSetLedColor, kErrorInternal);
    return;
  }
  send_frame(kSetLedColor | kResponseMask, sequence, nullptr, 0);
}

void Endpoint::send_error(uint16_t sequence, uint8_t command,
                          uint8_t error_code) {
  const uint8_t payload[] = {command, error_code};
  send_frame(kErrorResponse, sequence, payload, sizeof(payload));
}

void Endpoint::send_frame(uint8_t type, uint16_t sequence, const uint8_t* payload,
                          size_t payload_length) {
  if (serial_ == nullptr || payload_length > kMaxPayloadLength) {
    return;
  }
  uint8_t raw[kMaxRawFrameLength]{};
  raw[0] = kProtocolVersion;
  raw[1] = type;
  write_u16_le(raw + 2, sequence);
  write_u16_le(raw + 4, static_cast<uint16_t>(payload_length));
  if (payload_length > 0) {
    memcpy(raw + kHeaderLength, payload, payload_length);
  }
  const size_t raw_without_crc_length = kHeaderLength + payload_length;
  write_u16_le(raw + raw_without_crc_length,
               crc16_ccitt_false(raw, raw_without_crc_length));

  uint8_t encoded[kMaxEncodedFrameLength]{};
  const size_t encoded_length =
      cobs_encode(raw, raw_without_crc_length + kCrcLength, encoded);
  serial_->write(encoded, encoded_length);
  serial_->write(static_cast<uint8_t>(0));
}

}  // namespace armor::serial_protocol
