#include <Arduino.h>
#include <crumbs.h>
#include <crumbs_message_helpers.h>

#include <bread/dcmt_ops.h>

#include "globals.h"

static bool is_valid_mode(uint8_t mode)
{
    if (mode == OPEN_LOOP || mode == CLOSED_LOOP_POSITION)
        return true;
#if DCMT_ENABLE_SPEED_LOOP
    if (mode == CLOSED_LOOP_SPEED)
        return true;
#endif
    return false;
}

void handler_set_open_loop(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    int16_t m1 = 0;
    int16_t m2 = 0;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (crumbs_msg_read_i16(data, data_len, 0, &m1) != 0)
        return;
    if (crumbs_msg_read_i16(data, data_len, 2, &m2) != 0)
        return;

    if (slice.mode != OPEN_LOOP)
        return;

    slice.motor1PWM = constrain(m1, -255, 255);
    slice.motor2PWM = constrain(m2, -255, 255);
}

void handler_set_brake(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    uint8_t b1 = 0;
    uint8_t b2 = 0;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (crumbs_msg_read_u8(data, data_len, 0, &b1) != 0)
        return;
    if (crumbs_msg_read_u8(data, data_len, 1, &b2) != 0)
        return;

    slice.motor1Brake = (b1 != 0);
    slice.motor2Brake = (b2 != 0);
}

void handler_set_mode(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    uint8_t mode = OPEN_LOOP;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (crumbs_msg_read_u8(data, data_len, 0, &mode) != 0)
        return;

    if (!is_valid_mode(mode))
        return;
    slice.mode = static_cast<ControlModes>(mode);
    if (slice.mode != OPEN_LOOP)
    {
        slice.motor1PWM = 0;
        slice.motor2PWM = 0;
    }
}

void handler_set_setpoint(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    int16_t t1 = 0;
    int16_t t2 = 0;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (crumbs_msg_read_i16(data, data_len, 0, &t1) != 0)
        return;
    if (crumbs_msg_read_i16(data, data_len, 2, &t2) != 0)
        return;

    // Persist setpoints independent of current mode so controllers can preload.
    slice.motor1PositionSetpoint = t1;
    slice.motor2PositionSetpoint = t2;
#if DCMT_ENABLE_SPEED_LOOP
    slice.motor1SpeedSetpoint = t1;
    slice.motor2SpeedSetpoint = t2;
#endif
}

void handler_set_pid(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    uint8_t kp1_x10 = 0;
    uint8_t ki1_x10 = 0;
    uint8_t kd1_x10 = 0;
    uint8_t kp2_x10 = 0;
    uint8_t ki2_x10 = 0;
    uint8_t kd2_x10 = 0;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (crumbs_msg_read_u8(data, data_len, 0, &kp1_x10) != 0)
        return;
    if (crumbs_msg_read_u8(data, data_len, 1, &ki1_x10) != 0)
        return;
    if (crumbs_msg_read_u8(data, data_len, 2, &kd1_x10) != 0)
        return;
    if (crumbs_msg_read_u8(data, data_len, 3, &kp2_x10) != 0)
        return;
    if (crumbs_msg_read_u8(data, data_len, 4, &ki2_x10) != 0)
        return;
    if (crumbs_msg_read_u8(data, data_len, 5, &kd2_x10) != 0)
        return;

    const float kp1 = static_cast<float>(kp1_x10) / 10.0f;
    const float ki1 = static_cast<float>(ki1_x10) / 10.0f;
    const float kd1 = static_cast<float>(kd1_x10) / 10.0f;
    const float kp2 = static_cast<float>(kp2_x10) / 10.0f;
    const float ki2 = static_cast<float>(ki2_x10) / 10.0f;
    const float kd2 = static_cast<float>(kd2_x10) / 10.0f;

    // Persist PID tunings independent of current mode so controllers can preload.
    slice.posPid1 = {kp1, ki1, kd1};
    slice.posPid2 = {kp2, ki2, kd2};
#if DCMT_ENABLE_SPEED_LOOP
    slice.speedPid1 = {kp1, ki1, kd1};
    slice.speedPid2 = {kp2, ki2, kd2};
#endif
}

void handler_set_watchdog(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    uint16_t timeout_ms = 0;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (crumbs_msg_read_u16(data, data_len, 0, &timeout_ms) != 0)
        return;

    wdTimeoutMs = timeout_ms;
    wdLastRxMs = millis();
    wdTripped = false;
}

