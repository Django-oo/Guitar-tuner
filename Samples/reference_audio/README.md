# Reference Audio Samples

Downloaded internet WAV samples used to stimulate the microphone board through
a speaker, with STM32 DMA capture armed at playback time.

Run:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\Tools\download_reference_audio.ps1
```

Then capture all references:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\Tools\play_reference_with_stm_capture.ps1 -All -SerialNumber 0670FF525282494867244060
```

Source and license metadata is written to `sources.json`.
