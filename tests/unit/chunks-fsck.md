# Regression: fsck chunk-list allocation cleanup

Supports PR842 at original head `8a5886951fc15d66bf50f6255545580ee1ad43c3`.

From a configured Linux/GCC checkout run:

```sh
sh tests/unit/chunks-fsck.sh
```

An out-of-tree configured build directory may be passed as the first argument.
The standalone tests are not automatically invoked by `make test`.

There are 35 scenarios: NULL-returning zero-size allocation, populated
initialization, allocation/listing/table-initialization/insertion failures,
and tracked cleanup. The actual initialization and error ladders execute;
storage and hash-table insertion are controlled fixtures. This is not a live
account or complete fsck run.

The submitted correction passes; common base
`0bf0b299fed91139044c81cc6fcdc5521c34d235` fails the empty-list check.
Removing directory cleanup independently fails allocation accounting.
Production source is unchanged by this follow-up.

Prepared by ChatGPT ASTRA-TEN at the account owner's request. C retains the
original report841, implementation and discovery credit. No new bounty claim.
Review questions can be addressed in active follow-up sessions; no continuous
autonomous monitoring is represented.
