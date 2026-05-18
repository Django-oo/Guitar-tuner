# Microphone DMA Validation

This branch keeps the microphone acquisition isolated in:

- `Core/Inc/microphone_dma.h`
- `Core/Src/microphone_dma.c`

The main loop only calls `MicrophoneDma_Task()`. ADC DMA callbacks only forward events to the module.

## IDE-Independent Polling

To check the same values without relying on CubeIDE Live Expressions, run:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\Tools\read_mic_dma_diag.ps1 -Polls 20 -DelayMs 500
```

If several ST-LINK probes are connected, add:

```powershell
-SerialNumber 0670FF525282494867244060
```

The script reads symbol addresses from `Debug\TP_Calcul_Rapide.elf`, attaches in SWD
hotplug mode, reads RAM, and resumes the core after each poll.

To freeze and export one raw 2048-sample block after the next detected sound:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\Tools\read_mic_dma_diag.ps1 -ArmCapture -Polls 30 -DelayMs 250 -CaptureCsv .\capture_voice.csv -SerialNumber 0670FF525282494867244060
```

The capture triggers when a block amplitude reaches `MIC_DMA_CAPTURE_AMPLITUDE_MIN`
and then keeps the samples stable in `micDmaCaptureBuffer` until another capture is
armed. Use `cap_ready`, `cap_amp`, and `cap_from_mean` to confirm the capture.

## Live Expressions

Add these symbols while debugging:

- `micDmaDiag.state`
- `micDmaDiag.last_error`
- `micDmaDiag.self_test_result`
- `micDmaDiag.half_callback_count`
- `micDmaDiag.full_callback_count`
- `micDmaDiag.block_count`
- `micDmaDiag.min`
- `micDmaDiag.max`
- `micDmaDiag.lifetime_min`
- `micDmaDiag.lifetime_max`
- `micDmaDiag.lifetime_amplitude_max`
- `micDmaDiag.lifetime_max_abs_from_mean`
- `micDmaDiag.mean`
- `micDmaDiag.amplitude`
- `micDmaDiag.avg_abs_centered`
- `micDmaDiag.max_abs_centered`
- `micDmaDiag.avg_abs_from_mean`
- `micDmaDiag.max_abs_from_mean`
- `micDmaDiag.signal_present`
- `micDmaDiag.expected_sample_rate_hz`
- `micDmaDiag.estimated_sample_rate_hz`
- `micDmaDiag.capture_armed`
- `micDmaDiag.capture_ready`
- `micDmaDiag.capture_amplitude`
- `micDmaDiag.capture_max_abs_from_mean`
- `micDmaDiag.callback_delta_ms`
- `micDmaDiag.adc_error_code`
- `micDmaDiag.dma_error_code`
- `micDmaProcessedBlocks`

## Expected Values

With ADC1 triggered by TIM2 at 16 kHz and a 4096-sample circular DMA buffer:

- `micDmaDiag.state` should be `2` after startup.
- `micDmaDiag.self_test_result` should be `0`.
- Half/full callbacks should increment about every 128 ms.
- `expected_sample_rate_hz` and `estimated_sample_rate_hz` should be close to `16000`.
- `micDmaDiag.block_count` should keep increasing in the main loop.
- `micDmaDiag.mean` should usually be near the microphone bias, often around `2048`.
- `micDmaDiag.amplitude` should rise clearly when clapping or playing a guitar string.
- `lifetime_amplitude_max` and `lifetime_max_abs_from_mean` latch the strongest block since boot,
  which is more reliable than trying to catch a short event in Live Expressions.

## Pin Check

This project currently samples `ADC_CHANNEL_0`, which is PA0 / ADC1_IN0 on the NUCLEO-F446RE.
If the microphone output is connected to PC0 instead, change the ADC regular channel to
`ADC_CHANNEL_10` and regenerate or update the GPIO/channel configuration accordingly.

## Quick Failure Map

- Callbacks stay at 0: check TIM2 start, ADC external trigger, DMA IRQ, and `HAL_ADC_Start_DMA`.
- Callbacks increment but block count stays at 0: the main loop is not reaching `MicrophoneDma_Task()`.
- Block count increments but amplitude stays near 0: wrong ADC pin, missing microphone bias, or no signal.
- `last_error != 0`: inspect `last_error`, `adc_error_code`, and `dma_error_code`.
