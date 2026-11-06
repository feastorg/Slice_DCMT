// Closed-loop host tests: the firmware with the real DCMotorServo,
// DCMotorTacho and PID_v1, driving a modelled motor on each channel.
//
// Each loop() pass advances the fake clock 1 ms (the e-stop poll's
// digitalRead()), and sim_run() then steps both motor models by the time the
// pass took: the model reads what the firmware last asked of the LMD18200
// stub and writes the Encoder stub's count. Motor 1's encoder count and
// applied PWM are recorded each pass in sim_trace, which the speed helpers
// read.
#pragma once
#include <stdint.h>
#include <vector>

// First-order DC motor. Defaults are the fit used for #90: 24.3 counts/s per
// PWM above a deadband of 10, time constant 30 ms. Braked, in the deadband
// or with no drive, the speed decays to 0 with the same time constant.
// While held, the shaft doesn't move.
struct MotorModel
{
    double countsPerSecPerPwm = 24.3;
    int deadbandPwm = 10;
    double tauMs = 30;
    bool held = false;

    double velocity = 0; // counts/s
    double position = 0; // counts

    void step(int pwm, bool braked, double dtMs);
};

extern MotorModel sim_motor[2];

struct SimSample
{
    unsigned long t; // millis()
    long pos;        // motor 1 encoder count
    int16_t pwm;     // motor 1 applied PWM (motor1AppliedPWM)
};
extern std::vector<SimSample> sim_trace;

// The speed loop's configuration for the model motor, sent by sim_boot():
// SET_PID speed gains Kp 0.6, Ki 3.0 (x1000), and SET_SPEED_FF with the
// model's feedforward, the values an open-loop PWM sweep of it gives:
// static 10 (the deadband) and 24.3 counts/s per PWM, which at CPR 848 is
// 24.3 * 60 / 848 = 1.719 RPM per PWM; trim 20.
#define SIM_SPEED_KP_X1000 600
#define SIM_SPEED_KI_X1000 3000
#define SIM_FF_STATIC 10
#define SIM_FF_RPM_PER_PWM_X1000 1719
#define SIM_FF_TRIM 20

// setup(), one loop() pass, the speed-loop configuration above for both
// motors, and one more pass.
void sim_boot();

// Runs loop() for ms of fake time, stepping both motors after every pass.
void sim_run(unsigned long ms);

// Commands, through the CRUMBS receive path. Each returns true if GET_ACK
// read APPLIED.
bool sim_open_loop(int16_t pwm1, int16_t pwm2);
bool sim_speed_setpoint(int16_t rpm1, int16_t rpm2);
bool sim_mode(uint8_t mode);

// Motor 1's encoder count at the last pass at or before t.
long sim_pos_at(unsigned long t);
// Motor 1's mean speed from position over [from, to], in RPM at MOTOR1_CPR.
double sim_rpm(unsigned long from, unsigned long to);
// The highest mean speed over any window of windowMs inside [from, to],
// stepping 10 ms.
double sim_peak_rpm(unsigned long from, unsigned long to, unsigned long windowMs);

// The building block for speed-loop cases: puts motor 1 in closed-loop speed
// at rpm (motor 2 at 0), runs for durationMs, and holds motor 1's shaft from
// holdFromMs to holdToMs after the command (no hold when they are equal).
// Returns the time of the command, so a case measures over
// [t0 + a, t0 + b]. Fails a host_check if a command isn't applied.
unsigned long sim_speed_run(int16_t rpm, unsigned long durationMs, unsigned long holdFromMs = 0,
                            unsigned long holdToMs = 0);
