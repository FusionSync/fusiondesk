param(
    [ValidateSet("LocalNativeText", "Agent", "Client")]
    [string]$Mode = "LocalNativeText",
    [ValidateSet("Text", "Html", "Rtf", "Image", "File", "DragPreflight", "DragLoop")]
    [string]$Scenario = "Text",
    [string]$AgentAddress = "127.0.0.1",
    [int]$ControlPort = 47101,
    [int]$SmallDataPort = 47102,
    [int]$MainScreenPort = 47103,
    [int]$LargeDataPort = 47104,
    [string]$Text = "fusiondesk pc native clipboard text",
    [string[]]$FilePath = @(),
    [string[]]$FileRequirement = @(),
    [int]$FileReadChunkBytes = 0,
    [string]$SaveFilesDir = "",
    [string]$HtmlPath = "",
    [string]$RtfPath = "",
    [string]$ImagePath = "",
    [string]$DragFilePath = "",
    [string]$Configuration = "Release",
    [string]$PackageRoot = "",
    [string]$LogDir = "",
    [int]$OpenRetryCount = 200,
    [int]$OpenRetryDelayMs = 10,
    [int]$DragLoopTimeoutMs = 120000,
    [switch]$DryRun,
    [switch]$FixedPorts
)

$ErrorActionPreference = "Stop"

if ($Scenario -eq "DragLoop" -and $DryRun) {
    throw "-Scenario DragLoop cannot be combined with -DryRun"
}

function Resolve-ScriptRoot {
    return (Split-Path -Parent $PSCommandPath)
}

function Resolve-Executable {
    param(
        [string]$Name
    )

    $scriptRoot = Resolve-ScriptRoot
    $candidates = @()

    if (-not [string]::IsNullOrWhiteSpace($PackageRoot)) {
        $resolvedPackageRoot = (Resolve-Path -LiteralPath $PackageRoot).Path
        $candidates += (Join-Path $resolvedPackageRoot "bin\$Name")
        $candidates += (Join-Path $resolvedPackageRoot $Name)
    }

    $candidates += (Join-Path $scriptRoot "..\build\$Configuration\$Name")
    $candidates += (Join-Path $scriptRoot "..\bin\$Name")
    $candidates += (Join-Path $scriptRoot "bin\$Name")

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "Executable was not found: $Name"
}

function Resolve-OptionalExecutable {
    param(
        [string]$Name
    )

    try {
        return Resolve-Executable -Name $Name
    } catch {
        return ""
    }
}

function New-FreeTcpPort {
    $listener = [System.Net.Sockets.TcpListener]::new(
        [System.Net.IPAddress]::Loopback,
        0)
    $listener.Start()
    try {
        return $listener.LocalEndpoint.Port
    } finally {
        $listener.Stop()
    }
}

function ConvertTo-CommandLineArgument {
    param(
        [string]$Value
    )

    if ($null -eq $Value) {
        return '""'
    }

    if ($Value.Length -eq 0) {
        return '""'
    }

    if ($Value -notmatch '[\s"]') {
        return $Value
    }

    $result = '"'
    $backslashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq [char]'\') {
            $backslashes += 1
            continue
        }
        if ($character -eq [char]'"') {
            $result += ('\' * (($backslashes * 2) + 1))
            $result += '"'
            $backslashes = 0
            continue
        }
        if ($backslashes -gt 0) {
            $result += ('\' * $backslashes)
            $backslashes = 0
        }
        $result += $character
    }

    if ($backslashes -gt 0) {
        $result += ('\' * ($backslashes * 2))
    }
    $result += '"'
    return $result
}

function Join-ProcessArguments {
    param(
        [string[]]$Arguments
    )

    return (($Arguments | ForEach-Object {
        ConvertTo-CommandLineArgument -Value $_
    }) -join " ")
}

function Invoke-LoggedCommand {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$Name,
        [string]$WorkingDirectory
    )

    $stdoutPath = Join-Path $LogDir "$Name.stdout.log"
    $stderrPath = Join-Path $LogDir "$Name.stderr.log"
    & $FilePath @Arguments 1> $stdoutPath 2> $stderrPath
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        Write-Output "$Name failed with exitCode=$exitCode"
        Write-Output "$Name stdout: $stdoutPath"
        Write-Output "$Name stderr: $stderrPath"
        throw "$Name failed"
    }
}

