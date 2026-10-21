// Host stand-in for FastLED: one colour per CRGB, and show() records the
// colour of the status LED (host.cpp).
#pragma once
#include <stdint.h>

struct CRGB
{
    enum Col { Black = 0, Red = 1, Green = 2, Blue = 3 };
    int c = Black;
    CRGB() {}
    CRGB(Col x) : c(x) {}
    bool operator==(const CRGB &o) const { return c == o.c; }
    bool operator!=(const CRGB &o) const { return c != o.c; }
};

#define NEOPIXEL 0
struct FastLEDStub
{
    template <int TYPE, int PIN> void addLeds(CRGB *, int) {}
    void setBrightness(int) {}
    void show();
};
extern FastLEDStub FastLED;
