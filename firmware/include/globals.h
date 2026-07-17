// include/globals.h
#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <stdint.h>
#include <FastLED.h>
#include <crumbs.h>
#include "config_hardware.h"

enum ControlModes : uint8_t
{
    OPEN_LOOP = 0,
    CLOSED_LOOP_POSITION = 1,
    CLOSED_LOOP_SPEED = 2
};

struct PIDTunings
{
    float kp = 1.0f;
    float ki = 0.0f;
    float kd = 0.0f;

    PIDTunings() = default;
    PIDTunings(float p, float i, float d) : kp(p), ki(i), kd(d) {}
};

// ---- Shared state structs ----
struct DCMT_SLICE
{
    ControlModes mode = OPEN_LOOP;
    int16_t motor1PWM = 0;
    int16_t motor2PWM = 0;
    bool motor1Brake = false;
    bool motor2Brake = false;
    bool eStop = false;

    int16_t motor1PositionSetpoint = 0;
    int16_t motor2PositionSetpoint = 0;
    int16_t motor1Position = 0;
    int16_t motor2Position = 0;
    int16_t motor1SpeedSetpoint = 0;
    int16_t motor2SpeedSetpoint = 0;
    int16_t motor1Speed = 0;
    int16_t motor2Speed = 0;

    PIDTunings posPid1 = {DCMT_POS_PID_KP, DCMT_POS_PID_KI, DCMT_POS_PID_KD};
    PIDTunings posPid2 = {DCMT_POS_PID_KP, DCMT_POS_PID_KI, DCMT_POS_PID_KD};
    PIDTunings speedPid1 = {DCMT_SPEED_PID_KP, DCMT_SPEED_PID_KI, DCMT_SPEED_PID_KD};
    PIDTunings speedPid2 = {DCMT_SPEED_PID_KP, DCMT_SPEED_PID_KI, DCMT_SPEED_PID_KD};
};

struct Timing
{
    unsigned long lastSerialPrint;
};

// ---- Extern globals (defined in main.cpp) ----
extern volatile bool estopTriggered;
extern CRGB led;
extern DCMT_SLICE slice;
extern Timing timing;

// Command watchdog (BREAD_OP_SET/GET_WATCHDOG): boots disarmed (timeout 0)
// unless DCMT_WATCHDOG_BOOT_MS is defined. ISR handlers write these; main-loop
// access goes through short masked windows (multi-byte volatiles on AVR).
extern volatile uint16_t wdTimeoutMs;
extern volatile unsigned long wdLastRxMs;
extern volatile bool wdTripped;
extern volatile uint8_t wdTripCount;

// ---- Functions implemented across translation units ----
void setupSlice();
void setupDCMT();
void pollEStop();
void estopISR();
void processEStop();
void watchdogLogic();
void motorControlLogic();
void printSliceState(Print &out);
void printSerialOutput();
void serialCommands();
void handler_set_open_loop(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data);
void handler_set_brake(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data);
void handler_set_mode(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data);
void handler_set_setpoint(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data);
void handler_set_pid(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data);
void handler_set_watchdog(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data);
void reply_version(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data);
void reply_get_state(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data);
void reply_get_caps(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data);
void reply_get_watchdog(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data);

#endif // GLOBALS_H
