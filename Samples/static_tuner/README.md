# Static Tuner Real Audio Sample

`gc.wav` is the source file used to generate the embedded real-audio input in
`Core/Inc/static_tuner_real_sample.h`.

- Source URL: https://raw.githubusercontent.com/pdx-cs-sound/wavs/main/gc.wav
- Repository: https://github.com/pdx-cs-sound/wavs
- Description in source README: short acoustic guitar sample
- License in source README: Creative Commons CC0 unless otherwise indicated
- SHA-256 of downloaded WAV:
  `88638E63464F47B57AB3AF6F54F105302E122EAC484892872EA6F32FB81F1C99`

To regenerate the embedded C array:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\Tools\convert_wav_to_static_tuner_sample.ps1
```

The converter takes a 4096-sample mono slice at 16 kHz and writes its source
metadata and checksum into the generated header.