function Start-LoggedProcess {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$Name,
        [string]$WorkingDirectory
    )

    $stdoutPath = Join-Path $LogDir "$Name.stdout.log"
    $stderrPath = Join-Path $LogDir "$Name.stderr.log"
    $argumentLine = Join-ProcessArguments -Arguments $Arguments
    $commandLine = ConvertTo-CommandLineArgument -Value $FilePath
    if (-not [string]::IsNullOrWhiteSpace($argumentLine)) {
        $commandLine += " $argumentLine"
    }
    $commandLine += " > "
    $commandLine += ConvertTo-CommandLineArgument -Value $stdoutPath
    $commandLine += " 2> "
    $commandLine += ConvertTo-CommandLineArgument -Value $stderrPath

    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = if ($env:ComSpec) { $env:ComSpec } else { "cmd.exe" }
    $startInfo.Arguments = "/d /c $commandLine"
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $false
    $startInfo.RedirectStandardError = $false
    $startInfo.CreateNoWindow = $true

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo

    if (-not $process.Start()) {
        throw "Failed to start $Name"
    }

    return [PSCustomObject]@{
        Name = $Name
        Process = $process
        StdoutPath = $stdoutPath
        StderrPath = $stderrPath
    }
}

function Wait-LoggedProcess {
    param(
        [object]$Handle,
        [int]$TimeoutMs
    )

    if (-not $Handle.Process.WaitForExit($TimeoutMs)) {
        try {
            $Handle.Process.Kill()
        } catch {
        }
        throw "$($Handle.Name) timed out after $TimeoutMs ms"
    }

    $Handle.Process.WaitForExit()
    $Handle.Process.Refresh()
    $exitCode = $Handle.Process.ExitCode
    return $exitCode
}

function Write-LoggedProcessOutput {
    param(
        [object]$Handle
    )

    Write-Output "$($Handle.Name) stdout: $($Handle.StdoutPath)"
    if ((Test-Path -LiteralPath $Handle.StdoutPath) -and
        ((Get-Item -LiteralPath $Handle.StdoutPath).Length -gt 0)) {
        Get-Content -LiteralPath $Handle.StdoutPath
    }
    Write-Output "$($Handle.Name) stderr: $($Handle.StderrPath)"
    if ((Test-Path -LiteralPath $Handle.StderrPath) -and
        ((Get-Item -LiteralPath $Handle.StderrPath).Length -gt 0)) {
        Get-Content -LiteralPath $Handle.StderrPath
    }
}

function Assert-LogContains {
    param(
        [string]$Path,
        [string[]]$Needles,
        [string]$Name
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Name log was not found: $Path"
    }

    $content = Get-Content -Raw -LiteralPath $Path
    foreach ($needle in $Needles) {
        if (-not $content.Contains($needle)) {
            throw "$Name log did not contain '$needle': $Path"
        }
    }
}

function New-ClipboardProfiles {
    param(
        [string]$ProfilePlanExe,
        [string]$ClientProfile,
        [string]$AgentProfile
    )

    $arguments = @(
        "--client-profile", $ClientProfile,
        "--agent-profile", $AgentProfile,
        "--client-ready-prefix", "pc-clipboard-validation-client",
        "--agent-ready-prefix", "pc-clipboard-validation-agent",
        "--channel", "control=$($AgentAddress):$ControlPort",
        "--channel", "small_data=$($AgentAddress):$SmallDataPort",
        "--channel", "main_screen=$($AgentAddress):$MainScreenPort",
        "--channel", "large_data=$($AgentAddress):$LargeDataPort"
    )

    Invoke-LoggedCommand `
        -FilePath $ProfilePlanExe `
        -Arguments $arguments `
        -Name "profile_plan" `
        -WorkingDirectory (Split-Path -Parent $ProfilePlanExe)
}

function Resolve-DragSeedFile {
    if (-not [string]::IsNullOrWhiteSpace($DragFilePath)) {
        return (Resolve-Path -LiteralPath $DragFilePath).Path
    }

    $path = Join-Path $LogDir "drag_payload.txt"
    Set-Content -LiteralPath $path -Value $Text -Encoding UTF8 -NoNewline
    return $path
}

