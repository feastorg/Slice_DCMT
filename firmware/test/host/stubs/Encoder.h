// Host stand-in for the Encoder library: a count a test can set.
#pragma once
#include <stdint.h>

class Encoder
{
public:
    Encoder(uint8_t, uint8_t) {}
    long read() { return count; }
    void write(long n) { count = n; }

    long count = 0;
};
