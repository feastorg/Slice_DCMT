#include <Arduino.h>
#include "analog_inputs.h"

#if DCMT_HAS_ANALOG_INPUTS
#include "config_hardware.h"

// slot is the pin's place in GET_INPUTS: 0 p1a, 1 p1b, 2 p2a, 3 p2b. Its
// valid bit is DCMT_INPUT_VALID_* (1 << slot).
struct AnalogPin
{
    uint8_t pin;
    uint8_t slot;
};

static const AnalogPin analogPins[] = {
#if DCMT_PORT1_INPUT == DCMT_INPUT_ANALOG
    {MOTOR1_ENCODER_PIN1, 0},
    {MOTOR1_ENCODER_PIN2, 1},
#endif
#if DCMT_PORT2_INPUT == DCMT_INPUT_ANALOG
    {MOTOR2_ENCODER_PIN1, 2},
    {MOTOR2_ENCODER_PIN2, 3},
#endif
};
static const uint8_t ANALOG_PIN_COUNT = sizeof analogPins / sizeof analogPins[0];

static_assert(DCMT_INPUT_VALID_P1A == 1u << 0 && DCMT_INPUT_VALID_P1B == 1u << 1 &&
                  DCMT_INPUT_VALID_P2A == 1u << 2 && DCMT_INPUT_VALID_P2B == 1u << 3,
              "a pin's valid bit is 1 << its slot");
// 16 reads of 1023 sum to 16368.
static_assert((uint32_t)DCMT_ANALOG_AVERAGE * 1023u <= 0xFFFFu, "the sum fits a uint16_t");

// Loop-owned: the sums being taken.
static uint8_t nextPin = 0;
static uint16_t sums[ANALOG_PIN_COUNT];
static uint8_t reads[ANALOG_PIN_COUNT];

// Published in a masked window, read by the reply handler in the I2C
// interrupt.
static volatile uint16_t published[4];
static volatile uint8_t validBits = 0;

void analogInputsBegin()
{
#if defined(ARDUINO_ARCH_MEGAAVR)
    analogReference(VDD);
#endif
}

void analogInputsPoll()
{
    const AnalogPin &p = analogPins[nextPin];
    sums[nextPin] += (uint16_t)analogRead(p.pin);
    if (++reads[nextPin] == DCMT_ANALOG_AVERAGE)
    {
        // Rounded to the nearest count.
        const uint16_t avg = (uint16_t)((sums[nextPin] + DCMT_ANALOG_AVERAGE / 2) / DCMT_ANALOG_AVERAGE);
        noInterrupts();
        published[p.slot] = avg;
        validBits |= (uint8_t)(1u << p.slot);
        interrupts();
        sums[nextPin] = 0;
        reads[nextPin] = 0;
    }
    if (++nextPin == ANALOG_PIN_COUNT)
        nextPin = 0;
}

void analogInputsRead(dcmt_inputs_t *out)
{
    out->valid = validBits;
    out->bits = DCMT_ANALOG_BITS;
    out->p1a = published[0];
    out->p1b = published[1];
    out->p2a = published[2];
    out->p2b = published[3];
}
#endif
