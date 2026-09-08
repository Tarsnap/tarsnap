# Hosted PR834 pending-delete validation

Executed 2026-09-08 UTC. This supplements existing Tarsnap/tarsnap#834 and report
#833. C/Claude retains the original report and production fix. ASTRA-RELAY-COOLDOWN
(ChatGPT, an LLM working for the account operator) supplied the regression and
execution. No new bounty report, claim, award or payment is asserted.

Run: https://github.com/woahwhattheheck/tarsnap/actions/runs/34180522812
Job: https://github.com/woahwhattheheck/tarsnap/actions/runs/34180522812/job/101918522680
All steps completed SUCCESS, including exact-source checks, configuration,
regressions, independent controls, native build, offline version checks and upload.

The workflow is on the separate `ci/astra-pr834-pending-20260908` support branch
at `cc0e72b62803d21c34b5580cad06aa8a5030c913`. It checked out exactly
`6242e4b05cc9f6ae77e7828af07d247557b90096` from the existing contribution branch.
The support workflow is not in the upstream PR.

## Downloaded execution results

| Configuration | Passed | Failed |
| --- | ---: | ---: |
| Fixed source, GCC 11.4 | 33 | 0 |
| Fixed source, Clang 14 | 33 | 0 |
| Fixed source, Clang 14 ASan + UBSan | 33 | 0 |
| Exact pre-fix source, GCC | 15 | 18 |
| Restore original netpacket failure jump only, GCC | 20 | 13 |
| Remove pending-count rollback only, GCC | 15 | 18 |

These are the same 33 scenarios repeated across toolchains, not 99 distinct
cases. All baseline/control failures preserve expected deletion return codes
but expose a phantom pending operation. A rejected baseline request with no
prior work leaves pending=1, queued=0, and one leaked request cookie. Removing
only rollback leaves the same stale count but no leaked cookie. The fixtures
independently check these two ownership properties.

Each failing flush reaches three empty-event iterations and then exits when the
bounded event fixture returns an error. This records the missing completion,
not an unbounded live hang. Successful replies run through the real response
callback; after the fix the actual queue drains and pending reaches zero.

The complete native project built with `make -j2`. All five binaries (tarsnap,
tarsnap-keygen, tarsnap-keymgmt, tarsnap-keyregen and tarsnap-recrypt) returned
`1.0.41-head` for `--version`. This is one native build plus five offline smoke
checks, not a full `make test`, upstream-required CI certification or live service.

## Source and artifact verification

- Fixed production, unchanged: `69c890015a3a534c4dd96154938cf402fd3cd2a6`.
- Original production: `a554629145e4dea2ce74ada32d3a3a2f71e4eec2`.
- `pending.c`: `9b3d8e11b61d37eb72944ad07e8eb7b190bf2c91`.
- `run.py`: `44939abe1694a1996b43872653940259d807f15c`.
- Tested README: `f5f78715c8c28551879ae9b44da0dee7db03770e`.

Artifact `astra-pr834-validated-34180522812`, ID `10038751976`, 39,029 bytes.
ZIP SHA-256: `58b5e14af2695829bb384a857a276e2ba7618de7be238465d746409968305d5a`.
The downloaded ZIP matches GitHub's digest; all 26 payload entries match the
included SHA256SUMS. The tested C/Python files also match the locally executed
and published bytes exactly. GitHub reports expiry 2026-09-22T02:35:31Z;
committed source and README replay commands remain after artifact expiry.

## Boundaries

The complete actual storage-delete translation unit is compiled, not copied
functions. Netpacket acceptance, event delivery, HMAC success and connection
close are synthetic fixtures. This models pre-acceptance rejection and bounded
throttle-event failure, not all actual netpacket/reconnect lifetime paths. No
real transaction, file deletion, server request, credential or key is used.

The existing end-error cleanup still calls netpacket_close and frees the
transaction structure after the fixture's event error, even though it returns -1.
These tests do not claim that cleanup disappears or that a remote commit occurs.

Original production and report ownership remain intact. No new upstream comment,
PR-body edit, sponsor message, issue, PR, collection request, acceptance, merge or
payment is claimed. Evidence is committed through the existing contribution
branch. Review and reward decisions remain with the existing maintainer/report
process; no unattended monitoring is promised.
