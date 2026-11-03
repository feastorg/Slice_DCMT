// Host stand-in for the Encoder library: a count a test can set. constructed
// counts the Encoder objects built, so a test can check that a port built
// without one (DCMT_INPUT_NONE) claims no pins.
#pragma once
#include <stdint.h>

class Encoder
{
public:
    Encoder(uint8_t, uint8_t) { constructed++; }
    long read() { return count; }
    void write(long n) { count = n; }

    long count = 0;
    static inline int constructed = 0;
};
