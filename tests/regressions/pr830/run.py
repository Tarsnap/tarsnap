#!/usr/bin/env python3
"""Compile the complete PR830 writer and exercise only disposable local caches."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def blob(path: Path) -> str:
    data = path.read_bytes()
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def scenarios() -> list[tuple[str, int, int]]:
    rows = [("empty", 0, 0), ("skipped", 6, 0), ("mixed", 6, 0)]
    for count in (1, 3, 6):
        rows += [(name, count, 0) for name in (
            "success", "fopen", "count", "record-before", "data-before",
            "fsync", "fclose", "unlink", "rename", "dirsync")]
        rows += [("asprintf", count, i) for i in (1, 2)]
        rows += [(name, count, i) for name in ("record-after", "data-after")
                 for i in range(1, count + 1)]
        rows += [("fwrite", count, i) for i in range(1, 4 * count + 2)]
        rows += [("malloc", count, i) for i in range(1, {1: 1, 3: 2, 6: 4}[count] + 1)]
    rows += [(name, 0, 0) for name in (
        "remove-existing", "remove-missing", "remove-asprintf", "remove-unlink")]
    assert len(rows) == len(set(rows))
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--source", type=Path)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--auto-init-pattern", action="store_true", help="Compiler-initialize automatic storage for the late-init mutation control")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    source = (args.source or root / "tar/ccache/ccache_write.c").resolve()
    fixture = Path(__file__).with_name("cache_write.c").resolve()
    if not (root / "config.h").is_file():
        parser.error("configure the source checkout first (autoreconf -i && ./configure)")
    flags = ["-std=c99", "-D_GNU_SOURCE", "-DHAVE_CONFIG_H", "-O1", "-g",
             "-Wall", "-Wextra", "-fno-omit-frame-pointer", "-I" + str(root)]
    include_dirs = sorted({path.parent for folder in ("tar", "lib", "lib-platform", "libcperciva", "libarchive")
                           for path in (root / folder).rglob("*.h")})
    flags += ["-I" + str(path) for path in include_dirs]
    if args.auto_init_pattern:
        flags += ["-ftrivial-auto-var-init=pattern"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
    report = {"source_blob": blob(source), "fixture_blob": blob(fixture),
              "runner_blob": blob(Path(__file__)), "patricia_blob": blob(root / "lib/datastruct/patricia.c"),
              "compiler": subprocess.check_output([args.cc, "--version"], text=True).splitlines()[0],
              "sanitize": args.sanitize, "auto_init_pattern": args.auto_init_pattern, "commands": [], "results": []}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="pr830-build-") as directory:
        work = Path(directory)
        patricia = work / "patricia.o"
        binary = work / "cache-write"
        # PR862 separately fixes old Patricia signed shifts. Keep its unchanged
        # source and disable ONLY shift checks in this dependency object; retain
        # its address/other UB checks and ALL writer/harness sanitizers.
        dep_flags = flags + (["-fno-sanitize=shift"] if args.sanitize else [])
        commands = [
            [args.cc, *dep_flags, "-c", str(root / "lib/datastruct/patricia.c"), "-o", str(patricia)],
            [args.cc, *flags, '-DCCACHE_SOURCE="' + str(source) + '"', str(fixture), str(patricia), "-o", str(binary)],
        ]
        for command in commands:
            result = subprocess.run(command, capture_output=True, text=True, timeout=90)
            report["commands"].append({"argv": command, "exit_code": result.returncode,
                                       "stdout": result.stdout, "stderr": result.stderr})
            if result.returncode:
                report["status"] = "COMPILE_ERROR"
                args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
                print(result.stderr)
                return 2
        env = {**os.environ, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
               "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
        for name, count, parameter in scenarios():
            try:
                result = subprocess.run([str(binary), name, str(count), str(parameter)],
                                        text=True, capture_output=True, timeout=10, env=env)
                row = json.loads(result.stdout)
                row.update(exit_code=result.returncode, stderr=result.stderr)
                row["passed"] = bool(row.get("passed")) and result.returncode == 0 and not result.stderr
            except (ValueError, subprocess.TimeoutExpired) as exc:
                row = {"scenario": name, "records": count, "parameter": parameter, "passed": False,
                       "error": type(exc).__name__}
                if isinstance(exc, ValueError):
                    row.update(exit_code=result.returncode, stdout=result.stdout, stderr=result.stderr)
            report["results"].append(row)
    report["cases"] = len(report["results"])
    report["passed"] = sum(row["passed"] for row in report["results"])
    report["failed"] = report["cases"] - report["passed"]
    report["status"] = "PASS" if not report["failed"] else "FAIL"
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("status", "cases", "passed", "failed", "source_blob", "compiler")}))
    return 0 if not report["failed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
