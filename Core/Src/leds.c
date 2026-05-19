#include "leds.h"

#include "main.h"
#include "static_tuner.h"

#include <string.h>

#define LED_RING_STATUS_FIRST_LED       6U
#define LED_RING_STATUS_LAST_LED        8U
#define LED_RING_ALL_STRINGS_MASK       0x3FU
#define LED_RING_RESET_SLOTS            48U
#define LED_RING_BITS_PER_LED           24U
#define LED_RING_PWM_WORDS              \
  ((LED_RING_COUNT * LED_RING_BITS_PER_LED) + LED_RING_RESET_SLOTS)
#define LED_RING_TIMER_PERIOD           104U
#define LED_RING_DUTY_ZERO              33U
#define LED_RING_DUTY_ONE               67U
#define LED_RING_REFRESH_MS             40U
#define LED_RING_BLINK_MS               250U
#define LED_RING_COLOR_LEVEL            18U

typedef struct
{
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} LedColor;

static TIM_HandleTypeDef ledTimer;
DMA_HandleTypeDef hdma_tim4_ch1;

static uint16_t ledPwmData[LED_RING_PWM_WORDS];
static LedColor ledColors[LED_RING_COUNT];
static uint32_t ledInitialized;
static uint32_t ledLastRefreshMs;
static uint32_t ledLastBlinkMs;
static uint32_t ledBlinkOn;

volatile uint32_t ledRingStatus = LED_RING_STATUS_STOPPED;
volatile uint32_t ledRingTunedStringMask;
volatile uint32_t ledRingDmaBusy;
volatile uint32_t ledRingLastError;

static const LedColor LED_OFF = { 0U, 0U, 0U };
static const LedColor LED_RED = { LED_RING_COLOR_LEVEL, 0U, 0U };
static const LedColor LED_GREEN = { 0U, LED_RING_COLOR_LEVEL, 0U };
static const LedColor LED_BLUE = { 0U, 0U, LED_RING_COLOR_LEVEL };

static void Leds_RecordError(uint32_t error)
{
  ledRingLastError = error;
  ledRingStatus = LED_RING_STATUS_FAILED;
}

static uint32_t Leds_InitHardware(void)
{
  GPIO_InitTypeDef gpio = { 0 };
  TIM_OC_InitTypeDef output_compare = { 0 };

  if (ledInitialized != 0U)
  {
    return 1U;
  }

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_TIM4_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  gpio.Pin = LED_RING_DATA_Pin;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF2_TIM4;
  HAL_GPIO_Init(LED_RING_DATA_GPIO_Port, &gpio);

  ledTimer.Instance = TIM4;
  ledTimer.Init.Prescaler = 0U;
  ledTimer.Init.CounterMode = TIM_COUNTERMODE_UP;
  ledTimer.Init.Period = LED_RING_TIMER_PERIOD;
  ledTimer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  ledTimer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

  if (HAL_TIM_PWM_Init(&ledTimer) != HAL_OK)
  {
    Leds_RecordError(1U);
    return 0U;
  }

  output_compare.OCMode = TIM_OCMODE_PWM1;
  output_compare.Pulse = 0U;
  output_compare.OCPolarity = TIM_OCPOLARITY_HIGH;
  output_compare.OCFastMode = TIM_OCFAST_DISABLE;

  if (HAL_TIM_PWM_ConfigChannel(&ledTimer,
                                &output_compare,
                                TIM_CHANNEL_1) != HAL_OK)
  {
    Leds_RecordError(2U);
    return 0U;
  }

  hdma_tim4_ch1.Instance = DMA1_Stream0;
  hdma_tim4_ch1.Init.Channel = DMA_CHANNEL_2;
  hdma_tim4_ch1.Init.Direction = DMA_MEMORY_TO_PERIPH;
  hdma_tim4_ch1.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_tim4_ch1.Init.MemInc = DMA_MINC_ENABLE;
  hdma_tim4_ch1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  hdma_tim4_ch1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
  hdma_tim4_ch1.Init.Mode = DMA_NORMAL;
  hdma_tim4_ch1.Init.Priority = DMA_PRIORITY_HIGH;
  hdma_tim4_ch1.Init.FIFOMode = DMA_FIFOMODE_DISABLE;

  if (HAL_DMA_Init(&hdma_tim4_ch1) != HAL_OK)
  {
    Leds_RecordError(3U);
    return 0U;
  }

  __HAL_LINKDMA(&ledTimer, hdma[TIM_DMA_ID_CC1], hdma_tim4_ch1);

  HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 2U, 0U);
  HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);

  ledBlinkOn = 1U;
  ledLastBlinkMs = HAL_GetTick();
  ledInitialized = 1U;

  return 1U;
}

static void Leds_SetAll(LedColor color)
{
  for (uint32_t i = 0U; i < LED_RING_COUNT; i++)
  {
    ledColors[i] = color;
  }
}

static void Leds_SetStatusCluster(LedColor color)
{
  for (uint32_t i = LED_RING_STATUS_FIRST_LED;
       i <= LED_RING_STATUS_LAST_LED;
       i++)
  {
    ledColors[i] = color;
  }
}

