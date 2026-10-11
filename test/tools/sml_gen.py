#!/usr/bin/env python3
"""Synthetic SML frames for variants we have no capture of yet (e.g. the LGZ with PIN enabled, feed-in,
other integer sizes, scalers, long frames), plus the values the decoder must find in them.

Writes test/test_decoder/sml_synth.h. The frames are built like a meter does it: SML messages
(OpenResponse, GetListResponse, CloseResponse) with message CRC, wrapped in the transport frame with
padding and CRC. A new variant is one more function below.

usage (from project root):  python3 test/tools/sml_gen.py
"""
import os
import struct

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
OUT = os.path.join(ROOT, "test", "test_decoder", "sml_synth.h")


# ---- SML encoding -------------------------------------------------------------------------------
def crc16(data):
    c = 0xFFFF
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0x8408 if c & 1 else c >> 1
    return c ^ 0xFFFF


def tl(typ, n_data):
    """type/length field for octet string, boolean, integer, unsigned (length counts the TL bytes)"""
    for k in (1, 2, 3):
        total = n_data + k
        if total < 16 ** k:
            digits = [(total >> (4 * (k - 1 - i))) & 0xF for i in range(k)]
            out = bytearray()
            for i, d in enumerate(digits):
                more = 0x80 if i < k - 1 else 0
                out.append(more | ((typ << 4) if i == 0 else 0) | d)
            return bytes(out)
    raise ValueError("too long")


def lst(items):
    n = len(items)
    head = bytes([0x70 | n]) if n < 16 else bytes([0xF0 | (n >> 4), n & 0x0F])
    return head + b"".join(items)


ABSENT = b"\x01"


def octets(b):
    return tl(0, len(b)) + bytes(b)


def uint(v, size):
    return tl(6, size) + v.to_bytes(size, "big")


def sint(v, size):
    return tl(5, size) + v.to_bytes(size, "big", signed=True)


def sml_time(kind, v):  # kind 1 = secIndex, 2 = timestamp
    return lst([uint(kind, 1), uint(v, 4)])


def message(tag, body):
    inner = lst([uint(tag, 2), body])
    head = lst([octets(b"\x05\x01\x91\xbb\x10"), uint(0, 1), uint(0, 1), inner])  # list(6) prefix is fixed below
    # the message is list(6): transactionId, groupNo, abortOnError, messageBody, crc, endOfMessage
    msg = bytes([0x76]) + octets(b"\x05\x01\x91\xbb\x10") + uint(0, 1) + uint(0, 1) + inner
    crc = crc16(msg)
    return msg + b"\x63" + bytes([crc & 0xFF, crc >> 8]) + b"\x00"


def entry(obis, value, scaler=None, unit=None, status=None, vtime=None, sig=None):
    """value: ('u', v, size) | ('i', v, size) | ('o', bytes)"""
    kind = value[0]
    if kind == "u":
        v = uint(value[1], value[2])
    elif kind == "i":
        v = sint(value[1], value[2])
    else:
        v = octets(value[1])
    return lst([
        octets(bytes(obis)),
        ABSENT if status is None else uint(status, 4),
        ABSENT if vtime is None else sml_time(*vtime),
        ABSENT if unit is None else uint(unit, 1),
        ABSENT if scaler is None else sint(scaler, 1),
        v,
        ABSENT if sig is None else octets(sig),
    ])


def frame(server_id, entries, sensor_time=None, list_sig=None):
    openresp = lst([ABSENT, ABSENT, octets(b"\x01\x02\x03\x04"), octets(server_id), ABSENT, ABSENT])
    getlist = lst([
        ABSENT,
        octets(server_id),
        ABSENT,
        ABSENT if sensor_time is None else sml_time(*sensor_time),
        lst(entries),
        ABSENT if list_sig is None else octets(list_sig),
        ABSENT,
    ])
    closeresp = lst([ABSENT])
    body = message(0x0101, openresp) + message(0x0701, getlist) + message(0x0201, closeresp)
    pad = (-len(body)) % 4
    body += b"\x00" * pad
    f = b"\x1b\x1b\x1b\x1b\x01\x01\x01\x01" + body + b"\x1b\x1b\x1b\x1b\x1a" + bytes([pad])
    crc = crc16(f)
    return f + bytes([crc & 0xFF, crc >> 8])


# ---- variants -------------------------------------------------------------------------------------
LGZ_ID = bytes.fromhex("0a014c475a0001260d00")
ISK_ID = bytes.fromhex("090149534b000421d3d9")
O = lambda *a: a  # noqa: E731  OBIS code as a tuple of 6 bytes
WH, W, V, A, HZ = 30, 27, 35, 33, 44

cases = []  # (name, frame bytes, expected dict)


def case(name, fr, **exp):
    cases.append((name, fr, exp))


def hexid(b):
    return b.hex()


