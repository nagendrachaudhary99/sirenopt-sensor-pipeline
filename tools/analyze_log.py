#!/usr/bin/env python3
"""Read a pipeline binary log and print per-sensor statistics.

Log format (50 bytes per record, little-endian):
  magic(2) | sensor_id(2) | seq(4) | timestamp_us(8) | payload(32) | crc16(2)
Payload's first 4 bytes are a float32 reading. This is the same validation the
C++ processor does, re-implemented in Python as a cross-check / analysis tool.
"""
import struct
import sys

WIRE = 50
MAGIC = 0x51E0


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def main(path: str) -> int:
    raw = open(path, "rb").read()
    if len(raw) % WIRE:
        print(f"corrupt log: {len(raw)} bytes is not a multiple of {WIRE}")
        return 1
    stats = {}
    bad = 0
    for off in range(0, len(raw), WIRE):
        rec = raw[off:off + WIRE]
        magic, sensor, seq, ts = struct.unpack_from("<HHIQ", rec, 0)
        crc_stored = struct.unpack_from("<H", rec, WIRE - 2)[0]
        if magic != MAGIC or crc16_ccitt(rec[:WIRE - 2]) != crc_stored:
            bad += 1
            continue
        (reading,) = struct.unpack_from("<f", rec, 16)
        s = stats.setdefault(sensor, {"n": 0, "sum": 0.0, "min": reading, "max": reading,
                                      "first_ts": ts, "last_ts": ts})
        s["n"] += 1
        s["sum"] += reading
        s["min"] = min(s["min"], reading)
        s["max"] = max(s["max"], reading)
        s["last_ts"] = ts
    for sensor in sorted(stats):
        s = stats[sensor]
        dur_s = (s["last_ts"] - s["first_ts"]) / 1e6
        print(f"sensor {sensor}: n={s['n']} mean={s['sum']/s['n']:.3f} "
              f"min={s['min']:.3f} max={s['max']:.3f} span={dur_s:.2f}s")
    print(f"records={len(raw)//WIRE} rejected={bad}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "pipeline.binlog"))
