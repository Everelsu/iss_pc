# SPDX-License-Identifier: GPL-2.0-or-later
"""launcher/dlss_setup.py: folders, DLL checks and the download, against a local test server (no real 56 MB)."""
import http.server
import os
import sys
import tempfile
import threading
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'launcher'))
import dlss_setup  # noqa: E402

REAL_DLL = Path(r'D:\backup-linux\backup\backup\decomp\dlss_dll\nvngx_dlss.dll')
SMALL = dict(min_size=1000, max_size=10 * 1024 * 1024)


def fake_dll(size=4000):
    return b'MZ' + b'\0' * (size - 2)


class Server:
    def __init__(self, body, status=200, lie_length=None, slow=False):
        outer = self
        self.hits = 0

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                outer.hits += 1
                self.send_response(status)
                self.send_header('Content-Length', str(lie_length if lie_length is not None else len(body)))
                self.end_headers()
                if slow:
                    for i in range(0, len(body), 1000):
                        try:
                            self.wfile.write(body[i:i + 1000])
                            self.wfile.flush()
                        except OSError:
                            return
                        threading.Event().wait(0.02)
                else:
                    self.wfile.write(body)

            def log_message(self, *args):
                pass
        self.httpd = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.url = f'http://127.0.0.1:{self.httpd.server_address[1]}/nvngx_dlss.dll'
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def close(self):
        self.httpd.shutdown()
        self.httpd.server_close()


def good_signature(path):
    return True, 'CN=NVIDIA Corporation'


class Folders(unittest.TestCase):
    def test_order(self):
        folders = dlss_setup.search_folders('u', 'c', 'e')
        self.assertEqual([str(f) for f in folders], ['c', os.path.join('u', 'dlss'), 'e'])

    def test_find(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / 'a').mkdir()
            (Path(d) / 'a' / 'x.dll').write_bytes(b'MZ')
            self.assertEqual(dlss_setup.find('x.dll', [Path(d) / 'b', Path(d) / 'a']), Path(d) / 'a' / 'x.dll')
            self.assertIsNone(dlss_setup.find('y.dll', [Path(d) / 'a']))


