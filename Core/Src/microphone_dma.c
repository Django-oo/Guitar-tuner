#include "microphone_dma.h"

#include <string.h>

uint16_t micDmaBuffer[MIC_DMA_BUFFER_LENGTH] __attribute__((aligned(4)));
uint16_t micDmaCaptureBuffer[MIC_DMA_CAPTURE_LENGTH] __attribute__((aligned(4)));
volatile MicrophoneDmaDiagnostics micDmaDiag;
volatile uint32_t micDmaCaptureControl;

static ADC_HandleTypeDef *micAdcHandle;
static TIM_HandleTypeDef *micTriggerTimerHandle;
static volatile uint8_t micHalfPending;
static volatile uint8_t micFullPending;
static volatile uint32_t micLastCallbackTickMs;

static void MicrophoneDma_ResetDiagnostics(void)
{
  (void)memset((void *)&micDmaDiag, 0, sizeof(micDmaDiag));
  micDmaDiag.state = MIC_DMA_STATE_STOPPED;
  micDmaDiag.last_error = MIC_DMA_ERROR_NONE;
  micDmaDiag.lifetime_min = 4095U;
  micDmaDiag.lifetime_max = 0U;
  micDmaDiag.callback_delta_min_ms = 0xFFFFFFFFU;
  micDmaDiag.capture_armed = 1U;
  micDmaCaptureControl = 0U;
}

static void MicrophoneDma_ArmCapture(void)
{
  micDmaDiag.capture_armed = 1U;
  micDmaDiag.capture_ready = 0U;
  micDmaDiag.capture_block_index = 0U;
  micDmaDiag.capture_mean = 0U;
  micDmaDiag.capture_amplitude = 0U;
  micDmaDiag.capture_max_abs_from_mean = 0U;
}

static void MicrophoneDma_HandleCaptureControl(void)
{
  if (micDmaCaptureControl == MIC_DMA_CAPTURE_CONTROL_ARM)
  {
    MicrophoneDma_ArmCapture();
    micDmaCaptureControl = 0U;
  }
}

static void MicrophoneDma_RecordHalError(MicrophoneDmaError error)
{
  micDmaDiag.last_error = error;
  micDmaDiag.state = MIC_DMA_STATE_ERROR;

  if (micAdcHandle != NULL)
  {
    micDmaDiag.adc_error_code = micAdcHandle->ErrorCode;

    if (micAdcHandle->DMA_Handle != NULL)
    {
      micDmaDiag.dma_error_code = micAdcHandle->DMA_Handle->ErrorCode;
    }
  }
}

static uint8_t MicrophoneDma_IsExpectedAdc(ADC_HandleTypeDef *hadc)
{
  return (hadc != NULL) && (hadc == micAdcHandle);
}

static uint8_t MicrophoneDma_ConsumePending(volatile uint8_t *pending)
{
  uint8_t ready = 0U;
  uint32_t primask = __get_PRIMASK();

  __disable_irq();

  if (*pending > 0U)
  {
    (*pending)--;
    ready = 1U;
  }

  if (primask == 0U)
  {
    __enable_irq();
  }

  return ready;
}

static void MicrophoneDma_AddPending(volatile uint8_t *pending,
                                     volatile uint32_t *overrun_counter)
{
  if (*pending < 255U)
  {
    (*pending)++;
  }
  else
  {
    (*overrun_counter)++;
  }
}

