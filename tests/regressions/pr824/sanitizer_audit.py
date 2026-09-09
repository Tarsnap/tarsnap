#!/usr/bin/env python3
"""Describe unchanged sanitizer failures; never relabel the full suites PASS."""
import argparse
import json
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--candidate', type=Path, required=True)
    p.add_argument('--baseline', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    reports = {k: json.loads(getattr(args, k).read_text()) for k in ('candidate', 'baseline')}
    expected = {'candidate': ('8b6a162b7835cffcd59c7ae3c39d77ec3002bbaf', 128, 28),
                'baseline': ('e16564560086441ccbc7f7beade122b1ac115bc2', 84, 72)}
    known = {}
    summary = {'full_sanitizer_status': 'FAIL', 'suppressions': False, 'reports': {}}
    for name, r in reports.items():
        assert r['sanitized_caller'] and r['status'] == 'FAIL'
        assert (r['source_blob'], r['passed'], r['failed']) == expected[name]
        assert r['cases'] == 156
        issues = {}
        for row in r['results']:
            key = (row['case'], row['fault'], row['quiet'])
            if 'memcpy-param-overlap' in row['stderr']:
                assert row['case'] == 'strip-hardlink' and not row['passed']
                issues[key] = 'unchanged hardlink-copy overlap'
            elif 'LeakSanitizer: detected memory leaks' in row['stderr']:
                assert row['case'] in {'substitution-only', 'substitution-then-strip',
                    'substitution-then-absolute', 'empty-substitution-skips',
                    'substitution-no-match', 'bad-backreference-skips'}
                assert not row['passed'] and 'regcomp' in row['stderr']
                issues[key] = 'unchanged regex-cleanup leak'
        assert len(issues) == 28
        for row in r['results']:
            if (row['case'], row['fault'], row['quiet']) in issues:
                continue
            assert 'error' not in row
            if name == 'candidate' or row['passed']:
                assert row['passed']
            else:
                actual = row['actual']
                assert actual['exit_code'] == 0 and actual['returned'] == 1
                assert actual['result'] == 0 and actual['path'] is None
                assert actual['failed_duplicates'] == 1 and actual['null_copies'] == 1
                assert 'Sanitizer' not in row['stderr']
        known[name] = issues
        summary['reports'][name] = {k: r[k] for k in ('source_blob', 'cases', 'passed', 'failed', 'caller_compiler')}
        summary['reports'][name]['dependency_findings'] = [
            dict(case=k[0], fault=k[1], quiet=k[2], kind=v) for k, v in issues.items()]
    assert known['candidate'] == known['baseline']
    summary['matching_dependency_findings'] = 28
    summary['additional_original_functional_failures'] = 44
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({k: v for k, v in summary.items() if k != 'reports'}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
