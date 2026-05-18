#ifndef STATIC_TUNER_H
#define STATIC_TUNER_H

#include <stdint.h>

#define STATIC_TUNER_SAMPLE_RATE_HZ       16000U
#define STATIC_TUNER_FRAME_LENGTH         4096U
#define STATIC_TUNER_STRING_COUNT         6U
#define STATIC_TUNER_TESTS_PER_STRING     3U
#define STATIC_TUNER_TEST_COUNT           \
  (STATIC_TUNER_STRING_COUNT * STATIC_TUNER_TESTS_PER_STRING)
#define STATIC_TUNER_SPECTRUM_BIN_COUNT   64U
#define STATIC_TUNER_SPECTRUM_MAX_HZ      500U
#define STATIC_TUNER_RUN_ALL_TESTS        0xFFFFFFFFU
#define STATIC_TUNER_IN_TUNE_LIMIT_X10    50
#define STATIC_TUNER_INPUT_SYNTH          0U
#define STATIC_TUNER_INPUT_REAL           1U

typedef enum
{
  STATIC_TUNER_STRING_LOW_E = 0,
  STATIC_TUNER_STRING_A = 1,
  STATIC_TUNER_STRING_D = 2,
  STATIC_TUNER_STRING_G = 3,
  STATIC_TUNER_STRING_B = 4,
  STATIC_TUNER_STRING_HIGH_E = 5,
  STATIC_TUNER_STRING_UNKNOWN = 255
} StaticTunerString;

typedef enum
{
  STATIC_TUNER_STATE_UNDEFINED = 0,
  STATIC_TUNER_STATE_IN_TUNE = 1,
  STATIC_TUNER_STATE_FLAT = 2,
  STATIC_TUNER_STATE_SHARP = 3,
  STATIC_TUNER_STATE_NO_SIGNAL = 4,
  STATIC_TUNER_STATE_ERROR = 5
} StaticTunerState;

typedef struct
{
  volatile uint32_t initialized;
  volatile uint32_t run_count;
  volatile uint32_t all_tests_run_count;
  volatile uint32_t pass_count;
  volatile uint32_t fail_count;
  volatile uint32_t input_source;
  volatile uint32_t selected_test;
  volatile uint32_t expected_string;
  volatile uint32_t detected_string;
  volatile uint32_t expected_state;
  volatile uint32_t tuning_state;
  volatile int32_t input_cents_offset_x10;
  volatile int32_t cents_error_x10;
  volatile float input_hz;
  volatile float detected_hz;
  volatile float target_hz;
  volatile float confidence;
  volatile float fft_low_e_mag;
  volatile float fft_a_mag;
  volatile float fft_d_mag;
  volatile float fft_g_mag;
  volatile float fft_b_mag;
  volatile float fft_high_e_mag;
  volatile float fft_peak_hz;
  volatile float fft_peak_mag;
  volatile uint32_t signal_amplitude;
  volatile uint32_t yin_tau;
  volatile uint32_t corr_tau;
  volatile uint32_t corr_detected_string;
  volatile uint32_t corr_state;
  volatile int32_t corr_cents_error_x10;
  volatile float corr_detected_hz;
  volatile float corr_confidence;
  volatile uint32_t corr_pass_count;
  volatile uint32_t corr_fail_count;
  volatile uint32_t real_sample_count;
  volatile uint32_t real_sample_rate_hz;
  volatile uint32_t real_sample_original_rate_hz;
  volatile uint32_t real_sample_start_output_frame;
  volatile uint32_t real_sample_checksum;
  volatile uint32_t fft_peak_bin;
  volatile uint32_t last_error;
} StaticTunerDiagnostics;

extern int16_t tunerStaticInput[STATIC_TUNER_FRAME_LENGTH];
extern volatile float tunerFftStringMagnitudes[STATIC_TUNER_STRING_COUNT];
extern volatile float tunerSpectrum64[STATIC_TUNER_SPECTRUM_BIN_COUNT];
extern volatile float tunerGraphLowE;
extern volatile float tunerGraphA;
extern volatile float tunerGraphD;
extern volatile float tunerGraphG;
extern volatile float tunerGraphB;
extern volatile float tunerGraphHighE;
extern volatile float tunerGraphPeakHz;
extern volatile float tunerGraphPeakMagnitude;
extern volatile int32_t tunerGraphCentsErrorX10;
extern volatile float tunerGraphCorrHz;
extern volatile int32_t tunerGraphCorrCentsErrorX10;
extern volatile StaticTunerDiagnostics tunerDiag;
extern const char tunerRealAudioSourceFile[];
extern const char tunerRealAudioSourceUrl[];
extern const char tunerRealAudioSourceSha256[];
extern const char tunerRealAudioLicense[];
extern volatile uint32_t tunerRealAudioMetadataKeepAlive;
extern volatile uint32_t tunerInputSource;
extern volatile uint32_t tunerSelectedTest;
extern volatile uint32_t tunerRunRequest;
extern volatile uint32_t tunerAutoDemoEnabled;
extern volatile uint32_t tunerAutoDemoPeriodMs;

void StaticTuner_Init(void);
void StaticTuner_Task(void);
void StaticTuner_RunSelectedTest(uint32_t test_index);
void StaticTuner_RunAllTests(void);

#endif /* STATIC_TUNER_H */
