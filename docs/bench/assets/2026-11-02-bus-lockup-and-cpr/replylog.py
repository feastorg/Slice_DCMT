"""Poll GET_STATE (0x80) at one address every 100 ms for SECONDS, with the
binding's query retries off, and print every reply that fails to decode or
whose payload after the first byte is all 0xFF, with the raw bytes read.
Read-only.

  python replylog.py ADDR TYPE_ID SECONDS
"""

import sys
import time

from crumbs_i2c import Controller, LinuxBus


def stamp():
    return time.strftime("%H:%M:%S") + f".{int(time.time() * 1000) % 1000:03d}"


def main():
    addr, type_id, seconds = int(sys.argv[1], 0), int(sys.argv[2], 0), float(sys.argv[3])
    polls = bad = ff = 0
    with LinuxBus("/dev/i2c-1") as bus:
        last = {}
        real_read = bus.read

        def read(address, count):
            last["raw"] = real_read(address, count)
            return last["raw"]

        bus.read = read
        c = Controller(bus, query_attempts=1)
        print(stamp(), f"start addr={addr} type={type_id}")
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            polls += 1
            last.clear()
            try:
                q = c.query(addr, type_id=type_id, opcode=0x80)
                if len(q.data) > 1 and set(q.data[1:]) == {0xFF}:
                    ff += 1
                    print(stamp(), "all-FF payload passed CRC", last["raw"].hex())
            except Exception as e:
                bad += 1
                print(stamp(), repr(e), last.get("raw", b"").hex())
            time.sleep(0.1)
    print(f"polls={polls} failed={bad} ff_passed={ff}")


if __name__ == "__main__":
    main()