static void MicrophoneDma_UpdateDiagnostics(const MicrophoneDmaBlockStats *stats,
                                            uint32_t block_index)
{
  uint32_t signal_present = 0U;

  if (stats->amplitude >= MIC_DMA_ACTIVITY_AMPLITUDE_MIN)
  {
    signal_present = 1U;
  }

  micDmaDiag.block_count++;
  micDmaDiag.last_block_index = block_index;
  micDmaDiag.min = stats->min;
  micDmaDiag.max = stats->max;

  if (stats->min < micDmaDiag.lifetime_min)
  {
    micDmaDiag.lifetime_min = stats->min;
  }

  if (stats->max > micDmaDiag.lifetime_max)
  {
    micDmaDiag.lifetime_max = stats->max;
  }

  if (stats->amplitude > micDmaDiag.lifetime_amplitude_max)
  {
    micDmaDiag.lifetime_amplitude_max = stats->amplitude;
  }

  if (stats->max_abs_from_mean > micDmaDiag.lifetime_max_abs_from_mean)
  {
    micDmaDiag.lifetime_max_abs_from_mean = stats->max_abs_from_mean;
  }

  micDmaDiag.mean = stats->mean;
  micDmaDiag.amplitude = stats->amplitude;
  micDmaDiag.avg_abs_centered = stats->avg_abs_centered;
  micDmaDiag.max_abs_centered = stats->max_abs_centered;
  micDmaDiag.avg_abs_from_mean = stats->avg_abs_from_mean;
  micDmaDiag.max_abs_from_mean = stats->max_abs_from_mean;
  micDmaDiag.zero_crossings = stats->zero_crossings;
  micDmaDiag.clipped_low_count = stats->clipped_low_count;
  micDmaDiag.clipped_high_count = stats->clipped_high_count;
  micDmaDiag.signal_present = signal_present;

  if (signal_present != 0U)
  {
    micDmaDiag.active_block_count++;
  }
  else
  {
    micDmaDiag.quiet_block_count++;
  }
}

static void MicrophoneDma_UpdateClockDiagnostics(void)
{
  uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
  uint32_t timclk = pclk1;
  uint32_t prescaler = micTriggerTimerHandle->Init.Prescaler + 1U;
  uint32_t period = micTriggerTimerHandle->Init.Period + 1U;

  if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_HCLK_DIV1)
  {
    timclk = pclk1 * 2U;
  }

  micDmaDiag.sysclk_hz = HAL_RCC_GetSysClockFreq();
  micDmaDiag.pclk1_hz = pclk1;
  micDmaDiag.tim2_clk_hz = timclk;
  micDmaDiag.tim2_prescaler = micTriggerTimerHandle->Init.Prescaler;
  micDmaDiag.tim2_period = micTriggerTimerHandle->Init.Period;
  micDmaDiag.expected_sample_rate_hz = timclk / (prescaler * period);
}

static void MicrophoneDma_RecordCallbackTiming(void)
{
  uint32_t now = HAL_GetTick();

  if (micLastCallbackTickMs != 0U)
  {
    uint32_t delta = now - micLastCallbackTickMs;

    micDmaDiag.callback_delta_ms = delta;

    if (delta != 0U)
    {
      if (delta < micDmaDiag.callback_delta_min_ms)
      {
        micDmaDiag.callback_delta_min_ms = delta;
      }

      if (delta > micDmaDiag.callback_delta_max_ms)
      {
        micDmaDiag.callback_delta_max_ms = delta;
      }

      micDmaDiag.estimated_sample_rate_hz =
          (MIC_DMA_BLOCK_LENGTH * 1000U) / delta;
    }
  }

  micLastCallbackTickMs = now;
}

static void MicrophoneDma_TryCapture(const uint16_t *block,
                                     const MicrophoneDmaBlockStats *stats,
                                     uint32_t block_index)
{
  if ((block == NULL) || (stats == NULL))
  {
    return;
  }

  if ((micDmaDiag.capture_armed == 0U) ||
      (stats->amplitude < MIC_DMA_CAPTURE_AMPLITUDE_MIN))
  {
    return;
  }

  (void)memcpy(micDmaCaptureBuffer, block, sizeof(micDmaCaptureBuffer));
  micDmaDiag.capture_armed = 0U;
  micDmaDiag.capture_ready = 1U;
  micDmaDiag.capture_count++;
  micDmaDiag.capture_block_index = block_index;
  micDmaDiag.capture_mean = stats->mean;
  micDmaDiag.capture_amplitude = stats->amplitude;
  micDmaDiag.capture_max_abs_from_mean = stats->max_abs_from_mean;
}

static void MicrophoneDma_ProcessBlock(uint32_t block_index)
{
  MicrophoneDmaBlockStats stats;
  const uint16_t *block = &micDmaBuffer[block_index * MIC_DMA_BLOCK_LENGTH];

  MicrophoneDma_AnalyzeSamples(block, MIC_DMA_BLOCK_LENGTH, &stats);
  MicrophoneDma_UpdateDiagnostics(&stats, block_index);
  MicrophoneDma_TryCapture(block, &stats, block_index);
}

