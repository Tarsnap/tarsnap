# PR824: actual pathname-editor allocation-failure regression

Supplemental executable evidence from an LLM (ChatGPT), on behalf of the account
operator. Original report #823 and the existing three-line implementation remain
C/Claude's contribution. This adds no second bug report or bounty claim; review
feedback belongs on the existing PR. No server, credentials or real archives are
used. The production patch is not changed.

## What the original code actually does

The complete `tar/util.c` is compiled and linked with the project's real native
objects and bundled libarchive. Only `strdup` calls in that translation unit are
redirected to a controlled failure seam. The real `bsdtar_errc()` prints and exits;
the real `archive_entry_copy_pathname()` is called by a counting wrapper, not
replaced by a mock. A process-exit observer records state and releases test inputs.

When the final pathname duplicate returns NULL with errno=ENOMEM, the original
editor returns 0 and libarchive clears the pathname. **This fixture observes a
missing pathname after reported success, not the NULL-dereference crash described
in the original source-only explanation.** The fix emits the real out-of-memory
diagnostic, exits 1 and never passes NULL to pathname-copy. It preserves the entry's
pathname at that boundary (which can already contain an earlier substitution).
No claim is made about a later archive operation or end-user data loss.

`./file` alone is retained by this function. It is rewritten when the explicit
strip-components option reaches that component. Substitution-only changes can
avoid the final strdup entirely; the matrix distinguishes that case too.

## Reproduce on Linux with the native GNU-linker build

```sh
autoreconf -i
./configure
make -j2
python3 tests/regressions/pr824/run.py --cc gcc --output /tmp/pr824-gcc.json
python3 tests/regressions/pr824/run.py --cc clang --output /tmp/pr824-clang.json
python3 tests/regressions/pr824/controls.py --cc gcc --output /tmp/pr824-controls
```

The make fragment reads the configured project's real object and link variables.
It substitutes only the complete utility translation unit and renames native main
to allow the test entrypoint. Native substitution/libarchive code is retained.
An unexpected network-select call stops the fixture. All pathname and link inputs
are synthetic strings; no archive is written or extracted.

39 named cases, each with allocation success/failure and quiet on/off, give 156
scenarios. They cover unchanged paths, absolute/Windows/UNC prefixes, root/empty
paths, strip-component early returns, hardlinks/symlinks, actual substitutions,
UTF-8 and long strings. Expected output strings, counts, status and diagnostics are
explicit; a second copy of the pathname algorithm is not used as an oracle.

## Measured local results

Exact original head: `707dbc28141a79ab216676a2e20aa77aaed371b8`.
Source tree: `314d1556abc1f58be94a0883fef40529819b7d44` (all 422 tracked files).
Candidate util blob: `8b6a162b7835cffcd59c7ae3c39d77ec3002bbaf`.
Original util blob: `e16564560086441ccbc7f7beade122b1ac115bc2` at base
`0bf0b299fed91139044c81cc6fcdc5521c34d235`.

GCC14.2 and Clang17 callers each pass all156 scenarios. These are repeated compiler
configurations, not312 distinct tests. Native project build and all five binary
`--version` checks also pass. Full repository tests and live-server behavior are
not claimed. The local configure log also records an unavailable
AX_CFLAGS_WARN_ALL macro (the warning-option macro was not expanded); compilation
still completed. The isolated package cache has no autoconf-archive candidate.
The hosted reproduction installs that documented build dependency before
autoreconf and records its own clean native build separately.

The exact original fails50 with the observed NULL-pathname/success signature.
Moving the check after copy, changing its exit code to0 and omitting errno each
fail50; suppressing the check in quiet mode fails25. The controls require those
specific behavior differences and that all remaining scenarios stay correct.

## Sanitizer limits, deliberately retained

```sh
python3 tests/regressions/pr824/run.py --cc clang --sanitize --output /tmp/pr824-san.json
python3 tests/regressions/pr824/run.py --cc clang --sanitize \
  --source /tmp/pr824-controls/baseline.c --output /tmp/pr824-old-san.json
python3 tests/regressions/pr824/sanitizer_audit.py \
  --candidate /tmp/pr824-san.json --baseline /tmp/pr824-old-san.json \
  --output /tmp/pr824-san-limits.json
```

The first two commands return1: the **full sanitizer suites fail**. Candidate is
128/156; original is84/156. Both have the same4 hardlink `memcpy-param-overlap`
findings in unchanged libarchive calls and24 regex-cleanup leak findings originating
in native regcomp allocations. Original has44 further functional failures outside
those paths. Missing regfree is also mentioned in existing upstream PR676; no
second report is created here. The audit command verifies this distinction; its
success is not a sanitizer pass. No sanitizer finding is suppressed and no
unrelated production fix is mixed into this branch.

Sanitizer instrumentation covers the test caller and complete utility translation
unit, plus runtime interceptors; dependency objects retain their native build.
GCC/Clang full-matrix behavioral passes and the sanitizer limitation are separate
results. Preserve both when citing this contribution.

## Sources and delivery

- Existing PR and report: https://github.com/Tarsnap/tarsnap/pull/824 and https://github.com/Tarsnap/tarsnap/issues/823
- Bundled NULL handling: `libarchive/archive_entry.c::aes_copy_mbs`
- Prior regex-cleanup mention: https://github.com/Tarsnap/tarsnap/pull/676
- Sponsor rules: https://www.tarsnap.com/bugbounty.html (checked September8,2026)

Bounty eligibility/classification is the sponsor's decision on the original report,
not established by these tests. Hosted execution and exact artifact readback will
be recorded separately in `HOSTED.md`; this local receipt is not a hosted result.
