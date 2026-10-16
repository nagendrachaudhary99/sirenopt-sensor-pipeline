#include <cstdio>
#include <cstring>
#include <cmath>
#include "../src/crc.hpp"
#include "../src/frame.hpp"
#include "../src/processor.hpp"
#include "../src/ring_buffer.hpp"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static Frame make_frame(uint16_t sensor, uint32_t seq, uint64_t ts, float reading) {
    Frame f;
    f.sensor_id = sensor;
    f.seq = seq;
    f.timestamp_us = ts;
    std::memcpy(f.payload.data(), &reading, 4);
    f.seal();
    return f;
}

void test_crc_known_vector() {
    // CRC-16/CCITT-FALSE of "123456789" is 0x29B1.
    const char* s = "123456789";
    CHECK(crc16_ccitt(reinterpret_cast<const uint8_t*>(s), 9) == 0x29B1);
}

void test_frame_roundtrip_and_tamper() {
    Frame f = make_frame(3, 42, 1000000, 25.5f);
    uint8_t buf[Frame::kWireBytes];
    CHECK(f.serialize(buf) == Frame::kWireBytes);
    Frame g;
    CHECK(Frame::deserialize(buf, Frame::kWireBytes, g));
    CHECK(g.valid());
    CHECK(g.sensor_id == 3 && g.seq == 42 && g.timestamp_us == 1000000);
    float r; std::memcpy(&r, g.payload.data(), 4);
    CHECK(std::fabs(r - 25.5f) < 1e-6);
    buf[20] ^= 0xFF;  // corrupt one payload byte
    Frame h;
    CHECK(Frame::deserialize(buf, Frame::kWireBytes, h));
    CHECK(!h.valid());
}

void test_ring_wraparound() {
    SpscRing<int> ring(8);  // capacity 8, usable 7
    for (int i = 0; i < 7; ++i) CHECK(ring.try_push(i));
    CHECK(!ring.try_push(99));  // full
    int v = -1;
    for (int i = 0; i < 7; ++i) { CHECK(ring.try_pop(v)); CHECK(v == i); }
    CHECK(!ring.try_pop(v));  // empty
    for (int i = 100; i < 107; ++i) CHECK(ring.try_push(i));  // wrap
    for (int i = 100; i < 107; ++i) { CHECK(ring.try_pop(v)); CHECK(v == i); }
}

void test_processor_validation() {
    Processor p;
    Counters c{};
    CHECK(p.ingest(make_frame(0, 0, 1000, 25.0f), c));
    CHECK(p.ingest(make_frame(0, 1, 2000, 25.1f), c));
    CHECK(!p.ingest(make_frame(0, 1, 3000, 25.1f), c));   // duplicate seq
    CHECK(c.duplicate == 1);
    CHECK(p.ingest(make_frame(0, 5, 4000, 25.2f), c));    // gap 2..4
    CHECK(c.seq_gap == 1);
    CHECK(!p.ingest(make_frame(0, 6, 500, 25.0f), c));    // ts regression
    CHECK(c.ts_regression == 1);
    Frame bad = make_frame(0, 7, 6000, 25.0f);
    bad.crc ^= 0x01;
    CHECK(!p.ingest(bad, c));
    CHECK(c.bad_crc == 1);
    Frame badmagic = make_frame(0, 7, 6000, 25.0f);
    badmagic.magic = 0xDEAD;
    CHECK(!p.ingest(badmagic, c));
    CHECK(c.bad_magic == 1);
    CHECK(c.ok == 3);
}

void test_processor_stats_and_anomaly() {
    Processor p;
    Counters c{};
    uint64_t ts = 1000;
    for (uint32_t i = 0; i < 100; ++i)  // stable signal ~10.0
        CHECK(p.ingest(make_frame(1, i, ts += 100, 10.0f + (i % 3) * 0.01f), c));
    CHECK(std::fabs(p.mean(1) - 10.01) < 0.01);
    const uint64_t anomalies_before = c.anomalies;
    CHECK(p.ingest(make_frame(1, 100, ts += 100, 90.0f), c));  // huge spike
    CHECK(c.anomalies == anomalies_before + 1);
}

int main() {
    test_crc_known_vector();
    test_frame_roundtrip_and_tamper();
    test_ring_wraparound();
    test_processor_validation();
    test_processor_stats_and_anomaly();
    if (failures == 0) std::printf("ALL TESTS PASSED\n");
    else std::printf("%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
