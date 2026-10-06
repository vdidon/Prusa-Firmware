/**
 * @file
 * @brief fastdiv() of Firmware/fastdiv.h, the Linear Advance divisor of the stepper ISR.
 */

#include "catch2/catch_test_macros.hpp"
#include "fastdiv.h"

TEST_CASE("fastdiv divides exactly by 1 to 4 over the whole uint16_t range", "[fastdiv]") {
    // stepper.cpp calls it with ticks + 1 <= 4 and with step loops <= 4.
    for (uint8_t d = 1; d <= 4; ++d) {
        uint32_t wrong = 0;
        for (uint32_t q = 0; q <= 0xFFFF; ++q) {
            if (fastdiv(uint16_t(q), d) != q / d) {
                if (!wrong)
                    UNSCOPED_INFO("first error: q=" << q << " d=" << int(d));
                ++wrong;
            }
        }
        CHECK(wrong == 0);
    }
}
