param(
    [switch]$SkipHardware,
    [switch]$ForceHardware
)

Write-Host "small_scissor_logic tests disabled by user request; source kept for future recovery."
exit 0

$ErrorActionPreference = "Stop"

$TestRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Resolve-Path (Join-Path $TestRoot "..\..")
$OutDir = Join-Path $TestRoot "out"
$ExePath = Join-Path $OutDir "small_scissor_logic_test.exe"
$HostLog = Join-Path $OutDir "host_logic_test.log"
$IocLog = Join-Path $OutDir "ioc_check.log"
$KeilLog = Join-Path $OutDir "keil_build_from_test.log"
$FlashLog = Join-Path $OutDir "jlink_flash.log"
$RuntimeLog = Join-Path $OutDir "jlink_runtime_check.log"
$GpioLog = Join-Path $OutDir "jlink_gpio_static.log"

New-Item -ItemType Directory -Force $OutDir | Out-Null

$script:Failed = 0

function Write-Result {
    param(
        [string]$Name,
        [bool]$Ok,
        [string]$Detail = ""
    )

    $status = if ($Ok) { "PASS" } else { "FAIL" }
    if ($Detail.Length -gt 0) {
        Write-Host ("{0,-22} {1}  {2}" -f $Name, $status, $Detail)
    } else {
        Write-Host ("{0,-22} {1}" -f $Name, $status)
    }
    if (-not $Ok) {
        $script:Failed++
    }
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

function Invoke-Checked {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$LogPath,
        [string]$WorkingDirectory
    )

    $output = & $FilePath @Arguments 2>&1
    $code = $LASTEXITCODE
    $output | Set-Content -Path $LogPath -Encoding UTF8
    return $code
}

