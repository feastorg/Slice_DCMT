// A build with port 1 analog and port 2 empty (DCMT_PORT1_INPUT =
// DCMT_INPUT_ANALOG, DCMT_PORT2_INPUT = DCMT_INPUT_NONE, the *_analog1
// envs): no encoder on port 1's pins, which are read one analogRead() per
// loop() pass, round-robin, and averaged 16 reads at a time into GET_INPUTS.
// It is open loop only, as a _noenc build is, and GET_CAPS adds
// DCMT_CAP_INPUTS.
#include "host.h"
#include "globals.h"
#include <DCMotorServo.h>
#include <Encoder.h>
#include <bread/bread_caps.h>
#include <bread/dcmt_ops.h>
#include <cstdio>
#include <string>

#if !(DCMT_PORT1_INPUT == DCMT_INPUT_ANALOG && DCMT_PORT2_INPUT == DCMT_INPUT_NONE)
#error "test_analog is built for the *_analog1 envs only"
#endif

void setup();
void loop();

static const uint8_t P1A = MOTOR1_ENCODER_PIN1;
static const uint8_t P1B = MOTOR1_ENCODER_PIN2;

static bool refused(uint8_t reason)
{
    const crumbs_ack_t a = host_read_ack();
    return a.status == CRUMBS_ACK_REFUSED && a.reason == reason;
}

static void set_mode(uint8_t mode) { host_send(DCMT_TYPE_ID, DCMT_OP_SET_MODE, &mode, 1); }

static bool get_inputs(dcmt_inputs_t *v)
{
    crumbs_message_t reply;
    return host_query(DCMT_OP_GET_INPUTS, &reply) && reply.type_id == DCMT_TYPE_ID &&
           reply.opcode == DCMT_OP_GET_INPUTS && dcmt_parse_inputs_payload(reply.data, reply.data_len, v) == 0;
}

static int total_reads()
{
    int n = 0;
    for (int r : host_analog_reads)
        n += r;
    return n;
}

// The value each port 1 pin reads on its i-th read (0-based).
static int (*value_a)(int) = nullptr;
static int (*value_b)(int) = nullptr;

// One loop() pass, with each pin's next reading injected first.
static void pass()
{
    host_analog_value[P1A] = value_a(host_analog_reads[P1A]);
    host_analog_value[P1B] = value_b(host_analog_reads[P1B]);
    loop();
}

static std::string serial(const char *line)
{
    Serial.out.clear();
    Serial.in = line;
    loop();
    return Serial.out;
}

