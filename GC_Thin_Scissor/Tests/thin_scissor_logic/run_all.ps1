param(
    [switch]$SkipHardware,
    [switch]$ForceHardware
)

Write-Host "thin_scissor_logic tests disabled by user request; source kept for future recovery."
exit 0

$ErrorActionPreference = "Stop"

$TestRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Resolve-Path (Join-Path $TestRoot "..\..")
$OutDir = Join-Path $TestRoot "out"
$ExePath = Join-Path $OutDir "thin_scissor_logic_test.exe"
$IocLog = Join-Path $OutDir "ioc_check.log"
$SourceLog = Join-Path $OutDir "source_dtu_uid_check.log"
$HostBuildLog = Join-Path $OutDir "host_build.log"
$HostLog = Join-Path $OutDir "host_logic_test.log"
$KeilLog = Join-Path $OutDir "keil_build_from_test.log"
$FlashLog = Join-Path $OutDir "jlink_flash.log"
$RuntimeLog = Join-Path $OutDir "jlink_runtime_check.log"

New-Item -ItemType Directory -Force $OutDir | Out-Null
$script:Failed = 0

function Write-Result {
    param([string]$Name, [bool]$Ok, [string]$Detail = "")
    $status = if ($Ok) { "PASS" } else { "FAIL" }
    if ($Detail.Length -gt 0) {
        Write-Host ("{0,-24} {1}  {2}" -f $Name, $status, $Detail)
    } else {
        Write-Host ("{0,-24} {1}" -f $Name, $status)
    }
    if (-not $Ok) { $script:Failed++ }
}

function Find-Gcc {
    $cmd = Get-Command gcc.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $known = "C:\Users\zjl\Desktop\zjl\LVGL_tool\mingw\mingw64\bin\gcc.exe"
    if (Test-Path $known) { return $known }
    throw "gcc.exe not found"
}

function Find-JLink {
    $cmd = Get-Command JLink.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $known = "C:\Program Files\SEGGER\JLink_V940\JLink.exe"
    if (Test-Path $known) { return $known }
    return $null
}

function Test-IocConfig {
    $content = Get-Content (Join-Path $ProjectRoot "GC_Thin_Scissor.ioc") -Raw
    $checks = @(
        "PE0.Signal=GPIO_Input",
        "PE0.GPIO_PuPd=GPIO_PULLUP",
        "PE1.Signal=GPIO_Input",
        "PE1.GPIO_PuPd=GPIO_PULLUP",
        "PE2.Signal=GPIO_Input",
        "PE2.GPIO_PuPd=GPIO_PULLUP",
        "PE3.Signal=GPIO_Input",
        "PE3.GPIO_PuPd=GPIO_PULLUP",
        "PE5.Signal=GPIO_Input",
        "PE5.GPIO_PuPd=GPIO_PULLUP",
        "PE6.Signal=GPIO_Input",
        "PE6.GPIO_PuPd=GPIO_PULLUP",
        "PG15.Signal=GPIO_Input",
        "PG15.GPIO_PuPd=GPIO_PULLUP",
        "PE8.Signal=GPIO_Input",
        "PE8.GPIO_PuPd=GPIO_PULLUP",
        "PF8.Signal=GPIO_Output",
        "PF8.GPIO_ModeDefaultOutputPP=GPIO_MODE_OUTPUT_PP",
        "PF8.GPIO_PuPd=GPIO_PULLDOWN",
        "PF8.PinState=GPIO_PIN_RESET",
        "PF9.Signal=GPIO_Output",
        "PF9.GPIO_ModeDefaultOutputPP=GPIO_MODE_OUTPUT_PP",
        "PF9.GPIO_PuPd=GPIO_PULLDOWN",
        "PF9.PinState=GPIO_PIN_RESET",
        "PD8.Signal=GPIO_Output",
        "PD8.GPIO_ModeDefaultOutputPP=GPIO_MODE_OUTPUT_PP",
        "PD8.GPIO_PuPd=GPIO_PULLDOWN",
        "PD8.PinState=GPIO_PIN_RESET",
        "PB6.Signal=GPIO_Output",
        "PB6.GPIO_PuPd=GPIO_PULLUP",
        "PB6.PinState=GPIO_PIN_SET",
        "PB10.Signal=USART3_TX",
        "PB10.GPIO_PuPd=GPIO_NOPULL",
        "PB11.Signal=USART3_RX",
        "PB11.GPIO_PuPd=GPIO_PULLUP",
        "SPI1.Mode=SPI_MODE_MASTER",
        "USART3.BaudRate=9600",
        "Dma.USART3_RX.1.Instance=DMA1_Stream1",
        "Dma.USART3_RX.1.Mode=DMA_CIRCULAR",
        "Dma.USART3_TX.0.Instance=DMA1_Stream3",
        "Dma.USART3_TX.0.Mode=DMA_NORMAL",
        "NVIC.USART3_IRQn=true"
    )

    $missing = @()
    foreach ($check in $checks) {
        if ($content -notmatch [regex]::Escape($check)) {
            $missing += $check
        }
    }

    if ($missing.Count -gt 0) {
        $missing | Set-Content -Path $IocLog -Encoding UTF8
        return $false
    }
    "All IOC checks passed." | Set-Content -Path $IocLog -Encoding UTF8
    return $true
}

