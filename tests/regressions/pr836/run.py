#!/usr/bin/env python3
"""Compare real Padme C against an unbounded-integer oracle, without allocation.

Run from a configured Linux checkout. Requires Python 3.9+, a C99 compiler and
GNU-compatible linker. No server, key, network call or giant chunk buffer is used.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import shlex
import subprocess
import tempfile


def oracle(length: int, maximum: int) -> int:
    if length <= 8:
        return 0
    exponent = length.bit_length() - 1
    quantum = 2 ** (exponent - exponent.bit_length())
    rounded = ((length + quantum - 1) // quantum) * quantum
    return min(rounded, maximum) - length


def vectors(bound: int) -> list[tuple[int, int]]:
    cases = {(length, cap) for length in range(1, 4097)
             for cap in (length, min(bound, length + 1), bound)}
    for exponent in range(bound.bit_length()):
        for offset in (-7, -2, -1, 0, 1, 2, 7):
            length = 2 ** exponent + offset
            if not 1 <= length <= bound:
                continue
            e = length.bit_length() - 1
            quantum = 1 if length <= 8 else 2 ** (e - e.bit_length())
            for cap in (length, length + 1, length + quantum - 1,
                        length + quantum, bound):
                cases.add((length, min(bound, cap)))
    randomizer = random.Random(8352026)
    for _ in range(2048):
        length = randomizer.randint(1, bound)
        cases.add((length, randomizer.randint(length, bound)))
    for offset in range(32):
        cases.add((bound - offset, bound))
    return sorted(cases)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument('--source', type=Path)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--probe', type=int, help='Run only this scalar length, up to the supported bound')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    source = (args.source or root/'tar/chunks/chunks_write.c').resolve()
    if not source.is_file() or not (root/'config.h').is_file():
        parser.error('Run autoreconf -i and ./configure first; source must exist.')
    includes = [root, root/'lib-platform', root/'lib/crypto', root/'libcperciva/crypto',
                root/'libcperciva/util', root/'lib/datastruct', root/'tar/chunks', root/'tar/storage']
    with tempfile.TemporaryDirectory(prefix='tarsnap-pr836-') as temp:
        binary = Path(temp)/'padding'
        command = shlex.split(args.cc) + ['-std=c99', '-DHAVE_CONFIG_H', '-O1', '-g',
                    '-Wall', '-Wextra', '-Werror', '-ffunction-sections', '-fdata-sections']
        if args.sanitize:
            command += ['-fsanitize=undefined', '-fno-sanitize-recover=undefined']
        command += ['-I'+str(path) for path in includes]
        command += ['-DCHUNKS_SOURCE='+json.dumps(str(source)),
                    str(Path(__file__).with_name('padding.c')), '-Wl,--gc-sections', '-o', str(binary)]
        compiled = subprocess.run(command, text=True, capture_output=True, timeout=90)
        if compiled.returncode:
            print(compiled.stdout+compiled.stderr)
            return 2
        identity = subprocess.run([str(binary), '--info'], text=True, capture_output=True, timeout=15)
        if identity.returncode:
            print(identity.stderr)
            return 2
        info = json.loads(identity.stdout)
        if args.probe is not None and not 1 <= args.probe <= info['bound']:
            parser.error('Probe length is outside the tested helper domain.')
        cases = [(args.probe, info['bound'])] if args.probe is not None else vectors(info['bound'])
        inputs = ''.join(f'{length} {maximum}\n' for length, maximum in cases)
        result = subprocess.run([str(binary)], input=inputs, text=True, capture_output=True, timeout=30)
    expected = [oracle(length, maximum) for length, maximum in cases]
    try:
        actual = [int(line) for line in result.stdout.splitlines()]
    except ValueError:
        actual = []
    mismatches = [dict(index=i, length=length, maximum=maximum,
                       expected=expected[i], actual=actual[i] if i < len(actual) else None)
                  for i, (length, maximum) in enumerate(cases)
                  if i >= len(actual) or actual[i] != expected[i]]
    data = source.read_bytes()
    report = dict(source_git_blob=hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest(),
                  compiler=args.cc, sanitized=args.sanitize, **info, cases=len(cases),
                  matched=len(cases)-len(mismatches), output_lines=len(actual),
                  input_sha256=hashlib.sha256(inputs.encode()).hexdigest(), exit_code=result.returncode,
                  diagnostics=result.stderr[-4000:], mismatches=mismatches[:20])
    passed = not mismatches and len(actual) == len(cases) and not result.returncode and not result.stderr
    report['passed'] = passed
    text = json.dumps(report, indent=2)+'\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding='utf-8')
    print(text, end='')
    return 0 if passed else 1


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, subprocess.TimeoutExpired) as exc:
        print('Regression execution failed:', type(exc).__name__)
        raise SystemExit(2)
