# PR836 Padme scalar regression

This validates the existing one-cast fix in Tarsnap/tarsnap#836 and report #835.
C/Claude retains discovery and implementation credit. ASTRA-RELAY-COOLDOWN
(ChatGPT, an LLM working for the operator) added this test. No new bounty report,
award, acceptance or payment is asserted. Review follow-through stays with the
existing PR; this receipt does not promise unattended monitoring.

## Execution, not a giant-buffer simulation

The fixture includes the complete production `tar/chunks/chunks_write.c` with
its real project headers and calls its actual static `padme()` helper. Unused
chunk-storage functions are discarded by the linker. The helper takes scalar
lengths, so its arithmetic can be tested without constructing or allocating a
137 GB chunk buffer. No production function is copied, extracted or rewritten.
There are no storage or cryptographic mocks in this fixture.

The Python oracle uses unbounded-integer bit lengths and integer division to
round to the required quantum, then applies the documented maximum clamp. It
does not copy the C shift loops or use floating-point logarithms.

The deterministic 64-bit matrix has 16,266 unique (length, maximum) vectors:
small values 1..4096, adjacent power/rounding/clamp boundaries, 2,048 seeded large
pairs, and the largest tested bounds. It uses seed 8352026. The maximum is the
scalar compression-buffer bound calculated from the largest maxchunksize accepted
by `chunks_write_start`; it is not a claim such a buffer was allocated. Inputs
stay within that bound so unrelated SIZE_MAX addition overflow is not introduced.

**The shipped client's MAXCHUNK remains below these undefined-shift thresholds.**
This is evidence about the scalar helper's type-width contract, not a practical
large-chunk client run, buffer-overrun claim, server attack, or severity escalation.

## Actual local results, 2026-09-08 UTC

Source commit: `ffd9d38fb36b0f430bf0d068111ddf5398ee0057`.
Fixed source blob: `2bf28840ec0a7283c80296c47143f7a132b37499`.
Exact base source blob: `503f772b1a4095a7936efb9d94f95beede1b883c`.

The fixed source matches all 16,266 oracle values with GCC 14.2, Clang 17 and
Clang 17 UndefinedBehaviorSanitizer (nonrecovering). The exact original source
matches 13,964 values with GCC and differs on 2,302. That unsanitized behavior is
compiler-dependent; undefined behavior must not be specified as a stable result.

Separate original-source UBSan probes reproduce both reported boundaries:
- Length 137438953472 (2^37): left shift of 1 by 31 cannot be represented in int.
- Length 274877906944 (2^38): shift exponent 32 is too large for 32-bit int.

Those failing executions are expected controls, not successful baseline tests.
Native size_t is 64 bits and int is 32 bits; 32-bit targets are not claimed tested.
Matrix input SHA-256: `b7e6dd234f58a5a2caa547d84cf0bc4e54977892fae6dedae83eda8e50d52331`.

## Replay

Requires a configured Linux checkout, Python 3.9+, GCC/Clang and a GNU-compatible
linker. Use the project's normal dependencies, including ext2fs headers on Linux.

```sh
autoreconf -i
./configure
python3 tests/regressions/pr836/run.py --output /tmp/pr836-fixed.json
python3 tests/regressions/pr836/run.py --cc clang --sanitize --output /tmp/pr836-ubsan.json
git show 0bf0b299fed91139044c81cc6fcdc5521c34d235:tar/chunks/chunks_write.c > /tmp/pr836-base.c
# Expected exit 1 and the corresponding UBSan shift diagnostic:
python3 tests/regressions/pr836/run.py --source /tmp/pr836-base.c --cc clang --sanitize --probe 137438953472
python3 tests/regressions/pr836/run.py --source /tmp/pr836-base.c --cc clang --sanitize --probe 274877906944
```

Candidate success exits 0; a mismatch or sanitizer runtime failure exits 1;
configuration/compilation failure exits 2. JSON records source hash, native widths,
input digest, matched count, process status and diagnostics. Only the first 20
mismatches are rendered; the matched total counts the complete output.

This additive test does not change the original production patch, account state,
sponsor contact or claim. Any hosted results and full-build evidence are recorded
separately; local scalar tests alone do not certify upstream-required CI or a
complete project build. The support workflow is kept outside this PR.
