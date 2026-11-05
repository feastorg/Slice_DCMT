// A build with no encoder on either port (DCMT_PORTn_INPUT = DCMT_INPUT_NONE,
// the *_noenc envs) is open loop only, and says so: GET_CAPS reports level 1
// with no closed-loop flag, the closed-loop modes, setpoints and gains are
// refused NOT_BUILT over I2C and absent over serial, and open loop, the
// brakes, the watchdog and the e-stop hold work as in every other build.
#include "host.h"
#include "globals.h"
#include <DCMotorServo.h>
#include <Encoder.h>
#include <LMD18200.h>
#include <bread/bread_caps.h>
#include <bread/bread_faults.h>
#include <bread/dcmt_ops.h>
#include <cstdio>
#include <string>

#if DCMT_HAS_POSITION_LOOP
#error "test_noenc is built for the *_noenc envs only"
#endif

void setup();
void loop();
extern LMD18200 motor1Driver;
extern LMD18200 motor2Driver;

static bool applied() { return host_read_ack().status == CRUMBS_ACK_APPLIED; }

static bool refused(uint8_t reason)
{
    const crumbs_ack_t a = host_read_ack();
    return a.status == CRUMBS_ACK_REFUSED && a.reason == reason;
}

static void set_mode(uint8_t mode) { host_send(DCMT_TYPE_ID, DCMT_OP_SET_MODE, &mode, 1); }

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

static void setpoint4(int16_t t1, int16_t t2)
{
    const uint8_t p[4] = {(uint8_t)(t1 & 0xFF), (uint8_t)((uint16_t)t1 >> 8), (uint8_t)(t2 & 0xFF),
                          (uint8_t)((uint16_t)t2 >> 8)};
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_SETPOINT, p, 4);
}

static void setpoint5(uint8_t loop_id, int16_t t1, int16_t t2)
{
    const uint8_t p[5] = {loop_id, (uint8_t)(t1 & 0xFF), (uint8_t)((uint16_t)t1 >> 8), (uint8_t)(t2 & 0xFF),
                          (uint8_t)((uint16_t)t2 >> 8)};
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_SETPOINT, p, 5);
}

static void set_pid(uint8_t loop_id)
{
    crumbs_message_t m;
    dcmt_set_pid_t v = {};
    v.loop = loop_id;
    v.kp1_x1000 = v.kp2_x1000 = 1000;
    crumbs_msg_init(&m, DCMT_TYPE_ID, DCMT_OP_SET_PID);
    (void)dcmt_set_pid_pack(&m, &v);
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_PID, m.data, m.data_len);
}

static bool get_state(dcmt_state_t *s)
{
    crumbs_message_t reply;
    return host_query(DCMT_OP_GET_STATE, &reply) && dcmt_state_unpack(reply.data, reply.data_len, s) == 0;
}

static std::string serial(const char *line)
{
    Serial.out.clear();
    Serial.in = line;
    loop();
    return Serial.out;
}

static bool has(const std::string &s, const char *what) { return s.find(what) != std::string::npos; }

