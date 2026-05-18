#include "static_tuner.h"

#include "arm_math.h"
#include "stm32f4xx_hal.h"

#include <math.h>
#include <stddef.h>

#define STATIC_TUNER_MIN_FREQ_HZ      70.0f
#define STATIC_TUNER_MAX_FREQ_HZ      400.0f
#define STATIC_TUNER_YIN_THRESHOLD    0.18f
#define STATIC_TUNER_SIGNAL_MIN_AMP   200U
#define STATIC_TUNER_PI               3.14159265358979323846f
#define STATIC_TUNER_TWO_PI           (2.0f * STATIC_TUNER_PI)
#define STATIC_TUNER_FFT_BIN_HZ       \
  ((float)STATIC_TUNER_SAMPLE_RATE_HZ / (float)STATIC_TUNER_FRAME_LENGTH)
#define STATIC_TUNER_CORR_FRAME_LENGTH 2048U
#define STATIC_TUNER_CORR_OUTPUT_LENGTH \
  ((2U * STATIC_TUNER_CORR_FRAME_LENGTH) - 1U)
#define STATIC_TUNER_CORR_MIN_CONFIDENCE 0.30f
#define STATIC_TUNER_CORR_FIRST_PEAK_RATIO 0.75f

typedef struct
{
  float frequency_hz;
} StaticTunerStringInfo;

typedef struct
{
  float detected_hz;
  float target_hz;
  float confidence;
  uint32_t detected_string;
  StaticTunerState state;
  int32_t cents_error_x10;
  uint32_t signal_amplitude;
  uint32_t yin_tau;
  float corr_detected_hz;
  float corr_confidence;
  uint32_t corr_tau;
  uint32_t corr_detected_string;
  StaticTunerState corr_state;
  int32_t corr_cents_error_x10;
} StaticTunerResult;

int16_t tunerStaticInput[STATIC_TUNER_FRAME_LENGTH];
volatile float tunerFftStringMagnitudes[STATIC_TUNER_STRING_COUNT];
volatile float tunerSpectrum64[STATIC_TUNER_SPECTRUM_BIN_COUNT];
volatile float tunerGraphLowE;
volatile float tunerGraphA;
volatile float tunerGraphD;
volatile float tunerGraphG;
volatile float tunerGraphB;
volatile float tunerGraphHighE;
volatile float tunerGraphPeakHz;
volatile float tunerGraphPeakMagnitude;
volatile int32_t tunerGraphCentsErrorX10;
volatile float tunerGraphCorrHz;
volatile int32_t tunerGraphCorrCentsErrorX10;
volatile StaticTunerDiagnostics tunerDiag;
volatile uint32_t tunerSelectedTest;
volatile uint32_t tunerRunRequest;
volatile uint32_t tunerAutoDemoEnabled;
volatile uint32_t tunerAutoDemoPeriodMs;

static float tunerYinDiff[(STATIC_TUNER_SAMPLE_RATE_HZ / 70U) + 2U];
static arm_rfft_fast_instance_f32 tunerFftInstance;
static uint32_t tunerFftInitialized;
static float tunerFftInput[STATIC_TUNER_FRAME_LENGTH];
static float tunerFftOutput[STATIC_TUNER_FRAME_LENGTH];
static float tunerCorrInput[STATIC_TUNER_CORR_FRAME_LENGTH];
static float tunerCorrOutput[STATIC_TUNER_CORR_OUTPUT_LENGTH];
static uint32_t tunerAutoDemoLastTickMs;
static uint32_t tunerAutoDemoTestIndex;

static const StaticTunerStringInfo tunerStrings[STATIC_TUNER_STRING_COUNT] = {
  { 82.41f },
  { 110.00f },
  { 146.83f },
  { 196.00f },
  { 246.94f },
  { 329.63f }
};

static const int32_t tunerTestOffsetsX10[STATIC_TUNER_TESTS_PER_STRING] = {
  -200,
  0,
  200
};

static const StaticTunerState tunerExpectedStates[STATIC_TUNER_TESTS_PER_STRING] = {
  STATIC_TUNER_STATE_FLAT,
  STATIC_TUNER_STATE_IN_TUNE,
  STATIC_TUNER_STATE_SHARP
};

static float StaticTuner_GetMean(const int16_t *samples, uint32_t length);

static float StaticTuner_AbsF(float value)
{
  return (value < 0.0f) ? -value : value;
}

