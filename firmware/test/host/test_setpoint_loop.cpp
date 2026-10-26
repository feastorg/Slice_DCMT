// SET_SETPOINT names its loop (#57, DCMT_CAP_SETPOINT_LOOP): 4 bytes write
// the position setpoints only, 5 bytes [loop][t1][t2] write the named loop's.
// Before, the 4-byte form wrote both, so a position target in counts became
// the speed request at the next switch to closed-loop speed.
#include "host.h"
#include "globals.h"
#include <DCMotorTacho.h>
#include <bread/bread_caps.h>
#include <bread/dcmt_ops.h>

void setup();
void loop();

static void setpoint4(int16_t t1, int16_t t2)
{
    const uint8_t p[4] = {(uint8_t)(t1 & 0xFF), (uint8_t)((uint16_t)t1 >> 8),
                          (uint8_t)(t2 & 0xFF), (uint8_t)((uint16_t)t2 >> 8)};
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_SETPOINT, p, 4);
}

static void setpoint5(uint8_t loop_id, int16_t t1, int16_t t2)
{
    const uint8_t p[5] = {loop_id, (uint8_t)(t1 & 0xFF), (uint8_t)((uint16_t)t1 >> 8),
                          (uint8_t)(t2 & 0xFF), (uint8_t)((uint16_t)t2 >> 8)};
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_SETPOINT, p, 5);
}

static void set_mode(uint8_t mode) { host_send(DCMT_TYPE_ID, DCMT_OP_SET_MODE, &mode, 1); }

static bool applied() { return host_read_ack().status == CRUMBS_ACK_APPLIED; }

static bool refused(uint8_t reason)
{
    const crumbs_ack_t a = host_read_ack();
    return a.status == CRUMBS_ACK_REFUSED && a.reason == reason;
}

static bool positions_are(int16_t p1, int16_t p2)
{
    return slice.motor1PositionSetpoint == p1 && slice.motor2PositionSetpoint == p2;
}

static bool speeds_are(int16_t s1, int16_t s2)
{
    return slice.motor1SpeedSetpoint == s1 && slice.motor2SpeedSetpoint == s2;
}

int main()
{
    setup();
    loop();

    crumbs_message_t reply;
    bread_caps_result_t caps;
    host_check("GET_CAPS advertises DCMT_CAP_SETPOINT_LOOP",
               host_query(BREAD_OP_GET_CAPS, &reply) && bread_caps_parse_payload(reply.data, reply.data_len, &caps) == 0 &&
                   (caps.flags & DCMT_CAP_SETPOINT_LOOP) != 0);

    setpoint4(5000, 6000);
    host_check("the 4-byte form is applied", applied());
    host_check("and writes the position setpoints only", positions_are(5000, 6000) && speeds_are(0, 0));

    setpoint5(DCMT_PID_LOOP_POSITION, 100, 200);
    host_check("the 5-byte position form writes the position setpoints only",
               applied() && positions_are(100, 200) && speeds_are(0, 0));

    setpoint5(0x03, 7, 8);
    host_check("an undefined loop is refused UNDEFINED_VALUE and changes nothing",
               refused(BREAD_REFUSE_UNDEFINED_VALUE) && positions_are(100, 200) && speeds_are(0, 0));

    const uint8_t three[3] = {1, 2, 3};
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_SETPOINT, three, 3);
    host_check("a 3-byte SET_SETPOINT is refused MALFORMED",
               refused(BREAD_REFUSE_MALFORMED) && positions_are(100, 200));
    const uint8_t six[6] = {DCMT_PID_LOOP_POSITION, 9, 0, 9, 0, 0};
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_SETPOINT, six, 6);
    host_check("a 6-byte SET_SETPOINT is refused MALFORMED",
               refused(BREAD_REFUSE_MALFORMED) && positions_are(100, 200));

#if DCMT_ENABLE_SPEED_LOOP
    // The regression from #57: a position target, then speed mode.
    setpoint4(5000, 6000);
    (void)applied();
    set_mode(DCMT_MODE_CLOSED_SPEED);
    (void)applied();
    loop();
    host_check("speed mode after a position target requests 0, not the target",
               host_tacho_requested[0] == 0 && host_tacho_requested[1] == 0);

    set_mode(DCMT_MODE_OPEN_LOOP);
    (void)applied();
    loop();
    setpoint5(DCMT_PID_LOOP_SPEED, 60, -45);
    host_check("the 5-byte speed form writes the speed setpoints only",
               applied() && speeds_are(60, -45) && positions_are(5000, 6000));
    set_mode(DCMT_MODE_CLOSED_SPEED);
    (void)applied();
    loop();
    host_check("speed mode then requests the speed setpoints",
               host_tacho_requested[0] == 60 && host_tacho_requested[1] == -45);

    // A speed target leaves the post-hold position follow running; a
    // position target ends it.
    holdResyncMask = HOLD_RESYNC_M1 | HOLD_RESYNC_M2;
    setpoint5(DCMT_PID_LOOP_SPEED, 30, 30);
    host_check("a speed target keeps the post-hold follow",
               applied() && holdResyncMask == (HOLD_RESYNC_M1 | HOLD_RESYNC_M2));
    setpoint5(DCMT_PID_LOOP_POSITION, 1, 2);
    host_check("a position target ends it", applied() && holdResyncMask == 0);
#else
    setpoint5(DCMT_PID_LOOP_SPEED, 60, 60);
    host_check("the speed form is refused NOT_BUILT without the speed loop",
               refused(BREAD_REFUSE_NOT_BUILT) && speeds_are(0, 0) && positions_are(100, 200));
    holdResyncMask = HOLD_RESYNC_M1 | HOLD_RESYNC_M2;
    setpoint4(1, 2);
    host_check("a position target ends the post-hold follow", applied() && holdResyncMask == 0);
#endif

    return host_finish();
}
