// #90 regression: closed-loop speed at 60 RPM with the speed loop configured
// as sim_boot() sends it (Kp 0.6, Ki 3.0, the model motor's feedforward and
// trim 20), the real DCMotorTacho, DCMotorServo and PID_v1, and the modelled
// motor.
//
// Under DCMotorServo 1.x the speed PID moved the position target and settled
// at 69 % of the target for seconds before the integral closed the rest
// (#90). DCMotorServo 2.x drives PWM as feedforward plus a PI trim, so the
// feedforward carries the speed and the trim closes the rest.
//
// The motor is the test's first-order model (motor_sim.h), fitted to one
// bench motor; the firmware carries none of it, and the gains are a
// starting point for that motor, not for every motor.
#include "host.h"
#include "motor_sim.h"
#include <cstdio>

int main()
{
    const double target = 60;
    const unsigned long dur = 20000;
    sim_boot();
    const unsigned long t0 = sim_speed_run((int16_t)target, dur);

    const unsigned long marks[] = {1, 2, 5, 10, 20};
    for (unsigned long s : marks)
        printf("     %2lu s: %.1f RPM\n", s, sim_rpm(t0 + (s - 1) * 1000, t0 + s * 1000));

    // The end of the first 200 ms window whose mean is within 5 % of the
    // target, stepping 10 ms, and the highest 200 ms mean over the first 10 s.
    long first = -1;
    double peak = -1e9;
    for (unsigned long t = t0; t + 200 <= t0 + 10000; t += 10)
    {
        const double r = sim_rpm(t, t + 200);
        if (r > peak)
            peak = r;
        if (first < 0 && r >= 0.95 * target && r <= 1.05 * target)
            first = (long)(t + 200 - t0);
    }
    const double at10 = sim_rpm(t0 + 9000, t0 + 10000);
    const double late = sim_rpm(t0 + 15000, t0 + 20000);
    printf("     first within 5 %%: %ld ms; peak 200 ms mean %.2f RPM; 9-10 s: %.2f RPM; 15-20 s: %.2f RPM\n", first,
           peak, at10, late);

    host_check("#90: speed is within 5 % of the 60 RPM target by 2 s after the command",
               first >= 0 && first <= 2000);
    host_check("#90: over 9-10 s, speed is within 1 RPM of the target", at10 >= target - 1 && at10 <= target + 1);
    host_check("#90: over 15-20 s, still within 1 RPM", late >= target - 1 && late <= target + 1);
    host_check("#90: the peak 200 ms mean overshoots by under 10 %", peak < 1.10 * target);
    return host_finish();
}
