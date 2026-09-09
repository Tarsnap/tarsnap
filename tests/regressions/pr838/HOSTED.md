# Hosted PR838 validation receipt

Executed 2026-09-08 UTC. This advances existing Tarsnap/tarsnap#838 and report #837;
C/Claude retains discovery and implementation credit. ASTRA-RELAY-COOLDOWN
(ChatGPT, an LLM working for the account operator) supplied this validation.

Run: https://github.com/woahwhattheheck/tarsnap/actions/runs/34179206496
Job: https://github.com/woahwhattheheck/tarsnap/actions/runs/34179206496/job/101914645696
The job completed SUCCESS, including exact-source checks, all regression/control
commands, native build, five offline version checks, and artifact upload.

The support workflow is on `ci/astra-pr838-cleanup-20260908` at
`6345fcb3bae0573b9c76e3dbe4513cd7a8f1fae9`; it checks out the contribution's exact
`d6d6aeb2e4c9db931b5148e7fa3e429176c39cf4`. It is not part of the upstream PR.

## Results read from the downloaded artifact

| Configuration | Passed | Failed |
| --- | ---: | ---: |
| Fixed source, GCC 11.4 | 94 | 0 |
| Fixed source, Clang 14 | 94 | 0 |
| Fixed source, Clang 14 ASan + UBSan | 94 | 0 |
| Exact pre-fix source, GCC | 66 | 28 |
| Only hash-error cleanup removed, GCC | 82 | 12 |
| Only mismatch cleanup removed, GCC | 78 | 16 |

Every negative-control failure returned exit 1 and retained allocations. Their
failing scenario sets were checked independently. The 94 cases are the same
matrix across compilers, not 282 distinct tests. All five compiled binaries
returned `1.0.41-head` for `--version`.

Exact tested blobs:
- Production, unchanged: `487d27fbc9915f6d4c35f2a2e503395b730e988a`.
- `cleanup.c`: `a26da8767627f66975e3440cb74d20742c00c59c`.
- `run.py`: `d64c348c4542f9b80ad4382daf0fef184838e8af`.
- Pre-fix production: `f715b9dba159edf2be71d517a79d54e29781ebc2`.

Artifact `astra-pr838-validated-34179206496`, ID `10038303224`, 42,453 bytes.
ZIP SHA-256: `9c01e1db14dd73a9799b9ec83e73ea44251224cafda13667c5f31a027db2ecf0`.
The downloaded ZIP matches GitHub's digest; all 26 payload entries match the
included SHA256SUMS. The tested C/Python files also match the locally executed
and published files byte-for-byte. Artifact expiry reported by GitHub:
2026-09-22T02:12:33Z. Source and replay remain committed after artifact expiry.

The earlier source-capture run 34178725998 failed during configuration because
its disposable runner lacked ext2fs headers. This successful follow-up installed
the normal build dependency. No production fix was changed to make it pass.

## Boundaries and publication

See README.md for the exact fixture and replay. Real metadata parsing/get/free
and byte comparison execute; storage, hash outputs and RSA verification are
synthetic. This does not validate cryptography, exercise live servers, execute a
full `make test`, certify upstream-required CI, or prove exploitability.

One evidence-comment attempt to upstream PR838 returned HTTP 403, `Resource not
accessible by integration`; no upstream comment was posted. This receipt is
therefore committed to the existing contribution branch, which already backs the
upstream PR. No duplicate issue/PR, repeated sponsor send, new bounty claim,
maintainer acceptance, merge, award or payment is represented here.
