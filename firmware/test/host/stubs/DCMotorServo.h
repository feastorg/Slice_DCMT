// Host stand-in for DCMotorServo. It doesn't control anything: run() passes
// host_servo_output[n] to the motor-write callback, and stop() calls the
// brake callback, as the real library does through the firmware's wrappers.
// Instances are numbered in construction order, so servo1 is 0 and servo2
// is 1 (both are defined in main.cpp).
#pragma once
#include <stdint.h>

typedef void (*MotorWriteFunc)(int16_t speed);
typedef void (*MotorBrakeFunc)();
typedef long (*EncoderReadFunc)();
typedef void (*EncoderWriteFunc)(long newPosition);

// The PWM each servo's run() writes; a test sets it.
inline int16_t host_servo_output[2] = {0, 0};

class DCMotorServo
{
public:
    DCMotorServo(MotorWriteFunc w, MotorBrakeFunc b, EncoderReadFunc, EncoderWriteFunc)
        : index(count() < 2 ? count()++ : 1), write_(w), brake_(b) {}
    void run() { write_(host_servo_output[index]); }
    void stop() { brake_(); }
    void moveTo(long p) { target = p; }
    long getActualPosition() { return 0; }
    bool setPWMSkip(uint8_t) { return true; }
    void setMaxPWM(uint8_t) {}
    void setPIDTunings(double, double, double) {}
    void setAccuracy(unsigned int) {}
    void setCurrentPosition(long) {}

    const int index;
    long target = 0;

private:
    static int &count() { static int n = 0; return n; }
    MotorWriteFunc write_;
    MotorBrakeFunc brake_;
};