HAL_StatusTypeDef MicrophoneDma_Start(ADC_HandleTypeDef *hadc,
                                      TIM_HandleTypeDef *trigger_timer)
{
  uint32_t self_test_result;

  MicrophoneDma_ResetDiagnostics();
  self_test_result = MicrophoneDma_RunAnalysisSelfTest();
  micDmaDiag.self_test_result = self_test_result;

  if ((hadc == NULL) || (trigger_timer == NULL))
  {
    MicrophoneDma_RecordHalError(MIC_DMA_ERROR_NULL_HANDLE);
    return HAL_ERROR;
  }

  if (self_test_result != 0U)
  {
    MicrophoneDma_RecordHalError(MIC_DMA_ERROR_SELF_TEST);
    return HAL_ERROR;
  }

  micAdcHandle = hadc;
  micTriggerTimerHandle = trigger_timer;
  micHalfPending = 0U;
  micFullPending = 0U;
  micLastCallbackTickMs = 0U;
  micDmaDiag.state = MIC_DMA_STATE_STARTING;
  MicrophoneDma_UpdateClockDiagnostics();

  if (HAL_ADC_Start_DMA(micAdcHandle,
                        (uint32_t *)micDmaBuffer,
                        MIC_DMA_BUFFER_LENGTH) != HAL_OK)
  {
    MicrophoneDma_RecordHalError(MIC_DMA_ERROR_ADC_START);
    return HAL_ERROR;
  }

  if (HAL_TIM_Base_Start(micTriggerTimerHandle) != HAL_OK)
  {
    (void)HAL_ADC_Stop_DMA(micAdcHandle);
    MicrophoneDma_RecordHalError(MIC_DMA_ERROR_TIM_START);
    return HAL_ERROR;
  }

  micDmaDiag.state = MIC_DMA_STATE_RUNNING;
  micDmaDiag.start_count++;

  return HAL_OK;
}

HAL_StatusTypeDef MicrophoneDma_Stop(void)
{
  HAL_StatusTypeDef status = HAL_OK;

  if (micTriggerTimerHandle != NULL)
  {
    status = HAL_TIM_Base_Stop(micTriggerTimerHandle);
  }

  if ((status == HAL_OK) && (micAdcHandle != NULL))
  {
    status = HAL_ADC_Stop_DMA(micAdcHandle);
  }

  if (status == HAL_OK)
  {
    micDmaDiag.state = MIC_DMA_STATE_STOPPED;
  }

  return status;
}

uint32_t MicrophoneDma_Task(void)
{
  uint32_t processed_blocks = 0U;

  MicrophoneDma_HandleCaptureControl();

  while (MicrophoneDma_ConsumePending(&micHalfPending) != 0U)
  {
    MicrophoneDma_ProcessBlock(0U);
    processed_blocks++;
  }

  while (MicrophoneDma_ConsumePending(&micFullPending) != 0U)
  {
    MicrophoneDma_ProcessBlock(1U);
    processed_blocks++;
  }

  return processed_blocks;
}

void MicrophoneDma_OnAdcHalfCplt(ADC_HandleTypeDef *hadc)
{
  if (MicrophoneDma_IsExpectedAdc(hadc) != 0U)
  {
    micDmaDiag.half_callback_count++;
    MicrophoneDma_RecordCallbackTiming();
    MicrophoneDma_AddPending(&micHalfPending,
                             &micDmaDiag.half_pending_overrun_count);
  }
}

void MicrophoneDma_OnAdcCplt(ADC_HandleTypeDef *hadc)
{
  if (MicrophoneDma_IsExpectedAdc(hadc) != 0U)
  {
    micDmaDiag.full_callback_count++;
    MicrophoneDma_RecordCallbackTiming();
    MicrophoneDma_AddPending(&micFullPending,
                             &micDmaDiag.full_pending_overrun_count);
  }
}

void MicrophoneDma_OnAdcError(ADC_HandleTypeDef *hadc)
{
  if (MicrophoneDma_IsExpectedAdc(hadc) != 0U)
  {
    MicrophoneDma_RecordHalError(MIC_DMA_ERROR_ADC_CALLBACK);
  }
}

