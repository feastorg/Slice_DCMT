#include "globals.h"
#include "config.h"

void printSliceState(Print &out)
{
    out.print(F("Mode:"));
    if (slice.mode == OPEN_LOOP)
        out.print(F("OPEN"));
    else if (slice.mode == CLOSED_LOOP_POSITION)
        out.print(F("POS"));
#if DCMT_ENABLE_SPEED_LOOP
    else
        out.print(F("SPEED"));
#else
    else
        out.print(F("UNKNOWN"));
#endif

    out.print(F(", M1PWM:"));
    out.print(slice.motor1PWM);
    out.print(F(", M2PWM:"));
    out.print(slice.motor2PWM);
    out.print(F(", M1POS_SP:"));
    out.print(slice.motor1PositionSetpoint);
    out.print(F(", M2POS_SP:"));
    out.print(slice.motor2PositionSetpoint);
    out.print(F(", M1POS:"));
    out.print(slice.motor1Position);
    out.print(F(", M2POS:"));
    out.print(slice.motor2Position);
#if DCMT_ENABLE_SPEED_LOOP
    out.print(F(", M1SPD_SP:"));
    out.print(slice.motor1SpeedSetpoint);
    out.print(F(", M2SPD_SP:"));
    out.print(slice.motor2SpeedSetpoint);
    out.print(F(", M1SPD:"));
    out.print(slice.motor1Speed);
    out.print(F(", M2SPD:"));
    out.print(slice.motor2Speed);
#endif
    out.print(F(", B1:"));
    out.print(slice.motor1Brake);
    out.print(F(", B2:"));
    out.print(slice.motor2Brake);
    out.print(F(", ESTOP:"));
    out.print(slice.eStop);

    out.print(F(", WDOG:"));
    uint16_t wdTimeout;
    bool wdTrip;
    uint8_t wdTrips;
    noInterrupts();
    wdTimeout = wdTimeoutMs;
    wdTrip = wdTripped;
    wdTrips = wdTripCount;
    interrupts();
    if (wdTimeout == 0)
        out.print(F("off"));
    else
        out.print(wdTimeout);
    out.print(F(", WDTRIP:"));
    out.print(wdTrip);
    out.print(F("/"));
    out.println(wdTrips);
}

void printSerialOutput()
{
    if (millis() - timing.lastSerialPrint >= SERIAL_UPDATE_TIME_MS)
    {
#ifdef SLICE_DEBUG
        printSliceState(Serial);
#endif
        timing.lastSerialPrint = millis();
    }
}
