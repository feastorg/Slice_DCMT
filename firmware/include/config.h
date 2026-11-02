#ifndef CONFIG_H
#define CONFIG_H

// Hardware generation has no default: it selects the motor pin map, and a
// wrong guess drives the motors through the wrong pins (#17).
#ifndef DCMT_HW_GEN
#error "DCMT_HW_GEN is not set: build a gen* env from firmware/platformio.ini, e.g. pio run -e gen2_nano"
#endif

// What each encoder header carries, per port: port 1 feeds motor 1, port 2
// motor 2. ENCODER (the default) builds the quadrature encoder; NONE builds
// nothing on that port's pins, and the motor's position reads 0, as an empty
// encoder header reads today. The *_noenc envs set both to NONE.
// Override with build flags: -DDCMT_PORT1_INPUT=DCMT_INPUT_NONE
// Neither value is 0: a misspelt name reads as 0 in #if, and must hit the
// #error below rather than quietly build a port as NONE.
#define DCMT_INPUT_NONE 1
#define DCMT_INPUT_ENCODER 2
#ifndef DCMT_PORT1_INPUT
#define DCMT_PORT1_INPUT DCMT_INPUT_ENCODER
#endif
#ifndef DCMT_PORT2_INPUT
#define DCMT_PORT2_INPUT DCMT_INPUT_ENCODER
#endif
#if (DCMT_PORT1_INPUT != DCMT_INPUT_NONE && DCMT_PORT1_INPUT != DCMT_INPUT_ENCODER) || \
    (DCMT_PORT2_INPUT != DCMT_INPUT_NONE && DCMT_PORT2_INPUT != DCMT_INPUT_ENCODER)
#error "DCMT_PORT1_INPUT and DCMT_PORT2_INPUT take DCMT_INPUT_ENCODER or DCMT_INPUT_NONE"
#endif

// The control mode applies to both motors, so closed loop is built only when
// both ports carry an encoder. Otherwise the build is open loop only:
// GET_CAPS reports level 1, and the closed-loop modes, setpoints and gains
// are refused BREAD_REFUSE_NOT_BUILT.
#define DCMT_HAS_POSITION_LOOP \
    (DCMT_PORT1_INPUT == DCMT_INPUT_ENCODER && DCMT_PORT2_INPUT == DCMT_INPUT_ENCODER)

// Speed-loop default is based on MCU family:
// - classic Nano (AVR) -> disabled by default
// - Nano Every (megaAVR) -> enabled by default
// and on both ports carrying an encoder.
// Override with build flag: -DDCMT_ENABLE_SPEED_LOOP=0/1
#ifndef DCMT_ENABLE_SPEED_LOOP
#if DCMT_HAS_POSITION_LOOP && (defined(ARDUINO_ARCH_MEGAAVR) || defined(ARDUINO_AVR_NANO_EVERY))
#define DCMT_ENABLE_SPEED_LOOP 1
#else
#define DCMT_ENABLE_SPEED_LOOP 0
#endif
#endif
#if DCMT_ENABLE_SPEED_LOOP && !DCMT_HAS_POSITION_LOOP
#error "DCMT_ENABLE_SPEED_LOOP needs an encoder on both ports"
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
