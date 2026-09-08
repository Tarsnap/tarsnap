# PR834 pending-delete regression

This is additive validation of Tarsnap/tarsnap#834 and the existing report #833.
C/Claude retains original discovery and implementation credit. The original
production patch is unchanged. ASTRA-RELAY-COOLDOWN (ChatGPT, an LLM working for
the account operator) added this fixture and execution. No new bounty claim,
award, acceptance or payment is asserted; review remains in the existing PR.
This receipt does not promise unattended monitoring.

## Actual implementation exercised

`pending.c` includes the complete production `tar/storage/storage_delete.c` with
its real headers. It executes `storage_delete_file`, the real send/response
callbacks, `storage_delete_flush` and `storage_delete_end`. Production functions
and structures are not extracted or reimplemented. Link-time section collection
discards unused entrypoints; the required USERAGENT macro is a fixture string
and is never used to open a connection.

The netpacket queue, event service, HMAC result and connection-close service are
synthetic local fixtures. The rejected operation models a failure before queue
acceptance or callback invocation. Existing accepted operations are seeded by
actual `storage_delete_file` calls, and their synthetic successful replies pass
through the real response callback to decrement counters and free cookies.

This does **not** validate every actual netpacket failure stage, reconnection,
network lifetime, cryptographic behavior, server transaction or remote deletion.
In particular, failure after the real netpacket layer has retained an operation
is outside this fixture. No request leaves this process and no real file is deleted.

Allocation wrappers perform real allocation/free and count live pointers and
invalid frees. A phantom wait is bounded: the event fixture returns success for
two empty iterations and then returns an error on the third. Thus the regression
proves a stale pending counter reaches an empty-event wait; it does not run an
unbounded live hang. All failures are recorded before fixture leftovers are freed.

## Cases and local results

The 33 cases cover successful submission, rejected submission, readonly and
allocation failures with 0/1/512/1023/1024 already queued operations; failures
after 0/1/17/127/511 completions in the throttle loop; repeated rejection; and a
successful request following rejection. They require pending count to match the
actual fixture queue, correct cookie ownership, clean drain and final cleanup.

Exact original PR head: `7f31e5155093211990d376bdf7352551e9f92ca0`.
Fixed source blob: `69c890015a3a534c4dd96154938cf402fd3cd2a6`.
Exact base source blob: `a554629145e4dea2ce74ada32d3a3a2f71e4eec2` at
`0bf0b299fed91139044c81cc6fcdc5521c34d235`.

Isolated local execution on 2026-09-08 UTC:

| Source / toolchain | Passed | Failed |
| --- | ---: | ---: |
| Original submitted fix, GCC 14.2 | 33 | 0 |
| Original submitted fix, Clang 17 | 33 | 0 |
| Original submitted fix, Clang 17 ASan + UBSan | 33 | 0 |
| Exact pre-fix source, GCC | 15 | 18 |
| Restore old netpacket error route only, GCC | 20 | 13 |
| Remove pending-count rollback only, GCC | 15 | 18 |

The baseline rejected request leaves one phantom pending operation and one leaked
request cookie. Removing only rollback still frees that cookie but leaves the
phantom counter, which the independent checks distinguish. Removing only the old
error-route repair preserves the corrected throttle path while failing rejected
submission cases. Early readonly/allocation failures and success remain passing.

One precise cleanup distinction matters: when the bounded event fixture errors,
`storage_delete_end` returns -1 **but still calls netpacket_close and frees the
transaction structure through its existing error cleanup**. These tests do not
claim that cleanup is absent, nor that a remote transaction was committed. The
repair removes the phantom wait and leaked request cookie in its scoped paths.

## Replay

Requires a configured Linux checkout, Python 3.9+, GCC/Clang and a GNU-compatible
linker. Use the project's normal build dependencies, including ext2fs headers on
Linux. No Python packages are needed.

```sh
autoreconf -i
./configure
python3 tests/regressions/pr834/run.py --output /tmp/pr834-fixed.json
python3 tests/regressions/pr834/run.py --cc clang --sanitize --output /tmp/pr834-sanitized.json
git show 0bf0b299fed91139044c81cc6fcdc5521c34d235:tar/storage/storage_delete.c > /tmp/pr834-base.c
# Expected exit 1, with 18 failed cases:
python3 tests/regressions/pr834/run.py --source /tmp/pr834-base.c --output /tmp/pr834-base.json
```

A temporary fixed-source copy with only the new `netpacket_op` failure jump
restored to `err0` produces 13 failures. Removing only the new rollback statement
produces 18 failures without the rejected-operation cookie leak. Neither control
edits the checkout. Success exits 0; failed regression exits 1; configuration or
compilation failure exits 2. All cases are recorded as JSON, including queue,
pending count, allocations, flush/end results and empty-event iterations.

Hosted and whole-project build results are recorded separately when executed;
local focused tests do not imply upstream-required CI or full server integration.
The optional support workflow remains on a separate owner-fork branch.