static int32_t StaticTuner_RoundToI32(float value)
{
  if (value >= 0.0f)
  {
    return (int32_t)(value + 0.5f);
  }

  return (int32_t)(value - 0.5f);
}

static float StaticTuner_CentsFactor(int32_t cents_x10)
{
  float cents = (float)cents_x10 * 0.1f;

  return 1.0f + (cents * 0.00057762265f);
}

static uint32_t StaticTuner_GetStringIndex(uint32_t test_index)
{
  return test_index / STATIC_TUNER_TESTS_PER_STRING;
}

static uint32_t StaticTuner_GetOffsetIndex(uint32_t test_index)
{
  return test_index % STATIC_TUNER_TESTS_PER_STRING;
}

static uint32_t StaticTuner_IsValidTest(uint32_t test_index)
{
  return test_index < STATIC_TUNER_TEST_COUNT;
}

static uint32_t StaticTuner_FftBinFromHz(float frequency_hz)
{
  return (uint32_t)((frequency_hz / STATIC_TUNER_FFT_BIN_HZ) + 0.5f);
}

static float StaticTuner_GetFftMagnitude(uint32_t bin)
{
  float real;
  float imag;

  if (bin == 0U)
  {
    real = tunerFftOutput[0];
    imag = 0.0f;
  }
  else if (bin == (STATIC_TUNER_FRAME_LENGTH / 2U))
  {
    real = tunerFftOutput[1];
    imag = 0.0f;
  }
  else
  {
    real = tunerFftOutput[2U * bin];
    imag = tunerFftOutput[(2U * bin) + 1U];
  }

  return sqrtf((real * real) + (imag * imag));
}

static float StaticTuner_GetLocalFftMagnitude(float frequency_hz)
{
  uint32_t center_bin = StaticTuner_FftBinFromHz(frequency_hz);
  uint32_t max_bin = STATIC_TUNER_FRAME_LENGTH / 2U;
  uint32_t first_bin = (center_bin > 1U) ? (center_bin - 1U) : center_bin;
  uint32_t last_bin = center_bin + 1U;
  float best_magnitude = 0.0f;

  if (last_bin > max_bin)
  {
    last_bin = max_bin;
  }

  for (uint32_t bin = first_bin; bin <= last_bin; bin++)
  {
    float magnitude = StaticTuner_GetFftMagnitude(bin);

    if (magnitude > best_magnitude)
    {
      best_magnitude = magnitude;
    }
  }

  return best_magnitude;
}

