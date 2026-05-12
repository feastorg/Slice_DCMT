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
#ifdef __AVR__
#include <avr/wdt.h>
#elif defined(ARDUINO_ARCH_MEGAAVR)
#include <avr/wdt.h>
#endif

// ---- CRUMBS context ----
static crumbs_context_t ctx;

// ---- Global flags/objects ----
volatile bool estopTriggered = false;
CRGB led;

static const unsigned long ESTOP_DEBOUNCE_MS = 25;
static bool estopDebouncePending = false;
static unsigned long estopDebounceStartMs = 0;

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

// ---- Hardware adapter wrappers for DCMotorServo ----
static void motor1_write(int16_t speed)
{
    motor1Driver.write(speed);
}

static void motor1_brake(void)
{
    motor1Driver.brake();
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
}

static void motor2_brake(void)
{
    motor2Driver.brake();
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

static void apply_position_pid_if_needed(void)
{
    if (positionPidApplied &&
        pid_tunings_equal(appliedPosPid1, slice.posPid1) &&
        pid_tunings_equal(appliedPosPid2, slice.posPid2))
    {
        return;
    }

    servo1.setPIDTunings(slice.posPid1.kp, slice.posPid1.ki, slice.posPid1.kd);
    servo2.setPIDTunings(slice.posPid2.kp, slice.posPid2.ki, slice.posPid2.kd);

    appliedPosPid1 = slice.posPid1;
    appliedPosPid2 = slice.posPid2;
    positionPidApplied = true;
}

#if DCMT_ENABLE_SPEED_LOOP
static void apply_speed_pid_if_needed(void)
{
    if (speedPidApplied &&
        pid_tunings_equal(appliedSpeedPid1, slice.speedPid1) &&
        pid_tunings_equal(appliedSpeedPid2, slice.speedPid2))
    {
        return;
    }

    tacho1.setPIDTunings(slice.speedPid1.kp, slice.speedPid1.ki, slice.speedPid1.kd);
    tacho2.setPIDTunings(slice.speedPid2.kp, slice.speedPid2.ki, slice.speedPid2.kd);

    appliedSpeedPid1 = slice.speedPid1;
    appliedSpeedPid2 = slice.speedPid2;
    speedPidApplied = true;
}
#endif

static void apply_mode_transition(void)
{
    if (slice.mode == appliedMode)
        return;

    stop_control_loops();

    if (slice.mode == CLOSED_LOOP_POSITION)
    {
        // Preserve controller-provided setpoints across mode transitions.
        servo1.moveTo(slice.motor1PositionSetpoint);
        servo2.moveTo(slice.motor2PositionSetpoint);
    }
#if DCMT_ENABLE_SPEED_LOOP
    else if (slice.mode == CLOSED_LOOP_SPEED)
    {
        // Preserve controller-provided speed setpoints across mode transitions.
        tacho1.setSpeedRPM(slice.motor1SpeedSetpoint);
        tacho2.setSpeedRPM(slice.motor2SpeedSetpoint);
    }
#endif

    appliedMode = slice.mode;
}

void setup()
{
    wdt_disable();
    setupSlice();
    setupDCMT();
    delay(1000);
    wdt_enable(WDTO_1S);
}

void loop()
{
    wdt_reset();
    pollEStop();
    motorControlLogic();
    serialCommands();
    printSerialOutput();
}

// ---- Implementation (previously in .ino files) ----

void setupSlice()
{
    int rc;
    Serial.begin(115200);

    // CRUMBS
    crumbs_arduino_init_peripheral(&ctx, I2C_ADR);

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

    rc = crumbs_register_reply_handler(&ctx, 0x00, reply_version, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register version reply handler"));

    rc = crumbs_register_reply_handler(&ctx, DCMT_OP_GET_STATE, reply_get_state, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register DCMT_OP_GET_STATE reply handler"));

    rc = crumbs_register_reply_handler(&ctx, BREAD_OP_GET_CAPS, reply_get_caps, nullptr);
    if (rc != 0)
        SLICE_DEBUG_PRINTLN(F("CRUMBS: Failed to register BREAD_OP_GET_CAPS reply handler"));

    // LED
    FastLED.addLeds<NEOPIXEL, LED_PIN>(&led, 1);
    FastLED.setBrightness(50);
    led = CRGB::Blue;
    FastLED.show();

    // e-stop: no external bias resistor on current boards, so use internal pull-up.
    pinMode(ESTOP, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(ESTOP), estopISR, CHANGE);
    estopTriggered = true; // Force initial debounced state sync after boot.

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
    motor1Driver.write(0);
    motor2Driver.write(0);

    apply_position_pid_if_needed();
    servo1.setPWMSkip(DCMT_SERVO_PWM_SKIP);
    servo2.setPWMSkip(DCMT_SERVO_PWM_SKIP);
    servo1.setMaxPWM(DCMT_SERVO_MAX_PWM);
    servo2.setMaxPWM(DCMT_SERVO_MAX_PWM);
    servo1.setAccuracy(DCMT_SERVO_ACCURACY);
    servo2.setAccuracy(DCMT_SERVO_ACCURACY);

    servo1.setCurrentPosition(motor1Encoder.read());
    servo2.setCurrentPosition(motor2Encoder.read());

#if DCMT_ENABLE_SPEED_LOOP
    apply_speed_pid_if_needed();
    tacho1.setSpeedRPM(0);
    tacho2.setSpeedRPM(0);
#endif

    slice.motor1Position = clamp_i16(servo1.getActualPosition());
    slice.motor2Position = clamp_i16(servo2.getActualPosition());

    timing.lastSerialPrint = millis();
    appliedMode = slice.mode;
}

// Poll/ISR/processing
void pollEStop()
{
    if (estopTriggered)
    {
        estopTriggered = false;
        estopDebouncePending = true;
        estopDebounceStartMs = millis();
    }

    if (estopDebouncePending && (millis() - estopDebounceStartMs >= ESTOP_DEBOUNCE_MS))
    {
        processEStop();
        estopDebouncePending = false;
    }

    {
        static CRGB lastLed = CRGB::Black;
        CRGB next = slice.eStop ? CRGB::Red : CRGB::Green;
        if (next != lastLed)
        {
            led = next;
            FastLED.show();
            lastLed = next;
        }
    }
}

void estopISR()
{
    estopTriggered = true;
}

void processEStop()
{
    // Internal pull-up means asserted e-stop pulls the line LOW.
    if (digitalRead(ESTOP) == LOW)
    {
        stop_control_loops();
        motor1Driver.brake();
        motor2Driver.brake();

        slice.eStop = true;
        slice.motor1PWM = 0;
        slice.motor2PWM = 0;
        slice.motor1SpeedSetpoint = 0;
        slice.motor2SpeedSetpoint = 0;
        SLICE_DEBUG_PRINTLN(F("ESTOP PRESSED!"));
    }
    else
    {
        slice.eStop = false;
        SLICE_DEBUG_PRINTLN(F("ESTOP RELEASED!"));
    }
}

void motorControlLogic()
{
    if (slice.eStop)
    {
        stop_control_loops();
        motor1Driver.write(0);
        motor2Driver.write(0);
        motor1Driver.brake();
        motor2Driver.brake();
        return;
    }

    apply_mode_transition();

    if (slice.mode == OPEN_LOOP)
    {
        if (slice.motor1Brake)
        {
            slice.motor1PWM = 0;
            motor1Driver.write(0);
            motor1Driver.brake();
        }
        else
        {
            motor1Driver.write(clamp_pwm(slice.motor1PWM));
        }

        if (slice.motor2Brake)
        {
            slice.motor2PWM = 0;
            motor2Driver.write(0);
            motor2Driver.brake();
        }
        else
        {
            motor2Driver.write(clamp_pwm(slice.motor2PWM));
        }

        slice.motor1Position = clamp_i16(servo1.getActualPosition());
        slice.motor2Position = clamp_i16(servo2.getActualPosition());
        slice.motor1Speed = 0;
        slice.motor2Speed = 0;
        return;
    }

    if (slice.mode == CLOSED_LOOP_POSITION)
    {
        apply_position_pid_if_needed();

        if (slice.motor1Brake)
        {
            slice.motor1PWM = 0;
            servo1.stop();
            motor1Driver.brake();
        }
        else
        {
            // DCMotorServo::moveTo() is idempotent for same-value calls (setpoint assign only).
            servo1.moveTo(slice.motor1PositionSetpoint);
            servo1.run();
        }

        if (slice.motor2Brake)
        {
            slice.motor2PWM = 0;
            servo2.stop();
            motor2Driver.brake();
        }
        else
        {
            servo2.moveTo(slice.motor2PositionSetpoint);
            servo2.run();
        }

        slice.motor1Position = clamp_i16(servo1.getActualPosition());
        slice.motor2Position = clamp_i16(servo2.getActualPosition());
        slice.motor1Speed = 0;
        slice.motor2Speed = 0;
        return;
    }

#if DCMT_ENABLE_SPEED_LOOP
    if (slice.mode == CLOSED_LOOP_SPEED)
    {
        apply_speed_pid_if_needed();

        if (slice.motor1Brake)
        {
            slice.motor1PWM = 0;
            tacho1.stop();
            motor1Driver.brake();
            slice.motor1Speed = 0;
        }
        else
        {
            tacho1.setSpeedRPM(slice.motor1SpeedSetpoint);
            tacho1.run();
            slice.motor1Speed = clamp_i16((long)tacho1.getMeasuredSpeedRPM());
        }

        if (slice.motor2Brake)
        {
            slice.motor2PWM = 0;
            tacho2.stop();
            motor2Driver.brake();
            slice.motor2Speed = 0;
        }
        else
        {
            tacho2.setSpeedRPM(slice.motor2SpeedSetpoint);
            tacho2.run();
            slice.motor2Speed = clamp_i16((long)tacho2.getMeasuredSpeedRPM());
        }

        slice.motor1Position = clamp_i16(servo1.getActualPosition());
        slice.motor2Position = clamp_i16(servo2.getActualPosition());
        return;
    }
#endif

    // Unknown mode: fail safe to open-loop with brakes.
    slice.mode = OPEN_LOOP;
    slice.motor1Brake = true;
    slice.motor2Brake = true;
    SLICE_DEBUG_PRINTLN(F("ERROR: Unknown mode, forcing OPEN_LOOP with brakes."));
}
