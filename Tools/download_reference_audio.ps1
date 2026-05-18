param(
  [string]$OutputRoot = ""
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

if ($OutputRoot -eq "") {
  $OutputRoot = Join-Path $ProjectRoot "Samples\reference_audio"
}

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$userAgent = "TP_Calcul_Rapide_microphone_DMA_reference_downloader/1.0 (student project; contact local user)"

$samples = @(
  [ordered]@{
    label = "voice"
    filename = "voice.wav"
    url = "https://raw.githubusercontent.com/pdx-cs-sound/wavs/main/voice.wav"
    source_page = "https://github.com/pdx-cs-sound/wavs"
    description = "Short voice sample"
    author = "Bart Massey"
    license = "CC0"
  },
  [ordered]@{
    label = "voice_note"
    filename = "voice_note.wav"
    url = "https://raw.githubusercontent.com/pdx-cs-sound/wavs/main/voice-note.wav"
    source_page = "https://github.com/pdx-cs-sound/wavs"
    description = "Single sung voice note"
    author = "Bart Massey"
    license = "CC0"
  },
  [ordered]@{
    label = "guitar"
    filename = "guitar.wav"
    url = "https://raw.githubusercontent.com/pdx-cs-sound/wavs/main/gc.wav"
    source_page = "https://github.com/pdx-cs-sound/wavs"
    description = "Short acoustic guitar sample"
    author = "Bart Massey"
    license = "CC0"
  },
  [ordered]@{
    label = "clap"
    filename = "clap.wav"
    url = "https://oramics.github.io/sampled/DM/LM-2/samples/clap.wav"
    source_page = "https://oramics.github.io/sampled/DM/LM-2/"
    description = "LM-2 clap sample"
    author = "Oramics sampled library"
    license = "Public domain"
  }
)

$manifest = @()

foreach ($sample in $samples) {
  $destination = Join-Path $OutputRoot $sample.filename
  Write-Host "Downloading $($sample.label) -> $destination"
  Invoke-WebRequest -Uri $sample.url -OutFile $destination -UserAgent $userAgent

  $manifest += [ordered]@{
    label = $sample.label
    filename = $sample.filename
    path = $destination
    source_page = $sample.source_page
    download_url = $sample.url
    description = $sample.description
    author = $sample.author
    license = $sample.license
  }
}

$manifestPath = Join-Path $OutputRoot "sources.json"
$manifest | ConvertTo-Json -Depth 4 | Out-File -FilePath $manifestPath -Encoding utf8

Write-Host "Wrote $manifestPath"
