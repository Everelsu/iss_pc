# SPDX-License-Identifier: GPL-2.0-or-later
"""The classic (Tk) launcher must build its window. A real display is needed; the test is skipped
without one (CI has none), so run it by hand after touching launcher/infamous_launcher.py.
Regression: show_game_language() ran during build() and used the play button, which is created later."""
import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'launcher'))
import paths  # noqa: E402,F401  (a throwaway launcher settings folder)


class TkLauncherBuilds(unittest.TestCase):
    def test_window_builds_and_updates(self):
        try:
            import tkinter as tk
            root = tk.Tk()
        except Exception as error:   # no tkinter or no display
            self.skipTest(f'no Tk display: {error}')
        try:
            import infamous_launcher
            app = infamous_launcher.Launcher(root)
            root.update_idletasks()
            root.update()
            self.assertTrue(hasattr(app, 'play_button'))
            app.show_game_language()          # also safe when called again after the build
            self.check_native_boxes(app)
        finally:
            root.destroy()

    def check_native_boxes(self, app):
        """The output / internal resolution boxes write the two keys the game reads."""
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            app.native_ini = Path(folder) / 'tlg.ini'
            app.native_output_box.current(2)                  # 1440p
            app.native_output_picked()
            app.native_internal_box.current(1)                # 720p
            app.native_internal_picked()
            self.assertEqual(app.native_state(), (2, 50))
            app.native_output_box.current(1)                  # 1080p: 720p stays 720p (67%, about)
            app.native_output_picked()
            self.assertEqual(app.native_state(), (1, 67))
            app.native_output_box.current(5)                  # 5120x1440 cannot do 2160p; 720p is fine
            app.native_output_picked()
            self.assertEqual(app.native_state(), (5, 50))
            app.native_internal_box.current(5)                # 2160p is over the budget: refused, unchanged
            app.native_internal_picked()
            self.assertEqual(app.native_state(), (5, 50))
            app.native_output_box.current(0)
            app.native_output_picked()
            self.assertEqual(app.native_state()[0], 0)


if __name__ == '__main__':
    unittest.main()
