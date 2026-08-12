/**
 * @file led_controller.h
 * @brief Owns identical static-color output for the two WS2812 light strips.
 */

#pragma once

#include <Arduino.h>

namespace armor::led {

struct RgbColor {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

enum class Effect : uint8_t {
  kStatic = 0,
  kRandomBreathing = 1,
};

class Controller {
 public:
  /** Initializes both eight-pixel WS2812 strips and turns them off. */
  void begin();

  /** Applies one RGB color to every LED on both strips. */
  bool set_color(RgbColor color, uint8_t brightness_percent);

  /** Starts or stops independent, overlapping random color breathing. */
  bool set_effect(Effect effect);

  /** Advances dynamic effects without blocking serial communication. */
  void tick(uint32_t now_ms);

  /** Returns the single color shared by both strips. */
  RgbColor color() const;

  /** Returns the 0..100 brightness applied to the shared RGB ratio. */
  uint8_t brightness_percent() const;

  Effect effect() const;

  /** Reports whether the WS2812 output devices were initialized. */
  bool is_initialized() const;

  /** Reports whether the selected color can be stored in NVS. */
  bool is_persistence_healthy() const;

 private:
  bool load_color();
  bool save_color(RgbColor color, uint8_t brightness_percent) const;
  void render_static_color();
  void initialize_breathing(uint32_t now_ms);
  void render_breathing(uint32_t now_ms);

  bool initialized_ = false;
  bool persistence_healthy_ = false;
  RgbColor color_{128, 0, 128};
  uint8_t brightness_percent_ = 100;
  Effect effect_ = Effect::kStatic;
};

}  // namespace armor::led
