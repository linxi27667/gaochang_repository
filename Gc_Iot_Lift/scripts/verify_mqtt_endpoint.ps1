[CmdletBinding()]
param(
    [string]$RepoRoot
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = Split-Path -Parent $PSScriptRoot
}
$domain = 'mqtt.gclift.net'
$port = 1883
$oldEndpoints = @('8.134.201.118', '8.134.167.240')

$firmwareHeaders = @(
    'GC-Two_Pillars/APP/Inc/app_tas_dtu.h',
    'GC_Small_Scissor/APP/Inc/app_tas_dtu.h',
    'GC_Thin_Scissor/APP/Inc/app_tas_dtu.h',
    'GC_Big_Scissor/APP/Inc/app_tas_dtu.h'
)

$operationalFiles = @(
    'Gaochang_Iot_Web/server.js',
    'Gaochang_Iot_Web/README.md',
    'Gaochang_Iot_Web/tools/mqtt_full_test.js',
    'Gaochang_Iot_Web/tools/maintenance_flow_test.js',
    'Gaochang_Iot_Web/tools/e2e_test.js'
)

$failures = [System.Collections.Generic.List[string]]::new()

function Get-RepoPath([string]$relativePath) {
    return Join-Path $RepoRoot $relativePath
}

foreach ($relativePath in $firmwareHeaders) {
    $path = Get-RepoPath $relativePath
    if (-not (Test-Path -LiteralPath $path)) {
        $failures.Add("Missing firmware header: $relativePath")
        continue
    }

    $content = Get-Content -Raw -LiteralPath $path
    $match = [regex]::Match($content, '#define\s+TAS_DTU_BROKER_HOST\s+"([^"]+)"')
    if (-not $match.Success) {
        $failures.Add("Broker host macro not found: $relativePath")
        continue
    }
    if ($match.Groups[1].Value -ne $domain) {
        $failures.Add("Unexpected broker in ${relativePath}: $($match.Groups[1].Value)")
    }
}

foreach ($relativePath in $operationalFiles) {
    $path = Get-RepoPath $relativePath
    if (-not (Test-Path -LiteralPath $path)) {
        $failures.Add("Missing operational file: $relativePath")
        continue
    }

    $content = Get-Content -Raw -LiteralPath $path
    foreach ($oldEndpoint in $oldEndpoints) {
        if ($content.Contains($oldEndpoint)) {
            $failures.Add("Deprecated endpoint $oldEndpoint remains in $relativePath")
        }
    }
}

$server = Get-Content -Raw -LiteralPath (Get-RepoPath 'Gaochang_Iot_Web/server.js')
if ($server -notmatch "process\.env\.MQTT_BROKER\s*\|\|\s*'mqtt://127\.0\.0\.1:1883'") {
    $failures.Add('server.js must default to the local Mosquitto broker')
}

$ipport = 'AT+IPPORT="{0}",{1},1' -f $domain, $port
if ($domain.Length -gt 63) {
    $failures.Add("Broker hostname exceeds the firmware validation limit: $($domain.Length) characters")
}
if ($ipport.Length -ge 384) {
    $failures.Add("Generated AT command exceeds the firmware command buffer: $($ipport.Length) characters")
}

$sourceRoots = @('GC-Two_Pillars/APP', 'GC-Two_Pillars/Driver', 'GC_Small_Scissor/APP', 'GC_Small_Scissor/Driver', 'GC_Thin_Scissor/APP', 'GC_Thin_Scissor/Driver', 'GC_Big_Scissor/APP', 'GC_Big_Scissor/Driver')
foreach ($sourceRoot in $sourceRoots) {
    $path = Get-RepoPath $sourceRoot
    if (-not (Test-Path -LiteralPath $path)) { continue }
    $matches = Get-ChildItem -LiteralPath $path -Recurse -File -Include *.c,*.h | Select-String -SimpleMatch -Pattern $oldEndpoints -ErrorAction SilentlyContinue
    if ($matches) {
        $failures.Add("Deprecated endpoint remains in firmware source under $sourceRoot")
    }
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Output "PASS: all firmware broker macros use $domain"
Write-Output "PASS: operational defaults contain no deprecated IP endpoint"
Write-Output "PASS: generated $ipport fits the firmware command buffer"
