# Static Tuner Validation

This branch validates the guitar tuner algorithm with synthetic inputs and an
embedded real-audio WAV slice instead of the microphone.

## Debug Variables

Add these to Live Expressions:

- `tunerSelectedTest`
- `tunerRunRequest`
- `tunerInputSource`
- `tunerDiag.initialized`
- `tunerDiag.input_source`
- `tunerDiag.pass_count`
- `tunerDiag.fail_count`
- `tunerDiag.selected_test`
- `tunerDiag.expected_string`
- `tunerDiag.detected_string`
- `tunerDiag.expected_state`
- `tunerDiag.tuning_state`
- `tunerDiag.input_hz`
- `tunerDiag.detected_hz`
- `tunerDiag.cents_error_x10`
- `tunerDiag.confidence`
- `tunerDiag.corr_detected_hz`
- `tunerDiag.corr_cents_error_x10`
- `tunerDiag.corr_confidence`
- `tunerDiag.corr_tau`
- `tunerDiag.corr_detected_string`
- `tunerDiag.corr_state`
- `tunerDiag.corr_pass_count`
- `tunerDiag.corr_fail_count`
- `tunerDiag.real_sample_count`
- `tunerDiag.real_sample_rate_hz`
- `tunerDiag.real_sample_original_rate_hz`
- `tunerDiag.real_sample_start_output_frame`
- `tunerDiag.real_sample_checksum`
- `tunerDiag.fft_low_e_mag`
- `tunerDiag.fft_a_mag`
- `tunerDiag.fft_d_mag`
- `tunerDiag.fft_g_mag`
- `tunerDiag.fft_b_mag`
- `tunerDiag.fft_high_e_mag`
- `tunerDiag.fft_peak_hz`
- `tunerDiag.fft_peak_mag`
- `tunerDiag.signal_amplitude`
- `tunerDiag.yin_tau`
- `tunerDiag.fft_peak_bin`

For graph-style views, also add individual spectrum entries:

- `tunerSpectrum64[0]`
- `tunerSpectrum64[10]`
- `tunerSpectrum64[20]`
- `tunerSpectrum64[30]`
- `tunerSpectrum64[40]`
- `tunerSpectrum64[50]`

`tunerSpectrum64` spans about 0 to 500 Hz, so each entry represents roughly
7.8 Hz. The six named `fft_*_mag` fields are easier for SWV timeline graphs
focused on standard guitar strings.

## SWV Graph Variables

For SWV Data Trace, prefer these plain global symbols instead of struct fields:

- `tunerGraphLowE`
- `tunerGraphA`
- `tunerGraphD`
- `tunerGraphG`
- `tunerGraphB`
- `tunerGraphHighE`
- `tunerGraphPeakHz`
- `tunerGraphPeakMagnitude`
- `tunerGraphCentsErrorX10`
- `tunerGraphCorrHz`
- `tunerGraphCorrCentsErrorX10`

Set the SWV comparator access to `Write`, because the graph should update when
the firmware writes a new value.

CubeIDE usually provides four data comparators, so graph four values at a time.
For example:

- `tunerGraphLowE`
- `tunerGraphA`
- `tunerGraphD`
- `tunerGraphHighE`

The firmware enables `tunerAutoDemoEnabled = 1` by default and advances to the
next in-tune string every `tunerAutoDemoPeriodMs = 1000`, so the graph changes
once per second without manually editing `tunerSelectedTest`.

## Input Source

The tuner can analyze two input sources:

- `tunerInputSource = 0`: synthetic generated guitar tone
- `tunerInputSource = 1`: embedded real WAV slice

The real input is generated from `Samples/static_tuner/gc.wav`, a CC0 acoustic
guitar WAV from `pdx-cs-sound/wavs`. The converter stores the source URL,
source SHA-256, and sample checksum in `Core/Inc/static_tuner_real_sample.h`.

For proof in STM32CubeIDE, watch:

- `tunerRealAudioSourceFile`
- `tunerRealAudioSourceUrl`
- `tunerRealAudioSourceSha256`
- `tunerRealAudioLicense`
- `tunerDiag.real_sample_checksum`

To analyze the real audio:

1. Set `tunerAutoDemoEnabled` to `0`.
2. Set `tunerInputSource` to `1`.
3. Set `tunerRunRequest` to `1`.

Real-audio runs do not increment `pass_count` or `fail_count`, because the WAV
is an external recording, not one of the 18 synthetic flat/in-tune/sharp test
cases.

## Test Index Map

Each string has three tests: flat, in tune, sharp.

| Test | String | Input |
| ---: | --- | --- |
| 0 | low E / 82.41 Hz | -20 cents |
| 1 | low E / 82.41 Hz | in tune |
| 2 | low E / 82.41 Hz | +20 cents |
| 3 | A / 110.00 Hz | -20 cents |
| 4 | A / 110.00 Hz | in tune |
| 5 | A / 110.00 Hz | +20 cents |
| 6 | D / 146.83 Hz | -20 cents |
| 7 | D / 146.83 Hz | in tune |
| 8 | D / 146.83 Hz | +20 cents |
| 9 | G / 196.00 Hz | -20 cents |
| 10 | G / 196.00 Hz | in tune |
| 11 | G / 196.00 Hz | +20 cents |
| 12 | B / 246.94 Hz | -20 cents |
| 13 | B / 246.94 Hz | in tune |
| 14 | B / 246.94 Hz | +20 cents |
| 15 | high E / 329.63 Hz | -20 cents |
| 16 | high E / 329.63 Hz | in tune |
| 17 | high E / 329.63 Hz | +20 cents |

## State Values

- `1`: in tune
- `2`: flat
- `3`: sharp
- `4`: no signal
- `5`: error

The in-tune tolerance is `+/-5 cents`, reported as `+/-50` in
`cents_error_x10`.

## Correlation Detector

The branch now runs a second detector based on CMSIS-DSP
`arm_correlate_f32`. The original YIN-style detector still drives
`tunerDiag.detected_hz`, `tunerDiag.cents_error_x10`, and
`tunerDiag.tuning_state`.

The correlation detector uses a 2048-sample frame and chooses the first strong
autocorrelation peak. This avoids octave errors where a later, larger peak
would report half the real frequency.

The correlation result is published separately:

- `tunerDiag.corr_detected_hz`
- `tunerDiag.corr_cents_error_x10`
- `tunerDiag.corr_confidence`
- `tunerDiag.corr_tau`
- `tunerDiag.corr_state`
- `tunerDiag.corr_pass_count`
- `tunerDiag.corr_fail_count`

For the correlation detector, `corr_fail_count` should also stay at `0` after
the startup test run.

## Running Tests

At startup, all 18 tests run once, then the selected display test is refreshed.
`fail_count` should stay at `0`.

To rerun one test:

1. Set `tunerSelectedTest` to a value from `0` to `17`.
2. Set `tunerRunRequest` to `1`.

To rerun all tests, set `tunerRunRequest` to `0xFFFFFFFF`.

To stop the automatic graph demo, set `tunerAutoDemoEnabled` to `0`.

## FFT View

The firmware computes an FFT after each generated test input and exposes:

- six string-focused magnitudes in `tunerDiag.fft_*_mag`
- a compact 64-point graph array in `tunerSpectrum64`
- the strongest low-frequency bin in `tunerDiag.fft_peak_hz`

For example, with `tunerSelectedTest = 16`, high E in tune, the high-E
magnitude and peak should dominate near 330 Hz.
