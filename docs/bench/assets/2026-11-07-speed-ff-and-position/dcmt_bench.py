"""DCMT bench helper for a Pi controller: CRUMBS over /dev/i2c-1 and the
board's USB serial port. Run it with the venv beside it, which pins
crumbs-i2c 0.16.2 and bread-crumbs-contracts 0.11.0:

  ~/dcmt-bench/venv/bin/python ~/dcmt-bench/dcmt_bench.py state

  dcmt_bench.py version | caps | state | wdog | ack
  dcmt_bench.py open M1 M2 | brake B1 B2 | mode N | sp T1 T2 | wd MS | clear
  dcmt_bench.py sp5 LOOP T1 T2     5-byte SET_SETPOINT (LOOP 1 position, 2 speed)
  dcmt_bench.py ffsweep M PWM...   open-loop sweep of motor M (forward and reverse
                                   at each PWM), fit rpm = rpm_per_pwm*(pwm - static)
                                   for SET_SPEED_FF; set MOTOR_CPR to the firmware's
  dcmt_bench.py inputs [N]        GET_INPUTS (0x81): analog header pins, N reads
                                   500 ms apart; volts assume a 5.000 V reference
  dcmt_bench.py wdl MS FLAGS       3-byte SET_WATCHDOG (the lease; 1 = commands only)
  dcmt_bench.py raw OP HEX         any SET opcode with a hex payload
  dcmt_bench.py watch SECONDS      poll GET_STATE every 200 ms, print each change
                                   of mode, PWM, brakes, e-stop or setpoint (not
                                   position), then GET_WATCHDOG
  dcmt_bench.py ser LINE...        send serial lines, print the replies
  dcmt_bench.py flood SECONDS      invalid serial lines nonstop while polling
                                   GET_STATE every 20 ms; prints each change
                                   of estop/estop_count and any failed poll
  dcmt_bench.py prep               stop the port from resetting the board on
                                   every open (stty -hupcl); run once
  dcmt_bench.py faults | fclear    GET_FAULTS (M1_STALL 0x01, M2_STALL 0x02);
                                   CLEAR_FAULTS (empty payload)
  dcmt_bench.py pid LOOP MOTOR KP KI KD
                                   13-byte SET_PID for LOOP (1 position,
                                   2 speed). The frame always carries both
                                   motors' gains: MOTOR 1 or 2 gets KP KI KD
                                   and the other motor this firmware's
                                   default for that loop (printed); MOTOR 0
                                   gives both KP KI KD
  dcmt_bench.py speedlog MOTOR SECONDS [TARGET_RPM]
  dcmt_bench.py home P1 P2         position mode to (P1, P2), poll until both
                                   arrive and stop (60 s), then brake; exit 1
                                   if not reached, so a chain stops there
  dcmt_bench.py speedto MOTOR COUNTS RPM   speed mode until MOTOR has moved
                                   COUNTS, then setpoint 0 and both brakes
                                   poll GET_STATE every 100 ms; print each
                                   poll with RPM from position over the last
                                   200 ms and 1 s (MOTOR_CPR counts/rev,
                                   default 798, the archive figure), then
                                   GET_FAULTS and a summary. TARGET_RPM, or
                                   env SPEEDLOG_TARGET, adds the first time
                                   within 5 % of it

Every command prints a timestamp. SETs print the GET_ACK that follows.
Address: DCMT_ADDR (default 21). Port: DCMT_PORT, or the only ttyACM*/ttyUSB*
present.
"""

import math
import os
import subprocess
import sys
import time

from bread_crumbs_contracts import dcmt_ops as d
from bread_crumbs_contracts.bread_faults import bread_faults_parse_payload
from crumbs_i2c import Controller, LinuxBus, Message

ADDR = int(os.environ.get("DCMT_ADDR", "21"), 0)


def default_port():
    # A Nano Every enumerates as ttyACM*, a classic Nano as ttyUSB*. Use the
    # only one present; with none or several, DCMT_PORT has to say which.
    import glob

    ports = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
    return ports[0] if len(ports) == 1 else None


