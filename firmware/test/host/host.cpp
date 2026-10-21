#include "host.h"
#include "globals.h"
#include <crumbs_message_helpers.h>
#include <cstdio>

HardwareSerial Serial;
FastLEDStub FastLED;

unsigned long host_now = 0;
int host_estop_level = HIGH;
int host_led_shown[64];
int host_led_shows = 0;
crumbs_context_t *host_ctx = nullptr;

unsigned long millis() { return host_now; }
void noInterrupts() {}
void interrupts() {}
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t, uint8_t) {}

int digitalRead(uint8_t pin)
{
    host_now++;
    return pin == ESTOP ? host_estop_level : LOW;
}

void FastLEDStub::show()
{
#if DCMT_HAS_STATUS_LED
    if (host_led_shows < 64)
        host_led_shown[host_led_shows] = led.c;
#endif
    host_led_shows++;
}

extern "C" void crumbs_arduino_init_peripheral(crumbs_context_t *ctx, uint8_t address)
{
    crumbs_init(ctx, CRUMBS_ROLE_PERIPHERAL, address);
    host_ctx = ctx;
}

void host_send(uint8_t type_id, uint8_t opcode, const uint8_t *data, uint8_t len)
{
    crumbs_message_t m;
    crumbs_msg_init(&m, type_id, opcode);
    for (uint8_t i = 0; i < len; i++)
        m.data[i] = data[i];
    m.data_len = len;
    uint8_t buf[CRUMBS_MESSAGE_MAX_SIZE];
    size_t n = crumbs_encode_message(&m, buf, sizeof buf);
    crumbs_peripheral_handle_receive(host_ctx, buf, n);
}

bool host_query(uint8_t opcode, crumbs_message_t *reply)
{
    host_send(CRUMBS_TYPE_ID_ANY, CRUMBS_CMD_SET_REPLY, &opcode, 1);
    uint8_t out[CRUMBS_MESSAGE_MAX_SIZE];
    size_t n = 0;
    crumbs_peripheral_build_reply(host_ctx, out, sizeof out, &n);
    return n > 0 && crumbs_decode_message(out, n, reply, nullptr) == 0;
}

crumbs_ack_t host_read_ack()
{
    crumbs_message_t reply;
    crumbs_ack_t a = {};
    if (host_query(CRUMBS_CMD_GET_ACK, &reply))
        crumbs_ack_decode(&reply, &a);
    return a;
}

static int fails = 0;

void host_check(const char *what, bool ok)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
}

int host_finish()
{
    printf("%d failed\n", fails);
    return fails ? 1 : 0;
}
