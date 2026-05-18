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

- a YIN-style pitch detector, used as the main result
- a CMSIS-DSP `arm_correlate_f32` autocorrelation detector, exposed for comparison
- FFT diagnostics for SWV/Data Trace graphing
- a real-audio replay sequence history for checking frame-by-frame results

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
| Synthetic YIN tests | 18/18 pass | Passing, `tunerDiag.fail_count = 0` |
| Synthetic correlation tests | 18/18 pass | Passing, `tunerDiag.corr_fail_count = 0` |
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

## Next Engineering Steps

1. Keep the static/replay branch as an algorithm validation baseline.
2. Optimize runtime later by reducing debug delays, using compiler optimization, and selecting one primary detector.
3. Return to microphone DMA once the algorithm behavior is clear and documented.
4. Feed live DMA frames into the same tuner analysis path.
