// src/main.cpp
/**
 * @file main.cpp
 * @brief DCMT firmware (PlatformIO) for the BREAD system.
 */

#include <Arduino.h>
#include <FastLED.h>
#include <crumbs.h>
#include <crumbs_arduino.h>
#include <LMD18200.h>
#include <Encoder.h>
#include "config.h"
#include "config_hardware.h"
#include "globals.h"
#include <DCMotorServo.h>
#if DCMT_ENABLE_SPEED_LOOP
#include <DCMotorTacho.h>
#endif
#include <bread/dcmt_ops.h>
#include <math.h>

// Watchdog
#if defined(__AVR__) || defined(ARDUINO_ARCH_MEGAAVR)
#include <avr/wdt.h>
#endif

// ---- CRUMBS context ----
static crumbs_context_t ctx;

// ---- Global flags/objects ----
#if DCMT_HAS_STATUS_LED
CRGB led;
#endif

// Command watchdog state (see globals.h). ISR-written; main loop snapshots
// under short masked windows.
volatile uint16_t wdTimeoutMs = 0;
volatile unsigned long wdLastRxMs = 0;
volatile bool wdTripped = false;
volatile uint8_t wdTripCount = 0;
volatile uint8_t estopCount = 0;
volatile uint8_t holdResyncMask = 0;
volatile int16_t motor1AppliedPWM = 0;
volatile int16_t motor2AppliedPWM = 0;

static const unsigned long ESTOP_DEBOUNCE_MS = 25;
static bool estopReleasePending = false;
static unsigned long estopReleaseStartMs = 0;

// Motor drivers
LMD18200 motor1Driver(MOTOR1_PWM_PIN, MOTOR1_DIR_PIN, MOTOR1_BRAKE_PIN, MOTOR1_CSENSE_PIN);
LMD18200 motor2Driver(MOTOR2_PWM_PIN, MOTOR2_DIR_PIN, MOTOR2_BRAKE_PIN, MOTOR2_CSENSE_PIN);

// Encoders
Encoder motor1Encoder(MOTOR1_ENCODER_PIN1, MOTOR1_ENCODER_PIN2);
Encoder motor2Encoder(MOTOR2_ENCODER_PIN1, MOTOR2_ENCODER_PIN2);

// Shared state
DCMT_SLICE slice;
Timing timing = {0};

static ControlModes appliedMode = OPEN_LOOP;

static PIDTunings appliedPosPid1;
static PIDTunings appliedPosPid2;
static bool positionPidApplied = false;

#if DCMT_ENABLE_SPEED_LOOP
static PIDTunings appliedSpeedPid1;
static PIDTunings appliedSpeedPid2;
static bool speedPidApplied = false;
#endif

static int16_t clamp_pwm(long v)
{
    if (v < -255)
        return -255;
    if (v > 255)
        return 255;
    return static_cast<int16_t>(v);
}

static int16_t clamp_i16(long v)
{
    if (v < -32767)
        return -32767;
    if (v > 32767)
        return 32767;
    return static_cast<int16_t>(v);
}

static bool pid_tunings_equal(const PIDTunings &a, const PIDTunings &b)
{
    const float eps = 0.0001f;
    return fabsf(a.kp - b.kp) < eps &&
           fabsf(a.ki - b.ki) < eps &&
           fabsf(a.kd - b.kd) < eps;
}

// ---- Motor output ----
// Every write and brake goes through these, so motorNAppliedPWM is the PWM
// each motor is driven at (GET_STATE m1_pwm/m2_pwm, #61): the clamped write,
// or 0 when braked. DCMotorServo calls them too. None is called inside a
// masked window, so the record masks its own.
static void record_applied(volatile int16_t &applied, int16_t pwm)
{
    noInterrupts();
    applied = pwm;
    interrupts();
}

static void motor1_write(int16_t speed)
{
    motor1Driver.write(speed);
    record_applied(motor1AppliedPWM, clamp_pwm(speed));
}

