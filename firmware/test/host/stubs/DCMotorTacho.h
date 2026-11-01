// Host stand-in for DCMotorTacho: records the requested speed, the lead cap
// and the PID gains, and run() runs its servo, as the real speed loop does.
#pragma once
#include "DCMotorServo.h"

// The cap each motor's setMaxLead() last set, by servo index; 0 is none.
inline long host_tacho_max_lead[2] = {0, 0};
// The speed each motor's setSpeedRPM() last requested, by servo index.
inline double host_tacho_requested[2] = {0, 0};
// The gains each motor's setPIDTunings() last set (kp, ki, kd), by servo
// index, and how many times it was called.
inline double host_tacho_tunings[2][3] = {{-1, -1, -1}, {-1, -1, -1}};
inline unsigned host_tacho_tunings_calls[2] = {0, 0};

class DCMotorTacho
{
public:
    DCMotorTacho(DCMotorServo *s, double, unsigned long = 50) : servo(s) {}
    void setSpeedRPM(double rpm)
    {
        requested = rpm;
        host_tacho_requested[servo->index] = rpm;
    }
    void setPIDTunings(double kp, double ki, double kd)
    {
        host_tacho_tunings[servo->index][0] = kp;
        host_tacho_tunings[servo->index][1] = ki;
        host_tacho_tunings[servo->index][2] = kd;
        host_tacho_tunings_calls[servo->index]++;
    }
    void setMaxLead(long counts) { host_tacho_max_lead[servo->index] = counts; }
    double getMeasuredSpeedRPM() const { return 0; }
    void run() { servo->run(); }
    void stop() { servo->stop(); }

    DCMotorServo *servo;
    double requested = 0;
};
