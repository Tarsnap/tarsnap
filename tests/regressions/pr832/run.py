#!/usr/bin/env python3
"""Run PR832's complete C reader and real Patricia tree on temporary files.

Requires a configured Linux checkout, Python3.9+, a C99 compiler, and the
repository's unchanged support sources. No Tarsnap account, keys or server.
The fixture reports parser allocations before freeing its own leftovers;
leaks are assertion failures, not suppressed sanitizer results.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[3]
READER = Path('tar/ccache/ccache_read.c')


def identity(path: Path) -> dict:
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data),
                sha256=hashlib.sha256(data).hexdigest(),
                git_blob=hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest())


def image(rows: list[tuple[bytes, bytes, int]], chunk_size: int) -> tuple[bytes, dict]:
    metadata = [struct.pack('<I', len(rows))]
    payloads = []
    expected = dict(records=len(rows), chunks=0, trailer_bytes=0,
                    plain_bytes=0, inode_sum=0, payload_sum=0)
    for index, (key, plain, chunks) in enumerate(rows):
        compressed = zlib.compress(plain) if plain else b''
        ino = 100 + index
        metadata.append(struct.pack('<QQQQIIIII', ino, len(plain) + chunks * 32,
                                    1000, chunks, len(plain), len(compressed),
                                    0, len(key), 1))
        metadata.append(key)
        payloads.append(b'\0' * (chunks * chunk_size) + compressed)
        expected['chunks'] += chunks
        expected['trailer_bytes'] += len(compressed)
        expected['plain_bytes'] += len(plain)
        expected['inode_sum'] += ino
        expected['payload_sum'] += sum(compressed)
    return b''.join(metadata + payloads), expected


def cases(chunk_size: int, mode: str) -> list[dict]:
    one_rows = [(b'/a', b'abc', 0)]
    three_rows = [(b'/a', b'abc', 0), (b'/b', b'defg', 0), (b'/c', b'ijklm', 0)]
    one, one_expected = image(one_rows, chunk_size)
    three, three_expected = image(three_rows, chunk_size)
    empty, empty_expected = image([], chunk_size)
    duplicate, _ = image([one_rows[0], one_rows[0]], chunk_size)
    growing, _ = image([(b'/a', b'abc', 0), (b'/' + b'long' * 32, b'defg', 0)], chunk_size)
    chunk_only, chunk_expected = image([(b'/a', b'', 1)], chunk_size)
    mixed, mixed_expected = image([(b'/a', b'alpha', 1), (b'/b', b'beta', 0)], chunk_size)
    invalid, _ = image([(b'/a', b'', 0)], chunk_size)
    output = []

    def add(name: str, content: bytes | None, success: bool,
            expected: dict | None = None, insert: int = 0, zero_null: int = 0,
            malloc: int = 0, realloc: int = 0) -> None:
        output.append(dict(name=name, content=content, success=success,
                           expected=expected or empty_expected, insert=insert,
                           zero_null=zero_null, malloc=malloc, realloc=realloc))

    add('missing-file', None, True, empty_expected)
    add('empty-cache', empty, True, empty_expected)
    add('empty-cache-null-zero-allocation', empty, True, empty_expected, zero_null=1)
    add('one-record', one, True, one_expected)
    add('three-records', three, True, three_expected)
    add('chunk-only-record', chunk_only, True, chunk_expected)
    add('mixed-chunks-and-trailers', mixed, True, mixed_expected)
    add('truncated-count', b'\0\0', False)
    add('truncated-record', one[:4 + 51], False)
    add('truncated-path', one[:4 + 52 + len(one_rows[0][0]) - 1], False)
    add('truncated-payload', one[:-1], False)
    add('invalid-record-without-data', invalid, False)
    add('duplicate-key', duplicate, False)
    add('insert-failure-first', one, False, insert=1)
    add('insert-failure-middle', three, False, insert=2)
    add('insert-failure-last', three, False, insert=3)
    add('context-allocation-failure', one, False, malloc=1)
    add('record-allocation-failure', one, False, malloc=2)
    add('second-record-allocation-failure', three, False, malloc=3)
    add('data-allocation-failure', one, mode == 'mmap', one_expected, malloc=3)
    add('first-path-allocation-failure', one, False, realloc=1)
    add('growing-path-allocation-failure', growing, False, realloc=2)
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--patricia-source', type=Path,
                        help='Explicit dependency source for a labeled composition run')
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--mode', choices=['read', 'mmap'], required=True)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    source = (args.source or root / READER).resolve()
    patricia = (args.patricia_source or root / 'lib/datastruct/patricia.c').resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = dict(status='error', mode=args.mode, compiler=args.cc,
                  sanitized=args.sanitize, cases=[], failed_cases=[])
    try:
        if not (root / 'config.h').is_file():
            raise ValueError('Run autoreconf -i and ./configure before the regression.')
        report['source'] = identity(source)
        report['patricia_source'] = identity(patricia)
        report['dependency_override'] = args.patricia_source is not None
        report['fixture'] = identity(Path(__file__).with_name('cache_read.c'))
        report['runner'] = identity(Path(__file__))
        includes = [root, root / 'lib-platform', root / 'libcperciva/util',
                    root / 'libcperciva/crypto', root / 'lib/crypto',
                    root / 'lib/datastruct', root / 'tar/ccache',
                    root / 'tar/multitape', root / 'tar/chunks', root / 'tar/storage']
        with tempfile.TemporaryDirectory(prefix='pr832-native-') as temporary:
            temp = Path(temporary)
            binary = temp / 'cache-read'
            command = shlex.split(args.cc) + [
                '-std=c99', '-DHAVE_CONFIG_H', '-include', str(root / 'config.h'),
                '-O1', '-g', '-Wall', '-Wextra', '-Werror',
            ]
            if args.mode == 'read':
                command.append('-DTEST_NO_MMAP')
            if args.sanitize:
                command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
            command += ['-I' + str(path) for path in includes]
            command += ['-DCCACHE_SOURCE=' + json.dumps(str(source)),
                        str(Path(__file__).with_name('cache_read.c')),
                        str(patricia),
                        str(root / 'libcperciva/util/asprintf.c'),
                        str(root / 'libcperciva/util/warnp.c'), '-o', str(binary)]
            report['compile_command'] = command
            compiled = subprocess.run(command, text=True, capture_output=True, timeout=90)
            (output / 'compile.stdout.txt').write_text(compiled.stdout)
            (output / 'compile.stderr.txt').write_text(compiled.stderr)
            if compiled.returncode:
                raise ValueError(f'Compiler exited {compiled.returncode}; see compile.stderr.txt')
            version = subprocess.run(shlex.split(args.cc) + ['--version'], text=True,
                                     capture_output=True, check=True, timeout=15)
            report['compiler_version'] = version.stdout.splitlines()[0]
            info = subprocess.run([str(binary), '--layout'], text=True,
                                  capture_output=True, check=True, timeout=15)
            layout = json.loads(info.stdout)
            report['layout'] = layout
            if layout['mode'] != args.mode:
                raise ValueError('The actual compiled mmap mode differs from the requested mode.')
            env = os.environ.copy()
            env['ASAN_OPTIONS'] = 'detect_leaks=1:halt_on_error=1'
            env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=0'
            for case in cases(layout['chunkheader_size'], args.mode):
                directory = temp / case['name']
                directory.mkdir()
                if case['content'] is not None:
                    (directory / 'cache').write_bytes(case['content'])
                    (output / (case['name'] + '.cache')).write_bytes(case['content'])
                command = [str(binary), str(directory), str(case['insert']),
                           str(case['zero_null']), str(case['malloc']), str(case['realloc'])]
                result = subprocess.run(command, text=True, capture_output=True,
                                        timeout=15, env=env)
                (output / (case['name'] + '.stdout.txt')).write_text(result.stdout)
                (output / (case['name'] + '.stderr.txt')).write_text(result.stderr)
                entry = dict(name=case['name'], passed=False, returncode=result.returncode,
                             expected_success=case['success'], problems=[])
                try:
                    observed = json.loads(result.stdout)
                    entry['observed'] = observed
                    expected = dict(success=case['success'], visit_result=0,
                                    live_allocations=0, live_bytes=0, open_files=0,
                                    zero_reads=0, insert_injected=bool(case['insert']))
                    if case['success']:
                        expected.update(case['expected'])
                    for key, value in expected.items():
                        if observed.get(key) != value:
                            entry['problems'].append(f'{key}: {observed.get(key)!r} != {value!r}')
                    if case['realloc'] and observed['realloc_calls'] < case['realloc']:
                        entry['problems'].append('The requested realloc failure was not reached.')
                    malloc_expected = case['malloc'] and not (
                        case['name'] == 'data-allocation-failure' and args.mode == 'mmap')
                    if malloc_expected and observed['malloc_calls'] < case['malloc']:
                        entry['problems'].append('The requested malloc failure was not reached.')
                except (ValueError, KeyError) as exc:
                    entry['problems'].append('Invalid native observations: ' + str(exc))
                if result.returncode:
                    entry['problems'].append('Native process did not exit successfully.')
                if 'runtime error:' in result.stderr or 'Sanitizer' in result.stderr:
                    entry['problems'].append('Sanitizer diagnostic present.')
                entry['passed'] = not entry['problems']
                report['cases'].append(entry)
            report['failed_cases'] = [entry['name'] for entry in report['cases'] if not entry['passed']]
            report['passed_cases'] = len(report['cases']) - len(report['failed_cases'])
            report['status'] = 'failed' if report['failed_cases'] else 'passed'
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        report['error'] = str(exc)
    text = json.dumps(report, indent=2) + '\n'
    (output / 'report.json').write_text(text, encoding='utf-8')
    print(text, end='')
    return 0 if report['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
