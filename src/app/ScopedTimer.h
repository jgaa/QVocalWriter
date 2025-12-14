#pragma once

#include <chrono>

class ScopedTimer {
public:
    ScopedTimer() = default;

    // in seconds, with millisecond precision
    double elapsed() const noexcept {
        const auto end_time =  std::chrono::steady_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time_).count();
        return static_cast<double>(duration) / 1000.0;
    }

private:
    const std::chrono::steady_clock::time_point start_time_{std::chrono::steady_clock::now()};

};
