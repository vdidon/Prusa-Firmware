/**
 * @file
 * @brief nozzle_category_from_um() of Firmware/preheat_data.h, which picks the preheat column.
 */

#include "catch2/catch_test_macros.hpp"
#include "preheat_data.h"

TEST_CASE("nozzle_category_from_um sorts the nozzle diameters", "[preheat]") {
    CHECK(nozzle_category_from_um(250) == NozzleCategory::Default);
    CHECK(nozzle_category_from_um(400) == NozzleCategory::Default);
    CHECK(nozzle_category_from_um(500) == NozzleCategory::Default);
    CHECK(nozzle_category_from_um(599) == NozzleCategory::Default);
    CHECK(nozzle_category_from_um(600) == NozzleCategory::Dia060);
    CHECK(nozzle_category_from_um(700) == NozzleCategory::Dia060);
    CHECK(nozzle_category_from_um(799) == NozzleCategory::Dia060);
    CHECK(nozzle_category_from_um(800) == NozzleCategory::Dia080);
    CHECK(nozzle_category_from_um(1000) == NozzleCategory::Dia080);
}

TEST_CASE("nozzle_category_from_um keeps the default temperatures without a diameter", "[preheat]") {
    CHECK(nozzle_category_from_um(0) == NozzleCategory::Default);
    // A blank EEPROM word reads 0xFFFF: it must not select the 0.8 mm temperatures.
    CHECK(nozzle_category_from_um(0xFFFF) == NozzleCategory::Default);
}
