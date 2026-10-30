"""Comparison controls exercised through the real native renderer."""
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import ablate

CLI = pathlib.Path(sys.argv.pop(1)).resolve()


class PaletteTest(unittest.TestCase):
    def test_palette_changes_pixels_without_moving_camera(self):
        with tempfile.TemporaryDirectory() as tmp:
            first, h0 = ablate.frame(CLI, tmp, 600, palette=0)
            ice, h1 = ablate.frame(CLI, tmp, 600, palette=1)
            ember, h3 = ablate.frame(CLI, tmp, 600, palette=3)
            again, repeated = ablate.frame(CLI, tmp, 600, palette=1)
            self.assertNotEqual(first[2], ice[2])
            self.assertNotEqual(ice[2], ember[2])
            self.assertEqual(ice, again)
            self.assertEqual(h1, repeated)
            self.assertEqual(len({h0, h1, h3}), 3)

    def test_out_of_range_palette_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            for palette in (-1, 4):
                with self.subTest(palette=palette), self.assertRaises(ValueError):
                    ablate.frame(CLI, tmp, 600, palette=palette)


if __name__ == '__main__':
    unittest.main()
