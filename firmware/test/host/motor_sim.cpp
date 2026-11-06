#include "motor_sim.h"
#include "host.h"
#include "globals.h"
#include <Encoder.h>
#include <LMD18200.h>
#include <bread/dcmt_ops.h>
#include <algorithm>
#include <math.h>

void setup();
void loop();

// Defined in main.cpp.
extern LMD18200 motor1Driver;
extern LMD18200 motor2Driver;
extern Encoder motor1Encoder;
extern Encoder motor2Encoder;

MotorModel sim_motor[2];
std::vector<SimSample> sim_trace;

void MotorModel::step(int pwm, bool braked, double dtMs)
{
    if (held)
    {
        velocity = 0;
        return;
    }
    if (pwm > 255)
        pwm = 255;
    if (pwm < -255)
        pwm = -255;
    double target = 0;
    if (!braked && abs(pwm) > deadbandPwm)
        target = (pwm > 0 ? 1 : -1) * countsPerSecPerPwm * (abs(pwm) - deadbandPwm);
    const double v0 = velocity;
    const double k = exp(-dtMs / tauMs);
    velocity = target + (v0 - target) * k;
    // Exact integral of the exponential approach over the step.
    position += (target * dtMs + (v0 - target) * tauMs * (1 - k)) / 1000.0;
}

// The count each model last wrote, so a write by the firmware
// (setCurrentPosition) moves the model.
static long written[2] = {0, 0};

static void step_motor(int i, LMD18200 &driver, Encoder &enc, double dtMs)
{
    if (enc.count != written[i])
        sim_motor[i].position = enc.count;
    sim_motor[i].step(driver.lastWrite, driver.braked, dtMs);
    enc.count = written[i] = (long)floor(sim_motor[i].position);
}

static void record()
{
    sim_trace.push_back({host_now, motor1Encoder.count, motor1AppliedPWM});
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static bool applied();

void sim_boot()
{
    setup();
    loop();

    // The speed loop configured as a controller would for the model motor:
    // gains Kp 0.6 Ki 3.0, and the feedforward an open-loop sweep of it gives.
    uint8_t pid[13] = {DCMT_PID_LOOP_SPEED};
    put16(pid + 1, SIM_SPEED_KP_X1000);
    put16(pid + 3, SIM_SPEED_KI_X1000);
    put16(pid + 7, SIM_SPEED_KP_X1000);
    put16(pid + 9, SIM_SPEED_KI_X1000);
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_PID, pid, 13);
    host_check("SET_PID speed (Kp 0.6, Ki 3.0) is applied", applied());
    uint8_t ff[8] = {SIM_FF_STATIC, 0, 0, SIM_FF_TRIM, SIM_FF_STATIC, 0, 0, SIM_FF_TRIM};
    put16(ff + 1, SIM_FF_RPM_PER_PWM_X1000);
    put16(ff + 5, SIM_FF_RPM_PER_PWM_X1000);
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_SPEED_FF, ff, 8);
    host_check("SET_SPEED_FF (static 10, 1.719 RPM per PWM, trim 20) is applied", applied());

    loop();
    record();
}

void sim_run(unsigned long ms)
{
    const unsigned long end = host_now + ms;
    while ((long)(end - host_now) > 0)
    {
        const unsigned long t0 = host_now;
        loop();
        if (host_now == t0)
            host_now++;
        const double dt = (double)(host_now - t0);
        step_motor(0, motor1Driver, motor1Encoder, dt);
        step_motor(1, motor2Driver, motor2Encoder, dt);
        record();
        Serial.out.clear();
    }
}

static bool applied() { return host_read_ack().status == CRUMBS_ACK_APPLIED; }

bool sim_open_loop(int16_t pwm1, int16_t pwm2)
{
    const uint8_t p[4] = {(uint8_t)(pwm1 & 0xFF), (uint8_t)((uint16_t)pwm1 >> 8), (uint8_t)(pwm2 & 0xFF),
                          (uint8_t)((uint16_t)pwm2 >> 8)};
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_OPEN_LOOP, p, 4);
    return applied();
}

bool sim_speed_setpoint(int16_t rpm1, int16_t rpm2)
{
    const uint8_t p[5] = {DCMT_PID_LOOP_SPEED, (uint8_t)(rpm1 & 0xFF), (uint8_t)((uint16_t)rpm1 >> 8),
                          (uint8_t)(rpm2 & 0xFF), (uint8_t)((uint16_t)rpm2 >> 8)};
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_SETPOINT, p, 5);
    return applied();
}

bool sim_mode(uint8_t mode)
{
    host_send(DCMT_TYPE_ID, DCMT_OP_SET_MODE, &mode, 1);
    return applied();
}

long sim_pos_at(unsigned long t)
{
    if (sim_trace.empty())
        return 0;
    auto it = std::upper_bound(sim_trace.begin(), sim_trace.end(), t,
                               [](unsigned long v, const SimSample &s) { return v < s.t; });
    return it == sim_trace.begin() ? it->pos : (it - 1)->pos;
}

double sim_rpm(unsigned long from, unsigned long to)
{
    if (to <= from)
        return 0;
    return (double)(sim_pos_at(to) - sim_pos_at(from)) / MOTOR1_CPR * 60000.0 / (double)(to - from);
}

double sim_peak_rpm(unsigned long from, unsigned long to, unsigned long windowMs)
{
    double peak = -1e9;
    for (unsigned long t = from; t + windowMs <= to; t += 10)
    {
        const double r = sim_rpm(t, t + windowMs);
        if (r > peak)
            peak = r;
    }
    return peak;
}

unsigned long sim_speed_run(int16_t rpm, unsigned long durationMs, unsigned long holdFromMs, unsigned long holdToMs)
{
    const unsigned long t0 = host_now;
    host_check("SET_SETPOINT speed is applied", sim_speed_setpoint(rpm, 0));
    if (slice.mode != CLOSED_LOOP_SPEED)
        host_check("SET_MODE speed is applied", sim_mode(DCMT_MODE_CLOSED_SPEED));
    if (holdToMs > holdFromMs && holdToMs <= durationMs)
    {
        sim_run(holdFromMs);
        sim_motor[0].held = true;
        sim_run(holdToMs - holdFromMs);
        sim_motor[0].held = false;
        sim_run(durationMs - holdToMs);
    }
    else
    {
        sim_run(durationMs);
    }
    return t0;
}
