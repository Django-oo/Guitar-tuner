#ifndef LEDS_H
#define LEDS_H

#include <stdint.h>

#define LED_RING_COUNT          12U
#define LED_RING_STRING_COUNT   6U

typedef enum
{
  LED_RING_STATUS_STOPPED = 0,
  LED_RING_STATUS_RUNNING = 1,
  LED_RING_STATUS_SUCCESS = 2,
  LED_RING_STATUS_FAILED = 3
} LedRingStatus;

extern volatile uint32_t ledRingStatus;
extern volatile uint32_t ledRingTunedStringMask;
extern volatile uint32_t ledRingDmaBusy;
extern volatile uint32_t ledRingLastError;

void Leds_Task(void);

#endif /* LEDS_H */
