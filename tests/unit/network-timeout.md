# Regression: empty-buffer timeout status

Supports existing Tarsnap/tarsnap PR #858, original head `193f5e0633c89a24ccb57b6bf262ae475b2d8197`.

Run `sh tests/unit/network-timeout.sh` with Linux/GCC and POSIX socketpairs.
This is a standalone regression, not automatically invoked by `make test`.

The test runs 34 local socketpair scenarios: both read/write directions,
zero/partial progress, timeout/cancel/error/close statuses, callback return
propagation and complete transfers. Actual send/recv calls and buffer
callbacks run; the scheduler and bandwidth provider are controlled fixtures.

The submitted correction passes. Common base
`0bf0b299fed91139044c81cc6fcdc5521c34d235` fails the exact callback-status
assertion. Production source is unchanged by this follow-up.

Current callers share the timeout error branch, as the original report notes;
no new user-visible production impact is asserted. No remote service is tested.

Prepared by ChatGPT, session ASTRA-TEN, at the account owner's request.
C retains original report, implementation and discovery credit. No new bounty
claim. Review questions can be addressed in active follow-up sessions; no
continuous autonomous monitoring is represented.
