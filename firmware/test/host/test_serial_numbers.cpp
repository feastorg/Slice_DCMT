// Serial numbers are parsed strictly (#60): the whole token must parse, the
// value must be finite and in range for its field, and anything else is
// refused with no change. Each row of #60 is a case here.
#include "host.h"
#include "globals.h"
#include <cstdio>
#include <string>

void setup();
void loop();

static void serial(const char *line)
{
    Serial.out.clear();
    Serial.in += line;
    Serial.in += "\n";
    loop();
}

static bool refused() { return Serial.out.find("Ignored:") != std::string::npos ||
                               Serial.out.find("format:") != std::string::npos; }

static void refuses(const char *line, bool unchanged)
{
    serial(line);
    char what[96];
    snprintf(what, sizeof what, "%s is refused with no change", line);
    host_check(what, refused() && unchanged);
}

static bool pos_gains_are(float kp, float ki, float kd)
{
    return slice.posPid1.kp == kp && slice.posPid1.ki == ki && slice.posPid1.kd == kd &&
           slice.posPid2.kp == kp && slice.posPid2.ki == ki && slice.posPid2.kd == kd;
}

int main()
{
    setup();
    loop();

    // PID gains: three fields, each finite and in SET_PID's 0 to 65.535.
    serial("PIDPOS=0.5,0.25,0.125");
    host_check("PIDPOS=0.5,0.25,0.125 is applied", !refused() && pos_gains_are(0.5f, 0.25f, 0.125f));
    const char *badPid[] = {
        "PIDPOS=1,2,3garbage", "PIDPOS=-1,0,0", "PIDPOS=nan,0,0", "PIDPOS=inf,0,0",
        "PIDPOS=1e40,0,0",     "PIDPOS=1,,3",   "PIDPOS=1,2",     "PIDPOS=1,2,3,4",
        "PIDPOS=70,0,0",
    };
    for (const char *line : badPid)
    {
        serial(line);
        char what[96];
        snprintf(what, sizeof what, "%s is refused with no change", line);
        host_check(what, refused() && pos_gains_are(0.5f, 0.25f, 0.125f));
    }

    // Positions: an int16, but not -32768, GET_STATE's invalid sentinel.
    serial("MODE=POS");
    serial("M1POS=1234");
    host_check("M1POS=1234 is applied", !refused() && slice.motor1PositionSetpoint == 1234);
    for (const char *line : {"M1POS=40000", "M1POS=32768", "M1POS=-32768", "M1POS=abc", "M1POS=", "M1POS=12x"})
        refuses(line, slice.motor1PositionSetpoint == 1234);
    serial("M2POS=-32767");
    host_check("M2POS=-32767 is applied", !refused() && slice.motor2PositionSetpoint == -32767);

    // Open-loop PWM: refused outside -255 to 255, as SET_OPEN_LOOP refuses it,
    // where it used to clamp.
    serial("MODE=OPEN");
    serial("M1PWM=-255");
    host_check("M1PWM=-255 is applied", !refused() && slice.motor1PWM == -255);
    for (const char *line : {"M1PWM=300", "M1PWM=abc", "M1PWM=", "M1PWM=10.5"})
        refuses(line, slice.motor1PWM == -255);

#if DCMT_ENABLE_SPEED_LOOP
    serial("MODE=SPEED");
    serial("M1SPEED=30");
    host_check("M1SPEED=30 is applied", !refused() && slice.motor1SpeedSetpoint == 30);
    for (const char *line : {"M1SPEED=40000", "M1SPEED=abc"})
        refuses(line, slice.motor1SpeedSetpoint == 30);
    serial("PIDSPEED=1,0,0");
    host_check("PIDSPEED=1,0,0 is applied", !refused() && slice.speedPid1.kp == 1.0f);
    refuses("PIDSPEED=1,0,nan", slice.speedPid1.kp == 1.0f && slice.speedPid1.kd == 0.0f);
#endif

    // The watchdog: a typo no longer disarms it.
    serial("WDOG=500");
    host_check("WDOG=500 arms the watchdog", !refused() && commandWatchdog.timeoutMs() == 500);
    for (const char *line : {"WDOG=abc", "WDOG=", "WDOG=-1", "WDOG=65536"})
        refuses(line, commandWatchdog.timeoutMs() == 500);
    serial("WDOG=0");
    host_check("WDOG=0 still disarms it", !refused() && commandWatchdog.timeoutMs() == 0);

    return host_finish();
}
