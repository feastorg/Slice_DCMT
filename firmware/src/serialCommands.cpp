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

static bool parse_pid_triplet(const char *s, float &kp, float &ki, float &kd)
{
    char *end1;
    char *end2;
    char *end3;
    kp = strtod(s, &end1);
    if (*end1 != ',')
        return false;
    ki = strtod(end1 + 1, &end2);
    if (*end2 != ',')
        return false;
    kd = strtod(end2 + 1, &end3);
    if (end3 == end2 + 1)
        return false;
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

    // A serial operator is a live master too: feed the command watchdog.
    // Ordinary input (READ included) does not clear a latched trip, so a
    // logger polling READ cannot release a hold; only WDCLEAR does.
    noInterrupts();
    wdLastRxMs = millis();
    interrupts();

    // While an e-stop or a watchdog trip is held, commands that could make a
    // motor move are refused, as over I2C (dcmt_handlers.cpp): releasing the
    // e-stop or clearing the trip resumes nothing. Engaging a brake, PID
    // tuning, WDOG=, WDCLEAR and READ stay available.
    if ((slice.eStop || wdTripped) &&
        (starts_with_P(cmd, PSTR("MODE=")) ||
         starts_with_P(cmd, PSTR("M1PWM=")) || starts_with_P(cmd, PSTR("M2PWM=")) ||
         starts_with_P(cmd, PSTR("M1POS=")) || starts_with_P(cmd, PSTR("M2POS=")) ||
         starts_with_P(cmd, PSTR("M1SPEED=")) || starts_with_P(cmd, PSTR("M2SPEED=")) ||
         strcmp_P(cmd, PSTR("BRAKE1=0")) == 0 || strcmp_P(cmd, PSTR("BRAKE2=0")) == 0))
    {
        if (slice.eStop)
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
        long v = atol(after_prefix_P(cmd, PSTR("M1PWM=")));
        if (v < -255)
            v = -255;
        if (v > 255)
            v = 255;

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
        long v = atol(after_prefix_P(cmd, PSTR("M2PWM=")));
        if (v < -255)
            v = -255;
        if (v > 255)
            v = 255;

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
        const int16_t sp = (int16_t)atol(after_prefix_P(cmd, PSTR("M1POS=")));
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
        const int16_t sp = (int16_t)atol(after_prefix_P(cmd, PSTR("M2POS=")));
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
            Serial.println(F("PIDPOS format: PIDPOS=kp,ki,kd"));
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
        const int16_t sp = (int16_t)atol(after_prefix_P(cmd, PSTR("M1SPEED=")));
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
        const int16_t sp = (int16_t)atol(after_prefix_P(cmd, PSTR("M2SPEED=")));
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
            Serial.println(F("PIDSPEED format: PIDSPEED=kp,ki,kd"));
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
        long v = atol(after_prefix_P(cmd, PSTR("WDOG=")));
        if (v < 0)
            v = 0;
        if (v > 65535)
            v = 65535;
        noInterrupts();
        wdTimeoutMs = (uint16_t)v;
        wdLastRxMs = millis();
        interrupts();
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
        noInterrupts();
        wdTripped = false;
        interrupts();
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
