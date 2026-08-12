/**
 * @file led_controller.cpp
 * @brief WS2812 driver for left GPIO0 and right GPIO21 light strips.
 */

#include "led_controller.h"

#include <Adafruit_NeoPixel.h>
#include <math.h>
#include <Preferences.h>

namespace armor::led {
namespace {

constexpr uint8_t kPixelsPerStrip = 8;
constexpr uint8_t kLeftStripPin = 0;
constexpr uint8_t kRightStripPin = 21;
constexpr neoPixelType kPixelType = NEO_GRB + NEO_KHZ800;
constexpr char kPreferencesNamespace[] = "armor-led";
constexpr char kColorKey[] = "rgb";
constexpr char kBrightnessKey[] = "brightness";
constexpr uint32_t kColorMask = 0x00FFFFFF;
constexpr RgbColor kDefaultColor{128, 0, 255};
constexpr uint8_t kDefaultBrightnessPercent = 100;
constexpr float kSrgbGamma = 2.2F;

Adafruit_NeoPixel left_strip(kPixelsPerStrip, kLeftStripPin, kPixelType);
Adafruit_NeoPixel right_strip(kPixelsPerStrip, kRightStripPin, kPixelType);

void fill_strip(Adafruit_NeoPixel& strip, uint32_t packed_color) {
  for (uint8_t pixel = 0; pixel < kPixelsPerStrip; ++pixel) {
    strip.setPixelColor(pixel, packed_color);
  }
}

uint8_t srgb_to_led_pwm(uint8_t component) {
  const float normalized = static_cast<float>(component) / 255.0F;
  return static_cast<uint8_t>(roundf(powf(normalized, kSrgbGamma) * 255.0F));
}

uint8_t scaled_component(uint8_t component, uint8_t maximum_component,
                         uint8_t brightness_percent) {
  if (maximum_component == 0 || brightness_percent == 0) {
    return 0;
  }
  const float ratio = static_cast<float>(component) / maximum_component;
  return static_cast<uint8_t>(roundf(ratio * brightness_percent * 2.55F));
}

uint32_t corrected_color(const RgbColor& color, uint8_t brightness_percent) {
  const uint8_t maximum_component = max(color.red, max(color.green, color.blue));
  return left_strip.Color(
      srgb_to_led_pwm(scaled_component(color.red, maximum_component, brightness_percent)),
      srgb_to_led_pwm(scaled_component(color.green, maximum_component, brightness_percent)),
      srgb_to_led_pwm(scaled_component(color.blue, maximum_component, brightness_percent)));
}

}  // namespace

void Controller::begin() {
  left_strip.begin();
  right_strip.begin();
  initialized_ = true;
  persistence_healthy_ = load_color();
  const uint32_t packed_color = corrected_color(color_, brightness_percent_);
  fill_strip(left_strip, packed_color);
  fill_strip(right_strip, packed_color);
  left_strip.show();
  right_strip.show();
}

bool Controller::set_color(RgbColor color, uint8_t brightness_percent) {
  if (!initialized_ || !persistence_healthy_ || brightness_percent > 100 ||
      !save_color(color, brightness_percent)) {
    persistence_healthy_ = false;
    return false;
  }
  color_ = color;
  brightness_percent_ = brightness_percent;
  const uint32_t packed_color = corrected_color(color, brightness_percent);
  fill_strip(left_strip, packed_color);
  fill_strip(right_strip, packed_color);
  left_strip.show();
  right_strip.show();
  return true;
}

RgbColor Controller::color() const { return color_; }

uint8_t Controller::brightness_percent() const { return brightness_percent_; }

bool Controller::is_initialized() const { return initialized_; }

bool Controller::is_persistence_healthy() const { return persistence_healthy_; }

bool Controller::load_color() {
  Preferences preferences;
  // Open read-write on first boot so the namespace can be created before a
  // later SET_LED_COLOR command persists the user's selection.
  if (!preferences.begin(kPreferencesNamespace, false)) {
    color_ = kDefaultColor;
    brightness_percent_ = kDefaultBrightnessPercent;
    return false;
  }
  const uint32_t packed_color = preferences.getUInt(kColorKey, kColorMask + 1);
  const uint8_t stored_brightness =
      preferences.getUChar(kBrightnessKey, kDefaultBrightnessPercent);
  preferences.end();
  if (packed_color > kColorMask) {
    color_ = kDefaultColor;
    brightness_percent_ = kDefaultBrightnessPercent;
    return true;
  }
  color_ = {
      static_cast<uint8_t>(packed_color >> 16),
      static_cast<uint8_t>(packed_color >> 8),
      static_cast<uint8_t>(packed_color),
  };
  brightness_percent_ = stored_brightness;
  if (brightness_percent_ > 100) {
    color_ = kDefaultColor;
    brightness_percent_ = kDefaultBrightnessPercent;
  }
  return true;
}

bool Controller::save_color(RgbColor color, uint8_t brightness_percent) const {
  Preferences preferences;
  if (!preferences.begin(kPreferencesNamespace, false)) {
    return false;
  }
  const uint32_t packed_color = (static_cast<uint32_t>(color.red) << 16) |
                                (static_cast<uint32_t>(color.green) << 8) |
                                color.blue;
  const bool saved_color =
      preferences.putUInt(kColorKey, packed_color) == sizeof(packed_color);
  const bool saved_brightness =
      preferences.putUChar(kBrightnessKey, brightness_percent) == sizeof(brightness_percent);
  preferences.end();
  return saved_color && saved_brightness;
}

}  // namespace armor::led
