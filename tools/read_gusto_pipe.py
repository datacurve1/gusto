#!/usr/bin/env python3
"""Read Gusto's local pipe using Windows Python (or Python under Wine)."""
import argparse
import json
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pid', type=int, help='EQ game PID reported by /gusto pipes status')
    args = parser.parse_args()
    if args.pid <= 0:
        parser.error('PID must be positive')
    endpoint = rf'\\.\pipe\gusto_{args.pid}'
    try:
        with open(endpoint, 'rb', buffering=0) as pipe:
            pending = b''
            while True:
                chunk = pipe.read(4096)
                if not chunk:
                    return 0
                pending += chunk
                while b'\n' in pending:
                    record, pending = pending.split(b'\n', 1)
                    message = json.loads(record)
                    data = message['data']
                    if len(data.encode('utf-8')) != message['data_len']:
                        raise ValueError('Invalid pipe data_len')
                    message['data'] = json.loads(data)
                    print(json.dumps(message, ensure_ascii=False), flush=True)
                if len(pending) > 65536:
                    raise ValueError('Oversized pipe record')
    except KeyboardInterrupt:
        return 0
    except (OSError, ValueError, KeyError) as exc:
        print(f'{endpoint}: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
