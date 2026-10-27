# SPDX-License-Identifier: GPL-2.0-or-later
"""launcher/native_resolution.py against src/iss_native_resolution.h: same tables, same answers."""
import re
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'launcher'))
import native_resolution as n  # noqa: E402

HEADER = (ROOT / 'src' / 'iss_native_resolution.h').read_text(encoding='utf-8')
CASES = (Path(__file__).resolve().parent / 'native_resolution_cases.txt').read_text(encoding='utf-8')


def header_list(name):
    text = re.search(name + r'\[[^\]]*\]\s*=\s*\{([^}]*)\}', HEADER).group(1)
    return tuple(int(x) for x in text.split(','))


def t(key, **fmt):
    return n.TEXT[key][0].format(**fmt)


class SharedTables(unittest.TestCase):
    def test_tables_match_the_header(self):
        widths, heights = header_list('iss_native_widths'), header_list('iss_native_heights')
        self.assertEqual(n.OUTPUTS, tuple(zip(widths, heights)))
        self.assertEqual(n.STEPS, header_list('iss_native_step_heights'))
        self.assertEqual(len(n.OUTPUT_LABELS), len(n.OUTPUTS))
        self.assertEqual(n.PIXEL_BUDGET, 3840 * 2160)
        self.assertIn('3840u*2160u', HEADER)

    def test_shared_cases(self):
        count = 0
        for line in CASES.splitlines():
            if not line or line.startswith('#'):
                continue
            kind, rest = line.split(' ', 1)
            args, want = rest.split(' -> ')
            args = [int(x) for x in args.split()]
            if kind == 'plan':
                got = n.plan(*args)
                self.assertEqual('refused' if got is None else f'{got[0]} {got[1]}', want, line)
            elif kind == 'pick':
                self.assertEqual(' '.join(str(v) for v in n.pick(*args)), want, line)
            elif kind == 'step':
                self.assertEqual(str(n.step_for_percent(*args)), want, line)
            elif kind == 'retarget':
                self.assertEqual(str(n.retarget(*args)), want, line)
            else:
                self.fail(line)
            count += 1
        self.assertGreaterEqual(count, 60)


class Behaviour(unittest.TestCase):
    def test_720p_in_1440p_is_exact(self):
        status, percent, w, h, error = n.pick(2, 720)
        self.assertEqual((status, percent, w, h, error), (n.PICK_OK, 50, 1280, 720, 0))

    def test_720p_in_1080p_is_close_not_exact(self):
        status, percent, w, h, error = n.pick(1, 720)
        self.assertEqual((status, percent, h, error), (n.PICK_OK, 67, 724, 4))
        text, tone = n.summary(t, 1, 67)
        self.assertEqual(tone, 'warn')
        self.assertIn('Not exact', text)
        self.assertIn('1440p · 2560×1440 50%', text)       # where 720p is exact

    def test_budget_is_refused(self):
        self.assertEqual(n.pick(4, 2160)[0], n.PICK_OK)           # 4K at 100% is exactly the budget
        self.assertEqual(n.pick(5, 2160)[0], n.PICK_BUDGET)
        text, tone = n.summary(t, 4, 150)
        self.assertEqual(tone, 'bad')
        self.assertIn('4K pixel limit', text)
        options = {v: (label, ok) for v, label, ok in n.internal_options(t, 5, 100)}
        self.assertFalse(options['2160'][1])
        self.assertIn('over the 4K limit', options['2160'][0])
        self.assertTrue(options['1440'][1])

    def test_off_and_custom(self):
        self.assertEqual(n.summary(t, 0, 100)[1], 'info')
        self.assertEqual(n.internal_value(2, 50), '720')
        self.assertEqual(n.internal_value(2, 61), 'custom')
        self.assertEqual(n.internal_options(t, 2, 61)[-1][0], 'custom')
        self.assertEqual(n.internal_options(t, 2, 50)[-1][0], '2160')

    def test_changing_output_keeps_the_internal_height(self):
        self.assertEqual(n.retarget(2, 50, 4), 33)       # 720p on 4K: 33% (about 712p)
        self.assertEqual(n.retarget(2, 50, 1), 67)

    def test_ini_round_trip_keeps_other_keys(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'tlg.ini'
            path.write_text('overlay_corner=3\niss_native_percent=80\n', encoding='utf-8')
            n.save(path, 2, 50)
            text = path.read_text(encoding='utf-8')
            self.assertIn('overlay_corner=3', text)
            self.assertEqual(text.count('iss_native_percent'), 1)
            values = dict(line.split('=') for line in text.split())
            self.assertEqual(n.read(values), (2, 50))
        self.assertEqual(n.read({}), (0, 100))
        self.assertEqual(n.read({'iss_native_output': '9', 'iss_native_percent': 'x'}), (0, 100))

    def test_texts_have_both_languages_and_equal_placeholders(self):
        for key, (en, pt) in n.TEXT.items():
            self.assertEqual(sorted(re.findall(r'\{(\w+)', en)), sorted(re.findall(r'\{(\w+)', pt)), key)
            self.assertTrue(en and pt, key)


if __name__ == '__main__':
    unittest.main()