PORT = os.environ.get("DCMT_PORT") or default_port()
BREAD_OP_GET_WATCHDOG = 0x7D
BREAD_OP_SET_WATCHDOG = 0x7E
BREAD_OP_CLEAR_WATCHDOG_TRIP = 0x7C
BREAD_OP_GET_CAPS = 0x7F
BREAD_OP_GET_FAULTS = 0x7A
BREAD_OP_CLEAR_FAULTS = 0x7B
FAULT_NAMES = {d.DCMT_FAULT_M1_STALL: "M1_STALL", d.DCMT_FAULT_M2_STALL: "M2_STALL"}
# Firmware defaults (config_hardware.h at 41c62c6), sent for the motor a
# one-motor `pid` leaves alone: SET_PID has no per-motor form and there is
# no GET_PID to read the other motor's gains back.
PID_DEFAULTS = {
    d.DCMT_PID_LOOP_POSITION: (0.15, 0.01, 0.01),
    d.DCMT_PID_LOOP_SPEED: (0.15, 0.2, 0.01),
}
CPR = int(os.environ.get("MOTOR_CPR", "848"))
POS_LIMIT = 32767  # GET_STATE positions saturate here


def stamp():
    # One clock read, so the seconds and the milliseconds agree.
    t = time.time()
    return time.strftime("%H:%M:%S", time.localtime(t)) + f".{int(t * 1000) % 1000:03d}"


def show_state(c):
    s = d.dcmt_parse_state_payload(
        c.query(ADDR, type_id=d.DCMT_TYPE_ID, opcode=d.DCMT_OP_GET_STATE).data
    )
    print(
        f"{stamp()} state mode={s.mode} m1_pwm={s.m1_pwm} m2_pwm={s.m2_pwm} "
        f"sp={s.sp1},{s.sp2} pos={s.pos1},{s.pos2} spd={s.spd1},{s.spd2} "
        f"brakes={s.brakes:#04b} estop={s.estop} estop_count={s.estop_count}"
    )
    return s


def watch(c, seconds):
    # GET_STATE only: under a commands-only lease these polls do not refresh
    # the watchdog, which is what the lease step checks.
    end = time.monotonic() + seconds
    last = None
    while time.monotonic() < end:
        try:
            s = d.dcmt_parse_state_payload(
                c.query(ADDR, type_id=d.DCMT_TYPE_ID, opcode=d.DCMT_OP_GET_STATE).data
            )
            now = (s.mode, s.m1_pwm, s.m2_pwm, s.brakes, s.estop, s.sp1, s.sp2)
            if now != last:
                show_state(c)
                last = now
        except Exception as e:
            print(f"{stamp()} poll failed: {e!r}")
        time.sleep(0.2)
    q = c.query(ADDR, type_id=d.DCMT_TYPE_ID, opcode=BREAD_OP_GET_WATCHDOG)
    print(stamp(), "wdog [armed][timeout_ms][tripped][trips]", q.data.hex())


def send(c, opcode, payload):
    c.send(ADDR, Message(type_id=d.DCMT_TYPE_ID, opcode=opcode, data=payload))
    time.sleep(0.01)
    a = c.read_ack(ADDR)
    print(f"{stamp()} sent 0x{opcode:02X} {payload.hex()} -> ack {a}")


def serial_port():
    import serial

    if not PORT:
        sys.exit("no single serial port found; set DCMT_PORT")
    return serial.Serial(PORT, 115200, timeout=0.2)


