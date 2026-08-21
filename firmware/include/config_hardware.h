#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include "config.h"

// ----- General BREAD -----
#define ESTOP 2

// Whether this board populates the shared BREAD status LED.
//
// LED_PIN lives in this platform block and is inherited by every slice,
// populated or not -- Slice_RLHT carries the identical line. Gen1 DCMT boards
// do not populate the LED, and gen1 additionally maps MOTOR1_DIR to the same
// MCU pin (5), which is what turns an unpopulated LED from harmless into
// harmful: FastLED bit-bangs NeoPixel timing onto motor 1's direction line.
//
// Slice_RLHT encoded the same fact as RLHT_HAS_STATUS_LED in the gen1/gen2
// split (97c11af, 2026-03-09); DCMT's split landed the same day without it.
//
// LED_PIN is defined only where the LED exists, so a future call site that
// reaches for it on gen1 is a build failure rather than a silent repeat of
// this bug.
#if (DCMT_HW_GEN == 1)
#define DCMT_HAS_STATUS_LED 0
#elif (DCMT_HW_GEN == 2)
#define DCMT_HAS_STATUS_LED 1
#else
#error "Unsupported DCMT_HW_GEN value"
#endif

#if DCMT_HAS_STATUS_LED
#define LED_PIN 5
#endif

// ----- DCMT Specific -----

// Timing constants
#define SERIAL_UPDATE_TIME_MS 1000

// Proven closed-loop tuning defaults from archive implementation.
#define DCMT_POS_PID_KP 0.15f
#define DCMT_POS_PID_KI 0.01f
#define DCMT_POS_PID_KD 0.01f

#define DCMT_SPEED_PID_KP 0.15f
#define DCMT_SPEED_PID_KI 0.01f
#define DCMT_SPEED_PID_KD 0.01f

#define DCMT_SERVO_PWM_SKIP 15
#define DCMT_SERVO_MAX_PWM 150
#define DCMT_SERVO_ACCURACY 10

// Speed-loop update interval in milliseconds.
#define DCMT_TACHO_INTERVAL_MS 5

// ----- Motor1 Definitions -----
#if (DCMT_HW_GEN == 1)
#define MOTOR1_PWM_PIN 6
#define MOTOR1_DIR_PIN 5
#define MOTOR1_BRAKE_PIN 7
#elif (DCMT_HW_GEN == 2)
#define MOTOR1_PWM_PIN 6
#define MOTOR1_DIR_PIN 7
#define MOTOR1_BRAKE_PIN 8
#define MOTOR1_THERMAL_PIN 9
#else
#error "Unsupported DCMT_HW_GEN value"
#endif
#define MOTOR1_ENCODER_PIN1 A2
#define MOTOR1_ENCODER_PIN2 A3
#define MOTOR1_CSENSE_PIN A6

// ----- Motor2 Definitions -----
#if (DCMT_HW_GEN == 1)
#define MOTOR2_PWM_PIN 10
#define MOTOR2_DIR_PIN 9
#define MOTOR2_BRAKE_PIN 11
#elif (DCMT_HW_GEN == 2)
#define MOTOR2_PWM_PIN 11
#define MOTOR2_DIR_PIN 10
#define MOTOR2_BRAKE_PIN 12
#define MOTOR2_THERMAL_PIN 13
#else
#error "Unsupported DCMT_HW_GEN value"
#endif
#define MOTOR2_ENCODER_PIN1 A0
#define MOTOR2_ENCODER_PIN2 A1
#define MOTOR2_CSENSE_PIN A7

// Encoder counts-per-rev (archive-proven closed-loop value).
#define MOTOR1_CPR 798
#define MOTOR2_CPR 798

#endif // HARDWARE_CONFIG_H
