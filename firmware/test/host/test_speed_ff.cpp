// The speed loop's gains and feedforward (module major 3, DCMotorServo 2.x):
// GET_VERSION reports major 3; SET_PID's speed gains reach each tacho as
// setPIDTunings(kp, ki); SET_SPEED_FF and serial SPEEDFF= store each motor's
// static, RPM per PWM step and trim, and the loop applies them as
// setFeedforward(static, kv) with kv in counts/s per PWM step at that
// motor's CPR, and setTrimLimit(trim). Until one is sent: no feedforward and
// trim 255, a plain PI. A wrong length is refused MALFORMED and stores
// nothing; a build without the speed loop refuses it NOT_BUILT, has no
// SPEEDFF=, and doesn't advertise DCMT_CAP_SPEED_FEEDFORWARD.
#include "host.h"
#include "globals.h"
#include <DCMotorTacho.h>
#include <bread/bread_caps.h>
#include <bread/dcmt_ops.h>
#include <math.h>
#include <cstdio>
#include <string>

void setup();
void loop();

static bool refused(uint8_t reason)
{
    const crumbs_ack_t a = host_read_ack();
    return a.status == CRUMBS_ACK_REFUSED && a.reason == reason;
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void set_speed_ff(uint8_t s1, uint16_t r1, uint8_t t1, uint8_t s2, uint16_t r2, uint8_t t2, uint8_t len = 8)
{
    uint8_t p[9] = {s1, 0, 0, t1, s2, 0, 0, t2, 0};
    put16(p + 1, r1);
    put16(p + 5, r2);
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_SPEED_FF, p, len);
}

static void serial(const char *line)
{
    Serial.out.clear();
    Serial.in += line;
    Serial.in += "\n";
    loop();
}

static void passes(int n)
{
    for (int i = 0; i < n; i++)
        loop();
}

#if DCMT_ENABLE_SPEED_LOOP
static bool stored(const SpeedFeedforward &f, uint8_t s, uint16_t r, uint8_t t)
{
    return f.pwmStatic == s && f.rpmPerPwmX1000 == r && f.trim == t;
}

// What motor m's tacho was last given: static, kv (counts/s per PWM step)
// and trim.
static bool tacho_ff_is(int m, double s, double kv, double trim)
{
    return fabs(host_tacho_ff[m][0] - s) < 1e-6 && fabs(host_tacho_ff[m][1] - kv) < 1e-6 &&
           fabs(host_tacho_trim[m] - trim) < 1e-6;
}

// kv for an RPM per PWM step x1000 at a CPR.
static double kv_of(uint16_t rpp_x1000, double cpr) { return rpp_x1000 / 1000.0 * cpr / 60.0; }

static bool tunings_are(int m, double kp, double ki)
{
    return fabs(host_tacho_tunings[m][0] - kp) < 1e-4 && fabs(host_tacho_tunings[m][1] - ki) < 1e-4;
}
#endif