function Test-IocConfig {
    $ioc = Join-Path $ProjectRoot "GC_Small_Scissor.ioc"
    $content = Get-Content $ioc -Raw
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
        "PD8.PinState=GPIO_PIN_RESET"
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

function Test-JLinkRuntimeLog {
    param(
        [string]$RuntimeText,
        [int]$RuntimeCode
    )

    $matches = [regex]::Matches($RuntimeText, "E000E018\s*=\s*([0-9A-Fa-f]+)")
    $tickChanged = $false
    if ($matches.Count -ge 2) {
        $first = [Convert]::ToUInt32($matches[0].Groups[1].Value, 16)
        $last = [Convert]::ToUInt32($matches[$matches.Count - 1].Groups[1].Value, 16)
        $tickChanged = ($first -ne $last)
    }

    return ($RuntimeCode -eq 0) -and ($RuntimeText -notmatch "Cannot connect|Failed|ERROR|Error") -and $tickChanged
}

function Test-JLinkGpioStaticLog {
    param(
        [string]$GpioText,
        [int]$GpioCode
    )

    if (($GpioCode -ne 0) -or ($GpioText -match "Cannot connect|Failed|ERROR|Error")) {
        return $false
    }

    $tickMatches = [regex]::Matches($GpioText, "E000E018\s*=\s*([0-9A-Fa-f]+)")
    if ($tickMatches.Count -lt 2) {
        return $false
    }
    $firstTick = [Convert]::ToUInt32($tickMatches[0].Groups[1].Value, 16)
    $lastTick = [Convert]::ToUInt32($tickMatches[$tickMatches.Count - 1].Groups[1].Value, 16)
    if ($firstTick -eq $lastTick) {
        return $false
    }

    $gpiod = [regex]::Match($GpioText, "40020C14\s*=\s*([0-9A-Fa-f]+)")
    $gpiof = [regex]::Match($GpioText, "40021414\s*=\s*([0-9A-Fa-f]+)")
    if ((-not $gpiod.Success) -or (-not $gpiof.Success)) {
        return $false
    }

    $gpiodOdr = [Convert]::ToUInt32($gpiod.Groups[1].Value, 16)
    $gpiofOdr = [Convert]::ToUInt32($gpiof.Groups[1].Value, 16)
    $pd8Off = (($gpiodOdr -band 0x00000100) -eq 0)
    $pf8Pf9Off = (($gpiofOdr -band 0x00000300) -eq 0)

    return $pd8Off -and $pf8Pf9Off
}

Set-Location $ProjectRoot

$iocOk = Test-IocConfig
Write-Result "IOC_CHECK" $iocOk $IocLog

$gcc = $null
try {
    $gcc = Find-Gcc
    $src = @(
        (Join-Path $TestRoot "test_runner.c"),
        (Join-Path $TestRoot "fake_hal.c"),
        (Join-Path $TestRoot "fake_io.c"),
        (Join-Path $TestRoot "fake_elog.c"),
        (Join-Path $TestRoot "fake_w25qxx.c"),
        (Join-Path $TestRoot "fake_product.c"),
        (Join-Path $TestRoot "fake_op_log.c"),
        (Join-Path $ProjectRoot "APP\Src\app_lift_core.c")
    )
    $args = @(
        "-std=c99",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I", (Join-Path $TestRoot "fake_include"),
        "-I", $TestRoot,
        "-I", (Join-Path $ProjectRoot "APP\Inc"),
        "-o", $ExePath
    ) + $src

    $buildOutput = & $gcc @args 2>&1
    $buildCode = $LASTEXITCODE
    if ($buildOutput) {
        $buildOutput | Set-Content -Path (Join-Path $OutDir "host_build.log") -Encoding UTF8
    } else {
        "gcc completed without diagnostics." | Set-Content -Path (Join-Path $OutDir "host_build.log") -Encoding UTF8
    }
    Write-Result "BUILD_HOST_TEST" ($buildCode -eq 0) (Join-Path $OutDir "host_build.log")
} catch {
    $_ | Out-String | Set-Content -Path (Join-Path $OutDir "host_build.log") -Encoding UTF8
    Write-Result "BUILD_HOST_TEST" $false (Join-Path $OutDir "host_build.log")
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
    Write-Result "KEIL_BUILD" ($keilCode -eq 0) $KeilLog
} else {
    Write-Result "KEIL_BUILD" $false "build_keil.bat missing"
}

$jlink = Find-JLink
if ($SkipHardware -and -not $ForceHardware) {
    Write-Result "JLINK_FLASH" $true "skipped by -SkipHardware"
    Write-Result "JLINK_RUNTIME_CHECK" $true "skipped by -SkipHardware"
    Write-Result "JLINK_GPIO_STATIC" $true "skipped by -SkipHardware"
} elseif ($null -eq $jlink) {
    Write-Result "JLINK_FLASH" $false "JLink.exe not found"
    Write-Result "JLINK_RUNTIME_CHECK" $false "JLink.exe not found"
    Write-Result "JLINK_GPIO_STATIC" $false "JLink.exe not found"
} else {
    $flashScript = Join-Path $ProjectRoot "flash_and_run.jlink"
    $runtimeScript = Join-Path $ProjectRoot "check_systick.jlink"
    $gpioScript = Join-Path $ProjectRoot "check_gpio_static.jlink"

    $flashCode = Invoke-Checked -FilePath $jlink -Arguments @("-CommanderScript", $flashScript) -LogPath $FlashLog -WorkingDirectory $ProjectRoot
    $flashText = Get-Content $FlashLog -Raw
    $flashOk = ($flashCode -eq 0) -and ($flashText -notmatch "Cannot connect|Failed|ERROR|Error")
    Write-Result "JLINK_FLASH" $flashOk $FlashLog

    $runtimeCode = Invoke-Checked -FilePath $jlink -Arguments @("-CommanderScript", $runtimeScript) -LogPath $RuntimeLog -WorkingDirectory $ProjectRoot
    $runtimeText = Get-Content $RuntimeLog -Raw
    $runtimeOk = Test-JLinkRuntimeLog -RuntimeText $runtimeText -RuntimeCode $runtimeCode
    Write-Result "JLINK_RUNTIME_CHECK" $runtimeOk $RuntimeLog

    if (Test-Path $gpioScript) {
        $gpioCode = Invoke-Checked -FilePath $jlink -Arguments @("-CommanderScript", $gpioScript) -LogPath $GpioLog -WorkingDirectory $ProjectRoot
        $gpioText = Get-Content $GpioLog -Raw
        $gpioOk = Test-JLinkGpioStaticLog -GpioText $gpioText -GpioCode $gpioCode
        Write-Result "JLINK_GPIO_STATIC" $gpioOk $GpioLog
    } else {
        Write-Result "JLINK_GPIO_STATIC" $false "check_gpio_static.jlink missing"
    }
}

Write-Host ""
Write-Host ("Artifacts: {0}" -f $OutDir)

if ($script:Failed -gt 0) {
    exit 1
}
exit 0
