#!/usr/bin/env python3
"""Run FLINT's source-bound PR822 regression without a client/archive/server.

C/Claude owns report821 and the production fix; FLINT owns the initial actual-
glibc reproduction and boundary matrix. QUAY packages and executes this runner.
A missing locale/converter prerequisite exits77 and is never reported as PASS.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile

BASE_BLOB = "e16564560086441ccbc7f7beade122b1ac115bc2"
FIXED_BLOB = "16bf2f878d1eda209d41054060e75b6d16b05adf"
OLD = "\t\t/* If our output buffer is full, dump it and keep going. */\n\t\tif (i > (sizeof(outbuff) - 20)) {"
NEW = """\t\t/*
\t\t * If the buffer might not have room for the worst-case
\t\t * expansion of the next character -- bsdtar_expand_char()
\t\t * can emit four bytes per input byte, and sprintf() adds a
\t\t * terminating '\\0' after the last of them -- dump it
\t\t * and keep going.
\t\t */
\t\tif (i > (sizeof(outbuff) - (4 * (size_t)MB_CUR_MAX + 1))) {"""
HEADERS = """#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <locale.h>
#include <gnu/libc-version.h>
static size_t bsdtar_expand_char(char *, size_t, char);
"""
PREFIXES = (0, 230, 231, 232, 233, 234, 235, 236, 237, 255, 256, 512)
SEQUENCES = {4: bytes.fromhex("f4 90 80 80"),
             5: bytes.fromhex("f8 88 80 80 80"),
             6: bytes.fromhex("fc 84 80 80 80 80")}
# These six original failures are FLINT's existing matrix, not newly inferred.
ORIGINAL_FAILURES = {(236, 5)} | {(prefix, 6) for prefix in range(232, 237)}
CONTROL_OUTPUTS = (b"", b"ordinary ASCII", b"\\\\", b"\\a\\b\\f\\n\\r\\t\\v",
                   "é".encode(), "€".encode(), b"\\377\\303\\251", b"A\\n\\\\\\tZ")
ASAN = "detect_leaks=0:halt_on_error=1:abort_on_error=0:exitcode=97"


def blob(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def cores(source: Path, baseline: Path | None) -> tuple[str, str, str]:
    fixed = source.read_bytes()
    require(blob(fixed) == FIXED_BLOB, "candidate source blob differs from PR822")
    text = fixed.decode("utf-8")
    require(text.count(NEW) == 1, "unique production fix hunk not found")
    base_text = text.replace(NEW, OLD)
    require(blob(base_text.encode()) == BASE_BLOB, "reconstructed baseline blob mismatch")
    if baseline is not None:
        require(baseline.read_bytes() == base_text.encode(), "supplied baseline differs")
    start = "void\nsafe_fprintf("
    stop = "\nstatic void\nbsdtar_vwarnc("
    require(text.count(start) == 1 and text.count(stop) == 1, "ambiguous source extraction")
    require(text.index(start) < text.index(stop), "invalid extraction order")
    begin = text.index(start)
    notice = text[:text.index('#include "bsdtar_platform.h"')]
    fixed_core = text[begin:text.index(stop)]
    base_core = base_text[base_text.index(start):base_text.index(stop)]
    require(fixed_core.count("\nbsdtar_expand_char(") == 1, "expander body not found")
    return notice, base_core, fixed_core


def execute(command: list[str], env: dict[str, str], out: Path, label: str,
            timeout: int = 10) -> subprocess.CompletedProcess:
    result = subprocess.run(command, capture_output=True, env=env, timeout=timeout, check=False)
    (out / (label + ".stdout")).write_bytes(result.stdout)
    (out / (label + ".stderr")).write_bytes(result.stderr)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path,
                        default=Path(__file__).resolve().parents[3] / "tar/util.c")
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--cc", default="cc", help="one compiler executable, not a shell command")
    parser.add_argument("--opt", choices=("0", "1", "2"), default="1")
    parser.add_argument("--output", type=Path, help="new evidence directory; must not exist")
    args = parser.parse_args()
    out = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix="pr822-asan-"))
    if args.output:
        out.mkdir(parents=True, exist_ok=False)
    summary = {"status": "FAIL", "source_blob": FIXED_BLOB, "baseline_blob": BASE_BLOB,
               "platform": platform.platform(), "asan_options": ASAN, "cases": []}
    env = dict(os.environ, ASAN_OPTIONS=ASAN)
    try:
        notice, original, fixed = cores(args.source, args.baseline)
        compiler = shutil.which(args.cc)
        require(compiler is not None, "compiler not found: " + args.cc)
        version = execute([compiler, "--version"], env, out, "compiler", 30)
        require(version.returncode == 0, "compiler version query failed")
        summary["compiler"] = version.stdout.decode(errors="replace").splitlines()[0]
        summary["source_sha256"] = hashlib.sha256(args.source.read_bytes()).hexdigest()
        driver = Path(__file__).with_name("driver.c").read_text(encoding="utf-8")
        summary["driver_sha256"] = hashlib.sha256(driver.encode()).hexdigest()
        for variant, core in (("original", original), ("fixed", fixed)):
            cfile = out / (variant + ".c")
            binary = out / variant
            cfile.write_text(notice + HEADERS + core + driver, encoding="utf-8")
            command = [compiler, "-Wall", "-Wextra", "-Werror", "-std=c99", "-g",
                       "-O" + args.opt, "-fno-omit-frame-pointer", "-fsanitize=address",
                       "-fno-pie", "-no-pie", str(cfile), "-o", str(binary)]
            summary.setdefault("build_commands", []).append(command)
            build = execute(command, env, out, variant + "-build", 60)
            require(build.returncode == 0, variant + " compilation failed")
            probe = execute([str(binary), "probe"], env, out, variant + "-probe")
            summary[variant + "_probe"] = probe.stderr.decode(errors="replace")
            if probe.returncode == 77:
                summary["status"] = "SKIP_PREREQUISITE"
                return 77
            require(probe.returncode == 0, variant + " locale/sanitizer probe failed")
            for prefix in PREFIXES:
                for width, sequence in SEQUENCES.items():
                    label = f"{variant}-p{prefix}-w{width}"
                    result = execute([str(binary), str(prefix), str(width)], env, out, label)
                    overflow = variant == "original" and (prefix, width) in ORIGINAL_FAILURES
                    expected = b"A" * prefix + b"".join(f"\\{byte:03o}".encode() for byte in sequence)
                    matched = (result.returncode == 97 and
                               b"AddressSanitizer: stack-buffer-overflow" in result.stderr) if overflow else (
                                   result.returncode == 0 and result.stdout == expected and
                                   b"ERROR: AddressSanitizer" not in result.stderr)
                    summary["cases"].append({"label": label, "exit": result.returncode,
                                             "expected": "ASAN_OVERFLOW" if overflow else "EXACT_OUTPUT",
                                             "matched": matched, "stdout_bytes": len(result.stdout)})
            for index, expected in enumerate(CONTROL_OUTPUTS):
                label = f"{variant}-control{index}"
                result = execute([str(binary), "control", str(index)], env, out, label)
                matched = result.returncode == 0 and result.stdout == expected
                summary["cases"].append({"label": label, "exit": result.returncode,
                                         "expected": "EXACT_OUTPUT", "matched": matched,
                                         "stdout_bytes": len(result.stdout)})
        require(len(summary["cases"]) == 88, "incomplete matrix")
        failures = [case["label"] for case in summary["cases"] if not case["matched"]]
        require(not failures, "unexpected outcomes: " + ", ".join(failures))
        summary["status"] = "PASS"
        summary["matched"] = len(summary["cases"])
        summary["expected_original_overflows"] = len(ORIGINAL_FAILURES)
        print("PASS: 72 boundary expectations + 16 output controls; evidence:", out)
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        summary["error"] = str(exc)
        print("FAIL:", exc, "Evidence:", out, file=sys.stderr)
        return 1
    finally:
        (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())
