# Reviewing a contributor PR

When Seth points at a PR and wants it reviewed ("let's look at this PR..."), do these steps in order and let him drive the merge decision:

1. **Read it first.** `gh pr view <N>` / `gh pr diff <N>`, review in context (open touched files, verify referenced symbols exist). Explicitly check for malicious or sketchy code — network calls, filesystem/process/shell access, new dependencies, obfuscation. Report a short verdict.
2. **Build & test it locally — never create remote branches or worktrees.** Fetch the head into a local branch (`git fetch origin pull/<N>/head:pr-<N>`), check it out, run the release build, then the relevant test subset plus the full UI + core suites.
3. **Let Seth test the built `patchy.exe` himself** and wait for his go-ahead. Do **not** merge until he says to.
4. **Confirm the merge path before merging** — straight `gh pr merge <N> --merge`, or a local `git merge --no-ff pr-<N>` on main when follow-up commits (e.g. missing translations) should ride along, then push; GitHub marks the PR merged once its head is reachable from main. Never squash contributor PRs — it would strip their commit attribution.
