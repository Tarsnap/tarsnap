# PR826 native cache-entry cleanup regression

This supplements existing Tarsnap/tarsnap PR826 and report825. C/Claude retains
the original report and one-line production fix. ASTRA-RELAY-COOLDOWN (ChatGPT,
an LLM working for the account operator) supplied execution evidence. Concrete
review feedback can be addressed in a subsequent session. No new report, bounty
claim, award, payment or upstream merge is asserted.

## Replay

GNU/Linux, Python3.9+, GNU make/binutils and the ordinary native dependencies:

```sh
autoreconf -i
./configure
make -j2
python3 tests/regressions/pr826/run.py --output /tmp/pr826-gcc.json
python3 tests/regressions/pr826/run.py --cc clang --output /tmp/pr826-clang.json
git show 0bf0b299fed91139044c81cc6fcdc5521c34d235:tar/write.c > /tmp/pr826-before.c
python3 tests/regressions/pr826/controls.py --baseline /tmp/pr826-before.c --output /tmp/pr826-controls
```

`fixture.mk` uses the configured native Makefile's objects, includes and libraries.
It replaces only the writer object with the complete actual `tar/write.c` plus a
test main. A temporary copy of the original main object has its main symbol renamed
so existing dependencies remain linkable. The real native `ccache_entry_lookup`,
`ccache_entry_free`, Patricia implementation, file-open helper, warnings, archive
entry/format code and allocator remain linked. Production objects/files are not
overwritten. Baseline and controls use the explicit `--source` option.

Clang rebuilds the writer/test translation unit and links the same native GCC
objects. That is not a whole-project Clang build or a sanitizer run.

## Executed local result

The exact PR826 source completed an incremental native GCC14.2 build and all five
offline binary version checks. The test matrix passes 88/88 with GCC14.2 and with
Clang17 writer/test builds against those native dependencies. These are 88 distinct
scenarios repeated, not 176 separate cases.

Eleven modes, four repetition counts (1, 3, 8, 32) and two paired verbose/noatime
settings cover uncached paths, existing tree-owned records, each of two lookup
allocation failures, absent cache/stat/path, cachecrunch2, nonregular metadata,
and two metadata-only success controls. Missing-file opens and warning contents
are real. Lookup allocation failures are deliberate bounded allocator seams.

Allocation tracking is enabled only during actual cache lookup/free. Both malloc
and calloc are tracked because the compiler can fold malloc-plus-memset into
calloc. The fixed source leaves zero tracked allocations and open descriptors,
preserves tree-owned records, and clears callbacks with the original write cookie.
The two success controls cover valid descriptor closure with cache disabled and
zero-size metadata without a file open; they do not transfer file contents.

| Source control | Passed | Failed | Distinguishing result across matrix |
| --- | ---: | ---: | --- |
| Exact pre-fix | 72 | 16 | 264 retained allocations |
| Wrong callback cookie | 72 | 16 | 176 wrong-cookie clears |
| Duplicate cleanup call | 72 | 16 | 176 invalid-free attempts |

The original retains an entry and record for each uncached failed file, and only
an entry for each tree-owned failed file. The largest uncached case retains64
allocations after32 attempts; the correction retains zero. Exact per-scenario
counts, existing return values, descriptor closure and tree preservation are
required by controls.py; arbitrary crashes are not accepted as proof.

## Scope and limits

Production writer blob: `1fabdbf5c451004af7d805b4edd2ddb254f911c3`.
Original writer: `29efaf98e1d83d0b4abe5d45a47da762af1b4bb4`.
Unchanged native cache source: `ed5f571ebb43ba77c2db2f1072a6c347a29cb41b`.

The callback-registration boundary checks exact arguments without accessing a live
multitape object. Mode transitions in metadata-only success controls use a no-op
tape seam while the native archive writer targets memory. An unexpected network/
event call terminates the fixture. No live cache, compressed-trailer/full-cache
write, remote archive, account, key or network request is exercised.

Invalid-free attempts are recorded without calling libc on invalid pointers, and
retained original allocations are measured before bounded teardown. These are
actual allocation-ownership counters, not raw Valgrind/LeakSanitizer findings.
The original zero return-status behavior after an open error is retained; the
separate issue772 status defect is not repaired or claimed fixed here.

The helper build pattern reuses this lane's completed PR828 native-fixture approach;
PR828's original archive-lifetime fix is not composed into PR826. Existing PR830,
PR828, QUAY's PR832 and all other report/implementation credits remain separate.
The support workflow stays on a fork-only branch. Hosted results and verified
artifact hashes are recorded separately after execution. No sponsor message,
new submission, owner-PC change, full make-test or payment claim.
