// GET_STATE m1_pwm and m2_pwm report the PWM the motors are driven at, in
// every mode (#61). The contract calls them the current PWM output.
#include "host.h"
#include "globals.h"
#include <bread/dcmt_ops.h>
#include <DCMotorServo.h>
#include <crumbs_message_helpers.h>
#include <cstdio>

void setup();
void loop();

static void send(uint8_t op, const crumbs_message_t &m)
{
    host_send(DCMT_TYPE_ID, op, m.data, m.data_len);
}

static void open_loop(int16_t m1, int16_t m2)
{
    crumbs_message_t m;
    crumbs_msg_init(&m, DCMT_TYPE_ID, DCMT_OP_SET_OPEN_LOOP);
    dcmt_set_open_loop_t v = {m1, m2};
    dcmt_set_open_loop_pack(&m, &v);
    send(DCMT_OP_SET_OPEN_LOOP, m);
}

static void brakes(uint8_t m1, uint8_t m2)
{
    crumbs_message_t m;
    crumbs_msg_init(&m, DCMT_TYPE_ID, DCMT_OP_SET_BRAKE);
    dcmt_set_brake_t v = {m1, m2};
    dcmt_set_brake_pack(&m, &v);
    send(DCMT_OP_SET_BRAKE, m);
}

static void mode(uint8_t md)
{
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_MODE, &md, 1);
}

static bool pwm_is(int16_t m1, int16_t m2, const char *what)
{
    crumbs_message_t reply;
    dcmt_state_t s = {};
    const bool ok = host_query(DCMT_OP_GET_STATE, &reply) &&
                    dcmt_state_unpack(reply.data, reply.data_len, &s) == 0;
    if (!ok || s.m1_pwm != m1 || s.m2_pwm != m2)
        printf("     %s: m1_pwm %d m2_pwm %d, want %d %d\n", what, s.m1_pwm, s.m2_pwm, m1, m2);
    return ok && s.m1_pwm == m1 && s.m2_pwm == m2;
}

int main()
{
    setup();
    loop();

    open_loop(200, -150);
    loop();
    host_check("open loop: GET_STATE reports the driven PWM", pwm_is(200, -150, "open loop"));
    brakes(1, 0);
    loop();
    host_check("open loop, motor 1 braked: m1_pwm reads 0", pwm_is(0, -150, "open loop braked"));
    brakes(0, 0);
    open_loop(0, 0);
    loop();

    mode(DCMT_MODE_CLOSED_POSITION);
    host_servo_output[0] = 120;
    host_servo_output[1] = -90;
    loop();
    host_check("position mode: GET_STATE reports the servo's PWM", pwm_is(120, -90, "position"));
    brakes(0, 1);
    loop();
    host_check("position mode, motor 2 braked: m2_pwm reads 0", pwm_is(120, 0, "position braked"));
    brakes(0, 0);
    loop();

#if DCMT_ENABLE_SPEED_LOOP
    mode(DCMT_MODE_CLOSED_SPEED);
    host_servo_output[0] = -60;
    host_servo_output[1] = 75;
    loop();
    host_check("speed mode: GET_STATE reports the servo's PWM", pwm_is(-60, 75, "speed"));
#endif

    // An e-stop brakes both motors wherever they were.
    host_estop_level = LOW;
    loop();
    host_check("e-stop: both read 0", pwm_is(0, 0, "e-stop"));

    return host_finish();
}
