// #62: entering closed-loop speed after open-loop travel starts from where
// the shaft is, not from the speed baseline the open-loop travel left
// behind. In DCMotorServo 2.x, setSpeedRPM() from 0 restarts the speed
// measurement and the integral there; with the shaft still coasting
// backwards, the first samples read it, and the loop must still never drive
// backwards for a forward target (#62). Each case boots cold, so it runs as
// its own process.
#include "host.h"
#include "motor_sim.h"
#include "globals.h"
#include <bread/dcmt_ops.h>
#include <cstdio>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

// Motor 1's applied PWM over (from, to].
static void pwm_range(unsigned long from, unsigned long to, int &lo, int &hi)
{
    lo = 255;
    hi = -255;
    for (const SimSample &s : sim_trace)
        if (s.t > from && s.t <= to)
        {
            lo = s.pwm < lo ? s.pwm : lo;
            hi = s.pwm > hi ? s.pwm : hi;
        }
}

// Open loop at full reverse PWM for 600 ms, then straight into speed mode at
// +60 RPM while the shaft is still coasting backwards.
static void reverse_then_speed()
{
    sim_boot();
    host_check("open loop at -255 is applied", sim_open_loop(-255, 0));
    sim_run(600);
    const unsigned long te = host_now;
    printf("     entry: %ld counts, %.0f counts/s\n", sim_pos_at(te), sim_motor[0].velocity);
    sim_speed_run(60, 1000);

    int lo, hi;
    pwm_range(te, te + 1000, lo, hi);
    printf("     first 1 s: PWM %d to %d\n", lo, hi);
    host_check("#62 reverse: entering speed at +60 RPM never drives the motor backwards", lo >= 0);
    host_check("#62 reverse: nor at the PWM limit (DCMT_SERVO_MAX_PWM)", hi < DCMT_SERVO_MAX_PWM);
}

// The travel of #62's own run: open loop forward at full PWM for 600 ms,
// stop, then speed mode at +60 RPM with the shaft at rest.
static void forward_stop_then_speed()
{
    sim_boot();
    host_check("open loop at +255 is applied", sim_open_loop(255, 0));
    sim_run(600);
    host_check("open loop at 0 is applied", sim_open_loop(0, 0));
    sim_run(300);
    const unsigned long te = host_now;
    const long pe = sim_pos_at(te);
    printf("     entry: %ld counts, %.0f counts/s\n", pe, sim_motor[0].velocity);
    sim_speed_run(60, 1000);

    int lo, hi;
    pwm_range(te, te + 1000, lo, hi);
    long low = pe;
    for (const SimSample &s : sim_trace)
        if (s.t > te && s.t <= te + 1000 && s.pos < low)
            low = s.pos;
    printf("     first 1 s: PWM %d to %d, lowest position %+ld counts from entry\n", lo, hi, low - pe);
    host_check("#62 forward: entering speed at +60 RPM never drives the motor backwards", lo >= 0);
    host_check("#62 forward: nor at the PWM limit (DCMT_SERVO_MAX_PWM)", hi < DCMT_SERVO_MAX_PWM);
    host_check("#62 forward: the shaft never moves back from where it entered", low >= pe);
}

// The reverse case over the entry timing: open loop at full reverse for each
// of 550 to 750 ms in 2 ms steps, then speed mode at +60 RPM. Where the entry
// falls against the speed interval and the coasting shaft decides what the
// first speed samples read, so one timing passing is no evidence for the
// next. Each timing boots cold, so each runs in a forked child, which sends
// back its lowest PWM over the first 1 s and whether its commands applied.
static void reverse_sweep()
{
    const unsigned long first = 550, last = 750, step = 2;
    int runs = 0, negative = 0, broken = 0, lowest = 255;
    for (unsigned long ms = first; ms <= last; ms += step)
    {
        int fd[2];
        if (pipe(fd) != 0)
        {
            host_check("pipe() for the sweep", false);
            return;
        }
        fflush(stdout);
        const pid_t pid = fork();
        if (pid == 0)
        {
            close(fd[0]);
            if (!freopen("/dev/null", "w", stdout))
                _exit(3);
            sim_boot();
            const bool ok = sim_open_loop(-255, 0);
            sim_run(ms);
            const unsigned long te = host_now;
            sim_speed_run(60, 1000);
            int lo, hi;
            pwm_range(te, te + 1000, lo, hi);
            const int msg[2] = {lo, (ok && host_finish() == 0) ? 1 : 0};
            _exit(write(fd[1], msg, sizeof msg) == (ssize_t)sizeof msg ? 0 : 3);
        }
        close(fd[1]);
        int msg[2] = {0, 0};
        const bool got = pid > 0 && read(fd[0], msg, sizeof msg) == (ssize_t)sizeof msg;
        close(fd[0]);
        int status = 0;
        if (pid > 0)
            waitpid(pid, &status, 0);
        runs++;
        if (!got || !WIFEXITED(status) || WEXITSTATUS(status) != 0 || msg[1] != 1)
        {
            printf("     %lu ms: the case didn't run to the end\n", ms);
            broken++;
            continue;
        }
        if (msg[0] < 0)
        {
            printf("     %lu ms: lowest PWM %d in the first 1 s\n", ms, msg[0]);
            negative++;
        }
        lowest = msg[0] < lowest ? msg[0] : lowest;
    }
    printf("     %d entry timings, %lu to %lu ms: %d with negative PWM; lowest PWM %d\n", runs, first, last, negative,
           lowest);
    host_check("#62 sweep: every timing ran, its commands applied", broken == 0 && runs == 101);
    host_check("#62 sweep: entering speed at +60 RPM after 550-750 ms of full reverse never drives backwards",
               negative == 0);
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "reverse") == 0)
        reverse_then_speed();
    else if (argc > 1 && strcmp(argv[1], "forward") == 0)
        forward_stop_then_speed();
    else if (argc > 1 && strcmp(argv[1], "sweep") == 0)
        reverse_sweep();
    else
    {
        printf("usage: %s reverse|forward|sweep\n", argv[0]);
        return 2;
    }
    return host_finish();
}
