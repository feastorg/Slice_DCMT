// The status LED through a firmware hold: amber while a command-watchdog trip
// or a latched motor fault holds the motors, red over amber while the e-stop
// is held, amber again on the release while the hold stays latched, and green
// once it is cleared. Gen1 boards have no LED, and their firmware must never
// drive one. The servo stub's stall latch stands in for the library's, as in
// test_stall_fault.cpp.
#include "host.h"
#include "globals.h"
#include <bread/dcmt_ops.h>
#include <bsc_status_led.h>
#include <DCMotorServo.h>

void setup();
void loop();

// The last frame the core's StatusLed sent, in GRB order (BSC_HOST_TEST
// records it instead of sending it).
static bool shown(uint8_t g, uint8_t r, uint8_t b)
{
    return bsc_host_led_writes > 0 && bsc_host_led_frame[0] == g && bsc_host_led_frame[1] == r &&
           bsc_host_led_frame[2] == b;
}

static bool green() { return shown(25, 0, 0); }
static bool red() { return shown(0, 50, 0); }
static bool amber() { return shown(16, 50, 0); }

static void arm(uint16_t ms)
{
    const uint8_t d[2] = {(uint8_t)(ms & 0xFF), (uint8_t)(ms >> 8)};
    host_send(DCMT_TYPE_ID, BREAD_OP_SET_WATCHDOG, d, 2);
    (void)host_read_ack();
}

static void press()
{
    host_estop_level = LOW;
    loop();
}

// Past the core's 25 ms release debounce.
static void release()
{
    host_estop_level = HIGH;
    loop();
    host_now += 30;
    loop();
    loop();
}

static void check(const char *what, bool led_ok)
{
#if DCMT_HAS_STATUS_LED
    host_check(what, led_ok);
#else
    (void)what;
    (void)led_ok;
#endif
}

int main()
{
    host_estop_level = HIGH;
    setup();
    loop();
    check("running shows green", green());

    // ---- A command-watchdog trip.
    arm(100);
    host_now += 100;
    loop(); // trips
    loop(); // shows it
    check("a watchdog trip shows amber", commandWatchdog.tripped() && amber());
    press();
    check("an e-stop over a watchdog trip shows red", red());
    release();
    check("the release with the trip latched shows amber", commandWatchdog.tripped() && amber());
    arm(0);
    host_send(DCMT_TYPE_ID, BREAD_OP_CLEAR_WATCHDOG_TRIP, nullptr, 0);
    (void)host_read_ack();
    loop();
    check("clearing the trip shows green", !commandWatchdog.tripped() && green());

    // ---- A latched stall fault.
    host_servo_stalled[0] = true;
    loop(); // latches
    loop(); // shows it
    check("a stall fault shows amber", motorFaults != 0 && amber());
    press();
    check("an e-stop over a stall fault shows red", red());
    release();
    check("the release with the fault latched shows amber", motorFaults != 0 && amber());
    host_send(DCMT_TYPE_ID, BREAD_OP_CLEAR_FAULTS, nullptr, 0);
    (void)host_read_ack();
    loop();
    check("clearing the fault shows green", motorFaults == 0 && green());

#if !DCMT_HAS_STATUS_LED
    host_check("no LED is driven on a board without one", bsc_host_led_writes == 0);
#endif
    return host_finish();
}
