# PR828 native append-input lifetime regression

This supplements existing Tarsnap/tarsnap PR828 and report827. C/Claude retains
the original report and two-line production fix. ASTRA-RELAY-COOLDOWN (ChatGPT,
an LLM working for the account operator) supplied tests and execution evidence;
concrete review feedback can be addressed in a subsequent session. No new bug
report, bounty claim, award, payment or upstream merge is asserted.

## Run

GNU/Linux, Python3.9+, GNU make/binutils and the normal native build dependencies
are required. Configure and build the existing contribution branch first:

```sh
autoreconf -i
./configure
make -j2
python3 tests/regressions/pr828/run.py --output /tmp/pr828-gcc.json
python3 tests/regressions/pr828/run.py --cc clang --output /tmp/pr828-clang.json
git show 0bf0b299fed91139044c81cc6fcdc5521c34d235:tar/write.c > /tmp/pr828-before.c
python3 tests/regressions/pr828/controls.py --baseline /tmp/pr828-before.c --output /tmp/pr828-controls
```

`fixture.mk` reuses the configured native Makefile's exact objects, include flags
and libraries. It replaces only the write object with a fixture that includes the
complete actual `tar/write.c`, and renames main in a temporary copy of the native
main object so its existing dependencies remain linkable. No production file,
object or build configuration is replaced. `--source` compiles an explicit
baseline/control source; no function is extracted or copied into a substitute.

The optional `--cc clang` rebuilds the changed translation unit and test entrypoint
with Clang and links the same native dependencies. It does not claim a whole
project Clang rebuild or sanitizer coverage.

## What actually runs

Both static append entrypoints run with the real bundled archive allocator,
format readers, error strings, finish/free code, warnings and native project
objects. Missing local files reach the early-open failure. Invalid files and
directories reach the later read-error path and serve as unchanged controls.
Empty local tar archives exercise successful cleanup without a data-copy loop.

The Tarsnap open boundary alone is synthetic: it either sets a real archive error
and returns NULL or opens an in-memory empty tar and returns a fixture cookie.
No remote archive, account, credential or network request is used. A network-select
call would terminate the fixture as a failed scenario. This is not live-server or
full-command-line argument-processing evidence.

Link-time wrappers count real reader allocation/release and retain actual warning
contents. They also detect error-string access after release and duplicate finish
attempts, avoiding an actual invalid dereference/free in deliberate controls.
Outstanding original-source readers are measured before bounded teardown.
These counters are not claimed to be raw LeakSanitizer or Valgrind output.

The 36 scenarios cover nine local/remote-boundary/success/recovery/mixed modes,
each repeated 1, 3, 8 and 32 times. Return values stay zero for recoverable input
errors while the sticky process status becomes one. Warnings retain their real
error contents. Every candidate run ends with zero live readers, exactly one
finish per allocation, no late error-string read, no duplicate finish, and peak
live-reader count one. Mixed sequences contain both failures and later successes.

## Executed local results

The full local native project built with GCC14.2. All 36 scenarios pass with
GCC14.2 and Clang17 caller/test builds against those native dependencies.
The same scenarios are repeated, not 72 distinct cases.

| Source control | Passed | Failed |
| --- | ---: | ---: |
| Exact pre-fix source | 16 | 20 |
| Remove only local-file cleanup | 24 | 12 |
| Remove only Tarsnap-open cleanup | 24 | 12 |
| Move the added frees before error-string consumption | 16 | 20 |
| Duplicate the added frees | 16 | 20 |

The original accumulates one actual reader per early-open failure; the largest
mixed case retains 64 readers after 160 append attempts. The corrected source
releases every reader. Late read-error controls remain passing on the original.
Each cleanup line is independently necessary, and its ordering is independently
checked. `controls.py` requires exact per-scenario retained-reader, late-read and
double-finish counts, not merely any process failure.

Source blobs: fixed `5511a8effd072804af97988666d0915e26c02100`; before
`29efaf98e1d83d0b4abe5d45a47da762af1b4bb4`. The fixed production file is unchanged.

Hosted execution and downloaded-artifact receipts are recorded separately after
verification. The support workflow is fork-only and never enters this upstream
PR. Original contributor ownership, unrelated PR826, peer PR832 and all previous
validated submissions remain intact. No owner-PC activity, sponsor notification,
new report, bounty submission, acceptance, payment or full make-test claim.
