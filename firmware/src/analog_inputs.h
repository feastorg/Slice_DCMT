// Analog inputs on the encoder headers (DCMT_INPUT_ANALOG, #106 step 2).
// Built only when a port is ANALOG (DCMT_HAS_ANALOG_INPUTS): each such port
// reads both of its pins, port 1 MOTOR1_ENCODER_PIN1/PIN2 (A2/A3) as p1a/p1b,
// port 2 MOTOR2_ENCODER_PIN1/PIN2 (A0/A1) as p2a/p2b.
#pragma once
#include "config.h"

#if DCMT_HAS_ANALOG_INPUTS
#include <stdint.h>
#include <bread/dcmt_ops.h>

// Reads averaged into one published value, per pin.
#define DCMT_ANALOG_AVERAGE 16
// The ADC resolution GET_INPUTS reports: analogRead() returns 10 bits on
// both MCUs.
#define DCMT_ANALOG_BITS 10

// Sets the ADC reference to VDD on the Nano Every. Its core's init() already
// selects VDD (DEFAULT is INTERNAL0V55, which nothing here uses); this keeps
// the reference explicit next to the reader. The classic Nano keeps AVcc.
void analogInputsBegin();

// One analogRead() per call, round-robin over the analog pins. Called once
// per loop() pass. Publishes a pin's average, and sets its valid bit, every
// DCMT_ANALOG_AVERAGE reads of that pin.
void analogInputsPoll();

// The published values as GET_INPUTS reports them. Call with interrupts off:
// the reply handler runs in the I2C interrupt, and analogInputsPoll()
// publishes in a masked window, as motorControlLogic() does for GET_STATE.
void analogInputsRead(dcmt_inputs_t *out);
#endif
