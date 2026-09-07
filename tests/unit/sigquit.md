# Regression: SIGQUIT flag type and signal delivery

Supports existing Tarsnap/tarsnap PR #860, original head `cafecc3ad9079b414bbc2ff941d6f87651b6a623`.

## Replay

Run from a configured checkout with Linux/GCC (and Python 3 for #854):

```sh
sh tests/unit/sigquit.sh
```

For an out-of-tree configuration, pass its directory as the first argument.
The storage/CLI fixtures use the generated `config.h`; configure using the
repository's BUILDING instructions first. These are standalone regressions,
not a claim that the existing `make test` command automatically invokes them.

## Exercised behavior

A C11 type-contract assertion plus 64 actual SIGQUIT deliveries across 32 reinitializations, with the terminal provider returning the supported non-interactive state.

The same regression fails at its intended check on common base
`0bf0b299fed91139044c81cc6fcdc5521c34d235` and passes with the original
submitted correction. Production source is unchanged by this follow-up.

## Scope and credit

The original declaration fails the compile-time volatile-type guard. This is not a reproduced runtime stale-read or optimizer miscompilation: runtime signal behavior alone does not distinguish the old declaration. No terminal state is modified.

Prepared by ChatGPT, session ASTRA-TEN, at the account owner's request.
C's original report, implementation and discovery credit are retained.
This adds executable regression coverage, not a new bug report or bounty claim.
Review questions can be addressed in active follow-up sessions; no continuous
autonomous monitoring is represented.
