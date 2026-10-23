// Which frames the firmware takes as commands (#45, #50): the real setup()
// against the real CRUMBS core, every opcode sent as a command frame, and the
// result read back through GET_ACK.
#include "host.h"
#include "globals.h"
#include <bread/dcmt_ops.h>
#include <cstdio>

void setup();

static const uint8_t COMMANDS[] = {
    DCMT_OP_SET_OPEN_LOOP, DCMT_OP_SET_BRAKE, DCMT_OP_SET_MODE, DCMT_OP_SET_SETPOINT,
    DCMT_OP_SET_PID, BREAD_OP_SET_WATCHDOG, BREAD_OP_CLEAR_WATCHDOG_TRIP,
};
static const uint8_t QUERIES[] = {DCMT_OP_GET_STATE, BREAD_OP_GET_WATCHDOG, BREAD_OP_GET_CAPS};
static const uint8_t RLHT_TYPE = 0x01;

static bool is_command(uint8_t op)
{
    for (uint8_t c : COMMANDS)
        if (c == op)
            return true;
    return false;
}

static bool refused_unknown(const crumbs_ack_t &a)
{
    return a.status == CRUMBS_ACK_REFUSED && a.reason == BREAD_REFUSE_UNKNOWN_OP;
}

int main()
{
    setup();
    host_check("setup() initialises CRUMBS", host_ctx != nullptr);
    if (!host_ctx)
        return host_finish();

    // Every opcode but SET_REPLY, as a command of DCMT's type with a 6-byte
    // zero payload. A command is never refused UNKNOWN_OP; anything else is.
    const uint8_t zeros[CRUMBS_MAX_PAYLOAD] = {0};
    int wrong_cmd = 0, wrong_other = 0;
    for (int op = 0; op <= 0xFF; op++)
    {
        if (op == CRUMBS_CMD_SET_REPLY)
            continue;
        host_send(DCMT_TYPE_ID, (uint8_t)op, zeros, 6);
        const crumbs_ack_t a = host_read_ack();
        if (is_command((uint8_t)op) && refused_unknown(a))
        {
            printf("     0x%02X: command refused UNKNOWN_OP\n", op);
            wrong_cmd++;
        }
        if (!is_command((uint8_t)op) && !refused_unknown(a))
        {
            printf("     0x%02X: status %u reason 0x%02X, want refused UNKNOWN_OP\n", op, a.status, a.reason);
            wrong_other++;
        }
    }
    host_check("no command opcode is refused UNKNOWN_OP", wrong_cmd == 0);
    host_check("every other opcode is refused UNKNOWN_OP", wrong_other == 0);

    char what[80];
    for (uint8_t q : QUERIES)
    {
        host_send(DCMT_TYPE_ID, q, nullptr, 0);
        snprintf(what, sizeof what, "GET 0x%02X sent as a command is refused UNKNOWN_OP", q);
        host_check(what, refused_unknown(host_read_ack()));

        crumbs_message_t reply;
        snprintf(what, sizeof what, "GET 0x%02X through SET_REPLY answers with that opcode", q);
        host_check(what, host_query(q, &reply) && reply.opcode == q);
    }

    // SET_WATCHDOG 0 disarms, so it is safe to apply on any build.
    const uint8_t disarm[2] = {0, 0};
    host_send(DCMT_TYPE_ID, BREAD_OP_SET_WATCHDOG, disarm, 2);
    host_check("a command of DCMT's type is applied", host_read_ack().status == CRUMBS_ACK_APPLIED);
    crumbs_message_t ack;
    host_send(DCMT_TYPE_ID, BREAD_OP_SET_WATCHDOG, disarm, 2);
    host_check("GET_ACK replies carry DCMT's type", host_query(CRUMBS_CMD_GET_ACK, &ack) && ack.type_id == DCMT_TYPE_ID);
    host_send(CRUMBS_TYPE_ID_ANY, BREAD_OP_SET_WATCHDOG, disarm, 2);
    host_check("a command of type 0x00 is applied", host_read_ack().status == CRUMBS_ACK_APPLIED);

    // Another family's frame is rejected before any handler runs: it neither
    // arms the watchdog nor counts as bus liveness. With a 1000 ms timeout
    // armed, the frame arrives 900 ms in; the trip still comes at 1000 ms,
    // where a refresh would have moved it to 1900 ms.
    const uint8_t arm500[2] = {0xF4, 0x01};
    host_now += 1000;
    commandWatchdog.arm(1000, host_now);
    host_now += 900;
    host_send(RLHT_TYPE, BREAD_OP_SET_WATCHDOG, arm500, 2);
    const crumbs_ack_t a = host_read_ack();
    host_check("another family's frame is rejected REJECT_TYPE",
               a.status == CRUMBS_ACK_REJECTED && a.reason == CRUMBS_ACK_REJECT_TYPE);
    host_check("another family's frame does not arm the watchdog", commandWatchdog.timeoutMs() == 1000);
    host_now += 100;
    host_check("another family's frame does not refresh the watchdog", commandWatchdog.check(host_now));

    return host_finish();
}
