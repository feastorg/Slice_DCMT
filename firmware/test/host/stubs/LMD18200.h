// Host stand-in for the LMD18200 driver. Each instance records what the
// firmware last asked of it, so a test can check the motor output.
#pragma once
#include <stdint.h>

class LMD18200
{
public:
    LMD18200(uint8_t, uint8_t, uint8_t, uint8_t = 255, uint8_t = 255) {}
    void begin() { begun = true; }
    void write(int16_t speed) { lastWrite = speed; braked = false; writes++; }
    void brake() { braked = true; brakes++; }
    void releaseBrake() { braked = false; }

    bool begun = false;
    bool braked = false;
    int16_t lastWrite = 0;
    unsigned writes = 0;
    unsigned brakes = 0;
};
