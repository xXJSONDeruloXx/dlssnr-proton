import importlib.machinery
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
loader = importlib.machinery.SourceFileLoader('launcher', str(ROOT / 'nr-run'))
spec = importlib.util.spec_from_loader(loader.name, loader)
nr = importlib.util.module_from_spec(spec)
loader.exec_module(nr)


class LauncherTests(unittest.TestCase):
    def test_steam_command_with_spaces_and_options(self):
        with tempfile.TemporaryDirectory() as root:
            game = Path(root) / 'Game With Spaces'
            game.mkdir()
            exe = game / 'Game.exe'
            exe.touch()
            self.assertEqual(nr.game_directory(['wrapper', '--', str(exe), '-windowed']), game.resolve())

    def test_unknown_command_requires_explicit_directory(self):
        with self.assertRaisesRegex(RuntimeError, 'Cannot find'):
            nr.game_directory(['echo', 'game'])

    def test_conflict_preserved_before_any_staging(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)
            source = path / 'source'
            source.write_bytes(b'bridge')
            existing = path / 'version.dll'
            existing.write_bytes(b'existing mod')
            with self.assertRaises(RuntimeError):
                with nr.Staging(path, {'first.dll': source, 'version.dll': source}):
                    pass
            self.assertEqual(existing.read_bytes(), b'existing mod')
            self.assertFalse((path / 'first.dll').exists())

    def test_cleanup_only_owns_its_own_links(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)
            source = path / 'source'
            source.write_bytes(b'bridge')
            with nr.Staging(path, {'one.dll': source, 'two.dll': source}):
                (path / 'one.dll').unlink()
                (path / 'one.dll').write_bytes(b'user replacement')
            self.assertEqual((path / 'one.dll').read_bytes(), b'user replacement')
            self.assertFalse(os.path.lexists(path / 'two.dll'))

    def test_bad_payload_does_not_replace_existing_cache(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / 'payload'
            path.write_bytes(b'old')
            with self.assertRaises(RuntimeError):
                nr.atomic_bytes(path, b'corrupt', '0' * 64)
            self.assertEqual(path.read_bytes(), b'old')

    @unittest.skipUnless(os.environ.get('DLSSNR_TEST_MODEL'), 'optional user-supplied model')
    def test_conversion_matches_independent_runtime_digest(self):
        with tempfile.TemporaryDirectory() as root:
            packed = nr.prepare_weights(Path(os.environ['DLSSNR_TEST_MODEL']), Path(root))
            self.assertEqual(nr.digest(packed), nr.WEIGHTS_SHA)


if __name__ == '__main__':
    unittest.main()
