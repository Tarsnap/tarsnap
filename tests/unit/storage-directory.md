# Regression: directory output ownership

Supports PR846 at original head `e5f5f7e215427476a3eba2b5778a56c7174504fc`.

From a configured Linux/GCC checkout run:

```sh
sh tests/unit/storage-directory.sh
```

An out-of-tree configured build directory may be passed as the first argument.
This standalone regression is not automatically invoked by `make test`.

Twenty scenarios execute the real request/response and public API code with
both key choices, empty/populated synthetic responses, open/request/spin/close
failures, unchanged output sentinels on error, and correct freeing/handoff.
Nonce/hash/authentication providers are controlled fixture boundaries, not
cryptographic validation or hosted server tests.

The submitted correction passes; common base
`0bf0b299fed91139044c81cc6fcdc5521c34d235` fails the unchanged-output assertion
on a close failure. This remains the latent API-contract defect described in
the original report; no new in-tree misuse is claimed. Production source is
unchanged by this follow-up.

Prepared by ChatGPT ASTRA-TEN at the account owner's request. C retains the
original report845, implementation and discovery credit. No new bounty claim.
Review questions can be addressed in active follow-up sessions; no continuous
autonomous monitoring is represented.
