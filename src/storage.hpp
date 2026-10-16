#pragma once
#include <cstdio>
#include <string>
#include "frame.hpp"

// Appends validated wire frames to a binary log; tools/analyze_log.py reads it.
class LogWriter {
public:
    explicit LogWriter(const std::string& path) : f_(std::fopen(path.c_str(), "wb")) {}
    ~LogWriter() { if (f_) std::fclose(f_); }
    bool ok() const { return f_ != nullptr; }

    bool append(const Frame& fr) {
        if (!f_) return false;
        uint8_t buf[Frame::kWireBytes];
        size_t n = fr.serialize(buf);
        return std::fwrite(buf, 1, n, f_) == n;
    }

    void flush() { if (f_) std::fflush(f_); }

private:
    std::FILE* f_;
};
