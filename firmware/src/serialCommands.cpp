// src/serialCommands.cpp
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>
#include "globals.h"

// Static receive buffer — no heap allocation.
static char cmdBuf[64];
static uint8_t cmdIdx = 0;

static bool starts_with_P(const char *s, PGM_P prefix)
{
    return strncmp_P(s, prefix, strlen_P(prefix)) == 0;
}

static const char *after_prefix_P(const char *s, PGM_P prefix)
{
    return s + strlen_P(prefix);
}

// Numbers are parsed strictly (#60, bread-infra spec/slice/firmware.md):
// the whole token must parse and be in range for its field, or the command
// is refused with no change.
static bool parse_arg(const char *s, long lo, long hi, long &out)
{
    if (bsc::parseLong(s, lo, hi, &out))
        return true;
    Serial.print(F("Ignored: expected a whole number from "));
    Serial.print(lo);
    Serial.print(F(" to "));
    Serial.println(hi);
    return false;
}

// Exactly three gains, kp,ki,kd, each finite and in SET_PID's range (u16
// x1000 on the wire: 0 to 65.535).
static bool parse_pid_triplet(const char *s, float &kp, float &ki, float &kd)
{
    char buf[sizeof cmdBuf];
    if (strlen(s) >= sizeof buf)
        return false;
    strcpy(buf, s);
    char *field[3];
    uint8_t n = 0;
    char *p = buf;
    for (;;)
    {
        if (n == 3)
            return false;
        field[n++] = p;
        char *comma = strchr(p, ',');
        if (comma == nullptr)
            break;
        *comma = '\0';
        p = comma + 1;
    }
    if (n != 3)
        return false;
    float v[3];
    for (uint8_t i = 0; i < 3; i++)
        if (!bsc::parseFloat(field[i], 0.0f, 65.535f, &v[i]))
            return false;
    kp = v[0];
    ki = v[1];
    kd = v[2];
    return true;
}

static void strupper(char *s)
{
    for (; *s; ++s)
        *s = toupper((unsigned char)*s);
}

