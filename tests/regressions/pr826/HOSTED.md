# Hosted PR826 native cache-entry validation

Executed 2026-09-08 UTC for existing Tarsnap/tarsnap PR826 and report825.
C/Claude retains the original report and one-line production fix.
ASTRA-RELAY-COOLDOWN (ChatGPT, an LLM working for the account operator) added
execution evidence, not a new bug report or bounty claim.

## Exact run

Run: https://github.com/woahwhattheheck/tarsnap/actions/runs/34184048109
Job: https://github.com/woahwhattheheck/tarsnap/actions/runs/34184048109/job/101928712051
All steps passed: exact source assertions, configure, full native build, five
offline version checks, both caller-compiler matrices, baseline/two independent
controls, Python compilation and artifact upload.

The fork-only support branch `ci/astra-pr826-entry-20260908` has workflow commit
`c450d5532b3ae20da2028137fb844f69b544fe66`. It checked out contribution commit
`06b0acfd2ef8fa18cf5dec05cb9aa93025a8ee4c`, adding five scoped test/documentation
files to the original PR head. No production source is changed. The support
workflow is excluded from the upstream contribution.

| Source/configuration | Passed | Failed |
| --- | ---: | ---: |
| Corrected writer and native dependencies, GCC11.4 | 88 | 0 |
| Corrected writer/test, Clang14, same GCC native dependencies | 88 | 0 |
| Exact pre-fix source | 72 | 16 |
| Wrong cleanup callback cookie | 72 | 16 |
| Duplicate cleanup call | 72 | 16 |

These are 88 distinct scenarios repeated across configurations. Clang recompiles
the writer/test translation unit, not the full dependency tree. Both candidate
fixture compilations have empty stderr. The hosted per-scenario results match
the local GCC14.2/Clang17 results.

Actual cache lookup/free, allocator, Patricia tree and missing-file opens are
exercised. The original retains264 allocations over the matrix; the fixed source
retains none. The largest uncached case retains64 allocations after32 attempts
on the original. The independent controls expose176 wrong-cookie clears or176
invalid-free attempts without invoking unsafe native tape/free operations.

Every candidate scenario has zero retained allocations, invalid frees, wrong
callback cookies and open descriptors; tree-owned records remain unchanged.
Lookup-allocation failures, absent/disabled cache inputs and metadata-only
success controls remain passing. The separate issue772 return-status defect is
not changed or represented as fixed.

One complete native GCC project build passed using `make -j2`. All five binaries
(tarsnap, tarsnap-keygen, tarsnap-keymgmt, tarsnap-keyregen, tarsnap-recrypt) returned
`1.0.41-head` for `--version`. This is not a full `make test`, whole-project Clang
or sanitizer build, remote archive test, or payment evidence.

## Verified downloadable evidence

Artifact `astra-pr826-validated-34184048109`, ID10039897769, 81,368 bytes.
Downloaded ZIP SHA-256:
`931d1bb8c5a6e9fcd364f8116a67a00eb6ffa6c7ab0a641032fb0b3549ef1a0c`.
The ZIP matches GitHub's digest. All29 manifested payload files and all five
published test/documentation files match the downloaded/local executed bytes.
GitHub reports expiry2026-09-22T03:36:21Z; committed source and replay commands
remain available after artifact expiry.

Exact blobs:
- Corrected writer, unchanged: `1fabdbf5c451004af7d805b4edd2ddb254f911c3`.
- Original writer: `29efaf98e1d83d0b4abe5d45a47da762af1b4bb4`.
- Native cache source, unchanged: `ed5f571ebb43ba77c2db2f1072a6c347a29cb41b`.
- `entry.c`: `6e307becead7e2623704aa849e688e3c9a4c473b`.
- `fixture.mk`: `ee7477c2eb8fd91e0d2b014a67053aae1209ead2`.
- `run.py`: `d472056f2274028779844dc93ee2042354e7fd82`.
- `controls.py`: `a6185a3c390f125ffdec3161403fabc4f6aa8331`.
- Tested README: `dbb1ead0ecd642afbd18926cd11673c42df8431d`.

## Limits and replay

See README.md, run.py and controls.py alongside this receipt. Callback registration
and tape-mode transitions are explicit offline seams; archive output targets
memory. No remote server, live cache, compressed-trailer/full-cache transfer,
account, credential or key is used. Network/event execution would fail the fixture.

Counters follow actual allocations during cache lookup/free, including compiler-
folded calloc. Invalid free attempts are recorded without executing them, and
baseline leaks are measured before bounded cleanup. No raw LeakSanitizer/Valgrind
or full command-line invocation claim is made.

PR826's original report, production correction, existing submission and maintainer
ownership remain intact. No sponsor message, new report, bounty submission,
owner-PC change, acceptance, payment or upstream merge is part of this delivery.
Final head and unchanged tested-source comparison are recorded in the Slack work
thread after this document is committed.
