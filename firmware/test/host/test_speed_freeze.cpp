// The speed loop freezes its integral while the lead cap clamps (#89): in
// closed-loop speed, a motor whose position target sits DCMT_TACHO_MAX_LEAD
// or more counts from its encoder runs the speed PID with Ki 0, and the
// other motor, and the same motor once the lead drops, run the gains in
// force. Those are the runtime gains from SET_PID, not the compile-time
// defaults, clamped or not.
#include "host.h"
#include "globals.h"
#include <DCMotorTacho.h>
#include <bread/dcmt_ops.h>
#include <math.h>
#include <cstdio>

void setup();
void loop();

#if DCMT_ENABLE_SPEED_LOOP
static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

// SET_PID on the speed loop, gains x1000.
static bool set_speed_pid(uint16_t kp1, uint16_t ki1, uint16_t kd1, uint16_t kp2, uint16_t ki2, uint16_t kd2)
{
    uint8_t p[13] = {DCMT_PID_LOOP_SPEED};
    put16(p + 1, kp1);
    put16(p + 3, ki1);
    put16(p + 5, kd1);
    put16(p + 7, kp2);
    put16(p + 9, ki2);
    put16(p + 11, kd2);
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_PID, p, 13);
    return host_read_ack().status == CRUMBS_ACK_APPLIED;
}

static bool tunings_are(int m, double kp, double ki, double kd)
{
    const double *t = host_tacho_tunings[m];
    return fabs(t[0] - kp) < 1e-4 && fabs(t[1] - ki) < 1e-4 && fabs(t[2] - kd) < 1e-4;
}

static void passes(int n)
{
    for (int i = 0; i < n; i++)
        loop();
}
#endif

int main()
{
    setup();
    loop();

#if DCMT_ENABLE_SPEED_LOOP
    host_check("SET_PID speed is applied", set_speed_pid(500, 300, 20, 400, 250, 30));
    const uint8_t sp[5] = {DCMT_PID_LOOP_SPEED, 60, 0, 60, 0};
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_SETPOINT, sp, 5);
    const uint8_t mode = DCMT_MODE_CLOSED_SPEED;
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_MODE, &mode, 1);
    host_check("speed mode is applied", host_read_ack().status == CRUMBS_ACK_APPLIED);
    passes(5);

    host_check("unclamped, both motors run the runtime gains",
               tunings_are(0, 0.5, 0.3, 0.02) && tunings_are(1, 0.4, 0.25, 0.03));

    // Encoder far behind the target: lead >= DCMT_TACHO_MAX_LEAD.
    host_servo_actual[0] = -100000;
    passes(5);
    printf("     motor 1 clamped: kp %.3f ki %.3f kd %.3f\n", host_tacho_tunings[0][0], host_tacho_tunings[0][1],
           host_tacho_tunings[0][2]);
    host_check("#89: a positive lead at the cap runs motor 1 with Ki 0 and the runtime Kp, Kd",
               tunings_are(0, 0.5, 0, 0.02));
    host_check("#89: and motor 2, unclamped, keeps its runtime gains", tunings_are(1, 0.4, 0.25, 0.03));

    host_servo_actual[0] = 100000;
    passes(5);
    host_check("#89: a negative lead at the cap freezes it too", tunings_are(0, 0.5, 0, 0.02));

    host_check("SET_PID speed while clamped is applied", set_speed_pid(700, 400, 50, 400, 250, 30));
    passes(5);
    host_check("#89: a SET_PID while clamped takes effect, still with Ki 0", tunings_are(0, 0.7, 0, 0.05));

    const unsigned calls = host_tacho_tunings_calls[0];
    passes(20);
    host_check("the frozen gains are set once, not on every pass", host_tacho_tunings_calls[0] == calls);

    host_servo_actual[0] = 0;
    passes(5);
    host_check("#89: once the lead drops, motor 1 runs the runtime gains with their Ki",
               tunings_are(0, 0.7, 0.4, 0.05));
#else
    printf("     skip: the speed loop isn't built in this env\n");
#endif
    return host_finish();
}
