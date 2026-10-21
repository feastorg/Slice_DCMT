// What the status LED shows through boot and on the first loop() pass (#56).
// argv[1] is "held" or "released": the e-stop line from power-up. Each case
// is its own process, so each is a cold boot. Gen1 boards have no LED, and
// their firmware must never drive one.
#include "host.h"
#include "globals.h"
#include <cstdio>
#include <cstring>

void setup();
void loop();

static int last_shown()
{
    if (host_led_shows == 0)
        return -1;
    return host_led_shown[(host_led_shows > 64 ? 64 : host_led_shows) - 1];
}

int main(int argc, char **argv)
{
    const bool held = argc > 1 && strcmp(argv[1], "held") == 0;
    const char *line = held ? "held" : "released";
    char what[80];
    host_estop_level = held ? LOW : HIGH;

    setup();
#if DCMT_HAS_STATUS_LED
    snprintf(what, sizeof what, "e-stop %s: the boot wait ends on blue", line);
    host_check(what, last_shown() == CRGB::Blue);
    loop();
    snprintf(what, sizeof what, "e-stop %s: the first loop() pass shows %s", line, held ? "red" : "green");
    host_check(what, last_shown() == (held ? CRGB::Red : CRGB::Green));
#else
    loop();
    snprintf(what, sizeof what, "e-stop %s: no LED is driven on a board without one", line);
    host_check(what, host_led_shows == 0);
#endif
    return host_finish();
}