def main():
    cmd, *args = sys.argv[1:]
    if cmd == "prep":
        if not PORT:
            sys.exit("no single serial port found; set DCMT_PORT")
        subprocess.run(["stty", "-F", PORT, "-hupcl"], check=True)
        print(f"{stamp()} {PORT}: hupcl off")
        return
    if cmd == "ser":
        with serial_port() as p:
            for line in args:
                p.write(line.encode() + b"\n")
                time.sleep(0.15)
                print(f"{stamp()} > {line}")
                for r in p.read(4096).decode(errors="replace").splitlines():
                    print(f"{stamp()} < {r}")
        return
    with LinuxBus("/dev/i2c-1") as bus:
        c = Controller(bus)
        if cmd == "version":
            print(stamp(), c.get_version(ADDR, type_id=d.DCMT_TYPE_ID))
        elif cmd == "caps":
            r = c.query(ADDR, type_id=d.DCMT_TYPE_ID, opcode=BREAD_OP_GET_CAPS)
            print(stamp(), "caps", r.data.hex())
        elif cmd == "wdog":
            r = c.query(ADDR, type_id=d.DCMT_TYPE_ID, opcode=BREAD_OP_GET_WATCHDOG)
            print(stamp(), "wdog [armed][timeout_ms][tripped][trips]", r.data.hex())
        elif cmd == "state":
            show_state(c)
        elif cmd == "ffsweep":
            ffsweep(c, int(args[0]), [int(x) for x in args[1:]])
        elif cmd == "inputs":
            # GET_INPUTS (0x81, contracts 0.12.0), decoded here so the venv's
            # older contracts package is enough: [valid][bits][p1a][p1b][p2a][p2b].
            n = int(args[0]) if args else 1
            for i in range(n):
                r = c.query(ADDR, type_id=d.DCMT_TYPE_ID, opcode=0x81)
                b = r.data
                if len(b) != 10:
                    print(stamp(), "inputs: bad length", len(b), b.hex())
                else:
                    valid, bits = b[0], b[1]
                    vals = [b[2 + 2 * k] | (b[3 + 2 * k] << 8) for k in range(4)]
                    names = ["p1a", "p1b", "p2a", "p2b"]
                    parts = []
                    for k in range(4):
                        ok = valid & (1 << k)
                        v = vals[k]
                        parts.append(f"{names[k]}={v}({v * 5.0 / (1 << bits):.3f}V)" if ok else f"{names[k]}=-")
                    print(stamp(), f"inputs valid=0x{valid:02x} bits={bits}", " ".join(parts))
                if i + 1 < n:
                    time.sleep(0.5)
        elif cmd == "ack":
            print(stamp(), c.read_ack(ADDR))
        elif cmd == "open":
            send(c, d.DCMT_OP_SET_OPEN_LOOP, d.dcmt_send_set_open_loop(int(args[0]), int(args[1])))
        elif cmd == "brake":
            send(c, d.DCMT_OP_SET_BRAKE, d.dcmt_send_set_brake(int(args[0]), int(args[1])))
        elif cmd == "mode":
            send(c, d.DCMT_OP_SET_MODE, d.dcmt_send_set_mode(int(args[0])))
        elif cmd == "sp":
            send(c, d.DCMT_OP_SET_SETPOINT, d.dcmt_send_set_setpoint(int(args[0]), int(args[1])))
        elif cmd == "wd":
            send(c, BREAD_OP_SET_WATCHDOG, d.dcmt_send_set_watchdog(int(args[0])))
        elif cmd == "clear":
            send(c, BREAD_OP_CLEAR_WATCHDOG_TRIP, d.dcmt_send_clear_watchdog_trip())
        elif cmd == "flood":
            flood(c, float(args[0]))
        elif cmd == "sp5":
            send(c, d.DCMT_OP_SET_SETPOINT, d.dcmt_send_set_setpoint_loop(int(args[0]), int(args[1]), int(args[2])))
        elif cmd == "wdl":
            send(c, BREAD_OP_SET_WATCHDOG, d.dcmt_send_set_watchdog_lease(int(args[0]), int(args[1], 0)))
        elif cmd == "raw":
            send(c, int(args[0], 0), bytes.fromhex(args[1] if len(args) > 1 else ""))
        elif cmd == "watch":
            watch(c, float(args[0]))
        elif cmd == "faults":
            show_faults(c)
        elif cmd == "fclear":
            send(c, BREAD_OP_CLEAR_FAULTS, d.dcmt_send_clear_faults())
        elif cmd == "pid":
            send(c, d.DCMT_OP_SET_PID, pid_payload(*args[:5]))
        elif cmd == "home":
            home(c, int(args[0]), int(args[1]))
        elif cmd == "speedto":
            speedto(c, int(args[0]), int(args[1]), int(args[2]))
        elif cmd == "speedlog":
            target = args[2] if len(args) > 2 else os.environ.get("SPEEDLOG_TARGET")
            speedlog(c, int(args[0]), float(args[1]), float(target) if target else None)
        else:
            sys.exit(f"unknown command {cmd}")


