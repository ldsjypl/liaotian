// time.h - monotonic and wall-clock helpers.
#pragma once

#include <cstdint>
#include <ctime>

namespace lt {

inline uint64_t mono_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

inline int64_t wall_sec() { return static_cast<int64_t>(::time(nullptr)); }

}  // namespace lt
