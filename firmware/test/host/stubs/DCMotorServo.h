// Host stand-in for DCMotorServo: inert. These tests cover the firmware
// around the closed-loop library, not the library itself.
#pragma once
#include <stdint.h>

typedef void (*MotorWriteFunc)(int16_t speed);
typedef void (*MotorBrakeFunc)();
typedef long (*EncoderReadFunc)();
typedef void (*EncoderWriteFunc)(long newPosition);

class DCMotorServo
{
public:
    DCMotorServo(MotorWriteFunc, MotorBrakeFunc, EncoderReadFunc, EncoderWriteFunc) {}
    void run() {}
    void stop() {}
    void moveTo(long p) { target = p; }
    long getActualPosition() { return 0; }
    bool setPWMSkip(uint8_t) { return true; }
    void setMaxPWM(uint8_t) {}
    void setPIDTunings(double, double, double) {}
    void setAccuracy(unsigned int) {}
    void setCurrentPosition(long) {}

    long target = 0;
};