int main()
{
    setup();
    loop();

    host_check("no Encoder is constructed", Encoder::constructed == 0);

    // The encoder callbacks the servos were built with: each reads 0, and a
    // write (homing, or setCurrentPosition) changes nothing.
    host_check("both servos were built", host_servo[0] != nullptr && host_servo[1] != nullptr);
    if (host_servo[0] == nullptr || host_servo[1] == nullptr)
        return host_finish();
    host_servo[0]->writeEncoder(1234);
    host_servo[1]->writeEncoder(-1234);
    host_check("each motor's encoder read is 0, before and after a write",
               host_servo[0]->readEncoder() == 0 && host_servo[1]->readEncoder() == 0);

    // GET_CAPS: level 1, and exactly the open-loop flags.
    const uint32_t want = DCMT_CAP_BASELINE_FLAGS | DCMT_CAP_CMD_WATCHDOG | DCMT_CAP_CLEAR_WATCHDOG_TRIP |
                          DCMT_CAP_ESTOP_HOLD | DCMT_CAP_REFUSES | DCMT_CAP_WATCHDOG_LEASE;
    crumbs_message_t reply;
    bread_caps_result_t caps = {};
    const bool got_caps =
        host_query(BREAD_OP_GET_CAPS, &reply) && bread_caps_parse_payload(reply.data, reply.data_len, &caps) == 0;
    printf("     GET_CAPS: schema %u level %u flags 0x%03lX\n", caps.schema, caps.level, (unsigned long)caps.flags);
    host_check("GET_CAPS answers schema 1", got_caps && caps.schema == BREAD_CAPS_SCHEMA_V1);
    host_check("GET_CAPS reports level 1", caps.level == DCMT_CAP_LEVEL_1);
    host_check("GET_CAPS flags are 0x5E3: open loop, brakes, watchdog, trip clear, e-stop hold, refusals, lease",
               caps.flags == want && want == 0x5E3u);
    host_check("GET_CAPS doesn't advertise faults: nothing here can latch a stall",
               (caps.flags & DCMT_CAP_FAULTS) == 0);

    // The closed-loop modes are refused NOT_BUILT; an undefined one as before.
    set_mode(DCMT_MODE_CLOSED_POSITION);
    host_check("SET_MODE position is refused NOT_BUILT", refused(BREAD_REFUSE_NOT_BUILT));
    set_mode(DCMT_MODE_CLOSED_SPEED);
    host_check("SET_MODE speed is refused NOT_BUILT", refused(BREAD_REFUSE_NOT_BUILT));
    set_mode(0x03);
    host_check("SET_MODE 3 is refused UNDEFINED_VALUE", refused(BREAD_REFUSE_UNDEFINED_VALUE));
    set_mode(DCMT_MODE_OPEN_LOOP);
    host_check("SET_MODE open loop is applied", applied());
    host_check("the mode stays open loop", slice.mode == OPEN_LOOP);

    // Setpoints and gains: no loop takes them.
    setpoint4(500, 600);
    host_check("SET_SETPOINT (4 bytes) is refused NOT_BUILT and stores nothing",
               refused(BREAD_REFUSE_NOT_BUILT) && slice.motor1PositionSetpoint == 0 &&
                   slice.motor2PositionSetpoint == 0);
    setpoint5(DCMT_PID_LOOP_POSITION, 500, 600);
    host_check("SET_SETPOINT position is refused NOT_BUILT and stores nothing",
               refused(BREAD_REFUSE_NOT_BUILT) && slice.motor1PositionSetpoint == 0 &&
                   slice.motor2PositionSetpoint == 0);
    setpoint5(DCMT_PID_LOOP_SPEED, 50, 60);
    host_check("SET_SETPOINT speed is refused NOT_BUILT and stores nothing",
               refused(BREAD_REFUSE_NOT_BUILT) && slice.motor1SpeedSetpoint == 0 && slice.motor2SpeedSetpoint == 0);
    setpoint5(0x03, 1, 2);
    host_check("SET_SETPOINT loop 3 is refused UNDEFINED_VALUE", refused(BREAD_REFUSE_UNDEFINED_VALUE));
    const uint8_t three[3] = {1, 2, 3};
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_SETPOINT, three, 3);
    host_check("a 3-byte SET_SETPOINT is refused MALFORMED, as before", refused(BREAD_REFUSE_MALFORMED));
    // set_pid() sends kp 1.0, which no default gain is.
    const PIDTunings p1 = slice.posPid1, p2 = slice.posPid2, s1 = slice.speedPid1, s2 = slice.speedPid2;
    const bool distinct = p1.kp != 1.0f && p2.kp != 1.0f && s1.kp != 1.0f && s2.kp != 1.0f;
    host_check("the default gains differ from what the test sends", distinct);
    auto gains_kept = [&]() {
        return slice.posPid1.kp == p1.kp && slice.posPid2.kp == p2.kp && slice.speedPid1.kp == s1.kp &&
               slice.speedPid2.kp == s2.kp;
    };
    set_pid(DCMT_PID_LOOP_POSITION);
    host_check("SET_PID position is refused NOT_BUILT and changes no gain",
               refused(BREAD_REFUSE_NOT_BUILT) && gains_kept());
    set_pid(DCMT_PID_LOOP_SPEED);
    host_check("SET_PID speed is refused NOT_BUILT and changes no gain",
               refused(BREAD_REFUSE_NOT_BUILT) && gains_kept());

    // Open loop drives both motors, and the positions read 0.
    brake(0, 0);
    host_check("SET_BRAKE(0,0) is applied", applied());
    open_loop(120, -80);
    host_check("SET_OPEN_LOOP is applied", applied());
    loop();
    host_check("open loop drives both motors", !motor1Driver.braked && motor1Driver.lastWrite == 120 &&
                                                   !motor2Driver.braked && motor2Driver.lastWrite == -80);
    dcmt_state_t s = {};
    host_check("GET_STATE reports open loop, the PWMs and positions 0",
               get_state(&s) && s.mode == DCMT_MODE_OPEN_LOOP && s.m1_pwm == 120 && s.m2_pwm == -80 &&
                   s.pos1 == 0 && s.pos2 == 0);

    // A closed-loop mode that gets past the refusals has no branch to run:
    // the unknown-mode fallback forces open loop and brakes both motors.
    slice.mode = CLOSED_LOOP_POSITION;
    loop();
    host_check("a smuggled position mode falls back to open loop with both brake flags set",
               slice.mode == OPEN_LOOP && slice.motor1Brake && slice.motor2Brake);
    loop();
    host_check("and the next pass brakes both motors", motor1Driver.braked && motor2Driver.braked);

    // Serial: the position and gain commands are absent, as the speed
    // commands are on a build without the speed loop.
    host_check("serial MODE=POS lists only OPEN and keeps open loop",
               has(serial("MODE=POS\n"), "MODE options: OPEN (no encoders") && slice.mode == OPEN_LOOP);
    host_check("serial M1POS= is an invalid command", has(serial("M1POS=100\n"), "Invalid command."));
    host_check("serial PIDPOS= is an invalid command", has(serial("PIDPOS=1,0,0\n"), "Invalid command."));
    host_check("serial M1PWM= still works", has(serial("M1PWM=30\n"), "M1PWM-> 30"));

    // Faults: not advertised, still answered, and nothing latches one.
    bread_faults_result_t f = {0xFF};
    host_check("GET_FAULTS answers 0", host_query(BREAD_OP_GET_FAULTS, &reply) &&
                                           bread_faults_parse_payload(reply.data, reply.data_len, &f) == 0 &&
                                           f.faults == 0);
    host_send(DCMT_TYPE_ID, BREAD_OP_CLEAR_FAULTS, nullptr, 0);
    host_check("CLEAR_FAULTS is applied", applied());

    // No analog port: GET_INPUTS has no reply handler, so no frame comes
    // back, and DCMT_CAP_INPUTS (outside 0x5E3, checked above) is not
    // advertised.
    host_check("GET_INPUTS is not answered", !host_query(DCMT_OP_GET_INPUTS, &reply));

    return host_finish();
}
