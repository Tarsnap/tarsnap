#!/usr/bin/env python3
"""Run the full native pathname editor, preserving its real libarchive behavior."""
from __future__ import annotations

import argparse
import errno
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def case(name, path, final, duplicate=0, **kw):
    return dict(name=name, path=path, final=final, duplicate=duplicate, **kw)


# Expectations are explicit, not computed by a second implementation of the editor.
CASES = [
    case('relative', 'file.txt', 'file.txt'),
    case('dot-relative-is-retained', './file.txt', './file.txt'),
    case('nested-relative', 'one/two/file', 'one/two/file'),
    case('parent-relative-is-retained', '../file', '../file'),
    case('unicode-relative', 'résumé/資料.txt', 'résumé/資料.txt'),
    case('empty-becomes-dot', '', '.', 1),
    case('root', '/', '.', 1, warning='slash'),
    case('repeated-root', '////', '.', 1, warning='slash'),
    case('absolute', '/one/file', 'one/file', 1, warning='slash'),
    case('repeated-leading-slashes', '///one/file', 'one/file', 1, warning='slash'),
    case('absolute-parent', '/../one/file', 'one/file', 1, warning='drive'),
    case('absolute-parent-only', '/../', '.', 1, warning='drive'),
    case('backslash-prefix', '\\one\\file', 'one\\file', 1, warning='slash'),
    case('drive-prefix', 'C:\\one\\file', 'one\\file', 1, warning='drive'),
    case('drive-relative', 'c:one/file', 'one/file', 1, warning='drive'),
    case('windows-device', '//./C:/one/file', 'one/file', 1, warning='drive'),
    case('windows-unc', '//?/UNC/server/share/file', 'server/share/file', 1, warning='drive'),
    case('windows-backslash-unc', '\\\\?\\UNC\\server\\share\\file', 'server\\share\\file', 1, warning='drive'),
    case('mixed-prefix', '//C:/../file', 'file', 1, warning='drive'),
    case('absolute-allowed', '/one/file', '/one/file', absolute=1),
    case('absolute-allowed-repeated', '///one/file', '/one/file', 1, absolute=1),
    case('drive-allowed', 'C:\\one\\file', 'C:\\one\\file', absolute=1),
    case('strip-one', 'one/two/file', 'two/file', 1, strip=1),
    case('strip-two', 'one/two/file', 'file', 1, strip=2),
    case('strip-too-many', 'one/two/file', 'one/two/file', strip=3, result=1),
    case('strip-repeated-separators', 'one///file', 'file', 1, strip=1),
    case('strip-dot-relative', './file', 'file', 1, strip=1),
    case('strip-trailing-only', 'one/', 'one/', strip=1, result=1),
    case('strip-hardlink', 'one/file', 'file', 1, strip=1, hardlink='one/target', final_hardlink='target'),
    case('short-hardlink-skips-entry', 'one/file', 'one/file', strip=1, hardlink='target', result=1),
    case('symlink-target-is-retained', 'one/file', 'file', 1, strip=1, symlink='../target'),
    case('substitution-only', 'old/file', 'new/file', rule=',^old/,new/,', copies_before=1),
    case('substitution-then-strip', 'old/file', 'file', 1, rule=',^old/,new/,', strip=1, pre_guard='new/file', copies_before=1),
    case('substitution-then-absolute', 'old/file', 'new/file', 1, rule=',^old/,/new/,', pre_guard='/new/file', copies_before=1, warning='slash'),
    case('empty-substitution-skips', 'old', '', rule=',old,,', result=-1, copies_before=1),
    case('substitution-no-match', 'same/file', 'same/file', rule=',^old/,new/,'),
    case('bad-backreference-skips', 'old', 'old', rule=',old,\\1,', result=1, invalid_substitution=True),
    case('unicode-absolute', '/résumé/資料.txt', 'résumé/資料.txt', 1, warning='slash'),
    case('long-path', '/prefix/' + 'a' * 2048, 'prefix/' + 'a' * 2048, 1, warning='slash'),
]


def blob(path: Path) -> str:
    data = path.read_bytes()
    return hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()


