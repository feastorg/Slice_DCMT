// What the status LED shows through boot and on the first loop() pass (#56).
// argv[1] is "held" or "released": the e-stop line from power-up. Each case
// is its own process, so each is a cold boot. Gen1 boards have no LED, and
// their firmware must never drive one.
#include "host.h"
#include "globals.h"
#include <bsc_status_led.h>
#include <cstdio>
#include <cstring>

void setup();
void loop();

// The last frame the core's StatusLed sent, in GRB order (BSC_HOST_TEST
// records it instead of sending it).
static bool shown(uint8_t g, uint8_t r, uint8_t b)
{
    return bsc_host_led_writes > 0 && bsc_host_led_frame[0] == g && bsc_host_led_frame[1] == r &&
           bsc_host_led_frame[2] == b;
}

static bool blue() { return shown(0, 0, 50); }
static bool green() { return shown(25, 0, 0); }
static bool red() { return shown(0, 50, 0); }

int main(int argc, char **argv)
{
    const bool held = argc > 1 && strcmp(argv[1], "held") == 0;
    const char *line = held ? "held" : "released";
    char what[80];
    host_estop_level = held ? LOW : HIGH;

    setup();
#if DCMT_HAS_STATUS_LED
    snprintf(what, sizeof what, "e-stop %s: the boot wait ends on blue", line);
    host_check(what, blue());
    loop();
    snprintf(what, sizeof what, "e-stop %s: the first loop() pass shows %s", line, held ? "red" : "green");
    host_check(what, held ? red() : green());
#else
    loop();
    snprintf(what, sizeof what, "e-stop %s: no LED is driven on a board without one", line);
    host_check(what, bsc_host_led_writes == 0);
#endif
    return host_finish();
}
