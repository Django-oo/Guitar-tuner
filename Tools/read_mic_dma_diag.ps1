param(
  [int]$Polls = 1,
  [int]$DelayMs = 500,
  [string]$SerialNumber = "",
  [string]$CubeIdeRoot = "D:\STM32CubeIDE_1.18.1\STM32CubeIDE",
  [string]$ElfPath = "",
  [switch]$ArmCapture,
  [string]$CaptureCsv = "",
  [int]$CaptureSamples = 2048
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

if ($ElfPath -eq "") {
  $ElfPath = Join-Path $ProjectRoot "Debug\TP_Calcul_Rapide.elf"
}

function Find-Tool {
  param(
    [string]$Root,
    [string]$Name
  )

  $tool = Get-ChildItem -Path (Join-Path $Root "plugins") -Recurse -Filter $Name |
      Select-Object -First 1 -ExpandProperty FullName

  if ($null -eq $tool) {
    throw "Could not find $Name under $Root"
  }

  return $tool
}

$ProgrammerCli = Find-Tool -Root $CubeIdeRoot -Name "STM32_Programmer_CLI.exe"
$NmTool = Find-Tool -Root $CubeIdeRoot -Name "arm-none-eabi-nm.exe"

$Symbols = @{}
& $NmTool -n $ElfPath | ForEach-Object {
  if ($_ -match "^\s*([0-9A-Fa-f]+)\s+\w\s+(micDmaDiag|micDmaBuffer|micDmaCaptureBuffer|micDmaCaptureControl|appDebugStep|micDmaProcessedBlocks)$") {
    $Symbols[$matches[2]] = [Convert]::ToUInt32($matches[1], 16)
  }
}

foreach ($name in @("micDmaDiag", "appDebugStep")) {
  if (-not $Symbols.ContainsKey($name)) {
    throw "Symbol $name not found in $ElfPath"
  }
}

if (($ArmCapture -or ($CaptureCsv -ne "")) -and
    (-not $Symbols.ContainsKey("micDmaCaptureBuffer") -or
     -not $Symbols.ContainsKey("micDmaCaptureControl"))) {
  throw "Capture symbols not found in $ElfPath. Rebuild and flash the capture-enabled firmware first."
}

$DiagFields = @(
  "state",
  "last_error",
  "self_test_result",
  "start_count",
  "half_callback_count",
  "full_callback_count",
  "half_pending_overrun_count",
  "full_pending_overrun_count",
  "block_count",
  "last_block_index",
  "min",
  "max",
  "lifetime_min",
  "lifetime_max",
  "lifetime_amplitude_max",
  "lifetime_max_abs_from_mean",
  "mean",
  "amplitude",
  "avg_abs_centered",
  "max_abs_centered",
  "avg_abs_from_mean",
  "max_abs_from_mean",
  "zero_crossings",
  "clipped_low_count",
  "clipped_high_count",
  "signal_present",
  "quiet_block_count",
  "active_block_count",
  "adc_error_code",
  "dma_error_code",
  "sysclk_hz",
  "pclk1_hz",
  "tim2_clk_hz",
  "tim2_prescaler",
  "tim2_period",
  "expected_sample_rate_hz",
  "callback_delta_ms",
  "callback_delta_min_ms",
  "callback_delta_max_ms",
  "estimated_sample_rate_hz",
  "capture_armed",
  "capture_ready",
  "capture_count",
  "capture_block_index",
  "capture_mean",
  "capture_amplitude",
  "capture_max_abs_from_mean"
)

function Get-ConnectArgs {
  $connectArgs = @("-q", "-c", "port=SWD", "mode=HOTPLUG", "freq=4000")
  if ($SerialNumber -ne "") {
    $connectArgs += "sn=$SerialNumber"
  }

  return $connectArgs
}

function Read-Words {
  param(
    [uint32]$Address,
    [int]$Bytes
  )

  $connectArgs = Get-ConnectArgs
  $output = & $ProgrammerCli @connectArgs -r32 ("0x{0:X8}" -f $Address) $Bytes -run 2>&1
  $words = @()

  foreach ($line in $output) {
    if ($line -match "^0x[0-9A-Fa-f]+\s*:\s*(.*)$") {
      [regex]::Matches($matches[1], "[0-9A-Fa-f]{8}") | ForEach-Object {
        $words += [Convert]::ToUInt32($_.Value, 16)
      }
    }
  }

  return $words
}

function Read-HalfWords {
  param(
    [uint32]$Address,
    [int]$Bytes
  )

  $connectArgs = Get-ConnectArgs
  $output = & $ProgrammerCli @connectArgs -r16 ("0x{0:X8}" -f $Address) $Bytes -run 2>&1
  $halfWords = @()

  foreach ($line in $output) {
    if ($line -match "^0x[0-9A-Fa-f]+\s*:\s*(.*)$") {
      [regex]::Matches($matches[1], "[0-9A-Fa-f]{4}") | ForEach-Object {
        $halfWords += [Convert]::ToUInt16($_.Value, 16)
      }
    }
  }

  return $halfWords
}

function Write-Word {
  param(
    [uint32]$Address,
    [uint32]$Value
  )

  $connectArgs = Get-ConnectArgs
  & $ProgrammerCli @connectArgs -w32 ("0x{0:X8}" -f $Address) ("0x{0:X8}" -f $Value) -run | Out-Null
}

if ($ArmCapture) {
  Write-Word -Address $Symbols["micDmaCaptureControl"] -Value ([Convert]::ToUInt32("A5A50001", 16))
}

$lastDiag = $null

for ($poll = 0; $poll -lt $Polls; $poll++) {
  $stepWords = Read-Words -Address $Symbols["appDebugStep"] -Bytes 4
  $diagWords = Read-Words -Address $Symbols["micDmaDiag"] -Bytes ($DiagFields.Count * 4)

  if ($diagWords.Count -lt $DiagFields.Count) {
    throw "Only read $($diagWords.Count) diagnostic words, expected $($DiagFields.Count)"
  }

  $diag = @{}
  for ($i = 0; $i -lt $DiagFields.Count; $i++) {
    $diag[$DiagFields[$i]] = $diagWords[$i]
  }
  $lastDiag = $diag

  [pscustomobject]@{
    poll = $poll
    app_step = $stepWords[0]
    state = $diag["state"]
    err = $diag["last_error"]
    blocks = $diag["block_count"]
    mean = $diag["mean"]
    min = $diag["min"]
    max = $diag["max"]
    amp = $diag["amplitude"]
    max_from_mean = $diag["max_abs_from_mean"]
    life_amp = $diag["lifetime_amplitude_max"]
    life_from_mean = $diag["lifetime_max_abs_from_mean"]
    active_blocks = $diag["active_block_count"]
    est_hz = $diag["estimated_sample_rate_hz"]
    cap_armed = $diag["capture_armed"]
    cap_ready = $diag["capture_ready"]
    cap_count = $diag["capture_count"]
    cap_amp = $diag["capture_amplitude"]
    cap_from_mean = $diag["capture_max_abs_from_mean"]
  }

  if ($poll + 1 -lt $Polls) {
    Start-Sleep -Milliseconds $DelayMs
  }
}

if ($CaptureCsv -ne "") {
  if (($null -eq $lastDiag) -or ($lastDiag["capture_ready"] -eq 0)) {
    Write-Warning "No capture is ready. Run again with -ArmCapture and make a sound above the capture threshold."
  }
  else {
    $samples = Read-HalfWords -Address $Symbols["micDmaCaptureBuffer"] -Bytes ($CaptureSamples * 2)
    $mean = [int]$lastDiag["capture_mean"]
    $rows = for ($i = 0; $i -lt $samples.Count; $i++) {
      [pscustomobject]@{
        index = $i
        sample = $samples[$i]
        centered = ([int]$samples[$i] - $mean)
      }
    }

    $rows | Export-Csv -NoTypeInformation -Path $CaptureCsv
    Write-Host "Capture exported to $CaptureCsv ($($samples.Count) samples, mean=$mean)"
  }
}
