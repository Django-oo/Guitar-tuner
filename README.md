# STM32 Guitar Tuner

Embedded guitar tuner project for STM32F446RE using DSP techniques to acquire or replay audio, estimate pitch, identify the nearest standard guitar string, and classify the note as flat, sharp, or in tune.

## Goal

The final goal is a live guitar tuner:

1. Acquire microphone audio on the STM32.
2. Process short audio frames continuously.
3. Estimate the dominant pitch.
4. Map the pitch to a standard guitar string.
5. Report whether the note is flat, in tune, or sharp.

Standard tuning is used:

| ID | String | Frequency |
| ---: | --- | ---: |
| 0 | low E / E2 | 82.41 Hz |
| 1 | A / A2 | 110.00 Hz |
| 2 | D / D3 | 146.83 Hz |
| 3 | G / G3 | 196.00 Hz |
| 4 | B / B3 | 246.94 Hz |
| 5 | high E / E4 | 329.63 Hz |
| 255 | unknown | low-confidence or invalid frame |

Tuning states:

| ID | Meaning |
| ---: | --- |
| 1 | in tune |
| 2 | flat |
| 3 | sharp |
| 4 | no signal |
| 5 | error |

## Current Branch

The `static_tuner` branch validates the tuner algorithm before returning to microphone DMA. It has two input modes:

| Mode | Value | Description |
| --- | ---: | --- |
| Synthetic | `tunerInputSource = 0` | Generated guitar-like test tones for all six strings, flat/in-tune/sharp. |
| Real replay | `tunerInputSource = 1` | A 2.048 s embedded excerpt from a real acoustic guitar WAV file. |

The firmware runs:

- a CMSIS-DSP `arm_correlate_f32` autocorrelation detector, used as the active result
- a YIN-style detector kept in source but compiled out for now
- FFT diagnostics for SWV/Data Trace graphing, scaled to `0..1000`
- a real-audio replay sequence history for checking frame-by-frame results
- DWT cycle timing for FFT, correlation, and total frame analysis

## Real Audio Provenance

The embedded real replay is generated from:

```text
Samples/static_tuner/gc.wav
```

Source metadata is kept in:

```text
Core/Inc/static_tuner_real_sample.h
Samples/static_tuner/README.md
```

Proof values:

| Field | Value |
| --- | --- |
| Source URL | `https://raw.githubusercontent.com/pdx-cs-sound/wavs/main/gc.wav` |
| Source SHA-256 | `88638E63464F47B57AB3AF6F54F105302E122EAC484892872EA6F32FB81F1C99` |
| Embedded checksum | `0xC1E6FA47` |
| Embedded duration | `2048 ms` |
| Sample rate | `16000 Hz` |

## Validation Summary

| Validation | Expected result | Status |
| --- | --- | --- |
| Synthetic active-detector tests | 18/18 pass | Passing, `tunerDiag.fail_count = 0` |
| Synthetic correlation diagnostics | 18/18 pass | Passing, `tunerDiag.corr_fail_count = 0` |
| Real replay provenance | SHA/checksum visible in firmware | Passing |
| Real replay sequence | 29 frames from real WAV excerpt | Passing, `tunerDiag.real_sequence_done = 1` |

Independent offline analysis of the embedded real excerpt agrees with the STM32 sequence at a high level:

```text
A-region around 110 Hz
unstable transition
G-ish region around 170-186 Hz
unstable transition
D-class region around 130 Hz, flat relative to D3
```

## Testing In STM32CubeIDE

Build and debug the `static_tuner` branch. The current default mode is the real replay stream:

```c
tunerInputSource = 1
tunerRealAutoReplayEnabled = 1
```

Wait until:

```c
tunerDiag.real_sequence_done == 1
```

Then inspect the recorded sequence:

```c
tunerRealHistoryTimeMs[0..28]
tunerRealHistoryString[0..28]
tunerRealHistoryHz[0..28]
tunerRealHistoryConfidence[0..28]
```

Useful summary/debug fields:

```c
tunerDiag.display_string
tunerDiag.display_state
tunerDiag.display_frequency_x100
tunerDiag.display_cents_x10
tunerDiag.display_confidence_x1000
tunerDiag.real_sequence_longest_string
tunerDiag.real_sequence_longest_start_ms
tunerDiag.real_sequence_longest_duration_ms
tunerDiag.real_sequence_avg_hz_x100
tunerDiag.real_sequence_avg_confidence_x1000
tunerDiag.perf_fft_us
tunerDiag.perf_corr_us
tunerDiag.perf_total_us
```

For synthetic validation, set:

```c
tunerInputSource = 0
tunerRunRequest = 0xFFFFFFFF
```

Then check:

```c
tunerDiag.fail_count == 0
tunerDiag.corr_fail_count == 0
```

## Full WAV Analysis

The STM32 cannot embed the whole `gc.wav` file because the complete downsampled
audio would exceed the practical flash budget. To inspect the whole recording,
use the host-side analyzer:

```powershell
python Tools/analyze_full_wav.py
```

It analyzes the full WAV with the same 16 kHz sample rate, 4096-sample frame,
and 1024-sample hop used by the firmware replay.

Generated outputs:

```text
Samples/static_tuner/analysis/gc_full_analysis.csv
Samples/static_tuner/analysis/gc_full_analysis.md
```

The current full-file analysis covers `403` frames over `26.011 s`.

## Branch Structure

| Branch | Purpose |
| --- | --- |
| `master` | Original STM32 tutorial baseline. |
| `microphone_DMA` | Microphone/ADC DMA acquisition diagnostics and capture tooling. |
| `static_tuner` | Tuner algorithm validation using synthetic tones and real WAV replay. |

The intended future merge path is to keep `static_tuner` as the algorithm
baseline, then feed microphone DMA frames into the same analysis path.

## Next Engineering Steps

1. Keep the static/replay branch as an algorithm validation baseline.
2. Optimize runtime later by reducing debug delays, using compiler optimization, and selecting one primary detector.
3. Return to microphone DMA once the algorithm behavior is clear and documented.
4. Feed live DMA frames into the same tuner analysis path.
