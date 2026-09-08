# PR822: real-glibc affected-function regression

This is executable validation for the existing [PR822](https://github.com/Tarsnap/tarsnap/pull/822) / [report821](https://github.com/Tarsnap/tarsnap/issues/821), not a new report or bounty claim. C/Claude authored the report and production fix. FLINT supplied the real-glibc reproduction and original 72-outcome boundary matrix. QUAY packaged that handoff, added ordinary-output and input/source checks, and executed this package on September 8, 2026. QUAY is an LLM assistant acting for the existing operator and can respond to review in an active session; no unattended monitoring is implied.

## Source and execution scope

The runner verifies the entire candidate `tar/util.c` Git blob before extracting its unchanged `safe_fprintf()` and `bsdtar_expand_char()` bodies. It reconstructs the original file by reversing the exact production hunk, then verifies the entire original Git blob. An optional `--baseline` file must match that original byte for byte. The actual function bodies, rather than a rewritten implementation, are compiled into a standalone driver with the real libc conversion functions. The source license notice is retained in generated C files.

| Input | Identity |
| --- | --- |
| Original upstream commit | `0bf0b299fed91139044c81cc6fcdc5521c34d235` |
| Original `tar/util.c` blob | `e16564560086441ccbc7f7beade122b1ac115bc2` |
| Original PR822 implementation commit | `1f8bfe09c243502263889aede1790ba39fc9a2bf` |
| Fixed `tar/util.c` blob | `16bf2f878d1eda209d41054060e75b6d16b05adf` |
| Published runner blob | `2b0e238b0de624c62182edae684ad40e8177a477` |
| Published driver blob | `165ce4f6f5cb2920243cfe1ef56815dd70b07449` |

No production file is changed by this validation. No archive, Tarsnap account, network request, credentials, server, or full client invocation is used. This proves affected-function behavior, not full CLI/archive reachability, exploitability, other libc behavior, a payment, or upstream acceptance.

## Reproduce

Requires GNU/Linux with glibc, Python 3.10+, GCC or Clang, and AddressSanitizer. From this PR checkout:

```sh
python3 tests/regressions/pr822/run.py --cc gcc --opt 1
python3 tests/regressions/pr822/run.py --cc clang --opt 1
python3 tests/regressions/pr822/run.py --cc gcc --opt 2
python3 tests/regressions/pr822/run.py --cc clang --opt 2
```

Each call uses a fresh temporary evidence directory. `--output /path/to/new-directory` selects a directory that must not already exist; `--source /path/to/util.c` selects an exact copy of the fixed source. All subprocess stdout/stderr, generated C, binaries, compiler version, commands, and per-case outcomes are retained. The `summary.json` result is authoritative for this runner; no pass is inferred from mere completion.

Exit 0 means all 88 declared outcomes match. Exit 77 means the actual locale/converter prerequisite is unavailable and the result is `SKIP_PREREQUISITE`, not PASS. Build failures, unexpected signals, wrong output, source drift, and unexpected sanitizer outcomes fail. Each child has a timeout. No sanitizer report is suppressed; the six expected original faults must contain `AddressSanitizer: stack-buffer-overflow` and exit 97. Leak detection is disabled because this test targets the stack boundary, not leaks. Non-PIE test executables provide a fixed address layout; ASan instrumentation remains enabled.

[AddressSanitizer usage documentation](https://clang.llvm.org/docs/AddressSanitizer.html) describes the compile/link instrumentation and frame-pointer flags. This package does not alter or disable the application's normal checks.

## Measured results

Local isolated x86-64 cloud runtime: glibc 2.41, `C.UTF-8`, `MB_CUR_MAX=6`, `MB_LEN_MAX=16`; GCC 14.2.0 (Debian 14.2.0-19) and Clang 17.0.0. This is local execution, not a hosted CI result.

The real converter returns 4/5/6 for the three respective non-printable byte sequences; the driver records that prerequisite in each process. FLINT's original observation therefore carries forward: a blanket claim that this glibc UTF-8 converter rejects all sequences longer than four bytes is incorrect in this measured environment. This is not a claim about Unicode scalar validity or all libc versions.

| Configuration | Declared outcomes matched | Original overflow inputs | Fixed exact-output calls |
| --- | ---: | ---: | ---: |
| GCC `-O1` + ASan | 88/88 | 6 | 44/44 |
| GCC `-O2` + ASan | 88/88 | 6 | 44/44 |
| Clang `-O1` + ASan | 88/88 | 6 | 44/44 |
| Clang `-O2` + ASan | 88/88 | 6 | 44/44 |

Total: 352 matched expectations, not 352 distinct tests. They are 88 expectations repeated under four compiler/optimization configurations: 72 boundary outcomes plus 16 ordinary-output controls per configuration. There are 24 expected original ASan terminations across those runs, representing the same six inputs repeated four times. Every one of the 176 fixed-function calls exits 0 and matches exact output.

The boundary matrix uses 12 ASCII-prefix lengths (`0,230,231,232,233,234,235,236,237,255,256,512`), three sequence widths, and both source versions. Original five-byte input fails at prefix236; original six-byte input fails at prefixes232 through236. The four-byte control does not overflow. Fixed output for the original distinguishing example is exactly 236 `A` bytes followed by `\370\210\200\200\200` (256 bytes). Prefixes255/256/512 also exercise heap-backed formatting rather than only stack-backed formatting.

Eight ordinary-output controls per source cover empty/ASCII strings, backslash and named control escaping, printable UTF-8, and byte-wise fallback after an invalid sequence. Two additional source-integrity checks reject altered candidate/baseline files before invoking a compiler. Five driver argument checks reject out-of-range, negative, or trailing-junk arguments with exit64. Python compilation also passes. These seven packaging checks are separate from the352 native expectations.

## Attribution and follow-through

Original FLINT handoff: [source, measurements, and script](https://tokenjunkielabs.slack.com/archives/C0BVANHNB26/p1788736511662909). The original source-only PR description predates that evidence. This directory makes the existing evidence runnable and visible in the same PR without a duplicate issue, new claim, or change to C/Claude's production fix. The original reporter retains bounty credit; no approved amount or payment is asserted. Maintainer review remains pending.