function Resolve-ImageSeedFile {
    if (-not [string]::IsNullOrWhiteSpace($ImagePath)) {
        return (Resolve-Path -LiteralPath $ImagePath).Path
    }

    $path = Join-Path $LogDir "image_payload.png"
    $bytes = [byte[]](
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
        0x89, 0x00, 0x00, 0x00, 0x0a, 0x49, 0x44, 0x41,
        0x54, 0x78, 0x9c, 0x63, 0x00, 0x01, 0x00, 0x00,
        0x05, 0x00, 0x01, 0x0d, 0x0a, 0x2d, 0xb4, 0x00,
        0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae,
        0x42, 0x60, 0x82)
    [System.IO.File]::WriteAllBytes($path, $bytes)
    return $path
}

function Resolve-HtmlSeedFile {
    if (-not [string]::IsNullOrWhiteSpace($HtmlPath)) {
        return (Resolve-Path -LiteralPath $HtmlPath).Path
    }

    $path = Join-Path $LogDir "html_payload.html"
    Write-ValidationTextFile `
        -Path $path `
        -Value '<!doctype html><meta charset="utf-8"><p>fusiondesk <b>html</b></p>'
    return $path
}

function Resolve-RtfSeedFile {
    if (-not [string]::IsNullOrWhiteSpace($RtfPath)) {
        return (Resolve-Path -LiteralPath $RtfPath).Path
    }

    $path = Join-Path $LogDir "rtf_payload.rtf"
    Write-ValidationTextFile `
        -Path $path `
        -Value '{\rtf1\ansi\deff0{\fonttbl{\f0 Segoe UI;}}\f0 fusiondesk \b rtf\b0 payload}'
    return $path
}

function Write-ValidationTextFile {
    param(
        [string]$Path,
        [string]$Value
    )

    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Value, $encoding)
}

function Resolve-FileSeedPaths {
    if ($FilePath.Count -gt 0) {
        return @($FilePath | ForEach-Object {
            (Resolve-Path -LiteralPath $_).Path
        })
    }

    $root = Join-Path $LogDir "file_payload"
    $payloadDir = Join-Path $root "payload_dir"
    $nestedDir = Join-Path $payloadDir "nested"
    New-Item -ItemType Directory -Force -Path $nestedDir | Out-Null

    Write-ValidationTextFile `
        -Path (Join-Path $payloadDir "alpha.txt") `
        -Value $Text
    Write-ValidationTextFile `
        -Path (Join-Path $nestedDir "beta.txt") `
        -Value "$Text nested"
    Write-ValidationTextFile `
        -Path (Join-Path $root "loose.txt") `
        -Value "$Text loose"

    return @($payloadDir, (Join-Path $root "loose.txt"))
}

function Resolve-FileRequirements {
    if ($FileRequirement.Count -gt 0) {
        return @($FileRequirement)
    }

    if ($FilePath.Count -gt 0) {
        throw "-Scenario File with -FilePath requires -FileRequirement on the receiving side"
    }

    return @(
        "payload_dir/alpha.txt=$Text",
        "payload_dir/nested/beta.txt=$Text nested",
        "loose.txt=$Text loose"
    )
}

function Get-FileRequirementExpectedText {
    param(
        [string]$Requirement
    )

    $separator = $Requirement.IndexOf("=")
    if ($separator -lt 0) {
        return $Requirement
    }

    return $Requirement.Substring($separator + 1)
}

function Get-FileRequirementRelativePath {
    param(
        [string]$Requirement
    )

    $separator = $Requirement.IndexOf("=")
    if ($separator -lt 0) {
        return ""
    }

    return $Requirement.Substring(0, $separator)
}

function Get-FileRequirementCount {
    return (Resolve-FileRequirements).Count
}

function Get-FileRangeRequestCount {
    if ($FileReadChunkBytes -le 0) {
        return (Get-FileRequirementCount)
    }

    $count = 0
    foreach ($requirement in (Resolve-FileRequirements)) {
        $bytes = [System.Text.Encoding]::UTF8.GetByteCount(
            (Get-FileRequirementExpectedText -Requirement $requirement))
        if ($bytes -le 0) {
            $count += 1
        } else {
            $count += [int][Math]::Ceiling(
                [double]$bytes / [double]$FileReadChunkBytes)
        }
    }
    return $count
}

function Get-FileRequirementByteCount {
    $bytes = 0
    foreach ($requirement in (Resolve-FileRequirements)) {
        $bytes += [System.Text.Encoding]::UTF8.GetByteCount(
            (Get-FileRequirementExpectedText -Requirement $requirement))
    }
    return $bytes
}

