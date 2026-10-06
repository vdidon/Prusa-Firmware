//! @file
//! @brief CRC32 used by the EEPROM backup, kept apart to be tested on the host

#pragma once
#include <stdint.h>

//! @brief Calculate CRC32 (reflected polynomial 0xEDB88320) with byte-by-byte updates
//! @details Bitwise variant: no 1KB lookup table in flash. Only used on backup/restore/verify
//! (4KB of data), so the extra cycles are irrelevant.
static uint32_t __attribute__((noinline)) crc32_update(uint32_t crc, uint8_t data) {
    crc ^= data;
    for (uint8_t i = 0; i < 8; i++) {
        if (crc & 1) {
            crc = (crc >> 1) ^ 0xEDB88320UL;
        } else {
            crc >>= 1;
        }
    }
    return crc;
}
