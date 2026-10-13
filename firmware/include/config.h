#ifndef CONFIG_H
#define CONFIG_H

// Hardware generation has no default: it selects the motor pin map, and a
// wrong guess drives the motors through the wrong pins (#17).
#ifndef DCMT_HW_GEN
#error "DCMT_HW_GEN is not set: build a gen* env from firmware/platformio.ini, e.g. pio run -e gen2_nano"
#endif

// Speed-loop default is based on MCU family:
// - classic Nano (AVR) -> disabled by default
// - Nano Every (megaAVR) -> enabled by default
// Override with build flag: -DDCMT_ENABLE_SPEED_LOOP=0/1
#ifndef DCMT_ENABLE_SPEED_LOOP
#if defined(ARDUINO_ARCH_MEGAAVR) || defined(ARDUINO_AVR_NANO_EVERY)
#define DCMT_ENABLE_SPEED_LOOP 1
#else
#define DCMT_ENABLE_SPEED_LOOP 0
#endif
#endif

// Bus address is independent of hardware generation.
// Override with a build flag: -DI2C_ADR=<addr>
#ifndef I2C_ADR
#define I2C_ADR 21
#endif

#ifdef SLICE_DEBUG

#define SLICE_DEBUG_PRINT(...) \
    Serial.print(__VA_ARGS__)

#define SLICE_DEBUG_PRINTLN(...) \
    Serial.println(__VA_ARGS__)

#else
#define SLICE_DEBUG_PRINT(...)
#define SLICE_DEBUG_PRINTLN(...)
#endif

#endif // CONFIG_H
