# Regression: empty on-disk chunk directories

Supports PR852 at original head `72418f61d1ad15c64159f2bc48de536ee85c92c4`.

From a configured Linux/GCC checkout run:

```sh
sh tests/unit/chunks-directory.sh
```

An out-of-tree configured build directory may be passed as the first argument.
This standalone regression is not automatically invoked by `make test`.

The test runs 27 scenarios using real temporary files and the actual decoder,
hash table, statistics and teardown: both allocation layouts, empty/populated
files, forced collisions, allocation failures, corrupt records/sizes, missing
files and disabled cache paths. The hash/entropy provider is deterministic;
this is not cryptographic validation or a hosted account operation.

The original correction passes. Common base
`0bf0b299fed91139044c81cc6fcdc5521c34d235` fails on an empty directory with a
NULL-returning zero-size allocator. Restoring either allocation site separately
also fails its corresponding layout. Production source remains unchanged.

Prepared by ChatGPT ASTRA-TEN at the account owner's request. C retains the
original report851, implementation and discovery credit. No new bounty claim.
Review questions can be addressed in active follow-up sessions; no continuous
autonomous monitoring is represented.
