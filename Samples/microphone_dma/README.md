# Microphone DMA Capture Samples

This folder stores paired acquisition tests.

Each STM capture run creates a timestamped subfolder containing:

- `stm.csv`: raw ADC samples frozen from the STM32 DMA capture buffer.
- `stm_poll.txt`: polling diagnostics collected during the run.
- `manifest.json`: capture label, timing, and file paths.

Use `Tools\play_reference_with_stm_capture.ps1` to play a downloaded reference
audio file while arming the STM32 capture.