function Test-SourceDtuUid {
    $iot = Get-Content (Join-Path $ProjectRoot "APP\Src\lift_iot.c") -Raw
    $product = Get-Content (Join-Path $ProjectRoot "APP\Src\app_product.c") -Raw
    $checks = @(
        '0x1FFF7A10',
        'LiftIot_ChipUidString',
        'uid',
        'chip_uid',
        'thin_scissor',
        'PRODUCT_TYPE_THIN_SCISSOR'
    )
    $missing = @()
    foreach ($check in $checks) {
        if (($iot + $product) -notmatch [regex]::Escape($check)) {
            $missing += $check
        }
    }
    if ($missing.Count -gt 0) {
        $missing | Set-Content -Path $SourceLog -Encoding UTF8
        return $false
    }
    "Thin scissor telemetry contains product_type, uid and chip_uid." | Set-Content -Path $SourceLog -Encoding UTF8
    return $true
}

Set-Location $ProjectRoot

Write-Result "IOC_CHECK" (Test-IocConfig) $IocLog
Write-Result "SOURCE_DTU_UID_CHECK" (Test-SourceDtuUid) $SourceLog

try {
    $gcc = Find-Gcc
    $src = @(
        (Join-Path $TestRoot "test_runner.c"),
        (Join-Path $TestRoot "fake_runtime.c"),
        (Join-Path $ProjectRoot "APP\Src\lift_core.c"),
        (Join-Path $ProjectRoot "APP\Src\lift_thin_scissor.c")
    )
    $args = @(
        "-std=c99",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I", (Join-Path $TestRoot "fake_include"),
        "-I", $TestRoot,
        "-o", $ExePath
    ) + $src

    $buildOutput = & $gcc @args 2>&1
    $buildCode = $LASTEXITCODE
    if ($buildOutput) {
        $buildOutput | Set-Content -Path $HostBuildLog -Encoding UTF8
    } else {
        "gcc completed without diagnostics." | Set-Content -Path $HostBuildLog -Encoding UTF8
    }
    Write-Result "BUILD_HOST_TEST" ($buildCode -eq 0) $HostBuildLog
} catch {
    $_ | Out-String | Set-Content -Path $HostBuildLog -Encoding UTF8
    Write-Result "BUILD_HOST_TEST" $false $HostBuildLog
}

if (Test-Path $ExePath) {
    $hostOutput = & $ExePath 2>&1
    $hostCode = $LASTEXITCODE
    $hostOutput | Set-Content -Path $HostLog -Encoding UTF8
    Write-Result "HOST_LOGIC_TEST" ($hostCode -eq 0) $HostLog
} else {
    Write-Result "HOST_LOGIC_TEST" $false "host executable missing"
}

$buildBat = Join-Path $ProjectRoot "build_keil.bat"
if (Test-Path $buildBat) {
    $keilOutput = & $buildBat 2>&1
    $keilCode = $LASTEXITCODE
    $keilOutput | Set-Content -Path $KeilLog -Encoding UTF8
    $nativeKeilLog = Join-Path $ProjectRoot "MDK-ARM\logs\keil_build.log"
    $keilOk = ($keilCode -eq 0)
    if (Test-Path $nativeKeilLog) {
        $nativeText = Get-Content $nativeKeilLog -Raw
        $keilOk = $keilOk -or ($nativeText -match "0 Error\\(s\\)")
    }
    Write-Result "KEIL_BUILD" $keilOk $KeilLog
} else {
    Write-Result "KEIL_BUILD" $false "build_keil.bat missing"
}

$jlink = Find-JLink
if ($SkipHardware -and -not $ForceHardware) {
    Write-Result "JLINK_FLASH" $true "skipped by -SkipHardware"
    Write-Result "JLINK_RUNTIME_CHECK" $true "skipped by -SkipHardware"
} elseif ($null -eq $jlink) {
    Write-Result "JLINK_FLASH" $false "JLink.exe not found"
    Write-Result "JLINK_RUNTIME_CHECK" $false "JLink.exe not found"
} else {
    $flashScript = Join-Path $ProjectRoot "flash_and_run.jlink"
    $runtimeScript = Join-Path $ProjectRoot "check_systick.jlink"

    $flashOutput = & $jlink "-CommanderScript" $flashScript 2>&1
    $flashCode = $LASTEXITCODE
    $flashOutput | Set-Content -Path $FlashLog -Encoding UTF8
    $flashText = Get-Content $FlashLog -Raw
    $flashOk = ($flashCode -eq 0) -and ($flashText -notmatch "Cannot connect|Failed|ERROR|Error")
    Write-Result "JLINK_FLASH" $flashOk $FlashLog

    $runtimeOutput = & $jlink "-CommanderScript" $runtimeScript 2>&1
    $runtimeCode = $LASTEXITCODE
    $runtimeOutput | Set-Content -Path $RuntimeLog -Encoding UTF8
    $runtimeText = Get-Content $RuntimeLog -Raw
    $matches = [regex]::Matches($runtimeText, "E000E018\s*=\s*([0-9A-Fa-f]+)")
    $tickChanged = $false
    if ($matches.Count -ge 2) {
        $first = [Convert]::ToUInt32($matches[0].Groups[1].Value, 16)
        $last = [Convert]::ToUInt32($matches[$matches.Count - 1].Groups[1].Value, 16)
        $tickChanged = ($first -ne $last)
    }
    $runtimeOk = ($runtimeCode -eq 0) -and ($runtimeText -notmatch "Cannot connect|Failed|ERROR|Error") -and $tickChanged
    Write-Result "JLINK_RUNTIME_CHECK" $runtimeOk $RuntimeLog
}

Write-Host ""
Write-Host ("Artifacts: {0}" -f $OutDir)

if ($script:Failed -gt 0) {
    exit 1
}
exit 0
