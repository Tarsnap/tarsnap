# PR820: compiled strip-components parser and native CLI checks

This directory supplies executable validation for existing [PR820](https://github.com/Tarsnap/tarsnap/pull/820) and [report819](https://github.com/Tarsnap/tarsnap/issues/819). C/Claude owns the report and production fix. QUAY, an LLM assistant for the same operator, added and executed these regressions on September 8, 2026 and can respond to review during an active session. No new report, bounty claim, account operation, or payment is asserted.

## What is tested

`run.py` first verifies the entire fixed `tar/bsdtar.c` blob. It extracts the complete, unchanged `OPTION_STRIP_COMPONENTS` switch arm, reconstructs the original entire source by reversing that arm, and verifies the baseline blob. The optional `--baseline` argument must match it exactly. Both arms are compiled with the real libc `strtol`; test plumbing supplies the small state structure, diagnostic sink and accepted-value output. Generated C retains the source copyright/license notice. This is actual parser-arm execution, not a whole-main claim.

An optional **separate native-client path** invokes two real, normally built Tarsnap binaries. Seventy cases parse each input before `--version`, which exits during command-line processing; twelve more cases use `--verify-config` to exercise the existing mode gate. Every native call includes `--no-default-config`. No key, archive, account, or server access occurs. The version checks establish acceptance/rejection and diagnostic behavior; accepted integer values come from the direct-arm checks, not from the version banner.

| Source | Git blob |
| --- | --- |
| Original `tar/bsdtar.c`, master `0bf0b299fed91139044c81cc6fcdc5521c34d235` | `cb79db9ed8ad2ce7b5d74197cf43d2a49b6ad88d` |
| Fixed `tar/bsdtar.c`, original PR head `2e17c8678a9411a6ae247304f7f65f38bf5e4fa9` | `dc6c24574a73cd8209625bea5b926c3c6cb8a9f1` |
| `run.py` | `cfb0331fa10be320f6d01d5534bcf472e116e63f` |
| `driver.c` | `04421262b90ae5296a37add76be38a451d39dfd4` |

The production fix is unchanged. Only this additive regression directory is delivered.

## Reproduce the parser matrix

From the existing PR checkout, with Python 3.10+ and a GCC/Clang compiler supporting UndefinedBehaviorSanitizer:

```sh
python3 tests/regressions/pr820/run.py --cc gcc --opt 2
python3 tests/regressions/pr820/run.py --cc gcc --opt 1
python3 tests/regressions/pr820/run.py --cc clang --opt 2
python3 tests/regressions/pr820/run.py --cc clang --opt 1
```

The driver verifies 32-bit `int` and 64-bit `long`. An unsupported ABI returns77 and is not PASS. Baseline narrowing expectations describe the measured GNU/Linux LP64 implementation, not portable promises about out-of-range long-to-int conversion. The matrix uses a C locale. It preserves valid leading whitespace, plus signs, and negative zero where the production fix accepts them; it does not silently add stricter input policy.

Each run writes a fresh evidence directory, or `--output /path/to/new-directory` chooses one that must not already exist. It retains all child stdout/stderr, generated C, compiler commands, ABI records, binary hashes when supplied, and a per-input `summary.json`. Unexpected diagnostics, exits, sanitizer output, incorrect accepted values, timeouts, and incomplete matrices fail. No failed test is converted to a pass.

## Reproduce the independent native CLI checks

Use two disposable **cloud** source trees, one for each exact implementation ref. Follow the repository's `BUILDING` prerequisites, including OpenSSL/zlib/ext2fs development headers and the autotools/autoconf-archive tools. A checkout containing both refs can export them without additional clones:

```sh
work=$(mktemp -d)
mkdir "$work/baseline" "$work/fixed"
git archive 0bf0b299fed91139044c81cc6fcdc5521c34d235 | tar -x -C "$work/baseline"
git archive 2e17c8678a9411a6ae247304f7f65f38bf5e4fa9 | tar -x -C "$work/fixed"
(cd "$work/baseline" && autoreconf -i && ./configure && make -j2)
(cd "$work/fixed" && autoreconf -i && ./configure && make -j2)
python3 tests/regressions/pr820/run.py --cc gcc --opt 2 \
  --baseline-cli "$work/baseline/tarsnap" \
  --candidate-cli "$work/fixed/tarsnap"
```

The runner does not build or download dependencies itself. Supplying only one CLI binary is an argument error. Native binary SHA-256 values are recorded; the source-to-binary relationship must be retained by the accompanying build logs. Do not substitute an unrelated installed client and label it this source revision.

## Executed results

Isolated x86-64 GNU/Linux cloud runtime, glibc2.41, GCC14.2.0 and Clang17.0.0. Direct-arm builds use `-Wall -Wextra -Werror`, `-O1`/`-O2`, `-fsanitize=undefined`, and `-fno-sanitize-recover=all`.

| Execution | Matched outcomes |
| --- | ---: |
| GCC `-O1`, original/fixed parser | 70/70 |
| GCC `-O2`, original/fixed parser | 70/70 |
| Clang `-O1`, original/fixed parser | 70/70 |
| Clang `-O2`, original/fixed parser | 70/70 |
| Real GCC-built original/fixed clients, version path | 70/70 |
| Real GCC-built original/fixed clients, configuration-mode path | 12/12 |
| **Total declared expectations** | **362/362** |

This is 35 distinct parser inputs repeated across the configurations and native paths, not362 distinct test cases. Twenty-one inputs distinguish old/new behavior; seventeen are newly rejected by the fixed parser. There were no unexpected outcomes or UBSan findings. Two separate integrity checks reject modified fixed/baseline source before a compiler is invoked. Python compilation and trailing-whitespace checks also passed.

Examples observed from the actual parser:

| Input | Original | Fixed |
| --- | --- | --- |
| `010` | accepts8 | accepts10 |
| `08` | accepts0 | accepts8 |
| `0x10` | accepts16 | rejects |
| `abc` | accepts0 | rejects |
| `2x` | accepts2 | rejects |
| `2147483648` | accepts-2147483648 | rejects |
| `4294967296` | accepts0 | rejects |
| `2147483647` | accepts2147483647 | same |

The real `--verify-config` path independently confirms the zero-value consequence: original `abc` and `4294967296` exit0, while the fixed client exits1 with the parsing diagnostic. A valid nonzero value instead retains the existing mode diagnostic in both clients. Ordinary valid zero still succeeds.

## Native build provenance and limits

The complete GCC baseline project build succeeded; replacing only the verified `tar/bsdtar.c` with the fixed blob then rebuilt that object and relinked the candidate. Both build logs are retained. The source was recovered from the existing saved archive; its sole PR830 `ccache_write.c` difference was restored to the verified baseline before building. No peer production fix was accidentally included.

Several local configure attempts were interrupted by execution time limits. Their logs are retained. Reusing completed autoconf cache entries initially carried the interrupted `gid_t` result; removing that incomplete probe allowed it to be measured normally and configuration/build to finish. The local environment also reported missing optional `AX_CFLAGS_WARN_ALL`; this warning was not hidden. Normal native compiler flags were `-g -O2`. This does not claim a warning-clean or fully configured upstream CI environment. The extracted-arm builds independently enforce the explicit warning flags above.

These are local executions, not hosted CI. There is no archive extraction, full application test-suite, 32-bit platform, other-libc, hosted-service, or payment result. Original report/fix authorship stays with C/Claude; upstream review and any award decision remain separate.
