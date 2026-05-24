param(
    [string]$BuildDir = "build",
    [string]$Configuration = "Release",
    [string]$OutputDir = "artifacts\release",
    [string]$Version = "",
    [string]$Platform = "windows-x64",
    [switch]$SkipSmoke,
    [switch]$SkipQtDeploy
)

$ErrorActionPreference = "Stop"

function Resolve-RepoRoot {
    $scriptRoot = Split-Path -Parent $PSCommandPath
    return (Resolve-Path (Join-Path $scriptRoot "..")).Path
}

function Resolve-Exe {
    param(
        [string]$BuildRoot,
        [string]$Config,
        [string]$Name
    )

    $candidates = @(
        (Join-Path $BuildRoot "$Config\$Name"),
        (Join-Path $BuildRoot $Name)
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "Required executable was not found: $Name under $BuildRoot"
}

function Resolve-GitValue {
    param([string[]]$Arguments)

    try {
        $value = & git @Arguments 2>$null
        if ($LASTEXITCODE -eq 0) {
            return ($value | Select-Object -First 1)
        }
    } catch {
    }
    return ""
}

function Resolve-WindeployQt {
    $command = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $qmake = Get-Command qmake6.exe -ErrorAction SilentlyContinue
    if (-not $qmake) {
        $qmake = Get-Command qmake.exe -ErrorAction SilentlyContinue
    }
    if ($qmake) {
        $qtBins = & $qmake.Source -query QT_INSTALL_BINS
        if ($LASTEXITCODE -eq 0 -and $qtBins) {
            $candidate = Join-Path $qtBins "windeployqt.exe"
            if (Test-Path -LiteralPath $candidate) {
                return (Resolve-Path -LiteralPath $candidate).Path
            }
        }
    }

    if ($env:QT_ROOT_DIR) {
        $candidate = Join-Path $env:QT_ROOT_DIR "bin\windeployqt.exe"
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "windeployqt.exe was not found. Add Qt bin to PATH or set QT_ROOT_DIR."
}

function Write-TextFile {
    param(
        [string]$Path,
        [string]$Content
    )
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Content, $utf8NoBom)
}

$repoRoot = Resolve-RepoRoot
$buildRoot = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRoot $BuildDir }
$outputRoot = if ([System.IO.Path]::IsPathRooted($OutputDir)) { $OutputDir } else { Join-Path $repoRoot $OutputDir }
$resolvedOutput = New-Item -ItemType Directory -Force -Path $outputRoot
$resolvedOutputPath = $resolvedOutput.FullName

if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = Resolve-GitValue @("describe", "--tags", "--always", "--dirty")
}
if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = "dev"
}

$safeVersion = $Version -replace "[^A-Za-z0-9_.-]", "-"
$packageName = "fusiondesk-clip-$Platform-$safeVersion"
$stageRoot = Join-Path $resolvedOutputPath $packageName
$archivePath = Join-Path $resolvedOutputPath "$packageName.zip"

$resolvedOutputFull = (Resolve-Path -LiteralPath $resolvedOutputPath).Path
$stageParent = Split-Path -Parent $stageRoot
if ((Resolve-Path -LiteralPath $stageParent).Path -ne $resolvedOutputFull) {
    throw "Refusing to stage outside output directory: $stageRoot"
}

if (Test-Path -LiteralPath $stageRoot) {
    Remove-Item -LiteralPath $stageRoot -Recurse -Force
}
if (Test-Path -LiteralPath $archivePath) {
    Remove-Item -LiteralPath $archivePath -Force
}

$binDir = Join-Path $stageRoot "bin"
$sampleDir = Join-Path $stageRoot "samples"
$toolsDir = Join-Path $stageRoot "tools"
New-Item -ItemType Directory -Force -Path $binDir, $sampleDir, $toolsDir | Out-Null

$clipExe = Resolve-Exe $buildRoot $Configuration "fusiondesk_clip.exe"
$profilePlanExe = Resolve-Exe $buildRoot $Configuration "fusiondesk_pc_profile_plan.exe"

Copy-Item -LiteralPath $clipExe -Destination $binDir
Copy-Item -LiteralPath $profilePlanExe -Destination $binDir
Copy-Item -LiteralPath (Join-Path $repoRoot "tools\clipboard_windows_validation.ps1") -Destination $toolsDir

