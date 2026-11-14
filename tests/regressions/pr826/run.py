#!/usr/bin/env python3
"""Run actual native write/cache ownership scenarios against disposable files."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

MODES = ('fresh', 'existing', 'lookup-first-alloc', 'lookup-second-alloc',
         'disabled', 'crunch', 'no-stat', 'no-path', 'nonregular',
         'success-nocache', 'success-empty')
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
        parser.error('complete the native build first: autoreconf -i && ./configure && make -j2')
    cache_objects = [p for name in ('tarsnap-ccache_entry.o', 'ccache_entry.o')
                     if (p := root / 'tar/ccache' / name).is_file()]
    if len(cache_objects) != 1:
        parser.error('expected exactly one native cache-entry object')
    report = {'source_blob': blob(source), 'cache_source_blob': blob(root / 'tar/ccache/ccache_entry.c'),
              'test_blobs': {p.name: blob(p) for p in (here / 'entry.c', here / 'fixture.mk', Path(__file__))},
              'caller_compiler': subprocess.check_output([args.cc, '--version'], text=True).splitlines()[0],
              'native_cache_object_sha256': hashlib.sha256(cache_objects[0].read_bytes()).hexdigest(),
              'results': []}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='pr826-') as directory:
        work = Path(directory)
        present = work / 'present'; present.write_bytes(b'synthetic source')
        missing = work / 'missing'
        command = ['make', '-s', '-f', 'Makefile', '-f', str(here / 'fixture.mk'), 'pr826-fixture',
                   'PR826_CC=' + args.cc, 'PR826_SOURCE=' + str(source), 'PR826_OUT=' + str(work / 'build')]
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=120)
        report['build'] = {'command': command, 'exit_code': result.returncode,
                           'stdout': result.stdout, 'stderr': result.stderr}
        if result.returncode:
            report['status'] = 'COMPILE_ERROR'
            args.output.write_text(json.dumps(report, indent=2) + '\n')
            print(result.stdout + result.stderr)
            return 2
        for mode in MODES:
            for verbose in (0, 1):
                for n in COUNTS:
                    try:
                        path = present if mode == 'success-nocache' else missing
                        result = subprocess.run([str(work / 'build/entry'), mode, str(n), str(verbose), str(path)],
                                                text=True, capture_output=True, timeout=10,
                                                env={**os.environ, 'LC_ALL': 'C'})
                        row = json.loads(result.stdout)
                        row.update(exit_code=result.returncode, stderr=result.stderr)
                        row['passed'] = bool(row['passed']) and result.returncode == 0
                        if mode.startswith('success-'):
                            row['passed'] &= not result.stderr
                        else:
                            expected = 'could not open file' if verbose == 0 else 'No such file or directory'
                            row['passed'] &= result.stderr.count(expected) == n
                    except (ValueError, subprocess.TimeoutExpired) as exc:
                        row = {'mode': mode, 'repetitions': n, 'verbose': verbose, 'passed': False,
                               'error': type(exc).__name__}
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
