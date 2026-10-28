#pragma once
#include <cstdint>
struct pico_unique_board_id_t { uint8_t id[8]{}; };
inline void pico_get_unique_board_id(pico_unique_board_id_t *out) { *out = {}; }
