<#
.SYNOPSIS
Removes a linked git worktree (and optionally its branch) without ever deleting what a junction or symlink
inside it points at.

.DESCRIPTION
On Windows both `git worktree remove` and `Remove-Item -Recurse` follow a directory junction and delete the
contents of its target. A worktree with a `.deps` or `local-test-fixtures` junction to the main checkout therefore
takes the main checkout's real folders with it. This script is the one way agents and tools remove a worktree.

In order, stopping at the first problem:
  1. Refuses (exit 2) anything that is not a linked worktree of its repository, is locked, has uncommitted or
     untracked files, or has commits not in -Base.
  2. Refuses (exit 2) a worktree some process still holds a file or folder open in: the folder is renamed and
     renamed back, which Windows refuses while anything inside is open.
  3. Copies build\issue-shots to <ArchiveShots>\<worktree name> when -ArchiveShots is given.
  4. Finds every junction and symlink with a walk that never enters one and removes each as a link only, checks
     that its target still exists, and rescans: none may remain.
  5. Runs `git worktree remove` without --force, then deletes the branch with -DeleteBranch (only when its tip
     is in -Base).
The -Watch folders (default: the main checkout's .deps and local-test-fixtures) are fingerprinted before and
after steps 4 and 5; any change is an error.

Exit codes: 0 removed (or would be, with -DryRun), 2 skipped as not safe (nothing changed), 1 error.
The last line printed is `RESULT removed|skipped|error <detail>`.

.EXAMPLE
pwsh -File scripts\dev\remove-worktree-safely.ps1 -Path .claude\worktrees\my-topic -DeleteBranch
#>
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [string]$Base = 'main',
    [switch]$DeleteBranch,
    [string]$ArchiveShots,
    [string[]]$Watch,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'

function Done([int]$code, [string]$kind, [string]$detail) {
    "RESULT $kind $detail"
    exit $code
}

function Find-Links([string]$root) {
    # Depth-first walk that records a reparse point and never enters it.
    $found = New-Object System.Collections.Generic.List[string]
    $stack = New-Object System.Collections.Stack
    $stack.Push($root)
    while ($stack.Count -gt 0) {
        $dir = $stack.Pop()
        foreach ($e in [System.IO.Directory]::GetFileSystemEntries($dir)) {
            $a = [System.IO.File]::GetAttributes($e)
            if (($a -band [IO.FileAttributes]::ReparsePoint) -ne 0) { $found.Add($e) }
            elseif (($a -band [IO.FileAttributes]::Directory) -ne 0) { $stack.Push($e) }
        }
    }
    return , $found
}

function Get-Fingerprint([string[]]$dirs) {
    $lines = foreach ($d in $dirs) {
        if (-not (Test-Path -LiteralPath $d)) { "$d=missing"; continue }
        $top = @([System.IO.Directory]::GetFileSystemEntries($d))
        "$d=$($top.Count)"
        foreach ($s in $top) {
            if ([System.IO.Directory]::Exists($s)) { "$s=$(@([System.IO.Directory]::GetFileSystemEntries($s)).Count)" }
        }
    }
    return ($lines -join "`n")
}

function Invoke-Git([string]$Repo, [string[]]$GitArgs) {
    # Windows PowerShell 5.1 turns redirected native stderr into errors under 'Stop'; read it as text instead.
    $ErrorActionPreference = 'Continue'
    $out = & git.exe -C $Repo @GitArgs 2>&1 | ForEach-Object { "$_" }
    $script:gitCode = $LASTEXITCODE
    return $out
}

try {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) { Done 2 'skipped' "not a folder: $Path" }
    $wt = (Resolve-Path -LiteralPath $Path).ProviderPath.TrimEnd('\', '/')
    $name = Split-Path $wt -Leaf

    $dotGit = Join-Path $wt '.git'
    if (-not (Test-Path -LiteralPath $dotGit -PathType Leaf)) { Done 2 'skipped' "not a linked worktree (no .git file): $wt" }
    $common = (Invoke-Git $wt @('rev-parse', '--path-format=absolute', '--git-common-dir') | Select-Object -First 1)
    if ($gitCode -ne 0) { Done 2 'skipped' "git cannot read $wt" }
    $main = Split-Path ($common -replace '/', '\') -Parent
    if ($main -ieq $wt) { Done 2 'skipped' "the main checkout is never removed: $wt" }

    # The worktree must be one git lists, by this exact path.
    $entry = $null; $cur = $null
    foreach ($line in (Invoke-Git $main @('worktree', 'list', '--porcelain'))) {
        if ($line -like 'worktree *') { $cur = @{ path = ($line.Substring(9) -replace '/', '\'); branch = ''; locked = $false } }
        elseif ($line -like 'branch *' -and $cur) { $cur.branch = $line.Substring(7) -replace '^refs/heads/', '' }
        elseif ($line -like 'locked*' -and $cur) { $cur.locked = $true }
        if ($cur -and $cur.path -ieq $wt) { $entry = $cur }
    }
    if (-not $entry) { Done 2 'skipped' "git does not list $wt as a worktree" }
    if ($entry.locked) { Done 2 'skipped' "the worktree is locked (git worktree lock): $wt" }
    $branch = $entry.branch

    $head = (Invoke-Git $wt @('rev-parse', 'HEAD') | Select-Object -First 1)
    $ahead = (Invoke-Git $main @('rev-list', '--count', "$Base..$head") | Select-Object -First 1)
    if ($gitCode -ne 0) { Done 2 'skipped' "cannot compare with $Base" }
    if ([int]$ahead -ne 0) { Done 2 'skipped' "$ahead commit(s) not in $Base" }
    $dirty = @(Invoke-Git $wt @('status', '--porcelain', '--untracked-files=normal'))
    if ($gitCode -ne 0) { Done 2 'skipped' "git status failed in $wt" }
    if ($dirty.Count -ne 0) { Done 2 'skipped' "$($dirty.Count) uncommitted or untracked entries" }

    if (-not $Watch) { $Watch = @((Join-Path $main '.deps'), (Join-Path $main 'local-test-fixtures')) }
    $Watch = @($Watch | Where-Object { Test-Path -LiteralPath $_ })

    $links = Find-Links $wt
    if ($DryRun) {
        foreach ($l in $links) { "would unlink $l" }
        Done 0 'removed' "dry run: $wt ($($links.Count) links) branch=$branch"
    }

    # In-use probe: Windows refuses to rename a folder while anything inside it is open.
    $probe = "$wt.rmcheck"
    try { Rename-Item -LiteralPath $wt -NewName (Split-Path $probe -Leaf) }
    catch { Done 2 'skipped' "in use (a process holds something open inside it)" }
    Rename-Item -LiteralPath $probe -NewName $name

    if ($ArchiveShots) {
        $shots = Join-Path $wt 'build\issue-shots'
        if (Test-Path -LiteralPath $shots) {
            $dest = Join-Path $ArchiveShots $name
            $null = & robocopy $shots $dest /E /XJ /XO /R:1 /W:1 /NFL /NDL /NJH /NJS /NP
            if ($LASTEXITCODE -ge 8) { Done 1 'error' "copying issue shots failed (robocopy $LASTEXITCODE)" }
        }
    }

    $before = Get-Fingerprint $Watch
    foreach ($l in $links) {
        $item = Get-Item -LiteralPath $l -Force
        $target = @($item.Target) | Select-Object -First 1
        $targetExisted = $target -and (Test-Path -LiteralPath $target)
        if ($item.PSIsContainer) { & cmd /c rmdir "$l" } else { & cmd /c del /f /q /a "$l" }
        if (Test-Path -LiteralPath $l) { Done 1 'error' "could not remove the link $l" }
        if ($targetExisted -and -not (Test-Path -LiteralPath $target)) { Done 1 'error' "STOP: link target vanished: $target" }
        "unlinked $l -> $target"
    }
    if ((Find-Links $wt).Count -ne 0) { Done 1 'error' "links remain in $wt after unlinking" }
    if ((Get-Fingerprint $Watch) -ne $before) { Done 1 'error' "STOP: a watched folder changed while unlinking" }

    $out = Invoke-Git $main @('worktree', 'remove', $wt)
    if ($gitCode -ne 0) { Done 1 'error' "git worktree remove failed: $($out -join ' ')" }
    if ((Get-Fingerprint $Watch) -ne $before) { Done 1 'error' "STOP: a watched folder changed during git worktree remove" }

    if ($DeleteBranch -and $branch) {
        $tip = (Invoke-Git $main @('rev-parse', "refs/heads/$branch") | Select-Object -First 1)
        $null = Invoke-Git $main @('merge-base', '--is-ancestor', $tip, $Base)
        if ($gitCode -eq 0) {
            $null = Invoke-Git $main @('branch', '-D', $branch)
            if ($gitCode -ne 0) { Done 1 'error' "removed $wt but could not delete branch $branch" }
            "deleted branch $branch (was $($tip.Substring(0, 9)))"
        } else {
            "kept branch ${branch}: its tip is not in $Base"
        }
    }
    Done 0 'removed' "$wt branch=$branch links=$($links.Count)"
}
catch {
    Done 1 'error' $_.Exception.Message
}
