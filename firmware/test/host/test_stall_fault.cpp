// Stall faults (#64, DCMT_CAP_FAULTS): a stall DCMotorServo reports latches
// DCMT_FAULT_M1_STALL or _M2_STALL, brakes both motors and refuses the
// commands that could make one move with BREAD_REFUSE_FAULT. GET_FAULTS
// reports the bits; CLEAR_FAULTS and serial FAULTCLEAR clear them and resume
// nothing. The servo stub's stall latch stands in for the library's: the
// motor tests (test_stall_fault_64.cpp) cover the detection itself.
#include "host.h"
#include "globals.h"
#include <bread/dcmt_ops.h>
#include <crumbs_message_helpers.h>
#include <DCMotorServo.h>
#include <LMD18200.h>
#include <cstdio>
#include <string>

void setup();
void loop();
extern LMD18200 motor1Driver;
extern LMD18200 motor2Driver;

static crumbs_ack_t ack() { return host_read_ack(); }
static bool applied() { return ack().status == CRUMBS_ACK_APPLIED; }
static bool refused(uint8_t reason)
{
    const crumbs_ack_t a = ack();
    return a.status == CRUMBS_ACK_REFUSED && a.reason == reason;
}

static uint8_t faults()
{
    crumbs_message_t reply;
    bread_faults_result_t f = {0xFF};
    if (!host_query(BREAD_OP_GET_FAULTS, &reply) || reply.data_len != BREAD_FAULTS_FIXED_LEN ||
        bread_faults_parse_payload(reply.data, reply.data_len, &f) != 0)
        host_check("GET_FAULTS answers [faults:u8]", false);
    return f.faults;
}

static dcmt_state_t state()
{
    crumbs_message_t reply;
    dcmt_state_t s = {};
    if (!host_query(DCMT_OP_GET_STATE, &reply) || dcmt_state_unpack(reply.data, reply.data_len, &s) != 0)
        host_check("GET_STATE answers", false);
    return s;
}

static void open_loop(int16_t m1, int16_t m2)
{
    const uint8_t p[4] = {(uint8_t)(m1 & 0xFF), (uint8_t)((uint16_t)m1 >> 8), (uint8_t)(m2 & 0xFF),
                          (uint8_t)((uint16_t)m2 >> 8)};
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_OPEN_LOOP, p, 4);
}
static void brake(uint8_t b1, uint8_t b2)
{
    const uint8_t p[2] = {b1, b2};
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_BRAKE, p, 2);
}
static void mode(uint8_t m) { host_send(DCMT_TYPE_ID, DCMT_OP_SET_MODE, &m, 1); }
static void setpoint4(int16_t a, int16_t b)
{
    const uint8_t p[4] = {(uint8_t)(a & 0xFF), (uint8_t)((uint16_t)a >> 8), (uint8_t)(b & 0xFF),
                          (uint8_t)((uint16_t)b >> 8)};
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_SETPOINT, p, 4);
}
static void setpoint5(uint8_t loop, int16_t a, int16_t b)
{
    const uint8_t p[5] = {loop, (uint8_t)(a & 0xFF), (uint8_t)((uint16_t)a >> 8), (uint8_t)(b & 0xFF),
                          (uint8_t)((uint16_t)b >> 8)};
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_SETPOINT, p, 5);
}
static void clear_faults() { host_send(DCMT_TYPE_ID, BREAD_OP_CLEAR_FAULTS, nullptr, 0); }

static bool braked() { return motor1Driver.braked && motor2Driver.braked; }

