// GET_STATE spd1 reads 0 once motor 1 stops at a speed setpoint of 0, with
// the real DCMotorTacho and the modelled motor. DCMotorServo before 1.2.1
// stopped measuring while the setpoint was 0, so spd1 kept the last running
// speed for as long as it stayed 0 (feastorg/dcmotorservo#18).
#include "host.h"
#include "motor_sim.h"
#include <bread/dcmt_ops.h>
#include <cstdio>

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
    sim_boot();
    sim_speed_run(60, 3000);
    const dcmt_state_t running = state();

    host_check("SET_SETPOINT 0 is applied", sim_speed_setpoint(0, 0));
    sim_run(2000);
    const double shaft = sim_rpm(host_now - 1000, host_now);
    const dcmt_state_t stopped = state();
    printf("     at 60 RPM: spd1 %d; 2 s after setpoint 0: spd1 %d, shaft %.2f RPM\n", running.spd1, stopped.spd1,
           shaft);

    host_check("at 60 RPM, spd1 reads within 5 RPM of it", running.spd1 >= 55 && running.spd1 <= 65);
    host_check("2 s after setpoint 0, the shaft has stopped", shaft > -0.5 && shaft < 0.5);
    host_check("2 s after setpoint 0, spd1 reads 0", stopped.spd1 == 0);
    return host_finish();
}
