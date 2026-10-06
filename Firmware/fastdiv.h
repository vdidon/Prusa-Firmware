//! @file
//! @brief Linear Advance divisor, kept apart to be tested on the host

#pragma once
#include <stdint.h>

// @wavexx: fast uint16_t division for small dividends<5
//          q/3 based on "Hacker's delight" formula
static inline __attribute__((always_inline)) uint16_t fastdiv(uint16_t q, uint8_t d)
{
    if(d != 3) return q >> (d / 2);
    else return (uint16_t)(((uint32_t)0xAAAB * q) >> 16) >> 1; // avoid a 17-step 32-bit shift loop
}
