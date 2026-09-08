# Regression: write-queue cancellation progress

Supports existing Tarsnap/tarsnap PR #856. The current PR head before this evidence-only follow-up is `eaba87578fa948166e94c54c5b76fd88c5731c80`; the regression was originally added against earlier head `f8e1a58d14d8e57fd3df348c8232d5f8787b0f59`.

## Replay

Run from a configured checkout with Linux/GCC:

```sh
sh tests/unit/network-writeq.sh
```

For an out-of-tree configuration, pass its directory as the first argument.
The fixtures use the generated `config.h`; configure using the repository's
BUILDING instructions first. These are standalone regressions, not a claim
that the existing `make test` command automatically invokes them.

## Exercised behavior

60 scenarios: registered and unregistered queues, next-write and absolute-time
failure paths, first-error retention, one callback per buffer, empty
cancellation and subsequent queue reuse.

The regression was previously recorded failing at its intended check on common
base `0bf0b299fed91139044c81cc6fcdc5521c34d235` and passing the then-submitted
correction. Production source changed afterward in
`eaba87578fa948166e94c54c5b76fd88c5731c80`: `network_writeq_cancel()` now uses
a `head_version` generation counter instead of comparing a saved queue-head
pointer after synchronous deregistration callbacks. This document does not
claim a hosted or independent exact-current-head test pass for that follow-up.

## Scope and credit

The real queue implementation executes with controlled transport/clock
operations. A bounded deregistration-call assertion detects the original
non-progress loop without leaving a process hung. No remote socket or teardown
is performed.

Prepared by ChatGPT, session ASTRA-TEN, at the account owner's request.
C's original report, implementation and discovery credit are retained.
This adds executable regression coverage, not a new bug report or bounty claim.
Review questions can be addressed in active follow-up sessions; no continuous
autonomous monitoring is represented.
