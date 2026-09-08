# PR832: native cache-reader regression

Operation: `astra-quay-ccache-read-832-20260908`.

This adds executable validation to the existing [PR832](https://github.com/Tarsnap/tarsnap/pull/832)
and [report831](https://github.com/Tarsnap/tarsnap/issues/831). The original
C/Claude report and two production fixes retain their authorship and bounty
attribution. ASTRA-QUAY, an LLM coding assistant working for @woahwhattheheck,
authored these regressions and is available for review follow-through.
This is not another bug report, bounty claim, upstream merge, or payment receipt.

## What is actually executed

`cache_read.c` includes the complete, unmodified production `ccache_read.c`
and links the real repository Patricia tree, `asprintf`, and warning code.
It calls `ccache_read()` and `ccache_free()` on actual temporary cache files.
The Python runner creates valid serialized records, chunk-header payloads and
zlib-compressed trailer bytes, as well as deliberately malformed inputs.
Successful reads verify record counts, metadata, chunk/trailer lengths and
trailer byte sums. This is not a `ccache_write()` round trip or a whole-client
build, and no server, account, keys, real backup data or network is involved.

The fixture tracks allocations made by the reader and observes outstanding
allocations **before** cleaning its own leftovers. That makes the original
leak a deterministic assertion failure rather than relying on an eventual
process-wide leak report. Real `fopen`/`fread`/`fclose` and the real Patricia
implementation still run. Reader-local allocation failures are explicit;
`patricia_insert()` failures are injected at its API boundary before insertion,
not manufactured by replacing the tree with a mock. The duplicate-key failure
uses the real tree with no insertion injection.

The suite contains 22 scenarios: missing/empty caches, two valid zero-byte
allocation behaviors, one/multiple records, chunks and trailers, truncated
count/record/path/payload, invalid record fields, duplicate keys, first/middle/
last insertion failures, and context/record/data/path allocation failures.
Read and memory-mapped modes are explicitly compiled and checked. No assertion
is skipped to accommodate the original defects.

## Measured result

[Hosted run34181793420](https://github.com/woahwhattheheck/tarsnap/actions/runs/34181793420)
and job101922199919 completed successfully. The coordinator verifies that all
14 configurations match their declared outcomes; this does **not** mean every
underlying invocation passed. Negative controls and the inherited sanitizer
failure remain explicit failed invocations in the reports.

Environment: Ubuntu24.04, Python3.11.16, GCC13.3.0 and Clang18.1.3. The native
C99 builds use `-O1 -Wall -Wextra -Werror`; configured feature definitions are
applied to every translation unit. The measured cache-record size is88 bytes
and chunk-header size is40 bytes.

| Reader/dependency/configuration | Normal read | Memory mapped |
| --- | --- | --- |
| PR832, GCC, unchanged Patricia | 22/22 pass | 22/22 pass |
| PR832, Clang, unchanged Patricia | 22/22 pass | 22/22 pass |
| Actual original reader, GCC | 16/22 pass; 6 expected failures | 18/22 pass; 4 expected failures |
| PR832 with only record-free removed | 18/22 pass; 4 expected failures | 18/22 pass; 4 expected failures |
| PR832 with only empty-read guard removed | 20/22 pass; 2 expected failures | 22/22 pass |
| PR832, Clang ASan+UBSan, unchanged Patricia | 19/22 pass; 3 inherited shift failures | 19/22 pass; 3 inherited shift failures |
| PR832 + exact PR862 Patricia, Clang ASan+UBSan | 22/22 pass | 22/22 pass |

These are 22 distinct scenarios replayed across configurations, not hundreds
of distinct tests. There are88 successful standalone native-case executions
and44 successful sanitized composition-case executions. Additional runs
retain the original/reverted failures and the unchanged-dependency diagnostics.

### The two original defects are independently distinguished

With the actual original reader, an empty four-byte cache fails in normal-read
mode and performs one zero-sized `fread`, both with the native `malloc(0)`
behavior and when that allocation returns NULL. The fixed reader succeeds and
performs zero such reads. The mmap path stays successful.

The original reader leaves exactly one88-byte cache-record allocation after
real duplicate-key rejection and after each of the three injected insertion
failures. The fixed reader leaves none. Previously inserted records and open
file handles are cleaned in both versions. Removing only the new `free(ccr)`
reproduces those four leaks, while removing only the data-length guard
reproduces just the two non-mmap empty-read failures.

### Sanitizer dependency is kept separate

The unchanged Patricia implementation has the already-reported
[issue861](https://github.com/Tarsnap/tarsnap/issues/861): a negative left shift
at `lib/datastruct/patricia.c:357`. Full ASan+UBSan therefore exits nonzero in
`three-records`, `mixed-chunks-and-trailers`, and `insert-failure-last` in each
mode. The coordinator requires those exact case names and the matching shift
diagnostic; the runner itself still returns failure. No sanitizer is disabled
and no check is relabeled as passing.

The separately labeled composition substitutes only the exact Patricia source
from [PR862](https://github.com/Tarsnap/tarsnap/pull/862), commit
`1bc8e0b53e823280db53dae2f3c110c3fc8cbd15`. All22 cases then pass under ASan+UBSan
in each mode. That source is read from Git into the evidence directory; it is
not added to this PR or silently substituted by the default runner. Original
C/Claude and ASTRA-TEN credits for PR862 remain theirs. An unmodified PR832
checkout is not claimed to be globally sanitizer-clean.

## Reproduce

From a checkout of this contribution with GCC/Clang and ordinary project build
prerequisites installed:

```sh
autoreconf -i
./configure
python3 tests/regressions/pr832/run.py --cc gcc --mode read --output /tmp/pr832-read
python3 tests/regressions/pr832/run.py --cc clang --mode mmap --output /tmp/pr832-mmap
```

Ubuntu configuration requires `libext2fs-dev` in addition to the standard
compiler/autotools and project libraries. This is a runner prerequisite, not
a source change. `--source` selects a particular reader file for original or
mutation controls. Without overrides the actual current repository sources
are used. Every output report records full input-file Git blob and SHA256
identities, compilation flags, outcomes, and diagnostics.

To observe the inherited sanitizer problem, add `--sanitize`; the invocation
must remain nonzero until the separate Patricia fix is present. To replay the
explicitly labeled composition, first make the PR862 commit available in the
local Git object database and then run:

```sh
git show 1bc8e0b53e823280db53dae2f3c110c3fc8cbd15:lib/datastruct/patricia.c > /tmp/pr862-patricia.c
python3 tests/regressions/pr832/run.py --cc clang --mode read --sanitize \
  --patricia-source /tmp/pr862-patricia.c --output /tmp/pr832-with-pr862-read
python3 tests/regressions/pr832/run.py --cc clang --mode mmap --sanitize \
  --patricia-source /tmp/pr862-patricia.c --output /tmp/pr832-with-pr862-mmap
```

## Source and evidence identity

The hosted source checkout was `ad1df1452f5878f68e1bf1559c70d8793cfc74f0`.
The [finite verification workflow](https://github.com/woahwhattheheck/tarsnap/blob/5b3e18d5a59b3966f969d4a48511fd122d265485/.github/workflows/astra-quay-pr832.yml)
lives on a separate review branch and is not part of the upstream PR.
Only the tested fixture, runner and this explanation are added here; both
production source files remain untouched by this validation contribution.

Exact Git blobs verified before execution and again after artifact download:

| Input | Git blob |
| --- | --- |
| Current `ccache_read.c` | `0156cdeea371dd535a1b7f6e49836b439284aa92` |
| Original reader from `0bf0b299fed91139044c81cc6fcdc5521c34d235` | `18afc8338dc5676a4d2cf177af4f740f9308bf08` |
| Unchanged `patricia.c` | `5347cd0c9e479aa159015290c5afd0d472667a65` |
| Separate PR862 dependency | `74313d08070d4acc24d0aa64d12ccb484c1d6ec5` |
| `cache_read.c` fixture | `bb0475dcb30e0323e63df282a726da1feb9f891d` |
| `run.py` | `05f8d9e95caf4c602bc4105b9843bd4b39edb51a` |

[Artifact10039162311](https://github.com/woahwhattheheck/tarsnap/actions/runs/34181793420/artifacts/10039162311),
`astra-quay-pr832-native`, contains the source inputs, executable regressions,
real cache-file fixtures, all compile/runtime logs and structured reports.
Its downloaded400,240-byte ZIP matches the GitHub SHA256 digest:

`cc9b2ccf372cada2b7380174535a685ffdc6a440f5734df1e20a7430925b41da`

All991 entries in the evidence SHA256 manifest were independently verified;
the ZIP contains1011 members including the source/fixture files outside that
manifest. Artifact retention is30 days; the pinned runner and workflow remain
available in Git. Earlier setup failures (missing ext2fs headers and missing
feature definitions) ran no test cases. A subsequent run exposed the retained
Patricia sanitizer issue; none of those earlier runs is represented as green.
