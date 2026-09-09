#!/usr/bin/env python3
"""Source-pinned PR820 parser and optional real offline CLI regression.

C/Claude owns report819 and the parser fix. QUAY adds executable validation.
Only generated test files are written. Native CLI probes end at --version or
--verify-config with default configuration disabled, before key/network work.
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

BASE_BLOB = "cb79db9ed8ad2ce7b5d74197cf43d2a49b6ad88d"
FIXED_BLOB = "dc6c24574a73cd8209625bea5b926c3c6cb8a9f1"
START = "\t\tcase OPTION_STRIP_COMPONENTS: /* GNU tar 1.15 */\n"
STOP = "\t\tcase 'T': /* GNU tar */"
BASE_ARM = '''\t\tcase OPTION_STRIP_COMPONENTS: /* GNU tar 1.15 */
\t\t\terrno = 0;
\t\t\tbsdtar->strip_components = strtol(bsdtar->optarg,
\t\t\t    NULL, 0);
\t\t\tif (errno)
\t\t\t\tbsdtar_errc(bsdtar, 1, 0,
\t\t\t\t    "Invalid --strip-components argument: %s",
\t\t\t\t    bsdtar->optarg);
\t\t\tbreak;
'''
# Original values below are the measured GNU/Linux LP64 behavior, not portable
# promises about narrowing a long into int on other C implementations.
# Each tuple is (input, original accepted value, fixed accepted value).
# None means the parser must reject with its existing diagnostic and exit1.
CASES = (
    ("0", 0, 0), ("1", 1, 1), ("2", 2, 2), ("10", 10, 10),
    ("010", 8, 10), ("08", 0, 8), ("00010", 8, 10),
    ("0x10", 16, None), ("0X10", 16, None),
    ("abc", 0, None), ("", 0, None), ("2x", 2, None),
    ("2 ", 2, None), (" 2", 2, 2), ("\t3", 3, 3),
    (" ", 0, None), ("+", 0, None), ("-", 0, None),
    ("+10", 10, 10), ("-1", -1, None), ("-0", 0, 0),
    ("2147483647", 2147483647, 2147483647),
    ("2147483648", -2147483648, None), ("4294967296", 0, None),
    ("4294967297", 1, None), ("9223372036854775807", -1, None),
    ("9223372036854775808", None, None),
    ("-9223372036854775808", 0, None),
    ("-9223372036854775809", None, None),
    ("1e3", 1, None), ("00", 0, 0), ("09", 0, 9),
    ("123456", 123456, 123456), ("\n4", 4, 4), ("0\n", 0, None),
)


def blob(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def load_source(path: Path, baseline: Path | None) -> tuple[str, str, str]:
    data = path.read_bytes()
    require(blob(data) == FIXED_BLOB, "fixed bsdtar.c blob differs from PR820")
    text = data.decode()
    require(text.count(START) == 1 and text.count(STOP) == 1, "ambiguous switch arm")
    begin, end = text.index(START), text.index(STOP)
    require(begin < end, "wrong source boundary order")
    original = text[:begin] + BASE_ARM + text[end:]
    require(blob(original.encode()) == BASE_BLOB, "baseline reconstruction mismatch")
    if baseline is not None:
        require(baseline.read_bytes() == original.encode(), "supplied baseline mismatch")
    # Preserve the original copyright/license notice in generated C sources.
    notice = text[:text.index('#include "bsdtar_platform.h"')]
    return notice, BASE_ARM, text[begin:end]


def invoke(command: list[str], out: Path, label: str,
           timeout: int = 10) -> subprocess.CompletedProcess:
    env = dict(os.environ, LC_ALL="C")
    result = subprocess.run(command, env=env, cwd=out, capture_output=True,
                            timeout=timeout, check=False)
    (out / (label + ".stdout")).write_bytes(result.stdout)
    (out / (label + ".stderr")).write_bytes(result.stderr)
    return result


def matches(result: subprocess.CompletedProcess, value: int | None,
            argument: str, cli: bool = False) -> bool:
    if value is None:
        return (result.returncode == 1 and not result.stdout and
                ("Invalid --strip-components argument: " + argument).encode()
                in result.stderr)
    if cli:
        return (result.returncode == 0 and result.stdout.startswith(b"tarsnap ")
                and result.stdout.endswith(b"\n") and not result.stderr)
    return (result.returncode == 0 and result.stdout == f"{value}\n".encode()
            and not result.stderr)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path,
                        default=Path(__file__).resolve().parents[3] / "tar/bsdtar.c")
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--opt", choices=("0", "1", "2"), default="2")
    parser.add_argument("--baseline-cli", type=Path)
    parser.add_argument("--candidate-cli", type=Path)
    parser.add_argument("--output", type=Path, help="new evidence directory")
    args = parser.parse_args()
    if bool(args.baseline_cli) != bool(args.candidate_cli):
        parser.error("supply both native CLI binaries, or neither")
    out = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix="pr820-"))
    if args.output:
        out.mkdir(parents=True, exist_ok=False)
    summary = {"status": "FAIL", "platform": platform.platform(),
               "source_blob": FIXED_BLOB, "baseline_blob": BASE_BLOB,
               "native_cli": bool(args.baseline_cli), "cases": []}
    try:
        notice, base_arm, fixed_arm = load_source(args.source, args.baseline)
        template = Path(__file__).with_name("driver.c").read_text()
        require(template.count("/* PR820_CASE */") == 1, "driver insertion marker invalid")
        summary["driver_sha256"] = hashlib.sha256(template.encode()).hexdigest()
        compiler = shutil.which(args.cc)
        require(compiler is not None, "compiler unavailable")
        version = invoke([compiler, "--version"], out, "compiler", 30)
        require(version.returncode == 0, "compiler version query failed")
        summary["compiler"] = version.stdout.decode(errors="replace").splitlines()[0]
        for variant, arm in (("original", base_arm), ("fixed", fixed_arm)):
            cfile, binary = out / (variant + ".c"), out / variant
            cfile.write_text(notice + template.replace("/* PR820_CASE */", arm))
            command = [compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                       "-O" + args.opt, "-fsanitize=undefined",
                       "-fno-sanitize-recover=all", str(cfile), "-o", str(binary)]
            summary.setdefault("build_commands", []).append(command)
            build = invoke(command, out, variant + "-build", 60)
            require(build.returncode == 0, variant + " parser compilation failed")
            probe = invoke([str(binary), "--probe"], out, variant + "-probe")
            summary[variant + "_abi"] = probe.stdout.decode(errors="replace")
            if probe.returncode == 77:
                summary["status"] = "UNSUPPORTED_ABI"
                return 77
            require(probe.returncode == 0, "parser ABI probe failed")
            cli = args.baseline_cli if variant == "original" else args.candidate_cli
            if cli:
                cli = cli.resolve()
                summary[variant + "_cli_sha256"] = hashlib.sha256(cli.read_bytes()).hexdigest()
            for index, (argument, before, after) in enumerate(CASES):
                value = before if variant == "original" else after
                result = invoke([str(binary), argument], out, f"{variant}-parser-{index}")
                summary["cases"].append({"variant": variant, "kind": "parser",
                    "input": argument, "expected_value": value, "exit": result.returncode,
                    "matched": matches(result, value, argument)})
                if cli:
                    # version() exits during option parsing. No archive command,
                    # config, key, provider account, or server is touched.
                    command = [str(cli), "--no-default-config",
                               "--strip-components=" + argument, "--version"]
                    result = invoke(command, out, f"{variant}-cli-{index}")
                    summary["cases"].append({"variant": variant, "kind": "native_cli",
                        "input": argument, "expected_exit": 1 if value is None else 0,
                        "exit": result.returncode, "matched": matches(result, value, argument, True)})
            if cli:
                # --verify-config exits before key loading. This also checks that
                # erroneous original zero results bypassed the existing mode gate.
                for index, (argument, before, after) in enumerate(CASES):
                    if argument not in ("0", "1", "abc", "2x", "4294967296", "010"):
                        continue
                    value = before if variant == "original" else after
                    result = invoke([str(cli), "--no-default-config", "--verify-config",
                                     "--strip-components=" + argument], out,
                                    f"{variant}-mode-{index}")
                    if value is None:
                        matched = matches(result, value, argument)
                        outcome = "PARSE_ERROR"
                    elif value == 0:
                        matched = result.returncode == 0 and not result.stdout and not result.stderr
                        outcome = "CONFIG_VALID"
                    else:
                        matched = (result.returncode == 1 and not result.stdout and
                            b"Option --strip-components is not permitted in mode --verify-config"
                            in result.stderr)
                        outcome = "MODE_ERROR"
                    summary["cases"].append({"variant": variant, "kind": "native_mode_gate",
                        "input": argument, "expected": outcome, "exit": result.returncode,
                        "matched": matched})
        failures = [case for case in summary["cases"] if not case["matched"]]
        require(not failures, "unexpected outcomes: " + repr(failures))
        expected = 2 * len(CASES) * (2 if args.baseline_cli else 1) + (12 if args.baseline_cli else 0)
        require(len(summary["cases"]) == expected, "incomplete execution")
        summary.update(status="PASS", matched=expected, input_count=len(CASES))
        print(f"PASS: {expected} matched outcomes ({len(CASES)} inputs); evidence: {out}")
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        summary["error"] = str(exc)
        print("FAIL:", exc, "Evidence:", out, file=sys.stderr)
        return 1
    finally:
        (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