# 1. Landis+Gyr with PIN enabled: actual power, phase powers, voltages, currents, frequency
ents = [
    entry(O(1, 0, 96, 50, 1, 1), ("o", b"LGZ")),
    entry(O(1, 0, 96, 1, 0, 255), ("o", LGZ_ID)),
    entry(O(1, 0, 1, 8, 0, 255), ("u", 4780009, 8), -1, WH, vtime=(1, 8780000)),
    entry(O(1, 0, 2, 8, 0, 255), ("u", 0, 8), -1, WH, vtime=(1, 8780000)),
    entry(O(1, 0, 16, 7, 0, 255), ("i", 437, 4), 0, W),
    entry(O(1, 0, 36, 7, 0, 255), ("i", 150, 4), 0, W),
    entry(O(1, 0, 56, 7, 0, 255), ("i", 140, 4), 0, W),
    entry(O(1, 0, 76, 7, 0, 255), ("i", 147, 4), 0, W),
    entry(O(1, 0, 32, 7, 0, 255), ("u", 2315, 2), -1, V),
    entry(O(1, 0, 52, 7, 0, 255), ("u", 2310, 2), -1, V),
    entry(O(1, 0, 72, 7, 0, 255), ("u", 2322, 2), -1, V),
    entry(O(1, 0, 31, 7, 0, 255), ("u", 65, 2), -2, A),
    entry(O(1, 0, 14, 7, 0, 255), ("u", 5001, 2), -2, HZ),
    entry(O(1, 0, 0, 2, 0, 0), ("o", b"01.02.05")),
    entry(O(1, 0, 96, 90, 2, 1), ("o", b"3053")),
]
case("LGZ_PIN", frame(LGZ_ID, ents, (1, 8780000)), meterid="LGZ" + hexid(LGZ_ID), act=437.0, sum=478000.9,
     exp=0.0, time=8780000)

# 2. feed-in: negative power, export energy counter
ents = [
    entry(O(1, 0, 96, 50, 1, 1), ("o", b"LGZ")),
    entry(O(1, 0, 96, 1, 0, 255), ("o", LGZ_ID)),
    entry(O(1, 0, 1, 8, 0, 255), ("u", 1234567, 8), -1, WH),
    entry(O(1, 0, 2, 8, 0, 255), ("u", 98765, 8), -1, WH),
    entry(O(1, 0, 16, 7, 0, 255), ("i", -1250, 4), 0, W),
]
case("NEG_POWER", frame(LGZ_ID, ents, (1, 100)), meterid="LGZ" + hexid(LGZ_ID), act=-1250.0, sum=123456.7,
     exp=9876.5, time=100)

# 3. other integer sizes: int16 power, 64 bit counter beyond 2^32, 16 bit export
ents = [
    entry(O(129, 129, 199, 130, 3, 255), ("o", b"ISK")),
    entry(O(1, 0, 0, 0, 9, 255), ("o", ISK_ID)),
    entry(O(1, 0, 1, 8, 0, 255), ("u", 2 ** 40, 8), -1, WH),
    entry(O(1, 0, 2, 8, 0, 255), ("u", 65535, 2), 0, WH),
    entry(O(1, 0, 16, 7, 0, 255), ("i", -3000, 2), 0, W),
]
case("INT_SIZES", frame(ISK_ID, ents, (1, 5)), meterid="ISK" + hexid(ISK_ID), act=-3000.0,
     sum=2 ** 40 / 10.0, exp=65535.0, time=5)

# 4. positive scaler and 8 bit power
ents = [
    entry(O(129, 129, 199, 130, 3, 255), ("o", b"ISK")),
    entry(O(1, 0, 0, 0, 9, 255), ("o", ISK_ID)),
    entry(O(1, 0, 1, 8, 0, 255), ("u", 123456, 4), -3, WH),
    entry(O(1, 0, 16, 7, 0, 255), ("i", -5, 1), 1, W),  # -5 * 10 W
]
case("SCALER", frame(ISK_ID, ents, (1, 6)), meterid="ISK" + hexid(ISK_ID), act=-50.0, sum=123.456, time=6)

# 5. minimal: no sensor time, no vendor/serial entries (serial from the server id), no unit, no scaler
ents = [entry(O(1, 0, 1, 8, 0, 255), ("u", 1000, 4))]
case("MINIMAL", frame(LGZ_ID, ents, None), meterid=hexid(LGZ_ID), sum=1000.0)