int main()
{
    setup();

    host_check("no Encoder is constructed", Encoder::constructed == 0);
    host_check("both servos were built", host_servo[0] != nullptr && host_servo[1] != nullptr);
    if (host_servo[0] == nullptr || host_servo[1] == nullptr)
        return host_finish();
    host_servo[0]->writeEncoder(1234);
    host_check("each motor's encoder read is 0, before and after a write",
               host_servo[0]->readEncoder() == 0 && host_servo[1]->readEncoder() == 0);

#if defined(ARDUINO_ARCH_MEGAAVR)
    host_check("setup() sets the ADC reference to VDD", host_analog_reference == VDD);
#else
    host_check("setup() leaves the ADC reference at its default", host_analog_reference == -1);
#endif

    // GET_CAPS: the _noenc flags, plus DCMT_CAP_INPUTS.
    crumbs_message_t reply;
    bread_caps_result_t caps = {};
    const bool got_caps =
        host_query(BREAD_OP_GET_CAPS, &reply) && bread_caps_parse_payload(reply.data, reply.data_len, &caps) == 0;
    printf("     GET_CAPS: schema %u level %u flags 0x%04lX\n", caps.schema, caps.level, (unsigned long)caps.flags);
    host_check("GET_CAPS reports level 1", got_caps && caps.level == DCMT_CAP_LEVEL_1);
    host_check("GET_CAPS advertises DCMT_CAP_INPUTS", (caps.flags & DCMT_CAP_INPUTS) != 0);
    host_check("GET_CAPS flags are 0x15E3: the _noenc flags 0x5E3 and DCMT_CAP_INPUTS",
               caps.flags == (0x5E3u | DCMT_CAP_INPUTS) && DCMT_CAP_INPUTS == 0x1000u);

    // Nothing averaged yet.
    dcmt_inputs_t v = {};
    host_check("GET_INPUTS answers before any read, all invalid, bits 10",
               get_inputs(&v) && v.valid == 0 && v.bits == 10 && v.p1a == 0 && v.p1b == 0 && v.p2a == 0 &&
                   v.p2b == 0);

    // First average. p1a reads 500..515: mean 507.5, published rounded, 508.
    // p1b alternates 1000 and 1003: mean 1001.5, 1002.
    value_a = [](int i) { return 500 + i; };
    value_b = [](int i) { return 1000 + (i % 2) * 3; };
    const int before = total_reads();
    for (int i = 0; i < 30; i++)
        pass();
    host_check("one analogRead() per loop() pass, alternating the two pins",
               total_reads() - before == 30 && host_analog_reads[P1A] == 15 && host_analog_reads[P1B] == 15);
    host_check("port 2's pins are never read",
               host_analog_reads[MOTOR2_ENCODER_PIN1] == 0 && host_analog_reads[MOTOR2_ENCODER_PIN2] == 0);
    host_check("after 15 reads per pin, both valid bits are clear and the values 0",
               get_inputs(&v) && v.valid == 0 && v.p1a == 0 && v.p1b == 0);

    pass();
    host_check("p1a's 16th read sets its valid bit alone",
               get_inputs(&v) && v.valid == DCMT_INPUT_VALID_P1A && host_analog_reads[P1A] == 16);
    host_check("p1a is the mean of its 16 reads, rounded (508)", v.p1a == 508);
    host_check("p1b still reads 0", v.p1b == 0);

    pass();
    host_check("p1b's 16th read sets its valid bit",
               get_inputs(&v) && v.valid == (DCMT_INPUT_VALID_P1A | DCMT_INPUT_VALID_P1B));
    printf("     GET_INPUTS: valid 0x%02X bits %u p1a %u p1b %u p2a %u p2b %u\n", v.valid, v.bits, v.p1a, v.p1b,
           v.p2a, v.p2b);
    host_check("p1b is the mean of its 16 reads, rounded (1002)", v.p1b == 1002 && v.p1a == 508);
    host_check("GET_INPUTS reports 10 bits", v.bits == 10);
    host_check("port 2's pins read 0 with their valid bits clear",
               v.p2a == 0 && v.p2b == 0 && (v.valid & (DCMT_INPUT_VALID_P2A | DCMT_INPUT_VALID_P2B)) == 0);

    // The reply's bytes, as the contract lays them out.
    host_check("GET_INPUTS is 10 bytes", host_query(DCMT_OP_GET_INPUTS, &reply) &&
                                             reply.data_len == DCMT_INPUTS_FIXED_LEN &&
                                             DCMT_INPUTS_FIXED_LEN == 10);
    const uint8_t want[10] = {0x03, 10, 508 & 0xFF, 508 >> 8, 1002 & 0xFF, 1002 >> 8, 0, 0, 0, 0};
    host_check("GET_INPUTS bytes are [valid][bits][p1a][p1b][p2a][p2b], little-endian",
               reply.data_len == 10 && memcmp(reply.data, want, 10) == 0);

    // The next average starts from nothing, and the last one stands until it
    // completes.
    value_a = [](int) { return 300; };
    value_b = [](int) { return 700; };
    for (int i = 0; i < 30; i++)
        pass();
    host_check("the published values hold until the next 16 reads",
               get_inputs(&v) && v.p1a == 508 && v.p1b == 1002 && v.valid == 0x03);
    pass();
    pass();
    host_check("the second average is of its own 16 reads (300, 700)",
               get_inputs(&v) && v.p1a == 300 && v.p1b == 700 && v.valid == 0x03);

    // Open loop only, as a _noenc build.
    set_mode(DCMT_MODE_CLOSED_POSITION);
    host_check("SET_MODE position is refused NOT_BUILT", refused(BREAD_REFUSE_NOT_BUILT));
    set_mode(DCMT_MODE_CLOSED_SPEED);
    host_check("SET_MODE speed is refused NOT_BUILT", refused(BREAD_REFUSE_NOT_BUILT));
    host_check("the mode stays open loop", slice.mode == OPEN_LOOP);
    const uint8_t sp[4] = {1, 0, 2, 0};
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_SETPOINT, sp, 4);
    host_check("SET_SETPOINT is refused NOT_BUILT", refused(BREAD_REFUSE_NOT_BUILT));
    host_check("serial MODE=POS lists only OPEN",
               serial("MODE=POS\n").find("MODE options: OPEN (no encoders") != std::string::npos);

    return host_finish();
}
