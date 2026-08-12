/**
 * @file led_controller.cpp
 * @brief WS2812 driver for left GPIO0 and right GPIO21 light strips.
 */

#include "led_controller.h"

#include <Adafruit_NeoPixel.h>

namespace armor::led {
namespace {

constexpr uint8_t kPixelsPerStrip = 8;
constexpr uint8_t kLeftStripPin = 0;
constexpr uint8_t kRightStripPin = 21;
constexpr neoPixelType kPixelType = NEO_GRB + NEO_KHZ800;

Adafruit_NeoPixel left_strip(kPixelsPerStrip, kLeftStripPin, kPixelType);
Adafruit_NeoPixel right_strip(kPixelsPerStrip, kRightStripPin, kPixelType);

void fill_strip(Adafruit_NeoPixel& strip, uint32_t packed_color) {
  for (uint8_t pixel = 0; pixel < kPixelsPerStrip; ++pixel) {
    strip.setPixelColor(pixel, packed_color);
  }
}

}  // namespace

void Controller::begin() {
  left_strip.begin();
  right_strip.begin();
  initialized_ = true;
  set_color({0, 0, 0});
}

void Controller::set_color(RgbColor color) {
  if (!initialized_) {
    return;
  }
  color_ = color;
  const uint32_t packed_color = left_strip.Color(color.red, color.green, color.blue);
  fill_strip(left_strip, packed_color);
  fill_strip(right_strip, packed_color);
  left_strip.show();
  right_strip.show();
}

RgbColor Controller::color() const { return color_; }

bool Controller::is_initialized() const { return initialized_; }

}  // namespace armor::led
