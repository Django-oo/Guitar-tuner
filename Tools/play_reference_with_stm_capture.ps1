param(
  [string]$SamplePath = "",
  [switch]$All,
  [int]$Polls = 40,
  [int]$DelayMs = 250,
  [int]$PreArmDelayMs = 300,
  [string]$SerialNumber = "",
  [string]$ReferenceRoot = "",
  [string]$OutputRoot = ""
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

if ($ReferenceRoot -eq "") {
  $ReferenceRoot = Join-Path $ProjectRoot "Samples\reference_audio"
}

if ($OutputRoot -eq "") {
  $OutputRoot = Join-Path $ProjectRoot "Samples\microphone_dma"
}

$readDiagScript = Join-Path $PSScriptRoot "read_mic_dma_diag.ps1"

function Play-AudioAndWait {
  param([string]$Path)

  $resolvedPath = (Resolve-Path $Path).ProviderPath
  $extension = [IO.Path]::GetExtension($resolvedPath).ToLowerInvariant()

  if ($extension -ne ".wav") {
    throw "Only WAV playback is supported by this script: $resolvedPath"
  }

  $player = New-Object System.Media.SoundPlayer $resolvedPath
  $player.Load()
  $player.PlaySync()
}

function Capture-Sample {
  param([string]$Path)

  $label = [IO.Path]::GetFileNameWithoutExtension($Path)
  $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
  $runDir = Join-Path $OutputRoot "$stamp`_$label"
  New-Item -ItemType Directory -Force -Path $runDir | Out-Null

  $stmCsvPath = Join-Path $runDir "stm.csv"
  $pollLogPath = Join-Path $runDir "stm_poll.txt"
  $manifestPath = Join-Path $runDir "manifest.json"

  $stmArgs = @(
    "-ExecutionPolicy", "Bypass",
    "-File", $readDiagScript,
    "-ArmCapture",
    "-Polls", $Polls,
    "-DelayMs", $DelayMs,
    "-CaptureCsv", $stmCsvPath
  )

  if ($SerialNumber -ne "") {
    $stmArgs += @("-SerialNumber", $SerialNumber)
  }

  $stmJob = Start-Job -ScriptBlock {
    param([string[]]$ArgsForPowerShell)
    & powershell.exe @ArgsForPowerShell
  } -ArgumentList (,$stmArgs)

  try {
    Start-Sleep -Milliseconds $PreArmDelayMs
    Play-AudioAndWait -Path $Path

    Receive-Job -Job $stmJob -Wait | Out-File -FilePath $pollLogPath -Encoding utf8
  }
  finally {
    if ($null -ne $stmJob) {
      Stop-Job -Job $stmJob -ErrorAction SilentlyContinue
      Remove-Job -Job $stmJob -Force -ErrorAction SilentlyContinue
    }
  }

  [ordered]@{
    sample = (Resolve-Path $Path).Path
    started_at = $stamp
    polls = $Polls
    delay_ms = $DelayMs
    pre_arm_delay_ms = $PreArmDelayMs
    stm_csv = $stmCsvPath
    stm_poll = $pollLogPath
  } | ConvertTo-Json | Out-File -FilePath $manifestPath -Encoding utf8

  [pscustomobject]@{
    sample = $label
    folder = $runDir
    stm_csv = $stmCsvPath
    stm_poll = $pollLogPath
  }
}

if ($All) {
  Get-ChildItem -Path $ReferenceRoot -Include *.wav,*.mp3 -File -Recurse | Sort-Object Name | ForEach-Object {
    Capture-Sample -Path $_.FullName
  }
}
elseif ($SamplePath -ne "") {
  Capture-Sample -Path $SamplePath
}
else {
  throw "Pass -All or -SamplePath <file.wav>"
}