static void StaticTuner_UpdateFftDiagnostics(const int16_t *samples,
                                             uint32_t length)
{
  float mean = StaticTuner_GetMean(samples, length);
  uint32_t peak_bin = 0U;
  float peak_magnitude = 0.0f;
  uint32_t max_graph_bin =
      StaticTuner_FftBinFromHz((float)STATIC_TUNER_SPECTRUM_MAX_HZ);

  if (tunerFftInitialized == 0U)
  {
    if (arm_rfft_fast_init_f32(&tunerFftInstance,
                               STATIC_TUNER_FRAME_LENGTH) != ARM_MATH_SUCCESS)
    {
      tunerDiag.last_error = 2U;
      return;
    }

    tunerFftInitialized = 1U;
  }

  for (uint32_t i = 0U; i < STATIC_TUNER_FRAME_LENGTH; i++)
  {
    float window =
        0.5f -
        (0.5f * arm_cos_f32(STATIC_TUNER_TWO_PI *
                            (float)i /
                            (float)(STATIC_TUNER_FRAME_LENGTH - 1U)));

    tunerFftInput[i] = ((float)samples[i] - mean) * window;
  }

  arm_rfft_fast_f32(&tunerFftInstance, tunerFftInput, tunerFftOutput, 0);

  tunerDiag.fft_low_e_mag =
      StaticTuner_GetLocalFftMagnitude(tunerStrings[STATIC_TUNER_STRING_LOW_E].frequency_hz);
  tunerDiag.fft_a_mag =
      StaticTuner_GetLocalFftMagnitude(tunerStrings[STATIC_TUNER_STRING_A].frequency_hz);
  tunerDiag.fft_d_mag =
      StaticTuner_GetLocalFftMagnitude(tunerStrings[STATIC_TUNER_STRING_D].frequency_hz);
  tunerDiag.fft_g_mag =
      StaticTuner_GetLocalFftMagnitude(tunerStrings[STATIC_TUNER_STRING_G].frequency_hz);
  tunerDiag.fft_b_mag =
      StaticTuner_GetLocalFftMagnitude(tunerStrings[STATIC_TUNER_STRING_B].frequency_hz);
  tunerDiag.fft_high_e_mag =
      StaticTuner_GetLocalFftMagnitude(tunerStrings[STATIC_TUNER_STRING_HIGH_E].frequency_hz);

  tunerFftStringMagnitudes[STATIC_TUNER_STRING_LOW_E] = tunerDiag.fft_low_e_mag;
  tunerFftStringMagnitudes[STATIC_TUNER_STRING_A] = tunerDiag.fft_a_mag;
  tunerFftStringMagnitudes[STATIC_TUNER_STRING_D] = tunerDiag.fft_d_mag;
  tunerFftStringMagnitudes[STATIC_TUNER_STRING_G] = tunerDiag.fft_g_mag;
  tunerFftStringMagnitudes[STATIC_TUNER_STRING_B] = tunerDiag.fft_b_mag;
  tunerFftStringMagnitudes[STATIC_TUNER_STRING_HIGH_E] = tunerDiag.fft_high_e_mag;

  if (max_graph_bin >= (STATIC_TUNER_FRAME_LENGTH / 2U))
  {
    max_graph_bin = (STATIC_TUNER_FRAME_LENGTH / 2U) - 1U;
  }

  for (uint32_t i = 0U; i < STATIC_TUNER_SPECTRUM_BIN_COUNT; i++)
  {
    uint32_t first_bin =
        (i * max_graph_bin) / STATIC_TUNER_SPECTRUM_BIN_COUNT;
    uint32_t last_bin =
        ((i + 1U) * max_graph_bin) / STATIC_TUNER_SPECTRUM_BIN_COUNT;
    float max_magnitude = 0.0f;

    if (first_bin == 0U)
    {
      first_bin = 1U;
    }

    if (last_bin < first_bin)
    {
      last_bin = first_bin;
    }

    for (uint32_t bin = first_bin; bin <= last_bin; bin++)
    {
      float magnitude = StaticTuner_GetFftMagnitude(bin);

      if (magnitude > max_magnitude)
      {
        max_magnitude = magnitude;
      }

    }

    tunerSpectrum64[i] = max_magnitude;
  }

  for (uint32_t bin = 1U; bin <= max_graph_bin; bin++)
  {
    float magnitude = StaticTuner_GetFftMagnitude(bin);

    if (magnitude > peak_magnitude)
    {
      peak_magnitude = magnitude;
      peak_bin = bin;
    }
  }

  tunerDiag.fft_peak_bin = peak_bin;
  tunerDiag.fft_peak_hz = (float)peak_bin * STATIC_TUNER_FFT_BIN_HZ;
  tunerDiag.fft_peak_mag = peak_magnitude;

  tunerGraphLowE = tunerDiag.fft_low_e_mag;
  tunerGraphA = tunerDiag.fft_a_mag;
  tunerGraphD = tunerDiag.fft_d_mag;
  tunerGraphG = tunerDiag.fft_g_mag;
  tunerGraphB = tunerDiag.fft_b_mag;
  tunerGraphHighE = tunerDiag.fft_high_e_mag;
  tunerGraphPeakHz = tunerDiag.fft_peak_hz;
  tunerGraphPeakMagnitude = tunerDiag.fft_peak_mag;
}

static uint32_t StaticTuner_GetSignalAmplitude(const int16_t *samples,
                                               uint32_t length)
{
  int16_t min_value = 32767;
  int16_t max_value = -32768;

  for (uint32_t i = 0U; i < length; i++)
  {
    if (samples[i] < min_value)
    {
      min_value = samples[i];
    }

    if (samples[i] > max_value)
    {
      max_value = samples[i];
    }
  }

  return (uint32_t)((int32_t)max_value - (int32_t)min_value);
}

