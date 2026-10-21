// Leaving open loop clears the open-loop PWM, whichever path changes the
// mode (#36). Otherwise a later return to open loop drives the motor at the
// old PWM with no new PWM command, and GET_STATE reports a PWM in closed loop.
#include "host.h"
#include "globals.h"
#include <bread/dcmt_ops.h>
#include <LMD18200.h>

void setup();
void loop();
extern LMD18200 motor1Driver;
extern LMD18200 motor2Driver;

static void serial(const char *line)
{
    Serial.in += line;
    Serial.in += "\n";
    loop();
}

static bool state(dcmt_state_t *s)
{
    crumbs_message_t reply;
    return host_query(DCMT_OP_GET_STATE, &reply) && dcmt_state_unpack(reply.data, reply.data_len, s) == 0;
}

static void set_mode(uint8_t mode)
{
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_MODE, &mode, 1);
}

int main()
{
    setup();
    loop();
    dcmt_state_t s;

    // Serial: PWM in open loop, then MODE=POS, then MODE=OPEN.
    serial("M1PWM=200");
    serial("M2PWM=-150");
    loop();
    host_check("serial: open-loop PWM drives motor 1", motor1Driver.lastWrite == 200);
    // GET_STATE reports the output (#61), which the next pass sets.
    serial("MODE=POS");
    loop();
    host_check("serial MODE=POS: the next pass drives neither motor at the old PWM",
               state(&s) && s.m1_pwm == 0 && s.m2_pwm == 0);
    serial("MODE=OPEN");
    loop();
    host_check("serial MODE=OPEN after POS: motor 1 is not driven at the old PWM", motor1Driver.lastWrite == 0);
    host_check("serial MODE=OPEN after POS: motor 2 is not driven at the old PWM", motor2Driver.lastWrite == 0);

    // The same over CRUMBS: SET_MODE leaving open loop has always cleared it.
    serial("M1PWM=200");
    loop();
    set_mode(DCMT_MODE_CLOSED_POSITION);
    host_check("SET_MODE(POS) is applied", host_read_ack().status == CRUMBS_ACK_APPLIED);
    loop();
    host_check("SET_MODE(POS): the next pass drives motor 1 at 0", state(&s) && s.m1_pwm == 0);
    set_mode(DCMT_MODE_OPEN_LOOP);
    loop();
    host_check("SET_MODE(OPEN) after POS: motor 1 is not driven at the old PWM", motor1Driver.lastWrite == 0);

    // Staying in open loop keeps the PWM: only leaving open loop clears it.
    serial("M1PWM=120");
    serial("MODE=OPEN");
    loop();
    host_check("serial MODE=OPEN while open: motor 1 keeps its PWM", motor1Driver.lastWrite == 120);

    return host_finish();
}