function Test-SaveFilesRequested {
    return (-not [string]::IsNullOrWhiteSpace($SaveFilesDir))
}

function Get-FileReadPassCount {
    if (Test-SaveFilesRequested) {
        return 2
    }
    return 1
}

function Get-FileObjectTransferCount {
    return ((Get-FileRequirementCount) * (Get-FileReadPassCount))
}

function Get-FileRangeTransferCount {
    return ((Get-FileRangeRequestCount) * (Get-FileReadPassCount))
}

function Get-FileByteTransferCount {
    return ((Get-FileRequirementByteCount) * (Get-FileReadPassCount))
}

function Assert-SavedFiles {
    if (-not (Test-SaveFilesRequested)) {
        return
    }

    foreach ($requirement in (Resolve-FileRequirements)) {
        $relativePath = Get-FileRequirementRelativePath -Requirement $requirement
        if ([string]::IsNullOrWhiteSpace($relativePath)) {
            continue
        }

        $expected = Get-FileRequirementExpectedText -Requirement $requirement
        $path = Join-Path $SaveFilesDir $relativePath
        if (-not (Test-Path -LiteralPath $path)) {
            throw "Saved clipboard file was not found: $path"
        }

        $actual = [System.IO.File]::ReadAllText(
            $path,
            [System.Text.Encoding]::UTF8)
        if ($actual -ne $expected) {
            throw "Saved clipboard file mismatch: $path"
        }
    }
}

function Test-NativeValidationRequested {
    if ($Scenario -eq "File") {
        return $false
    }

    return (-not $DryRun)
}

function Test-DragScenario {
    return ($Scenario -eq "DragPreflight" -or $Scenario -eq "DragLoop")
}

function Test-NativeDragLoopScenario {
    return ($Scenario -eq "DragLoop")
}

function Test-NativeClipboardScenario {
    return (-not $DryRun) -and (
        $Scenario -eq "Text" -or
        $Scenario -eq "Html" -or
        $Scenario -eq "Rtf" -or
        $Scenario -eq "Image")
}

function Assert-NativeClipboardAccess {
    if (-not (Test-NativeClipboardScenario)) {
        return
    }

    if ($env:OS -ne "Windows_NT") {
        throw "Native Windows clipboard validation requires Windows; use -DryRun for protocol/runtime validation."
    }

    $probeSource = @"
using System;
using System.Runtime.InteropServices;

public static class FusionDeskClipboardNativeAccessProbe {
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool OpenClipboard(IntPtr hWndNewOwner);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool CloseClipboard();
}
"@

    Add-Type -TypeDefinition $probeSource -ErrorAction Stop | Out-Null
    $opened = [FusionDeskClipboardNativeAccessProbe]::OpenClipboard([IntPtr]::Zero)
    $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    if ($opened) {
        [FusionDeskClipboardNativeAccessProbe]::CloseClipboard() | Out-Null
        Write-Output "VALIDATION_NATIVE_CLIPBOARD_ACCESS=ok"
        return
    }

    throw "Native Windows clipboard is not accessible from this process/session (OpenClipboard error=$errorCode). Run the validation from an unlocked logged-in desktop session, or add -DryRun to validate only the runtime/network/module path."
}

function Get-RunMilliseconds {
    param(
        [int]$Default
    )

    if (Test-NativeDragLoopScenario) {
        return $DragLoopTimeoutMs
    }
    return $Default
}

function Get-WaitMilliseconds {
    param(
        [int]$Default
    )

    if (Test-NativeDragLoopScenario) {
        return ($DragLoopTimeoutMs + 15000)
    }
    return $Default
}

function New-NativeClipboardArguments {
    if ($DryRun) {
        return @()
    }

    return @(
        "--windows-clipboard-native",
        "--clipboard-no-owner-suppression",
        "--clipboard-no-delayed-rendering",
        "--clipboard-open-retry-count", "$OpenRetryCount",
        "--clipboard-open-retry-delay-ms", "$OpenRetryDelayMs"
    )
}

