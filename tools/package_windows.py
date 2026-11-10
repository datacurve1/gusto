#!/usr/bin/env python3
"""Build the Windows-only redistributable from the verified HUD release binary."""
import hashlib
import json
from pathlib import Path
import zipfile
from verify_windows_package import audit

root = Path(__file__).resolve().parents[1]
version = (root / 'VERSION').read_text().strip()
hud = root / f'releases/GUSTO-{version}-RoF2-Windows-Wine.zip'
release = root / f'releases/GUSTO-{version}-RoF2-Windows.zip'
binary = (root / 'build/GUSTO.dll').read_bytes()
imports = audit(binary)
digest = hashlib.sha256(binary).hexdigest()
with zipfile.ZipFile(hud) as package:
    manifest = json.loads(package.read('BUILD.json'))
    assert manifest['binary_sha256'] == digest
    assert package.read('GUSTO.asi') == binary
manifest['distribution'] = 'Windows x86 ASI plugin; external compatible 32-bit ASI loader required'
manifest['windows_imports'] = imports
manifest['windows_installer_sha256'] = hashlib.sha256((root / 'windows/Install-Gusto.ps1').read_bytes()).hexdigest()
with zipfile.ZipFile(release, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as package:
    package.writestr('GUSTO.asi', binary)
    package.writestr('GUSTO.asi.sha256', digest + '  GUSTO.asi\n')
    package.write(root / 'windows/Install-Gusto.ps1', 'Install-Gusto.ps1')
    package.write(root / 'docs/windows.md', 'WINDOWS-INSTALL.md')
    for name in ('VERSION', 'CHANGELOG.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md'):
        package.write(root / name, name)
    guide = (root / 'docs/gamepad.md').read_text().split('## Linux / Wine controllers')[0]
    package.writestr('CONTROLLER-KEYBINDS.md', guide)
    package.writestr('BUILD.json', json.dumps(manifest, indent=2) + '\n')
    package.writestr('DEPENDENCIES.txt', 'Windows system imports only:\n' + '\n'.join(imports) + '\nXInput and WinMM load dynamically; a compatible 32-bit ASI loader is required.\n')
    package.write(root / 'build/gusto-hud-gamepad-picker-preview.png', 'previews/keyboard-shortcuts.png')
    package.write(root / 'build/gusto-hud-rpg-bars-preview.png', 'previews/rpg-bars.png')
    package.write(root / 'build/gusto-hud-map-controller-preview.png', 'previews/map-controller-step.png')
    package.write(root / 'build/gusto-hud-map-keyboard-preview.png', 'previews/map-keyboard-step.png')
with zipfile.ZipFile(release) as package:
    assert package.testzip() is None
    assert hashlib.sha256(package.read('GUSTO.asi')).hexdigest() == digest
    assert not any(name.endswith('.ini') for name in package.namelist())
checksum = hashlib.sha256(release.read_bytes()).hexdigest()
release.with_suffix('.zip.sha256').write_text(checksum + '  ' + release.name + '\n')
print('Windows redistributable:', release)
print('Plugin SHA256:', digest)
print('Archive SHA256:', checksum)