static void StaticTuner_GenerateTone(uint32_t string_index,
                                     int32_t cents_offset_x10,
                                     int16_t *samples,
                                     uint32_t length)
{
  float base_hz = tunerStrings[string_index].frequency_hz;
  float frequency_hz = base_hz * StaticTuner_CentsFactor(cents_offset_x10);
  float phase = 0.0f;
  float phase_step =
      STATIC_TUNER_TWO_PI * frequency_hz / (float)STATIC_TUNER_SAMPLE_RATE_HZ;

  for (uint32_t i = 0U; i < length; i++)
  {
    float envelope = 1.0f - (0.25f * (float)i / (float)length);
    float sample =
        (0.80f * arm_sin_f32(phase)) +
        (0.15f * arm_sin_f32(2.0f * phase)) +
        (0.05f * arm_sin_f32(3.0f * phase));

    samples[i] = (int16_t)(sample * envelope * 14000.0f);
    phase += phase_step;

    while (phase >= STATIC_TUNER_TWO_PI)
    {
      phase -= STATIC_TUNER_TWO_PI;
    }
  }
}

static float StaticTuner_GetMean(const int16_t *samples, uint32_t length)
{
  int64_t sum = 0;

  for (uint32_t i = 0U; i < length; i++)
  {
    sum += samples[i];
  }

  return (float)sum / (float)length;
}

static uint32_t StaticTuner_FindNearestString(float frequency_hz)
{
  uint32_t nearest = STATIC_TUNER_STRING_UNKNOWN;
  float best_error = 1000000.0f;

  for (uint32_t i = 0U; i < STATIC_TUNER_STRING_COUNT; i++)
  {
    float target = tunerStrings[i].frequency_hz;
    float relative_error = StaticTuner_AbsF((frequency_hz - target) / target);

    if (relative_error < best_error)
    {
      best_error = relative_error;
      nearest = i;
    }
  }

  return nearest;
}

static float StaticTuner_InterpolateTau(uint32_t tau, uint32_t max_lag)
{
  float better_tau = (float)tau;

  if ((tau > 1U) && (tau < max_lag))
  {
    float previous = tunerYinDiff[tau - 1U];
    float current = tunerYinDiff[tau];
    float next = tunerYinDiff[tau + 1U];
    float denominator = previous - (2.0f * current) + next;

    if (StaticTuner_AbsF(denominator) > 0.000001f)
    {
      better_tau += 0.5f * (previous - next) / denominator;
    }
  }

  return better_tau;
}

static StaticTunerState StaticTuner_GetTuningState(int32_t cents_x10)
{
  if (cents_x10 > STATIC_TUNER_IN_TUNE_LIMIT_X10)
  {
    return STATIC_TUNER_STATE_SHARP;
  }

  if (cents_x10 < -STATIC_TUNER_IN_TUNE_LIMIT_X10)
  {
    return STATIC_TUNER_STATE_FLAT;
  }

  return STATIC_TUNER_STATE_IN_TUNE;
}

static void StaticTuner_FillPitchResult(float detected_hz,
                                        float confidence,
                                        uint32_t tau,
                                        StaticTunerResult *result)
{
  uint32_t nearest_string = StaticTuner_FindNearestString(detected_hz);
  float target_hz = tunerStrings[nearest_string].frequency_hz;
  float relative_error = (detected_hz - target_hz) / target_hz;
  int32_t cents_x10 = StaticTuner_RoundToI32(relative_error * 17312.34f);

  result->detected_hz = detected_hz;
  result->target_hz = target_hz;
  result->confidence = confidence;
  result->detected_string = nearest_string;
  result->cents_error_x10 = cents_x10;
  result->yin_tau = tau;
  result->state = StaticTuner_GetTuningState(cents_x10);
}

static float StaticTuner_GetCorrScore(uint32_t tau, float zero_lag)
{
  uint32_t center_index = STATIC_TUNER_CORR_FRAME_LENGTH - 1U;
  uint32_t overlap = STATIC_TUNER_CORR_FRAME_LENGTH - tau;
  float expected_energy = zero_lag * (float)overlap /
                          (float)STATIC_TUNER_CORR_FRAME_LENGTH;

  if (expected_energy <= 0.000001f)
  {
    return 0.0f;
  }

  return tunerCorrOutput[center_index + tau] / expected_energy;
}

