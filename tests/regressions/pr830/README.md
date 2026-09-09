# PR830 cache-write cleanup regression

This is additive execution evidence for existing Tarsnap/tarsnap PR830 and
report829. C/Claude authored the report and production correction.
ASTRA-RELAY-COOLDOWN (ChatGPT, an LLM working for the account operator) supplied
these tests and can address concrete review feedback in a subsequent session.
No new bug report, bounty claim, award, payment or upstream merge is asserted.

## Reproduce

Use a configured checkout of the existing `ccache-write-error-paths` branch.
The runner needs Python3.9+, a C compiler and the project's generated `config.h`.
For the sanitizer and late-initialization control use Clang14 or newer.

```sh
autoreconf -i
./configure
python3 tests/regressions/pr830/run.py --output /tmp/pr830-gcc.json
python3 tests/regressions/pr830/run.py --cc clang --output /tmp/pr830-clang.json
python3 tests/regressions/pr830/run.py --cc clang --sanitize --auto-init-pattern --output /tmp/pr830-sanitized.json
git show 0bf0b299fed91139044c81cc6fcdc5521c34d235:tar/ccache/ccache_write.c > /tmp/pr830-before.c
python3 tests/regressions/pr830/controls.py --baseline /tmp/pr830-before.c --output /tmp/pr830-controls
```

Native integration remains the project's ordinary `make -j2`, followed by
`--version` for tarsnap, tarsnap-keygen, tarsnap-keymgmt, tarsnap-keyregen and
tarsnap-recrypt. These commands need no account, server or real archive.
A native build and version checks are not a full `make test` or live-service test.

## Executed local result

The complete unchanged `tar/ccache/ccache_write.c` translation unit is compiled
inside the fixture, rather than copied/reimplemented functions. Both
`ccache_write()` and `ccache_remove()` run against unique temporary directories.
Real Patricia initialization, insertion and traversal supply records; real stdio
writes create cache files. An independent reader checks record count, portable
integer fields, reconstructed prefix/suffix paths, age increment, chunks,
compressed-trailer bytes and EOF. Empty, all-skipped, mixed and changing path
lengths are covered.

The same113 scenarios pass with GCC14.2, Clang17 and Clang17 address/undefined
behavior sanitizers in an isolated Linux runtime. This is113 distinct scenarios,
not339 independent cases. Every candidate scenario has zero tracked outstanding
allocations, zero invalid-free attempts and zero open writer streams.

Exact production source:
- Before: `e6249385097d40b149b6986b9ff8d6f54982cfb7`.
- Original PR830 fix, unchanged: `55f3e58200d435ee5b135740bc4fd83e06b10a36`.

The exact pre-fix source fails25 scenarios:24 leaked path buffers and one invalid
free attempt on the remove/asprintf failure. Independent controls distinguish:

| Control | Failed scenarios | Retained buffers | Invalid-free attempts |
| --- | ---: | ---: | ---: |
| Remove error-path buffer free only | 24 | 24 | 0 |
| Remove the post-free NULL assignment only | 36 | 0 | 36 |
| Restore the old remove/asprintf jump only | 1 | 0 | 1 |
| Move initialization back to the late location | 6 | 0 | 6 |

The last control uses Clang's automatic-storage pattern initialization to make
uninitialized pointer use deterministic. It does not alter production source
beyond relocating the actual initialization. `controls.py` checks each exact
failure count and requires preserved return codes, file closure and expected
contents, rather than accepting any arbitrary process failure as proof.

## Fault and sanitizer boundaries

Allocation and stdio/traversal fault seams are synthetic: injected asprintf,
fopen, malloc, record/count/data traversal, fwrite, flush/sync, fclose, unlink,
rename and directory-sync failures. Successful callbacks, portable encoding and
writer cleanup execute actual production code. The sync seams use local
fflush/fsync and a directory descriptor, not the full platform helper; the
ordinary native build checks linkage separately. No crash-durability claim is
made, and the separate unlink-before-rename behavior is not repaired here.

For asprintf failure, the output pointer is deliberately set to a nonheap sentinel
instead of relying on incidental stack contents. Its failure output is not a
usable allocation. The free seam records an invalid/double-free attempt without
passing it to libc; retained baseline allocations are counted before controlled
cleanup. These are bounded ownership witnesses, not a claimed observed glibc
crash or a raw LeakSanitizer report of the original source.

The unchanged Patricia dependency contains the separately reported signed-shift
issue addressed by PR862. Only its dependency-object shift sanitizer is disabled;
its address/other undefined checks and all writer/harness checks remain enabled.
No production source is changed to compose that separate fix.

Every scenario runs in a bounded subprocess and uses only generated records and
its own temporary cache files. No network request, real key, live cache, owner-PC
operation, sponsor notification or credential is used. JSON retains source hashes,
compiler commands, per-scenario results and exact diagnostic counts. The finite
verification workflow remains on a fork-only support branch, outside this PR.
Hosted validation and artifact receipts are recorded separately after execution.