static void Leds_UpdateTunedStringMask(void)
{
  uint32_t display_string = tunerDiag.display_string;

  if ((tunerDiag.input_source == STATIC_TUNER_INPUT_SYNTH) &&
      (tunerDiag.all_tests_run_count != 0U) &&
      (tunerDiag.fail_count == 0U) &&
      (tunerDiag.corr_fail_count == 0U))
  {
    ledRingTunedStringMask = LED_RING_ALL_STRINGS_MASK;
    return;
  }

  if ((display_string < LED_RING_STRING_COUNT) &&
      (tunerDiag.display_state == STATIC_TUNER_STATE_IN_TUNE))
  {
    ledRingTunedStringMask |= (1UL << display_string);
  }
}

static uint32_t Leds_GetCurrentStatus(void)
{
  if ((ledRingLastError != 0U) ||
      (tunerDiag.initialized == 0U) ||
      (tunerDiag.last_error != 0U))
  {
    return LED_RING_STATUS_FAILED;
  }

  if (tunerDiag.input_source == STATIC_TUNER_INPUT_REAL)
  {
    if (tunerDiag.real_sequence_done != 0U)
    {
      return LED_RING_STATUS_SUCCESS;
    }
  }
  else if (tunerDiag.input_source == STATIC_TUNER_INPUT_SYNTH)
  {
    if ((tunerDiag.all_tests_run_count != 0U) &&
        (tunerDiag.fail_count == 0U) &&
        (tunerDiag.corr_fail_count == 0U))
    {
      return LED_RING_STATUS_SUCCESS;
    }

    if ((tunerDiag.fail_count != 0U) || (tunerDiag.corr_fail_count != 0U))
    {
      return LED_RING_STATUS_FAILED;
    }
  }
  else
  {
    return LED_RING_STATUS_FAILED;
  }

  return LED_RING_STATUS_RUNNING;
}

static void Leds_UpdateModel(void)
{
  uint32_t status = Leds_GetCurrentStatus();
  uint32_t now = HAL_GetTick();

  if ((now - ledLastBlinkMs) >= LED_RING_BLINK_MS)
  {
    ledLastBlinkMs = now;
    ledBlinkOn = (ledBlinkOn == 0U) ? 1U : 0U;
  }

  Leds_UpdateTunedStringMask();
  Leds_SetAll(LED_OFF);

  for (uint32_t i = 0U; i < LED_RING_STRING_COUNT; i++)
  {
    if ((ledRingTunedStringMask & (1UL << i)) != 0U)
    {
      ledColors[i] = LED_GREEN;
    }
    else
    {
      ledColors[i] = LED_RED;
    }
  }

  if (status == LED_RING_STATUS_SUCCESS)
  {
    Leds_SetStatusCluster(LED_BLUE);
  }
  else if (status == LED_RING_STATUS_RUNNING)
  {
    Leds_SetStatusCluster((ledBlinkOn != 0U) ? LED_BLUE : LED_OFF);
  }
  else
  {
    Leds_SetStatusCluster(LED_RED);
  }

  ledRingStatus = status;
}

static void Leds_BuildPwmData(void)
{
  uint32_t index = 0U;

  for (uint32_t led = 0U; led < LED_RING_COUNT; led++)
  {
    uint8_t grb[3] = {
      ledColors[led].green,
      ledColors[led].red,
      ledColors[led].blue
    };

    for (uint32_t byte = 0U; byte < 3U; byte++)
    {
      for (uint32_t bit = 0U; bit < 8U; bit++)
      {
        if ((grb[byte] & (uint8_t)(0x80U >> bit)) != 0U)
        {
          ledPwmData[index++] = LED_RING_DUTY_ONE;
        }
        else
        {
          ledPwmData[index++] = LED_RING_DUTY_ZERO;
        }
      }
    }
  }

  (void)memset(&ledPwmData[index],
               0,
               (LED_RING_PWM_WORDS - index) * sizeof(ledPwmData[0]));
}

static void Leds_Send(void)
{
  if (ledRingDmaBusy != 0U)
  {
    return;
  }

  Leds_BuildPwmData();
  ledRingDmaBusy = 1U;

  if (HAL_TIM_PWM_Start_DMA(&ledTimer,
                            TIM_CHANNEL_1,
                            (uint32_t *)ledPwmData,
                            LED_RING_PWM_WORDS) != HAL_OK)
  {
    ledRingDmaBusy = 0U;
    Leds_RecordError(4U);
  }
}

void Leds_Task(void)
{
  uint32_t now = HAL_GetTick();

  if (Leds_InitHardware() == 0U)
  {
    return;
  }

  if ((now - ledLastRefreshMs) < LED_RING_REFRESH_MS)
  {
    return;
  }

  ledLastRefreshMs = now;
  Leds_UpdateModel();
  Leds_Send();
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
  if ((htim != NULL) && (htim->Instance == TIM4))
  {
    (void)HAL_TIM_PWM_Stop_DMA(&ledTimer, TIM_CHANNEL_1);
    __HAL_TIM_SET_COMPARE(&ledTimer, TIM_CHANNEL_1, 0U);
    ledRingDmaBusy = 0U;
  }
}
