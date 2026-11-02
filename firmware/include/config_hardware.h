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
// harmful: the LED driver bit-bangs WS2812 timing onto motor 1's direction line.
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

// Closed-loop tuning defaults. These, and the other motor-specific values
// below (counts/rev, PWM skip and cap, accuracy, the tacho interval and lead
// cap, the stall thresholds), come from a single bench motor: the PID gains
// and servo values are the archive implementation's, and the speed Ki was
// chosen against a first-order model fitted to that motor (#90, the host
// motor tests). They are a starting point, not a property of the firmware:
// tune them per motor at runtime with SET_PID / PIDPOS= / PIDSPEED=, or per
// build with flags, e.g. -DDCMT_SPEED_PID_KI=<ki>.
#ifndef DCMT_POS_PID_KP
#define DCMT_POS_PID_KP 0.15f
#endif
#ifndef DCMT_POS_PID_KI
#define DCMT_POS_PID_KI 0.01f
#endif
#ifndef DCMT_POS_PID_KD
#define DCMT_POS_PID_KD 0.01f
#endif

#ifndef DCMT_SPEED_PID_KP
#define DCMT_SPEED_PID_KP 0.15f
#endif
// 0.01 left the speed loop P-only for seconds: 60 RPM settled near 69 % of
// the target and took about 20 s to close the rest. 0.2 reaches it within
// 5 % in about 1.3 s in the model (#90).
#ifndef DCMT_SPEED_PID_KI
#define DCMT_SPEED_PID_KI 0.2f
#endif
#ifndef DCMT_SPEED_PID_KD
#define DCMT_SPEED_PID_KD 0.01f
#endif

#ifndef DCMT_SERVO_PWM_SKIP
#define DCMT_SERVO_PWM_SKIP 15
#endif
#ifndef DCMT_SERVO_MAX_PWM
#define DCMT_SERVO_MAX_PWM 150
#endif
#ifndef DCMT_SERVO_ACCURACY
#define DCMT_SERVO_ACCURACY 10
#endif

// Speed-loop update interval in milliseconds.
#ifndef DCMT_TACHO_INTERVAL_MS
#define DCMT_TACHO_INTERVAL_MS 5
#endif

// How far, in encoder counts, the speed loop may drive the position target
// ahead of the encoder. Without a cap a held shaft banks travel and lunges
// on release; 300 removed that on the 2026-10-22 bench (#63).
#ifndef DCMT_TACHO_MAX_LEAD
#define DCMT_TACHO_MAX_LEAD 300
#endif

// Stall faults (#64, DCMT_CAP_FAULTS). In closed loop, a motor driven for
// DCMT_STALL_TIMEOUT_MS without its encoder moving DCMT_STALL_MIN_COUNTS
// counts latches DCMT_FAULT_M1_STALL or _M2_STALL, which brakes both motors
// until CLEAR_FAULTS, serial FAULTCLEAR or a reboot. DCMotorServo's stall
// detection does the check. Starting values, not measured ones: override
// with build flags, e.g. -DDCMT_STALL_TIMEOUT_MS=<ms>.
#ifndef DCMT_STALL_TIMEOUT_MS
#define DCMT_STALL_TIMEOUT_MS 1000
#endif
#ifndef DCMT_STALL_MIN_COUNTS
#define DCMT_STALL_MIN_COUNTS 4
#endif

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
// Requires the D10<->D11 rework. As fabricated G2 routes /MC2 (PWM) to D11 and
// /DIR2 to D10; D11 has no hardware timer on the ATmega4809 (Nano Every), so
// analogWrite() there degrades to a digital write at a threshold of 128 and
// motor 2 loses proportional control on exactly the MCU used for closed loop.
// The rework swaps them so PWM lands on D10, which is a timer pin on both the
// ATmega328P and the ATmega4809. See docs/hardware-revisions.md.
#define MOTOR2_PWM_PIN 10
#define MOTOR2_DIR_PIN 11
#define MOTOR2_BRAKE_PIN 12
#define MOTOR2_THERMAL_PIN 13
#else
#error "Unsupported DCMT_HW_GEN value"
#endif
#define MOTOR2_ENCODER_PIN1 A0
#define MOTOR2_ENCODER_PIN2 A1
#define MOTOR2_CSENSE_PIN A7

// ----- Build-time PWM-pin validation -----
//
// Both G2 defects to date were the same mistake: a PWM signal assigned to a pin
// with no hardware timer. analogWrite() does not fail there -- it degrades to a
// digital write at a threshold of 128 -- so the board runs, the motor moves at
// full output above 128 and not at all below, and nothing reports a problem.
//
// D11 is the trap: it has a timer on the ATmega328P but not on the ATmega4809,
// so a map can be correct on a Nano and silently wrong on a Nano Every. Both
// MCUs are in use on these boards.
//
// Fail the build instead. Pin sets are from the cores' own timer tables
// (framework-arduino-avr/variants/standard, framework-arduino-megaavr/variants/nona4809).
#if defined(__AVR_ATmega4809__)
#define DCMT_PIN_HAS_TIMER(p) ((p) == 3 || (p) == 5 || (p) == 6 || (p) == 9 || (p) == 10)
#elif defined(__AVR_ATmega328P__)
#define DCMT_PIN_HAS_TIMER(p) ((p) == 3 || (p) == 5 || (p) == 6 || (p) == 9 || (p) == 10 || (p) == 11)
#else
#error "Unknown MCU: add its hardware-PWM pin set to DCMT_PIN_HAS_TIMER"
#endif

#if !DCMT_PIN_HAS_TIMER(MOTOR1_PWM_PIN)
#error "MOTOR1_PWM_PIN has no hardware timer on this MCU -- analogWrite would degrade to on/off. See docs/hardware-revisions.md"
#endif
#if !DCMT_PIN_HAS_TIMER(MOTOR2_PWM_PIN)
#error "MOTOR2_PWM_PIN has no hardware timer on this MCU -- analogWrite would degrade to on/off. See docs/hardware-revisions.md"
#endif

// Encoder counts per output-shaft revolution. 848 is the E-S Motor
// 36PG-3429-19-EN measured on the bench on 2026-11-02 (#103): -16 008
// counts left a flag on its output shaft 1/8 turn from where it started,
// 18.875 turns (about +-1.2 counts). The datasheet's 12 PPR x 4 x 1:19 would
// be 912, and the earlier 798 read speeds about 6 % high. Another motor sets
// its own with -DMOTOR1_CPR= / -DMOTOR2_CPR=.
#ifndef MOTOR1_CPR
#define MOTOR1_CPR 848
#endif
#ifndef MOTOR2_CPR
#define MOTOR2_CPR 848
#endif

#endif // HARDWARE_CONFIG_H