static void StaticTuner_AnalyzeCorrelation(const int16_t *samples,
                                           uint32_t length,
                                           StaticTunerResult *result)
{
  uint32_t min_lag =
      STATIC_TUNER_SAMPLE_RATE_HZ / (uint32_t)STATIC_TUNER_MAX_FREQ_HZ;
  uint32_t max_lag =
      STATIC_TUNER_SAMPLE_RATE_HZ / (uint32_t)STATIC_TUNER_MIN_FREQ_HZ;
  uint32_t corr_length = STATIC_TUNER_CORR_FRAME_LENGTH;
  float mean;
  float zero_lag;
  uint32_t best_tau = 0U;
  float best_score = -1.0f;

  result->corr_detected_hz = 0.0f;
  result->corr_confidence = 0.0f;
  result->corr_tau = 0U;
  result->corr_detected_string = STATIC_TUNER_STRING_UNKNOWN;
  result->corr_state = STATIC_TUNER_STATE_NO_SIGNAL;
  result->corr_cents_error_x10 = 0;

  if (length < corr_length)
  {
    corr_length = length;
  }

  if ((result->signal_amplitude < STATIC_TUNER_SIGNAL_MIN_AMP) ||
      (corr_length != STATIC_TUNER_CORR_FRAME_LENGTH))
  {
    return;
  }

  mean = StaticTuner_GetMean(samples, corr_length);

  for (uint32_t i = 0U; i < STATIC_TUNER_CORR_FRAME_LENGTH; i++)
  {
    tunerCorrInput[i] = (float)samples[i] - mean;
  }

  arm_correlate_f32(tunerCorrInput,
                    STATIC_TUNER_CORR_FRAME_LENGTH,
                    tunerCorrInput,
                    STATIC_TUNER_CORR_FRAME_LENGTH,
                    tunerCorrOutput);

  zero_lag = tunerCorrOutput[STATIC_TUNER_CORR_FRAME_LENGTH - 1U];

  if (zero_lag <= 0.000001f)
  {
    return;
  }

  if (max_lag >= STATIC_TUNER_CORR_FRAME_LENGTH - 1U)
  {
    max_lag = STATIC_TUNER_CORR_FRAME_LENGTH - 2U;
  }

  for (uint32_t tau = min_lag; tau <= max_lag; tau++)
  {
    float previous = StaticTuner_GetCorrScore(tau - 1U, zero_lag);
    float current = StaticTuner_GetCorrScore(tau, zero_lag);
    float next = StaticTuner_GetCorrScore(tau + 1U, zero_lag);

    if ((current >= previous) &&
        (current >= next) &&
        (current > best_score))
    {
      best_score = current;
      best_tau = tau;
    }
  }

  if ((best_tau == 0U) || (best_score < STATIC_TUNER_CORR_MIN_CONFIDENCE))
  {
    result->corr_state = STATIC_TUNER_STATE_ERROR;
    return;
  }

  for (uint32_t tau = min_lag; tau <= max_lag; tau++)
  {
    float previous = StaticTuner_GetCorrScore(tau - 1U, zero_lag);
    float current = StaticTuner_GetCorrScore(tau, zero_lag);
    float next = StaticTuner_GetCorrScore(tau + 1U, zero_lag);
    float strong_peak_threshold =
        best_score * STATIC_TUNER_CORR_FIRST_PEAK_RATIO;

    if (strong_peak_threshold < STATIC_TUNER_CORR_MIN_CONFIDENCE)
    {
      strong_peak_threshold = STATIC_TUNER_CORR_MIN_CONFIDENCE;
    }

    if ((current >= previous) &&
        (current >= next) &&
        (current >= strong_peak_threshold))
    {
      best_score = current;
      best_tau = tau;
      break;
    }
  }

  {
    float previous = StaticTuner_GetCorrScore(best_tau - 1U, zero_lag);
    float current = StaticTuner_GetCorrScore(best_tau, zero_lag);
    float next = StaticTuner_GetCorrScore(best_tau + 1U, zero_lag);
    float denominator = previous - (2.0f * current) + next;
    float better_tau = (float)best_tau;
    float detected_hz;
    uint32_t nearest_string;
    float target_hz;
    float relative_error;
    int32_t cents_x10;

    if (StaticTuner_AbsF(denominator) > 0.000001f)
    {
      better_tau += 0.5f * (previous - next) / denominator;
    }

    detected_hz = (float)STATIC_TUNER_SAMPLE_RATE_HZ / better_tau;
    nearest_string = StaticTuner_FindNearestString(detected_hz);
    target_hz = tunerStrings[nearest_string].frequency_hz;
    relative_error = (detected_hz - target_hz) / target_hz;
    cents_x10 = StaticTuner_RoundToI32(relative_error * 17312.34f);

    result->corr_detected_hz = detected_hz;
    result->corr_confidence = best_score;
    result->corr_tau = best_tau;
    result->corr_detected_string = nearest_string;
    result->corr_cents_error_x10 = cents_x10;
    result->corr_state = StaticTuner_GetTuningState(cents_x10);
  }
}

