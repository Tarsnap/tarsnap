#!/usr/bin/env python3
"""Verify exact pre-fix failure and each independent PR830 cleanup requirement."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import subprocess
from run import blob

FIXED = "55f3e58200d435ee5b135740bc4fd83e06b10a36"
BASELINE = "e6249385097d40b149b6986b9ff8d6f54982cfb7"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[3])
    args = parser.parse_args()
    root, output = args.root.resolve(), args.output.resolve()
    source_path = root / "tar/ccache/ccache_write.c"
    assert blob(source_path) == FIXED, "unexpected candidate source"
    assert blob(args.baseline) == BASELINE, "unexpected pre-fix source"
    output.mkdir(parents=True, exist_ok=True)
    source = source_path.read_text()
    route = 'if (asprintf(&s, "%s/cache", path) == -1) {\n\t\twarnp("asprintf");\n\t\tgoto err0;'
    init = '\t/* We have no last-path buffer yet. */\n\tW.sbuf = NULL;\n\tW.sbuflen = 0;\n\n'
    error_free = 'err2:\n\tfree(W.sbuf);'
    reset = '\tfree(W.sbuf);\n\tW.sbuf = NULL;'
    assert all(source.count(text) == 1 for text in (route, init, error_free, reset))
    variants = {
        "baseline": args.baseline.read_text(),
        "no-error-free": source.replace(error_free, 'err2:'),
        "no-reset": source.replace(reset, '\tfree(W.sbuf);'),
        "remove-old-route": source.replace(route, route.replace('goto err0;', 'goto err1;')),
        "late-init": source.replace(init, '').replace('\t/* Write the records and suffixes. */\n',
                         '\t/* Write the records and suffixes. */\n\tW.sbuf = NULL;\n\tW.sbuflen = 0;\n'),
    }
    expected = {"baseline": (25, 24, 1), "no-error-free": (24, 24, 0),
                "no-reset": (36, 0, 36), "remove-old-route": (1, 0, 1), "late-init": (6, 0, 6)}
    summaries = []
    runner = Path(__file__).with_name("run.py")
    for name, text in variants.items():
        target = output / (name + '.c')
        target.write_text(text)
        report_path = output / (name + '.json')
        command = ['python3', str(runner), '--root', str(root), '--source', str(target), '--output', str(report_path)]
        if name == 'late-init':
            command += ['--cc', 'clang', '--auto-init-pattern']
        result = subprocess.run(command, capture_output=True, text=True, timeout=120)
        (output / (name + '.log')).write_text(result.stdout + result.stderr)
        assert result.returncode == 1, (name, result.returncode, result.stdout, result.stderr)
        report = json.loads(report_path.read_text())
        assert report['cases'] == 113 and report['source_blob'] == blob(target)
        failed = [row for row in report['results'] if not row['passed']]
        actual = (len(failed), sum(row['live_buffers'] for row in failed),
                  sum(row['invalid_frees'] for row in failed))
        assert actual == expected[name], (name, actual, expected[name])
        assert all(row['exit_code'] == 1 and row['result'] == row['expected_result'] and
                   row['open_files'] == 0 and row['content_ok'] and not row['stderr'] for row in failed)
        summaries.append({'variant': name, 'source_blob': blob(target), 'failures': actual[0],
                          'leaked_buffers': actual[1], 'invalid_free_attempts': actual[2]})
    (output / 'controls-summary.json').write_text(json.dumps(summaries, indent=2) + '\n')
    print(json.dumps(summaries))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
