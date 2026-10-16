#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

#include "../src/frame.hpp"
#include "../src/processor.hpp"
#include "../src/ring_buffer.hpp"
#include "../src/storage.hpp"

// Simulates N sensors pushing framed readings through a lock-free ring into a
// single processing thread that validates, stats, flags and logs - the same
// shape as a real acquisition -> processing -> storage pipeline.
namespace {
uint64_t now_us() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
}  // namespace

int main(int argc, char** argv) {
    const int sensors = argc > 1 ? std::atoi(argv[1]) : 4;
    const int frames_per_sensor = argc > 2 ? std::atoi(argv[2]) : 2000;
    const char* log_path = argc > 3 ? argv[3] : "pipeline.binlog";

    SpscRing<Frame> ring(1 << 12);
    Processor proc;
    Counters counters{};
    LogWriter log(log_path);
    if (!log.ok()) { std::fprintf(stderr, "cannot open %s\n", log_path); return 1; }

    std::atomic<int> dropped{0};
    std::atomic<bool> done{false};

    std::vector<std::thread> producers;
    for (int s = 0; s < sensors; ++s) {
        producers.emplace_back([&, s] {
            std::mt19937 rng(1234 + s);
            std::normal_distribution<float> signal(25.0f + s, 0.5f);
            for (uint32_t i = 0; i < static_cast<uint32_t>(frames_per_sensor); ++i) {
                Frame f;
                f.sensor_id = static_cast<uint16_t>(s);
                f.seq = i;
                f.timestamp_us = now_us();
                float reading = signal(rng);
                // inject one spike per sensor to exercise anomaly flagging
                if (i == static_cast<uint32_t>(frames_per_sensor) / 2) reading += 30.0f;
                std::memcpy(f.payload.data(), &reading, 4);
                f.seal();
                while (!ring.try_push(f)) {
                    if (done.load(std::memory_order_acquire)) return;
                    std::this_thread::yield();
                }
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        });
    }

    std::thread consumer([&] {
        Frame f;
        int idle = 0;
        while (!done.load(std::memory_order_acquire) || idle < 1000) {
            if (ring.try_pop(f)) {
                idle = 0;
                if (proc.ingest(f, counters)) log.append(f);
            } else {
                ++idle;
                std::this_thread::yield();
            }
        }
    });

    for (auto& t : producers) t.join();
    done.store(true, std::memory_order_release);
    consumer.join();
    log.flush();

    std::printf("sensors=%d frames/sensor=%d\n", sensors, frames_per_sensor);
    std::printf("ok=%llu bad_crc=%llu bad_magic=%llu seq_gap=%llu duplicate=%llu ts_regression=%llu anomalies=%llu\n",
                (unsigned long long)counters.ok, (unsigned long long)counters.bad_crc,
                (unsigned long long)counters.bad_magic, (unsigned long long)counters.seq_gap,
                (unsigned long long)counters.duplicate, (unsigned long long)counters.ts_regression,
                (unsigned long long)counters.anomalies);
    for (int s = 0; s < sensors; ++s)
        std::printf("sensor %d: mean=%.3f stddev=%.3f\n", s, proc.mean(s), proc.stddev(s));
    std::printf("log=%s (%llu valid frames)\n", log_path, (unsigned long long)counters.ok);
    return 0;
}