static void StaticTuner_Analyze(const int16_t *samples,
                                uint32_t length,
                                StaticTunerResult *result)
{
  uint32_t min_lag = STATIC_TUNER_SAMPLE_RATE_HZ / (uint32_t)STATIC_TUNER_MAX_FREQ_HZ;
  uint32_t max_lag = STATIC_TUNER_SAMPLE_RATE_HZ / (uint32_t)STATIC_TUNER_MIN_FREQ_HZ;
  uint32_t analysis_length = length - max_lag;
  float mean = StaticTuner_GetMean(samples, length);
  float running_sum = 0.0f;
  uint32_t best_tau = 0U;
  float best_cmnd = 1000000.0f;

  result->detected_hz = 0.0f;
  result->target_hz = 0.0f;
  result->confidence = 0.0f;
  result->detected_string = STATIC_TUNER_STRING_UNKNOWN;
  result->state = STATIC_TUNER_STATE_NO_SIGNAL;
  result->cents_error_x10 = 0;
  result->signal_amplitude = StaticTuner_GetSignalAmplitude(samples, length);
  result->yin_tau = 0U;
  result->corr_detected_hz = 0.0f;
  result->corr_confidence = 0.0f;
  result->corr_tau = 0U;
  result->corr_detected_string = STATIC_TUNER_STRING_UNKNOWN;
  result->corr_state = STATIC_TUNER_STATE_NO_SIGNAL;
  result->corr_cents_error_x10 = 0;

  StaticTuner_AnalyzeCorrelation(samples, length, result);

  if (result->signal_amplitude < STATIC_TUNER_SIGNAL_MIN_AMP)
  {
    return;
  }

  tunerYinDiff[0] = 1.0f;

  for (uint32_t tau = 1U; tau <= max_lag; tau++)
  {
    float difference = 0.0f;

    for (uint32_t i = 0U; i < analysis_length; i++)
    {
      float a = (float)samples[i] - mean;
      float b = (float)samples[i + tau] - mean;
      float delta = a - b;

      difference += delta * delta;
    }

    running_sum += difference;

    if (running_sum > 0.0f)
    {
      tunerYinDiff[tau] = difference * (float)tau / running_sum;
    }
    else
    {
      tunerYinDiff[tau] = 1.0f;
    }
  }

  for (uint32_t tau = min_lag; tau <= max_lag; tau++)
  {
    float cmnd = tunerYinDiff[tau];

    if (cmnd < best_cmnd)
    {
      best_cmnd = cmnd;
      best_tau = tau;
    }

    if (cmnd < STATIC_TUNER_YIN_THRESHOLD)
    {
      while ((tau + 1U <= max_lag) && (tunerYinDiff[tau + 1U] < tunerYinDiff[tau]))
      {
        tau++;
      }

      best_tau = tau;
      best_cmnd = tunerYinDiff[tau];
      break;
    }
  }

  if (best_tau == 0U)
  {
    result->state = STATIC_TUNER_STATE_ERROR;
    return;
  }

  {
    float better_tau = StaticTuner_InterpolateTau(best_tau, max_lag);
    float detected_hz = (float)STATIC_TUNER_SAMPLE_RATE_HZ / better_tau;

    StaticTuner_FillPitchResult(detected_hz,
                                1.0f - best_cmnd,
                                best_tau,
                                result);
  }
}

static void StaticTuner_PublishResult(uint32_t test_index,
                                      uint32_t expected_string,
                                      StaticTunerState expected_state,
                                      int32_t cents_offset_x10,
                                      float input_hz,
                                      const StaticTunerResult *result)
{
  tunerDiag.run_count++;
  tunerDiag.selected_test = test_index;
  tunerDiag.expected_string = expected_string;
  tunerDiag.detected_string = result->detected_string;
  tunerDiag.expected_state = expected_state;
  tunerDiag.tuning_state = result->state;
  tunerDiag.input_cents_offset_x10 = cents_offset_x10;
  tunerDiag.cents_error_x10 = result->cents_error_x10;
  tunerDiag.input_hz = input_hz;
  tunerDiag.detected_hz = result->detected_hz;
  tunerDiag.target_hz = result->target_hz;
  tunerDiag.confidence = result->confidence;
  tunerDiag.signal_amplitude = result->signal_amplitude;
  tunerDiag.yin_tau = result->yin_tau;
  tunerDiag.corr_tau = result->corr_tau;
  tunerDiag.corr_detected_string = result->corr_detected_string;
  tunerDiag.corr_state = result->corr_state;
  tunerDiag.corr_cents_error_x10 = result->corr_cents_error_x10;
  tunerDiag.corr_detected_hz = result->corr_detected_hz;
  tunerDiag.corr_confidence = result->corr_confidence;
  tunerGraphCentsErrorX10 = result->cents_error_x10;
  tunerGraphCorrHz = result->corr_detected_hz;
  tunerGraphCorrCentsErrorX10 = result->corr_cents_error_x10;

  if ((result->detected_string == expected_string) &&
      (result->state == expected_state))
  {
    tunerDiag.pass_count++;
  }
  else
  {
    tunerDiag.fail_count++;
  }

  if ((result->corr_detected_string == expected_string) &&
      (result->corr_state == expected_state))
  {
    tunerDiag.corr_pass_count++;
  }
  else
  {
    tunerDiag.corr_fail_count++;
  }
}

