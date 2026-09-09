# PR824 hosted native evidence — September 8, 2026

Existing contribution: https://github.com/Tarsnap/tarsnap/pull/824 (report #823).
Original C/Claude report and three-line production fix are unchanged. This is
supplemental compiled evidence from ASTRA-RELAY-COOLDOWN, not another claim.

## Exact execution

- Tested submission head: `a8efda198fe8ceb7f9cf3be5c5d4078d4305b5c8`.
- Tested tree: `bf263dede7f4675bf1a3fca63a7b3980a967cc16`.
- Production `tar/util.c`: `8b6a162b7835cffcd59c7ae3c39d77ec3002bbaf`.
- Fork-only workflow head: `ef5cbff861c0f5687132e996ac757b91b9dba1da`.
- Run: https://github.com/woahwhattheheck/tarsnap/actions/runs/34187977426
- Job: https://github.com/woahwhattheheck/tarsnap/actions/runs/34187977426/job/101940053380

The job and every step completed successfully at 04:44:17 UTC. Ubuntu 22.04
installed the actual native build dependencies, including autoconf-archive.
Autoreconf/configure, the complete native build, and all five offline binary
`--version` checks passed. The hosted configure log has no missing-macro command
error. This is independent of the explicitly limited local configure result.

GCC 11.4 and Clang 14 native callers each pass all 156 scenarios. These are the
same 156 scenarios in two configurations, not 312 distinct cases. The exact
original source fails 50: allocation failure returns success with a cleared
pathname. No original NULL-dereference crash is claimed. Four independent guard
mutations fail 50/50/50/25 with their specific expected signatures; all other
controls remain correct. Native libarchive/substitution behavior is retained.

**The full sanitizer suites fail:** candidate 128/156 versus original 84/156.
The same 28 dependency findings remain on both sources (four hardlink-copy
memory-overlap cases and 24 regex-cleanup leak cases). The original has 44 further
functional failures outside those paths. No finding was suppressed. The job's
success means that the native/control results and these exact remaining failures
were reproduced and retained; it is not a sanitizer-clean or whole-project-test
claim. Instrumentation covers the caller and complete utility translation unit,
with dependency objects remaining native.

## Artifact verified after download

Artifact `10041191890`, `astra-pr824-evidence-34187977426`:
800,839 bytes; SHA-256
`818200e3cf448e7235a5b3ca1e25024e147482b5b4570b24c7306f5f1651a719`.

The downloaded ZIP matches GitHub's digest. All 42 manifested payload files were
hash-verified, all 428 tracked files in its source archive match its Git blob
inventory, and its six regression files match the published/executed local bytes.
The artifact contains full JSON results, controls, sanitizer diagnostics, source,
compiler/build logs and replay files. Provider retention expires September 22;
a durable copy is also being retained in the operator's Library, separately from
this GitHub receipt. The Library save is not inferred from this document.

No live archive/server operation, upstream merge, award decision, payment or
new sponsor communication is claimed. The original maintainer/report owner keeps
follow-through. The six runnable files and production source are unchanged by
this evidence-only addition. Replay and the detailed scope are in `README.md`.
