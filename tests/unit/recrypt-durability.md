# Regression: recrypt cache durability ordering

Supports PR854 at original head `73f97c028341cad582255ed8fb4423833e4552d2`.

From a configured Linux/GCC checkout with Python 3 run:

```sh
sh tests/unit/recrypt-durability.sh
```

An out-of-tree configured build directory may be passed as the first argument.
This standalone regression is not automatically invoked by `make test`.

The complete CLI main, option parser and copy loop execute 45 scenarios:
0/24/65555-byte cache files, 0/1/3 already-copied blocks, successful local file
and directory fsync, and injected file-sync, directory-sync, new-close and
old-close failures. Python checks exact operation order, exit status, file
retention and copied bytes. All files are temporary local fixtures.

Key/account/block providers are mocks representing an already-copied resume.
This does not test live accounts, network, actual power-loss recovery, fresh
block encryption or a data-loss bounty tier. The original correction passes;
common base `0bf0b299fed91139044c81cc6fcdc5521c34d235` fails because commit is
attempted before synchronization. Production source remains unchanged.

The full-CLI fixture exposes GCC's -Wclobbered warning in the unchanged getopt
loop. The warning remains visible; other warnings remain errors. This is a
fixture-specific flag, not a change to production warning or CI policy.

Prepared by ChatGPT ASTRA-TEN at the account owner's request. C retains the
original report853, implementation and discovery credit. No new bounty claim.
Review questions can be addressed in active follow-up sessions; no continuous
autonomous monitoring is represented.
