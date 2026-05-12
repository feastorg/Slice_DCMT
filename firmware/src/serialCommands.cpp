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

    if (starts_with_P(cmd, PSTR("MODE=")))
    {
        char *mode = (char *)after_prefix_P(cmd, PSTR("MODE="));
        while (*mode == ' ')
            ++mode;
        strupper(mode);

        if (strcmp_P(mode, PSTR("OPEN")) == 0)
            slice.mode = OPEN_LOOP;
        else if (strcmp_P(mode, PSTR("POS")) == 0)
            slice.mode = CLOSED_LOOP_POSITION;
#if DCMT_ENABLE_SPEED_LOOP
        else if (strcmp_P(mode, PSTR("SPEED")) == 0)
            slice.mode = CLOSED_LOOP_SPEED;
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
        if (slice.mode != OPEN_LOOP)
        {
            Serial.println(F("M1PWM only valid in OPEN mode"));
            return;
        }

        long v = atol(after_prefix_P(cmd, PSTR("M1PWM=")));
        if (v < -255)
            v = -255;
        if (v > 255)
            v = 255;
        slice.motor1PWM = (int16_t)v;
        Serial.print(F("M1PWM-> "));
        Serial.println(slice.motor1PWM);
    }
    else if (starts_with_P(cmd, PSTR("M2PWM=")))
    {
        if (slice.mode != OPEN_LOOP)
        {
            Serial.println(F("M2PWM only valid in OPEN mode"));
            return;
        }

        long v = atol(after_prefix_P(cmd, PSTR("M2PWM=")));
        if (v < -255)
            v = -255;
        if (v > 255)
            v = 255;
        slice.motor2PWM = (int16_t)v;
        Serial.print(F("M2PWM-> "));
        Serial.println(slice.motor2PWM);
    }
    else if (starts_with_P(cmd, PSTR("M1POS=")))
    {
        slice.motor1PositionSetpoint = (int16_t)atol(after_prefix_P(cmd, PSTR("M1POS=")));
        Serial.print(F("M1POS-> "));
        Serial.println(slice.motor1PositionSetpoint);
    }
    else if (starts_with_P(cmd, PSTR("M2POS=")))
    {
        slice.motor2PositionSetpoint = (int16_t)atol(after_prefix_P(cmd, PSTR("M2POS=")));
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
        slice.posPid1 = {kp, ki, kd};
        slice.posPid2 = {kp, ki, kd};
        Serial.println(F("Position PID updated"));
    }
#if DCMT_ENABLE_SPEED_LOOP
    else if (starts_with_P(cmd, PSTR("M1SPEED=")))
    {
        slice.motor1SpeedSetpoint = (int16_t)atol(after_prefix_P(cmd, PSTR("M1SPEED=")));
        Serial.print(F("M1SPEED-> "));
        Serial.println(slice.motor1SpeedSetpoint);
    }
    else if (starts_with_P(cmd, PSTR("M2SPEED=")))
    {
        slice.motor2SpeedSetpoint = (int16_t)atol(after_prefix_P(cmd, PSTR("M2SPEED=")));
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
        slice.speedPid1 = {kp, ki, kd};
        slice.speedPid2 = {kp, ki, kd};
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
        slice.motor1Brake = false;
        Serial.println(F("Motor1 brake released"));
    }
    else if (strcmp_P(cmd, PSTR("BRAKE2=1")) == 0)
    {
        slice.motor2Brake = true;
        Serial.println(F("Motor2 brake engaged"));
    }
    else if (strcmp_P(cmd, PSTR("BRAKE2=0")) == 0)
    {
        slice.motor2Brake = false;
        Serial.println(F("Motor2 brake released"));
    }
    else if (strcmp_P(cmd, PSTR("READ")) == 0)
    {
        printSliceState(Serial);
    }
    else
    {
        Serial.println(F("Invalid command."));
        Serial.println(F("Open/Pos: MODE=OPEN|POS, M1PWM=, M2PWM=, M1POS=, M2POS=, PIDPOS=kp,ki,kd, BRAKE1=0/1, BRAKE2=0/1, READ"));
#if DCMT_ENABLE_SPEED_LOOP
        Serial.println(F("Closed-loop: MODE=POS|SPEED, M1POS=, M2POS=, M1SPEED=, M2SPEED=, PIDPOS=kp,ki,kd, PIDSPEED=kp,ki,kd"));
#endif
    }
}

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
        }
        else if (cmdIdx < sizeof(cmdBuf) - 1)
        {
            cmdBuf[cmdIdx++] = c;
        }
        // else: silently discard overflow characters until newline.
    }
}
