#!/usr/bin/env python3
"""Read-only Linux controller/USB diagnosis; run on the game host, outside sandboxes."""
from pathlib import Path
import os


def read(path):
    try:
        return path.read_text().strip()
    except OSError:
        return ''


def main():
    usb_root = Path('/sys/bus/usb/devices')
    detected = False
    for device in sorted(usb_root.glob('*')):
        vendor, product = read(device / 'idVendor'), read(device / 'idProduct')
        name = read(device / 'product')
        if vendor == '1d6b':
            continue
        if vendor not in ('2dc8', '045e', '054c', '057e') and not any(
            token in name.lower() for token in ('controller', 'gamepad', 'joystick')
        ):
            continue
        detected = True
        print(f'USB {vendor}:{product}: {name}')
        unbound = False
        for interface in sorted(usb_root.glob(device.name + ':*')):
            driver = interface / 'driver'
            driver_name = driver.resolve().name if driver.exists() else 'UNBOUND'
            print(f'  {interface.name}: {driver_name}')
            unbound |= driver_name == 'UNBOUND'
        if vendor == '2dc8' and product == '3109':
            print('  Receiver is IDLE: wake/pair the controller; no gamepad input is expected yet.')
        if vendor == '2dc8' and product == '3106' and unbound:
            print('  8BitDo Xbox-mode interface needs the xpad driver.')
            print('  Administrator command: sudo /sbin/modprobe xpad')
            print('  If still unbound, register this ID for the current boot:')
            print("  printf '2dc8 3106\\n' | sudo tee /sys/bus/usb/drivers/xpad/new_id")
    if not detected:
        print('No supported controller/receiver found in USB sysfs.')
    inputs = read(Path('/proc/bus/input/devices'))
    controllers = []
    for block in inputs.split('\n\n'):
        if 'Handlers=' in block and ('js' in block or any(
            name in block.lower() for name in ('controller', 'gamepad', 'joystick')
        )):
            controllers.append(block)
    for block in controllers:
        for line in block.splitlines():
            if line.startswith(('N:', 'H:')):
                print(line)
    print(f'Linux controller input devices: {len(controllers)}')
    devices = Path('/dev/input')
    if not devices.exists():
        print('/dev/input is unavailable here; repeat outside the sandbox/container.')
    else:
        for device in sorted(devices.glob('js*')):
            print(f'{device}: readable={os.access(device, os.R_OK)}')
    print('After Linux detects input, check Wine using the game’s prefix:')
    print('  WINEPREFIX=/path/to/game-prefix wine control joy.cpl')
    print('Gusto: Gamepad → Backend Auto, enable gamepad, select controller slot.')


if __name__ == '__main__':
    main()
