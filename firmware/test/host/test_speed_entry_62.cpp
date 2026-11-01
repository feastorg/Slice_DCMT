// #62: entering closed-loop speed after open-loop travel starts from where
// the shaft is, not from the servo target and speed baseline the open-loop
// travel left behind. DCMotorServo 1.2.0 does that (DCMotorServo#13):
// stop(), which apply_mode_transition() calls, syncs the target to the
// encoder, and setSpeedRPM() from 0 restarts the speed sample there. Without
// either, the first passes drive toward the stale target: at the PWM limit,
// or backwards. Each case boots cold, so it runs as its own process.
#include "host.h"
#include "motor_sim.h"
#include "globals.h"
#include <bread/dcmt_ops.h>
#include <cstdio>
#include <cstring>

// Motor 1's applied PWM over (from, to].
static void pwm_range(unsigned long from, unsigned long to, int &lo, int &hi)
{
    lo = 255;
    hi = -255;
    for (const SimSample &s : sim_trace)
        if (s.t > from && s.t <= to)
        {
            lo = s.pwm < lo ? s.pwm : lo;
            hi = s.pwm > hi ? s.pwm : hi;
        }
}

// Open loop at full reverse PWM for 600 ms, then straight into speed mode at
// +60 RPM while the shaft is still coasting backwards.
static void reverse_then_speed()
{
    sim_boot();
    host_check("open loop at -255 is applied", sim_open_loop(-255, 0));
    sim_run(600);
    const unsigned long te = host_now;
    printf("     entry: %ld counts, %.0f counts/s\n", sim_pos_at(te), sim_motor[0].velocity);
    sim_speed_run(60, 1000);

    int lo, hi;
    pwm_range(te, te + 1000, lo, hi);
    printf("     first 1 s: PWM %d to %d\n", lo, hi);
    host_check("#62 reverse: entering speed at +60 RPM never drives the motor backwards", lo >= 0);
    host_check("#62 reverse: nor at the PWM limit (DCMT_SERVO_MAX_PWM)", hi < DCMT_SERVO_MAX_PWM);
}

// The travel of #62's own run: open loop forward at full PWM for 600 ms,
// stop, then speed mode at +60 RPM with the shaft at rest.
static void forward_stop_then_speed()
{
    sim_boot();
    host_check("open loop at +255 is applied", sim_open_loop(255, 0));
    sim_run(600);
    host_check("open loop at 0 is applied", sim_open_loop(0, 0));
    sim_run(300);
    const unsigned long te = host_now;
    const long pe = sim_pos_at(te);
    printf("     entry: %ld counts, %.0f counts/s\n", pe, sim_motor[0].velocity);
    sim_speed_run(60, 1000);

    int lo, hi;
    pwm_range(te, te + 1000, lo, hi);
    long low = pe;
    for (const SimSample &s : sim_trace)
        if (s.t > te && s.t <= te + 1000 && s.pos < low)
            low = s.pos;
    printf("     first 1 s: PWM %d to %d, lowest position %+ld counts from entry\n", lo, hi, low - pe);
    host_check("#62 forward: entering speed at +60 RPM never drives the motor backwards", lo >= 0);
    host_check("#62 forward: nor at the PWM limit (DCMT_SERVO_MAX_PWM)", hi < DCMT_SERVO_MAX_PWM);
    host_check("#62 forward: the shaft never moves back from where it entered", low >= pe);
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "reverse") == 0)
        reverse_then_speed();
    else if (argc > 1 && strcmp(argv[1], "forward") == 0)
        forward_stop_then_speed();
    else
    {
        printf("usage: %s reverse|forward\n", argv[0]);
        return 2;
    }
    return host_finish();
}
