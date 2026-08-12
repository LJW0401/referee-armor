/**
 * @file led_controller.cpp
 * @brief WS2812 driver for left GPIO0 and right GPIO21 light strips.
 */

#include "led_controller.h"

#include <Adafruit_NeoPixel.h>
#include <Preferences.h>

namespace armor::led {
namespace {

constexpr uint8_t kPixelsPerStrip = 8;
constexpr uint8_t kLeftStripPin = 0;
constexpr uint8_t kRightStripPin = 21;
constexpr neoPixelType kPixelType = NEO_GRB + NEO_KHZ800;
constexpr char kPreferencesNamespace[] = "armor-led";
constexpr char kColorKey[] = "rgb";
constexpr uint32_t kColorMask = 0x00FFFFFF;
constexpr RgbColor kDefaultColor{128, 0, 255};

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
  persistence_healthy_ = load_color();
  const uint32_t packed_color =
      left_strip.Color(color_.red, color_.green, color_.blue);
  fill_strip(left_strip, packed_color);
  fill_strip(right_strip, packed_color);
  left_strip.show();
  right_strip.show();
}

bool Controller::set_color(RgbColor color) {
  if (!initialized_ || !persistence_healthy_ || !save_color(color)) {
    persistence_healthy_ = false;
    return false;
  }
  color_ = color;
  const uint32_t packed_color = left_strip.Color(color.red, color.green, color.blue);
  fill_strip(left_strip, packed_color);
  fill_strip(right_strip, packed_color);
  left_strip.show();
  right_strip.show();
  return true;
}

RgbColor Controller::color() const { return color_; }

bool Controller::is_initialized() const { return initialized_; }

bool Controller::is_persistence_healthy() const { return persistence_healthy_; }

bool Controller::load_color() {
  Preferences preferences;
  // Open read-write on first boot so the namespace can be created before a
  // later SET_LED_COLOR command persists the user's selection.
  if (!preferences.begin(kPreferencesNamespace, false)) {
    color_ = kDefaultColor;
    return false;
  }
  const uint32_t packed_color = preferences.getUInt(kColorKey, kColorMask + 1);
  preferences.end();
  if (packed_color > kColorMask) {
    color_ = kDefaultColor;
    return true;
  }
  color_ = {
      static_cast<uint8_t>(packed_color >> 16),
      static_cast<uint8_t>(packed_color >> 8),
      static_cast<uint8_t>(packed_color),
  };
  return true;
}

bool Controller::save_color(RgbColor color) const {
  Preferences preferences;
  if (!preferences.begin(kPreferencesNamespace, false)) {
    return false;
  }
  const uint32_t packed_color = (static_cast<uint32_t>(color.red) << 16) |
                                (static_cast<uint32_t>(color.green) << 8) |
                                color.blue;
  const bool saved = preferences.putUInt(kColorKey, packed_color) == sizeof(packed_color);
  preferences.end();
  return saved;
}

}  // namespace armor::led