def show_faults(c):
    q = c.query(ADDR, type_id=d.DCMT_TYPE_ID, opcode=BREAD_OP_GET_FAULTS)
    bits = bread_faults_parse_payload(q.data).faults
    names = [n for b, n in FAULT_NAMES.items() if bits & b] or ["none"]
    if bits & ~0x03:
        names.append(f"undefined 0x{bits & ~0x03:02x}")
    print(f"{stamp()} faults 0x{bits:02x} {' '.join(names)}")
    return bits


def pid_payload(loop, motor, kp, ki, kd):
    loop, motor = int(loop, 0), int(motor)
    gains = (float(kp), float(ki), float(kd))
    if motor == 0:
        g1 = g2 = gains
    elif motor in (1, 2):
        if loop not in PID_DEFAULTS:
            sys.exit("pid: a one-motor form needs LOOP 1 or 2; use MOTOR 0 to send another loop")
        other = PID_DEFAULTS[loop]
        g1, g2 = (gains, other) if motor == 1 else (other, gains)
        print(f"{stamp()} pid: motor {3 - motor} gets the default {other[0]} {other[1]} {other[2]}")
    else:
        sys.exit("pid: MOTOR is 0 (both), 1 or 2")
    x = [round(v * 1000) for v in (*g1, *g2)]
    return d.dcmt_send_set_pid(loop, *x)


def _rpm(hist, now_t, now_pos, window):
    # Position over the last `window` seconds: the newest poll at least that
    # old. None until there is one; "sat" if either end sits at the
    # saturated GET_STATE position, where the difference means nothing.
    old = None
    for t, pos in reversed(hist):
        if now_t - t >= window - 0.02:
            old = (t, pos)
            break
    if old is None:
        return None
    if abs(old[1]) >= POS_LIMIT or abs(now_pos) >= POS_LIMIT:
        return "sat"
    return (now_pos - old[1]) / (now_t - old[0]) / CPR * 60.0


def _fmt(v):
    if v is None:
        return "-"
    if v == "sat":
        return "sat"
    return f"{v:.1f}"


