# Hosted PR828 native append validation

Executed 2026-09-08 UTC for existing Tarsnap/tarsnap PR828 and report827.
C/Claude retains the original report and two-line production correction.
ASTRA-RELAY-COOLDOWN (ChatGPT, an LLM working for the account operator) added
these execution results. No new bug report, bounty claim or award is asserted.

## Exact run

Run: https://github.com/woahwhattheheck/tarsnap/actions/runs/34183175743
Job: https://github.com/woahwhattheheck/tarsnap/actions/runs/34183175743/job/101926212354
All steps completed successfully: exact source assertions, configure, native
build, five offline version checks, two caller-compiler matrices, exact baseline
and four independent controls, Python compilation and artifact upload.

The fork-only support branch `ci/astra-pr828-append-20260908` has workflow commit
`cb618b84aba4f9f01bb663a3cf199324d38e7525`. It checked out the exact contribution
`1562d164e8026f9bd1429aa97019c671f05a89c3`. That checkout adds five scoped test/
documentation files to the original PR head; production source stays unchanged.
The support workflow is not part of the upstream PR.

| Executed source/configuration | Passed | Failed |
| --- | ---: | ---: |
| Fixed caller and native dependencies, GCC11.4 | 36 | 0 |
| Fixed caller/test with Clang14, same GCC native dependencies | 36 | 0 |
| Exact pre-fix source | 16 | 20 |
| Remove only local-file cleanup | 24 | 12 |
| Remove only Tarsnap-open cleanup | 24 | 12 |
| Move the added frees before error-string consumption | 16 | 20 |
| Duplicate the added frees | 16 | 20 |

These are 36 distinct scenarios repeated across configurations, not 72 new cases.
The changed translation unit and test entrypoint were compiled with Clang for the
second matrix; this is not a whole-project Clang build or sanitizer run. Both
candidate fixture builds have empty stderr. Every candidate scenario records
zero retained readers, late error-string reads, duplicate finishes or bad return
values, and a peak of one live reader. Real warning text is checked.

Downloaded controls match the local GCC14/Clang17 results. Across the original
36-case matrix, 264 real reader objects remain allocated before bounded teardown;
removing only either cleanup retains132. The largest mixed case retains64 readers
after160 append attempts on the original, versus zero after the correction.
Ordering and duplicate controls record264 late-error reads or double-finish
attempts, respectively, without invoking invalid memory access. Late read-error
paths for junk files/directories remain passing baseline controls.

One complete native project build passed using `make -j2`; all five binaries
(tarsnap, tarsnap-keygen, tarsnap-keymgmt, tarsnap-keyregen, tarsnap-recrypt) returned
`1.0.41-head` for `--version`. No full `make test`, remote archive or payment result
is inferred from these checks.

## Downloaded evidence

Artifact `astra-pr828-validated-34183175743`, ID10039616144, 101,144bytes.
ZIP SHA-256:
`a772e052af1a59c95acd9239cac59090af01466501211ee6ecdbeb64bec07988`.
The downloaded ZIP matches GitHub's digest. All35 payload files match the internal
SHA256SUMS and all five published test/documentation files match the locally
executed bytes. GitHub reports expiry2026-09-22T03:21:38Z; committed source and
replay instructions remain after artifact expiry.

Exact source blobs:
- Production, unchanged: `5511a8effd072804af97988666d0915e26c02100`.
- Pre-fix production: `29efaf98e1d83d0b4abe5d45a47da762af1b4bb4`.
- `append.c`: `d566d3213b416dccdf02addbcb2ef0b43b40ab69`.
- `fixture.mk`: `6e0ea0a74504252bdfd699142eadfadc849b2613`.
- `run.py`: `9ddca38c95c5637202138aeaebcd28cb64478a2c`.
- `controls.py`: `6bd517fd0683fde368f8cfc7bd777d718934a2c1`.
- Tested README: `cc645c2c8367701014e22df25cfa6fa650243c2f`.

## Boundaries

The complete actual `tar/write.c`, bundled libarchive allocator/readers/free code,
warning path and native project objects are used. Missing local files and later
format errors are real; the remote-open boundary uses a synthetic rejection or
empty in-memory archive. Unexpected network-select terminates the test. No live
server, key, account, credential or real archive is accessed.

Lifetime wrappers count real allocation/release, record attempted invalid use
without executing it, and release measured baseline leaks after recording. These
are bounded ownership measurements, not raw LeakSanitizer or Valgrind output.
See README.md, run.py and controls.py for exact replay and further scope.

No production modification, owner-PC action, sponsor notification, new report,
submission, acceptance, payment or upstream merge is part of this delivery.
Evidence is published through the existing contribution branch; original report
and maintainer ownership remain intact. Final branch/source readback is recorded
in the canonical Slack work thread after this evidence document is committed.
