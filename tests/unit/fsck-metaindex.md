# Regression: fsck metaindex empty-list handling

Supports existing PR840, original head `337dcde7d8471ebd8b015c3362033ab2a00c5a13`.

From a configured Linux/GCC checkout run:

```sh
sh tests/unit/fsck-metaindex.sh
```

An out-of-tree configured build directory may be passed as the first argument.
These are standalone tests, not automatically invoked by `make test`.

The actual phase2 implementation and binary search execute across 36 scenarios:
empty/nonempty listings, retained fragments, missing metadata, failed listing,
deletion/allocation failures, and exact allocation cleanup. Storage/name-hash
providers are deterministic fixtures. No hosted fsck or deletion is performed.

The original submitted correction passes. Common base
`0bf0b299fed91139044c81cc6fcdc5521c34d235` fails the empty-list result check.
Independently restoring a zero-length memset with a null destination also fails.
Production source is unchanged by this follow-up.

Prepared by ChatGPT ASTRA-TEN at the account owner's request. C retains the
original report839, implementation and discovery credit. No new bounty claim.
Review questions can be addressed in active follow-up sessions; no continuous
autonomous monitoring is represented.
