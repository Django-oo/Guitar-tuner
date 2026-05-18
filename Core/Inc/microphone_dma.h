#ifndef MICROPHONE_DMA_H
#define MICROPHONE_DMA_H

#include "stm32f4xx_hal.h"

#include <stdint.h>

#define MIC_DMA_SAMPLE_RATE_HZ          16000U
#define MIC_DMA_BUFFER_LENGTH           4096U
#define MIC_DMA_BLOCK_LENGTH            (MIC_DMA_BUFFER_LENGTH / 2U)
#define MIC_DMA_CAPTURE_LENGTH          MIC_DMA_BLOCK_LENGTH
#define MIC_DMA_ADC_MIDPOINT            2048U
#define MIC_DMA_ACTIVITY_AMPLITUDE_MIN  24U
#define MIC_DMA_CAPTURE_AMPLITUDE_MIN   64U
#define MIC_DMA_CAPTURE_CONTROL_ARM     0xA5A50001U

typedef enum
{
  MIC_DMA_STATE_STOPPED = 0,
  MIC_DMA_STATE_STARTING = 1,
  MIC_DMA_STATE_RUNNING = 2,
  MIC_DMA_STATE_ERROR = 3
} MicrophoneDmaState;

typedef enum
{
  MIC_DMA_ERROR_NONE = 0,
  MIC_DMA_ERROR_NULL_HANDLE = 1,
  MIC_DMA_ERROR_SELF_TEST = 2,
  MIC_DMA_ERROR_ADC_START = 3,
  MIC_DMA_ERROR_TIM_START = 4,
  MIC_DMA_ERROR_ADC_CALLBACK = 5
} MicrophoneDmaError;

typedef struct
{
  uint32_t min;
  uint32_t max;
  uint32_t mean;
  uint32_t amplitude;
  uint32_t avg_abs_centered;
  uint32_t max_abs_centered;
  uint32_t avg_abs_from_mean;
  uint32_t max_abs_from_mean;
  uint32_t zero_crossings;
  uint32_t clipped_low_count;
  uint32_t clipped_high_count;
} MicrophoneDmaBlockStats;

typedef struct
{
  volatile uint32_t state;
  volatile uint32_t last_error;
  volatile uint32_t self_test_result;
  volatile uint32_t start_count;
  volatile uint32_t half_callback_count;
  volatile uint32_t full_callback_count;
  volatile uint32_t half_pending_overrun_count;
  volatile uint32_t full_pending_overrun_count;
  volatile uint32_t block_count;
  volatile uint32_t last_block_index;
  volatile uint32_t min;
  volatile uint32_t max;
  volatile uint32_t lifetime_min;
  volatile uint32_t lifetime_max;
  volatile uint32_t lifetime_amplitude_max;
  volatile uint32_t lifetime_max_abs_from_mean;
  volatile uint32_t mean;
  volatile uint32_t amplitude;
  volatile uint32_t avg_abs_centered;
  volatile uint32_t max_abs_centered;
  volatile uint32_t avg_abs_from_mean;
  volatile uint32_t max_abs_from_mean;
  volatile uint32_t zero_crossings;
  volatile uint32_t clipped_low_count;
  volatile uint32_t clipped_high_count;
  volatile uint32_t signal_present;
  volatile uint32_t quiet_block_count;
  volatile uint32_t active_block_count;
  volatile uint32_t adc_error_code;
  volatile uint32_t dma_error_code;
  volatile uint32_t sysclk_hz;
  volatile uint32_t pclk1_hz;
  volatile uint32_t tim2_clk_hz;
  volatile uint32_t tim2_prescaler;
  volatile uint32_t tim2_period;
  volatile uint32_t expected_sample_rate_hz;
  volatile uint32_t callback_delta_ms;
  volatile uint32_t callback_delta_min_ms;
  volatile uint32_t callback_delta_max_ms;
  volatile uint32_t estimated_sample_rate_hz;
  volatile uint32_t capture_armed;
  volatile uint32_t capture_ready;
  volatile uint32_t capture_count;
  volatile uint32_t capture_block_index;
  volatile uint32_t capture_mean;
  volatile uint32_t capture_amplitude;
  volatile uint32_t capture_max_abs_from_mean;
} MicrophoneDmaDiagnostics;

extern uint16_t micDmaBuffer[MIC_DMA_BUFFER_LENGTH];
extern uint16_t micDmaCaptureBuffer[MIC_DMA_CAPTURE_LENGTH];
extern volatile MicrophoneDmaDiagnostics micDmaDiag;
extern volatile uint32_t micDmaCaptureControl;

HAL_StatusTypeDef MicrophoneDma_Start(ADC_HandleTypeDef *hadc,
                                      TIM_HandleTypeDef *trigger_timer);
HAL_StatusTypeDef MicrophoneDma_Stop(void);
uint32_t MicrophoneDma_Task(void);

void MicrophoneDma_OnAdcHalfCplt(ADC_HandleTypeDef *hadc);
void MicrophoneDma_OnAdcCplt(ADC_HandleTypeDef *hadc);
void MicrophoneDma_OnAdcError(ADC_HandleTypeDef *hadc);

void MicrophoneDma_AnalyzeSamples(const uint16_t *samples,
                                  uint32_t length,
                                  MicrophoneDmaBlockStats *stats);
uint32_t MicrophoneDma_RunAnalysisSelfTest(void);

#endif /* MICROPHONE_DMA_H */
