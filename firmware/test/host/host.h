// Shared host-test plumbing: a fake clock and e-stop line, the CRUMBS
// context setup() created, frame send/receive through the real CRUMBS core,
// and pass/fail reporting.
#pragma once
#include <crumbs.h>
#include <stddef.h>
#include <stdint.h>

// Fake time. millis() returns host_now. Every digitalRead() advances it by
// 1 ms, so setup()'s boot wait, which polls the e-stop until a second has
// passed, ends after about 1000 reads.
extern unsigned long host_now;

// Level of the e-stop input (pin ESTOP). HIGH is released: the firmware uses
// the internal pull-up and asserts on LOW.
extern int host_estop_level;

// The context setup() passed to crumbs_arduino_init_peripheral(), or null.
extern crumbs_context_t *host_ctx;

// Sends one command frame through the CRUMBS receive path, as the I2C
// receive interrupt would.
void host_send(uint8_t type_id, uint8_t opcode, const uint8_t *data, uint8_t len);

// Stages a reply opcode with SET_REPLY, builds the reply as the I2C request
// interrupt would, and decodes it. Returns false if no valid frame came back.
bool host_query(uint8_t opcode, crumbs_message_t *reply);

// Reads GET_ACK for the last command, the way a controller does.
crumbs_ack_t host_read_ack();

// Prints one result line and counts failures. host_finish() prints the
// total and returns the process exit code.
void host_check(const char *what, bool ok);
int host_finish();