def ffsweep(c, motor, pwms, settle=2.0, measure=2.0):
    # Open-loop PWM sweep of one motor for SET_SPEED_FF: at each PWM, forward
    # then reverse (so the position stays clear of the +-32767 limit), settle,
    # then measure counts/s from GET_STATE positions and convert with CPR.
    # Polling keeps an armed lease fed. Brakes both motors when done, whatever
    # happened. Prints a least-squares fit per direction:
    # rpm = rpm_per_pwm * (pwm - static).
    rows = {1: [], -1: []}

    bad = [0]

    def poll():
        # One bad reply (a cut-short frame, a CRC error) is retried, up to
        # 5 times; each is printed and counted.
        for attempt in range(5):
            try:
                s = d.dcmt_parse_state_payload(
                    c.query(ADDR, type_id=d.DCMT_TYPE_ID, opcode=d.DCMT_OP_GET_STATE).data
                )
                return s.pos1 if motor == 1 else s.pos2
            except Exception as e:
                bad[0] += 1
                print(f"{stamp()} ffsweep poll failed: {e!r}")
                time.sleep(0.05)
        raise RuntimeError("ffsweep: 5 bad replies in a row")

    def coast(seconds=0.5):
        # PWM 0 between directions, still polling: no hard reversal.
        send(c, d.DCMT_OP_SET_OPEN_LOOP, d.dcmt_send_set_open_loop(0, 0))
        t_end = time.monotonic() + seconds
        while time.monotonic() < t_end:
            poll()
            time.sleep(0.1)

    def run(pwm):
        coast()
        m1, m2 = (pwm, 0) if motor == 1 else (0, pwm)
        send(c, d.DCMT_OP_SET_OPEN_LOOP, d.dcmt_send_set_open_loop(m1, m2))
        t_end = time.monotonic() + settle
        while time.monotonic() < t_end:
            poll()
            time.sleep(0.1)
        t0, p0 = time.monotonic(), poll()
        while time.monotonic() - t0 < measure:
            time.sleep(0.1)
            t1, p1 = time.monotonic(), poll()
        if abs(p0) >= POS_LIMIT or abs(p1) >= POS_LIMIT:
            return None
        return (p1 - p0) / (t1 - t0) / CPR * 60.0

    try:
        send(c, d.DCMT_OP_SET_MODE, d.dcmt_send_set_mode(0))
        send(c, d.DCMT_OP_SET_OPEN_LOOP, d.dcmt_send_set_open_loop(0, 0))
        b1, b2 = (0, 1) if motor == 1 else (1, 0)
        send(c, d.DCMT_OP_SET_BRAKE, d.dcmt_send_set_brake(b1, b2))
        for pwm in pwms:
            for sign in (1, -1):
                rpm = run(sign * pwm)
                rows[sign].append((pwm, rpm))
                print(f"{stamp()} ffsweep motor {motor} pwm {sign * pwm:+d}: "
                      f"{'saturated' if rpm is None else f'{rpm:+.1f} RPM'}")
    finally:
        send(c, d.DCMT_OP_SET_OPEN_LOOP, d.dcmt_send_set_open_loop(0, 0))
        send(c, d.DCMT_OP_SET_BRAKE, d.dcmt_send_set_brake(1, 1))

    print(f"{stamp()} ffsweep: {bad[0]} bad replies retried")
    fits = {}
    for sign, label in ((1, "forward"), (-1, "reverse")):
        pts = [(pw, abs(r)) for pw, r in rows[sign] if r is not None and abs(r) > 0.5]
        if len(pts) < 2:
            print(f"{stamp()} ffsweep {label}: fewer than 2 moving points, no fit")
            continue
        n = len(pts)
        mx = sum(x for x, _ in pts) / n
        my = sum(y for _, y in pts) / n
        sxx = sum((x - mx) ** 2 for x, _ in pts)
        slope = sum((x - mx) * (y - my) for x, y in pts) / sxx
        static = mx - my / slope
        fits[sign] = (static, slope)
        print(f"{stamp()} ffsweep {label}: rpm_per_pwm {slope:.4f}, static {static:.1f} PWM ({n} points, CPR {CPR})")
    if len(fits) == 2:
        # Err low: the lower static and the higher rpm_per_pwm under-drive.
        # Rounded towards less drive: static down, rpm_per_pwm x1000 up.
        st = max(0, int(min(fits[1][0], fits[-1][0])))
        rpp = max(fits[1][1], fits[-1][1])
        print(f"{stamp()} ffsweep suggest: static {st}, rpm_per_pwm {rpp:.4f} "
              f"(x1000 {math.ceil(rpp * 1000)}), the lower-driving of the two directions")


