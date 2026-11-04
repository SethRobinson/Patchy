<#
.SYNOPSIS
Self-test for remove-worktree-safely.ps1. Builds a scratch repository under %TEMP%\patchy-rmwt-<random> whose
worktrees hold junctions to the scratch main checkout's .deps and local-test-fixtures, runs every case, and checks
that the junction targets keep every file. Exit 0 when all cases pass.

.EXAMPLE
pwsh -File scripts\dev\remove-worktree-safely.test.ps1
#>
$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot 'remove-worktree-safely.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) ('patchy-rmwt-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$failures = New-Object System.Collections.Generic.List[string]

function Check([bool]$ok, [string]$what) {
    if ($ok) { "  ok    $what" } else { "  FAIL  $what"; $script:failures.Add($what) }
}
function G { $ErrorActionPreference = 'Continue'; & git @args 2>&1 | ForEach-Object { "$_" }; if ($LASTEXITCODE) { throw "git $args failed" } }
function Run-Remove([string[]]$extra) {
    $ps = (Get-Process -Id $PID).Path
    $out = & $ps -NoProfile -File $script @extra 2>&1 | ForEach-Object { "$_" }
    return @{ code = $LASTEXITCODE; out = ($out -join "`n") }
}
function Count-Files([string]$dir) { @(Get-ChildItem -LiteralPath $dir -Recurse -File -Force).Count }
function New-Junction([string]$link, [string]$target) {
    New-Item -ItemType Directory -Force (Split-Path $link -Parent) | Out-Null
    & cmd /c mklink /J "$link" "$target" | Out-Null
    if (-not (Test-Path -LiteralPath $link)) { throw "mklink /J $link failed" }
}

$repo = Join-Path $root 'repo'
try {
    New-Item -ItemType Directory -Force $repo | Out-Null
    G -C $repo init -q -b main
    G -C $repo config user.email test@example.invalid
    G -C $repo config user.name test
    Set-Content (Join-Path $repo '.gitignore') ".deps/`nlocal-test-fixtures/`nbuild/`n"
    Set-Content (Join-Path $repo 'a.txt') 'a'
    G -C $repo add -A
    G -C $repo commit -q -m init
    $deps = Join-Path $repo '.deps'
    $fixtures = Join-Path $repo 'local-test-fixtures'
    foreach ($d in $deps, $fixtures) {
        New-Item -ItemType Directory -Force (Join-Path $d 'sub') | Out-Null
        1..5 | ForEach-Object { Set-Content (Join-Path $d "f$_.txt") "x$_"; Set-Content (Join-Path $d "sub\g$_.txt") "y$_" }
    }
    $depsCount = Count-Files $deps; $fixCount = Count-Files $fixtures
    $wtDir = Join-Path $repo '.claude\worktrees'

    function New-Wt([string]$name) {
        $p = Join-Path $wtDir $name
        G -C $repo worktree add -q -b "work/$name" $p main
        New-Junction (Join-Path $p '.deps') $deps
        New-Junction (Join-Path $p 'local-test-fixtures') $fixtures
        New-Junction (Join-Path $p 'build\deep\nested\deps-link') $deps
        New-Item -ItemType Directory -Force (Join-Path $p 'build\issue-shots\123') | Out-Null
        Set-Content (Join-Path $p 'build\issue-shots\123\after.png') 'png'
        return $p
    }

    "case: merged, clean worktree with three junctions is removed and the targets keep every file"
    $p = New-Wt 'merged'
    $archive = Join-Path $root 'shots'
    $r = Run-Remove @('-Path', $p, '-DeleteBranch', '-ArchiveShots', $archive)
    Check ($r.code -eq 0) "exit 0 (got $($r.code)): $($r.out)"
    Check (-not (Test-Path -LiteralPath $p)) 'worktree folder gone'
    Check ((Count-Files $deps) -eq $depsCount) '.deps keeps every file'
    Check ((Count-Files $fixtures) -eq $fixCount) 'local-test-fixtures keeps every file'
    Check ([string]::IsNullOrEmpty(((G -C $repo branch --list 'work/merged') -join ''))) 'branch deleted'
    Check (Test-Path -LiteralPath (Join-Path $archive 'merged\123\after.png')) 'issue shots archived'

    "case: dry run changes nothing"
    $p = New-Wt 'dry'
    $r = Run-Remove @('-Path', $p, '-DryRun')
    Check ($r.code -eq 0) "exit 0 (got $($r.code))"
    Check ((Test-Path -LiteralPath $p) -and (Test-Path -LiteralPath (Join-Path $p '.deps\f1.txt'))) 'worktree and its junction still there'

    "case: uncommitted change is skipped"
    $p = New-Wt 'dirty'
    Set-Content (Join-Path $p 'a.txt') 'changed'
    $r = Run-Remove @('-Path', $p, '-DeleteBranch')
    Check ($r.code -eq 2) "exit 2 (got $($r.code))"
    Check (Test-Path -LiteralPath (Join-Path $p '.deps\f1.txt')) 'junction left in place'

    "case: a commit not in main is skipped"
    $p = New-Wt 'ahead'
    Set-Content (Join-Path $p 'b.txt') 'b'
    G -C $p add b.txt
    G -C $p commit -q -m ahead
    $r = Run-Remove @('-Path', $p, '-DeleteBranch')
    Check ($r.code -eq 2) "exit 2 (got $($r.code))"
    Check (-not [string]::IsNullOrEmpty(((G -C $repo branch --list 'work/ahead') -join ''))) 'branch kept'

    "case: a file held open inside is skipped"
    $p = New-Wt 'inuse'
    $held = [IO.File]::Open((Join-Path $p 'build\issue-shots\123\after.png'), 'Open', 'Read', 'Read')
    try { $r = Run-Remove @('-Path', $p) } finally { $held.Close() }
    Check ($r.code -eq 2) "exit 2 (got $($r.code))"
    Check (Test-Path -LiteralPath (Join-Path $p '.deps\f1.txt')) 'junction left in place'

    "case: the main checkout is refused"
    $r = Run-Remove @('-Path', $repo)
    Check ($r.code -eq 2) "exit 2 (got $($r.code))"
    Check (Test-Path -LiteralPath (Join-Path $repo 'a.txt')) 'main checkout untouched'

    "case: a folder git does not list is refused"
    $stray = Join-Path $wtDir 'stray'
    New-Item -ItemType Directory -Force $stray | Out-Null
    $r = Run-Remove @('-Path', $stray)
    Check ($r.code -eq 2) "exit 2 (got $($r.code))"

    Check ((Count-Files $deps) -eq $depsCount) 'after every case: .deps keeps every file'
    Check ((Count-Files $fixtures) -eq $fixCount) 'after every case: local-test-fixtures keeps every file'
}
finally {
    # Unlink every junction left in the scratch tree before deleting it (the hazard this script exists for).
    if ($root -like '*\patchy-rmwt-*' -and (Test-Path -LiteralPath $root)) {
        $stack = New-Object System.Collections.Stack; $stack.Push($root)
        $links = New-Object System.Collections.Generic.List[string]
        while ($stack.Count) {
            foreach ($e in [IO.Directory]::GetFileSystemEntries($stack.Pop())) {
                $a = [IO.File]::GetAttributes($e)
                if ($a -band [IO.FileAttributes]::ReparsePoint) { $links.Add($e) } elseif ($a -band [IO.FileAttributes]::Directory) { $stack.Push($e) }
            }
        }
        foreach ($l in $links) { & cmd /c rmdir "$l" }
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}
if ($failures.Count) { "$($failures.Count) check(s) failed"; exit 1 }
"all checks passed"
exit 0