function New-AgentArguments {
    param(
        [string]$AgentProfile,
        [int]$RunMs
    )

    $arguments = @(
        "--listen-profile", $AgentProfile,
        "--session-id", "2",
        "--start-clipboard",
        "--pump-clipboard",
        "--clipboard-no-receive",
        "--print-clipboard-diagnostics",
        "--print-session-diagnostics",
        "--wait-channels-ms", "8000",
        "--run-ms", "$RunMs"
    )

    if ($Scenario -eq "File") {
        foreach ($path in (Resolve-FileSeedPaths)) {
            $arguments += @("--clipboard-seed-file", $path)
        }
    } elseif ($Scenario -eq "Html") {
        if (-not $DryRun) {
            $arguments += New-NativeClipboardArguments
        }
        $arguments += @("--clipboard-seed-html-file", (Resolve-HtmlSeedFile))
    } elseif ($Scenario -eq "Rtf") {
        if (-not $DryRun) {
            $arguments += New-NativeClipboardArguments
        }
        $arguments += @("--clipboard-seed-rtf-file", (Resolve-RtfSeedFile))
    } elseif ($Scenario -eq "Image") {
        if (-not $DryRun) {
            $arguments += New-NativeClipboardArguments
        }
        $arguments += @("--clipboard-seed-image-png", (Resolve-ImageSeedFile))
    } elseif (Test-DragScenario) {
        $arguments += @(
            "--clipboard-seed-file", (Resolve-DragSeedFile),
            "--clipboard-send-drag-drop",
            "--clipboard-drag-offer-wait-ms", "3000",
            "--clipboard-drag-session-id", "8801",
            "--clipboard-drag-start-x", "12",
            "--clipboard-drag-start-y", "24",
            "--clipboard-drag-move-x", "34",
            "--clipboard-drag-move-y", "46",
            "--clipboard-drag-drop-x", "56",
            "--clipboard-drag-drop-y", "68"
        )
        if (-not $DryRun) {
            $arguments += @("--clipboard-send-drag-start-only")
        }
    } elseif ($DryRun) {
        $arguments += @("--clipboard-dry-run-text", $Text)
    } else {
        $arguments += New-NativeClipboardArguments
        $arguments += @("--clipboard-seed-text", $Text)
    }

    return $arguments
}

function New-ClientArguments {
    param(
        [string]$ClientProfile,
        [int]$RunMs
    )

    $arguments = @(
        "--transport-profile", $ClientProfile,
        "--session-id", "1",
        "--start-clipboard",
        "--pump-clipboard",
        "--clipboard-no-announce",
        "--print-clipboard-diagnostics",
        "--print-session-diagnostics",
        "--wait-channels-ms", "8000",
        "--run-ms", "$RunMs"
    )

    if ($Scenario -eq "File") {
        foreach ($requirement in (Resolve-FileRequirements)) {
            $arguments += @("--require-clipboard-file-text", $requirement)
        }
        $arguments += @(
            "--clipboard-require-wait-ms", "5000",
            "--clipboard-file-read-timeout-ms", "5000"
        )
        if ($FileReadChunkBytes -gt 0) {
            $arguments += @("--clipboard-file-read-chunk-bytes", "$FileReadChunkBytes")
        }
        if (Test-SaveFilesRequested) {
            $resolvedSaveDir = (New-Item -ItemType Directory -Force -Path $SaveFilesDir).FullName
            $arguments += @("--save-clipboard-files-dir", $resolvedSaveDir)
        }
    } elseif ($Scenario -eq "Html") {
        $arguments += @(
            "--require-clipboard-html-file", (Resolve-HtmlSeedFile),
            "--clipboard-require-wait-ms", "5000"
        )
        if (-not $DryRun) {
            $arguments += New-NativeClipboardArguments
        }
    } elseif ($Scenario -eq "Rtf") {
        $arguments += @(
            "--require-clipboard-rtf-file", (Resolve-RtfSeedFile),
            "--clipboard-require-wait-ms", "5000"
        )
        if (-not $DryRun) {
            $arguments += New-NativeClipboardArguments
        }
    } elseif ($Scenario -eq "Image") {
        $arguments += @(
            "--require-clipboard-image-png", (Resolve-ImageSeedFile),
            "--clipboard-require-wait-ms", "5000"
        )
        if (-not $DryRun) {
            $arguments += New-NativeClipboardArguments
        }
    } elseif ($Scenario -eq "DragPreflight") {
        if (-not $DryRun) {
            $arguments += @("--windows-clipboard-native-drag-preflight")
        }
    } elseif (Test-NativeDragLoopScenario) {
        $arguments += @("--windows-clipboard-native-drag-loop")
    } else {
        $arguments += @(
            "--require-clipboard-text", $Text,
            "--clipboard-require-wait-ms", "5000"
        )
    }

    if ($Scenario -eq "Text" -and -not $DryRun) {
        $arguments += New-NativeClipboardArguments
    }

    return $arguments
}