static void motor1_brake(void)
{
    motor1Driver.brake();
    record_applied(motor1AppliedPWM, 0);
}

static long motor1_read_encoder(void)
{
    return motor1Encoder.read();
}

static void motor1_write_encoder(long newPosition)
{
    motor1Encoder.write(newPosition);
}

static void motor2_write(int16_t speed)
{
    motor2Driver.write(speed);
    record_applied(motor2AppliedPWM, clamp_pwm(speed));
}

static void motor2_brake(void)
{
    motor2Driver.brake();
    record_applied(motor2AppliedPWM, 0);
}

static long motor2_read_encoder(void)
{
    return motor2Encoder.read();
}

static void motor2_write_encoder(long newPosition)
{
    motor2Encoder.write(newPosition);
}

// ---- Closed-loop backend ----
static DCMotorServo servo1(motor1_write, motor1_brake, motor1_read_encoder, motor1_write_encoder);
static DCMotorServo servo2(motor2_write, motor2_brake, motor2_read_encoder, motor2_write_encoder);

#if DCMT_ENABLE_SPEED_LOOP
static DCMotorTacho tacho1(&servo1, MOTOR1_CPR, DCMT_TACHO_INTERVAL_MS);
static DCMotorTacho tacho2(&servo2, MOTOR2_CPR, DCMT_TACHO_INTERVAL_MS);
#endif

static void stop_control_loops(void)
{
    servo1.stop();
    servo2.stop();
#if DCMT_ENABLE_SPEED_LOOP
    tacho1.stop();
    tacho2.stop();
#endif
}

static void apply_position_pid_if_needed(const DCMT_SLICE &s)
{
    if (positionPidApplied &&
        pid_tunings_equal(appliedPosPid1, s.posPid1) &&
        pid_tunings_equal(appliedPosPid2, s.posPid2))
    {
        return;
    }

    servo1.setPIDTunings(s.posPid1.kp, s.posPid1.ki, s.posPid1.kd);
    servo2.setPIDTunings(s.posPid2.kp, s.posPid2.ki, s.posPid2.kd);

    appliedPosPid1 = s.posPid1;
    appliedPosPid2 = s.posPid2;
    positionPidApplied = true;
}

#if DCMT_ENABLE_SPEED_LOOP
static void apply_speed_pid_if_needed(const DCMT_SLICE &s)
{
    if (speedPidApplied &&
        pid_tunings_equal(appliedSpeedPid1, s.speedPid1) &&
        pid_tunings_equal(appliedSpeedPid2, s.speedPid2))
    {
        return;
    }

    tacho1.setPIDTunings(s.speedPid1.kp, s.speedPid1.ki, s.speedPid1.kd);
    tacho2.setPIDTunings(s.speedPid2.kp, s.speedPid2.ki, s.speedPid2.kd);

    appliedSpeedPid1 = s.speedPid1;
    appliedSpeedPid2 = s.speedPid2;
    speedPidApplied = true;
}
#endif

static void apply_mode_transition(const DCMT_SLICE &s)
{
    if (s.mode == appliedMode)
        return;

    stop_control_loops();

    if (s.mode == CLOSED_LOOP_POSITION)
    {
        // Preserve controller-provided setpoints across mode transitions.
        servo1.moveTo(s.motor1PositionSetpoint);
        servo2.moveTo(s.motor2PositionSetpoint);
    }
#if DCMT_ENABLE_SPEED_LOOP
    else if (s.mode == CLOSED_LOOP_SPEED)
    {
        // Preserve controller-provided speed setpoints across mode transitions.
        tacho1.setSpeedRPM(s.motor1SpeedSetpoint);
        tacho2.setSpeedRPM(s.motor2SpeedSetpoint);
    }
#endif

    appliedMode = s.mode;
}

