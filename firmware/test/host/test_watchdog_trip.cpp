// A command-watchdog trip brakes the motors, latches through a re-arm and
// refuses actuating commands until CLEAR_WATCHDOG_TRIP, and the clear resumes
// nothing. The I2C half of the bench's watchdog step.
#include "host.h"
#include "globals.h"
#include <bread/dcmt_ops.h>
#include <crumbs_message_helpers.h>
#include <LMD18200.h>

void setup();
void loop();

extern LMD18200 motor1Driver;
extern LMD18200 motor2Driver;

static void open_loop(int16_t m1, int16_t m2)
{
    crumbs_message_t m;
    crumbs_msg_init(&m, DCMT_TYPE_ID, DCMT_OP_SET_OPEN_LOOP);
    dcmt_set_open_loop_t v = {m1, m2};
    dcmt_set_open_loop_pack(&m, &v);
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_OPEN_LOOP, m.data, m.data_len);
}

static void arm(uint16_t ms)
{
    const uint8_t d[2] = {(uint8_t)(ms & 0xFF), (uint8_t)(ms >> 8)};
    host_send(DCMT_TYPE_ID, BREAD_OP_SET_WATCHDOG, d, 2);
}

// Both queries stamp the watchdog, as any reply build does.
static dcmt_state_t state()
{
    crumbs_message_t reply;
    dcmt_state_t s = {};
    if (!host_query(DCMT_OP_GET_STATE, &reply) || dcmt_state_unpack(reply.data, reply.data_len, &s) != 0)
        host_check("GET_STATE answers", false);
    return s;
}

static bread_watchdog_result_t wdog()
{
    crumbs_message_t reply;
    bread_watchdog_result_t w = {};
    if (!host_query(BREAD_OP_GET_WATCHDOG, &reply) || bread_watchdog_parse_payload(reply.data, reply.data_len, &w) != 0)
        host_check("GET_WATCHDOG answers", false);
    return w;
}

int main()
{
    setup();
    loop();

    open_loop(60, 0);
    loop();
    host_check("open loop drives before the trip", state().m1_pwm == 60);

    arm(100);
    host_now += 99;
    loop(); // the e-stop poll's read adds 1 ms: 100 ms since the arm
    bread_watchdog_result_t w = wdog();
    host_check("silence for the timeout trips the watchdog", w.armed == 1 && w.timeout_ms == 100 && w.tripped == 1 &&
                                                                 w.trip_count == 1);
    dcmt_state_t s = state();
    host_check("a trip brakes both motors", s.brakes == 0x03 && s.m1_pwm == 0 && s.m2_pwm == 0);

    // The hold is motorControlLogic()'s, on every pass while the trip is set,
    // and it outlasts a disarm (#38).
    const unsigned writes1 = motor1Driver.writes, writes2 = motor2Driver.writes;
    for (int i = 0; i < 50; i++)
        loop();
    host_check("the brakes stay on, with no write, while tripped",
               motor1Driver.braked && motor2Driver.braked &&
                   motor1Driver.writes == writes1 && motor2Driver.writes == writes2);
    arm(0);
    (void)host_read_ack();
    for (int i = 0; i < 50; i++)
        loop();
    host_check("disarming leaves the trip held, brakes on, no write",
               wdog().tripped == 1 && motor1Driver.braked && motor2Driver.braked &&
                   motor1Driver.writes == writes1 && motor2Driver.writes == writes2);
    arm(100);
    (void)host_read_ack();

    open_loop(60, 0);
    crumbs_ack_t a = host_read_ack();
    host_check("SET_OPEN_LOOP while tripped is refused WATCHDOG_TRIP",
               a.status == CRUMBS_ACK_REFUSED && a.reason == BREAD_REFUSE_WATCHDOG_TRIP);

    arm(1000);
    host_check("re-arming leaves the trip latched", wdog().tripped == 1);

    host_send(DCMT_TYPE_ID, BREAD_OP_CLEAR_WATCHDOG_TRIP, nullptr, 0);
    a = host_read_ack();
    host_check("CLEAR_WATCHDOG_TRIP is applied", a.status == CRUMBS_ACK_APPLIED);
    w = wdog();
    host_check("the clear lifts the trip and keeps the count", w.tripped == 0 && w.trip_count == 1 && w.timeout_ms == 1000);
    loop();
    s = state();
    host_check("the clear resumes nothing", s.brakes == 0x03 && s.m1_pwm == 0);

    return host_finish();
}
