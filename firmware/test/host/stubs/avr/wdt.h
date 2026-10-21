// Host stand-in for the AVR watchdog: does nothing.
#pragma once
#define WDTO_1S 6
inline void wdt_disable() {}
inline void wdt_enable(int) {}
inline void wdt_reset() {}