void reply_version(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data)
{
    (void)ctx;
    (void)user_data;
    // Every reply build proves a live master (SET_REPLY staging frames are
    // not dispatched to on_message, so this is where poll traffic stamps).
    wdLastRxMs = millis();
    crumbs_build_version_reply(reply, DCMT_TYPE_ID, DCMT_MODULE_VER_MAJOR, DCMT_MODULE_VER_MINOR, DCMT_MODULE_VER_PATCH);
}

void reply_get_state(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data)
{
    uint8_t brakes = 0;
    (void)ctx;
    (void)user_data;

    // The controller's periodic state poll is the primary liveness signal.
    wdLastRxMs = millis();

    if (slice.motor1Brake)
        brakes |= 0x01;
    if (slice.motor2Brake)
        brakes |= 0x02;

    // sp1/sp2: no setpoint concept in open-loop — emit BREAD_INVALID_I16.
    // In closed-loop modes, emit the setpoint for the active loop type.
    int16_t sp1 = BREAD_INVALID_I16;
    int16_t sp2 = BREAD_INVALID_I16;
    if (slice.mode == CLOSED_LOOP_POSITION)
    {
        sp1 = slice.motor1PositionSetpoint;
        sp2 = slice.motor2PositionSetpoint;
    }
#if DCMT_ENABLE_SPEED_LOOP
    else if (slice.mode == CLOSED_LOOP_SPEED)
    {
        sp1 = slice.motor1SpeedSetpoint;
        sp2 = slice.motor2SpeedSetpoint;
    }
#endif

    // spd1/spd2: tachometer only runs in CLOSED_LOOP_SPEED — BREAD_INVALID_I16 otherwise.
#if DCMT_ENABLE_SPEED_LOOP
    int16_t spd1 = (slice.mode == CLOSED_LOOP_SPEED) ? slice.motor1Speed : BREAD_INVALID_I16;
    int16_t spd2 = (slice.mode == CLOSED_LOOP_SPEED) ? slice.motor2Speed : BREAD_INVALID_I16;
#else
    int16_t spd1 = BREAD_INVALID_I16;
    int16_t spd2 = BREAD_INVALID_I16;
#endif

    crumbs_msg_init(reply, DCMT_TYPE_ID, DCMT_OP_GET_STATE);
    crumbs_msg_add_u8(reply, static_cast<uint8_t>(slice.mode));
    // Fixed payload layout across all modes:
    // [mode][m1_pwm][m2_pwm][sp1][sp2][pos1][pos2][spd1][spd2][brakes][estop]
    crumbs_msg_add_i16(reply, slice.motor1PWM);
    crumbs_msg_add_i16(reply, slice.motor2PWM);
    crumbs_msg_add_i16(reply, sp1);
    crumbs_msg_add_i16(reply, sp2);
    crumbs_msg_add_i16(reply, slice.motor1Position);
    crumbs_msg_add_i16(reply, slice.motor2Position);
    crumbs_msg_add_i16(reply, spd1);
    crumbs_msg_add_i16(reply, spd2);
    crumbs_msg_add_u8(reply, brakes);
    crumbs_msg_add_u8(reply, slice.eStop ? 1 : 0);
}

void reply_get_caps(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data)
{
    uint8_t level = DCMT_CAP_LEVEL_2;
    uint32_t flags = DCMT_CAP_BASELINE_FLAGS | DCMT_CAP_CLOSED_LOOP_POSITION | DCMT_CAP_PID_TUNING |
                     DCMT_CAP_CMD_WATCHDOG;
    (void)ctx;
    (void)user_data;

    wdLastRxMs = millis();

#if DCMT_ENABLE_SPEED_LOOP
    level = DCMT_CAP_LEVEL_3;
    flags |= DCMT_CAP_CLOSED_LOOP_SPEED;
#endif

    (void)bread_caps_build_reply(reply, DCMT_TYPE_ID, level, flags);
}

void reply_get_watchdog(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data)
{
    uint16_t timeout_ms = wdTimeoutMs;
    (void)ctx;
    (void)user_data;

    (void)bread_watchdog_build_reply(reply, DCMT_TYPE_ID,
                                     timeout_ms != 0 ? 1 : 0, timeout_ms,
                                     wdTripped ? 1 : 0, wdTripCount);
    // A reply build proves a live master too.
    wdLastRxMs = millis();
}
