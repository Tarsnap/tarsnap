# AGENTS.md


## Modifying files

- Do not make stylistic or spelling fixes to:
  - anything inside `libarchive/` or `libcperciva/external/`
  - `tar/bsdtar.c`
  - `tar/cmdline.c`
  - `tar/getdate.c`
  - `tar/matching.c`
  - `tar/read.c`
  - `tar/siginfo.c`
  - `tar/subst.c`
  - `tar/tree.c`
  - `tar/util.c`
  - `tar/write.c`
- If there are substantive errors in those files, fixes are welcome.


## Scope

- Prefer minimal, targeted changes.
- Avoid refactoring unrelated code while fixing a bug.


## Communication

- When submitting an issue or PR, clearly identify yourself as an LLM in the
  description.
- Specify whether you are available to discuss problems or code changes, or
  whether you are simply reporting an issue and will not be responding further.


## Bug bounties

- Bug bounty rules are available at https://www.tarsnap.com/bugbounty.html.
- In particular, bounties worth less than $100 are paid as Tarsnap account
  credits, not cash.
- We will never send any cryptocurrency, so do not post any wallet info.
- Stylistic or spelling fixes in upstream code (such as `libarchive/`) are not
  eligible for bug bounties.
- Most PRs require multiple rounds of review and revision in response to
  feedback, so do not submit PRs if you are unable to communicate with
  reviewers.
- We pay bug bounties for reporting bugs, not for providing patches.
  Some bugs are more easily explained by a patch, so by all means include
  code in a bug report.
- To emphasize the last point: if person A reports a bug and person B sends
  in a PR to fix that bug, person A gets a bounty and person B gets nothing.
