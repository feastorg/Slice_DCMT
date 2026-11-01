// #64: DCMotorServo's stall detection, on in closed loop at
// DCMT_STALL_TIMEOUT_MS / DCMT_STALL_MIN_COUNTS, latches a stall fault on a
// held shaft and not on a shaft that turns or rests. Run against the real
// DCMotorServo, DCMotorTacho and PID_v1 and the modelled motor. Each case
// boots cold, so it runs as its own process.
#include "host.h"
#include "motor_sim.h"
#include "globals.h"
#include <LMD18200.h>
#include <bread/dcmt_ops.h>
#include <cstdio>
#include <cstring>

extern LMD18200 motor1Driver;
extern LMD18200 motor2Driver;

static uint8_t faults()
{
    crumbs_message_t reply;
    bread_faults_result_t f = {0xFF};
    if (!host_query(BREAD_OP_GET_FAULTS, &reply) || bread_faults_parse_payload(reply.data, reply.data_len, &f) != 0)
        host_check("GET_FAULTS answers", false);
    return f.faults;
}

// Runs 1 ms at a time until a fault latches or limitMs passes; returns the
// time of the latch, or 0.
static unsigned long run_until_latch(unsigned long limitMs)
{
    const unsigned long end = host_now + limitMs;
    while ((long)(end - host_now) > 0)
    {
        sim_run(1);
        if (motorFaults != 0)
            return host_now;
    }
    return 0;
}

static bool position_setpoint(int16_t p1, int16_t p2)
{
    const uint8_t p[4] = {(uint8_t)(p1 & 0xFF), (uint8_t)((uint16_t)p1 >> 8), (uint8_t)(p2 & 0xFF),
                          (uint8_t)((uint16_t)p2 >> 8)};
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_SETPOINT, p, 4);
    return host_read_ack().status == CRUMBS_ACK_APPLIED;
}

// Motor 1's applied PWM is 0 at every pass in (from, to].
static bool pwm_zero(unsigned long from, unsigned long to)
{
    for (const SimSample &s : sim_trace)
        if (s.t > from && s.t <= to && s.pwm != 0)
            return false;
    return true;
}

// Speed mode at 60 RPM, shaft held at 5 s: the stall latches about
// DCMT_STALL_TIMEOUT_MS after the shaft stops, brakes both motors, and the
// motor stays braked after the shaft is let go.
static void speed_held()
{
    sim_boot();
    sim_speed_run(60, 5000);
    host_check("no fault while turning at 60 RPM", motorFaults == 0);
    const unsigned long th = host_now;
    sim_motor[0].held = true;
    const unsigned long tl = run_until_latch(5000);
    printf("     latched %lu ms after the hold\n", tl ? tl - th : 0);
    host_check("#64 speed: a held shaft latches DCMT_STALL_TIMEOUT_MS +/- 50 ms after the hold",
               tl != 0 && tl - th >= DCMT_STALL_TIMEOUT_MS - 50 && tl - th <= DCMT_STALL_TIMEOUT_MS + 50);
    host_check("#64 speed: GET_FAULTS reads M1_STALL", faults() == DCMT_FAULT_M1_STALL);
    host_check("#64 speed: both motors are braked", motor1Driver.braked && motor2Driver.braked);

    sim_run(1000);
    sim_motor[0].held = false;
    const unsigned long tr = host_now;
    sim_run(3000);
    printf("     after release: %.1f RPM over 3 s\n", sim_rpm(tr, tr + 3000));
    host_check("#64 speed: after the shaft is let go it stays braked and still, PWM 0 throughout",
               motor1Driver.braked && motor2Driver.braked && sim_pos_at(tr + 3000) == sim_pos_at(tr) &&
                   pwm_zero(tl, tr + 3000));
    host_check("#64 speed: and the fault stays latched", faults() == DCMT_FAULT_M1_STALL);
}

// Position mode: a move to 2000 counts, then 10 s at rest inside the
// deadband, never latches.
static void position_rest()
{
    sim_boot();
    host_check("SET_SETPOINT position is applied", position_setpoint(2000, 0));
    host_check("SET_MODE position is applied", sim_mode(DCMT_MODE_CLOSED_POSITION));
    sim_run(3000);
    const long p = sim_pos_at(host_now);
    printf("     at 3 s: %ld counts (target 2000)\n", p);
    host_check("#64 position: the move reaches the target's deadband", p > 2000 - DCMT_SERVO_ACCURACY && p < 2000 + DCMT_SERVO_ACCURACY);
    const unsigned long t = host_now;
    const unsigned long tl = run_until_latch(10000);
    host_check("#64 position: 10 s at rest inside the deadband latches nothing", tl == 0 && faults() == 0);
    host_check("#64 position: and the motor is not driven at rest", pwm_zero(t, t + 10000));
}

// Position mode, shaft held far from the target: the drive into the hold
// latches.
static void position_held()
{
    sim_boot();
    sim_motor[0].held = true;
    host_check("SET_SETPOINT position is applied", position_setpoint(5000, 0));
    const unsigned long t0 = host_now;
    host_check("SET_MODE position is applied", sim_mode(DCMT_MODE_CLOSED_POSITION));
    const unsigned long tl = run_until_latch(5000);
    printf("     latched %lu ms after the mode\n", tl ? tl - t0 : 0);
    host_check("#64 position: a shaft held 5000 counts from its target latches DCMT_STALL_TIMEOUT_MS +/- 50 ms after the mode",
               tl != 0 && tl - t0 >= DCMT_STALL_TIMEOUT_MS - 50 && tl - t0 <= DCMT_STALL_TIMEOUT_MS + 50);
    host_check("#64 position: GET_FAULTS reads M1_STALL and both motors are braked",
               faults() == DCMT_FAULT_M1_STALL && motor1Driver.braked && motor2Driver.braked);
}

// Speed mode at 10 RPM, the lowest speed in use, for 20 s: no latch.
static void speed_10()
{
    sim_boot();
    const unsigned long t0 = sim_speed_run(10, 20000);
    const double r = sim_rpm(t0 + 15000, t0 + 20000);
    printf("     15-20 s: %.1f RPM\n", r);
    host_check("#64 speed: 20 s at 10 RPM latches nothing", motorFaults == 0 && faults() == 0);
    host_check("#64 speed: and the shaft turns (over 5 RPM at 15-20 s)", r > 5);
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    if (strcmp(c, "speed_held") == 0)
        speed_held();
    else if (strcmp(c, "position_rest") == 0)
        position_rest();
    else if (strcmp(c, "position_held") == 0)
        position_held();
    else if (strcmp(c, "speed_10") == 0)
        speed_10();
    else
    {
        printf("usage: %s speed_held|position_rest|position_held|speed_10\n", argv[0]);
        return 2;
    }
    return host_finish();
}
