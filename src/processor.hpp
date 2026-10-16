#pragma once
#include <cstdint>
#include <cmath>
#include <unordered_map>
#include "frame.hpp"

// Per-sensor stream validation + rolling statistics.
// Validation: CRC, magic, sequence gaps, duplicates, timestamp regressions.
// Stats: Welford running mean/variance over the payload's first 4 bytes
// interpreted as a float reading, plus a z-score anomaly flag.
struct SensorState {
    uint32_t expected_seq = 0;
    bool seen_any = false;
    uint64_t last_ts_us = 0;
    // Welford
    double count = 0.0;
    double mean = 0.0;
    double m2 = 0.0;
};

struct Counters {
    uint64_t ok = 0;
    uint64_t bad_crc = 0;
    uint64_t bad_magic = 0;
    uint64_t seq_gap = 0;
    uint64_t duplicate = 0;
    uint64_t ts_regression = 0;
    uint64_t anomalies = 0;
};

class Processor {
public:
    // Returns true when the frame is valid and was ingested.
    bool ingest(const Frame& f, Counters& c) {
        if (f.magic != Frame::kMagic) { ++c.bad_magic; return false; }
        if (!f.valid()) { ++c.bad_crc; return false; }

        SensorState& s = states_[f.sensor_id];
        if (s.seen_any) {
            if (f.seq == s.expected_seq - 1) { ++c.duplicate; return false; }
            if (f.seq != s.expected_seq) ++c.seq_gap;
            if (f.timestamp_us < s.last_ts_us) { ++c.ts_regression; return false; }
        }
        s.seen_any = true;
        s.expected_seq = f.seq + 1;
        s.last_ts_us = f.timestamp_us;

        float reading;
        std::memcpy(&reading, f.payload.data(), 4);
        s.count += 1.0;
        const double delta = reading - s.mean;
        s.mean += delta / s.count;
        s.m2 += delta * (reading - s.mean);
        if (s.count >= 30.0) {  // need history before flagging
            const double var = s.m2 / (s.count - 1.0);
            const double sd = var > 0.0 ? std::sqrt(var) : 0.0;
            if (sd > 0.0 && std::fabs(reading - s.mean) > 4.0 * sd) ++c.anomalies;
        }
        ++c.ok;
        return true;
    }

    double mean(uint16_t sensor) const {
        auto it = states_.find(sensor);
        return it == states_.end() ? 0.0 : it->second.mean;
    }

    double stddev(uint16_t sensor) const {
        auto it = states_.find(sensor);
        if (it == states_.end() || it->second.count < 2.0) return 0.0;
        return std::sqrt(it->second.m2 / (it->second.count - 1.0));
    }

private:
    std::unordered_map<uint16_t, SensorState> states_;
};