int main()
{
    setup();
    loop();

    crumbs_message_t reply;
    const bool got_version = host_query(0x00, &reply) && reply.data_len >= 5;
    printf("     GET_VERSION: %u.%u.%u\n", reply.data[2], reply.data[3], reply.data[4]);
    host_check("GET_VERSION reports module major 3", got_version && reply.data[2] == 3 && DCMT_MODULE_VER_MAJOR == 3);

    bread_caps_result_t caps = {};
    const bool got_caps =
        host_query(BREAD_OP_GET_CAPS, &reply) && bread_caps_parse_payload(reply.data, reply.data_len, &caps) == 0;
    host_check("GET_CAPS answers", got_caps);

    // A wrong length is refused MALFORMED on every build, before NOT_BUILT.
    set_speed_ff(10, 1719, 20, 10, 1719, 20, 7);
    host_check("SET_SPEED_FF of 7 bytes is refused MALFORMED", refused(BREAD_REFUSE_MALFORMED));
    set_speed_ff(10, 1719, 20, 10, 1719, 20, 9);
    host_check("SET_SPEED_FF of 9 bytes is refused MALFORMED", refused(BREAD_REFUSE_MALFORMED));

#if DCMT_ENABLE_SPEED_LOOP
    host_check("GET_CAPS advertises DCMT_CAP_SPEED_FEEDFORWARD (bit 13) with DCMT_CAP_CLOSED_LOOP_SPEED",
               (caps.flags & DCMT_CAP_SPEED_FEEDFORWARD) != 0 && (caps.flags & DCMT_CAP_CLOSED_LOOP_SPEED) != 0 &&
                   DCMT_CAP_SPEED_FEEDFORWARD == (1u << 13));

    // Defaults: the library's PI gains, no feedforward, no trim bound.
    host_check("setup() gives both tachos the default speed gains, Kp 0.6 Ki 3.0",
               tunings_are(0, 0.6, 3.0) && tunings_are(1, 0.6, 3.0));
    host_check("setup() gives both tachos no feedforward and trim 255 (a plain PI)",
               tacho_ff_is(0, 0, 0, 255) && tacho_ff_is(1, 0, 0, 255));
    host_check("the malformed frames stored nothing",
               stored(slice.speedFf1, 0, 0, 255) && stored(slice.speedFf2, 0, 0, 255));

    // SET_PID speed: kp, ki reach the tachos; kd is stored and unused.
    uint8_t pid[13] = {DCMT_PID_LOOP_SPEED};
    put16(pid + 1, 500);
    put16(pid + 3, 2500);
    put16(pid + 5, 20);
    put16(pid + 7, 400);
    put16(pid + 9, 2000);
    put16(pid + 11, 30);
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_PID, pid, 13);
    host_check("SET_PID speed is applied", host_read_ack().status == CRUMBS_ACK_APPLIED);

    // Motor 1 is the motor model's (static 10, 24.3 counts/s per PWM at
    // CPR 848: 1.719 RPM per PWM), trim 20; motor 2 differs in every field.
    set_speed_ff(10, 1719, 20, 12, 2000, 30);
    host_check("SET_SPEED_FF is applied", host_read_ack().status == CRUMBS_ACK_APPLIED);
    host_check("SET_SPEED_FF stores each motor's static, RPM per PWM and trim",
               stored(slice.speedFf1, 10, 1719, 20) && stored(slice.speedFf2, 12, 2000, 30));

    const uint8_t sp[5] = {DCMT_PID_LOOP_SPEED, 60, 0, 60, 0};
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_SETPOINT, sp, 5);
    const uint8_t mode = DCMT_MODE_CLOSED_SPEED;
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_MODE, &mode, 1);
    host_check("speed mode is applied", host_read_ack().status == CRUMBS_ACK_APPLIED);
    passes(5);

    host_check("both tachos run the runtime speed gains (kp, ki)",
               tunings_are(0, 0.5, 2.5) && tunings_are(1, 0.4, 2.0));
    printf("     motor 1: static %.0f kv %.4f trim %.0f; motor 2: static %.0f kv %.4f trim %.0f\n", host_tacho_ff[0][0],
           host_tacho_ff[0][1], host_tacho_trim[0], host_tacho_ff[1][0], host_tacho_ff[1][1], host_tacho_trim[1]);
    host_check("motor 1's tacho gets static 10, kv 1.719 * MOTOR1_CPR / 60 counts/s per PWM, trim 20",
               tacho_ff_is(0, 10, kv_of(1719, MOTOR1_CPR), 20));
    host_check("at CPR 848 that kv is the motor model's 24.3 counts/s per PWM, within 0.01",
               MOTOR1_CPR != 848 || fabs(host_tacho_ff[0][1] - 24.3) < 0.01);
    host_check("motor 2's tacho gets static 12, kv 2.0 * MOTOR2_CPR / 60, trim 30",
               tacho_ff_is(1, 12, kv_of(2000, MOTOR2_CPR), 30));

    const unsigned calls = host_tacho_ff_calls[0];
    passes(20);
    host_check("the feedforward is set once, not on every pass", host_tacho_ff_calls[0] == calls);

    // rpm_per_pwm 0 is no speed term: kv 0. Trim 255 is unbounded.
    set_speed_ff(0, 0, 255, 5, 0, 255);
    host_check("SET_SPEED_FF with no speed term is applied", host_read_ack().status == CRUMBS_ACK_APPLIED);
    passes(5);
    host_check("rpm_per_pwm 0 reaches the tacho as kv 0, trim 255 as 255",
               tacho_ff_is(0, 0, 0, 255) && tacho_ff_is(1, 5, 0, 255));

    // A configuration write, as SET_PID: taken while the e-stop is held.
    host_estop_level = LOW;
    loop();
    set_speed_ff(8, 1500, 25, 8, 1500, 25);
    host_check("SET_SPEED_FF while the e-stop is held is applied, as SET_PID is",
               host_read_ack().status == CRUMBS_ACK_APPLIED && stored(slice.speedFf1, 8, 1500, 25));

    // A malformed frame leaves what was stored.
    set_speed_ff(1, 1, 1, 1, 1, 1, 7);
    host_check("a malformed SET_SPEED_FF after a good one is refused and changes nothing",
               refused(BREAD_REFUSE_MALFORMED) && stored(slice.speedFf1, 8, 1500, 25) &&
                   stored(slice.speedFf2, 8, 1500, 25));

    // Serial SPEEDFF=: the same six fields, rpp in RPM per PWM step.
    serial("SPEEDFF=10,1.719,20,12,2,30");
    host_check("SPEEDFF=10,1.719,20,12,2,30 is applied",
               Serial.out.find("updated") != std::string::npos && stored(slice.speedFf1, 10, 1719, 20) &&
                   stored(slice.speedFf2, 12, 2000, 30));
    const char *bad[] = {"SPEEDFF=10,1.719,20", "SPEEDFF=10,1.719,20,12,2,30,1", "SPEEDFF=256,1,20,0,0,0",
                         "SPEEDFF=10,70,20,0,0,0", "SPEEDFF=10,-1,20,0,0,0", "SPEEDFF=10,1,256,0,0,0",
                         "SPEEDFF=10,nan,20,0,0,0", "SPEEDFF=1.5,1,20,0,0,0", "SPEEDFF=10,1,20,0,0,x"};
    for (const char *line : bad)
    {
        serial(line);
        char what[96];
        snprintf(what, sizeof what, "%s is refused with no change", line);
        host_check(what, Serial.out.find("format:") != std::string::npos && stored(slice.speedFf1, 10, 1719, 20) &&
                             stored(slice.speedFf2, 12, 2000, 30));
    }
#else
    host_check("GET_CAPS doesn't advertise DCMT_CAP_SPEED_FEEDFORWARD without the speed loop",
               (caps.flags & DCMT_CAP_SPEED_FEEDFORWARD) == 0 && (caps.flags & DCMT_CAP_CLOSED_LOOP_SPEED) == 0);
    set_speed_ff(10, 1719, 20, 10, 1719, 20);
    host_check("SET_SPEED_FF is refused NOT_BUILT without the speed loop", refused(BREAD_REFUSE_NOT_BUILT));
    serial("SPEEDFF=10,1.719,20,10,1.719,20");
    host_check("serial SPEEDFF= isn't a command without the speed loop",
               Serial.out.find("Invalid command.") != std::string::npos);
    passes(2);
#endif
    return host_finish();
}