# 6. tariff registers: only 1.8.0 counts
ents = [
    entry(O(1, 0, 96, 50, 1, 1), ("o", b"LGZ")),
    entry(O(1, 0, 96, 1, 0, 255), ("o", LGZ_ID)),
    entry(O(1, 0, 1, 8, 1, 255), ("u", 6000, 8), -1, WH),
    entry(O(1, 0, 1, 8, 2, 255), ("u", 4000, 8), -1, WH),
    entry(O(1, 0, 1, 8, 0, 255), ("u", 10000, 8), -1, WH),
    entry(O(1, 0, 16, 7, 0, 255), ("u", 55, 4), 0, W),
]
case("TARIFF", frame(LGZ_ID, ents, (1, 7)), meterid="LGZ" + hexid(LGZ_ID), act=55.0, sum=1000.0, time=7)

# 7. historic values (F != 255) around the current one must not overwrite it
ents = [
    entry(O(1, 0, 96, 50, 1, 1), ("o", b"LGZ")),
    entry(O(1, 0, 96, 1, 0, 255), ("o", LGZ_ID)),
    entry(O(1, 0, 1, 8, 0, 1), ("u", 4000, 8), -1, WH),
    entry(O(1, 0, 1, 8, 0, 255), ("u", 5000, 8), -1, WH),
    entry(O(1, 0, 1, 8, 0, 2), ("u", 3000, 8), -1, WH),
]
case("HISTORIC", frame(LGZ_ID, ents, (1, 8)), meterid="LGZ" + hexid(LGZ_ID), sum=500.0, time=8)

# 8. real time stamps (tag 2), status words, value time
ents = [
    entry(O(1, 0, 96, 50, 1, 1), ("o", b"LGZ")),
    entry(O(1, 0, 96, 1, 0, 255), ("o", LGZ_ID)),
    entry(O(1, 0, 1, 8, 0, 255), ("u", 777, 8), -1, WH, status=0x00060202, vtime=(2, 1760000000)),
    entry(O(1, 0, 16, 7, 0, 255), ("i", 12, 4), 0, W, status=0x00060202, vtime=(2, 1760000000)),
]
case("TIMESTAMP", frame(LGZ_ID, ents, (2, 1760000000)), meterid="LGZ" + hexid(LGZ_ID), act=12.0, sum=77.7,
     time=1760000000)

# 9. long frame: many values, 48 byte signature per value, 64 byte list signature, > 600 bytes
ents = [
    entry(O(129, 129, 199, 130, 3, 255), ("o", b"ISK")),
    entry(O(1, 0, 0, 0, 9, 255), ("o", ISK_ID)),
    entry(O(1, 0, 1, 8, 0, 255), ("u", 167351414, 8), -1, WH, sig=bytes(range(48))),
    entry(O(1, 0, 16, 7, 0, 255), ("u", 429, 4), 0, W, sig=bytes(range(48))),
]
for i in range(20):
    ents.append(entry(O(1, 0, 80 + i % 8, 7, i, 255), ("i", i * 100 - 500, 4), -1, W, sig=bytes([i]) * 8))
ents.append(entry(O(129, 129, 199, 130, 5, 255), ("o", bytes(range(48)))))
case("LONG", frame(ISK_ID, ents, (1, 9), list_sig=bytes(range(64))), meterid="ISK" + hexid(ISK_ID),
     act=429.0, sum=16735141.4, time=9)


# ---- output ---------------------------------------------------------------------------------------
def cbool(d, k):
    return "true" if k in d else "false"


def main():
    out = [
        "// generated by test/tools/sml_gen.py - do not edit",
        "#ifndef SML_SYNTH_H",
        "#define SML_SYNTH_H",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
    ]
    for name, fr, _ in cases:
        out.append("// %d bytes" % len(fr))
        out.append("static const uint8_t SYN_%s[] = {" % name)
        for k in range(0, len(fr), 16):
            out.append("    " + ", ".join("0x%02x" % b for b in fr[k : k + 16]) + ",")
        out.append("};")
    out += [
        "",
        "struct SynthCase",
        "{",
        "    const char *name;",
        "    const uint8_t *data;",
        "    size_t len;",
        "    const char *meterid;",
        "    bool hasActW; double actW;",
        "    bool hasSumWh; double sumWh;",
        "    bool hasExpWh; double expWh;",
        "    bool hasTime; uint32_t sensorTime;",
        "};",
        "",
        "static const SynthCase synthCases[] = {",
    ]
    for name, fr, e in cases:
        out.append(
            '    {"%s", SYN_%s, sizeof(SYN_%s), "%s", %s, %r, %s, %r, %s, %r, %s, %du},'
            % (name, name, name, e["meterid"], cbool(e, "act"), e.get("act", 0.0), cbool(e, "sum"),
               e.get("sum", 0.0), cbool(e, "exp"), e.get("exp", 0.0), cbool(e, "time"), e.get("time", 0))
        )
    out += ["};", "", "#endif", ""]
    open(OUT, "w").write("\n".join(out))
    print("wrote", OUT, len(cases), "cases,", ", ".join("%s=%dB" % (n, len(f)) for n, f, _ in cases))


if __name__ == "__main__":
    main()