def home(c, p1, p2, timeout=60.0, tol=20):
    # Position mode to (p1, p2) with both brakes released, polling until both
    # motors are within tol counts and stopped (PWM 0), or timeout; then both
    # brakes. Polling keeps an armed lease fed. GET_STATE positions saturate
    # at +-32767, so a target beyond that is never reached.
    send(c, d.DCMT_OP_SET_BRAKE, d.dcmt_send_set_brake(1, 1))
    send(c, d.DCMT_OP_SET_MODE, d.dcmt_send_set_mode(1))
    send(c, d.DCMT_OP_SET_SETPOINT, d.dcmt_send_set_setpoint(p1, p2))
    send(c, d.DCMT_OP_SET_BRAKE, d.dcmt_send_set_brake(0, 0))
    t0 = time.monotonic()
    done = False
    try:
        while time.monotonic() - t0 < timeout:
            try:
                s = d.dcmt_parse_state_payload(
                    c.query(ADDR, type_id=d.DCMT_TYPE_ID, opcode=d.DCMT_OP_GET_STATE).data
                )
            except Exception as e:
                # One bad reply (a cut-short frame, a bus error) is retried;
                # the timeout still bounds the wait.
                print(f"{stamp()} home poll failed: {e!r}")
                time.sleep(0.1)
                continue
            if (abs(s.pos1 - p1) <= tol and abs(s.pos2 - p2) <= tol
                    and s.m1_pwm == 0 and s.m2_pwm == 0):
                done = True
                break
            time.sleep(0.1)
    finally:
        send(c, d.DCMT_OP_SET_BRAKE, d.dcmt_send_set_brake(1, 1))
    print(f"{stamp()} home ({p1}, {p2}): {'reached' if done else 'NOT reached'} "
          f"after {time.monotonic() - t0:.1f} s, pos {s.pos1},{s.pos2}")
    if not done:
        sys.exit(1)


def speedto(c, motor, counts, rpm):
    # Speed mode at RPM on one motor until it has moved COUNTS from where it
    # started, then setpoint 0 and both brakes, all from this one process so
    # the stop follows the crossing poll by one I2C transaction. Prints the
    # start, the crossing and the final position.
    m = motor - 1

    def pos():
        s = d.dcmt_parse_state_payload(
            c.query(ADDR, type_id=d.DCMT_TYPE_ID, opcode=d.DCMT_OP_GET_STATE).data
        )
        return (s.pos1, s.pos2)[m]

    send(c, d.DCMT_OP_SET_BRAKE, d.dcmt_send_set_brake(1, 1))
    send(c, d.DCMT_OP_SET_MODE, d.dcmt_send_set_mode(2))
    start = pos()
    t1, t2 = (rpm, 0) if motor == 1 else (0, rpm)
    send(c, d.DCMT_OP_SET_SETPOINT, d.dcmt_send_set_setpoint_loop(2, t1, t2))
    print(f"{stamp()} speedto motor {motor}: start {start}, stop at {start + counts}, {rpm} RPM")
    b1, b2 = (0, 1) if motor == 1 else (1, 0)
    send(c, d.DCMT_OP_SET_BRAKE, d.dcmt_send_set_brake(b1, b2))
    t0 = time.monotonic()
    last = -1.0
    try:
        while True:
            p = pos()
            if p - start >= counts:
                break
            if time.monotonic() - last >= 5:
                last = time.monotonic()
                print(f"{stamp()} {time.monotonic() - t0:6.1f} s pos {p}")
            time.sleep(0.05)
    finally:
        send(c, d.DCMT_OP_SET_SETPOINT, d.dcmt_send_set_setpoint_loop(2, 0, 0))
        send(c, d.DCMT_OP_SET_BRAKE, d.dcmt_send_set_brake(1, 1))
    print(f"{stamp()} crossed at {p} after {time.monotonic() - t0:.1f} s")
    # Poll through the settle rather than sleep, so an armed watchdog lease
    # keeps being fed.
    settle = time.monotonic() + 1.0
    while time.monotonic() < settle:
        end = pos()
        time.sleep(0.1)
    end = pos()
    print(f"{stamp()} final {end}: moved {end - start} counts")


