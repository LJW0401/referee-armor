/**
 * @file led_controller.cpp
 * @brief WS2812 driver for left GPIO0 and right GPIO21 light strips.
 */

#include "led_controller.h"

#include <Adafruit_NeoPixel.h>
#include <esp_system.h>
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
constexpr RgbColor kDefaultColor{128, 0, 128};
constexpr uint8_t kDefaultBrightnessPercent = 100;
constexpr float kSrgbGamma = 2.2F;
constexpr uint8_t kTotalPixels = kPixelsPerStrip * 2;
constexpr uint16_t kBreathingPeriodMinimumMs = 1800;
constexpr uint16_t kBreathingPeriodMaximumMs = 4000;
constexpr uint8_t kTransitionStartPercent = 55;
constexpr uint16_t kFrameIntervalMs = 25;

Adafruit_NeoPixel left_strip(kPixelsPerStrip, kLeftStripPin, kPixelType);
Adafruit_NeoPixel right_strip(kPixelsPerStrip, kRightStripPin, kPixelType);

struct BreathingPixel {
  RgbColor current;
  RgbColor next;
  uint32_t cycle_started_ms;
  uint16_t period_ms;
};

BreathingPixel breathing_pixels[kTotalPixels]{};
uint32_t last_breathing_frame_ms = 0;

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

RgbColor random_color() {
  return {static_cast<uint8_t>(random(32, 256)),
          static_cast<uint8_t>(random(32, 256)),
          static_cast<uint8_t>(random(32, 256))};
}

uint16_t random_period_ms() {
  return static_cast<uint16_t>(
      random(kBreathingPeriodMinimumMs, kBreathingPeriodMaximumMs + 1));
}

RgbColor interpolate(RgbColor first, RgbColor second, float amount) {
  return {
      static_cast<uint8_t>(roundf(first.red + (second.red - first.red) * amount)),
      static_cast<uint8_t>(roundf(first.green + (second.green - first.green) * amount)),
      static_cast<uint8_t>(roundf(first.blue + (second.blue - first.blue) * amount)),
  };
}

void set_physical_pixel(uint8_t index, uint32_t packed_color) {
  if (index < kPixelsPerStrip) {
    left_strip.setPixelColor(index, packed_color);
    return;
  }
  right_strip.setPixelColor(index - kPixelsPerStrip, packed_color);
}

}  // namespace

void Controller::begin() {
  left_strip.begin();
  right_strip.begin();
  initialized_ = true;
  persistence_healthy_ = load_color();
  render_static_color();
}

bool Controller::set_color(RgbColor color, uint8_t brightness_percent) {
  if (!initialized_ || !persistence_healthy_ || brightness_percent > 100 ||
      !save_color(color, brightness_percent)) {
    persistence_healthy_ = false;
    return false;
  }
  color_ = color;
  brightness_percent_ = brightness_percent;
  effect_ = Effect::kStatic;
  render_static_color();
  return true;
}

bool Controller::set_effect(Effect effect) {
  if (!initialized_ || static_cast<uint8_t>(effect) >
                           static_cast<uint8_t>(Effect::kRandomBreathing)) {
    return false;
  }
  effect_ = effect;
  if (effect_ == Effect::kStatic) {
    render_static_color();
  } else {
    initialize_breathing(millis());
  }
  return true;
}

void Controller::tick(uint32_t now_ms) {
  if (effect_ == Effect::kRandomBreathing) {
    render_breathing(now_ms);
  }
}

void Controller::render_static_color() {
  const uint32_t packed_color = corrected_color(color_, brightness_percent_);
  fill_strip(left_strip, packed_color);
  fill_strip(right_strip, packed_color);
  left_strip.show();
  right_strip.show();
}

RgbColor Controller::color() const { return color_; }

uint8_t Controller::brightness_percent() const { return brightness_percent_; }

Effect Controller::effect() const { return effect_; }

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

void Controller::initialize_breathing(uint32_t now_ms) {
  randomSeed(esp_random());
  for (uint8_t index = 0; index < kTotalPixels; ++index) {
    breathing_pixels[index] = {
        random_color(),
        random_color(),
        now_ms - static_cast<uint32_t>(random(0, kBreathingPeriodMaximumMs)),
        random_period_ms(),
    };
  }
  last_breathing_frame_ms = 0;
}

void Controller::render_breathing(uint32_t now_ms) {
  if (now_ms - last_breathing_frame_ms < kFrameIntervalMs) {
    return;
  }
  last_breathing_frame_ms = now_ms;
  for (uint8_t index = 0; index < kTotalPixels; ++index) {
    BreathingPixel& pixel = breathing_pixels[index];
    uint32_t elapsed_ms = now_ms - pixel.cycle_started_ms;
    while (elapsed_ms >= pixel.period_ms) {
      elapsed_ms -= pixel.period_ms;
      pixel.cycle_started_ms += pixel.period_ms;
      pixel.current = pixel.next;
      pixel.next = random_color();
      pixel.period_ms = random_period_ms();
    }
    const float phase = static_cast<float>(elapsed_ms) / pixel.period_ms;
    const float brightness = sinf(phase * PI);
    const float transition = phase <= kTransitionStartPercent / 100.0F
                                 ? 0.0F
                                 : (phase - kTransitionStartPercent / 100.0F) /
                                       (1.0F - kTransitionStartPercent / 100.0F);
    const RgbColor color = interpolate(pixel.current, pixel.next, transition);
    const uint8_t brightness_percent = static_cast<uint8_t>(
        roundf(brightness * brightness * brightness_percent_));
    set_physical_pixel(index, corrected_color(color, brightness_percent));
  }
  left_strip.show();
  right_strip.show();
}

}  // namespace armor::led