void StaticTuner_RunSelectedTest(uint32_t test_index)
{
  StaticTunerResult result;
  uint32_t string_index;
  uint32_t offset_index;
  int32_t cents_offset_x10;
  float input_hz;

  if (StaticTuner_IsValidTest(test_index) == 0U)
  {
    tunerDiag.last_error = 1U;
    return;
  }

  string_index = StaticTuner_GetStringIndex(test_index);
  offset_index = StaticTuner_GetOffsetIndex(test_index);
  cents_offset_x10 = tunerTestOffsetsX10[offset_index];
  input_hz = tunerStrings[string_index].frequency_hz *
             StaticTuner_CentsFactor(cents_offset_x10);

  StaticTuner_GenerateTone(string_index,
                           cents_offset_x10,
                           tunerStaticInput,
                           STATIC_TUNER_FRAME_LENGTH);
  StaticTuner_UpdateFftDiagnostics(tunerStaticInput, STATIC_TUNER_FRAME_LENGTH);
  StaticTuner_Analyze(tunerStaticInput, STATIC_TUNER_FRAME_LENGTH, &result);
  StaticTuner_PublishResult(test_index,
                            string_index,
                            tunerExpectedStates[offset_index],
                            cents_offset_x10,
                            input_hz,
                            &result);
}

void StaticTuner_RunAllTests(void)
{
  tunerDiag.pass_count = 0U;
  tunerDiag.fail_count = 0U;
  tunerDiag.corr_pass_count = 0U;
  tunerDiag.corr_fail_count = 0U;
  tunerDiag.all_tests_run_count++;

  for (uint32_t test = 0U; test < STATIC_TUNER_TEST_COUNT; test++)
  {
    StaticTuner_RunSelectedTest(test);
  }
}

void StaticTuner_Init(void)
{
  tunerDiag.initialized = 1U;
  tunerDiag.last_error = 0U;
  tunerSelectedTest = 16U;
  tunerRunRequest = 0U;
  tunerAutoDemoEnabled = 1U;
  tunerAutoDemoPeriodMs = 1000U;
  tunerAutoDemoLastTickMs = HAL_GetTick();
  tunerAutoDemoTestIndex = tunerSelectedTest;
  StaticTuner_RunAllTests();
  StaticTuner_RunSelectedTest(tunerSelectedTest);
}

void StaticTuner_Task(void)
{
  uint32_t request = tunerRunRequest;
  uint32_t now = HAL_GetTick();

  if ((tunerAutoDemoEnabled != 0U) &&
      ((now - tunerAutoDemoLastTickMs) >= tunerAutoDemoPeriodMs))
  {
    tunerAutoDemoLastTickMs = now;
    tunerAutoDemoTestIndex += STATIC_TUNER_TESTS_PER_STRING;

    if (tunerAutoDemoTestIndex >= STATIC_TUNER_TEST_COUNT)
    {
      tunerAutoDemoTestIndex = 1U;
    }

    tunerSelectedTest = tunerAutoDemoTestIndex;
    StaticTuner_RunSelectedTest(tunerSelectedTest);
    return;
  }

  if (request == 0U)
  {
    return;
  }

  tunerRunRequest = 0U;

  if (request == STATIC_TUNER_RUN_ALL_TESTS)
  {
    StaticTuner_RunAllTests();
    return;
  }

  StaticTuner_RunSelectedTest(tunerSelectedTest);
}
