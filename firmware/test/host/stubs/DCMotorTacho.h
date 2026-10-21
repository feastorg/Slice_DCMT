// Host stand-in for DCMotorTacho: records the requested speed, and run()
// runs its servo, as the real speed loop does.
#pragma once
#include "DCMotorServo.h"

class DCMotorTacho
{
public:
    DCMotorTacho(DCMotorServo *s, double, unsigned long = 50) : servo(s) {}
    void setSpeedRPM(double rpm) { requested = rpm; }
    void setPIDTunings(double, double, double) {}
    double getMeasuredSpeedRPM() const { return 0; }
    void run() { servo->run(); }
    void stop() { servo->stop(); }

    DCMotorServo *servo;
    double requested = 0;
};
