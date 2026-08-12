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

class Controller {
 public:
  /** Initializes both eight-pixel WS2812 strips and turns them off. */
  void begin();

  /** Applies one RGB color to every LED on both strips. */
  bool set_color(RgbColor color);

  /** Returns the single color shared by both strips. */
  RgbColor color() const;

  /** Reports whether the WS2812 output devices were initialized. */
  bool is_initialized() const;

  /** Reports whether the selected color can be stored in NVS. */
  bool is_persistence_healthy() const;

 private:
  bool load_color();
  bool save_color(RgbColor color) const;

  bool initialized_ = false;
  bool persistence_healthy_ = false;
  RgbColor color_{128, 0, 255};
};

}  // namespace armor::led
