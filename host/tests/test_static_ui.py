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
        self.assertEqual(document.count('type="range"'), 3)
        self.assertNotIn('type="color"', document)

    def test_script_sends_slider_components_to_led_api(self) -> None:
        script = (STATIC_DIRECTORY / "app.js").read_text(encoding="utf-8")
        self.assertIn("selectedColor", script)
        self.assertIn("{ red, green, blue }", script)


if __name__ == "__main__":
    unittest.main()
