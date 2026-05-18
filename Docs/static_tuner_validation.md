# Static Tuner Validation

This branch validates the guitar tuner algorithm with synthetic inputs instead
of the microphone.

## Debug Variables

Add these to Live Expressions:

- `tunerSelectedTest`
- `tunerRunRequest`
- `tunerDiag.initialized`
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
- `tunerDiag.signal_amplitude`
- `tunerDiag.yin_tau`

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

## Running Tests

At startup, all 18 tests run once. `fail_count` should stay at `0`.

To rerun one test:

1. Set `tunerSelectedTest` to a value from `0` to `17`.
2. Set `tunerRunRequest` to `1`.

To rerun all tests, set `tunerRunRequest` to `0xFFFFFFFF`.
