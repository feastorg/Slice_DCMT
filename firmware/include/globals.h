// include/globals.h
#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <stdint.h>
#include <bread_slice_core.h>
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
extern DCMT_SLICE slice;
extern Timing timing;

// The e-stop input (pin ESTOP) and the command watchdog
// (BREAD_OP_SET/GET_WATCHDOG), from bread-slice-core. Their state is what
// the I2C handlers read: asserted() and count() (dcmt_state estop and
// estop_count), tripped(), timeoutMs() and tripCount(). The watchdog boots
// disarmed unless DCMT_WATCHDOG_BOOT_MS is defined.
extern bsc::EStop estopInput;
extern bsc::CommandWatchdog commandWatchdog;
// The lease (DCMT_CAP_WATCHDOG_LEASE): set by a 3-byte SET_WATCHDOG with
// BREAD_WATCHDOG_FLAG_COMMANDS_ONLY, cleared by the 2-byte form and serial
// WDOG=. While set, only SET_OPEN_LOOP, SET_BRAKE, SET_MODE, SET_SETPOINT
// and SET_WATCHDOG refresh the watchdog.
extern volatile bool watchdogCommandsOnly;
// Refreshes the watchdog for a reply, a non-actuating frame or a serial line:
// unless the lease is commands-only.
void livenessStamp();
// One bit per motor, set when a hold (watchdog trip or e-stop press) engages
// the brakes and cleared when that motor's brake is released or a new
// setpoint for it arrives. While a bit is set and the brake is engaged, the
// motor's position setpoint follows its encoder, so the brake release holds
// where the shaft is (DCMT_CAP_ESTOP_HOLD).
#define HOLD_RESYNC_M1 0x01
#define HOLD_RESYNC_M2 0x02
extern volatile uint8_t holdResyncMask;

// The PWM each motor is driven at: the last motorN_write() value, clamped to
// +/-255, or 0 after motorN_brake(). Written by the main loop, read by the
// GET_STATE reply. slice.motorNPWM is the open-loop request, not the output.
extern volatile int16_t motor1AppliedPWM;
extern volatile int16_t motor2AppliedPWM;

// ---- Functions implemented across translation units ----
void setupSlice();
void setupDCMT();
void updateStatusLed();
void processEStop(bsc::EStop::Event e);
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
void handler_clear_watchdog_trip(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data);
void reply_version(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data);
void reply_get_state(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data);
void reply_get_caps(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data);
void reply_get_watchdog(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data);

#endif // GLOBALS_H
