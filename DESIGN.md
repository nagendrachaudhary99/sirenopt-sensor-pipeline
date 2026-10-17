# Design: multi-sensor acquisition -> processing -> storage pipeline

## Problem

Real-time materials-sensing platforms (SirenOpt's is one: instruments watch
plasma emissions during manufacturing and turn them into live material
measurements) live or die on their data pipeline. Sensors emit framed binary
readings continuously; the host has to acquire from many sensors at once,
validate every frame, compute live statistics, flag anomalies as they
happen, and store everything for later analysis. A pipeline that corrupts a
frame, silently drops a reading, or misses a drifting sensor is worse than
no pipeline, because downstream decisions trust it.

## What "solved" means here

Solved: a working C++17 pipeline from framed sensor bytes to validated,
anomaly-flagged, stored records - every frame accounted for in the counters,
a backpressure policy that blocks rather than loses data (with the stall
count surfaced), and an independent Python re-implementation that
re-validates the stored binary log and must agree exactly. The concurrency
correctness claim is stress-tested in CI: 8 full-speed producers, 160k
frames, zero sequence gaps, zero duplicates, zero timestamp regressions.

Deliberately not solved (labeled, boundary named): real hardware drivers
(the producer loop is the boundary where a Serial/Ethernet/USB driver plugs
in), fan-out to multiple consumers, and persistence beyond the append-only
binary log.

## Architecture

```
 sensor 0 --+   (one SPSC ring per sensor;        +------------------+
 sensor 1 --+    lock-free single-producer        | Processor        |
   ...     +-->  per sensor, no mutex on the      |  per-sensor:     |
 sensor N --+    hot path)                        |  magic + CRC-16  |
                +-------------------+             |  seq gap / dup   |
                | SpscRing<Frame>   |  round-     |  ts regression   |
                |  x N (4096 deep)  |  robin ---> |  Welford mean/   |
                +-------------------+  drain      |  var, 4-sigma    |
                       ^                            |  anomaly flag    |
              full -> producer BLOCKS               +--------+---------+
              (yield + retry, stalls                       v
               counted, never drop)               +------------------+
                                                  | LogWriter        |
                                                  | append-only      |
                                                  | binary log       |
                                                  +--------+---------+
                                                           v
                                        tools/analyze_log.py
                                        (independent re-validation:
                                         must agree exactly)
```

Components: `frame.hpp` (50-byte wire frame: magic, sensor id, sequence,
timestamp, 32-byte payload, CRC-16/CCITT-FALSE trailer), `ring_buffer.hpp`
(SPSC lock-free ring), `processor.hpp` (validation + rolling stats),
`storage.hpp` (binary log), `app/main.cpp` (demo wiring + counters),
`tools/analyze_log.py` (independent cross-check), `tests/` (CTest suite).

## Key tradeoffs

- **One SPSC ring per sensor, not one shared queue.** An SPSC ring is only
  safe with exactly one producer. The first cut shared one ring across all
  acquisition threads; a full-speed stress run exposed the race as sequence
  gaps (~5% of frames lost). Per-sensor rings keep the hot path lock-free,
  give natural per-sensor isolation, and the consumer drains them
  round-robin so no sensor starves. Considered: an MPMC CAS queue (more
  contention, much subtler) and a mutex-guarded queue (lock on the hot
  path). Both rejected.
- **Block on full ring, never overwrite or drop.** A sensing platform that
  silently loses readings hides exactly the events you monitor for.
  Producers yield and retry; `push_stalls` in the run summary makes the
  backpressure visible (2.2M stalls in the full-speed stress run, zero
  frames lost). Ring depth (4096) is the memory-vs-burst-tolerance knob.
- **Fixed 50-byte frames with magic + CRC-16 trailer.** Corruption and
  stream misalignment are detected per frame at a fixed cost; no length
  parsing ambiguity. Cost: payload fixed at 32 bytes.
- **Welford rolling statistics, not stored history.** O(1) memory per
  sensor, numerically stable online mean/variance, 4-sigma z-score anomaly
  flag after a 30-sample warmup. Tradeoff: z-scores assume a roughly normal
  signal, and there are no exact quantiles - fine for drift/spike flagging.
- **Append-only binary log + independent re-analysis.** Write throughput
  and exact replay beat queryability at this stage. The Python analyzer is
  a deliberate second implementation: two codebases agreeing on 160k
  records is a stronger correctness signal than either alone.

## Failure handling

- Corrupt frame (bad magic or CRC): rejected, counted (`bad_magic`,
  `bad_crc`).
- Lost frame in transit: sequence gap counted, frame accepted (loss is
  visible, not hidden).
- Duplicate delivery: rejected, counted (`duplicate`).
- Out-of-order clock: timestamp regression rejected, counted.
- Ring full: producer blocks (policy above); stalls counted.
- Log open failure: clean exit with an error, no half-written state.
- Anomaly warmup: no flags before 30 samples, so early noise is not
  reported as drift.

## Scaling path

- **More sensors:** rings and per-sensor state scale linearly; the single
  consumer becomes the ceiling. Partition sensors across a few consumer
  threads (each owning a disjoint ring set) - the Processor is already
  per-sensor keyed, so sharding is mechanical.
- **Faster sensors:** pin threads, batch pops, move the log to a memory
  mapped file or double-buffered writer.
- **Real hardware:** replace the producer loop with the driver read path;
  everything downstream of `Frame` is unchanged.
- **Persistence/query:** roll the binlog into Parquet/object storage on a
  timer; keep the analyzer as the verification gate.

## Honest limits

- Sensors are simulated (std::mt19937 normal signal, one injected spike
  each). No real hardware is attached.
- Single consumer thread; round-robin fairness under sustained full-speed
  bursts is adequate here but unquantified beyond the stress run.
- Z-score flagging assumes roughly normal readings per sensor.
