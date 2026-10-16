# Multi-Sensor Data Pipeline (C++17, Linux)

A host-runnable version of the acquisition → processing → storage pipeline shape
behind a real-time materials-sensing platform: multiple sensors emit framed
binary readings, a lock-free SPSC ring feeds a single processing thread, and
validated frames land in a binary log that a Python tool re-analyzes.

Built by Nagendra Chaudhary as a hands-on match to SirenOpt's
Software Engineer (Early Career) – C++ & Systems posting
(Greenhouse job 4452002009): modern C++17 on Linux, concurrency, CRC-checked
sensor frames, sequence/timestamp validation, rolling statistics with anomaly
flagging, CMake + ctest, and Python for analysis.

## Layout

- `src/frame.hpp` – 50-byte wire frame: magic, sensor id, sequence, timestamp,
  32-byte payload, CRC-16/CCITT-FALSE trailer.
- `src/ring_buffer.hpp` – single-producer/single-consumer lock-free ring
  (acquisition threads push, processing thread pops; no mutex on the hot path).
- `src/processor.hpp` – per-sensor validation (CRC, magic, sequence gaps,
  duplicates, timestamp regression) plus Welford rolling mean/variance and a
  4-sigma anomaly flag.
- `src/storage.hpp` – binary log writer.
- `app/main.cpp` – demo: N simulated sensors (one injects a spike), processing,
  counters, log write.
- `tools/analyze_log.py` – independent Python re-implementation of the frame
  validation + per-sensor stats, used to cross-check the C++ side.
- `tests/test_main.cpp` – unit tests (CRC known vector 0x29B1 for "123456789",
  frame round-trip + tamper detection, ring wraparound/full/empty, processor
  validation rules, stats + anomaly flag).

## Build, test, run

```sh
mkdir build && cd build
cmake .. && make
ctest --output-on-failure
./pipeline 4 2000 /tmp/pipeline.binlog      # sensors=4, 2000 frames each
python3 ../tools/analyze_log.py /tmp/pipeline.binlog
```

Verified output (this machine, g++ 11.4, C++17):

```
ok=8000 bad_crc=0 seq_gap=0 duplicate=0 ts_regression=0 anomalies=4
sensor 0: mean=25.021 stddev=0.843   ...   (one injected spike flagged per sensor)
records=8000 rejected=0               # Python cross-check agrees exactly
```

## What is real vs simulated

- REAL: frame format, CRC-16, SPSC ring, validation rules, Welford stats,
  anomaly flagging, storage format, tests, Python analysis.
- SIMULATED: the sensors themselves (std::mt19937 normal signal around a
  per-sensor mean, one injected +30 spike each). No real hardware is attached;
  the acquisition boundary is the `Frame` producer loop, which is where a real
  driver (Serial/Ethernet/USB) would plug in.
