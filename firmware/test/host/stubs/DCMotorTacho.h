// Host stand-in for DCMotorTacho: records the requested speed.
#pragma once
#include "DCMotorServo.h"

class DCMotorTacho
{
public:
    DCMotorTacho(DCMotorServo *, double, unsigned long = 50) {}
    void setSpeedRPM(double rpm) { requested = rpm; }
    void setPIDTunings(double, double, double) {}
    double getMeasuredSpeedRPM() const { return 0; }
    void run() {}
    void stop() {}

    double requested = 0;
};
