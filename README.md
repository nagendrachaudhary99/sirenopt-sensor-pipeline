# Multi-Sensor Data Pipeline (C++17, Linux)

[![ci](https://github.com/nagendrachaudhary99/sirenopt-sensor-pipeline/actions/workflows/ci.yml/badge.svg)](https://github.com/nagendrachaudhary99/sirenopt-sensor-pipeline/actions/workflows/ci.yml)

Real-time materials-sensing platforms (SirenOpt's is one - instruments watch
plasma emissions during manufacturing and turn them into live material
measurements) depend on a pipeline that acquires from many sensors at once,
validates every frame, flags anomalies live, and never silently drops or
corrupts a reading. This is a host-runnable version of that pipeline shape:
multiple sensors emit framed binary readings, one lock-free SPSC ring per
sensor feeds a processing thread, and validated frames land in a binary log
that an independent Python tool re-analyzes.

Built by Nagendra Chaudhary. Architecture, the concurrency bug the stress
test caught, tradeoffs, failure handling, and the scaling path are in
[DESIGN.md](DESIGN.md).

## Layout

- `src/frame.hpp` - 50-byte wire frame: magic, sensor id, sequence,
  timestamp, 32-byte payload, CRC-16/CCITT-FALSE trailer.
- `src/ring_buffer.hpp` - single-producer/single-consumer lock-free ring;
  one per sensor (a shared SPSC ring across producers is a data race - see
  DESIGN.md), no mutex on the hot path.
- `src/processor.hpp` - per-sensor validation (CRC, magic, sequence gaps,
  duplicates, timestamp regression) plus Welford rolling mean/variance and
  a 4-sigma anomaly flag.
- `src/storage.hpp` - append-only binary log writer.
- `app/main.cpp` - demo: N simulated sensors (one injects a spike),
  processing, counters, backpressure metrics, log write.
- `tools/analyze_log.py` - independent Python re-implementation of frame
  validation + per-sensor stats, used to cross-check the C++ side.
- `tests/test_main.cpp` - unit tests (CRC known vector 0x29B1 for
  "123456789", frame round-trip + tamper detection, ring
  wraparound/full/empty, processor validation rules, stats + anomaly flag).

## Build, test, run

```sh
mkdir build && cd build
cmake .. && make
ctest --output-on-failure
./pipeline 4 2000 /tmp/pipeline.binlog          # sensors=4, 2000 frames each
python3 ../tools/analyze_log.py /tmp/pipeline.binlog
./pipeline 8 20000 /tmp/stress.binlog 0         # full speed: exercises backpressure
```

Verified output (this machine, g++ 11.4, C++17):

```
ok=8000 bad_crc=0 seq_gap=0 duplicate=0 ts_regression=0 anomalies=4
sensor 0: mean=25.021 stddev=0.843   ...   (one injected spike flagged per sensor)
records=8000 rejected=0               # Python cross-check agrees exactly
```

Full-speed stress (8 sensors x 20,000 frames, producer sleep 0):

```
ok=160000 seq_gap=0 duplicate=0 ts_regression=0 anomalies=12
push_stalls=2210729                   # backpressure engaged; zero frames lost
records=160000 rejected=0             # Python cross-check agrees exactly
```

Both runs are asserted in CI on every push.

## What is real vs simulated

- REAL: frame format, CRC-16, per-sensor SPSC rings, validation rules,
  Welford stats, anomaly flagging, backpressure policy + metrics, storage
  format, unit tests, concurrency stress test, Python cross-check.
- SIMULATED: the sensors themselves (std::mt19937 normal signal around a
  per-sensor mean, one injected +30 spike each). No real hardware is
  attached; the acquisition boundary is the `Frame` producer loop, which is
  where a real driver (Serial/Ethernet/USB) plugs in.

## Source

- SirenOpt (the product shape this pipeline models): https://www.sirenopt.com/
