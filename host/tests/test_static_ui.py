"""Source-level contract tests for the browser RGB slider controls."""

from __future__ import annotations

from pathlib import Path
import unittest


STATIC_DIRECTORY = Path(__file__).parents[1] / "src" / "armor_host" / "static"


class StaticUiTests(unittest.TestCase):
    """Guard the UI contract without requiring a browser automation runtime."""

    def test_custom_color_uses_three_bounded_rgb_sliders(self) -> None:
        document = (STATIC_DIRECTORY / "index.html").read_text(encoding="utf-8")
        for component in ("red", "green", "blue"):
            self.assertIn(f'id="{component}-slider"', document)
        self.assertEqual(document.count('type="range"'), 4)
        self.assertIn('id="brightness-slider"', document)
        self.assertNotIn('type="color"', document)

    def test_script_sends_slider_components_to_led_api(self) -> None:
        script = (STATIC_DIRECTORY / "app.js").read_text(encoding="utf-8")
        self.assertIn("selectedColor", script)
        self.assertIn("{ red, green, blue, brightness_percent }", script)
        self.assertIn("brightnessPercent", script)

    def test_ui_exposes_random_breathing_control(self) -> None:
        document = (STATIC_DIRECTORY / "index.html").read_text(encoding="utf-8")
        script = (STATIC_DIRECTORY / "app.js").read_text(encoding="utf-8")
        self.assertIn('id="random-breathing"', document)
        self.assertIn('"/api/led-effect"', script)
        self.assertIn("effect: 1", script)

    def test_only_connection_snapshot_synchronizes_color_controls(self) -> None:
        script = (STATIC_DIRECTORY / "app.js").read_text(encoding="utf-8")
        self.assertIn("function render(snapshot, synchronizeColor = false)", script)
        self.assertIn("if (synchronizeColor) setSelectedColorFromRgb", script)
        self.assertIn("}), true);", script)


if __name__ == "__main__":
    unittest.main()
