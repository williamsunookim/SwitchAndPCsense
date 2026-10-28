#pragma once
#include <cstdint>
extern uint32_t fake_now;
inline uint32_t get_absolute_time() { return fake_now; }
inline uint32_t to_ms_since_boot(uint32_t value) { return value; }

inline uint64_t time_us_64() { return static_cast<uint64_t>(fake_now) * 1000; }
