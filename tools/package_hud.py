#!/usr/bin/env python3
"""Package the current VERSION after building and generating preview PNGs."""
from pathlib import Path
import hashlib,zipfile,json
root=Path(__file__).resolve().parents[1]
version=(root/'VERSION').read_text().strip()
release=root/f'releases/GUSTO-{version}-RoF2-Windows-Wine.zip'
dll=root/'build/GUSTO.dll'
digest=hashlib.sha256(dll.read_bytes()).hexdigest()
files=['VERSION','CHANGELOG.md','cmake/version.hpp.in','cmake/mingw32-toolchain.cmake','windows/gusto_version.rc.in','tools/package_hud.py','CMakeLists.txt','THIRD_PARTY_NOTICES.md','README.md','docs/hud.md','test/hud_model_tests.cpp','test/hud_preview.cpp','test/gusto_safety_tests.cpp','tools/verify_hud.py']
files+=['test/hook_chain_tests.cpp','tools/package_windows.py','docs/windows.md','windows/Install-Gusto.ps1','tools/verify_windows_package.py','tools/windows_runtime_smoke.cpp','tools/check_controller_linux.py','tools/controller_probe.cpp','tools/99-8bitdo-xpad.rules','test/pipe_snapshot_tests.cpp','docs/pipes.md','tools/read_gusto_pipe.py','include/gusto/third_party/json.hpp','test/pipe_tests.cpp','test/pipe_model_tests.cpp','test/gamepad_preview.cpp','docs/life_effects.md','docs/gamepad.md','test/gamepad_tests.cpp','test/life_fx_tests.cpp','test/life_fx_preview.cpp','tools/create_life_fx.py','tools/verify_life_assets.py']
files += ['test/bank_expansion_tests.cpp', 'docs/bank.md']
files += [str(p.relative_to(root)) for d in ('assets/life_fx','assets/ui') for p in (root/d).rglob('*') if p.is_file()]
files+= [str(p.relative_to(root)) for d in ('src/client/rof2','include/gusto/client/rof2') for p in (root/d).glob('*') if p.suffix in ('.cpp','.hpp')]
manifest={name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in sorted(set(files))}
assert ('GUSTO '+version+' - RoF2').encode()+b'\0' in dll.read_bytes(), 'Rebuild the plugin after changing VERSION'
assert version.encode('utf-16le') in dll.read_bytes(), 'Windows version metadata does not match VERSION'
info={'version':version,'binary_sha256':digest,
'native_game_sha256':'2a8702ad9f722704f01355c0750be7d6f164a8b9c9128ba0cf286ea32b405b0e',
'native_graphics_sha256':'ade556b68e98f6f39c5578f70431e11c0b9babcc15b8571eb22cf1e2b654417e',
'validation':'Release MinGW x86 build; safety, tagging, HUD, controller, life-effect and pipe suites under isolated Wine; D3D9 menu/settings/controller mouse capture, NPC bar world/UI-stage depth-clear regressions, actor model/invisible/alpha/DPVS visibility fixtures, no-visible-model pixel check and embedded meshes; standard ASI exports, system-only imports, runtime version and unsupported-host smoke checks. Live controller movement, keyboard/mouse mapping and 3D tags confirmed. Live NPC occlusion after the render-stage fix and physical Windows hardware remain unconfirmed.',
'source_sha256':manifest}

with zipfile.ZipFile(release,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 z.write(dll,'GUSTO.asi')
 for name in ('VERSION','CHANGELOG.md','README.md','LICENSE','THIRD_PARTY_NOTICES.md','docs/hud.md','docs/tagging.md','tools/verify_hud.py','tools/verify_tagging.py','docs/life_effects.md','docs/gamepad.md','docs/windows.md','docs/pipes.md','tools/read_gusto_pipe.py','tools/check_controller_linux.py','tools/controller_probe.cpp','tools/99-8bitdo-xpad.rules','tools/create_life_fx.py','tools/verify_life_assets.py'):
  z.write(root/name,name)
 z.write(root/'docs/bank.md','docs/bank.md')
 for asset in (root/'assets/ui').rglob('*'):
  if asset.is_file():z.write(asset,str(asset.relative_to(root)))
 for asset in (root/'assets/life_fx').rglob('*'):
  if asset.is_file():z.write(asset,str(asset.relative_to(root)))
 z.write(root/'build/gusto-life-effects-preview.png','previews/life-effects-d3d9.png')
 z.write(root/'build/gusto-hud-preview-6.png','previews/death-fd-menu.png')
 z.write(root/'build/gusto-hud-preview-7.png','previews/gamepad-menu.png')
 z.write(root/'build/gusto-hud-preview-8.png','previews/pipes-menu.png')
 z.write(root/'build/gusto-hud-rpg-bars-preview.png','previews/rpg-bars.png')
 z.write(root/'build/gusto-hud-gamepad-picker-preview.png','previews/controller-command-picker.png')
 z.write(root/'build/gusto-hud-gamepad-device-preview.png','previews/controller-device-settings.png')
 z.write(root/'build/gusto-hud-map-controller-preview.png','previews/map-controller-step.png')
 z.write(root/'build/gusto-hud-map-keyboard-preview.png','previews/map-keyboard-step.png')
 z.writestr('BUILD.json',json.dumps(info,indent=2)+'\n')
 z.writestr('GUSTO.asi.sha256',digest+'  GUSTO.asi\n')
 z.write(root/'build/gusto-hud-preview.png','preview.png')
 z.write(root/'build/gusto-external-map-preview.png','previews/external-map.png')
 for i,name in enumerate(('combat','overhead-bars','nameplates','camera','tags'),1):
  z.write(root/f'build/gusto-hud-preview-{i}.png',f'previews/{name}.png')
with zipfile.ZipFile(release) as z:
 assert z.testzip() is None
 assert hashlib.sha256(z.read('GUSTO.asi')).hexdigest()==digest
zipdigest=hashlib.sha256(release.read_bytes()).hexdigest()
release.with_suffix('.zip.sha256').write_text(zipdigest+'  '+release.name+'\n')
(root/'build/hud-expected-sha256.txt').write_text(digest+'\n')
print('Packaged:',release)
print('ASI SHA256:',digest)
print('ZIP SHA256:',zipdigest)
