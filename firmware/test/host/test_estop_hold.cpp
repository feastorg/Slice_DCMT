// An e-stop press holds the motors and GET_STATE reports it; a release
// resumes nothing (DCMT_CAP_ESTOP_HOLD). The I2C half of the bench's e-stop
// step: press during open-loop drive, a refused SET, release, press again.
#include "host.h"
#include "globals.h"
#include <bread/dcmt_ops.h>
#include <crumbs_message_helpers.h>

void setup();
void loop();

static void open_loop(int16_t m1, int16_t m2)
{
    crumbs_message_t m;
    crumbs_msg_init(&m, DCMT_TYPE_ID, DCMT_OP_SET_OPEN_LOOP);
    dcmt_set_open_loop_t v = {m1, m2};
    dcmt_set_open_loop_pack(&m, &v);
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_OPEN_LOOP, m.data, m.data_len);
}

static dcmt_state_t state()
{
    crumbs_message_t reply;
    dcmt_state_t s = {};
    if (!host_query(DCMT_OP_GET_STATE, &reply) || dcmt_state_unpack(reply.data, reply.data_len, &s) != 0)
        host_check("GET_STATE answers", false);
    return s;
}

int main()
{
    setup();
    loop();

    open_loop(60, 0);
    loop();
    dcmt_state_t s = state();
    host_check("open loop drives before the press", s.m1_pwm == 60 && s.estop == 0 && s.estop_count == 0);

    host_estop_level = LOW;
    loop();
    s = state();
    host_check("a press is reported in GET_STATE", s.estop == 1 && s.estop_count == 1);
    host_check("a press brakes both motors", s.brakes == 0x03 && s.m1_pwm == 0 && s.m2_pwm == 0);

    open_loop(60, 0);
    const crumbs_ack_t a = host_read_ack();
    host_check("SET_OPEN_LOOP while held is refused ESTOP",
               a.status == CRUMBS_ACK_REFUSED && a.reason == BREAD_REFUSE_ESTOP);

    loop();
    loop();
    host_check("a held line counts one press", state().estop_count == 1);

    // Release: the first high sample starts the 25 ms debounce.
    host_estop_level = HIGH;
    loop();
    host_check("the release is debounced", state().estop == 1);
    host_now += 30;
    loop();
    s = state();
    host_check("the release is reported", s.estop == 0);
    host_check("the release resumes nothing", s.brakes == 0x03 && s.m1_pwm == 0);

    host_estop_level = LOW;
    loop();
    host_check("a second press counts", state().estop_count == 2);

    return host_finish();
}
