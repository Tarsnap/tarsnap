#!/usr/bin/env python3
"""Compile and exercise the complete metadata translation unit, offline.

Requires a configured source checkout, a C99 compiler and GNU-compatible linker.
No server, account, private key, production-source editing or network call occurs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def run() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, help='Alternative metadata.c for baseline/control execution')
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    source = (args.source or root / 'tar/multitape/multitape_metadata.c').resolve()
    if not source.is_file() or not (root / 'config.h').is_file():
        parser.error('Run autoreconf -i and ./configure first; the selected source must exist.')
    # These are the actual project headers, not copies of production structs.
    includes = [root, root/'lib-platform', root/'lib/crypto', root/'libcperciva/crypto',
                root/'libcperciva/util', root/'tar/chunks', root/'tar/storage', root/'tar/multitape']
    rows: list[dict] = []
    with tempfile.TemporaryDirectory(prefix='tarsnap-pr838-') as temp:
        binary = Path(temp) / 'cleanup'
        command = shlex.split(args.cc) + ['-std=c99', '-DHAVE_CONFIG_H', '-O1', '-g',
            '-Wall', '-Wextra', '-Werror', '-ffunction-sections', '-fdata-sections']
        if args.sanitize:
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        command += ['-I'+str(p) for p in includes]
        command += ['-DMETADATA_SOURCE='+json.dumps(str(source)), str(Path(__file__).with_name('cleanup.c')),
                    str(root/'libcperciva/crypto/crypto_verify_bytes.c'), '-Wl,--gc-sections', '-o', str(binary)]
        build = subprocess.run(command, text=True, capture_output=True, timeout=90)
        if build.returncode:
            print(build.stdout + build.stderr)
            return 2
        cases = [(scenario, count, quiet, by_name)
                 for scenario in ('success', 'mismatch', 'hash-error')
                 for count in (0, 1, 3) for quiet in (0, 1) for by_name in (0, 1)]
        cases += [(scenario, 3, quiet, by_name)
                  for scenario in ('repeat-mismatch', 'missing', 'read-corrupt', 'read-error',
                                   'bad-signature', 'signature-error', 'short-signature',
                                   'unterminated-name', 'truncated-argv', 'negative-argc', 'trailing-byte')
                  for quiet in (0, 1) for by_name in (0, 1)]
        cases += [('initial-hash-error', 3, quiet, 1) for quiet in (0, 1)]
        cases += [(f'allocation-{number}', 3, 1, by_name) for number in range(1, 7) for by_name in (0, 1)]
        for scenario, count, quiet, by_name in cases:
            result = subprocess.run([str(binary), scenario, str(count), str(quiet), str(by_name)],
                                    capture_output=True, text=True, timeout=15)
            try:
                row = json.loads(result.stdout)
            except ValueError:
                row = dict(scenario=scenario, argc=count, quiet=quiet, by_name=by_name, passed=False,
                           diagnostic=result.stderr[-4000:])
            row['exit_code'] = result.returncode
            if result.returncode or result.stderr:
                row['passed'] = False
                if result.stderr:
                    row['diagnostic'] = result.stderr[-4000:]
            rows.append(row)
    data = source.read_bytes()
    report = {'source_sha256': hashlib.sha256(data).hexdigest(),
              'source_git_blob': hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest(),
              'compiler': args.cc, 'sanitized': args.sanitize, 'cases': len(rows),
              'passed': sum(bool(row['passed']) for row in rows), 'results': rows}
    text = json.dumps(report, indent=2) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding='utf-8')
    print(json.dumps({key: value for key, value in report.items() if key != 'results'}))
    for row in rows:
        if not row['passed']:
            print(json.dumps(row))
    return 0 if report['passed'] == len(rows) else 1


if __name__ == '__main__':
    raise SystemExit(run())
