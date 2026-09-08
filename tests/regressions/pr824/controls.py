#!/usr/bin/env python3
"""Distinguish guard absence, ordering, status, errno and quiet-mode regressions."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys

from run import blob

FIXED_BLOB = '8b6a162b7835cffcd59c7ae3c39d77ec3002bbaf'
BASELINE_BLOB = 'e16564560086441ccbc7f7beade122b1ac115bc2'
GUARD = '\t\tif (q == NULL)\n\t\t\tbsdtar_errc(bsdtar, 1, errno, "Out of memory");\n'
COPY = '\t\tarchive_entry_copy_pathname(entry, q);\n'


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[3])
    p.add_argument('--cc', default='gcc')
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    root, out = args.root.resolve(), args.output.resolve()
    source = root / 'tar/util.c'
    if blob(source) != FIXED_BLOB:
        p.error('controls require the exact PR824 source, not arbitrary string replacement')
    text = source.read_text()
    assert text.count(GUARD) == 1 and text.count(GUARD + COPY) == 1
    variants = {
        'baseline': text.replace('\n' + GUARD, ''),
        'late-check': text.replace(GUARD + COPY, COPY + GUARD),
        'success-exit': text.replace(GUARD, GUARD.replace('bsdtar, 1,', 'bsdtar, 0,')),
        'errno-omitted': text.replace(GUARD, GUARD.replace('1, errno,', '1, 0,')),
        'quiet-gated': text.replace(GUARD, GUARD.replace('q == NULL', 'q == NULL && !bsdtar->option_quiet')),
    }
    out.mkdir(parents=True, exist_ok=True)
    runner = Path(__file__).with_name('run.py')
    results = {}
    for name, content in variants.items():
        alternate = out / (name + '.c')
        alternate.write_text(content)
        if name == 'baseline':
            assert blob(alternate) == BASELINE_BLOB
        report_path = out / (name + '.json')
        r = subprocess.run([sys.executable, str(runner), '--root', str(root), '--source', str(alternate),
                            '--cc', args.cc, '--output', str(report_path)], text=True,
                           capture_output=True, timeout=180)
        (out / (name + '.log')).write_text(r.stdout + r.stderr)
        report = json.loads(report_path.read_text())
        assert r.returncode == 1 and report['status'] == 'FAIL', (name, r.returncode, report.get('status'))
        expected_count = 25 if name == 'quiet-gated' else 50
        failures = [row for row in report['results'] if not row['passed']]
        assert report['cases'] == 156 and len(failures) == expected_count, (name, len(failures))
        for row in report['results']:
            should_fail = bool(row['expected']['failed_duplicates']) and (name != 'quiet-gated' or row['quiet'] == 1)
            assert row['passed'] != should_fail, (name, row['case'], row['fault'], row['quiet'])
            assert 'error' not in row, (name, row)
            if not should_fail:
                continue
            actual, expected = row['actual'], dict(row['expected'])
            if name in ('baseline', 'quiet-gated'):
                expected.update(exit_code=0, returned=1, result=0,
                                copies=expected['copies'] + 1, null_copies=1, path=None)
                expected['stderr'] = expected['stderr'].split('pr824-fixture: Out of memory:', 1)[0]
            elif name == 'late-check':
                expected.update(copies=expected['copies'] + 1, null_copies=1, path=None)
            elif name == 'success-exit':
                expected['exit_code'] = 0
            else:
                expected['stderr'] = expected['stderr'].split('pr824-fixture: Out of memory:', 1)[0] + 'pr824-fixture: Out of memory\n'
            assert actual == expected, (name, row['case'], actual, expected)
        results[name] = dict(source_blob=blob(alternate), cases=report['cases'],
                             failed=report['failed'], exact_failure_signatures=True)
    (out / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps(results))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
