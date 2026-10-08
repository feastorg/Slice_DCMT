#include <Arduino.h>
#include <crumbs.h>
#include <crumbs_message_helpers.h>

#include <bread/dcmt_ops.h>

#include "globals.h"

// While a watchdog trip is held, SET_OPEN_LOOP, SET_SETPOINT, SET_MODE and a
// brake release are ignored rather than stored: clearing the trip resumes
// nothing, and the operator re-commands afterwards. wdTripped is set in the
// same masked window that zeroes the outputs (watchdogLogic), so these
// ISR-side checks cannot let a command in after the reset.

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
    dcmt_set_open_loop_t v;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (dcmt_set_open_loop_unpack(data, data_len, &v) != 0)
        return;
    if (wdTripped)
        return;

    if (slice.mode != OPEN_LOOP)
        return;

    slice.motor1PWM = constrain(v.m1_pwm, -255, 255);
    slice.motor2PWM = constrain(v.m2_pwm, -255, 255);
}

void handler_set_brake(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    dcmt_set_brake_t v;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (dcmt_set_brake_unpack(data, data_len, &v) != 0)
        return;

    // Engaging a brake is always allowed; releasing one is not while a
    // watchdog trip is held.
    const bool hold = wdTripped;
    if (v.m1_brake != 0)
        slice.motor1Brake = true;
    else if (!hold)
        slice.motor1Brake = false;
    if (v.m2_brake != 0)
        slice.motor2Brake = true;
    else if (!hold)
        slice.motor2Brake = false;
}

void handler_set_mode(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    dcmt_set_mode_t v;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (dcmt_set_mode_unpack(data, data_len, &v) != 0)
        return;
    if (wdTripped)
        return;

    if (!is_valid_mode(v.mode))
        return;
    slice.mode = static_cast<ControlModes>(v.mode);
    if (slice.mode != OPEN_LOOP)
    {
        slice.motor1PWM = 0;
        slice.motor2PWM = 0;
    }
}

void handler_set_setpoint(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    dcmt_set_setpoint_t v;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (dcmt_set_setpoint_unpack(data, data_len, &v) != 0)
        return;
    if (wdTripped)
        return;

    // Persist setpoints independent of current mode so controllers can preload.
    slice.motor1PositionSetpoint = v.target1;
    slice.motor2PositionSetpoint = v.target2;
#if DCMT_ENABLE_SPEED_LOOP
    slice.motor1SpeedSetpoint = v.target1;
    slice.motor2SpeedSetpoint = v.target2;
#endif
}

void handler_set_pid(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    dcmt_set_pid_t v;
    (void)ctx;
    (void)opcode;
    (void)user_data;

    if (dcmt_set_pid_unpack(data, data_len, &v) != 0)
        return;

    const float kp1 = static_cast<float>(v.kp1_x10) / 10.0f;
    const float ki1 = static_cast<float>(v.ki1_x10) / 10.0f;
    const float kd1 = static_cast<float>(v.kd1_x10) / 10.0f;
    const float kp2 = static_cast<float>(v.kp2_x10) / 10.0f;
    const float ki2 = static_cast<float>(v.ki2_x10) / 10.0f;
    const float kd2 = static_cast<float>(v.kd2_x10) / 10.0f;

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

    // Arms, re-arms or disarms (0) and stamps liveness. It does not clear a
    // latched trip: re-arming is safe to send while tripped, and releasing
    // the hold is a separate, explicit act (handler_clear_watchdog_trip).
    wdTimeoutMs = timeout_ms;
    wdLastRxMs = millis();
}

void handler_clear_watchdog_trip(crumbs_context_t *ctx, uint8_t opcode, const uint8_t *data, uint8_t data_len, void *user_data)
{
    (void)ctx;
    (void)opcode;
    (void)data;
    (void)user_data;

    // BREAD_OP_CLEAR_WATCHDOG_TRIP: an operator's acknowledgement that
    // releasing the hold is safe. Clears the trip and nothing else: the
    // timeout, armed state and trip count are left as they are (liveness is
    // stamped by on_crumbs_message, as for any valid frame).
    //
    // The payload is empty by contract. A non-empty one is rejected and the
    // trip stays set, so a future payload form is never taken for a clear.
    if (data_len != BREAD_WATCHDOG_CLEAR_TRIP_PAYLOAD_LEN)
        return;
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

    // Fixed payload layout across all modes, declared once in dcmt_ops.h
    // (dcmt_state): [mode][m1_pwm][m2_pwm][sp1][sp2][pos1][pos2][spd1][spd2]
    // [brakes][estop].
    dcmt_state_t st;
    st.mode = static_cast<uint8_t>(slice.mode);
    st.m1_pwm = slice.motor1PWM;
    st.m2_pwm = slice.motor2PWM;
    st.sp1 = sp1;
    st.sp2 = sp2;
    st.pos1 = slice.motor1Position;
    st.pos2 = slice.motor2Position;
    st.spd1 = spd1;
    st.spd2 = spd2;
    st.brakes = brakes;
    st.estop = slice.eStop ? 1 : 0;

    crumbs_msg_init(reply, DCMT_TYPE_ID, DCMT_OP_GET_STATE);
    (void)dcmt_state_pack(reply, &st);
}

void reply_get_caps(crumbs_context_t *ctx, crumbs_message_t *reply, void *user_data)
{
    uint8_t level = DCMT_CAP_LEVEL_2;
    uint32_t flags = DCMT_CAP_BASELINE_FLAGS | DCMT_CAP_CLOSED_LOOP_POSITION | DCMT_CAP_PID_TUNING |
                     DCMT_CAP_CMD_WATCHDOG | DCMT_CAP_CLEAR_WATCHDOG_TRIP;
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
