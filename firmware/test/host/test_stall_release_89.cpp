// #89 characterisation: at 60 RPM, motor 1's shaft is held and then
// released. While it is held, the lead cap (DCMT_TACHO_MAX_LEAD) stops the
// position target 300 counts ahead, and the speed PID's integral winds up.
//
// A hold of DCMT_STALL_TIMEOUT_MS or more latches a stall fault (#64), so the
// motor stays braked when the shaft is let go: the 4 s hold of #89's report
// no longer lunges. A shorter hold doesn't latch, and once released the
// shaft runs faster than it did before the hold. This pins both, so a change
// to the gains, the lead cap or the stall handling shows up here; when the
// wind-up is fixed, the bounds below change with it.
#include "host.h"
#include "motor_sim.h"
#include "globals.h"
#include <LMD18200.h>
#include <bread/dcmt_ops.h>
#include <cstdio>

extern LMD18200 motor1Driver;

int main()
{
    const double target = 60;
    const unsigned long shortHold = DCMT_STALL_TIMEOUT_MS - 100;
    sim_boot();
    // Short hold over [20 s, 20 s + shortHold), then 10 s free.
    const unsigned long t0 = sim_speed_run((int16_t)target, 30000 + shortHold, 20000, 20000 + shortHold);
    const unsigned long tr = t0 + 20000 + shortHold;

    const double before = sim_rpm(t0 + 15000, t0 + 20000);
    const double held = sim_rpm(t0 + 20000, tr);
    const double peak = sim_peak_rpm(tr, tr + 10000, 1000);
    printf("     before the hold (15-20 s): %.1f RPM\n", before);
    printf("     held for %lu ms: %.1f RPM\n", shortHold, held);
    printf("     after release: peak 1 s mean %.1f RPM\n", peak);

    host_check("#89: the shaft doesn't turn while held", held == 0);
    host_check("#89: before the hold, speed is short of the target (48-56 RPM, #90)", before >= 48 && before <= 56);
    host_check("#89: a hold under DCMT_STALL_TIMEOUT_MS latches no fault", motorFaults == 0);
    host_check("#89 overshoot: after a short hold, the peak 1 s speed is at least 5 RPM above the speed before it",
               peak >= before + 5);
    host_check("#89: and stays under 70 RPM (the lead cap bounds the lunge, #63)", peak < 70);

    // The hold of #89's report: 4 s.
    sim_motor[0].held = true;
    sim_run(4000);
    sim_motor[0].held = false;
    const unsigned long tr2 = host_now;
    sim_run(10000);
    const double after = sim_peak_rpm(tr2, tr2 + 10000, 1000);
    printf("     after a 4 s hold: faults 0x%02X, peak 1 s mean %.1f RPM\n", motorFaults, after);
    host_check("#89/#64: a 4 s hold latches M1_STALL", motorFaults == DCMT_FAULT_M1_STALL);
    host_check("#89/#64: and the shaft stays braked and still after release, with no lunge",
               motor1Driver.braked && sim_pos_at(tr2 + 10000) == sim_pos_at(tr2));
    return host_finish();
}
