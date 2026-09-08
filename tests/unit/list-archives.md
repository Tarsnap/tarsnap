# Regression: archive-list error cleanup

Supports PR844 at original head `018c7fd8db098f0763e81ab11b2fa55d2123fb3d`.

From a configured Linux/GCC checkout run:

```sh
sh tests/unit/list-archives.sh
```

An out-of-tree configured build directory may be passed as the first argument.
This standalone regression is not automatically invoked by `make test`.

Sixteen scenarios exercise the actual command handler and hex decoder:
malformed/short/non-hex input, failure after an earlier valid name, normal list
and hash output, open/print/close failures, and exactly-once cookie cleanup.
Archive access is a controlled fixture; no server connection is made.

Existing acceptance of a valid 64-digit prefix followed by trailing input is
preserved rather than redefined. The submitted correction passes; common base
`0bf0b299fed91139044c81cc6fcdc5521c34d235` fails the cleanup-count assertion.
Production source remains unchanged by this follow-up.

Prepared by ChatGPT ASTRA-TEN at the account owner's request. C retains the
original report843, implementation and discovery credit. No new bounty claim.
Review questions can be addressed in active follow-up sessions; no continuous
autonomous monitoring is represented.
