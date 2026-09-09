#!/usr/bin/env python3
"""Check the exact original leak and separate cleanup routing/double-call controls."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import subprocess
from run import blob


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root, out = args.root.resolve(), args.output.resolve()
    source = root / 'tar/write.c'
    assert blob(source) == '1fabdbf5c451004af7d805b4edd2ddb254f911c3'
    assert blob(args.baseline) == '29efaf98e1d83d0b4abe5d45a47da762af1b4bb4'
    text = source.read_text()
    call = '\t\t\tccache_entry_free(cce, bsdtar->write_cookie);'
    assert text.count(call) == 1
    variants = {
        'baseline': args.baseline.read_text(),
        'wrong-cookie': text.replace(call, call.replace('bsdtar->write_cookie', 'NULL')),
        'double-cleanup': text.replace(call, call + '\n' + call),
    }
    out.mkdir(parents=True, exist_ok=True)
    summaries = []
    for name, content in variants.items():
        path, report_path = out / (name + '.c'), out / (name + '.json')
        path.write_text(content)
        result = subprocess.run(['python3', str(Path(__file__).with_name('run.py')), '--root', str(root),
                                 '--source', str(path), '--output', str(report_path)],
                                capture_output=True, text=True, timeout=180)
        (out / (name + '.log')).write_text(result.stdout + result.stderr)
        assert result.returncode == 1, (name, result.returncode, result.stdout, result.stderr)
        report = json.loads(report_path.read_text())
        assert report['source_blob'] == blob(path) and report['cases'] == 88 and report['failed'] == 16
        for row in report['results']:
            n, mode = row['repetitions'], row['mode']
            active = mode in ('fresh', 'existing')
            leaks = (2*n if mode == 'fresh' else n if mode == 'existing' else 0) if name == 'baseline' else 0
            wrong = n if active and name == 'wrong-cookie' else 0
            duplicate = n if active and name == 'double-cleanup' else 0
            assert (row['live'], row['wrong_cookie'], row['invalid_free']) == (leaks, wrong, duplicate), (name, row)
            assert row['return_value'] == 0 and row['open_files'] == 0 and row['tree_preserved']
            assert row['exit_code'] == int(active)
        summaries.append({'variant': name, 'source_blob': blob(path), 'passed': report['passed'], 'failed': report['failed'],
                          'retained_allocations': sum(r['live'] for r in report['results']),
                          'wrong_callback_cookies': sum(r['wrong_cookie'] for r in report['results']),
                          'invalid_free_attempts': sum(r['invalid_free'] for r in report['results'])})
    (out / 'summary.json').write_text(json.dumps(summaries, indent=2) + '\n')
    print(json.dumps(summaries))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
