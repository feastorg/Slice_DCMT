// A burst of serial input is answered one line per loop() pass, so the
// e-stop is sampled between lines (#49).
#include "host.h"
#include "globals.h"
#include <LMD18200.h>
#include <cstdio>
#include <string>

void setup();
void loop();
extern LMD18200 motor1Driver;

static int count(const std::string &s, const char *what)
{
    int n = 0;
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + 1))
        n++;
    return n;
}

int main()
{
    setup();
    loop();

    // Five lines arrive at once.
    Serial.out.clear();
    Serial.in = "M1PWM=10\nM1PWM=20\nM1PWM=30\nM1PWM=40\nM1PWM=50\n";
    loop();
    host_check("one pass answers one line of a five-line burst", count(Serial.out, "M1PWM->") == 1);
    host_check("the rest of the burst is still buffered", Serial.available() > 0);

    for (int i = 0; i < 4; i++)
        loop();
    host_check("four more passes answer the other four lines", count(Serial.out, "M1PWM->") == 5);
    loop();
    host_check("the last line of the burst is applied", motor1Driver.lastWrite == 50);

    // The e-stop is sampled between the lines of a burst: a press during the
    // burst is asserted on the next pass, before the next line is handled,
    // and the lines after it are refused.
    Serial.out.clear();
    Serial.in = "M1PWM=60\nM1PWM=70\nM1PWM=80\n";
    loop();
    host_estop_level = LOW;
    loop();
    host_check("a press during a burst is asserted on the next pass", estopInput.asserted());
    host_check("the line after the press is refused, not applied",
               count(Serial.out, "M1PWM->") == 1 && count(Serial.out, "Ignored: e-stop held") == 1);

    return host_finish();
}
