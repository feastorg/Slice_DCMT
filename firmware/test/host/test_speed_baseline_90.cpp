// #90 baseline: closed-loop speed at 60 RPM with the firmware's speed-PID
// gains, the real DCMotorTacho, DCMotorServo and PID_v1, and the modelled
// motor. This pins today's behaviour so a retune shows up here; when #90 is
// fixed, the bounds below change with it.
//
// DCMotorTacho adds the speed PID's output, in counts, to the position target
// once per DCMT_TACHO_INTERVAL_MS, so for a few seconds the loop is P-only:
// speed = 15.04 * Kp * (target - speed) at 798 counts/rev and 5 ms, which
// settles at 69 % of the target with Kp 0.15. PID_v1 scales Ki by the sample
// time, so Ki 0.01 closes the rest with a time constant of about 20 s.
#include "host.h"
#include "motor_sim.h"
#include <cstdio>

int main()
{
    const double target = 60;
    sim_boot();
    const unsigned long t0 = sim_speed_run((int16_t)target, 60000);

    const unsigned long marks[] = {1, 2, 5, 10, 20, 40, 60};
    for (unsigned long s : marks)
        printf("     %2lu s: %.1f RPM\n", s, sim_rpm(t0 + (s - 1) * 1000, t0 + s * 1000));

    const double at5 = sim_rpm(t0 + 4000, t0 + 5000);
    const double at60 = sim_rpm(t0 + 55000, t0 + 60000);
    printf("     4-5 s: %.1f RPM (%.0f %%), 55-60 s: %.1f RPM\n", at5, 100 * at5 / target, at60);

    host_check("#90 baseline: 4-5 s after the command, speed is 65-80 % of the 60 RPM target",
               at5 >= 0.65 * target && at5 <= 0.80 * target);
    host_check("#90 baseline: over 55-60 s, speed has crept to within 3 RPM of the target",
               at60 >= target - 3 && at60 <= target + 3);
    return host_finish();
}
