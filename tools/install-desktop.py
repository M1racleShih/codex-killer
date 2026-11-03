#!/usr/bin/env python3
"""Install Codex Killer for the current user; optionally create a desktop shortcut.

No sudo, autostart, service, shell launcher, or program launch is performed.
"""
import argparse
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

APP_ID = 'io.github.M1racleShih.CodexKiller'
MARKER = 'X-Codex-Killer-Managed=true'


def value(text):
    """Escape a Desktop Entry string value without allowing injected key lines."""
    if any(c in text for c in '\n\r\t\0'):
        raise ValueError('Desktop entry paths cannot contain control characters')
    return text.replace('\\', '\\\\')


def executable(path):
    """Quote one Exec argument, applying Exec quoting and then string escaping."""
    text = str(path)
    if '=' in text:
        raise ValueError('Desktop Entry executable paths cannot contain =')
    text = text.replace('%', '%%')
    text = ''.join('\\' + c if c in '\\"`$' else c for c in text)
    return '"' + value(text) + '"'


def launcher(template, binary, icon):
    updates = {'Exec': executable(binary), 'TryExec': value(str(binary)), 'Icon': value(str(icon))}
    result = []
    for line in template.splitlines():
        key = line.partition('=')[0]
        if key in updates:
            result.append(key + '=' + updates.pop(key))
        else:
            result.append(line)
    result.extend(key + '=' + val for key, val in updates.items())
    return '\n'.join(result) + '\n'


def check_managed(path):
    if path.is_symlink():
        raise FileExistsError(f'Refusing to replace a symbolic-link launcher: {path}')
    if path.exists() and MARKER not in path.read_text().splitlines():
        raise FileExistsError(f'Refusing to replace an unrecognized launcher: {path}')


def atomic_write(path, text, mode):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=path.parent,
                                         prefix='.' + path.name + '.', delete=False) as f:
            temp = Path(f.name)
            f.write(text)
        temp.chmod(mode)
        temp.replace(path)
    finally:
        if temp and temp.exists():
            temp.unlink()


def desktop_directory():
    command = shutil.which('xdg-user-dir')
    path = Path(subprocess.check_output([command, 'DESKTOP'], text=True).strip()) if command else Path.home() / 'Desktop'
    if not path.is_absolute() or path == Path.home():
        raise ValueError('No dedicated desktop directory is configured; use --desktop-dir explicitly')
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build'))
    parser.add_argument('--prefix', type=Path, default=Path.home() / '.local')
    parser.add_argument('--desktop', action='store_true', help='Also create an executable shortcut on the desktop')
    parser.add_argument('--desktop-dir', type=Path, help='Override the detected desktop directory; requires --desktop')
    args = parser.parse_args()
    if args.desktop_dir and not args.desktop:
        parser.error('--desktop-dir requires --desktop')
    try:
        build = args.build_dir.expanduser().resolve()
        prefix = args.prefix.expanduser().resolve()
        if not os.access(build / 'codex-killer', os.X_OK):
            raise ValueError('Build codex-killer before installing it')
        app_file = prefix / 'share/applications' / (APP_ID + '.desktop')
        binary = prefix / 'bin/codex-killer'
        icon = prefix / 'share/icons/hicolor/scalable/apps/codex-killer.svg'
        shortcut = None
        if args.desktop:
            directory = args.desktop_dir.expanduser().resolve() if args.desktop_dir else desktop_directory()
            shortcut = directory / 'Codex Killer.desktop'
            check_managed(shortcut)
        check_managed(app_file)
        if (binary.exists() or binary.is_symlink()) and not app_file.exists():
            raise FileExistsError(f'Refusing to replace a binary without a recognized application entry: {binary}')
        # Validate quoting before installing any files.
        executable(binary)
        value(str(icon))
        subprocess.run(['cmake', '--install', str(build), '--prefix', str(prefix)], check=True)
        text = launcher(app_file.read_text(), binary, icon)
        atomic_write(app_file, text, 0o644)
        print(f'Application entry: {app_file}')
        if shortcut:
            atomic_write(shortcut, text, 0o755)
            print(f'Desktop shortcut: {shortcut}')
            gio = '/usr/bin/gio' if Path('/usr/bin/gio').exists() else shutil.which('gio')
            if gio:
                try:
                    result = subprocess.run([gio, 'set', str(shortcut), 'metadata::trusted', 'true'],
                                            capture_output=True, text=True, timeout=5)
                    if result.returncode:
                        print('Note: this file manager does not support GIO trust metadata; the shortcut is executable.')
                except subprocess.TimeoutExpired:
                    print('Note: GIO trust metadata timed out; the shortcut is executable.')
        refresh = shutil.which('update-desktop-database')
        if refresh:
            subprocess.run([refresh, str(app_file.parent)], check=False)
        print(f'Installed executable: {binary}')
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Installation failed: {error}\n')


if __name__ == '__main__':
    main()