void setup()
{
    wdt_disable();
    // e-stop: no external bias resistor on current boards, so use the internal
    // pull-up. Enable it first, so it has the rest of setup to charge the
    // line before the first sample below.
    pinMode(ESTOP, INPUT_PULLUP);
    setupSlice();
    setupDCMT();
    // I2C is live from setupSlice(), so sample the e-stop through the boot
    // second instead of sleeping: a line held at power-up, or pressed now,
    // is asserted before loop() runs. The status LED stays blue through it;
    // loop() shows the e-stop state from its first pass.
    const unsigned long bootStartMs = millis();
    while (millis() - bootStartMs < 1000)
        pollEStop();
    wdt_enable(WDTO_1S);
}

void loop()
{
    wdt_reset();
    pollEStop();
    updateStatusLed();
    watchdogLogic();
    motorControlLogic();
    serialCommands();
    printSerialOutput();
}

// Fires for every CRC-valid inbound command frame (SET_REPLY excluded by
// CRUMBS): any valid command proves a live master, so it stamps liveness.
//
// It deliberately does NOT clear the trip. A trip means the master went away
// while this board was driving something, and the hold engaged by
// watchdogLogic()/motorControlLogic() is a safe state. Incidental traffic
// arriving afterwards proves the master is back; it does not prove anyone
// decided it is safe to release a brake holding a load.
//
// Clearing here made the trip releasable by accident, and the accident had a
// name: the controller's e-stop ladder drives its safe state as ordinary
// command frames, so pressing e-stop on a tripped board cleared the trip, and
// the OPEN_LOOP write(0) that followed released the brake the watchdog had
// engaged. The emergency stop left the machine LESS stopped than it found it
// (anolishq/anolis#261).
//
// The trip now latches until an explicit clear: BREAD_OP_CLEAR_WATCHDOG_TRIP
// (handler_clear_watchdog_trip), the serial WDCLEAR command, or a reboot.
// SET_WATCHDOG re-arms without clearing (feastorg/Slice_DCMT#26), and
// DCMT_CAP_CLEAR_WATCHDOG_TRIP in GET_CAPS tells a controller so.
//
// Every valid frame stamps liveness, a refused one included: the watchdog
// watches for a live master, not for commands it accepts. on_message runs
// before the opcode's handler, so a frame whose opcode is not a command is
// refused here; CRUMBS cannot reject it itself, since on_message takes every
// frame.
//
// The commands are the opcodes setupSlice() registers a handler for, listed
// by hand: the contract's opcode tables also hold the GET opcodes, which are
// queries answered through SET_REPLY. A GET opcode sent as a command frame
// has nothing behind it, and read APPLIED before (feastorg/Slice_DCMT#45).
static bool is_command_op(uint8_t opcode)
{
    switch (opcode)
    {
    case DCMT_OP_SET_OPEN_LOOP:
    case DCMT_OP_SET_BRAKE:
    case DCMT_OP_SET_MODE:
    case DCMT_OP_SET_SETPOINT:
    case DCMT_OP_SET_PID:
    case BREAD_OP_SET_WATCHDOG:
    case BREAD_OP_CLEAR_WATCHDOG_TRIP:
        return true;
    default:
        return false;
    }
}

static void on_crumbs_message(crumbs_context_t *c, const crumbs_message_t *msg)
{
    wdLastRxMs = millis();
    if (!is_command_op(msg->opcode))
        (void)crumbs_refuse(c, BREAD_REFUSE_UNKNOWN_OP);
}

// ---- Implementation (previously in .ino files) ----