int main()
{
    setup();
    loop();

    crumbs_message_t reply;
    bread_caps_result_t caps;
    host_check("GET_CAPS advertises DCMT_CAP_FAULTS",
               host_query(BREAD_OP_GET_CAPS, &reply) && bread_caps_parse_payload(reply.data, reply.data_len, &caps) == 0 &&
                   (caps.flags & DCMT_CAP_FAULTS) != 0);
    host_check("setup() enables stall detection on both servos at DCMT_STALL_TIMEOUT_MS / DCMT_STALL_MIN_COUNTS",
               host_stall_timeout[0] == DCMT_STALL_TIMEOUT_MS && host_stall_timeout[1] == DCMT_STALL_TIMEOUT_MS &&
                   host_stall_min_counts[0] == DCMT_STALL_MIN_COUNTS &&
                   host_stall_min_counts[1] == DCMT_STALL_MIN_COUNTS);
    host_check("no fault at boot", faults() == 0);
    host_check("GET_STATE stays 20 bytes",
               host_query(DCMT_OP_GET_STATE, &reply) && reply.data_len == 20);

    // Closed-loop position, both motors driven.
#if DCMT_ENABLE_SPEED_LOOP
    setpoint5(DCMT_PID_LOOP_SPEED, 30, 30);
    host_check("a speed setpoint is applied", applied());
#endif
    setpoint4(500, 500);
    host_check("a position setpoint is applied", applied());
    mode(DCMT_MODE_CLOSED_POSITION);
    host_check("SET_MODE position is applied", applied());
    host_servo_output[0] = 100;
    host_servo_output[1] = 80;
    loop();
    dcmt_state_t s = state();
    host_check("both motors drive before the stall", s.m1_pwm == 100 && s.m2_pwm == 80 && s.brakes == 0);

    // The library latches a stall on motor 1.
    host_servo_stalled[0] = true;
    loop();
    host_check("a library stall on motor 1 latches DCMT_FAULT_M1_STALL", faults() == DCMT_FAULT_M1_STALL);
    host_check("and clears the library's own latch", !host_servo_stalled[0]);
    s = state();
    host_check("the stall brakes both motors", braked() && s.brakes == 0x03 && s.m1_pwm == 0 && s.m2_pwm == 0);
    host_check("and leaves no motion pending: PWM commands 0, position setpoints on the encoders",
               slice.motor1PWM == 0 && slice.motor2PWM == 0 && slice.motor1PositionSetpoint == 0 &&
                   slice.motor2PositionSetpoint == 0);
#if DCMT_ENABLE_SPEED_LOOP
    host_check("and the speed setpoints 0", slice.motor1SpeedSetpoint == 0 && slice.motor2SpeedSetpoint == 0);
#endif
    host_check("the mode is kept", s.mode == DCMT_MODE_CLOSED_POSITION);

    const unsigned w1 = motor1Driver.writes, w2 = motor2Driver.writes;
    for (int i = 0; i < 50; i++)
        loop();
    host_check("the brakes stay on, with no write, while latched",
               braked() && motor1Driver.writes == w1 && motor2Driver.writes == w2);

    // Refusals.
    open_loop(50, 50);
    host_check("SET_OPEN_LOOP is refused FAULT", refused(BREAD_REFUSE_FAULT));
    setpoint4(900, 900);
    host_check("SET_SETPOINT (4 bytes) is refused FAULT", refused(BREAD_REFUSE_FAULT));
    setpoint5(DCMT_PID_LOOP_POSITION, 900, 900);
    host_check("SET_SETPOINT (5 bytes) is refused FAULT", refused(BREAD_REFUSE_FAULT));
    mode(DCMT_MODE_OPEN_LOOP);
    host_check("SET_MODE is refused FAULT", refused(BREAD_REFUSE_FAULT));
    brake(0, 0);
    host_check("SET_BRAKE(0,0) is refused FAULT", refused(BREAD_REFUSE_FAULT));
    brake(1, 0);
    host_check("SET_BRAKE(1,0) is refused FAULT for the release", refused(BREAD_REFUSE_FAULT));
    brake(1, 1);
    host_check("SET_BRAKE(1,1) is applied", applied());
    host_check("nothing refused was stored",
               slice.mode == CLOSED_LOOP_POSITION && slice.motor1PositionSetpoint == 0 && slice.motor1Brake &&
                   slice.motor2Brake);

    // Precedence: an e-stop wins over a fault.
    host_estop_level = LOW;
    loop();
    open_loop(50, 50);
    host_check("with the e-stop held too, the refusal is ESTOP", refused(BREAD_REFUSE_ESTOP));
    host_estop_level = HIGH;
    for (int i = 0; i < 40; i++)
        loop();
    open_loop(50, 50);
    host_check("after the e-stop release, the fault still refuses", refused(BREAD_REFUSE_FAULT));

    // A second stall, on motor 2, adds its bit.
    host_servo_stalled[1] = true;
    loop();
    host_check("a stall reported on motor 2 adds DCMT_FAULT_M2_STALL",
               faults() == (DCMT_FAULT_M1_STALL | DCMT_FAULT_M2_STALL));

    // CLEAR_FAULTS.
    const uint8_t one = 0;
    host_send(DCMT_TYPE_ID, BREAD_OP_CLEAR_FAULTS, &one, 1);
    host_check("CLEAR_FAULTS with a payload is refused MALFORMED", refused(BREAD_REFUSE_MALFORMED));
    host_check("and the faults stay latched", faults() == (DCMT_FAULT_M1_STALL | DCMT_FAULT_M2_STALL));

    clear_faults();
    host_check("CLEAR_FAULTS is applied", applied());
    host_check("and clears every fault", faults() == 0);
    for (int i = 0; i < 10; i++)
        loop();
    s = state();
    host_check("the clear resumes nothing: both brakes stay engaged, PWM 0",
               braked() && s.brakes == 0x03 && s.m1_pwm == 0 && s.m2_pwm == 0);

    // Resume, step by step, as after a cleared trip.
    brake(0, 0);
    host_check("after the clear, SET_BRAKE(0,0) is applied", applied());
    setpoint4(700, 700);
    host_check("and SET_SETPOINT", applied());
    loop();
    s = state();
    host_check("and the servo runs again", s.m1_pwm == 100 && s.brakes == 0);

    // Serial: refused while latched, FAULTCLEAR clears.
    host_servo_stalled[0] = true;
    loop();
    host_check("a new stall latches again", faults() == DCMT_FAULT_M1_STALL);
    Serial.out.clear();
    Serial.in = "M1POS=100\n";
    loop();
    host_check("serial M1POS= is ignored while latched",
               Serial.out.find("Ignored: motor fault latched") != std::string::npos &&
                   slice.motor1PositionSetpoint != 100);
    Serial.in = "READ\n";
    loop();
    host_check("READ ends with FAULT:0x<bits>", Serial.out.find(", FAULT:0x1\n") != std::string::npos);
    Serial.in = "FAULTCLEAR\n";
    loop();
    host_check("serial FAULTCLEAR clears the fault",
               faults() == 0 && Serial.out.find("Faults cleared") != std::string::npos);
    loop();
    host_check("and resumes nothing", braked() && state().brakes == 0x03);

    return host_finish();
}
