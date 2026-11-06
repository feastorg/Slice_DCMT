// Host stand-in for DCMotorTacho (DCMotorServo 2.x): records the requested
// speed, the PI gains, the feedforward and the trim bound, and run() runs its
// servo, as the real speed loop drives it.
#pragma once
#include "DCMotorServo.h"

// The speed each motor's setSpeedRPM() last requested, by servo index.
inline double host_tacho_requested[2] = {0, 0};
// The gains each motor's setPIDTunings() last set (kp, ki), by servo index,
// and how many times it was called.
inline double host_tacho_tunings[2][2] = {{-1, -1}, {-1, -1}};
inline unsigned host_tacho_tunings_calls[2] = {0, 0};
// What each motor's setFeedforward() last set (pwmStatic, kv in counts/s per
// PWM step) and setTrimLimit() last set, by servo index; -1 if never.
inline double host_tacho_ff[2][2] = {{-1, -1}, {-1, -1}};
inline double host_tacho_trim[2] = {-1, -1};
inline unsigned host_tacho_ff_calls[2] = {0, 0};

class DCMotorTacho
{
public:
    DCMotorTacho(DCMotorServo *s, double, unsigned long = 50) : servo(s) {}
    void setSpeedRPM(double rpm)
    {
        requested = rpm;
        host_tacho_requested[servo->index] = rpm;
    }
    void setPIDTunings(double kp, double ki)
    {
        host_tacho_tunings[servo->index][0] = kp;
        host_tacho_tunings[servo->index][1] = ki;
        host_tacho_tunings_calls[servo->index]++;
    }
    void setFeedforward(double pwmStatic, double kv)
    {
        host_tacho_ff[servo->index][0] = pwmStatic;
        host_tacho_ff[servo->index][1] = kv;
        host_tacho_ff_calls[servo->index]++;
    }
    void setTrimLimit(double pwm) { host_tacho_trim[servo->index] = pwm; }
    double getMeasuredSpeedRPM() const { return 0; }
    void run() { servo->run(); }
    void stop() { servo->stop(); }

    DCMotorServo *servo;
    double requested = 0;
};
