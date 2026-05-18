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
$packageName = "fusiondesk-$Platform-$safeVersion"
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

$clientExe = Resolve-Exe $buildRoot $Configuration "fusiondesk_pc_client.exe"
$agentExe = Resolve-Exe $buildRoot $Configuration "fusiondesk_pc_agent.exe"
$profilePlanExe = Resolve-Exe $buildRoot $Configuration "fusiondesk_pc_profile_plan.exe"

Copy-Item -LiteralPath $clientExe -Destination $binDir
Copy-Item -LiteralPath $agentExe -Destination $binDir
Copy-Item -LiteralPath $profilePlanExe -Destination $binDir
Copy-Item -LiteralPath (Join-Path $repoRoot "tools\clipboard_windows_validation.ps1") -Destination $toolsDir

if (-not $SkipQtDeploy) {
    $windeployqt = Resolve-WindeployQt
    foreach ($exe in @("fusiondesk_pc_client.exe", "fusiondesk_pc_agent.exe", "fusiondesk_pc_profile_plan.exe")) {
        & $windeployqt --release --no-translations (Join-Path $binDir $exe)
        if ($LASTEXITCODE -ne 0) {
            throw "windeployqt failed for $exe"
        }
    }
}

$commit = Resolve-GitValue @("rev-parse", "--short", "HEAD")
$manifest = @"
name=fusiondesk
platform=$Platform
version=$Version
commit=$commit
configuration=$Configuration
createdUtc=$([System.DateTimeOffset]::UtcNow.ToString("u"))
"@
Write-TextFile -Path (Join-Path $stageRoot "manifest.txt") -Content $manifest

$readme = @"
FusionDesk release package

Contents:
- bin/fusiondesk_pc_agent.exe
- bin/fusiondesk_pc_client.exe
- bin/fusiondesk_pc_profile_plan.exe

Smoke:
  bin\fusiondesk_pc_agent.exe --smoke
  bin\fusiondesk_pc_client.exe --smoke

Windows clipboard validation:
  powershell -ExecutionPolicy Bypass -File tools\clipboard_windows_validation.ps1 -Mode LocalNativeText
  powershell -ExecutionPolicy Bypass -File tools\clipboard_windows_validation.ps1 -Mode LocalNativeText -DryRun
  bin\fusiondesk_pc_client.exe --smoke --clipboard-policy-export-file clipboard_policy.effective.json --clipboard-no-file-contents --clipboard-runtime-audit
  powershell -ExecutionPolicy Bypass -File tools\clipboard_windows_validation.ps1 -Mode LocalNativeText -Scenario Rtf -DryRun
  powershell -ExecutionPolicy Bypass -File tools\clipboard_windows_validation.ps1 -Mode LocalNativeText -Scenario Image -DryRun
  powershell -ExecutionPolicy Bypass -File tools\clipboard_windows_validation.ps1 -Mode LocalNativeText -Scenario File -FileReadChunkBytes 17 -SaveFilesDir saved_files
  powershell -ExecutionPolicy Bypass -File tools\clipboard_windows_validation.ps1 -Mode LocalNativeText -Scenario DragPreflight

Two-machine Windows clipboard validation:
  Agent desktop:
    powershell -ExecutionPolicy Bypass -File tools\clipboard_windows_validation.ps1 -Mode Agent -Scenario Text -AgentAddress <agent-lan-ip> -Text "fusiondesk clipboard validate"
  Client desktop:
    powershell -ExecutionPolicy Bypass -File tools\clipboard_windows_validation.ps1 -Mode Client -Scenario Text -AgentAddress <agent-lan-ip> -Text "fusiondesk clipboard validate"

Two-machine image validation:
  Agent desktop:
    powershell -ExecutionPolicy Bypass -File tools\clipboard_windows_validation.ps1 -Mode Agent -Scenario Image -AgentAddress <agent-lan-ip> -DryRun
  Client desktop:
    powershell -ExecutionPolicy Bypass -File tools\clipboard_windows_validation.ps1 -Mode Client -Scenario Image -AgentAddress <agent-lan-ip> -DryRun

Two-machine rich text validation:
  Agent desktop:
    powershell -ExecutionPolicy Bypass -File tools\clipboard_windows_validation.ps1 -Mode Agent -Scenario Rtf -AgentAddress <agent-lan-ip> -DryRun
  Client desktop:
    powershell -ExecutionPolicy Bypass -File tools\clipboard_windows_validation.ps1 -Mode Client -Scenario Rtf -AgentAddress <agent-lan-ip> -DryRun

Two-machine file validation:
  Agent desktop:
    powershell -ExecutionPolicy Bypass -File tools\clipboard_windows_validation.ps1 -Mode Agent -Scenario File -AgentAddress <agent-lan-ip> -Text "fusiondesk file validate" -FileReadChunkBytes 17
  Client desktop:
    powershell -ExecutionPolicy Bypass -File tools\clipboard_windows_validation.ps1 -Mode Client -Scenario File -AgentAddress <agent-lan-ip> -Text "fusiondesk file validate" -FileReadChunkBytes 17 -SaveFilesDir saved_files

Two-machine native drag validation:
  Agent desktop:
    powershell -ExecutionPolicy Bypass -File tools\clipboard_windows_validation.ps1 -Mode Agent -Scenario DragLoop -AgentAddress <agent-lan-ip>
  Client desktop:
    powershell -ExecutionPolicy Bypass -File tools\clipboard_windows_validation.ps1 -Mode Client -Scenario DragLoop -AgentAddress <agent-lan-ip> -DragLoopTimeoutMs 120000

Native clipboard and DragLoop validation must run from an unlocked logged-in interactive Windows desktop. Non-dry-run Text/Html/Rtf/Image validation preflights OpenClipboard and fails before launching agent/client processes when the current process/session cannot access the OS clipboard. DragLoop waits for the user to move the generated native drag over a local folder or drop target, then press and release the mouse before the timeout.

Generate paired local TCP profiles:
  bin\fusiondesk_pc_profile_plan.exe --client-profile samples\client.json --agent-profile samples\agent.json --channel control=127.0.0.1:47101 --channel small_data=127.0.0.1:47102 --channel main_screen=127.0.0.1:47103 --channel large_data=127.0.0.1:47104

This package is the FusionDesk engineering release zip. It is not a signed installer.
"@
Write-TextFile -Path (Join-Path $stageRoot "README.txt") -Content $readme

$sample = @"
Use fusiondesk_pc_profile_plan to generate runtime profiles for the local endpoints you want to test.

Example:
  ..\bin\fusiondesk_pc_profile_plan.exe --client-profile client.json --agent-profile agent.json --channel control=127.0.0.1:47101 --channel small_data=127.0.0.1:47102 --channel main_screen=127.0.0.1:47103 --channel large_data=127.0.0.1:47104
"@
Write-TextFile -Path (Join-Path $sampleDir "README.txt") -Content $sample

if (-not $SkipSmoke) {
    & (Join-Path $binDir "fusiondesk_pc_agent.exe") --smoke
    if ($LASTEXITCODE -ne 0) {
        throw "Packaged agent smoke failed"
    }
    & (Join-Path $binDir "fusiondesk_pc_client.exe") --smoke
    if ($LASTEXITCODE -ne 0) {
        throw "Packaged client smoke failed"
    }
}

Compress-Archive -LiteralPath $stageRoot -DestinationPath $archivePath -Force
Write-Output "PACKAGE_ARCHIVE=$archivePath"
