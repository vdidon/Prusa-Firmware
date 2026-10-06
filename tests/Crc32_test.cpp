/**
 * @file
 * @brief crc32_update() of Firmware/crc32.h, used by the EEPROM backup files.
 */

#include "catch2/catch_test_macros.hpp"
#include "crc32.h"
#include <cstring>
#include <random>

namespace {

uint32_t crc32_of(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i)
        crc = crc32_update(crc, data[i]);
    return ~crc;
}

uint32_t crc32_of(const char *s) {
    return crc32_of(reinterpret_cast<const uint8_t *>(s), strlen(s));
}

// Table version, as computed by the firmware before the bitwise rewrite: the backups already on the
// SD cards were checked with it.
uint32_t crc32_table(const uint8_t *data, size_t len) {
    static uint32_t table[256];
    if (!table[1]) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (c >> 1) ^ 0xEDB88320UL : c >> 1;
            table[i] = c;
        }
    }
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

} // namespace

TEST_CASE("crc32_update gives the standard CRC-32", "[crc32]") {
    CHECK(crc32_of("") == 0x00000000UL);
    CHECK(crc32_of("a") == 0xE8B7BE43UL);
    CHECK(crc32_of("123456789") == 0xCBF43926UL);
    CHECK(crc32_of("The quick brown fox jumps over the lazy dog") == 0x414FA339UL);
}

TEST_CASE("crc32_update matches the former table CRC on an EEPROM image", "[crc32]") {
    uint8_t eeprom[4096];
    std::mt19937 rng(4096);
    for (auto &b : eeprom)
        b = uint8_t(rng());
    CHECK(crc32_of(eeprom, sizeof(eeprom)) == crc32_table(eeprom, sizeof(eeprom)));

    memset(eeprom, 0xFF, sizeof(eeprom)); // blank EEPROM
    CHECK(crc32_of(eeprom, sizeof(eeprom)) == crc32_table(eeprom, sizeof(eeprom)));
}

TEST_CASE("crc32_update detects a single bit flip", "[crc32]") {
    uint8_t data[256];
    for (size_t i = 0; i < sizeof(data); ++i)
        data[i] = uint8_t(i);
    const uint32_t ref = crc32_of(data, sizeof(data));
    for (size_t i = 0; i < sizeof(data); i += 17) {
        data[i] ^= 0x10;
        CHECK(crc32_of(data, sizeof(data)) != ref);
        data[i] ^= 0x10;
    }
}
