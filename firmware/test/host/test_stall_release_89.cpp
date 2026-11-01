// #89 characterisation: at 60 RPM, motor 1's shaft is held for 4 s and then
// released. While it is held, the lead cap (DCMT_TACHO_MAX_LEAD) stops the
// position target 300 counts ahead, and both PIDs' integrals wind up. Once
// released, the shaft runs faster than it did before the hold, and above the
// target. This pins today's overshoot so a change to the gains, the lead cap
// or the stall handling (#64) shows up here; when #89 is fixed, the bounds
// below change with it.
#include "host.h"
#include "motor_sim.h"
#include <cstdio>

int main()
{
    const double target = 60;
    sim_boot();
    const unsigned long t0 = sim_speed_run((int16_t)target, 34000, 20000, 24000);

    const double before = sim_rpm(t0 + 15000, t0 + 20000);
    const double held = sim_rpm(t0 + 20000, t0 + 24000);
    const double peak = sim_peak_rpm(t0 + 24000, t0 + 34000, 1000);
    const double after = sim_rpm(t0 + 24000, t0 + 31000);
    printf("     before the hold (15-20 s): %.1f RPM\n", before);
    printf("     held (20-24 s): %.1f RPM\n", held);
    printf("     after release: peak 1 s mean %.1f RPM, 24-31 s mean %.1f RPM\n", peak, after);

    host_check("#89: the shaft doesn't turn while held", held == 0);
    host_check("#89: before the hold, speed is short of the target (48-56 RPM, #90)", before >= 48 && before <= 56);
    host_check("#89 overshoot: after release, the peak 1 s speed is at least 5 RPM above the speed before the hold",
               peak >= before + 5);
    host_check("#89 overshoot: and above the 60 RPM target", peak > target);
    host_check("#89: the overshoot stays under 70 RPM (the lead cap bounds the lunge, #63)", peak < 70);
    return host_finish();
}
