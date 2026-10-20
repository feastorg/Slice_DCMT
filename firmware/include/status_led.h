// include/status_led.h
//
// Driver for the gen2 status LED: one WS2812B on LED_PIN (D5), GRB order,
// 800 kHz. It replaces FastLED, which cost ~3 KB of flash and ~80-112 B of
// RAM to send three bytes.
//
// The bit loop is cycle-counted at 16 MHz. Per bit: 20 cycles (1.25 us),
// high for 5 cycles (312 ns) for a 0 and 12 cycles (750 ns) for a 1, inside
// the WS2812B windows of 250-550 ns and 650-950 ns. out, nop, sbrs, lsl, dec
// and brne take the same cycles on the ATmega328P (AVRe) and the ATmega4809
// (AVRxt), so one loop serves both; st would not, which is why the port is
// written with out.
//
// Interrupts are off for the frame, ~32 us. The firmware writes only on a
// colour change: once at boot and once per e-stop transition.
#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <Arduino.h>
#include "config_hardware.h"

#if DCMT_HAS_STATUS_LED

#if F_CPU != 16000000L
#error "status_led.h is cycle-counted for 16 MHz"
#endif
#if LED_PIN != 5
#error "status_led.h writes D5's port bit; update STATUS_LED_* for the new pin"
#endif

#if defined(__AVR_ATmega4809__)
#define STATUS_LED_OUT VPORTB_OUT // D5 is PB2 on the Nano Every
#define STATUS_LED_DIR VPORTB_DIR
#define STATUS_LED_BIT 2
#elif defined(__AVR_ATmega328P__)
#define STATUS_LED_OUT PORTD // D5 is PD5 on the Nano
#define STATUS_LED_DIR DDRD
#define STATUS_LED_BIT 5
#else
#error "status_led.h supports the ATmega328P and ATmega4809 only"
#endif

static inline void statusLedBegin()
{
    STATUS_LED_OUT &= (uint8_t)~_BV(STATUS_LED_BIT);
    STATUS_LED_DIR |= _BV(STATUS_LED_BIT);
}

static inline void statusLedShow(uint8_t r, uint8_t g, uint8_t b)
{
    const uint8_t frame[3] = {g, r, b};
    const uint8_t sreg = SREG;
    cli();
    // Read the port with interrupts off, so the frame rewrites the other
    // pins on it with the values they already hold.
    const uint8_t hi = STATUS_LED_OUT | _BV(STATUS_LED_BIT);
    const uint8_t lo = STATUS_LED_OUT & (uint8_t)~_BV(STATUS_LED_BIT);
    for (uint8_t i = 0; i < 3; i++)
    {
        uint8_t v = frame[i];
        uint8_t n;
        asm volatile(
            "ldi  %[n], 8\n"
            "1:\n\t"
            "out  %[port], %[hi]\n\t" // 0: high
            "nop\n\tnop\n\tnop\n\t"
            "sbrs %[v], 7\n\t"
            "out  %[port], %[lo]\n\t" // 5: low for a 0 bit
            "lsl  %[v]\n\t"
            "nop\n\tnop\n\tnop\n\tnop\n\tnop\n\t"
            "out  %[port], %[lo]\n\t" // 12: low for a 1 bit
            "nop\n\tnop\n\tnop\n\tnop\n\t"
            "dec  %[n]\n\t"
            "brne 1b\n\t" // 20: next bit
            : [v] "+r"(v), [n] "=&d"(n)
            : [port] "I"(_SFR_IO_ADDR(STATUS_LED_OUT)), [hi] "r"(hi), [lo] "r"(lo));
    }
    SREG = sreg;
}

#endif // DCMT_HAS_STATUS_LED

#endif // STATUS_LED_H
