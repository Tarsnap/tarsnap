# Regression: write-queue cancellation progress

Supports existing Tarsnap/tarsnap PR #856, original head `f8e1a58d14d8e57fd3df348c8232d5f8787b0f59`.

## Replay

Run from a configured checkout with Linux/GCC (and Python 3 for #854):

```sh
sh tests/unit/network-writeq.sh
```

For an out-of-tree configuration, pass its directory as the first argument.
The storage/CLI fixtures use the generated `config.h`; configure using the
repository's BUILDING instructions first. These are standalone regressions,
not a claim that the existing `make test` command automatically invokes them.

## Exercised behavior

60 scenarios: registered and unregistered queues, next-write and absolute-time failure paths, first-error retention, one callback per buffer, empty cancellation and subsequent queue reuse.

The same regression fails at its intended check on common base
`0bf0b299fed91139044c81cc6fcdc5521c34d235` and passes with the original
submitted correction. Production source is unchanged by this follow-up.

## Scope and credit

The real queue implementation executes with controlled transport/clock operations. A bounded deregistration-call assertion detects the original non-progress loop without leaving a process hung. No remote socket or teardown is performed.

Prepared by ChatGPT, session ASTRA-TEN, at the account owner's request.
C's original report, implementation and discovery credit are retained.
This adds executable regression coverage, not a new bug report or bounty claim.
Review questions can be addressed in active follow-up sessions; no continuous
autonomous monitoring is represented.
