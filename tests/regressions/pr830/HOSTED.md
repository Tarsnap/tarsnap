# Hosted PR830 validation

Executed 2026-09-08 UTC for existing Tarsnap/tarsnap PR830 and report829.
C/Claude retains the original report and production-fix credit.
ASTRA-RELAY-COOLDOWN (ChatGPT, an LLM working for the account operator) added
execution evidence; this is not a new bounty report or claim.

## Exact execution

Run: https://github.com/woahwhattheheck/tarsnap/actions/runs/34181926340
Job: https://github.com/woahwhattheheck/tarsnap/actions/runs/34181926340/job/101922593174
All job steps completed successfully, including exact-byte assertions, configure,
three compiler/sanitizer runs, independent mutation controls, native build, five
offline version checks and artifact upload.

The fork-only support workflow is on `ci/astra-pr830-cache-write-20260908`, commit
`68812bb3edb0eabdbcc8ca10747089b8f6e99ca0`. It checked out contribution commit
`ddcdf19467fb0e5382fd6c9b991ec30ca71cde46`. That commit adds four test/documentation
files to the original PR head; no production bytes or peer branch were changed.
The support workflow is not included in the upstream contribution.

| Executed source/configuration | Passed | Failed |
| --- | ---: | ---: |
| Fixed, GCC 11.4 | 113 | 0 |
| Fixed, Clang 14 | 113 | 0 |
| Fixed, Clang 14 ASan/UBSan plus automatic-storage pattern | 113 | 0 |
| Exact pre-fix, GCC | 88 | 25 |
| Remove error free only, GCC | 89 | 24 |
| Remove post-free NULL assignment only, GCC | 77 | 36 |
| Restore remove/asprintf error jump only, GCC | 112 | 1 |
| Move initialization late, Clang/pattern | 107 | 6 |

The same 113 scenarios are repeated across configurations; they are not 339
distinct tests. Downloaded JSON matches the local GCC14/Clang17 results. All
candidate compile commands have empty stderr, and every candidate case records
zero tracked outstanding allocations, invalid-free attempts and open streams.
Independent failure signatures distinguish 24 leaked buffers, the remove
asprintf invalid-free attempt, 36 later double-free attempts and six early
uninitialized-pointer attempts. Expected return codes and file closure remain
checked even in deliberately failing controls.

The complete native project built with `make -j2`; all five binaries returned
`1.0.41-head` for `--version`. This is one full native build and five offline
smoke checks, not a full `make test`, remote archive test or payment evidence.

## Verified artifact

Artifact: `astra-pr830-validated-34181926340`, ID `10039215082`, 65,557 bytes.
Downloaded ZIP SHA-256:
`6675d066889408d8bf39ca08439e7c8afdaffb663be1a122d26ade378387e0a2`.
The ZIP matches GitHub's digest; all 35 payload files match the internal
SHA256SUMS. All four test/documentation files match the local executed bytes.
GitHub reports expiry 2026-09-22T03:00:23Z; committed source and replay commands
remain available after artifact expiry.

Exact blobs:
- Production, unchanged: `55f3e58200d435ee5b135740bc4fd83e06b10a36`.
- Pre-fix production: `e6249385097d40b149b6986b9ff8d6f54982cfb7`.
- C fixture: `187ee009df35cf441c9387170a082f90ea444723`.
- Runner: `ac425ca854225d851ba5db43f6d961c7d0a1aad7`.
- Controls: `74b4d799f8a412ca8a33be32c0654393436e7f90`.
- Tested README: `c7f1f5514ea412d763501df73605d42ea92cfd19`.

Preparation run34181384442 configured/exported the exact source but failed before
native build because it attempted to copy make-generated configuration headers
too early. It is not counted as a passing build. The succeeding full run above
corrects the step ordering and provides the actual final execution evidence.

## Boundaries and replay

See README.md and the runnable `run.py`/`controls.py` alongside this receipt.
Production writer, Patricia traversal and stdio are real; allocation/error seams
and generated records are synthetic. Invalid frees are recorded without passing
them to libc, and measured baseline leaks are cleaned up after recording.
Sync seams use local flush/fsync rather than the complete platform helper. Only
the unchanged Patricia dependency's separately known signed-shift sanitizer is
excluded; its other checks and all writer/harness sanitizers remain enabled.
No claim is made about raw baseline sanitizer crashes or crash durability.

No real cache, server, key, credential, owner-PC resource, sponsor message,
additional report, bounty submission, accepted award, payment or upstream merge
is part of this validation. PR830 remains with its original reporter/maintainer
process; evidence is supplied through its existing contribution branch.
