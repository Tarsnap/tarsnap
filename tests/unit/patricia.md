# Regression: Patricia critical-bit arithmetic

Supports existing Tarsnap/tarsnap PR #862, original head `0e21682724928ab30df270730ab1e6f7aa1dc456`.

## Replay

Run from a configured checkout with Linux/GCC (and Python 3 for #854):

```sh
sh tests/unit/patricia.sh
```

For an out-of-tree configuration, pass its directory as the first argument.
The storage/CLI fixtures use the generated `config.h`; configure using the
repository's BUILDING instructions first. These are standalone regressions,
not a claim that the existing `make test` command automatically invokes them.

## Exercised behavior

261 binary keys, 34191 incremental lookups, every single-byte value, long/shared prefixes, duplicate insertions, lexical traversal, callback stop propagation and mutable record pointers under UBSan.

The same regression fails at its intended check on common base
`0bf0b299fed91139044c81cc6fcdc5521c34d235` and passes with the original
submitted correction. Production source is unchanged by this follow-up.

## Scope and credit

The actual module is compiled with undefined-behavior sanitization. The original negative shift is detected, and independently restoring either shift site also fails. No optimizer miscompilation, security impact or external service behavior is claimed.

Prepared by ChatGPT, session ASTRA-TEN, at the account owner's request.
C's original report, implementation and discovery credit are retained.
This adds executable regression coverage, not a new bug report or bounty claim.
Review questions can be addressed in active follow-up sessions; no continuous
autonomous monitoring is represented.