$scriptRoot = Resolve-ScriptRoot
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $LogDir = Join-Path $scriptRoot "..\logs\clipboard_validation_$stamp"
}
$LogDir = (New-Item -ItemType Directory -Force -Path $LogDir).FullName

if ($Mode -eq "LocalNativeText" -and -not $FixedPorts) {
    $AgentAddress = "127.0.0.1"
    $ControlPort = New-FreeTcpPort
    $SmallDataPort = New-FreeTcpPort
    $MainScreenPort = New-FreeTcpPort
    $LargeDataPort = New-FreeTcpPort
}

$clipExe = Resolve-OptionalExecutable -Name "fusiondesk_clip.exe"
$clientExe = Resolve-OptionalExecutable -Name "fusiondesk_pc_client.exe"
$agentExe = Resolve-OptionalExecutable -Name "fusiondesk_pc_agent.exe"
if ([string]::IsNullOrWhiteSpace($clientExe) -or
    [string]::IsNullOrWhiteSpace($agentExe)) {
    if ([string]::IsNullOrWhiteSpace($clipExe)) {
        throw "Neither fusiondesk_pc_client/fusiondesk_pc_agent nor fusiondesk_clip was found"
    }
    $clientExe = $clipExe
    $agentExe = $clipExe
}
$clientRoleArguments = @()
$agentRoleArguments = @()
if ($clientExe -eq $clipExe) {
    $clientRoleArguments = @("--clip-role", "client")
}
if ($agentExe -eq $clipExe) {
    $agentRoleArguments = @("--clip-role", "agent")
}
$profilePlanExe = Resolve-Executable -Name "fusiondesk_pc_profile_plan.exe"
$binDir = Split-Path -Parent $agentExe

$profileDir = New-Item -ItemType Directory -Force -Path (Join-Path $LogDir "profiles")
$clientProfile = Join-Path $profileDir.FullName "client_clipboard.json"
$agentProfile = Join-Path $profileDir.FullName "agent_clipboard.json"

New-ClipboardProfiles `
    -ProfilePlanExe $profilePlanExe `
    -ClientProfile $clientProfile `
    -AgentProfile $agentProfile

Write-Output "VALIDATION_MODE=$Mode"
Write-Output "VALIDATION_SCENARIO=$Scenario"
Write-Output "VALIDATION_NATIVE=$(Test-NativeValidationRequested)"
Write-Output "VALIDATION_LOG_DIR=$LogDir"
Write-Output "VALIDATION_ENDPOINT control=$($AgentAddress):$ControlPort"
Write-Output "VALIDATION_ENDPOINT small_data=$($AgentAddress):$SmallDataPort"
Write-Output "VALIDATION_ENDPOINT main_screen=$($AgentAddress):$MainScreenPort"
Write-Output "VALIDATION_ENDPOINT large_data=$($AgentAddress):$LargeDataPort"
Assert-NativeClipboardAccess
if (Test-NativeDragLoopScenario) {
    Write-Output "VALIDATION_ACTION=move the started native drag over a local folder or drop target, then press and release the mouse before timeoutMs=$DragLoopTimeoutMs"
}

if ($Mode -eq "Agent") {
    $agent = Start-LoggedProcess `
        -FilePath $agentExe `
        -Arguments ($agentRoleArguments + (New-AgentArguments -AgentProfile $agentProfile -RunMs (Get-RunMilliseconds -Default 20000))) `
        -Name "agent" `
        -WorkingDirectory $binDir
    $agentExitCode = Wait-LoggedProcess -Handle $agent -TimeoutMs (Get-WaitMilliseconds -Default 25000)
    Write-LoggedProcessOutput -Handle $agent
    if ($agentExitCode -ne 0) {
        throw "Agent validation failed with exitCode=$agentExitCode"
    }
    if (Test-DragScenario) {
        Assert-LogContains `
            -Path $agent.StdoutPath `
            -Name "agent" `
            -Needles @("clipboard.drag.sent", "session=8801")
        if (Test-NativeDragLoopScenario) {
            Assert-LogContains `
                -Path $agent.StdoutPath `
                -Name "agent" `
                -Needles @(
                    "objectLockRequestsReceived=1",
                    "objectLockResponsesSent=1",
                    "objectUnlockRequestsReceived=1",
                    "objectUnlockResponsesSent=1",
                    "fileRangeRequestsReceived=1",
                    "fileRangeResponsesSent=1"
                )
        }
    } elseif ($Scenario -eq "File") {
        $count = Get-FileObjectTransferCount
        $rangeCount = Get-FileRangeTransferCount
        $bytes = Get-FileByteTransferCount
        Assert-LogContains `
            -Path $agent.StdoutPath `
            -Name "agent" `
            -Needles @(
                "objectLockRequestsReceived=$count",
                "objectLockResponsesSent=$count",
                "objectUnlockRequestsReceived=$count",
                "objectUnlockResponsesSent=$count",
                "fileRangeRequestsReceived=$rangeCount",
                "fileRangeResponsesSent=$rangeCount",
                "fileRangeBytesSent=$bytes"
            )
    }
    Write-Output "VALIDATION_RESULT=passed"
    exit 0
}