if (-not $SkipQtDeploy) {
    $windeployqt = Resolve-WindeployQt
    foreach ($exe in @("fusiondesk_clip.exe", "fusiondesk_pc_profile_plan.exe")) {
        & $windeployqt --release --no-translations (Join-Path $binDir $exe)
        if ($LASTEXITCODE -ne 0) {
            throw "windeployqt failed for $exe"
        }
    }
}

Write-TextFile -Path (Join-Path $stageRoot "fusiondesk_clip_agent.cmd") -Content @"
@echo off
"%~dp0bin\fusiondesk_clip.exe" --clip-role agent %*
"@

Write-TextFile -Path (Join-Path $stageRoot "fusiondesk_clip_client.cmd") -Content @"
@echo off
"%~dp0bin\fusiondesk_clip.exe" --clip-role client %*
"@

$commit = Resolve-GitValue @("rev-parse", "--short", "HEAD")
$manifest = @"
name=fusiondesk-clip
platform=$Platform
version=$Version
commit=$commit
configuration=$Configuration
createdUtc=$([System.DateTimeOffset]::UtcNow.ToString("u"))
"@
Write-TextFile -Path (Join-Path $stageRoot "manifest.txt") -Content $manifest

$readme = @"
FusionDesk clipboard CLI release package

Contents:
- bin/fusiondesk_clip.exe
- bin/fusiondesk_pc_profile_plan.exe
- fusiondesk_clip_agent.cmd
- fusiondesk_clip_client.cmd
- tools/clipboard_windows_validation.ps1

Smoke:
  bin\fusiondesk_clip.exe --clip-role agent --smoke
  bin\fusiondesk_clip.exe --clip-role client --smoke

Generate paired local TCP profiles:
  bin\fusiondesk_pc_profile_plan.exe --client-profile samples\client.json --agent-profile samples\agent.json --channel control=127.0.0.1:47101 --channel small_data=127.0.0.1:47102 --channel main_screen=127.0.0.1:47103 --channel large_data=127.0.0.1:47104

Run clipboard endpoints with generated profiles:
  fusiondesk_clip_agent.cmd --profile samples\agent.json --start-clipboard --pump-clipboard --print-clipboard-diagnostics
  fusiondesk_clip_client.cmd --profile samples\client.json --start-clipboard --pump-clipboard --print-clipboard-diagnostics

Windows validation:
  powershell -ExecutionPolicy Bypass -File tools\clipboard_windows_validation.ps1 -PackageRoot . -Mode LocalNativeText -Scenario Text -DryRun
  powershell -ExecutionPolicy Bypass -File tools\clipboard_windows_validation.ps1 -PackageRoot . -Mode LocalNativeText -Scenario Image -DryRun
  powershell -ExecutionPolicy Bypass -File tools\clipboard_windows_validation.ps1 -PackageRoot . -Mode LocalNativeText -Scenario File -FileReadChunkBytes 17 -SaveFilesDir saved_files
  powershell -ExecutionPolicy Bypass -File tools\clipboard_windows_validation.ps1 -PackageRoot . -Mode LocalNativeText -Scenario DragPreflight

Native clipboard and DragLoop validation must run from an unlocked logged-in
interactive Windows desktop. This package is an unsigned engineering release.
"@
Write-TextFile -Path (Join-Path $stageRoot "README.txt") -Content $readme

$sample = @"
Use fusiondesk_pc_profile_plan to generate runtime profiles for the local
clipboard endpoints you want to test.

Example:
  ..\bin\fusiondesk_pc_profile_plan.exe --client-profile client.json --agent-profile agent.json --channel control=127.0.0.1:47101 --channel small_data=127.0.0.1:47102 --channel main_screen=127.0.0.1:47103 --channel large_data=127.0.0.1:47104
"@
Write-TextFile -Path (Join-Path $sampleDir "README.txt") -Content $sample

if (-not $SkipSmoke) {
    & (Join-Path $binDir "fusiondesk_clip.exe") --clip-role agent --smoke
    if ($LASTEXITCODE -ne 0) {
        throw "Packaged clip agent smoke failed"
    }
    & (Join-Path $binDir "fusiondesk_clip.exe") --clip-role client --smoke
    if ($LASTEXITCODE -ne 0) {
        throw "Packaged clip client smoke failed"
    }
}

Compress-Archive -LiteralPath $stageRoot -DestinationPath $archivePath -Force
Write-Output "PACKAGE_ARCHIVE=$archivePath"