static void processCommand(char *cmd)
{
    // Trim leading/trailing whitespace in-place.
    while (*cmd == ' ' || *cmd == '\t')
        ++cmd;
    if (*cmd == '\0')
        return;
    char *end = cmd + strlen(cmd) - 1;
    while (end > cmd && (*end == ' ' || *end == '\t' || *end == '\r'))
        *end-- = '\0';

    if (*cmd == '\0')
        return;

    // A serial operator is a live master too: feed the command watchdog,
    // unless a commands-only lease is set. Ordinary input (READ included)
    // does not clear a latched trip, so a logger polling READ cannot release
    // a hold; only WDCLEAR does.
    livenessStamp();

    // While an e-stop or a watchdog trip is held, commands that could make a
    // motor move are refused, as over I2C (dcmt_handlers.cpp): releasing the
    // e-stop or clearing the trip resumes nothing. Engaging a brake, PID
    // tuning, WDOG=, WDCLEAR and READ stay available.
    if ((estopInput.asserted() || commandWatchdog.tripped()) &&
        (starts_with_P(cmd, PSTR("MODE=")) ||
         starts_with_P(cmd, PSTR("M1PWM=")) || starts_with_P(cmd, PSTR("M2PWM=")) ||
         starts_with_P(cmd, PSTR("M1POS=")) || starts_with_P(cmd, PSTR("M2POS=")) ||
         starts_with_P(cmd, PSTR("M1SPEED=")) || starts_with_P(cmd, PSTR("M2SPEED=")) ||
         strcmp_P(cmd, PSTR("BRAKE1=0")) == 0 || strcmp_P(cmd, PSTR("BRAKE2=0")) == 0))
    {
        if (estopInput.asserted())
            Serial.println(F("Ignored: e-stop held; release it, then release the brakes"));
        else
            Serial.println(F("Ignored: watchdog trip held; send WDCLEAR, then release the brakes"));
        return;
    }

    if (starts_with_P(cmd, PSTR("MODE=")))
    {
        char *mode = (char *)after_prefix_P(cmd, PSTR("MODE="));
        while (*mode == ' ')
            ++mode;
        strupper(mode);

        ControlModes next;
        if (strcmp_P(mode, PSTR("OPEN")) == 0)
            next = OPEN_LOOP;
        else if (strcmp_P(mode, PSTR("POS")) == 0)
            next = CLOSED_LOOP_POSITION;
#if DCMT_ENABLE_SPEED_LOOP
        else if (strcmp_P(mode, PSTR("SPEED")) == 0)
            next = CLOSED_LOOP_SPEED;
#endif
        else
        {
#if DCMT_ENABLE_SPEED_LOOP
            Serial.println(F("MODE options: OPEN | POS | SPEED"));
#else
            Serial.println(F("MODE options: OPEN | POS"));
#endif
            return;
        }

        // As SET_MODE does (dcmt_handlers.cpp): leaving open loop clears the
        // open-loop PWM, so a later return to open loop starts from 0.
        noInterrupts();
        slice.mode = next;
        if (next != OPEN_LOOP)
        {
            slice.motor1PWM = 0;
            slice.motor2PWM = 0;
        }
        interrupts();

        Serial.print(F("Mode-> "));
        if (slice.mode == OPEN_LOOP)
            Serial.println(F("OPEN_LOOP"));
        else if (slice.mode == CLOSED_LOOP_POSITION)
            Serial.println(F("CLOSED_LOOP_POSITION"));
#if DCMT_ENABLE_SPEED_LOOP
        else
            Serial.println(F("CLOSED_LOOP_SPEED"));
#endif
    }
    else if (starts_with_P(cmd, PSTR("M1PWM=")))
    {
        long v;
        if (!parse_arg(after_prefix_P(cmd, PSTR("M1PWM=")), -255, 255, v))
            return;

        // The mode check and the write share one masked window: an I2C
        // SET_MODE between them would zero the PWM, then this would store
        // one in closed loop. The I2C handlers write the same field.
        noInterrupts();
        const bool open = slice.mode == OPEN_LOOP;
        if (open)
            slice.motor1PWM = (int16_t)v;
        interrupts();
        if (!open)
        {
            Serial.println(F("M1PWM only valid in OPEN mode"));
            return;
        }
        Serial.print(F("M1PWM-> "));
        Serial.println((int16_t)v);
    }
    else if (starts_with_P(cmd, PSTR("M2PWM=")))
    {
        long v;
        if (!parse_arg(after_prefix_P(cmd, PSTR("M2PWM=")), -255, 255, v))
            return;

        noInterrupts();
        const bool open = slice.mode == OPEN_LOOP;
        if (open)
            slice.motor2PWM = (int16_t)v;
        interrupts();
        if (!open)
        {
            Serial.println(F("M2PWM only valid in OPEN mode"));
            return;
        }
        Serial.print(F("M2PWM-> "));
        Serial.println((int16_t)v);
    }
    else if (starts_with_P(cmd, PSTR("M1POS=")))
    {
        // -32768 is GET_STATE's invalid sentinel.
        long v;
        if (!parse_arg(after_prefix_P(cmd, PSTR("M1POS=")), -32767, 32767, v))
            return;
        const int16_t sp = (int16_t)v;
        // A new setpoint ends this motor's post-hold follow.
        noInterrupts();
        slice.motor1PositionSetpoint = sp;
        holdResyncMask &= (uint8_t)~HOLD_RESYNC_M1;
        interrupts();
        Serial.print(F("M1POS-> "));
        Serial.println(slice.motor1PositionSetpoint);
    }
    else if (starts_with_P(cmd, PSTR("M2POS=")))
    {
        // -32768 is GET_STATE's invalid sentinel.
        long v;
        if (!parse_arg(after_prefix_P(cmd, PSTR("M2POS=")), -32767, 32767, v))
            return;
        const int16_t sp = (int16_t)v;
        // A new setpoint ends this motor's post-hold follow.
        noInterrupts();
        slice.motor2PositionSetpoint = sp;
        holdResyncMask &= (uint8_t)~HOLD_RESYNC_M2;
        interrupts();
        Serial.print(F("M2POS-> "));
        Serial.println(slice.motor2PositionSetpoint);
    }
    else if (starts_with_P(cmd, PSTR("PIDPOS=")))
    {
        float kp = 0.0f;
        float ki = 0.0f;
        float kd = 0.0f;
        if (!parse_pid_triplet(after_prefix_P(cmd, PSTR("PIDPOS=")), kp, ki, kd))
        {
            Serial.println(F("PIDPOS format: PIDPOS=kp,ki,kd, each 0 to 65.535"));
            return;
        }
        // SET_PID writes these from the I2C interrupt too; mask so neither
        // motor ends up with gains from two different commands.
        noInterrupts();
        slice.posPid1 = {kp, ki, kd};
        slice.posPid2 = {kp, ki, kd};
        interrupts();
        Serial.println(F("Position PID updated"));
    }
#if DCMT_ENABLE_SPEED_LOOP
    else if (starts_with_P(cmd, PSTR("M1SPEED=")))
    {
        // -32768 is GET_STATE's invalid sentinel.
        long v;
        if (!parse_arg(after_prefix_P(cmd, PSTR("M1SPEED=")), -32767, 32767, v))
            return;
        const int16_t sp = (int16_t)v;
        // A new setpoint ends this motor's post-hold follow.
        noInterrupts();
        slice.motor1SpeedSetpoint = sp;
        holdResyncMask &= (uint8_t)~HOLD_RESYNC_M1;
        interrupts();
        Serial.print(F("M1SPEED-> "));
        Serial.println(slice.motor1SpeedSetpoint);
    }
    else if (starts_with_P(cmd, PSTR("M2SPEED=")))
    {
        // -32768 is GET_STATE's invalid sentinel.
        long v;
        if (!parse_arg(after_prefix_P(cmd, PSTR("M2SPEED=")), -32767, 32767, v))
            return;
        const int16_t sp = (int16_t)v;
        // A new setpoint ends this motor's post-hold follow.
        noInterrupts();
        slice.motor2SpeedSetpoint = sp;
        holdResyncMask &= (uint8_t)~HOLD_RESYNC_M2;
        interrupts();
        Serial.print(F("M2SPEED-> "));
        Serial.println(slice.motor2SpeedSetpoint);
    }
    else if (starts_with_P(cmd, PSTR("PIDSPEED=")))
    {
        float kp = 0.0f;
        float ki = 0.0f;
        float kd = 0.0f;
        if (!parse_pid_triplet(after_prefix_P(cmd, PSTR("PIDSPEED=")), kp, ki, kd))
        {
            Serial.println(F("PIDSPEED format: PIDSPEED=kp,ki,kd, each 0 to 65.535"));
            return;
        }
        noInterrupts();
        slice.speedPid1 = {kp, ki, kd};
        slice.speedPid2 = {kp, ki, kd};
        interrupts();
        Serial.println(F("Speed PID updated"));
    }
#endif
    else if (strcmp_P(cmd, PSTR("BRAKE1=1")) == 0)
    {
        slice.motor1Brake = true;
        Serial.println(F("Motor1 brake engaged"));
    }
    else if (strcmp_P(cmd, PSTR("BRAKE1=0")) == 0)
    {
        noInterrupts();
        slice.motor1Brake = false;
        holdResyncMask &= (uint8_t)~HOLD_RESYNC_M1;
        interrupts();
        Serial.println(F("Motor1 brake released"));
    }
    else if (strcmp_P(cmd, PSTR("BRAKE2=1")) == 0)
    {
        slice.motor2Brake = true;
        Serial.println(F("Motor2 brake engaged"));
    }
    else if (strcmp_P(cmd, PSTR("BRAKE2=0")) == 0)
    {
        noInterrupts();
        slice.motor2Brake = false;
        holdResyncMask &= (uint8_t)~HOLD_RESYNC_M2;
        interrupts();
        Serial.println(F("Motor2 brake released"));
    }
    else if (starts_with_P(cmd, PSTR("WDOG=")))
    {
        long v;
        if (!parse_arg(after_prefix_P(cmd, PSTR("WDOG=")), 0, 65535, v))
            return;
        // Ends a commands-only lease, as the 2-byte SET_WATCHDOG does.
        watchdogCommandsOnly = false;
        commandWatchdog.arm((uint16_t)v, millis());
        Serial.print(F("WDOG-> "));
        if (v == 0)
            Serial.println(F("disarmed"));
        else
        {
            Serial.print(v);
            Serial.println(F(" ms"));
        }
    }
    else if (strcmp_P(cmd, PSTR("WDCLEAR")) == 0)
    {
        // Serial counterpart of BREAD_OP_CLEAR_WATCHDOG_TRIP: clears the trip
        // and nothing else (timeout, armed state and trip count unchanged).
        commandWatchdog.clear();
        Serial.println(F("WDOG trip cleared"));
    }
    else if (strcmp_P(cmd, PSTR("READ")) == 0)
    {
        printSliceState(Serial);
    }
    else
    {
        Serial.println(F("Invalid command."));
        Serial.println(F("Open/Pos: MODE=OPEN|POS, M1PWM=, M2PWM=, M1POS=, M2POS=, PIDPOS=kp,ki,kd, BRAKE1=0/1, BRAKE2=0/1, WDOG=ms(0=off), WDCLEAR, READ"));
#if DCMT_ENABLE_SPEED_LOOP
        Serial.println(F("Closed-loop: MODE=POS|SPEED, M1POS=, M2POS=, M1SPEED=, M2SPEED=, PIDPOS=kp,ki,kd, PIDSPEED=kp,ki,kd"));
#endif
    }
}

// Handles at most one complete line per call (#49). Each reply blocks once
// the transmit buffer fills, and the e-stop is sampled once per loop pass,
// so draining a burst here would hold the e-stop sample for as long as the
// burst takes to answer. The rest of the input waits for the next pass.
void serialCommands()
{
    while (Serial.available())
    {
        char c = (char)Serial.read();
        if (c == '\n')
        {
            cmdBuf[cmdIdx] = '\0';
            processCommand(cmdBuf);
            cmdIdx = 0;
            return;
        }
        else if (cmdIdx < sizeof(cmdBuf) - 1)
        {
            cmdBuf[cmdIdx++] = c;
        }
        // else: silently discard overflow characters until newline.
    }
}