if ($Mode -eq "Client") {
    $client = Start-LoggedProcess `
        -FilePath $clientExe `
        -Arguments ($clientRoleArguments + (New-ClientArguments -ClientProfile $clientProfile -RunMs (Get-RunMilliseconds -Default 7000))) `
        -Name "client" `
        -WorkingDirectory $binDir
    $clientExitCode = Wait-LoggedProcess -Handle $client -TimeoutMs (Get-WaitMilliseconds -Default 15000)
    Write-LoggedProcessOutput -Handle $client
    if ($clientExitCode -ne 0) {
        throw "Client validation failed with exitCode=$clientExitCode"
    }
    if (Test-DragScenario) {
        $needles = @("dragStartsReceived=1", "dragNativePublicationFailures=0")
        if (-not $DryRun) {
            if (Test-NativeDragLoopScenario) {
                $needles += "nativeDragLoops=1"
                $needles += "nativeDragDrops=1"
            } else {
                $needles += "nativeDragPreflights=1"
                $needles += "nativeDragPreflightReads=1"
            }
            $needles += "objectLockRequestsSent=1"
            $needles += "objectLockResponsesReceived=1"
            $needles += "objectUnlockRequestsSent=1"
            $needles += "objectUnlockResponsesReceived=1"
            $needles += "fileRangeRequestsSent=1"
            $needles += "fileRangeResponsesReceived=1"
        }
        Assert-LogContains `
            -Path $client.StdoutPath `
            -Name "client" `
            -Needles $needles
    } elseif ($Scenario -eq "File") {
        $count = Get-FileObjectTransferCount
        $rangeCount = Get-FileRangeTransferCount
        $bytes = Get-FileByteTransferCount
        $clientNeedles = @(
            "objectLockRequestsSent=$count",
            "objectLockResponsesReceived=$count",
            "objectUnlockRequestsSent=$count",
            "objectUnlockResponsesReceived=$count",
            "fileRangeRequestsSent=$rangeCount",
            "fileRangeResponsesReceived=$rangeCount",
            "fileRangeBytesReceived=$bytes",
            "pendingReads=0"
        )
        if (Test-SaveFilesRequested) {
            $clientNeedles += "clipboard.files.saved"
            $clientNeedles += "files=$(Get-FileRequirementCount)"
            $clientNeedles += "bytes=$(Get-FileRequirementByteCount)"
            $clientNeedles += "chunks=$(Get-FileRangeRequestCount)"
        }
        Assert-LogContains `
            -Path $client.StdoutPath `
            -Name "client" `
            -Needles $clientNeedles
        Assert-SavedFiles
    }
    Write-Output "VALIDATION_RESULT=passed"
    exit 0
}

$agentHandle = Start-LoggedProcess `
    -FilePath $agentExe `
    -Arguments ($agentRoleArguments + (New-AgentArguments -AgentProfile $agentProfile -RunMs (Get-RunMilliseconds -Default 12000))) `
    -Name "agent" `
    -WorkingDirectory $binDir

Start-Sleep -Milliseconds 300

