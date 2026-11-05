// SPDX-License-Identifier: MIT
// Portable AES-128 block encryption (FIPS-197). Used by the Switch 2 pairing
// confirm step on targets where the platform crypto API differs (ESP-IDF v6
// moved to the PSA-only mbedTLS 4 API). One block, encrypt only, no tables
// beyond the S-box: pairing runs this once per session.
#pragma once
#include <cstdint>

void aes128_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
