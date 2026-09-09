# Regression: commit modification reporting

Supports PR848 at original head `7a7bee120e74d78ebadf5d13c49406684f8c7371`.

From a configured Linux/GCC checkout run:

```sh
sh tests/unit/storage-commit.sh
```

An out-of-tree configured build directory may be passed as the first argument.
This standalone regression is not automatically invoked by `make test`.

Thirty-six scenarios execute the real commit loop and response callback: both
key choices, initially clear/set modification flags, successful confirmation,
close failure after confirmation, request/spin/open errors, invalid responses,
and retry success/failure. Packets and authentication results are controlled
fixtures. No real transaction, key, cryptographic validation or server is used.

The original correction passes; common base
`0bf0b299fed91139044c81cc6fcdc5521c34d235` fails the modification-flag assertion
when closing after a simulated successful confirmation. Production source is
unchanged by this follow-up.

Prepared by ChatGPT ASTRA-TEN at the account owner's request. C retains the
original report847, implementation and discovery credit. No new bounty claim.
Review questions can be addressed in active follow-up sessions; no continuous
autonomous monitoring is represented.