def speedlog(c, motor, seconds, target=None):
    # GET_STATE every 100 ms on a fixed schedule. RPM is computed here from
    # the reported position, which moves by whole counts, not from spd, the
    # firmware's tacho figure (coarse at low speed); spd is printed beside it.
    m = motor - 1
    print(
        f"{stamp()} speedlog motor {motor} {seconds:g} s, CPR {CPR}, target "
        f"{'none' if target is None else f'{target:g} RPM'}"
    )
    print("stamp t pos rpm200 rpm1s spd pwm brakes estop")
    hist = []  # (t, pos) for every good poll
    rows = []  # (t, rpm200, rpm1s)
    fails = 0
    t0 = time.monotonic()
    nxt = t0
    while True:
        now = time.monotonic()
        if now - t0 > seconds:
            break
        try:
            s = d.dcmt_parse_state_payload(
                c.query(ADDR, type_id=d.DCMT_TYPE_ID, opcode=d.DCMT_OP_GET_STATE).data
            )
            t = time.monotonic() - t0
            pos = (s.pos1, s.pos2)[m]
            spd = (s.spd1, s.spd2)[m]
            pwm = (s.m1_pwm, s.m2_pwm)[m]
            r200 = _rpm(hist, t, pos, 0.2)
            r1 = _rpm(hist, t, pos, 1.0)
            hist.append((t, pos))
            rows.append((t, r200, r1))
            print(
                f"{stamp()} {t:6.2f} {pos} {_fmt(r200)} {_fmt(r1)} "
                f"{'-' if spd == -32768 else spd} {pwm} {s.brakes:#04b} {s.estop}"
            )
        except Exception as e:
            fails += 1
            print(f"{stamp()} poll failed: {e!r}")
        nxt += 0.1
        time.sleep(max(0.0, nxt - time.monotonic()))
    try:
        show_faults(c)
    except Exception as e:
        print(f"{stamp()} faults read failed: {e!r}")
    _speed_summary(hist, rows, fails, target)


def _speed_summary(hist, rows, fails, target):
    num = lambda v: isinstance(v, float)
    print(f"{stamp()} summary: {len(hist)} polls, {fails} failed")
    if target:
        band = abs(target) * 0.05
        for i, name in ((1, "200 ms"), (2, "1 s")):
            hit = next((r[0] for r in rows if num(r[i]) and abs(r[i] - target) <= band), None)
            print(f"  first within 5 % of {target:g} ({name} RPM): "
                  + ("never" if hit is None else f"t={hit:.2f} s"))
    r1s = [(r[2], r[0]) for r in rows if num(r[2])]
    if r1s:
        peak, at = max(r1s, key=lambda x: abs(x[0]))
        print(f"  peak 1 s mean: {peak:.1f} RPM at t={at:.2f} s")
    else:
        print("  peak 1 s mean: none (run under 1 s, or saturated)")
    if hist and hist[-1][0] >= 5.0:
        end_t, end_pos = hist[-1]
        start = next(((t, p) for t, p in hist if t >= end_t - 5.0 - 0.02), None)
        if abs(start[1]) >= POS_LIMIT or abs(end_pos) >= POS_LIMIT:
            print("  mean over the last 5 s: none (position saturated at +-32767)")
        else:
            rpm = (end_pos - start[1]) / (end_t - start[0]) / CPR * 60.0
            print(f"  mean over the last 5 s: {rpm:.1f} RPM ({start[0]:.2f} to {end_t:.2f} s)")
    else:
        print("  mean over the last 5 s: none (run under 5 s)")
    if any(abs(p) >= POS_LIMIT for _, p in hist):
        print("  position reached +-32767 (saturated): RPM after that is not measured")


def flood(c, seconds):
    # Each invalid line makes the firmware print its help text, so the
    # replies alone keep its transmit buffer full.
    with serial_port() as p:
        p.timeout = 0
        p.write_timeout = 0
        end = time.monotonic() + seconds
        last = None
        lines = polls = fails = 0
        while time.monotonic() < end:
            try:
                p.write(b"x\n" * 8)
                lines += 8
            except Exception:
                pass
            p.reset_input_buffer()
            try:
                s = d.dcmt_parse_state_payload(
                    c.query(ADDR, type_id=d.DCMT_TYPE_ID, opcode=d.DCMT_OP_GET_STATE).data
                )
                polls += 1
                now = (s.estop, s.estop_count)
                if now != last:
                    print(f"{stamp()} estop={s.estop} estop_count={s.estop_count}")
                    last = now
            except Exception as e:
                fails += 1
                print(f"{stamp()} poll failed: {e!r}")
            time.sleep(0.02)
        print(f"{stamp()} done: {lines} lines sent, {polls} polls, {fails} failed")


if __name__ == "__main__":
    main()