void setupSlice()
{
    int rc;
    Serial.begin(115200);

    // CRUMBS
    crumbs_arduino_init_peripheral(&ctx, I2C_ADR);
    // CRUMBS rejects a frame typed for another family before any handler
    // runs: the families' opcodes overlap, so one misaddressed would
    // otherwise be acted on as a DCMT command. Type 0x00 still matches.
    crumbs_set_type_id(&ctx, DCMT_TYPE_ID);
    crumbs_set_callbacks(&ctx, on_crumbs_message, nullptr, nullptr);

    rc = crumbs_register_handler(&ctx, DCMT_OP_SET_OPEN_LOOP, handler_set_open_loop, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register DCMT_OP_SET_OPEN_LOOP"));

    rc = crumbs_register_handler(&ctx, DCMT_OP_SET_BRAKE, handler_set_brake, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register DCMT_OP_SET_BRAKE"));

    rc = crumbs_register_handler(&ctx, DCMT_OP_SET_MODE, handler_set_mode, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register DCMT_OP_SET_MODE"));

    rc = crumbs_register_handler(&ctx, DCMT_OP_SET_SETPOINT, handler_set_setpoint, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register DCMT_OP_SET_SETPOINT"));

    rc = crumbs_register_handler(&ctx, DCMT_OP_SET_PID, handler_set_pid, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register DCMT_OP_SET_PID"));

    rc = crumbs_register_handler(&ctx, BREAD_OP_SET_WATCHDOG, handler_set_watchdog, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register BREAD_OP_SET_WATCHDOG"));

    rc = crumbs_register_handler(&ctx, BREAD_OP_CLEAR_WATCHDOG_TRIP, handler_clear_watchdog_trip, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register BREAD_OP_CLEAR_WATCHDOG_TRIP"));

    rc = crumbs_register_reply_handler(&ctx, 0x00, reply_version, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register version reply handler"));

    rc = crumbs_register_reply_handler(&ctx, DCMT_OP_GET_STATE, reply_get_state, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register DCMT_OP_GET_STATE reply handler"));

    rc = crumbs_register_reply_handler(&ctx, BREAD_OP_GET_CAPS, reply_get_caps, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register BREAD_OP_GET_CAPS reply handler"));

    rc = crumbs_register_reply_handler(&ctx, BREAD_OP_GET_WATCHDOG, reply_get_watchdog, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register BREAD_OP_GET_WATCHDOG reply handler"));

#ifdef DCMT_WATCHDOG_BOOT_MS
    // Integration opt-in: come up armed (e.g. e-stop wirings that power-cycle
    // the board). Default builds boot disarmed.
    wdTimeoutMs = (uint16_t)DCMT_WATCHDOG_BOOT_MS;
    wdLastRxMs = millis();
#endif

    // Status LED -- gen2 only; see config_hardware.h
#if DCMT_HAS_STATUS_LED
    FastLED.addLeds<NEOPIXEL, LED_PIN>(&led, 1);
    FastLED.setBrightness(50);
    led = CRGB::Blue;
    FastLED.show();
#endif

    SLICE_DEBUG_PRINTLN(F("DCMT SLICE INITIALIZED"));
    SLICE_DEBUG_PRINT(F("VERSION: "));
    SLICE_DEBUG_PRINT(DCMT_MODULE_VER_MAJOR);
    SLICE_DEBUG_PRINT(F("."));
    SLICE_DEBUG_PRINT(DCMT_MODULE_VER_MINOR);
    SLICE_DEBUG_PRINT(F("."));
    SLICE_DEBUG_PRINTLN(DCMT_MODULE_VER_PATCH);
#if (DCMT_HW_GEN == 1)
    SLICE_DEBUG_PRINTLN(F("HW Profile: Gen1"));
#else
    SLICE_DEBUG_PRINTLN(F("HW Profile: Gen2"));
#endif
    SLICE_DEBUG_PRINTLN(F("Closed-loop backend: DCMotorServo"));
    SLICE_DEBUG_PRINT(F("CPR M1/M2: "));
    SLICE_DEBUG_PRINT(MOTOR1_CPR);
    SLICE_DEBUG_PRINT(F("/"));
    SLICE_DEBUG_PRINTLN(MOTOR2_CPR);
#if DCMT_ENABLE_SPEED_LOOP
    SLICE_DEBUG_PRINTLN(F("Speed Profile: Enabled"));
#else
    SLICE_DEBUG_PRINTLN(F("Speed Profile: Disabled"));
#endif
}

void setupDCMT()
{
    motor1Driver.begin();
    motor2Driver.begin();
    motor1_write(0);
    motor2_write(0);

    apply_position_pid_if_needed(slice);
    servo1.setPWMSkip(DCMT_SERVO_PWM_SKIP);
    servo2.setPWMSkip(DCMT_SERVO_PWM_SKIP);
    servo1.setMaxPWM(DCMT_SERVO_MAX_PWM);
    servo2.setMaxPWM(DCMT_SERVO_MAX_PWM);
    servo1.setAccuracy(DCMT_SERVO_ACCURACY);
    servo2.setAccuracy(DCMT_SERVO_ACCURACY);

    servo1.setCurrentPosition(motor1Encoder.read());
    servo2.setCurrentPosition(motor2Encoder.read());

#if DCMT_ENABLE_SPEED_LOOP
    apply_speed_pid_if_needed(slice);
    tacho1.setSpeedRPM(0);
    tacho2.setSpeedRPM(0);
#endif

    slice.motor1Position = clamp_i16(servo1.getActualPosition());
    slice.motor2Position = clamp_i16(servo2.getActualPosition());

    timing.lastSerialPrint = millis();
    appliedMode = slice.mode;
}

// Poll/processing
// The line is sampled on every pass, not edge-triggered. The first LOW
// sample asserts the e-stop, so bounce or an edge train cannot postpone a
// stop (#39); release needs the line HIGH for ESTOP_DEBOUNCE_MS without a
// single LOW sample.
void pollEStop()
{
    // Internal pull-up means asserted e-stop pulls the line LOW.
    if (digitalRead(ESTOP) == LOW)
    {
        estopReleasePending = false;
        if (!slice.eStop)
            processEStop(true);
    }
    else if (slice.eStop)
    {
        if (!estopReleasePending)
        {
            estopReleasePending = true;
            estopReleaseStartMs = millis();
        }
        else if (millis() - estopReleaseStartMs >= ESTOP_DEBOUNCE_MS)
        {
            estopReleasePending = false;
            processEStop(false);
        }
    }
}

// Red while the e-stop is held, green otherwise, written only on a change.
// Called from loop() only. The boot wait in setup() polls the e-stop too, and
// when pollEStop() wrote the LED, its first pass replaced the boot blue
// within microseconds.
void updateStatusLed()
{
#if DCMT_HAS_STATUS_LED
    static CRGB lastLed = CRGB::Black;
    CRGB next = slice.eStop ? CRGB::Red : CRGB::Green;
    if (next != lastLed)
    {
        led = next;
        FastLED.show();
        lastLed = next;
    }
#endif
}

void processEStop(bool pressed)
{
    if (pressed)
    {
        stop_control_loops();
        motor1_brake();
        motor2_brake();

        // The press holds like a watchdog trip (DCMT_CAP_ESTOP_HOLD): both
        // brake flags set and the position setpoints at the encoders, so the
        // release moves nothing and a later brake release holds position.
        // Read the encoders outside the masked window (Encoder::read()
        // re-enables interrupts).
        const int16_t pos1 = clamp_i16(servo1.getActualPosition());
        const int16_t pos2 = clamp_i16(servo2.getActualPosition());

        // eStop is set in the same masked window, so the I2C handlers, which
        // refuse actuating commands while it is set, can never store one
        // after these fields are reset.
        noInterrupts();
        // Count a new press only (released -> pressed). A line held at
        // power-up counts as one press. Wraps at 255.
        if (!slice.eStop)
            estopCount++;
        slice.eStop = true;
        slice.motor1PWM = 0;
        slice.motor2PWM = 0;
        slice.motor1Speed = 0;
        slice.motor2Speed = 0;
        slice.motor1SpeedSetpoint = 0;
        slice.motor2SpeedSetpoint = 0;
        slice.motor1Brake = true;
        slice.motor2Brake = true;
        slice.motor1PositionSetpoint = pos1;
        slice.motor2PositionSetpoint = pos2;
        slice.motor1Position = pos1;
        slice.motor2Position = pos2;
        holdResyncMask = HOLD_RESYNC_M1 | HOLD_RESYNC_M2;
        interrupts();
        SLICE_DEBUG_PRINTLN(F("ESTOP PRESSED!"));
    }
    else
    {
        noInterrupts();
        slice.eStop = false;
        interrupts();
        SLICE_DEBUG_PRINTLN(F("ESTOP RELEASED!"));
    }
}

void watchdogLogic()
{
    uint16_t timeout;
    unsigned long lastRx;
    bool tripped;

    noInterrupts();
    timeout = wdTimeoutMs;
    lastRx = wdLastRxMs;
    tripped = wdTripped;
    interrupts();

    if (timeout == 0)
        return;

    if (tripped)
    {
        // Hold safe state until an explicit clear (CLEAR_WATCHDOG_TRIP or
        // serial WDCLEAR) releases the trip.
        // brake() only (no write(0) first): write() releases the brake pin,
        // so alternating them every iteration toggles the driver at loop
        // frequency — audible squeal (found live at first watchdog trip).
        stop_control_loops();
        motor1_brake();
        motor2_brake();
        return;
    }

    if (millis() - lastRx < timeout)
        return;

    stop_control_loops();
    motor1_brake();
    motor2_brake();

    // Clearing a trip resumes nothing: the operator re-commands afterwards.
    // So the trip leaves no motion pending: PWM and speed setpoints zeroed,
    // both brake flags set, and the position setpoints moved to where the
    // shafts are now, so a later brake release holds position instead of
    // finishing an interrupted move (motorControlLogic() keeps them synced
    // while the shafts coast during the hold). Read the encoders here,
    // outside the masked window (Encoder::read() re-enables interrupts), not
    // from slice.motorNPosition, which is one loop stale.
    const int16_t pos1 = clamp_i16(servo1.getActualPosition());
    const int16_t pos2 = clamp_i16(servo2.getActualPosition());

    // wdTripped is set in the same masked window, so the I2C handlers, which
    // refuse actuating commands while it is set, can never store one after
    // these fields are reset.
    noInterrupts();
    wdTripped = true;
    wdTripCount++;
    slice.motor1PWM = 0;
    slice.motor2PWM = 0;
    slice.motor1Speed = 0;
    slice.motor2Speed = 0;
    slice.motor1SpeedSetpoint = 0;
    slice.motor2SpeedSetpoint = 0;
    slice.motor1Brake = true;
    slice.motor2Brake = true;
    slice.motor1PositionSetpoint = pos1;
    slice.motor2PositionSetpoint = pos2;
    holdResyncMask = HOLD_RESYNC_M1 | HOLD_RESYNC_M2;
    interrupts();
    SLICE_DEBUG_PRINTLN(F("WATCHDOG TRIPPED: bus silent, motors braked"));
}

// Call inside the commit window with this pass's positions. After a hold
// ends, a motor whose hold-engaged brake is still on keeps its position
// setpoint on its encoder, so SET_BRAKE(0,0) holds where the shaft crept
// to rather than driving it back (DCMT_CAP_ESTOP_HOLD). Releasing that
// brake, or a new setpoint for that motor, clears its holdResyncMask bit,
// so a setpoint the controller sends is never overwritten. The speed
// setpoints need no follow: the hold zeroed them, and only a new setpoint,
// which ends the follow, sets them again.
static void follow_held_setpoints(int16_t pos1, int16_t pos2)
{
    if (slice.motor1Brake && (holdResyncMask & HOLD_RESYNC_M1))
        slice.motor1PositionSetpoint = pos1;
    if (slice.motor2Brake && (holdResyncMask & HOLD_RESYNC_M2))
        slice.motor2PositionSetpoint = pos2;
}

void motorControlLogic()
{
    // Snapshot only the scalar control inputs in a short masked window.
    // Copying the whole ~72-byte DCMT_SLICE here (~20-36 us with interrupts
    // off, every iteration) held off TWI ISR entry at the SLA+R boundary; the
    // resulting clock stretch is mishandled by Linux SBC I2C controllers and
    // corrupted ~30% of CRUMBS state queries (#3). Keep every cli window in
    // this loop under ~10 us. PID tunings are fetched in a separate short
    // window only when a closed-loop mode needs them; a torn view across the
    // two windows self-corrects next iteration.
    DCMT_SLICE local;
    bool wdTrippedNow;
    noInterrupts();
    local.mode = slice.mode;
    local.eStop = slice.eStop;
    local.motor1Brake = slice.motor1Brake;
    local.motor2Brake = slice.motor2Brake;
    local.motor1PWM = slice.motor1PWM;
    local.motor2PWM = slice.motor2PWM;
    local.motor1PositionSetpoint = slice.motor1PositionSetpoint;
    local.motor2PositionSetpoint = slice.motor2PositionSetpoint;
    local.motor1SpeedSetpoint = slice.motor1SpeedSetpoint;
    local.motor2SpeedSetpoint = slice.motor2SpeedSetpoint;
    wdTrippedNow = wdTripped;
    interrupts();

    // A tripped command watchdog holds the same safe state as a signal-wired
    // e-stop until an explicit clear releases the trip — mirrors RLHT's
    // relayControlLogic, which guards `slice.eStop || wdTripped`. Without the
    // wdTripped half, the OPEN_LOOP write(0) / CLOSED_LOOP servo.run() below
    // release the brake that watchdogLogic() engaged, so brake+PWM toggle every
    // loop: an audible driver whine and a soft oscillating brake instead of a
    // clean hard-brake on a driven motor (feastorg/Slice_DCMT#11).
    if (local.eStop || wdTrippedNow)
    {
        // brake() only — see the watchdog hold note; this branch also runs
        // every iteration while a signal-wired e-stop or watchdog trip is held.
        stop_control_loops();
        motor1_brake();
        motor2_brake();

        // The LMD18200 brake is dynamic, so a shaft keeps coasting after the
        // trip or press synced the setpoints. Re-sync them to the encoders
        // every loop of the hold, so a brake release after the clear or
        // release holds where the shaft stopped instead of driving it back
        // by the coast distance, and publish the positions, which the mode
        // branches below would otherwise leave frozen for GET_STATE. This
        // runs here, not in watchdogLogic(), which returns early once WDOG=0
        // disarms during a trip. Read before the masked window:
        // Encoder::read() re-enables interrupts.
        //
        // The release then starts the PID cleanly only because
        // stop_control_loops() -> DCMotorServo::stop()/haltMotor() zeroes
        // _PID_output (DCMotorServo 1.0.1 and 1.1.0). Without that, the
        // first run() after a release lunges on the stale output. Check it
        // again on a DCMotorServo bump.
        const int16_t h1 = clamp_i16(servo1.getActualPosition());
        const int16_t h2 = clamp_i16(servo2.getActualPosition());
        noInterrupts();
        if (wdTripped || slice.eStop)
        {
            slice.motor1PositionSetpoint = h1;
            slice.motor2PositionSetpoint = h2;
        }
        slice.motor1Position = h1;
        slice.motor2Position = h2;
        interrupts();
        return;
    }

    apply_mode_transition(local);

    if (local.mode == OPEN_LOOP)
    {
        if (local.motor1Brake)
        {
            motor1_brake();
        }
        else
        {
            motor1_write(clamp_pwm(local.motor1PWM));
        }

        if (local.motor2Brake)
        {
            motor2_brake();
        }
        else
        {
            motor2_write(clamp_pwm(local.motor2PWM));
        }

        local.motor1Position = clamp_i16(servo1.getActualPosition());
        local.motor2Position = clamp_i16(servo2.getActualPosition());
        local.motor1Speed = 0;
        local.motor2Speed = 0;

        // Commit outputs atomically (reply_get_state reads from slice in ISR).
        // motor1PWM/motor2PWM are handler-owned inputs — only zero them when braking.
        noInterrupts();
        if (local.motor1Brake) slice.motor1PWM = 0;
        if (local.motor2Brake) slice.motor2PWM = 0;
        follow_held_setpoints(local.motor1Position, local.motor2Position);
        slice.motor1Position = local.motor1Position;
        slice.motor2Position = local.motor2Position;
        slice.motor1Speed = local.motor1Speed;
        slice.motor2Speed = local.motor2Speed;
        interrupts();
        return;
    }

    if (local.mode == CLOSED_LOOP_POSITION)
    {
        noInterrupts();
        local.posPid1 = slice.posPid1;
        local.posPid2 = slice.posPid2;
        interrupts();
        apply_position_pid_if_needed(local);

        if (local.motor1Brake)
        {
            servo1.stop();
            motor1_brake();
        }
        else
        {
            // DCMotorServo::moveTo() is idempotent for same-value calls (setpoint assign only).
            servo1.moveTo(local.motor1PositionSetpoint);
            servo1.run();
        }

        if (local.motor2Brake)
        {
            servo2.stop();
            motor2_brake();
        }
        else
        {
            servo2.moveTo(local.motor2PositionSetpoint);
            servo2.run();
        }

        local.motor1Position = clamp_i16(servo1.getActualPosition());
        local.motor2Position = clamp_i16(servo2.getActualPosition());
        local.motor1Speed = 0;
        local.motor2Speed = 0;

        noInterrupts();
        if (local.motor1Brake) slice.motor1PWM = 0;
        if (local.motor2Brake) slice.motor2PWM = 0;
        follow_held_setpoints(local.motor1Position, local.motor2Position);
        slice.motor1Position = local.motor1Position;
        slice.motor2Position = local.motor2Position;
        slice.motor1Speed = local.motor1Speed;
        slice.motor2Speed = local.motor2Speed;
        interrupts();
        return;
    }

#if DCMT_ENABLE_SPEED_LOOP
    if (local.mode == CLOSED_LOOP_SPEED)
    {
        noInterrupts();
        local.speedPid1 = slice.speedPid1;
        local.speedPid2 = slice.speedPid2;
        interrupts();
        apply_speed_pid_if_needed(local);

        if (local.motor1Brake)
        {
            tacho1.stop();
            motor1_brake();
            local.motor1Speed = 0;
        }
        else
        {
            tacho1.setSpeedRPM(local.motor1SpeedSetpoint);
            tacho1.run();
            local.motor1Speed = clamp_i16((long)tacho1.getMeasuredSpeedRPM());
        }

        if (local.motor2Brake)
        {
            tacho2.stop();
            motor2_brake();
            local.motor2Speed = 0;
        }
        else
        {
            tacho2.setSpeedRPM(local.motor2SpeedSetpoint);
            tacho2.run();
            local.motor2Speed = clamp_i16((long)tacho2.getMeasuredSpeedRPM());
        }

        local.motor1Position = clamp_i16(servo1.getActualPosition());
        local.motor2Position = clamp_i16(servo2.getActualPosition());

        noInterrupts();
        if (local.motor1Brake) slice.motor1PWM = 0;
        if (local.motor2Brake) slice.motor2PWM = 0;
        follow_held_setpoints(local.motor1Position, local.motor2Position);
        slice.motor1Position = local.motor1Position;
        slice.motor2Position = local.motor2Position;
        slice.motor1Speed = local.motor1Speed;
        slice.motor2Speed = local.motor2Speed;
        interrupts();
        return;
    }
#endif

    // Unknown mode: fail safe to open-loop with brakes.
    noInterrupts();
    slice.mode = OPEN_LOOP;
    slice.motor1Brake = true;
    slice.motor2Brake = true;
    interrupts();
    SLICE_DEBUG_PRINTLN(F("ERROR: Unknown mode, forcing OPEN_LOOP with brakes."));
}
