// Host stand-in for DCMotorTacho: records the requested speed and the lead
// cap, and run() runs its servo, as the real speed loop does.
#pragma once
#include "DCMotorServo.h"

// The cap each motor's setMaxLead() last set, by servo index; 0 is none.
inline long host_tacho_max_lead[2] = {0, 0};

class DCMotorTacho
{
public:
    DCMotorTacho(DCMotorServo *s, double, unsigned long = 50) : servo(s) {}
    void setSpeedRPM(double rpm) { requested = rpm; }
    void setPIDTunings(double, double, double) {}
    void setMaxLead(long counts) { host_tacho_max_lead[servo->index] = counts; }
    double getMeasuredSpeedRPM() const { return 0; }
    void run() { servo->run(); }
    void stop() { servo->stop(); }

    DCMotorServo *servo;
    double requested = 0;
};