$clientHandle = Start-LoggedProcess `
    -FilePath $clientExe `
    -Arguments ($clientRoleArguments + (New-ClientArguments -ClientProfile $clientProfile -RunMs (Get-RunMilliseconds -Default 7000))) `
    -Name "client" `
    -WorkingDirectory $binDir

$clientExitCode = 1
$agentExitCode = 1
try {
    $clientExitCode = Wait-LoggedProcess -Handle $clientHandle -TimeoutMs (Get-WaitMilliseconds -Default 16000)
    $agentExitCode = Wait-LoggedProcess -Handle $agentHandle -TimeoutMs (Get-WaitMilliseconds -Default 17000)
} catch {
    try {
        if (-not $agentHandle.Process.HasExited) {
            $agentHandle.Process.Kill()
        }
    } catch {
    }
    throw
} finally {
    Write-LoggedProcessOutput -Handle $clientHandle
    Write-LoggedProcessOutput -Handle $agentHandle
}

if ($clientExitCode -ne 0 -or $agentExitCode -ne 0) {
    throw "Local validation failed clientExitCode=$clientExitCode agentExitCode=$agentExitCode"
}

if (Test-DragScenario) {
    $clientNeedles = @("dragStartsReceived=1", "dragNativePublicationFailures=0")
    if (-not $DryRun) {
        if (Test-NativeDragLoopScenario) {
            $clientNeedles += "nativeDragLoops=1"
            $clientNeedles += "nativeDragDrops=1"
        } else {
            $clientNeedles += "nativeDragPreflights=1"
            $clientNeedles += "nativeDragPreflightReads=1"
        }
        $clientNeedles += "objectLockRequestsSent=1"
        $clientNeedles += "objectLockResponsesReceived=1"
        $clientNeedles += "objectUnlockRequestsSent=1"
        $clientNeedles += "objectUnlockResponsesReceived=1"
        $clientNeedles += "fileRangeRequestsSent=1"
        $clientNeedles += "fileRangeResponsesReceived=1"
    }
    Assert-LogContains `
        -Path $clientHandle.StdoutPath `
        -Name "client" `
        -Needles $clientNeedles
    Assert-LogContains `
        -Path $agentHandle.StdoutPath `
        -Name "agent" `
        -Needles @("clipboard.drag.sent", "session=8801")
    if (-not $DryRun) {
        Assert-LogContains `
            -Path $agentHandle.StdoutPath `
            -Name "agent" `
            -Needles @(
                "objectLockRequestsReceived=1",
                "objectLockResponsesSent=1",
                "objectUnlockRequestsReceived=1",
                "objectUnlockResponsesSent=1",
                "fileRangeRequestsReceived=1",
                "fileRangeResponsesSent=1"
            )
    }
} elseif ($Scenario -eq "File") {
    $count = Get-FileObjectTransferCount
    $rangeCount = Get-FileRangeTransferCount
    $bytes = Get-FileByteTransferCount
    $clientNeedles = @(
        "objectLockRequestsSent=$count",
        "objectLockResponsesReceived=$count",
        "objectUnlockRequestsSent=$count",
        "objectUnlockResponsesReceived=$count",
        "fileRangeRequestsSent=$rangeCount",
        "fileRangeResponsesReceived=$rangeCount",
        "fileRangeBytesReceived=$bytes",
        "pendingReads=0"
    )
    if (Test-SaveFilesRequested) {
        $clientNeedles += "clipboard.files.saved"
        $clientNeedles += "files=$(Get-FileRequirementCount)"
        $clientNeedles += "bytes=$(Get-FileRequirementByteCount)"
        $clientNeedles += "chunks=$(Get-FileRangeRequestCount)"
    }
    Assert-LogContains `
        -Path $clientHandle.StdoutPath `
        -Name "client" `
        -Needles $clientNeedles
    Assert-SavedFiles
    Assert-LogContains `
        -Path $agentHandle.StdoutPath `
        -Name "agent" `
        -Needles @(
            "objectLockRequestsReceived=$count",
            "objectLockResponsesSent=$count",
            "objectUnlockRequestsReceived=$count",
            "objectUnlockResponsesSent=$count",
            "fileRangeRequestsReceived=$rangeCount",
            "fileRangeResponsesSent=$rangeCount",
            "fileRangeBytesSent=$bytes"
        )
}

Write-Output "VALIDATION_RESULT=passed"
