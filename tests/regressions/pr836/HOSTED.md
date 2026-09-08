# Hosted PR836 Padme validation

Executed 2026-09-08 UTC. This supplements existing Tarsnap/tarsnap#836 and report
#835. C/Claude retains original discovery and one-cast implementation credit.
ASTRA-RELAY-COOLDOWN (ChatGPT, an LLM working for the account operator) supplied
these tests and execution. This is not a new bounty claim, award or payment.

Run: https://github.com/woahwhattheheck/tarsnap/actions/runs/34179868827
Job: https://github.com/woahwhattheheck/tarsnap/actions/runs/34179868827/job/101916602894
All job steps completed SUCCESS: exact-source checks, configuration, candidate
oracle comparisons, original-source controls, native build, five offline version
checks, evidence manifest and artifact upload.

The separate support workflow is on `ci/astra-pr836-padding-20260908` at
`ab93831df4e2655541ba4d382cbeceaa6bd3cbbd`. It checked out exactly
`39a299ce94e807d359d0cee423ddda3378972f9b` from the contribution branch. The
support workflow is not included in the upstream PR.

## Results read from downloaded evidence

| Configuration | Matched scalar vectors | Total vectors |
| --- | ---: | ---: |
| Fixed source, GCC 11.4 | 16,266 | 16,266 |
| Fixed source, Clang 14 | 16,266 | 16,266 |
| Fixed source, Clang 14 nonrecovering UBSan | 16,266 | 16,266 |
| Exact pre-fix source, GCC 11.4 | 13,964 | 16,266 |

The same matrix is repeated across toolchains, not three distinct sets of cases.
Native size_t is 64 bits and int is 32 bits. The input SHA-256 is
`b7e6dd234f58a5a2caa547d84cf0bc4e54977892fae6dedae83eda8e50d52331`.

Both independent original-source UBSan probes failed as expected:
- Length 137438953472 (2^37): left shift of 1 by 31 places cannot be represented
  in type int.
- Length 274877906944 (2^38): shift exponent 32 is too large for 32-bit int.

Each probe returned native exit 1 with the corresponding runtime diagnostic.
The original unsanitized GCC run returned native exit 0 but disagreed with the
oracle on 2,302 values, so the regression runner correctly returned failure.
Undefined unsanitized behavior is measured here, not specified as a stable result.

The entire project also built with `make -j2`; tarsnap, tarsnap-keygen,
tarsnap-keymgmt, tarsnap-keyregen and tarsnap-recrypt each returned
`1.0.41-head` for `--version`. This is one native build and offline smoke set,
not a full `make test`, upstream-required CI certification or server integration.

## Exact source and evidence

- Fixed production, unchanged: `2bf28840ec0a7283c80296c47143f7a132b37499`.
- Original production: `503f772b1a4095a7936efb9d94f95beede1b883c`.
- `padding.c`: `01403b2133f8d670246dce7523aa558552a346cb`.
- `run.py`: `e9b9067c422456788717d8852376f55da68c7725`.
- Tested README: `897a1b462631a98729df4fe9f7116f70bb1d5335`.

Artifact: `astra-pr836-validated-34179868827`, ID `10038522460`, 31,264 bytes.
ZIP SHA-256: `dc9ab9ab8adb46751595f172ff687f843ae631f431c9e79254d801a8385c8620`.
The downloaded ZIP matches GitHub's digest. All 24 payload entries match the
included SHA256SUMS; the C/Python test files match the locally executed and
published bytes exactly. Artifact expiry reported by GitHub is
2026-09-22T02:24:00Z. Source and replay commands remain committed after expiry.

## Scope and limitations

The fixture calls the actual scalar helper from the complete production C
translation unit with real headers. It never allocates the represented large
chunk buffers and uses no storage or cryptographic mocks. README.md contains
replay commands and explains the independent integer oracle and bounded domain.

The shipped client's MAXCHUNK is below the reported shift thresholds. These
results do not establish a shipped-client exploit, buffer overrun, actual large
chunk allocation, server behavior or a higher severity. No 32-bit target was run.

Production code and original report ownership are unchanged. No duplicate issue,
PR, sponsor message, claim or collection request is created. The evidence is
published through the existing contribution branch; no new upstream comment or
PR-body edit is claimed. Maintainer review, merge and any reward decision remain
separate. This receipt does not promise unattended monitoring.
