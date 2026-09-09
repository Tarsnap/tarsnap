#!/usr/bin/env python3
"""Exercise the complete native append entrypoints with bounded local inputs."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

MODES = ('missing', 'junk', 'directory', 'file-ok', 'tape-fail', 'tape-ok',
         'file-recover', 'tape-recover', 'mixed')
COUNTS = (1, 3, 8, 32)


def blob(path: Path) -> str:
    data = path.read_bytes()
    return hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument('--source', type=Path)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    source = (args.source or root / 'tar/write.c').resolve()
    here = Path(__file__).resolve().parent
    if not (root / 'tarsnap').is_file() or not (root / 'libarchive/libarchive.a').is_file():
        parser.error('complete the normal native build first: autoreconf -i && ./configure && make -j2')
    report = {'source_blob': blob(source), 'test_blobs': {p.name: blob(p) for p in
              (here / 'append.c', here / 'fixture.mk', Path(__file__))},
              'caller_compiler': subprocess.check_output([args.cc, '--version'], text=True).splitlines()[0],
              'native_libarchive_sha256': hashlib.sha256((root / 'libarchive/libarchive.a').read_bytes()).hexdigest(),
              'results': []}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='pr828-') as directory:
        work = Path(directory)
        empty = work / 'empty.tar'; empty.write_bytes(bytes(1024))
        junk = work / 'junk'; junk.write_bytes(b'\xff\0BAD-ARCHIVE\x01' * 100)
        subdir = work / 'directory'; subdir.mkdir()
        command = ['make', '-s', '-f', 'Makefile', '-f', str(here / 'fixture.mk'), 'pr828-fixture',
                   'PR828_CC=' + args.cc, 'PR828_SOURCE=' + str(source), 'PR828_OUT=' + str(work / 'build')]
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=120)
        report['build'] = {'command': command, 'exit_code': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}
        if result.returncode:
            report['status'] = 'COMPILE_ERROR'
            args.output.write_text(json.dumps(report, indent=2) + '\n')
            print(result.stdout + result.stderr)
            return 2
        for mode in MODES:
            for n in COUNTS:
                try:
                    result = subprocess.run([str(work / 'build/append'), mode, str(n),
                            str(work / 'missing'), str(junk), str(subdir), str(empty)],
                            text=True, capture_output=True, timeout=10)
                    row = json.loads(result.stdout)
                    row['exit_code'], row['stderr'] = result.returncode, result.stderr
                    row['passed'] = bool(row['passed']) and result.returncode == 0
                    # Verify real diagnostics, not just the new/free counter.
                    if mode in ('missing', 'file-recover', 'mixed'):
                        row['passed'] &= 'Failed to open' in result.stderr
                    if mode in ('junk', 'directory', 'mixed'):
                        row['passed'] &= 'Unrecognized archive format' in result.stderr
                    if mode in ('tape-fail', 'tape-recover', 'mixed'):
                        row['passed'] &= 'Synthetic unavailable archive' in result.stderr
                    if mode in ('file-ok', 'tape-ok'):
                        row['passed'] &= not result.stderr
                except (ValueError, subprocess.TimeoutExpired) as exc:
                    row = {'mode': mode, 'repetitions': n, 'passed': False, 'error': type(exc).__name__}
                    if isinstance(exc, ValueError):
                        row.update(exit_code=result.returncode, stdout=result.stdout, stderr=result.stderr)
                report['results'].append(row)
    report['cases'] = len(report['results'])
    report['passed'] = sum(row['passed'] for row in report['results'])
    report['failed'] = report['cases'] - report['passed']
    report['status'] = 'PASS' if report['failed'] == 0 else 'FAIL'
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: report[k] for k in ('source_blob', 'caller_compiler', 'status', 'cases', 'passed', 'failed')}))
    return 0 if not report['failed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
