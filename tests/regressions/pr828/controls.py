#!/usr/bin/env python3
"""Require exact distinguishing failures for PR828's independent cleanup lines."""
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
    assert blob(source) == '5511a8effd072804af97988666d0915e26c02100'
    assert blob(args.baseline) == '29efaf98e1d83d0b4abe5d45a47da762af1b4bb4'
    text = source.read_text()
    marker = '\t\tbsdtar_warnc(bsdtar, 0, "%s", archive_error_string(ina));\n\t\tbsdtar->return_value = 1;\n\t\tarchive_read_finish(ina);'
    assert text.count(marker) == 2
    before_finish = marker.replace('\n\t\tarchive_read_finish(ina);', '')
    first, remainder = text.split(marker, 1)
    variants = {
        'baseline': args.baseline.read_text(),
        'no-file-finish': text.replace(marker, before_finish, 1),
        'no-tape-finish': first + marker + remainder.replace(marker, before_finish, 1),
        'early-finish': text.replace(marker, '\t\tarchive_read_finish(ina);\n' + before_finish),
        'double-finish': text.replace(marker, marker + '\n\t\tarchive_read_finish(ina);'),
    }
    counts = {'baseline': 20, 'no-file-finish': 12, 'no-tape-finish': 12, 'early-finish': 20, 'double-finish': 20}
    out.mkdir(parents=True, exist_ok=True)
    summaries = []
    for name, content in variants.items():
        path, report_path = out / (name + '.c'), out / (name + '.json')
        path.write_text(content)
        command = ['python3', str(Path(__file__).with_name('run.py')), '--root', str(root),
                   '--source', str(path), '--output', str(report_path)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=180)
        (out / (name + '.log')).write_text(result.stdout + result.stderr)
        assert result.returncode == 1, (name, result.returncode, result.stdout, result.stderr)
        report = json.loads(report_path.read_text())
        assert report['source_blob'] == blob(path) and report['cases'] == 36 and report['failed'] == counts[name]
        for row in report['results']:
            n, mode = row['repetitions'], row['mode']
            file_errors = n if mode in ('missing', 'file-recover', 'mixed') else 0
            tape_errors = n if mode in ('tape-fail', 'tape-recover', 'mixed') else 0
            total = file_errors + tape_errors
            leaks = (total if name == 'baseline' else file_errors if name == 'no-file-finish'
                     else tape_errors if name == 'no-tape-finish' else 0)
            late = total if name == 'early-finish' else 0
            duplicate = total if name == 'double-finish' else 0
            assert (row['live'], row['late_error_reads'], row['duplicate_finishes']) == (leaks, late, duplicate), (name, row)
            assert row['bad_returns'] == 0 and row['warnings'] == row['failures']
            assert row['exit_code'] == int(bool(leaks or late or duplicate))
        summaries.append({'variant': name, 'source_blob': blob(path), 'passed': report['passed'],
                          'failed': report['failed'], 'retained_readers': sum(r['live'] for r in report['results']),
                          'late_error_reads': sum(r['late_error_reads'] for r in report['results']),
                          'double_finish_attempts': sum(r['duplicate_finishes'] for r in report['results'])})
    (out / 'summary.json').write_text(json.dumps(summaries, indent=2) + '\n')
    print(json.dumps(summaries))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
