"""DCMT bench helper for a Pi controller: CRUMBS over /dev/i2c-1 and the
board's USB serial port. Run it with the venv beside it, which pins
crumbs-i2c 0.16.2 and bread-crumbs-contracts 0.11.0:

  ~/dcmt-bench/venv/bin/python ~/dcmt-bench/dcmt_bench.py state

  dcmt_bench.py version | caps | state | wdog | ack
  dcmt_bench.py open M1 M2 | brake B1 B2 | mode N | sp T1 T2 | wd MS | clear
  dcmt_bench.py sp5 LOOP T1 T2     5-byte SET_SETPOINT (LOOP 1 position, 2 speed)
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

Every command prints a timestamp. SETs print the GET_ACK that follows.
Address: DCMT_ADDR (default 21). Port: DCMT_PORT, or the only ttyACM*/ttyUSB*
present.
"""

import os
import subprocess
import sys
import time

from bread_crumbs_contracts import dcmt_ops as d
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


def stamp():
    return time.strftime("%H:%M:%S") + f".{int(time.time() * 1000) % 1000:03d}"


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
        else:
            sys.exit(f"unknown command {cmd}")


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