def expected(c: dict, fault: int, quiet: int) -> dict:
    fatal = bool(fault and c['duplicate'])
    warning = c.get('warning') and not quiet
    diagnostics = []
    if warning:
        if c['warning'] == 'slash':
            first = c.get('pre_guard', c['path'])[0]
            diagnostics.append(f"pr824-fixture: Removing leading '{first}' from member names\n")
        else:
            diagnostics.append('pr824-fixture: Removing leading drive letter from member names\n')
    if c.get('invalid_substitution'):
        diagnostics.append('pr824-fixture: Invalid substitution, skipping entry\n')
    if fatal:
        diagnostics.append('pr824-fixture: Out of memory: ' + os.strerror(errno.ENOMEM) + '\n')
    return dict(
        exit_code=1 if fatal else 0, returned=0 if fatal else 1,
        result=-999 if fatal else c.get('result', 0),
        duplicates=c['duplicate'], failed_duplicates=int(fatal),
        released=0 if fatal else c['duplicate'],
        copies=c.get('copies_before', 0) + (0 if fatal else c['duplicate']),
        null_copies=0, warned_lead_slash=int(bool(warning)),
        path=c.get('pre_guard', c['path']) if fatal else c['final'],
        hardlink=c.get('final_hardlink', c.get('hardlink')),
        symlink=c.get('symlink'), stderr=''.join(diagnostics),
    )


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[3])
    p.add_argument('--source', type=Path)
    p.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    p.add_argument('--sanitize', action='store_true', help='instrument the caller/complete util.c; dependencies stay native')
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    root = args.root.resolve()
    source = (args.source or root / 'tar/util.c').resolve()
    here = Path(__file__).resolve().parent
    if not (root / 'tarsnap').is_file() or not (root / 'libarchive/libarchive.a').is_file():
        p.error('complete native build required: autoreconf -i && ./configure && make -j2')
    report = dict(source_blob=blob(source), libarchive_source_blob=blob(root / 'libarchive/archive_entry.c'),
                  libarchive_archive_sha256=hashlib.sha256((root / 'libarchive/libarchive.a').read_bytes()).hexdigest(),
                  caller_compiler=subprocess.check_output([args.cc, '--version'], text=True).splitlines()[0],
                  sanitized_caller=args.sanitize,
                  test_blobs={f.name: blob(f) for f in (here / 'pathname.c', here / 'fixture.mk', Path(__file__))},
                  results=[])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='pr824-') as d:
        work = Path(d)
        command = ['make', '-s', '-f', 'Makefile', '-f', str(here / 'fixture.mk'), 'pr824-fixture',
                   'PR824_CC=' + args.cc, 'PR824_SOURCE=' + str(source), 'PR824_OUT=' + str(work)]
        if args.sanitize:
            command.append('PR824_EXTRA=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer')
        build = subprocess.run(command, cwd=root, text=True, capture_output=True, timeout=120)
        report['build'] = dict(command=command, exit_code=build.returncode, stdout=build.stdout, stderr=build.stderr)
        if build.returncode:
            report['status'] = 'COMPILE_ERROR'
            args.output.write_text(json.dumps(report, indent=2) + '\n')
            print(build.stdout + build.stderr)
            return 2
        for c in CASES:
            for fault in (0, 1):
                for quiet in (0, 1):
                    argv = [str(work / 'pathname'), c['path'], str(c.get('strip', 0)),
                            str(c.get('absolute', 0)), str(quiet), c.get('rule', ''),
                            c.get('hardlink', ''), c.get('symlink', ''), str(fault)]
                    row = dict(case=c['name'], fault=fault, quiet=quiet, expected=expected(c, fault, quiet))
                    env = {**os.environ, 'LC_ALL': 'C', 'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1',
                           'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'}
                    try:
                        r = subprocess.run(argv, cwd=work, text=True, capture_output=True, timeout=10, env=env)
                        row.update(exit_code=r.returncode, stdout=r.stdout, stderr=r.stderr)
                        actual = json.loads(r.stdout)
                        for key in ('path', 'hardlink', 'symlink'):
                            if actual[key] is not None:
                                actual[key] = bytes.fromhex(actual[key]).decode('utf-8')
                        actual.update(exit_code=r.returncode, stderr=r.stderr)
                        row['actual'] = actual
                        row['mismatches'] = {k: dict(expected=v, actual=actual.get(k))
                                             for k, v in row['expected'].items() if actual.get(k) != v}
                        row['passed'] = not row['mismatches']
                    except (ValueError, KeyError, subprocess.TimeoutExpired) as exc:
                        row.update(passed=False, error=type(exc).__name__)
                    report['results'].append(row)
    report['cases'] = len(report['results'])
    report['passed'] = sum(r['passed'] for r in report['results'])
    report['failed'] = report['cases'] - report['passed']
    report['status'] = 'PASS' if not report['failed'] else 'FAIL'
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
    print(json.dumps({k: report[k] for k in ('source_blob', 'caller_compiler', 'sanitized_caller', 'status', 'cases', 'passed', 'failed')}))
    return 0 if not report['failed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
