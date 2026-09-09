#!/usr/bin/env python3
"""Offline real-source storage deletion regression, using bounded event fixtures.

Requires a configured Linux checkout, Python 3.9+, GCC/Clang and GNU-compatible
linker. No real network, transaction, delete, credential or server is used.
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument('--source', type=Path)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    source = (args.source or root/'tar/storage/storage_delete.c').resolve()
    if not source.is_file() or not (root/'config.h').is_file():
        parser.error('Run autoreconf -i and ./configure first; source must exist.')
    includes = [root, root/'lib-platform', root/'lib/crypto', root/'libcperciva/crypto',
                root/'libcperciva/util', root/'tar/storage', root/'lib/netpacket',
                root/'lib/netproto', root/'lib/network']
    cases = [(mode, initial, 0) for mode in ('success', 'issue-reject', 'readonly', 'allocation-failure')
             for initial in (0, 1, 512, 1023, 1024)]
    cases += [('throttle-error', 1024, completed) for completed in (0, 1, 17, 127, 511)]
    cases += [('repeat-reject', 0, count) for count in (1, 2, 17, 100)]
    cases += [('success-after-reject', initial, 0) for initial in (0, 1, 1023, 1024)]
    rows = []
    with tempfile.TemporaryDirectory(prefix='tarsnap-pr834-') as temp:
        binary = Path(temp)/'pending'
        command = shlex.split(args.cc) + ['-std=c99', '-DHAVE_CONFIG_H', '-DUSERAGENT="pr834-offline"',
                    '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-ffunction-sections', '-fdata-sections']
        if args.sanitize:
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-sanitize-recover=all']
        command += ['-I'+str(path) for path in includes]
        command += ['-DDELETE_SOURCE='+json.dumps(str(source)),
                    str(Path(__file__).with_name('pending.c')), '-Wl,--gc-sections', '-o', str(binary)]
        built = subprocess.run(command, text=True, capture_output=True, timeout=90)
        if built.returncode:
            print(built.stdout+built.stderr)
            return 2
        for scenario, initial, parameter in cases:
            result = subprocess.run([str(binary), scenario, str(initial), str(parameter)],
                                    capture_output=True, text=True, timeout=15)
            try:
                row = json.loads(result.stdout)
            except ValueError:
                row = dict(scenario=scenario, initial=initial, parameter=parameter, passed=False)
            row['exit_code'] = result.returncode
            if result.returncode or result.stderr:
                row['passed'] = False
            if result.stderr:
                row['diagnostic'] = result.stderr[-4000:]
            rows.append(row)
    data = source.read_bytes()
    report = dict(source_git_blob=hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest(),
                  compiler=args.cc, sanitized=args.sanitize, cases=len(rows),
                  passed=sum(bool(row['passed']) for row in rows), results=rows)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({key: value for key, value in report.items() if key != 'results'}))
    for row in rows:
        if not row['passed']:
            print(json.dumps(row))
    return 0 if report['passed'] == len(rows) else 1


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, subprocess.TimeoutExpired) as exc:
        print('Regression execution failed:', type(exc).__name__)
        raise SystemExit(2)