class Inspect(unittest.TestCase):
    def test_missing_and_invalid(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(dlss_setup.inspect_nvidia_dll(None).state, 'missing')
            p = Path(d) / 'nvngx_dlss.dll'
            p.write_bytes(b'MZ' + b'\0' * 100)
            self.assertEqual(dlss_setup.inspect_nvidia_dll(p).detail, 'size')
            p.write_bytes(b'\0' * dlss_setup.MIN_SIZE)
            self.assertEqual(dlss_setup.inspect_nvidia_dll(p).detail, 'format')
            p.write_bytes(b'MZ' + b'\0' * dlss_setup.MIN_SIZE)
            self.assertEqual(dlss_setup.inspect_nvidia_dll(p, signature_check=lambda x: (False, '')).detail, 'signature')
            self.assertTrue(dlss_setup.inspect_nvidia_dll(p, signature_check=good_signature).ok)

    def test_bridge(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / 'tlg_dlss.dll'
            self.assertEqual(dlss_setup.inspect_bridge(p).state, 'missing')
            p.write_bytes(b'MZ' + b'\0' * 500)
            self.assertEqual(dlss_setup.inspect_bridge(p).state, 'invalid')

    @unittest.skipUnless(os.name == 'nt', 'Windows')
    def test_pe_exports_of_a_system_dll(self):
        names = dlss_setup.pe_exports(Path(os.environ['SystemRoot']) / 'System32' / 'kernel32.dll')
        self.assertIn('CreateFileW', names)

    @unittest.skipUnless(os.name == 'nt', 'Windows')
    def test_unsigned_file_is_refused(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / 'x.dll'
            p.write_bytes(fake_dll())
            self.assertFalse(dlss_setup.signature(p)[0])

    @unittest.skipUnless(os.name == 'nt' and REAL_DLL.is_file(), 'needs the NVIDIA DLL downloaded earlier')
    def test_real_dll(self):
        valid, subject = dlss_setup.signature(REAL_DLL)
        self.assertTrue(valid, subject)
        self.assertTrue(dlss_setup.file_version(REAL_DLL).startswith('310.'))
        self.assertTrue(dlss_setup.inspect_nvidia_dll(REAL_DLL).ok)


class Download(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name) / 'dlss'
        self.servers = []

    def tearDown(self):
        for s in self.servers:
            s.close()
        self.tmp.cleanup()

    def serve(self, *a, **k):
        s = Server(*a, **k)
        self.servers.append(s)
        return s

    def run_download(self, server, **k):
        k.setdefault('signature_check', good_signature)
        return dlss_setup.download(self.dir, server.url, **{**SMALL, **k})

    def left(self):
        return sorted(p.name for p in self.dir.iterdir()) if self.dir.exists() else []

    def test_success_with_progress(self):
        body = fake_dll(5000)
        seen = []
        result = self.run_download(self.serve(body), progress=lambda d, t: seen.append((d, t)))
        self.assertTrue(result.ok)
        self.assertEqual((self.dir / dlss_setup.DLL_NAME).read_bytes(), body)
        self.assertEqual(seen[-1], (5000, 5000))
        self.assertEqual(self.left(), [dlss_setup.DLL_NAME])

    def test_replaces_the_old_file_only_when_valid(self):
        self.dir.mkdir()
        (self.dir / dlss_setup.DLL_NAME).write_bytes(b'old')
        with self.assertRaises(dlss_setup.DlssError) as e:
            self.run_download(self.serve(fake_dll()), signature_check=lambda p: (False, 'CN=Someone'))
        self.assertEqual(e.exception.key, 'signature')
        self.assertEqual((self.dir / dlss_setup.DLL_NAME).read_bytes(), b'old')
        self.assertEqual(self.left(), [dlss_setup.DLL_NAME])

    def test_not_a_dll(self):
        with self.assertRaises(dlss_setup.DlssError) as e:
            self.run_download(self.serve(b'<html>' + b'x' * 4000))
        self.assertEqual(e.exception.key, 'format')
        self.assertEqual(self.left(), [])

    def test_size_limits(self):
        with self.assertRaises(dlss_setup.DlssError) as e:
            self.run_download(self.serve(fake_dll(200)))
        self.assertEqual(e.exception.key, 'size')
        with self.assertRaises(dlss_setup.DlssError) as e:
            self.run_download(self.serve(fake_dll(5000)), max_size=3000)
        self.assertEqual(e.exception.key, 'size')
        self.assertEqual(self.left(), [])

    def test_http_error(self):
        with self.assertRaises(dlss_setup.DlssError) as e:
            self.run_download(self.serve(b'', status=404))
        self.assertEqual(e.exception.key, 'network')
        self.assertEqual(self.left(), [])

    def test_truncated(self):
        with self.assertRaises(dlss_setup.DlssError) as e:
            self.run_download(self.serve(fake_dll(5000), lie_length=6000))
        self.assertEqual(e.exception.key, 'network')
        self.assertEqual(self.left(), [])

    def test_connection_refused(self):
        s = self.serve(b'')
        url = s.url
        s.close()
        self.servers.remove(s)
        with self.assertRaises(dlss_setup.DlssError) as e:
            dlss_setup.download(self.dir, url, signature_check=good_signature, **SMALL)
        self.assertEqual(e.exception.key, 'network')
        self.assertEqual(self.left(), [])

    def test_cancel(self):
        cancel = threading.Event()
        server = self.serve(fake_dll(200000), slow=True)

        def on_progress(done, total):
            if done >= 5000:
                cancel.set()
        with self.assertRaises(dlss_setup.DlssError) as e:
            self.run_download(server, progress=on_progress, cancel=cancel)
        self.assertEqual(e.exception.key, 'cancelled')
        self.assertEqual(self.left(), [])

    def test_only_https_or_loopback(self):
        for url in ('http://example.com/a.dll', 'ftp://example.com/a.dll', 'file:///c:/a.dll'):
            with self.assertRaises(dlss_setup.DlssError) as e:
                dlss_setup.download(self.dir, url)
            self.assertEqual(e.exception.key, 'url')
        self.assertTrue(dlss_setup.SOURCE_URL.startswith('https://github.com/NVIDIA/DLSS/'))


class Texts(unittest.TestCase):
    def test_both_languages_and_placeholders(self):
        import string
        for key, (en, pt) in dlss_setup.TEXT.items():
            names = lambda s: {f for _, f, _, _ in string.Formatter().parse(s) if f}
            self.assertEqual(names(en), names(pt), key)

    def test_every_error_key_has_text(self):
        for key in ('url', 'network', 'cancelled', 'size', 'format', 'signature', 'write'):
            self.assertIn('dlss_err_' + key, dlss_setup.TEXT)


if __name__ == '__main__':
    unittest.main()
