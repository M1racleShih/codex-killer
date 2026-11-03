import importlib.util
from pathlib import Path
import tempfile
import unittest

MODULE_PATH = Path(__file__).resolve().parents[1] / 'tools/install-desktop.py'
spec = importlib.util.spec_from_file_location('desktop_installer', MODULE_PATH)
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class DesktopTests(unittest.TestCase):
    def test_absolute_paths_do_not_depend_on_shell_path(self):
        template = '[Desktop Entry]\nType=Application\nExec=codex-killer\nIcon=codex-killer\nTerminal=false\n' + installer.MARKER + '\n'
        result = installer.launcher(template, Path('/example user/bin/codex-killer'), Path('/example user/icon.svg'))
        self.assertIn('Exec="/example user/bin/codex-killer"\n', result)
        self.assertIn('TryExec=/example user/bin/codex-killer\n', result)
        self.assertIn('Icon=/example user/icon.svg\n', result)
        self.assertIn('Terminal=false\n', result)
        self.assertNotIn('sh -c', result)
        self.assertEqual(result.count('Exec='), 2)  # Exec plus TryExec, no duplicate key.

    def test_reserved_characters_are_escaped(self):
        self.assertEqual(installer.executable('/test/100%/app'), '"/test/100%%/app"')
        self.assertEqual(installer.executable('/test/$HOME/app'), '"/test/\\\\$HOME/app"')
        self.assertEqual(installer.executable('/test/"app"'), '"/test/\\\\"app\\\\""')
        self.assertEqual(installer.executable('/test/\\app'), '"/test/\\\\\\\\app"')

    def test_rejects_path_injection(self):
        for path in ['/test\nExec=evil', '/test\rapp', '/test\tapp', '/test\0app', '/test/app=other']:
            with self.assertRaises(ValueError):
                installer.executable(path)

    def test_refuses_unrecognized_or_symlink_shortcut(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'Codex Killer.desktop'
            path.write_text('[Desktop Entry]\nName=Unrelated application\n')
            with self.assertRaises(FileExistsError):
                installer.check_managed(path)
            path.write_text(installer.MARKER + '\n')
            installer.check_managed(path)
            link = Path(directory) / 'link.desktop'
            link.symlink_to(path)
            with self.assertRaises(FileExistsError):
                installer.check_managed(link)

    def test_atomic_shortcut_has_executable_permission(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'desktop' / 'Codex Killer.desktop'
            installer.atomic_write(path, installer.MARKER + '\n', 0o755)
            self.assertEqual(path.stat().st_mode & 0o777, 0o755)
            self.assertEqual(path.read_text(), installer.MARKER + '\n')
            self.assertEqual(list(path.parent.iterdir()), [path])


if __name__ == '__main__':
    unittest.main()
