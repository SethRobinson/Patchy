# Test skips and local fixtures

Split from [testing.md](testing.md): how a skip is classified and recorded, how the release verifiers judge skips against an expected list per host, and the two environment variables for local fixtures (`PATCHY_REQUIRE_FIXTURES`, `PATCHY_LOCAL_FIXTURES_DIR`). The per-platform skip list and its reasons are in [platform-tests.md](platform-tests.md).

A skipped test checked nothing. On October 25, 2026 a worktree cleanup emptied `local-test-fixtures` (the `looks` and `dds` folders never came back), every test that needed them turned into a `[SKIP]`, and the release verifiers, which counted only `[PASS]` and `[FAIL]`, read the runs as clean. Skips are therefore counted, recorded and judged.

## Skip classes and the record

`patchy::test::skip(reason)` (tests/test_harness.hpp) classifies its reason from the wording (`classify_skip_reason`); a test that knows better calls `skip(SkipKind::..., reason)`. The classes:

| Class | Meaning | Typical reasons |
|---|---|---|
| `fixture` | an untracked input is absent | `skip_unless_exists`, "optional fixture missing", "local-test-fixtures", models or plug-ins "not staged" |
| `platform` | the host lacks it | an OS-only feature, a window system, an installed font face, a codec or decoder |
| `feature` | this build leaves it out | "build without ...", "this build has no ...", a `PATCHY_ENABLE_*` option |
| `other` | anything else | an opt-in corpus sweep, a missing companion binary |

The closing line splits the count, leaving out empty classes: `[SUMMARY] executed 2950, passed 2871, failed 0, skipped 79 (fixture 41, platform 30, feature 6, other 2) in 1503.2 s`.

Every finished run writes `test-artifacts/skipped-<suite>.txt` under its working directory (`build\opt-dev\test-artifacts\skipped-patchy_ui_visual_tests.txt`), one `<test name><TAB><reason>` line per skip, sorted by name, empty when nothing skipped. `<suite>` is the binary's name (`TestSuiteOptions::suite_name`, else argv[0]'s stem; the core suite sets `patchy_core_tests`, so the wasm run writes the same name). The file describes the last run, filtered or not.

A test that recorded a failure (`report_failure`, a `guarded()` callback, `when_dialog_visible`) and then skips is counted as failed: the runner prints its `[FAIL]` lines and `  then skipped: <reason>`, so a skip can never hide a failure found before it.

## Expected-skip lists

`verify-windows.ps1` (core and ui steps) and `verify-remote.ps1` (the suites step, which the release rehearsal runs alone with `-SuitesOnly`: [release-process.md](release-process.md#before-the-version-bump)) judge skips through `scripts\release\skip-check.ps1`. For each suite they print its `[SUMMARY]` lines and skip count, then compare the skipped names with:

```
<local fixtures>\expected-skips\<host>-<suite>.txt
```

`<local fixtures>` is `PATCHY_LOCAL_FIXTURES_DIR` when set, else the checkout's `local-test-fixtures` (untracked, so each machine keeps its own lists). `<host>` is the computer name for `verify-windows.ps1` and the ssh host's name up to its first dot for `verify-remote.ps1` (`user@mac-build-host.local` gives `mac-build-host`; an IPv4 address stays whole with dashes for dots, `user@10.0.0.5` gives `10-0-0-5`), lower case. `<suite>` is `patchy_core_tests` or `patchy_ui_visual_tests`.

Format: one test name per line; text after the first tab or space is ignored, as are blank lines and lines starting with `#`. A `skipped-<suite>.txt` record is therefore a valid list as it stands; keep its reasons as the comment column.

- A skip whose test is not listed fails the step with `[FAIL] unexpected skip in <suite>: <test>: <reason> (not in <list>; ...)`. Restore the input, or add the name when the skip is now expected (a deliberately dropped fixture, a host that lost a font).
- A listed test that did not skip prints `<test> is in the expected-skip list but did not skip`: remove the line, it no longer protects anything.
- With no list the step does not fail: it prints every skip, writes the candidate list `build\release-logs\expected-skips-<host>-<suite>.txt`, and prints the `copy` command that installs it. Review each skip before installing a list; a lost fixture folder installed as expected stays hidden.

## PATCHY_REQUIRE_FIXTURES

`PATCHY_REQUIRE_FIXTURES=1` (any value but empty or `0`) fails a fixture-class skip instead of skipping: `skip_unless_exists` throws `required fixture missing (PATCHY_REQUIRE_FIXTURES=1): <path>`, and the runner turns any other `fixture` skip into `[FAIL] <test>: required fixture missing ...`. Set it on a machine that is meant to hold the whole corpus (the release box) to prove nothing went missing; leave it unset on remotes and fresh checkouts, where fixture skips are expected.

## PATCHY_LOCAL_FIXTURES_DIR

`PATCHY_LOCAL_FIXTURES_DIR=<folder>` replaces `<source>/local-test-fixtures` as the root of every local fixture path: `local_fixtures_root()` in tests/local_psd_fixtures.hpp, which `local_psd_fixture_path`, `local_format_fixture_path`, `filter_foundry_fixture_dir`, the composite corpus overlay and the test font candidates (tests/test_fonts.hpp) use. A worktree, which has no untracked files, reads the main checkout's corpus with it instead of a junction (a junction is what a recursive worktree delete follows into the real folder):

```powershell
$env:PATCHY_LOCAL_FIXTURES_DIR = 'D:\path\to\main-checkout\local-test-fixtures'
```

New tests build local paths from `local_fixtures_root()`, never from `source_root_path() / "local-test-fixtures"`. The folder then belongs to another checkout, so nothing writes into it: with the variable set, a missing `local-test-fixtures/composite-corpus/<kind>-digests.txt` overlay baseline is written to `test-artifacts/composite-corpus/local-<kind>-digests.txt` instead, and the run says where to copy it to pin it. Any new code that writes beside a local fixture follows the same rule (`local_fixtures_overridden()`). The perf harness's `startup` baselines are recorded only on request (`PATCHY_PERF_STARTUP_RECORD`) and keep their own path variable, `PATCHY_PERF_STARTUP_BASELINE`.
