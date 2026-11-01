// motorControlLogic()'s held branch: while an e-stop is held, a watchdog trip
// is held or a motor fault is latched, each alone, both motors are braked on
// every pass and no PWM is written, whatever the brake flags and PWM
// commands say. The hold sets the brake flags and the handlers refuse a
// release, so the mode branches would brake too; the held branch is what
// brakes when those flags are wrong, so each case clears them and stores a
// PWM behind the hold's back.
#include "host.h"
#include "globals.h"
#include <bread/dcmt_ops.h>
#include <DCMotorServo.h>
#include <LMD18200.h>
#include <cstdio>

void setup();
void loop();
extern LMD18200 motor1Driver;
extern LMD18200 motor2Driver;

static bool applied() { return host_read_ack().status == CRUMBS_ACK_APPLIED; }

static void open_loop(int16_t m1, int16_t m2)
{
    const uint8_t p[4] = {(uint8_t)(m1 & 0xFF), (uint8_t)((uint16_t)m1 >> 8), (uint8_t)(m2 & 0xFF),
                          (uint8_t)((uint16_t)m2 >> 8)};
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_OPEN_LOOP, p, 4);
}

static void brake(uint8_t b1, uint8_t b2)
{
    const uint8_t p[2] = {b1, b2};
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_BRAKE, p, 2);
}

static void arm(uint16_t ms)
{
    const uint8_t d[2] = {(uint8_t)(ms & 0xFF), (uint8_t)(ms >> 8)};
    host_send(DCMT_TYPE_ID, BREAD_OP_SET_WATCHDOG, d, 2);
}

// Drives both motors in open loop, then engages the hold, then clears the
// brake flags and stores a PWM, and checks 50 passes.
static void check_hold(const char *name, void (*engage)(), void (*lift)())
{
    brake(0, 0);
    host_check("SET_BRAKE(0,0) is applied", applied());
    open_loop(60, -60);
    host_check("SET_OPEN_LOOP is applied", applied());
    loop();
    host_check("both motors drive before the hold", !motor1Driver.braked && motor1Driver.lastWrite == 60 &&
                                                        !motor2Driver.braked && motor2Driver.lastWrite == -60);

    engage();
    loop();
    const int holds = (estopInput.asserted() ? 1 : 0) + (commandWatchdog.tripped() ? 1 : 0) + (motorFaults != 0 ? 1 : 0);
    char what[160];
    snprintf(what, sizeof what, "%s is the only hold in force", name);
    host_check(what, holds == 1);

    slice.motor1Brake = false;
    slice.motor2Brake = false;
    slice.motor1PWM = 80;
    slice.motor2PWM = -80;

    bool everyPass = true;
    for (int i = 0; i < 50; i++)
    {
        const unsigned w1 = motor1Driver.writes, w2 = motor2Driver.writes;
        const unsigned b1 = motor1Driver.brakes, b2 = motor2Driver.brakes;
        loop();
        if (!motor1Driver.braked || !motor2Driver.braked || motor1Driver.writes != w1 ||
            motor2Driver.writes != w2 || motor1Driver.brakes == b1 || motor2Driver.brakes == b2)
            everyPass = false;
    }
    snprintf(what, sizeof what, "%s alone: both motors are braked on every pass and no PWM is written", name);
    host_check(what, everyPass);

    lift();
    slice.motor1PWM = 0;
    slice.motor2PWM = 0;
    loop();
}

static void estop_press() { host_estop_level = LOW; }
static void estop_release()
{
    host_estop_level = HIGH;
    for (int i = 0; i < 40; i++)
        loop();
}

static void trip()
{
    arm(100);
    host_now += 100;
}
static void trip_clear()
{
    arm(0);
    host_send(DCMT_TYPE_ID, BREAD_OP_CLEAR_WATCHDOG_TRIP, nullptr, 0);
    host_check("CLEAR_WATCHDOG_TRIP is applied", applied());
}

static void fault() { host_servo_stalled[0] = true; }
static void fault_clear()
{
    host_send(DCMT_TYPE_ID, BREAD_OP_CLEAR_FAULTS, nullptr, 0);
    host_check("CLEAR_FAULTS is applied", applied());
}

int main()
{
    setup();
    loop();

    check_hold("an e-stop", estop_press, estop_release);
    check_hold("a watchdog trip", trip, trip_clear);
    check_hold("a stall fault", fault, fault_clear);

    return host_finish();
}
