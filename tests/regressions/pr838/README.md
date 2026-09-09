# PR838 metadata cleanup regression

This is additive validation of the existing fix in Tarsnap/tarsnap#838 and report
#837. C/Claude retains the original discovery and implementation credit.
ASTRA-RELAY-COOLDOWN (ChatGPT, an LLM working for this account's operator) added
these tests. This is not a new bounty report or a claim of acceptance/payment.
Review follow-through remains in the existing PR and operator coordination thread;
this report does not promise unattended monitoring.

## What runs

`cleanup.c` includes the **complete, unmodified** production metadata translation
unit and authentic project headers. It links the actual `crypto_verify_bytes.c`.
The parser, get-by-name/get-by-hash entrypoints, and metadata-free implementation
therefore execute as production C, rather than a reimplementation or extracted
function. Link-time section collection drops unrelated entrypoints.

Storage, RSA verification, the name-hash operation, and allocation failures use
local synthetic fixtures. The storage fixture hands the real parser an encoded
metadata buffer. Successful RSA verification is simulated; this is neither a
cryptographic test nor a live server/tampering demonstration. No real keys,
account, server, network call or filesystem archive is used by the focused test.

Allocation wrappers perform actual allocations and releases while counting live
pointers/bytes and invalid frees. Failure is recorded before fixture leftovers
are cleaned. A baseline leak cannot become a pass because the fixture cleans up.

The 94 cases cover both entrypoints, quiet/nonquiet diagnostics, zero/one/three
arguments, both post-decode exits, 100 repeated mismatch requests, successful
caller ownership, missing/corrupt/error reads, initial name-hash failure, malformed
metadata, signature outcomes, and every allocation-failure point for three args.
No assertions require cleared pointers: the production free function does not
promise to zero the structure.

## Executed results

Original PR source commit: `bebf285f6c36f266e64edad064260411f3936b44`.
Unchanged fixed source blob: `487d27fbc9915f6d4c35f2a2e503395b730e988a`.
Pre-fix source blob at base `0bf0b299fed91139044c81cc6fcdc5521c34d235`:
`f715b9dba159edf2be71d517a79d54e29781ebc2`.

In isolated Linux execution on 2026-09-08 UTC:

| Source / toolchain | Passing cases | Failing cases |
| --- | ---: | ---: |
| Existing fix, GCC 14.2 | 94 | 0 |
| Existing fix, Clang 17 | 94 | 0 |
| Existing fix, Clang 17 ASan + UBSan | 94 | 0 |
| Exact pre-fix source, GCC | 66 | 28 |
| Remove only post-decode hash-error cleanup, GCC | 82 | 12 |
| Remove only name-mismatch cleanup, GCC | 78 | 16 |

The pre-fix failures retain 2/3/5 allocations (8/24/50 requested bytes in these
fixtures) for zero/one/three args. Return codes remain the expected -1 or 2;
checking only the return code would miss the defect. Each independent removal
fails exactly its targeted cleanup scenarios, while the other scenarios pass.

A fresh `autoreconf -i`, `./configure`, `make -j2`, and `--version` on all five
binaries also passed locally for this PR's source. This is one native build, not
server integration or a full `make test` result. The first hosted capture run
34178725998 stopped during configure because its image lacked ext2fs headers;
that failed build is not a source-test result or a hosted pass. Hosted follow-up
results, when available, are recorded in the existing PR/coordination thread.

## Replay

Use a configured Linux checkout, Python 3.9+, GCC or Clang, and a GNU-compatible
linker. Install the project's normal build dependencies first. On Ubuntu these
include the ext2fs development headers (`e2fslibs-dev`). No Python packages needed.

```sh
autoreconf -i
./configure
python3 tests/regressions/pr838/run.py --output /tmp/pr838-fixed.json
python3 tests/regressions/pr838/run.py --cc clang --sanitize --output /tmp/pr838-asan.json
# The same tests must fail against the original source. Do not edit the checkout.
git show 0bf0b299fed91139044c81cc6fcdc5521c34d235:tar/multitape/multitape_metadata.c > /tmp/pr838-baseline.c
python3 tests/regressions/pr838/run.py --source /tmp/pr838-baseline.c --output /tmp/pr838-baseline.json
```

The last command intentionally exits 1 with 28 failed cases. Compile/configuration
failure exits 2, and successful candidate execution exits 0. Independent controls
can remove either one of the two added `multitape_metadata_free(mdat)` calls in a
temporary source copy and pass that copy to `--source`; retain the other call.

These files do not change the existing production fix, maintainer workflow,
original report, sponsor contact, or bounty terms. The optional support workflow
is kept on a separate owner-fork CI branch, not this contribution branch.
