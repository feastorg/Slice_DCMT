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
// Each servo's stall latch, which isStalled() reads and clearStall() clears;
// a test sets it. And what enableStallDetection() last set (-1 if never).
inline bool host_servo_stalled[2] = {false, false};
inline long host_stall_timeout[2] = {-1, -1};
inline long host_stall_min_counts[2] = {-1, -1};
// The encoder count each servo's getActualPosition() returns; a test sets it.
inline long host_servo_actual[2] = {0, 0};

class DCMotorServo
{
public:
    DCMotorServo(MotorWriteFunc w, MotorBrakeFunc b, EncoderReadFunc, EncoderWriteFunc)
        : index(count() < 2 ? count()++ : 1), write_(w), brake_(b) {}
    void run() { write_(host_servo_output[index]); }
    void stop() { brake_(); }
    void moveTo(long p) { target = p; }
    long getActualPosition() { return host_servo_actual[index]; }
    long getRequestedPosition() { return target; }
    bool setPWMSkip(uint8_t) { return true; }
    void setMaxPWM(uint8_t) {}
    void setPIDTunings(double, double, double) {}
    void setAccuracy(unsigned int) {}
    void setCurrentPosition(long) {}
    void enableStallDetection(unsigned long timeout_ms, long min_counts = 4)
    {
        host_stall_timeout[index] = (long)timeout_ms;
        host_stall_min_counts[index] = min_counts;
    }
    bool isStalled() { return host_servo_stalled[index]; }
    void clearStall() { host_servo_stalled[index] = false; }

    const int index;
    long target = 0;

private:
    static int &count() { static int n = 0; return n; }
    MotorWriteFunc write_;
    MotorBrakeFunc brake_;
};