void MicrophoneDma_AnalyzeSamples(const uint16_t *samples,
                                  uint32_t length,
                                  MicrophoneDmaBlockStats *stats)
{
  uint32_t min_value = 4095U;
  uint32_t max_value = 0U;
  uint32_t sum = 0U;
  uint32_t sum_abs_centered = 0U;
  uint32_t max_abs_centered = 0U;
  uint32_t sum_abs_from_mean = 0U;
  uint32_t max_abs_from_mean = 0U;
  uint32_t zero_crossings = 0U;
  uint32_t clipped_low_count = 0U;
  uint32_t clipped_high_count = 0U;
  int32_t last_sign = 0;

  if (stats == NULL)
  {
    return;
  }

  (void)memset(stats, 0, sizeof(*stats));

  if ((samples == NULL) || (length == 0U))
  {
    return;
  }

  for (uint32_t i = 0U; i < length; i++)
  {
    uint32_t sample = samples[i];
    int32_t centered = (int32_t)sample - (int32_t)MIC_DMA_ADC_MIDPOINT;
    uint32_t abs_centered;
    int32_t sign = 0;

    if (sample < min_value)
    {
      min_value = sample;
    }

    if (sample > max_value)
    {
      max_value = sample;
    }

    if (sample == 0U)
    {
      clipped_low_count++;
    }
    else if (sample == 4095U)
    {
      clipped_high_count++;
    }

    abs_centered = (centered < 0) ? (uint32_t)(-centered) : (uint32_t)centered;
    sum += sample;
    sum_abs_centered += abs_centered;

    if (abs_centered > max_abs_centered)
    {
      max_abs_centered = abs_centered;
    }

    if (centered > 0)
    {
      sign = 1;
    }
    else if (centered < 0)
    {
      sign = -1;
    }

    if ((sign != 0) && (last_sign != 0) && (sign != last_sign))
    {
      zero_crossings++;
    }

    if (sign != 0)
    {
      last_sign = sign;
    }
  }

  stats->min = min_value;
  stats->max = max_value;
  stats->mean = sum / length;
  stats->amplitude = max_value - min_value;
  stats->avg_abs_centered = sum_abs_centered / length;
  stats->max_abs_centered = max_abs_centered;

  for (uint32_t i = 0U; i < length; i++)
  {
    int32_t relative = (int32_t)samples[i] - (int32_t)stats->mean;
    uint32_t abs_relative =
        (relative < 0) ? (uint32_t)(-relative) : (uint32_t)relative;

    sum_abs_from_mean += abs_relative;

    if (abs_relative > max_abs_from_mean)
    {
      max_abs_from_mean = abs_relative;
    }
  }

  stats->avg_abs_from_mean = sum_abs_from_mean / length;
  stats->max_abs_from_mean = max_abs_from_mean;
  stats->zero_crossings = zero_crossings;
  stats->clipped_low_count = clipped_low_count;
  stats->clipped_high_count = clipped_high_count;
}

uint32_t MicrophoneDma_RunAnalysisSelfTest(void)
{
  uint32_t failures = 0U;
  MicrophoneDmaBlockStats stats;
  const uint16_t flat[8] = {
    2048U, 2048U, 2048U, 2048U, 2048U, 2048U, 2048U, 2048U
  };
  const uint16_t wave[8] = {
    2048U, 2148U, 2048U, 1948U, 2048U, 2148U, 2048U, 1948U
  };

  MicrophoneDma_AnalyzeSamples(flat, 8U, &stats);

  if ((stats.min != 2048U) || (stats.max != 2048U) ||
      (stats.mean != 2048U) || (stats.amplitude != 0U) ||
      (stats.max_abs_centered != 0U))
  {
    failures |= 0x01U;
  }

  MicrophoneDma_AnalyzeSamples(wave, 8U, &stats);

  if ((stats.min != 1948U) || (stats.max != 2148U) ||
      (stats.mean != 2048U) || (stats.amplitude != 200U) ||
      (stats.max_abs_centered != 100U))
  {
    failures |= 0x02U;
  }

  if (stats.zero_crossings == 0U)
  {
    failures |= 0x04U;
  }

  return failures;
}
